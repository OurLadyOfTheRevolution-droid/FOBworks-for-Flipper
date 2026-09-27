#include "flipper_mazda.h"
#include "flipper_scratch.h"
#include <string.h>
#include <stdio.h>

static int mz_parity8(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return v & 1;
}

uint8_t mazda_checksum(uint32_t serial, uint8_t button, uint32_t counter) {
    counter &= 0xFFFFFu;
    uint32_t s = ((serial >> 24) & 0xFF) + ((serial >> 16) & 0xFF) +
                 ((serial >> 8) & 0xFF) + (serial & 0xFF) +
                 ((counter >> 8) & 0xFF) + (counter & 0xFF) +
                 ((((counter >> 16) & 0x0F) | ((button & 0x0F) << 4)) & 0xFF);
    return (uint8_t)s;
}

void mazda_decode_key(uint64_t rawkey, MazdaFrame* f) {
    uint8_t d[8];
    for(int i = 0; i < 8; i++) d[i] = (uint8_t)(rawkey >> (56 - i * 8));

    const uint8_t checksum = d[7];
    const int parity = mz_parity8(d[7]);
    const int limit = parity ? 6 : 5;
    const uint8_t mask = d[limit];
    for(int i = 0; i < limit; i++) d[i] ^= mask;
    if(!parity) d[6] ^= mask;

    const uint8_t clo = (uint8_t)((d[5] & 0x55) | (d[6] & 0xAA));
    const uint8_t cmid = (uint8_t)((d[6] & 0x55) | (d[5] & 0xAA));
    f->serial = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                ((uint32_t)d[2] << 8) | d[3];
    f->button = (d[4] >> 4) & 0x0F;
    f->counter = (((uint32_t)d[4] & 0x0F) << 16) | ((uint32_t)cmid << 8) | clo;
    f->checksum = checksum;
    f->crc_ok = (mazda_checksum(f->serial, f->button, f->counter) == checksum);
}

uint64_t mazda_encode_key(uint32_t serial, uint8_t button, uint32_t counter) {
    uint8_t d[8];
    counter &= 0xFFFFFu;
    button &= 0x0F;
    d[0] = (serial >> 24) & 0xFF;
    d[1] = (serial >> 16) & 0xFF;
    d[2] = (serial >> 8) & 0xFF;
    d[3] = serial & 0xFF;
    d[4] = (uint8_t)((button << 4) | ((counter >> 16) & 0x0F));
    d[5] = (counter >> 8) & 0xFF;
    d[6] = counter & 0xFF;
    d[7] = mazda_checksum(serial, button, counter);

    const uint8_t stored_5 = (uint8_t)((d[6] & 0x55) | (d[5] & 0xAA));
    const uint8_t stored_6 = (uint8_t)((d[6] & 0xAA) | (d[5] & 0x55));
    const uint8_t xor_mask = stored_5 ^ stored_6;
    const int replace_second = (mz_parity8(d[7]) == 0);
    const uint8_t forward_mask = replace_second ? stored_5 : stored_6;
    d[5] = replace_second ? stored_5 : xor_mask;
    d[6] = replace_second ? xor_mask : stored_6;
    for(int i = 0; i < 5; i++) d[i] ^= forward_mask;

    uint64_t k = 0;
    for(int i = 0; i < 8; i++) k = (k << 8) | d[i];
    return k;
}

#define MZ_MAXBITS 256

static int mz_manch_bits(const FlipperPulseBuf* buf, uint32_t te, int start,
                         int phase, uint8_t* bits) {
    uint8_t* half = flipper_scratch_a(0, FLIPPER_PULSE_MAX * 4);
    if(!half) return 0;
    int hn = 0;
    uint8_t level = (start & 1) ? 0 : 1;
    for(int i = start; i < buf->len && hn < FLIPPER_PULSE_MAX * 4; i++) {
        uint32_t d = buf->durations[i];
        int cnt = (int)((d + te / 2) / te);
        if(cnt < 1) cnt = 1;
        if(cnt > 4) break;
        for(int k = 0; k < cnt && hn < FLIPPER_PULSE_MAX * 4; k++) half[hn++] = level;
        level ^= 1;
    }
    int nb = 0;
    for(int i = phase; i + 1 < hn && nb < MZ_MAXBITS; i += 2) {
        uint8_t a = half[i], b = half[i + 1];
        if(a == 1 && b == 0) bits[nb++] = 1;
        else if(a == 0 && b == 1) bits[nb++] = 0;
        else break;
    }
    return nb;
}

static int mz_inr(uint32_t v, uint32_t ref, uint32_t pct) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d * 100 <= ref * pct;
}

