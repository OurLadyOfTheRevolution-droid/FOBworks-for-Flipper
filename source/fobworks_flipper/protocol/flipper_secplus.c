#include "flipper_decoders.h"
#include <string.h>
#include <stdio.h>

/* ── Security+ 1.0 decoder ───────────────────────────────────────────────── */
/*
 * Two packets of 21 ternary symbols (header + 20 payload) at TE ≈ 500 µs.
 * OOK grouping is 4 chips per symbol (argilo/secplus encode_ook, Flipper
 * firmware lib/subghz/protocols/secplus_v1.c, rtl_433 secplus_v1.c):
 *   0001 → 0, 0011 → 1, 0111 → 2, 0000 → inter-packet blank.
 * Packet 1 starts with header 0, packet 2 with header 2. The 40 payload
 * symbols recover rolling (bit-reversed 32-bit) and fixed (base-3) fields.
 * There is no transmitted checksum; Auto stays off until a live capture set
 * confirms the false-positive rate.
 */
static uint32_t secplus1_reverse32(uint32_t n) {
    uint32_t r = 0;
    for(int i = 0; i < 32; i++) {
        r = (r << 1) | (n & 1u);
        n >>= 1;
    }
    return r;
}

static int secplus1_mod3(int v) {
    int m = v % 3;
    return m < 0 ? m + 3 : m;
}

static bool secplus1_decode_payload(
    const uint8_t* code, uint32_t* rolling, uint32_t* fixed) {
    uint32_t roll = 0, fix = 0;
    int acc = 0;
    for(int i = 0; i < 40; i += 2) {
        if(i == 0 || i == 20) acc = 0;
        if(code[i] > 2 || code[i + 1] > 2) return false;
        roll = roll * 3u + code[i];
        acc += code[i];
        int digit = secplus1_mod3((int)code[i + 1] - acc);
        fix = fix * 3u + (uint32_t)digit;
        acc += digit;
    }
    *rolling = secplus1_reverse32(roll);
    *fixed = fix;
    return true;
}

static bool secplus1_accept(
    FlipperDecodeResult* r, uint32_t te, float mhz, const uint8_t* payload);

static int secplus1_chip_count(uint32_t duration, uint32_t te) {
    if(te == 0) return 0;
    uint32_t units = (duration + (te / 2u)) / te;
    if(units < 1) return 0;
    if(units > 48) return 48;
    return (int)units;
}

static bool secplus1_try_te(
    const FlipperPulseBuf* buf, FlipperDecodeResult* r, uint32_t te) {
    if(te < 300 || te > 800 || buf->len < 16) return false;

    uint8_t bits[512];
    int nb = 0;
    int high = 1;
    for(int i = 0; i < buf->len && nb < (int)sizeof(bits); i++) {
        int u = secplus1_chip_count(buf->durations[i], te);
        if(u <= 0) return false;
        while(u-- > 0 && nb < (int)sizeof(bits))
            bits[nb++] = (uint8_t)high;
        high ^= 1;
    }
    if(nb < 84) return false;

    for(int off = 0; off < 4; off++) {
        uint8_t payload[40];
        int plen = 0;
        int stage = 0; /* 0 hunt hdr0, 1 collect p1, 2 hunt hdr2, 3 collect p2 */
        int ok = 1;
        for(int i = off; i + 3 < nb && ok; i += 4) {
            int v = (bits[i] << 3) | (bits[i + 1] << 2) |
                    (bits[i + 2] << 1) | bits[i + 3];
            int trit;
            if(v == 0) continue; /* blank / gap */
            if(v == 1) trit = 0;
            else if(v == 3) trit = 1;
            else if(v == 7) trit = 2;
            else if(stage == 0) continue; /* still hunting the header */
            else {
                ok = 0;
                break;
            }

            if(stage == 0) {
                if(trit != 0) continue;
                stage = 1;
                plen = 0;
            } else if(stage == 1) {
                if(plen < 20) payload[plen++] = (uint8_t)trit;
                if(plen == 20) stage = 2;
            } else if(stage == 2) {
                if(trit != 2) {
                    ok = 0;
                    break;
                }
                stage = 3;
            } else {
                if(plen < 40) payload[plen++] = (uint8_t)trit;
                if(plen == 40) break;
            }
        }
        if(!ok || plen != 40) continue;
        if(secplus1_accept(r, te, buf->freq_mhz, payload)) return true;
    }
    return false;
}

