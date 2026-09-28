#include "flipper_subaru.h"
#include <string.h>
#include <stdio.h>

/* The Subaru RKE frames covered here are 80-bit OOK Manchester, with half
   symbols near 1013 µs and a sequential counter. The listed application
   coverage is 2004–2011 Impreza, Forester, Legacy, Outback, and Baja.

   Ten-byte frame, MSB first (Manchester 10=1, 01=0):
     B0       sync byte 0x55
     B1..B3   24-bit serial
     B5[3:0] and B6[3:0] repeat the command nibble and must agree
              1=Lock, 2=Unlock, 0xA=Panic, 0xB=Trunk
     B7..B9   20-bit counter: (B7<<12)|(B8<<4)|(B9>>4)
     B9[3:0]  checksum: ((XOR of nibbles B0..B8 and B9 high nibble) + 1) & 0xF

   The start byte, repeated command, and checksum form the parser's Auto gate.
   A decoded sequence exposes the counter for analysis; that alone does not
   establish that a receiver accepts a replay or rollback sequence. */

static uint8_t sub_csum(const uint8_t* p) {
    uint8_t cs = 0;
    for(int i = 0; i < 9; i++) {
        cs ^= (uint8_t)(p[i] & 0x0F);
        cs ^= (uint8_t)((p[i] >> 4) & 0x0F);
    }
    cs ^= (uint8_t)((p[9] >> 4) & 0x0F);
    return (uint8_t)((cs + 1u) & 0x0F);
}

static int sub_inr(uint32_t v, uint32_t ref, uint32_t pct) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d * 100 <= ref * pct;
}

bool flipper_decode_subaru(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    if(cnt < 40) return false;

    for(int si = 8; si < cnt - 40; si++) {
        uint32_t gp = B[si];
        if(gp < 2000 || gp > 7000) continue;          /* sync gap: 2..7 × TE */

        /* Estimate TE from at least 8 preceding pulses in the 600–1400 µs range. */
        uint32_t teSum = 0;
        int pc = 0;
        for(int j = si - 1; j >= 0 && j > si - 20 && pc < 16; j--) {
            uint32_t pw = B[j];
            if(pw < 600 || pw > 1400) { if(pc > 0) break; continue; }
            if(pc == 0) { teSum += pw; pc++; }
            else {
                uint32_t teCur = teSum / (uint32_t)pc;
                if(sub_inr(pw, teCur, 35)) { teSum += pw; pc++; } else break;
            }
        }
        if(pc < 8) continue;
        uint32_t te = teSum / (uint32_t)pc;
        if(te < 600 || te > 1400) continue;
        if(gp < te * 2 || gp > te * 7) continue;

        /* Start decoding after the sync gap. That LOW gap accounts for the first
           LOW half-symbol of the 0x55 start byte, whose MSB is 0. */
        int phase = 1, first_half = 0;
        uint8_t pkt[10];
        int bpos = 0;
        uint8_t cb = 0;
        int bidx = 0;
        bool err = false;
        for(int i = si + 1; i < cnt && bpos < 10; i++) {
            uint32_t pw = B[i];
            int pol = ((i - si) & 1) ? 1 : 0;          /* HI(1)/LO(0), strict alternation */
            int is_s = sub_inr(pw, te, 35);            /* half-symbol ≈ TE     */
            int is_f = sub_inr(pw, 2 * te, 35);        /* full-symbol ≈ 2×TE   */
            if(!is_s && !is_f) { err = true; break; }
            if(is_s) {
                if(phase == 0) { first_half = pol; phase = 1; }
                else {
                    cb = (uint8_t)((cb << 1) | first_half); bidx++;
                    if(bidx == 8) { pkt[bpos++] = cb; cb = 0; bidx = 0; }
                    phase = 0;
                }
            } else {                                    /* full symbol */
                if(phase == 0) { first_half = pol; phase = 1; }
                else {
                    cb = (uint8_t)((cb << 1) | first_half); bidx++;
                    if(bidx == 8) { pkt[bpos++] = cb; cb = 0; bidx = 0; }
                    first_half = pol;
                }
            }
        }
        if(err || bpos < 10) continue;
        if(pkt[0] != 0x55) continue;
        uint8_t ca = (uint8_t)(pkt[5] & 0x0F), cb2 = (uint8_t)(pkt[6] & 0x0F);
        if(ca != cb2) continue;                         /* command nibbles must match */
        if(sub_csum(pkt) != (uint8_t)(pkt[9] & 0x0F)) continue;

        uint8_t btn = ca;
        uint32_t ctr = (((uint32_t)pkt[7] << 12) |
                        ((uint32_t)pkt[8] << 4) | (pkt[9] >> 4)) & 0xFFFFF;
        uint32_t serial = ((uint32_t)pkt[1] << 16) | ((uint32_t)pkt[2] << 8) | pkt[3];

        r->addr = serial;
        r->cnt = ctr;
        r->hop = ctr;
        r->btn = btn;
        r->rolling = true;
        r->te_us = te;
        r->bits = 80;
        r->freq_mhz = buf->freq_mhz;
        r->predict_window = 256;
        r->predict_lo = (uint32_t)(ctr + 1) & 0xFFFFF;
        r->predict_hi = (uint32_t)(ctr + 8) & 0xFFFFF;
        strncpy(r->proto, "Subaru", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note),
                 "seq cnt=%u chk OK", (unsigned)ctr);
        return true;
    }
    return false;
}