static bool mazda_v1_pwm(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    for(int si = 1; si + 144 < cnt; si++) {
        if(!mz_inr(B[si], 14400, 20) || !mz_inr(B[si - 1], 450, 25)) continue;
        int j = si + 1;
        uint8_t pkt[9] = {0};
        bool ok = true;
        for(int b = 0; b < 72; b++) {
            uint32_t lo = B[j + b * 2 + 1];
            if(mz_inr(lo, 1350, 20)) pkt[b / 8] |= (uint8_t)(1u << (7 - (b % 8)));
            else if(!mz_inr(lo, 450, 25)) { ok = false; break; }
        }
        if(!ok) continue;
        uint32_t hop = ((uint32_t)pkt[0] << 24) | ((uint32_t)pkt[1] << 16) |
                       ((uint32_t)pkt[2] << 8) | pkt[3];
        uint32_t serial = ((uint32_t)pkt[4] << 16) | ((uint32_t)pkt[5] << 8) | pkt[6];
        uint8_t ctr = pkt[7], btn = pkt[8] >> 4, rx_ck = pkt[8] & 0x0F;
        uint8_t c = 0;
        for(int i = 0; i < 4; i++) c ^= (uint8_t)((hop >> (i * 8)) & 0xFF);
        for(int i = 0; i < 3; i++) c ^= (uint8_t)((serial >> (i * 8)) & 0xFF);
        c ^= (uint8_t)(ctr ^ (btn & 0x0F));
        if(rx_ck != (c & 0x0F) || (serial == 0 && hop == 0)) continue;

        r->addr = serial;
        r->cnt = ctr;
        r->hop = hop;
        r->btn = btn;
        r->rolling = true;
        r->te_us = 450;
        r->bits = 72;
        r->freq_mhz = buf->freq_mhz;
        r->predict_window = 256;
        r->predict_lo = (uint32_t)(ctr + 1) & 0xFF;
        r->predict_hi = (uint32_t)(ctr + 8) & 0xFF;
        strncpy(r->proto, "Mazda-VDO", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note), "Siemens-VDO cnt=%u chk OK", (unsigned)ctr);
        return true;
    }
    return false;
}

static int mzi_parity8(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return v & 1;
}

static void mzi_interleave(uint8_t b[8]) {
    uint8_t b5 = b[5], b6 = b[6];
    b[5] = (uint8_t)((b5 & 0xAA) | (b6 & 0x55));
    b[6] = (uint8_t)((b5 & 0x55) | (b6 & 0xAA));
}

static void mzi_whiten(uint8_t b[8]) {
    if(mzi_parity8(b[7])) {
        for(int k = 0; k < 6; k++) b[k] ^= b[6];
    } else {
        b[0] ^= b[5]; b[1] ^= b[5]; b[2] ^= b[5];
        b[3] ^= b[5]; b[4] ^= b[5]; b[6] ^= b[5];
    }
}

static void mzi_wire_to_logical(const uint8_t wire[8], uint8_t logical[8]) {
    uint8_t b[8];
    for(int i = 0; i < 8; i++) b[i] = (uint8_t)(255u - wire[i]);
    mzi_whiten(b);
    mzi_interleave(b);
    memcpy(logical, b, 8);
}