static bool secplus1_accept(
    FlipperDecodeResult* r, uint32_t te, float mhz,
    const uint8_t* payload) {
    uint32_t rolling = 0, fixed = 0;
    if(!secplus1_decode_payload(payload, &rolling, &fixed)) return false;
    if(fixed == 0 && rolling == 0) return false;
    r->addr = fixed;
    r->cnt = rolling;
    r->hop = rolling;
    r->btn = (uint8_t)(fixed % 3u);
    r->rolling = true;
    r->te_us = te;
    r->bits = 42;
    r->freq_mhz = mhz;
    r->predict_window = 0;
    r->predict_lo = 0;
    r->predict_hi = 0;
    strncpy(r->proto, "Security+1.0", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "Sec+1.0 fixed=%lu switch=%u",
             (unsigned long)fixed, (unsigned)r->btn);
    return true;
}

bool flipper_decode_secplus1(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 16 || buf->len > FLIPPER_PULSE_MAX) return false;

    uint32_t cands[4];
    int nc = 0;
    if(buf->te_us >= 300 && buf->te_us <= 800) cands[nc++] = buf->te_us;
    cands[nc++] = 500;
    if(buf->te_us >= 300 && buf->te_us <= 800) {
        uint32_t lo = buf->te_us * 85u / 100u;
        uint32_t hi = buf->te_us * 115u / 100u;
        if(lo >= 300 && lo <= 800) cands[nc++] = lo;
        if(hi >= 300 && hi <= 800 && nc < 4) cands[nc++] = hi;
    }

    for(int k = 0; k < nc; k++) {
        if(secplus1_try_te(buf, r, cands[k])) return true;
    }
    return false;
}

/* ── Security+ 2.0 decoder (argilo/secplus Manchester + scramble) ───────── */
/*
 * Two Manchester packets at ~250 µs half-bit (Flipper secplus_v2 / argilo).
 * Preamble ≈ 16×0 then 4×1; packet select bits; then scrambled fixed/rolling.
 * Force-only: scramble is the integrity gate, but Auto waits on live captures.
 *
 * ORDER/INVERT keys 0..10 with gaps at 3 and 7 (argilo). Packed as three
 * 2-bit lanes per byte to keep the FAP under the loader .text cap.
 */
static const uint8_t SP2_ORDER_P[11] = {
    /* (0,2,1)(2,0,1)(0,1,2)(0,0,0)(1,2,0)(1,0,2)(2,1,0)(0,0,0)(1,2,0)(2,1,0)(0,1,2) */
    0x18, 0x12, 0x24, 0x00, 0x09, 0x21, 0x06, 0x00, 0x09, 0x06, 0x24,
};
static const uint8_t SP2_INVERT_P[11] = {
    /* same key order; each lane is 0/1 */
    0x05, 0x04, 0x10, 0x00, 0x15, 0x11, 0x14, 0x00, 0x01, 0x00, 0x11,
};

static void secplus2_unpack3(uint8_t packed, uint8_t out[3]) {
    out[0] = (uint8_t)(packed & 3u);
    out[1] = (uint8_t)((packed >> 2) & 3u);
    out[2] = (uint8_t)((packed >> 4) & 3u);
}

static uint32_t secplus2_rev28(uint32_t n) {
    uint32_t r = 0;
    for(int i = 0; i < 28; i++) {
        r = (r << 1) | (n & 1u);
        n >>= 1;
    }
    return r;
}

