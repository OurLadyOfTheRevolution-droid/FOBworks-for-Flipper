#include "flipper_fiat.h"
#include "flipper_scratch.h"
#include <string.h>
#include <stdio.h>

#define FIAT_MAXBITS 128

static int fiat_near(uint32_t v, uint32_t ref, uint32_t delta) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d <= delta;
}

static int fiat_cells_run(const FlipperPulseBuf* buf, int* io_i, uint32_t sh,
                          uint32_t lo, uint32_t delta, uint8_t* out, int max) {
    int n = 0, i = *io_i;
    for(; i < buf->len && n < max; i++) {
        uint32_t d = buf->durations[i];
        int add = fiat_near(d, sh, delta) ? 1 : fiat_near(d, lo, delta) ? 2 : 0;
        if(!add) break;
        uint8_t level = (i & 1) ? 0 : 1;
        for(int k = 0; k < add && n < max; k++) out[n++] = level;
    }
    *io_i = i + 1;
    return n;
}

static bool fiat_pack(const uint8_t* cells, int cn, int start, int bits, bool invert,
                      uint8_t* raw, int raw_bytes) {
    if(start < 0 || start + bits * 2 > cn) return false;
    memset(raw, 0, (size_t)raw_bytes);
    for(int i = 0; i < bits; i++) {
        uint8_t a = cells[start + i * 2], b = cells[start + i * 2 + 1];
        if(a == b) return false;
        bool bit = (a != 0);
        if(invert) bit = !bit;
        if(bit) raw[i >> 3] |= (uint8_t)(0x80u >> (i & 7));
    }
    return true;
}

static uint32_t fiat_uid(const uint8_t* x) {
    return ((uint32_t)x[2] << 24) | ((uint32_t)x[3] << 16) |
           ((uint32_t)x[4] << 8) | x[5];
}

static uint32_t fiat1_cnt(const uint8_t* x) {
    return ((uint32_t)(x[6] & 0x0F) << 6) | (x[7] >> 2);
}

static uint32_t fiat1_hop(const uint8_t* x) {
    return ((uint32_t)(x[7] & 3) << 30) | ((uint32_t)x[8] << 22) |
           ((uint32_t)x[9] << 14) | ((uint32_t)x[10] << 6) | (x[11] >> 2);
}

static bool fiat1_valid(const uint8_t* x) {
    if(x[0] != 0 || x[1] != 1) return false;
    uint8_t q = 1;
    for(int i = 0; i < 12; i++) q ^= x[i];
    uint8_t b = x[6] >> 4;
    uint32_t uid = fiat_uid(x);
    return q == x[12] && (b == 1 || b == 2 || b == 4 || b == 8) &&
           uid != 0 && uid != 0xFFFFFFFFu;
}

static bool fiat2_fca(const uint8_t* x) { return (x[6] & 0xF0) == 0xD0; }

static uint32_t fiat2_hop(const uint8_t* x) {
    if(fiat2_fca(x))
        return ((uint32_t)x[10] << 24) | ((uint32_t)x[11] << 16) |
               ((uint32_t)x[12] << 8) | x[13];
    return ((uint32_t)x[9] << 24) | ((uint32_t)x[10] << 16) |
           ((uint32_t)x[11] << 8) | x[12];
}

static uint32_t fiat2_cnt(const uint8_t* x) {
    if(fiat2_fca(x))
        return (~(((uint32_t)x[8] << 6) | (x[9] >> 2))) & 0x3FFF;
    return (~(((uint32_t)(x[7] & 0x3F) << 5) | (x[8] >> 3))) & 0x7FF;
}

static bool fiat2_valid(const uint8_t* x) {
    if(x[0] != 0 || x[1] != 1) return false;
    uint8_t b = x[7] >> 6;
    uint32_t uid = fiat_uid(x);
    return (b == 1 || b == 2 || b == 3) && uid != 0 && uid != 0xFFFFFFFFu;
}

static bool fiat_try_v1(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    uint8_t* cells = flipper_scratch_a(0, FLIPPER_PULSE_MAX * 4);
    if(!cells) return false;
    const uint32_t scales[][3] = {{250, 500, 100}, {100, 200, 50}};
    for(int s = 0; s < 2; s++) {
        for(int i = 0; i < buf->len;) {
            int cn = fiat_cells_run(buf, &i, scales[s][0], scales[s][1], scales[s][2],
                                    cells, FLIPPER_PULSE_MAX * 4);
            if(cn < 208) continue;
            uint8_t raw[13];
            int max_start = cn - 208;
            if(max_start > 64) max_start = 64;
            for(int start = 0; start <= max_start; start++) for(int inv = 0; inv < 2; inv++) {
                if(!fiat_pack(cells, cn, start, 104, inv != 0, raw, 13) || !fiat1_valid(raw)) continue;
                r->addr = fiat_uid(raw); r->cnt = fiat1_cnt(raw); r->hop = fiat1_hop(raw);
                r->btn = (uint8_t)(raw[6] >> 4); r->rolling = true;
                r->te_us = scales[s][0]; r->bits = 104; r->freq_mhz = buf->freq_mhz;
                r->predict_window = 256; r->predict_lo = (r->cnt + 1) & 0x3FF;
                r->predict_hi = (r->cnt + 8) & 0x3FF;
                strncpy(r->proto, "Fiat-V1", sizeof(r->proto) - 1);
                snprintf(r->predict_note, sizeof(r->predict_note),
                         "XOR ck OK sn=%08lX", (unsigned long)r->addr);
                return true;
            }
        }
    }
    return false;
}

/* V2 is force-only: its header and button fields are structural checks, not an
   integrity check comparable to the V1 checksum. */
