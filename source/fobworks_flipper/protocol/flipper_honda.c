#include "flipper_honda.h"
#include "flipper_scratch.h"
#include <string.h>
#include <stdio.h>

/* This Honda RKE layout comes from the uploaded source and has not been
   independently verified. Keep the decoder force-only and prediction disabled
   until the layout and checksum are confirmed. */
bool flipper_decode_honda(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 128 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t te = buf->te_us;
    if(te < 180 || te > 600) return false;
    const uint32_t thr = te + (te >> 1);

    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i += 2) {
        int run = 0;
        while(i + run + 1 < buf->len &&
              buf->durations[i + run] <= thr &&
              buf->durations[i + run + 1] <= thr)
            run += 2;
        if(run / 2 >= 8) { ds = i + run; break; }
    }
    if(ds < 0 || ds + 128 > buf->len) return false;

    uint8_t bytes[8] = {0};
    for(int b = 0; b < 64; b++) {
        int idx = ds + 2 * b;
        uint32_t hi = buf->durations[idx];
        uint32_t lo = buf->durations[idx + 1];
        if(hi > te * 3 || lo > te * 3) return false;
        if(hi > thr) bytes[b >> 3] |= (uint8_t)(1u << (b & 7));
    }

    uint8_t sum = 0;
    for(int i = 0; i < 7; i++) sum = (uint8_t)(sum + bytes[i]);
    if(sum != bytes[7]) return false;

    uint32_t serial = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                      ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    uint32_t counter = (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8);
    if(serial == 0 && counter == 0) return false;

    r->addr = serial;
    r->cnt = counter;
    r->hop = counter;
    r->btn = bytes[6];
    r->rolling = true;
    r->te_us = te;
    r->bits = 64;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    r->predict_lo = 0;
    r->predict_hi = 0;
    strncpy(r->proto, "Honda-RKE?", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "Provisional layout/checksum; unverified; prediction disabled");
    return true;
}

static uint8_t hk_crc8(const uint8_t* d, int n) {
    uint8_t c = 0x00;
    for(int i = 0; i < n; i++) {
        c ^= d[i];
        for(int b = 0; b < 8; b++)
            c = (uint8_t)((c & 0x80) ? ((c << 1) ^ 0x2F) : (c << 1));
    }
    return c;
}

bool flipper_decode_honda_kr5(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 100 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t te = buf->te_us;
    if(te < 40 || te > 140) {
        uint32_t mn = 0xFFFFFFFFu;
        for(int i = 0; i < buf->len; i++) {
            uint32_t d = buf->durations[i];
            if(d >= 40 && d < mn) mn = d;
        }
        if(mn == 0xFFFFFFFFu) return false;
        te = mn;
    }
    if(te < 40 || te > 140) return false;

    /* Stay within the shared 2.5 KiB scratch arena. Truncate oversized
       expansions; accept a KR5 frame only when its preamble, payload, and CRC
       all fit in the available data. */
    enum { HK_LVL = FLIPPER_SCRATCH_A / 2, HK_BITS = FLIPPER_SCRATCH_A / 2 };
    uint8_t* lvl = flipper_scratch_a(0, HK_LVL);
    if(!lvl) return false;
    int ll = 0;
    for(int i = 0; i < buf->len && ll < HK_LVL - 6; i++) {
        uint32_t n = (buf->durations[i] + te / 2) / te;
        if(n < 1) n = 1;
        if(n > 6) n = 6;
        uint8_t pol = (i & 1) ? 0 : 1;
        for(uint32_t k = 0; k < n && ll < HK_LVL; k++) lvl[ll++] = pol;
    }

    static const uint8_t PRE[3] = {0xEC, 0x0F, 0x62};
    for(int ph = 0; ph < 2; ph++) {
        uint8_t* bits = flipper_scratch_a(HK_LVL, HK_BITS);
        if(!bits) return false;
        int nb = 0;
        int i = ph;
        while(i + 1 < ll && nb < HK_BITS) {
            uint8_t a = lvl[i], b = lvl[i + 1];
            if(a != b) {
                bits[nb++] = (uint8_t)(a == 1 ? 1 : 0);
                i += 2;
            } else {
                nb = 0;
                i += 1;
            }
        }
        for(int s = 0; s + 16 + 120 <= nb; s++) {
            int match = 1;
            for(int k = 0; k < 24; k++) {
                int want = (PRE[k >> 3] >> (7 - (k & 7))) & 1;
                if(bits[s + k] != want) { match = 0; break; }
            }
            if(!match) continue;
            uint8_t B[15];
            int base = s + 16;
            for(int by = 0; by < 15; by++) {
                uint8_t v = 0;
                for(int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | bits[base + by * 8 + k]);
                B[by] = v;
            }
            if(hk_crc8(B, 14) != B[14]) continue;
            uint32_t dev = ((uint32_t)B[2] << 24) | ((uint32_t)B[3] << 16) |
                           ((uint32_t)B[4] << 8) | B[5];
            if(dev == 0) continue;
            uint8_t event = B[6];
            uint32_t counter = ((uint32_t)B[7] << 16) | ((uint32_t)B[8] << 8) | B[9];
            uint32_t rolling = ((uint32_t)B[10] << 24) | ((uint32_t)B[11] << 16) |
                               ((uint32_t)B[12] << 8) | B[13];
            r->addr = dev; r->cnt = counter; r->hop = rolling; r->btn = event;
            r->rolling = true; r->te_us = te; r->bits = 120; r->freq_mhz = buf->freq_mhz;
            r->predict_window = 256; r->predict_lo = (counter + 1) & 0xFFFFFF;
            r->predict_hi = (counter + 8) & 0xFFFFFF;
            strncpy(r->proto, "Honda-KR5V2X", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "CRC OK  evt=0x%02X cnt=%lu", (unsigned)event, (unsigned long)counter);
            return true;
        }
    }
    return false;
}