static bool secplus2_lut(uint8_t key, const uint8_t* table, uint8_t out[3]) {
    if(key == 3 || key == 7 || key > 10) return false;
    secplus2_unpack3(table[key], out);
    return true;
}

static bool secplus2_half(
    const uint8_t* bits, int n, uint8_t rolling[9], uint32_t* fixed) {
    if(n < 40) return false; /* type(2)+ind(8)+payload(30) minimum */
    uint8_t packet_type = (uint8_t)((bits[0] << 1) | bits[1]);
    if(packet_type > 1) return false;
    const uint8_t* ind = bits + 2;
    const uint8_t* payload = bits + 10;
    int plen = packet_type == 0 ? 30 : 54;
    if(10 + plen > n) return false;

    uint8_t okey = (uint8_t)((ind[0] << 3) | (ind[1] << 2) | (ind[2] << 1) | ind[3]);
    uint8_t ikey = (uint8_t)((ind[4] << 3) | (ind[5] << 2) | (ind[6] << 1) | ind[7]);
    uint8_t order[3], invert[3];
    if(!secplus2_lut(okey, SP2_ORDER_P, order) || !secplus2_lut(ikey, SP2_INVERT_P, invert))
        return false;

    uint8_t parts[3][18];
    int plen3 = plen / 3;
    for(int i = 0; i < plen3; i++) {
        parts[0][i] = payload[i * 3];
        parts[1][i] = payload[i * 3 + 1];
        parts[2][i] = payload[i * 3 + 2];
    }
    for(int p = 0; p < 3; p++) {
        if(invert[p]) {
            for(int i = 0; i < plen3; i++) parts[p][i] ^= 1;
        }
    }
    uint8_t ordered[3][18];
    for(int p = 0; p < 3; p++)
        memcpy(ordered[order[p]], parts[p], (size_t)plen3);

    for(int i = 0; i < 4; i++)
        rolling[i] = (uint8_t)((ind[i * 2] << 1) | ind[i * 2 + 1]);
    int rc = 4;
    for(int i = 0; i + 1 < plen3 && rc < 9; i += 2)
        rolling[rc++] = (uint8_t)((ordered[2][i] << 1) | ordered[2][i + 1]);
    for(int i = 0; i < 9; i++)
        if(rolling[i] > 2) return false;

    *fixed = 0;
    for(int i = 0; i < 10; i++) *fixed = (*fixed << 1) | ordered[0][i];
    for(int i = 0; i < 10; i++) *fixed = (*fixed << 1) | ordered[1][i];
    return true;
}