static bool fiat_try_v2(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    uint8_t* cells = flipper_scratch_a(0, FLIPPER_PULSE_MAX * 4);
    if(!cells) return false;
    uint32_t scales[3][3];
    int ns = 0;
    scales[ns][0] = 210; scales[ns][1] = 420; scales[ns++][2] = 100;
    if(buf->te_us >= 150 && buf->te_us <= 280) {
        scales[ns][0] = buf->te_us; scales[ns][1] = buf->te_us * 2;
        scales[ns++][2] = 100;
    }
    scales[ns][0] = 200; scales[ns][1] = 400; scales[ns++][2] = 100;
    for(int s = 0; s < ns; s++) for(int i = 0; i < buf->len;) {
        int cn = fiat_cells_run(buf, &i, scales[s][0], scales[s][1], scales[s][2],
                                cells, FLIPPER_PULSE_MAX * 4);
        if(cn < 224) continue;
        uint8_t raw[14];
        int max_start = cn - 224;
        if(max_start > 64) max_start = 64;
        for(int start = 0; start <= max_start; start++) for(int inv = 0; inv < 2; inv++) {
            if(!fiat_pack(cells, cn, start, 112, inv != 0, raw, 14) || !fiat2_valid(raw)) continue;
            uint32_t hop = fiat2_hop(raw);
            if((hop >> 16) == 0) continue;
            r->addr = fiat_uid(raw); r->cnt = fiat2_cnt(raw); r->hop = hop;
            r->btn = raw[7]; r->rolling = true; r->te_us = scales[s][0];
            r->bits = 112; r->freq_mhz = buf->freq_mhz; r->predict_window = 256;
            r->predict_lo = (r->cnt + 1) & 0x3FFF; r->predict_hi = (r->cnt + 8) & 0x3FFF;
            strncpy(r->proto, fiat2_fca(raw) ? "Fiat-V2-FCA" : "Fiat-V2", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note), "hdr 0001 sn=%08lX",
                     (unsigned long)r->addr);
            return true;
        }
    }
    return false;
}

/* The legacy 64-bit Manchester V0 layout has no checksum, so it is force-only. */
static bool fiat_try_v0(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    uint8_t* bits = flipper_scratch_a(0, FIAT_MAXBITS);
    uint8_t* half = flipper_scratch_a(FIAT_MAXBITS, FLIPPER_PULSE_MAX * 4);
    if(!bits || !half) return false;
    uint32_t cands[3];
    int nc = 0;
    if(buf->te_us >= 120 && buf->te_us <= 320) cands[nc++] = buf->te_us;
    cands[nc++] = 200; cands[nc++] = 250;
    for(int t = 0; t < nc; t++) {
        uint32_t te = cands[t];
        if(te < 120 || te > 320) continue;
        for(int phase = 0; phase < 2; phase++) {
            int hn = 0;
            uint8_t level = 1;
            for(int i = 0; i < buf->len && hn < FLIPPER_PULSE_MAX * 4; i++) {
                int n = (int)((buf->durations[i] + te / 2) / te);
                if(n < 1) n = 1;
                if(n > 4) break;
                for(int k = 0; k < n && hn < FLIPPER_PULSE_MAX * 4; k++) half[hn++] = level;
                level ^= 1;
            }
            int nb = 0;
            for(int i = phase; i + 1 < hn && nb < FIAT_MAXBITS; i += 2) {
                if(half[i] == 1 && half[i + 1] == 0) bits[nb++] = 1;
                else if(half[i] == 0 && half[i + 1] == 1) bits[nb++] = 0;
                else break;
            }
            if(nb < 64) continue;
            int off = 0;
            while(off < nb && bits[off] == 1) off++;
            int starts[2] = {0, off};
            for(int o = 0; o < 2; o++) {
                int s = starts[o];
                if(s + 64 > nb) continue;
                uint64_t w = 0;
                for(int k = 0; k < 64; k++) w = (w << 1) | bits[s + k];
                uint32_t hop = (uint32_t)(w >> 32), fix = (uint32_t)w;
                if(fix == 0 || fix == 0xFFFFFFFFu || hop == 0 || hop == 0xFFFFFFFFu) continue;
                uint8_t btn = 0;
                for(int k = 0; k < 8 && s + 64 + k < nb; k++)
                    btn = (uint8_t)((btn << 1) | bits[s + 64 + k]);
                r->addr = fix; r->cnt = hop & 0xFFFF; r->hop = hop;
                r->btn = btn & 0x7F; r->rolling = true; r->te_us = te;
                r->bits = 64; r->freq_mhz = buf->freq_mhz; r->predict_window = 256;
                r->predict_lo = (hop + 1) & 0xFFFF; r->predict_hi = (hop + 8) & 0xFFFF;
                strncpy(r->proto, "Fiat", sizeof(r->proto) - 1);
                snprintf(r->predict_note, sizeof(r->predict_note), "no checksum fix=0x%08lX",
                         (unsigned long)fix);
                return true;
            }
        }
    }
    return false;
}

bool flipper_decode_fiat(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 64 || buf->len > FLIPPER_PULSE_MAX) return false;
    return fiat_try_v1(buf, r);
}

bool flipper_decode_fiat_forced(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 64 || buf->len > FLIPPER_PULSE_MAX) return false;
    if(fiat_try_v1(buf, r)) return true;
    memset(r, 0, sizeof(*r)); r->freq_mhz = buf->freq_mhz;
    if(fiat_try_v2(buf, r)) return true;
    memset(r, 0, sizeof(*r)); r->freq_mhz = buf->freq_mhz;
    return fiat_try_v0(buf, r);
}