static bool mazda_infinity_manch(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    uint32_t cands[4];
    int nc = 0;
    if(buf->te_us >= 150 && buf->te_us <= 380) cands[nc++] = buf->te_us;
    cands[nc++] = 250; cands[nc++] = 220; cands[nc++] = 280;
    int gapstart = 0;
    for(int i = 0; i + 1 < buf->len; i++)
        if(buf->durations[i] > 4000) { gapstart = i + 1; break; }
    static const uint8_t SYNC[24] = {
        1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,0,1,0,1,1,1
    };
    for(int t = 0; t < nc; t++) {
        uint32_t te = cands[t];
        if(te < 150 || te > 380) continue;
        int starts[2] = { 0, gapstart };
        for(int si = 0; si < 2; si++) {
            int st = starts[si];
            if(st < 0 || st >= buf->len) continue;
            for(int phase = 0; phase < 2; phase++) {
                uint8_t* bits = flipper_scratch_a(FLIPPER_PULSE_MAX * 4, MZ_MAXBITS);
                if(!bits) continue;
                int nb = mz_manch_bits(buf, te, st, phase, bits);
                if(nb < 24 + 64 + 8) continue;
                for(int j = 0; j + 24 + 64 + 8 <= nb; j++) {
                    int ok = 1;
                    for(int k = 0; k < 24; k++) if(bits[j + k] != SYNC[k]) { ok = 0; break; }
                    if(!ok) continue;
                    uint8_t wire[8];
                    for(int by = 0; by < 8; by++) {
                        uint8_t v = 0;
                        for(int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | bits[j + 24 + by * 8 + k]);
                        wire[by] = v;
                    }
                    uint8_t trailer = 0;
                    for(int k = 0; k < 8; k++) trailer = (uint8_t)((trailer << 1) | bits[j + 24 + 64 + k]);
                    if(trailer != 0x5A) continue;
                    uint8_t lg[8];
                    mzi_wire_to_logical(wire, lg);
                    uint32_t sum = 0;
                    for(int i = 0; i < 7; i++) sum += lg[i];
                    if((uint8_t)(sum & 0xFF) != lg[7]) continue;
                    uint32_t serial = ((uint32_t)lg[0] << 24) | ((uint32_t)lg[1] << 16) |
                                      ((uint32_t)lg[2] << 8) | lg[3];
                    if(serial == 0 || serial == 0xFFFFFFFFu) continue;
                    uint8_t btn = lg[4];
                    uint32_t ctr = ((uint32_t)lg[5] << 8) | lg[6];
                    r->addr = serial; r->cnt = ctr; r->hop = ctr; r->btn = btn;
                    r->rolling = true; r->te_us = te; r->bits = 64; r->freq_mhz = buf->freq_mhz;
                    r->predict_window = 256; r->predict_lo = (ctr + 1) & 0xFFFF;
                    r->predict_hi = (ctr + 8) & 0xFFFF;
                    strncpy(r->proto, "Mazda-Infinity", sizeof(r->proto) - 1);
                    snprintf(r->predict_note, sizeof(r->predict_note),
                             "sync FFFFD7 cnt=%lu chk OK", (unsigned long)ctr);
                    return true;
                }
            }
        }
    }
    return false;
}

bool flipper_decode_mazda(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 0 || buf->len > FLIPPER_PULSE_MAX) return false;
    if(mazda_infinity_manch(buf, r)) return true;
    memset(r, 0, sizeof(*r));
    r->freq_mhz = buf->freq_mhz;

    uint32_t cands[4];
    int nc = 0;
    if(buf->te_us >= 150 && buf->te_us <= 380) cands[nc++] = buf->te_us;
    cands[nc++] = 250; cands[nc++] = 220; cands[nc++] = 280;
    int gapstart = 0;
    for(int i = 0; i + 1 < buf->len; i++)
        if(buf->durations[i] > 4000) { gapstart = i + 1; break; }
    for(int t = 0; t < nc; t++) {
        uint32_t te = cands[t];
        if(te < 150 || te > 380) continue;
        int starts[2] = { 0, gapstart };
        for(int si = 0; si < 2; si++) {
            int st = starts[si];
            if(st < 0 || st >= buf->len) continue;
            for(int phase = 0; phase < 2; phase++) {
                uint8_t* bits = flipper_scratch_a(FLIPPER_PULSE_MAX * 4, MZ_MAXBITS);
                if(!bits) continue;
                int nb = mz_manch_bits(buf, te, st, phase, bits);
                if(nb < 8 + 64) continue;
                for(int j = 8; j + 8 + 64 <= nb; j++) {
                    int ones = 1;
                    for(int k = 1; k <= 8; k++) if(bits[j - k] != 1) { ones = 0; break; }
                    if(!ones) continue;
                    uint8_t sync = 0;
                    for(int k = 0; k < 8; k++) sync = (uint8_t)((sync << 1) | bits[j + k]);
                    if(sync != 0xD7) continue;
                    uint64_t air = 0;
                    for(int k = 0; k < 64; k++) air = (air << 1) | bits[j + 8 + k];
                    MazdaFrame f;
                    mazda_decode_key(~air, &f);
                    if(!f.crc_ok || (f.serial == 0 && f.counter == 0)) continue;
                    r->addr = f.serial; r->cnt = f.counter & 0xFFFF; r->hop = f.counter;
                    r->btn = f.button; r->rolling = true; r->te_us = te; r->bits = 64;
                    r->freq_mhz = buf->freq_mhz; r->predict_window = 256;
                    r->predict_lo = (f.counter + 1) & 0xFFFFF;
                    r->predict_hi = (f.counter + 8) & 0xFFFFF;
                    strncpy(r->proto, "Mazda", sizeof(r->proto) - 1);
                    snprintf(r->predict_note, sizeof(r->predict_note),
                             "cnt=%lu chk OK", (unsigned long)f.counter);
                    return true;
                }
            }
        }
    }
    if(mazda_v1_pwm(buf, r)) return true;
    return mazda_infinity_manch(buf, r);
}