static bool secplus2_try_manchester(
    const FlipperPulseBuf* buf, FlipperDecodeResult* r, uint32_t te) {
    if(te < 180 || te > 400 || buf->len < 32) return false;
    uint8_t chips[512];
    int nc = 0;
    int level = 1; /* durations[0] HIGH */
    for(int i = 0; i < buf->len && nc < (int)sizeof(chips); i++) {
        uint32_t units = (buf->durations[i] + (te / 2u)) / te;
        if(units < 1) units = 1;
        if(units > 40) units = 40;
        while(units-- && nc < (int)sizeof(chips)) chips[nc++] = (uint8_t)level;
        level ^= 1;
    }
    if(nc < 80) return false;

    /* argilo places a raw LOW blank between manchester packets. That blank is
       often odd-length and breaks a single global pair alignment. Split on
       long LOW runs and manchester-decode each segment on its own. */
    uint8_t half_bits[2][72];
    int half_n[2] = {0, 0};
    int halves = 0;
    int seg_start = 0;
    while(seg_start < nc && halves < 2) {
        while(seg_start < nc && chips[seg_start] == 0) seg_start++;
        if(seg_start >= nc) break;
        int seg_end = seg_start;
        while(seg_end < nc) {
            if(chips[seg_end] == 0) {
                int z = 0;
                while(seg_end + z < nc && chips[seg_end + z] == 0) z++;
                if(z >= 16) {
                    /* A packet that ends on manchester bit 0 finishes with a LOW
                       chip that merges into the raw blank. Reclaim one zero so
                       the segment keeps an even chip count. */
                    if(((seg_end - seg_start) & 1) != 0) seg_end += 1;
                    break;
                }
                seg_end += z;
            } else {
                seg_end++;
            }
        }
        for(int off = 0; off < 2 && halves < 2; off++) {
            uint8_t bits[128];
            int nb = 0;
            for(int i = seg_start + off; i + 1 < seg_end && nb < (int)sizeof(bits); i += 2) {
                if(chips[i] == 1 && chips[i + 1] == 0) bits[nb++] = 0;
                else if(chips[i] == 0 && chips[i + 1] == 1) bits[nb++] = 1;
                else {
                    nb = 0;
                    break;
                }
            }
            if(nb < 40) continue;
            /* preamble: ≥10 zeros, then 1111, frame-id, half */
            for(int i = 0; i + 20 < nb; i++) {
                int zeros = 0;
                while(i + zeros < nb && bits[i + zeros] == 0 && zeros < 24) zeros++;
                if(zeros < 10) continue;
                int j = i + zeros;
                if(j + 6 >= nb) break;
                if(!(bits[j] && bits[j + 1] && bits[j + 2] && bits[j + 3])) continue;
                int at = j + 6;
                int need = nb - at;
                if(need > 72) need = 72;
                if(need < 40) continue;
                memcpy(half_bits[halves], bits + at, (size_t)need);
                half_n[halves] = need;
                halves++;
                break;
            }
        }
        seg_start = seg_end;
        while(seg_start < nc && chips[seg_start] == 0) seg_start++;
    }
    if(halves < 2) return false;

    uint8_t roll1[9], roll2[9];
    uint32_t fix1 = 0, fix2 = 0;
    if(!secplus2_half(half_bits[0], half_n[0], roll1, &fix1)) return false;
    if(!secplus2_half(half_bits[1], half_n[1], roll2, &fix2)) return false;

    uint8_t digits[18] = {
        roll2[8], roll1[8],
        roll2[4], roll2[5], roll2[6], roll2[7],
        roll1[4], roll1[5], roll1[6], roll1[7],
        roll2[0], roll2[1], roll2[2], roll2[3],
        roll1[0], roll1[1], roll1[2], roll1[3],
    };
    uint32_t rolling = 0;
    for(int i = 0; i < 18; i++) rolling = rolling * 3u + digits[i];
    if(rolling >= (1u << 28)) return false;
    rolling = secplus2_rev28(rolling);

    uint8_t btn = (uint8_t)((fix1 >> 12) & 0xF);
    uint32_t serial = ((fix1 & 0xFFFu) << 20) | (fix2 & 0xFFFFFu);
    if(serial == 0 || btn == 0 || btn == 0xF) return false;

    r->addr = serial;
    r->cnt = rolling;
    r->hop = rolling;
    r->btn = btn;
    r->rolling = true;
    r->te_us = te;
    r->bits = 80;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    r->predict_lo = 0;
    r->predict_hi = 0;
    strncpy(r->proto, "Security+2.0", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "Sec+2.0 btn=%u", (unsigned)btn);
    return true;
}

bool flipper_decode_secplus2(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 32 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t cands[4];
    int nc = 0;
    cands[nc++] = 250;
    if(buf->te_us >= 180 && buf->te_us <= 400) cands[nc++] = buf->te_us;
    if(buf->te_us >= 180 && buf->te_us <= 400) {
        uint32_t half = buf->te_us / 2u;
        if(half >= 180 && half <= 400) cands[nc++] = half;
    }
    for(int k = 0; k < nc; k++) {
        if(secplus2_try_manchester(buf, r, cands[k])) return true;
    }
    return false;
}
