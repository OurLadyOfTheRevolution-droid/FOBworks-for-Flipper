#include "flipper_vehrke.h"
#include <string.h>
#include <stdio.h>

/* Provisional generic vehicle-RKE extractor; see the header for its limits.
   Assumed frame: at least eight equal-width preamble pairs (each half no more
   than about 1.5T), followed by 64 bits. A HIGH half above 1.5T represents 1.
   The guessed fields are [serial 32][counter 16][command 8][checksum 8].
   The checksum can reject some noise, but it does not validate these field
   positions or identify a real protocol. Keep callers force-only.
 */
bool vehrke_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* r,
                   const VehRkeSpec* s) {
    if(!buf || !r || !s) return false;
    uint32_t te = buf->te_us;
    if(te < s->te_min || te > s->te_max) return false;
    if(buf->len < 128) return false;

    const uint32_t thr = te + (te >> 1);   /* ~1.5T */

    /* Find a preamble run with at least eight pairs. */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i += 2) {
        int run = 0;
        while(i + run + 1 < buf->len &&
              buf->durations[i + run]     <= thr &&
              buf->durations[i + run + 1] <= thr)
            run += 2;
        if(run / 2 >= 8) { ds = i + run; break; }
    }
    if(ds < 0 || ds + 128 > buf->len) return false;

    uint8_t bytes[8];
    memset(bytes, 0, sizeof(bytes));
    for(int b = 0; b < 64; b++) {
        int idx = ds + 2 * b;
        if(idx + 1 >= buf->len) return false;
        uint32_t hi = buf->durations[idx];
        uint32_t lo = buf->durations[idx + 1];
        if(hi > te * 3 || lo > te * 3) return false;
        if(hi > thr) {
            if(s->lsb_first) bytes[b >> 3] |= (uint8_t)(1u << (b & 7));
            else             bytes[b >> 3] |= (uint8_t)(0x80 >> (b & 7));
        }
    }

    uint8_t ck = 0;
    if(s->ck == VehCkAdd)
        for(int i = 0; i < 7; i++) ck = (uint8_t)(ck + bytes[i]);
    else
        for(int i = 0; i < 7; i++) ck ^= bytes[i];
    if(ck != bytes[7]) return false;

    uint32_t serial  = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                       ((uint32_t)bytes[2] << 8)  |  (uint32_t)bytes[3];
    uint32_t counter = ((uint32_t)bytes[4] << 8)  |  (uint32_t)bytes[5];
    uint8_t  command = bytes[6];
    if(serial == 0 && counter == 0) return false;

    r->addr = serial;  r->cnt = counter;  r->hop = counter;
    r->btn = command;  r->rolling = true;  r->te_us = te;  r->bits = 64;
    r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
    r->predict_lo = (counter + 1) & 0xFFFF;
    r->predict_hi = (counter + 8) & 0xFFFF;
    strncpy(r->proto, s->proto, sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "provisional  cnt=%lu", (unsigned long)counter);
    return true;
}

/* Nissan (NXP PCF7952-class) is also handled by the provisional extractor. */
bool flipper_decode_nissan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    static const VehRkeSpec spec = { 220, 560, false, VehCkXor, "Nissan-RKE" };
    return vehrke_decode(buf, r, &spec);
}
