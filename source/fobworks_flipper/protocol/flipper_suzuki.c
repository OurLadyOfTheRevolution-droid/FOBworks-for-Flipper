#include "flipper_suzuki.h"
#include <string.h>
#include <stdio.h>

/* Suzuki's frame is 64-bit PWM rolling code (see the header for its layout). Each bit has a HIGH pulse followed by a short LOW: about 2×TE for 1 and 1×TE for 0. A run of short/short pairs precedes the data. The 8-bit payload CRC (polynomial 0x7F) is the gate I use for the Auto path; it is a frame check, not proof of receiver acceptance. A short preamble HIGH looks like a data 0, so the preamble alone cannot fix the data start. I try a bounded set of alignments after at least six pairs and accept the first one whose CRC matches. */

#define SZ_TE_MIN 170
#define SZ_TE_MAX 340

static uint8_t sz_crc8(const uint8_t* d, int n) {
    uint8_t c = 0;
    for(int i = 0; i < n; i++) {
        c ^= d[i];
        for(int j = 0; j < 8; j++)
            c = (uint8_t)((c & 0x80) ? ((c << 1) ^ 0x7F) : (c << 1));
    }
    return c;
}

/* I calculate the CRC-8 over the six payload bytes in frame bits [59:12]. */
static uint8_t sz_calc_crc(uint64_t data) {
    uint8_t cd[6];
    cd[0] = (uint8_t)((data >> 52) & 0xFF);
    cd[1] = (uint8_t)((data >> 44) & 0xFF);
    cd[2] = (uint8_t)((data >> 36) & 0xFF);
    cd[3] = (uint8_t)((data >> 28) & 0xFF);
    cd[4] = (uint8_t)((data >> 20) & 0xFF);
    cd[5] = (uint8_t)((data >> 12) & 0xFF);
    return sz_crc8(cd, 6);
}

static int sz_near(uint32_t v, uint32_t ref, uint32_t tol) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d <= tol;
}

bool flipper_decode_suzuki(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < SZ_TE_MIN || te > SZ_TE_MAX) return false;
    if(buf->len < 130) return false;                 /* 64-bit PWM + preamble */

    const uint32_t sh = te, lo = te * 2;
    const uint32_t tol = te * 44 / 100;              /* ~te_delta, scales with TE */

        /* I find a short/short preamble, then test a limited number of nearby data starts. Trying the CRC at every edge creates too many accidental matches; a window of at most eight pairs still covers leading zero bits. */
    for(int i = 0; i + 140 <= buf->len; i += 2) {
        int pp = 0;
        int j = i;
        while(j + 1 < buf->len &&
              sz_near(buf->durations[j], sh, tol) &&
              sz_near(buf->durations[j + 1], sh, tol)) {
            pp++;
            j += 2;
        }
        if(pp < 8) continue;

        /* Leading zero bits can look like more preamble pairs. I check up to eight pairs back from the first non-short pair and validate each alignment. */
        for(int back = 0; back <= 8 && back <= pp - 8; back++) {
            int ds = j - back * 2;
            if(ds + 128 > buf->len) continue;

            uint64_t data = 0;
            int ok = 1;
            for(int k = 0; k < 64; k++) {
                uint32_t h = buf->durations[ds + 2 * k];
                uint32_t l = buf->durations[ds + 2 * k + 1];
                int bit;
                if(sz_near(h, lo, tol)) bit = 1;
                else if(sz_near(h, sh, tol)) bit = 0;
                else { ok = 0; break; }
                if(k < 63 && l > te * 3) { ok = 0; break; }
                data = (data << 1) | (uint64_t)bit;
            }
            if(!ok) continue;

            uint8_t rx_crc = (uint8_t)((data >> 4) & 0xFF);
            if(rx_crc != sz_calc_crc(data)) continue;

            uint32_t cnt    = (uint32_t)((data >> 44) & 0xFFFFF);
            uint32_t serial = (uint32_t)((data >> 16) & 0x0FFFFFFF);
            uint8_t  btn    = (uint8_t)((data >> 12) & 0xF);
            if(serial == 0 || serial == 0x0FFFFFFFu) continue;
            if(btn != 0x1 && btn != 0x2 && btn != 0x4 && btn != 0x8) continue;

            r->addr    = serial;
            r->cnt     = cnt;
            r->hop     = (uint32_t)(data & 0xFFFFFFFF);
            r->btn     = btn;
            r->rolling = true;
            r->te_us   = te;
            r->bits    = 64;
            r->freq_mhz = buf->freq_mhz;
            r->predict_window = 256;
            r->predict_lo = (cnt + 1) & 0xFFFFF;
            r->predict_hi = (cnt + 8) & 0xFFFFF;
            strncpy(r->proto, "Suzuki", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "CRC OK  btn=%u cnt=%lu", (unsigned)btn, (unsigned long)cnt);
            return true;
        }
        i = j; /* I advance past this preamble run */
    }
    return false;
}
