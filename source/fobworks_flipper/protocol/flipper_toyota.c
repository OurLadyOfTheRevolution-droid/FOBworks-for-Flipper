#include "flipper_toyota.h"
#include "flipper_scratch.h"
#include <string.h>
#include <stdio.h>

static int toy_inr(uint32_t v, uint32_t ref, uint32_t pct) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d * 100 <= ref * pct;
}

bool flipper_decode_toyota(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t* cbuf = flipper_scratch_a(0, sizeof(uint32_t) * FLIPPER_PULSE_MAX);
    if(!cbuf) return false;
    int cnt = buf->len;
    if(cnt > FLIPPER_PULSE_MAX) cnt = FLIPPER_PULSE_MAX;
    if(cnt < 80) return false;
    cbuf[0] = buf->durations[0];
    for(int i = 1; i < cnt; i++)
        cbuf[i] = (buf->durations[i] <= 5000) ? buf->durations[i] : cbuf[i - 1];

    for(int pi = 0; pi + 79 < cnt; pi++) {
        uint32_t te0 = cbuf[pi];
        if(te0 < 150 || te0 > 700) continue;
        int pc = 0, j = pi;
        uint64_t teAcc = 0;
        while(j + 1 < cnt && toy_inr(cbuf[j], te0, 35) && toy_inr(cbuf[j + 1], te0, 50)) {
            teAcc += cbuf[j]; pc++; j += 2;
        }
        if(pc < 3) continue;
        uint32_t te = (uint32_t)(teAcc / (uint64_t)pc);

        int ds = j;
        if(j + 1 < cnt && toy_inr(cbuf[j], te, 35) &&
           cbuf[j + 1] >= te * 3 && cbuf[j + 1] <= 28000UL)
            ds = j + 2;
        if(ds + 79 > cnt) continue;

        uint64_t word = 0;
        bool ok = false;
        for(int dsTry = ds; dsTry <= ds + 1 && !ok; dsTry++) {
            if(dsTry + 79 > cnt) break;
            word = 0;
            bool ok2 = true;
            for(int b = 0; b < 40; b++) {
                uint32_t hi = cbuf[dsTry + b * 2];
                int is1 = toy_inr(hi, 2 * te, 35);
                int is0 = (!is1) && toy_inr(hi, te, 35);
                if(is1) word = (word << 1) | 1ULL;
                else if(is0) word <<= 1;
                else { ok2 = (b >= 38); word <<= 1; break; }
            }
            if(ok2) ok = true;
        }
        if(!ok) continue;
        uint32_t serial = (uint32_t)((word >> 16) & 0xFFFFFFUL);
        uint8_t btn = (uint8_t)((word >> 12) & 0xFU);
        uint16_t ctr = (uint16_t)(word & 0xFFFU);
        if(serial == 0 || serial == 0xFFFFFFUL) continue;
        int tr = 0;
        for(int i = 0; i < 39; i++)
            if(((word >> i) & 1) != ((word >> (i + 1)) & 1)) tr++;
        if(tr < 8 || tr > 32) continue;

        r->addr = serial; r->cnt = ctr; r->hop = ctr; r->btn = btn;
        r->rolling = true; r->te_us = te; r->bits = 40; r->freq_mhz = buf->freq_mhz;
        r->predict_window = 256; r->predict_lo = (uint32_t)(ctr + 1) & 0xFFF;
        r->predict_hi = (uint32_t)(ctr + 8) & 0xFFF;
        strncpy(r->proto, "Toyota", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note),
                 "structural (no checksum)  cnt=%u", (unsigned)ctr);
        return true;
    }
    return false;
}