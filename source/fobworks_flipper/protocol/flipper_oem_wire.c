#include "flipper_decoders.h"
#include "flipper_scratch.h"
#include "flipper_gm.h"
#include "flipper_ford.h"
#include "flipper_chrysler.h"
#include "flipper_kia.h"
#include "flipper_vag.h"
#include "flipper_psa.h"
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* These adapters turn raw pulse timings into MSB-first bytes for the existing
   protocol parsers: gm_parse, ford_v0_parse, chrysler_parse, kia_v0_parse,
   vag_parse_frame, and psa_decrypt_mode23. The parsers apply their own preamble,
   checksum, or CRC checks. That lets a recovered bitstream enter the Auto path;
   the gate can reject a bad timing estimate or alignment, but is not proof that
   the transmitter was authentic or that a receiver would accept its frame.

   For PWM, each bit uses a HIGH+LOW cell; HIGH above about 1.5T means 1.
   Manchester uses two half-bit levels: HIGH then LOW is 1, LOW then HIGH is 0. */
/* ─────────────────────────────────────────────────────────────────────────── */

#define OEM_MAX_BYTES 16                 /* 128-bit PSA frame is the largest     */

/* Try the estimated chip period plus offsets of ±15%. The margin covers an
   estimate that landed slightly off a merged pulse run. Return the count used. */
static int oem_te_candidates(const FlipperPulseBuf* buf, uint32_t* out, int max) {
    int n = 0;
    uint32_t te = buf->te_us;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te * 115 / 100;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te * 85 / 100;
    return n;
}

/* Find the first data cell after a long HIGH (at least 8T) and short LOW.
   Return 0 when that leader is absent and data starts at the first edge. */
static int oem_pwm_data_start(const FlipperPulseBuf* buf, uint32_t te) {
    for(int i = 0; i + 1 < buf->len; i++) {
        if(buf->durations[i] >= te * 8 && buf->durations[i + 1] <= te * 3)
            return i + 2;
    }
    return 0;
}

/* Decode nbits PWM cells at data_start into out, MSB first. Return true only
   if each consumed HIGH/LOW pair fits the expected bit-cell timing. */
static bool oem_pwm_extract(const FlipperPulseBuf* buf, uint32_t te, int data_start,
                            int nbits, uint8_t* out) {
    int nbytes = (nbits + 7) / 8;
    if(nbytes > OEM_MAX_BYTES) return false;
    memset(out, 0, nbytes);
    const uint32_t thr = te + (te >> 1);          /* ~1.5T split */
    for(int b = 0; b < nbits; b++) {
        int idx = data_start + 2 * b;
        if(idx + 1 >= buf->len) return false;
        uint32_t hi = buf->durations[idx];
        uint32_t lo = buf->durations[idx + 1];
        if(hi > te * 3 || lo > te * 3) return false;   /* not a bit cell */
        if(hi > thr) out[b >> 3] |= (uint8_t)(0x80 >> (b & 7));
    }
    return true;
}

/* Expand each Manchester run into rounded half-bit levels, then decode pairs
   starting at phase. Store the result MSB first and return the recovered count. */
static int oem_manch_extract(const FlipperPulseBuf* buf, uint32_t te, int start_idx,
                             int phase, int max_bits, uint8_t* out) {
    if(te == 0) return 0;
    uint8_t* half = flipper_scratch_a(0, FLIPPER_PULSE_MAX * 4);
    if(!half) return 0;
    int hn = 0;
    uint8_t level = (start_idx & 1) ? 0 : 1;      /* durations[0] is HIGH */
    for(int i = start_idx; i < buf->len && hn < FLIPPER_PULSE_MAX * 4; i++) {
        uint32_t d = buf->durations[i];
        int cnt = (int)((d + te / 2) / te);       /* nearest whole half-bits */
        if(cnt < 1) cnt = 1;
        if(cnt > 4) break;                        /* long gap ends the frame */
        for(int k = 0; k < cnt && hn < FLIPPER_PULSE_MAX * 4; k++) half[hn++] = level;
        level ^= 1;
    }
    int nbytes = (max_bits + 7) / 8;
    if(nbytes > OEM_MAX_BYTES) return 0;
    memset(out, 0, nbytes);
    int nb = 0;
    for(int i = phase; i + 1 < hn && nb < max_bits; i += 2) {
        uint8_t a = half[i], b = half[i + 1];
        int bit;
        if(a == 1 && b == 0) bit = 1;
        else if(a == 0 && b == 1) bit = 0;
        else break;                               /* not a valid Manchester pair */
        if(bit) out[nb >> 3] |= (uint8_t)(0x80 >> (nb & 7));
        nb++;
    }
    return nb;
}

/* ── GM — 112-bit PPM, additive mod-256 checksum ─────────────────────────── */
bool flipper_decode_gm(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 112) return false;
    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 100 || te > 1200) continue;
        int starts[2] = { oem_pwm_data_start(buf, te), 0 };
        for(int s = 0; s < 2; s++) {
            if(!oem_pwm_extract(buf, te, starts[s], 112, raw)) continue;
            GmFrame f;
            if(!gm_parse(raw, 112, &f)) continue;
            r->addr = f.id;  r->cnt = f.seq & 0xFFFF;  r->hop = f.seq;
            r->btn = f.button;  r->rolling = true;  r->te_us = te;  r->bits = 112;
            r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
            r->predict_lo = (f.seq + 1) & 0xFFFFFF;
            r->predict_hi = (f.seq + 8) & 0xFFFFFF;
            strncpy(r->proto, "GM", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "%s  id=0x%08lX", f.function, (unsigned long)f.id);
            return true;
        }
    }
    return false;
}

/* ── Ford — V0 (64-bit Manchester) then V2 (72-bit PWM) ──────────────────── */
bool flipper_decode_ford(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 64) return false;
    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 100 || te > 1000) continue;
        /* Try both Manchester phases for V0. V2's PWM path was removed because
           its 8-bit additive checksum also matched unrelated automotive PWM. */
        for(int phase = 0; phase < 2; phase++) {
            if(oem_manch_extract(buf, te, 0, phase, 64, raw) >= 64) {
                FordV0Frame f;
                if(ford_v0_parse(raw, 64, &f)) {
                    r->addr = f.serial;  r->cnt = f.counter;  r->hop = f.counter;
                    r->btn = f.button;  r->rolling = true;  r->te_us = te;
                    r->bits = 64;  r->freq_mhz = buf->freq_mhz;
                    r->predict_window = 256;
                    r->predict_lo = (f.counter + 1) & 0xFFFF;
                    r->predict_hi = (f.counter + 8) & 0xFFFF;
                    strncpy(r->proto, "Ford-V0", sizeof(r->proto) - 1);
                    snprintf(r->predict_note, sizeof(r->predict_note),
                             "%s", f.function ? f.function : "Ford");
                    return true;
                }
            }
        }
    }
    return false;
}

/* ── Chrysler — 80-bit PWM edge path + dual-packet XOR path ── */
static bool chrysler_edge_pwm(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    /* This path expects 300 µs HIGH for 0, 600 µs for 1, and a gap of at
       least 9000 µs. Its 8-bit gate is raw[5] == (msb ? raw[1] : raw[1]^0xC3). */
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    for(int si = 0; si + 161 < cnt; si++) {
        if(B[si] < 9000) continue;
        int j = si + 1;
        if(j + 160 > cnt) continue;
        uint8_t raw[10];
        memset(raw, 0, sizeof(raw));
        bool ok = true;
        for(int b = 0; b < 80; b++) {
            uint32_t hi = B[j + b * 2];
            if(hi >= 150 && hi <= 450) { /* zero */ }
            else if(hi > 450 && hi <= 900) raw[b / 8] |= (uint8_t)(1u << (b % 8));
            else { ok = false; break; }
        }
        if(!ok) continue;
        uint8_t msb = (raw[0] >> 7) & 1;
        if(raw[5] != (msb ? raw[1] : (uint8_t)(raw[1] ^ 0xC3u))) continue;
        uint32_t serial = ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
        if(serial == 0) continue;
        r->addr = serial;
        r->cnt = raw[3];
        r->hop = ((uint32_t)raw[4] << 8) | raw[5];
        r->btn = raw[4] & 0x0F;
        r->rolling = true;
        r->te_us = 300;
        r->bits = 80;
        r->freq_mhz = buf->freq_mhz;
        r->predict_window = 64;
        r->predict_lo = (r->cnt + 1) & 0xFF;
        r->predict_hi = (r->cnt + 4) & 0xFF;
        strncpy(r->proto, "Chrysler", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note), "edge-PWM ck OK");
        return true;
    }
    return false;
}

bool flipper_decode_chrysler(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 80) return false;
    if(chrysler_edge_pwm(buf, r)) return true;
    memset(r, 0, sizeof(*r)); r->freq_mhz = buf->freq_mhz;

    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 150 || te > 1500) continue;
        int starts[2] = { oem_pwm_data_start(buf, te), 0 };
        for(int s = 0; s < 2; s++) {
            if(!oem_pwm_extract(buf, te, starts[s], 80, raw)) continue;
            ChryslerFrame f;
            if(!chrysler_parse(raw, 80, &f)) continue;
            if(f.serial == 0 && f.counter == 0) continue;
            r->addr = f.serial;  r->cnt = f.counter;  r->hop = (uint32_t)f.plain_a;
            r->btn = f.button;  r->rolling = true;  r->te_us = te;  r->bits = 80;
            r->freq_mhz = buf->freq_mhz;  r->predict_window = 64;
            r->predict_lo = (f.counter + 1) & 0x3F;
            r->predict_hi = (f.counter + 4) & 0x3F;
            strncpy(r->proto, "Chrysler", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "%s", f.function ? f.function : "Chrysler");
            return true;
        }
    }
    return false;
}

/* ── Hyundai Santa Fe / Solaris 2013-2016 (TRW fob) ──────────────────────── */
/* The TRW frame is 80-bit MSB-first OOK PWM. HIGH stays near 375 µs; a
   roughly 12 ms LOW marks the frame boundary. The data is in the LOW width:
   125 µs represents 1 and 375 µs represents 0.
   Layout: [rolling 32][serial 24][counter 8][button 8][CRC-8].
   CRC-8 uses polynomial 0x31 and initial value 0xFF over the first 9 bytes.
   This checksum is the gate for the secondary path under KIA/Hyundai Auto. */
static uint8_t sf_crc8_31(const uint8_t* d, int n) {
    uint8_t c = 0xFF;
    for(int i = 0; i < n; i++) {
        c ^= d[i];
        for(int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

static int sf_inr(uint32_t v, uint32_t ref, uint32_t pct) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d * 100 <= ref * pct;
}

static bool santafe_pwm(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    for(int si = 1; si + 160 < cnt; si++) {
        if(!sf_inr(B[si], 12000, 20)) continue;
        if(!sf_inr(B[si - 1], 375, 25)) continue;
        int j = si + 1;
        uint8_t pkt[10] = {0};
        bool ok = true;
        for(int b = 0; b < 80; b++) {
            uint32_t lo = B[j + b * 2 + 1];
            if(sf_inr(lo, 125, 30))      pkt[b / 8] |= (uint8_t)(1u << (7 - (b % 8)));
            else if(sf_inr(lo, 375, 25)) { /* zero bit */ }
            else { ok = false; break; }
        }
        if(!ok) continue;
        if(pkt[9] != sf_crc8_31(pkt, 9)) continue;

        uint32_t rolling = ((uint32_t)pkt[0] << 24) | ((uint32_t)pkt[1] << 16) |
                           ((uint32_t)pkt[2] << 8) | pkt[3];
        uint32_t serial = ((uint32_t)pkt[4] << 16) | ((uint32_t)pkt[5] << 8) | pkt[6];
        uint8_t ctr = pkt[7], btn = pkt[8];
        if(serial == 0 && rolling == 0) continue;

        r->addr = serial;  r->cnt = ctr;  r->hop = rolling;
        r->btn = btn;  r->rolling = true;  r->te_us = 375;  r->bits = 80;
        r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
        r->predict_lo = (uint32_t)(ctr + 1) & 0xFF;
        r->predict_hi = (uint32_t)(ctr + 8) & 0xFF;
        strncpy(r->proto, "Hyundai-SantaFe", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note), "TRW CRC8 cnt=%u", (unsigned)ctr);
        return true;
    }
    return false;
}

/* ── Hyundai / Kia RIO early (~2001-2008) — 64-bit fixed-code MSB-first ──── */
/* Early Hyundai/Kia RIO uses a 64-bit fixed-code frame, MSB first. HIGH width
   carries each bit: about 312 µs for 0 and 728 µs for 1; sync is about
   10400 µs. Layout: [serial 32][button mask 16][checksum 16].
   The checksum is ~(serial ^ (serial >> 16) ^ button mask). As a fixed code,
   a captured frame can be replayed; this check does not make it rolling code. */
static bool hkr_pwm(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    for(int si = 1; si + 128 < cnt; si++) {
        if(!sf_inr(B[si], 10400, 20)) continue;
        if(!sf_inr(B[si - 1], 312, 25)) continue;
        int j = si + 1;
        uint64_t word = 0;
        bool ok = true;
        for(int b = 63; b >= 0; b--) {
            uint32_t hi = B[j + (63 - b) * 2];
            if(sf_inr(hi, 728, 20))      word |= (uint64_t)1 << b;
            else if(sf_inr(hi, 312, 25)) { /* zero bit */ }
            else { ok = false; break; }
        }
        if(!ok) continue;

        uint32_t serial = (uint32_t)(word >> 32);
        uint16_t btnMask = (uint16_t)((word >> 16) & 0xFFFF);
        uint16_t rx_ck = (uint16_t)(word & 0xFFFF);
        uint16_t c = (uint16_t)((serial ^ (serial >> 16)) ^ btnMask);
        if(rx_ck != (uint16_t)(~c)) continue;
        if(serial == 0) continue;

        r->addr = serial;  r->cnt = 0;  r->hop = btnMask;
        r->btn = (uint8_t)(btnMask & 0xFF);  r->rolling = false;
        r->replay_vuln = true;  r->te_us = 312;  r->bits = 64;
        r->freq_mhz = buf->freq_mhz;  r->predict_window = 0;
        strncpy(r->proto, "Hyundai-RIO", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note),
                 "fixed code — clone/replay exposed");
        return true;
    }
    return false;
}

/* ── KIA V7 — 64-bit Manchester, inverted wire, 0x4C header + CRC-8 ───────── */
/* KIA V7 uses Manchester half-symbols near 250 µs. The on-air bits are the
   complement of a 64-bit logical key with a fixed 0x4C top byte. The first
   nibble (0x4) is carried by the preamble/data sync, not by a Manchester cell;
   the payload therefore carries only the low 60 bits. Restore its complement,
   0xB, before checking the key.
   Recovered bytes: [0]=0x4C header, [1..2]=counter,
   [3..6]=28-bit serial plus 4-bit button, [7]=CRC-8.
   CRC-8 uses polynomial 0x7F and initial value 0x4C over bytes 0..6. This
   checksum is the gate for the secondary path under KIA/Hyundai Auto. */
static uint8_t kv7_crc8(const uint8_t* d, int n) {
    uint8_t crc = 0x4C;
    for(int i = 0; i < n; i++) {
        crc ^= d[i];
        for(int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x7F) : (uint8_t)(crc << 1);
    }
    return crc;
}

static bool kia_v7_manch(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    uint32_t cands[4];
    int nc = 0;
    uint32_t te0 = buf->te_us;
    if(te0 >= 150 && te0 <= 350) cands[nc++] = te0;
    cands[nc++] = 250; cands[nc++] = 220; cands[nc++] = 280;

    int gapstart = 0;
    for(int i = 0; i + 1 < buf->len; i++)
        if(buf->durations[i] > 3000) { gapstart = i + 1; break; }

    for(int t = 0; t < nc; t++) {
        uint32_t te = cands[t];
        if(te < 150 || te > 350) continue;
        int starts[2] = { 0, gapstart };
        for(int si = 0; si < 2; si++) {
            int st = starts[si];
            if(st < 0 || st >= buf->len) continue;
            for(int phase = 0; phase < 2; phase++) {
                uint8_t raw[OEM_MAX_BYTES];
                int nb = oem_manch_extract(buf, te, st, phase, 128, raw);
                if(nb < 60) continue;

                for(int j = 0; j + 60 <= nb; j++) {
                    uint64_t m = 0;
                    for(int k = 0; k < 60; k++) {
                        int bit = (raw[(j + k) >> 3] >> (7 - ((j + k) & 7))) & 1;
                        m = (m << 1) | (uint64_t)bit;
                    }
                    uint64_t decode_data = ((uint64_t)0xB << 60) | m;
                    uint64_t key = ~decode_data;
                    uint8_t by[8];
                    for(int i = 0; i < 8; i++) by[i] = (uint8_t)(key >> (56 - 8 * i));
                    if(by[0] != 0x4C) continue;
                    if(kv7_crc8(by, 7) != by[7]) continue;

                    uint32_t serial = (((uint32_t)by[3]) << 20) | (((uint32_t)by[4]) << 12) |
                                      (((uint32_t)by[5]) << 4) | (((uint32_t)by[6]) >> 4);
                    serial &= 0x0FFFFFFFu;
                    if(serial == 0) continue;
                    uint16_t cnt = ((uint16_t)by[1] << 8) | by[2];
                    uint8_t btn = by[6] & 0x0F;

                    r->addr = serial;  r->cnt = cnt;  r->hop = cnt;
                    r->btn = btn;  r->rolling = true;  r->te_us = te;  r->bits = 64;
                    r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
                    r->predict_lo = (cnt + 1) & 0xFFFF;
                    r->predict_hi = (cnt + 8) & 0xFFFF;
                    strncpy(r->proto, "Kia-V7", sizeof(r->proto) - 1);
                    snprintf(r->predict_note, sizeof(r->predict_note),
                             "hdr 4C CRC OK sn=%07lX", (unsigned long)serial);
                    return true;
                }
            }
        }
    }
    return false;
}

/* ── KIA/Hyundai — V0 (64-bit PWM CRC-8) + Santa Fe (80-bit CRC-8/0x31) +
      RIO (64-bit fixed, 16-bit checksum) + V7 (Manchester CRC-8) ───────────── */
bool flipper_decode_kia(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 64) return false;
    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        /* V0 uses roughly 250/500 µs PWM. A wider TE range, especially below
           200 µs, let the 1-in-256 CRC match Chrysler/VW noise during the
           combined TE, start-offset, and burst search. */
        if(te < 200 || te > 550) continue;
        int starts[2] = { oem_pwm_data_start(buf, te), 0 };
        for(int s = 0; s < 2; s++) {
            if(!oem_pwm_extract(buf, te, starts[s], 64, raw)) continue;
            KiaV0Frame f;
            if(!kia_v0_parse(raw, 64, &f)) continue;
            /* An all-zero frame passes this CRC because its checksum is zero.
               A zero serial also appeared in misaligned non-KIA PWM such as
               VW Golf4, so reject that result as well. */
            if(f.serial == 0) continue;
            r->addr = f.serial;  r->cnt = f.counter;  r->hop = f.counter;
            r->btn = f.button;  r->rolling = true;  r->te_us = te;  r->bits = 64;
            r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
            r->predict_lo = (f.counter + 1) & 0xFFFF;
            r->predict_hi = (f.counter + 8) & 0xFFFF;
            strncpy(r->proto, "KIA/Hyundai", sizeof(r->proto) - 1);
            return true;
        }
    }

    /* Second path: Santa Fe / Solaris TRW 80-bit CRC-8 (0x31). */
    if(santafe_pwm(buf, r)) return true;
    memset(r, 0, sizeof(*r));  r->freq_mhz = buf->freq_mhz;
    /* Third path: V7 Manchester (0x4C header + CRC-8). */
    if(kia_v7_manch(buf, r)) return true;
    memset(r, 0, sizeof(*r));  r->freq_mhz = buf->freq_mhz;
    /* Fourth path: early RIO 64-bit fixed code. */
    if(hkr_pwm(buf, r)) return true;
    memset(r, 0, sizeof(*r));  r->freq_mhz = buf->freq_mhz;
#ifndef FLIPPER_FAP_SLIM
    /* Fifth path: Kia V0. It expects at least 16 short/short
       preamble pairs, a long start bit, then a 60-bit word with a 0xF preamble
       and CRC-8 (polynomial 0x7F). That CRC alone is not selective enough with
       the wide ±40% TE search: Suzuki_v0_kd and other PWM noise still match.
       Keep this path force-only until a tighter TE or multi-frame gate has
       been calibrated. */
    if(!flipper_decode_forced()) return false;
    {
        const uint32_t* B = buf->durations;
        int cnt = buf->len;
        const uint32_t TE_S = 250, TE_L = 500, TE_D = 100;
        for(int si = 0; si + 30 < cnt; si++) {
            if(!sf_inr(B[si], TE_S, 40)) continue;
            int j = si, hc = 0;
            while(j + 1 < cnt && sf_inr(B[j], TE_S, 40) && sf_inr(B[j + 1], TE_S, 40)) {
                hc++; j += 2;
            }
            if(hc <= 20) continue;
            if(j + 1 >= cnt) continue;
            if(!sf_inr(B[j], TE_L, 40) || !sf_inr(B[j + 1], TE_L, 40)) continue;
            j += 2;
            if(j + 118 > cnt) continue;
            uint64_t data = (uint64_t)1 << 59;
            bool ok = true;
            int b;
            for(b = 58; b >= 0; b--) {
                if(j + 1 >= cnt) { ok = false; break; }
                uint32_t hi = B[j]; j++;
                if(hi >= TE_L + 2 * TE_D) break;
                j++;
                if(sf_inr(hi, TE_L, 40)) data |= (uint64_t)1 << b;
                else if(sf_inr(hi, TE_S, 40)) { /* zero */ }
                else { ok = false; break; }
            }
            if(!ok || b != -1) continue;
            if(((data >> 56) & 0xF) != 0xF) continue;
            uint16_t ctr = (uint16_t)((data >> 40) & 0xFFFF);
            uint32_t serial = (uint32_t)((data >> 12) & 0x0FFFFFFF);
            uint8_t btn = (uint8_t)((data >> 8) & 0x0F);
            uint8_t rx_crc = (uint8_t)(data & 0xFF);
            uint8_t cb[6] = {
                (uint8_t)(data >> 48), (uint8_t)(data >> 40),
                (uint8_t)(data >> 32), (uint8_t)(data >> 24),
                (uint8_t)(data >> 16), (uint8_t)(data >> 8),
            };
            uint8_t crc = 0x00;
            for(int i = 0; i < 6; i++) {
                crc ^= cb[i];
                for(int k = 0; k < 8; k++)
                    crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x7F) : (uint8_t)(crc << 1);
            }
            if(crc != rx_crc || serial == 0) continue;
            if(btn != 0x1 && btn != 0x2 && btn != 0x4 && btn != 0x8) continue;
            r->addr = serial; r->cnt = ctr; r->hop = ctr;
            r->btn = btn; r->rolling = true; r->te_us = TE_S; r->bits = 64;
            r->freq_mhz = buf->freq_mhz; r->predict_window = 256;
            r->predict_lo = (ctr + 1) & 0xFFFF;
            r->predict_hi = (ctr + 8) & 0xFFFF;
            strncpy(r->proto, "Kia-V0", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "hdr F CRC7F sn=%07lX", (unsigned long)serial);
            return true;
        }
    }
#endif
    return false;
}

/* ── VAG pre-2004 (VW/Audi/Seat/Skoda, ID48 era) — 64-bit PWM rolling ────── */
/* The pre-2004 VAG ID48 path reads 64 MSB-first PWM bits from HIGH widths:
   about 550 µs for 1 and 250 µs for 0, after an 11000 µs LOW sync.
   Layout: [transponder ID 32][counter 16][button 8][checksum 8].
   The checksum is the bitwise inverse of the 8-bit sum of the first 7 bytes.
   It gates this secondary path under VAG Auto; it does not validate receiver
   acceptance. */
static bool vag_pwm_id48(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    for(int si = 1; si + 128 < cnt; si++) {
        if(!sf_inr(B[si], 11000, 20)) continue;
        if(!sf_inr(B[si - 1], 550, 25)) continue;
        int j = si + 1;
        uint64_t word = 0;
        bool ok = true;
        for(int b = 63; b >= 0; b--) {
            uint32_t hi = B[j + (63 - b) * 2];
            if(sf_inr(hi, 550, 25))      word |= (uint64_t)1 << b;
            else if(sf_inr(hi, 250, 25)) { /* zero bit */ }
            else { ok = false; break; }
        }
        if(!ok) continue;

        uint32_t tid = (uint32_t)(word >> 32);
        uint16_t ctr = (uint16_t)((word >> 16) & 0xFFFF);
        uint8_t btn = (uint8_t)((word >> 8) & 0xFF);
        uint8_t rx_ck = (uint8_t)(word & 0xFF), s = 0;
        s += (tid >> 24) & 0xFF; s += (tid >> 16) & 0xFF;
        s += (tid >> 8) & 0xFF;  s += tid & 0xFF;
        s += (ctr >> 8) & 0xFF;  s += ctr & 0xFF; s += btn;
        if(rx_ck != (uint8_t)(~s)) continue;
        if(tid == 0) continue;

        r->addr = tid;  r->cnt = ctr;  r->hop = ctr;
        r->btn = btn;  r->rolling = true;  r->te_us = 550;  r->bits = 64;
        r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
        r->predict_lo = (uint32_t)(ctr + 1) & 0xFFFF;
        r->predict_hi = (uint32_t)(ctr + 8) & 0xFFFF;
        strncpy(r->proto, "VAG-ID48", sizeof(r->proto) - 1);
        snprintf(r->predict_note, sizeof(r->predict_note), "ID48 PWM cnt=%u", (unsigned)ctr);
        return true;
    }
    return false;
}

/* Land Rover/Jaguar V0 carries an 81-bit plaintext differential-Manchester
   frame. A 3-bit count-parity check, a 16-bit near-constant tail (0xFFFF or
   0x7FFF depending on count parity), and a final 1 bit form a structural
   consistency gate. They support Auto classification; they do not authenticate
   the sender or demonstrate receiver acceptance.

   Short and long half-symbols are about 250 and 500 µs. A long run of
   short-HIGH/short-LOW pairs precedes the sync: 750 µs HIGH, 750 µs LOW, then
   a 250 µs boundary pulse. The transmitter uses 319 preamble pairs, so the
   full preamble plus payload exceeds the 256-edge capture buffer. The decoder
   uses at least 8 pairs for clock alignment, then relies on the check and tail
   to reject noise.
 */
#define LR_S            250u
#define LR_L            500u
#define LR_DELTA        100u
#define LR_SYNC         750u
#define LR_SYNC_DELTA   120u
#define LR_SIG_UNLOCK   0xA285E3UL
#define LR_SIG_LOCK     0xC20363UL
#define LR_MIN_PREAMBLE 8

static int lr_short(uint32_t d) {
    uint32_t x = d > LR_S ? d - LR_S : LR_S - d;
    return x < LR_DELTA;
}
static int lr_long(uint32_t d) {
    uint32_t x = d > LR_L ? d - LR_L : LR_L - d;
    return x < LR_DELTA;
}
static int lr_sync(uint32_t d) {
    uint32_t x = d > LR_SYNC ? d - LR_SYNC : LR_SYNC - d;
    return x < LR_SYNC_DELTA;
}

/* Compute the 3-bit check from the 9-bit count using its linear parity taps. */
static uint8_t lr_calc_check(uint32_t c) {
    uint8_t c0 = ((c >> 1) ^ (c >> 2) ^ (c >> 3) ^ (c >> 4) ^ (c >> 6)) & 1;
    uint8_t c1 = ((c >> 0) ^ (c >> 2) ^ (c >> 3) ^ (c >> 4) ^ (c >> 5) ^ (c >> 6) ^ 1) & 1;
    uint8_t c2 = ((c >> 1) ^ (c >> 3) ^ (c >> 4) ^ (c >> 5) ^ (c >> 6)) & 1;
    return (uint8_t)(c0 | (c1 << 1) | (c2 << 2));
}
/* Select the 16-bit tail from one parity bit of the count. */
static uint16_t lr_calc_tail(uint32_t c) {
    int msb = (((c >> 0) ^ (c >> 2) ^ (c >> 4) ^ (c >> 5)) & 1) != 0;
    return msb ? 0xFFFF : 0x7FFF;
}

typedef struct {
    uint8_t raw[10];
    int bit_count;
    int extra_bit;
    int previous_bit;
    int boundary_pad_skipped;
    int pending_short;
} LrState;

static int lr_add_bit(LrState* s, int bit) {
    if(s->bit_count < 80) {
        if(bit) s->raw[s->bit_count / 8] |= (uint8_t)(0x80 >> (s->bit_count % 8));
        s->bit_count++;
    } else if(s->bit_count == 80) {
        s->extra_bit = bit;
        s->bit_count++;
    } else {
        return 0;
    }
    return 1;
}

/* Track differential-Manchester transitions using the state-machine rules. */
static int lr_transition(LrState* s, int level, uint32_t d) {
    if(!s->boundary_pad_skipped) {
        if(level && lr_short(d)) {
            s->boundary_pad_skipped = 1;
            return 1; /* consume the short-HIGH boundary pulse after sync */
        }
        s->boundary_pad_skipped = 1; /* fall through */
    }
    if(s->pending_short) {
        if(!s->previous_bit && !level && lr_short(d)) {
            s->pending_short = 0;
            return lr_add_bit(s, 0);
        }
        if(s->previous_bit && level && lr_short(d)) {
            s->pending_short = 0;
            return lr_add_bit(s, 1);
        }
        return 0;
    }
    if(!s->previous_bit) {
        if(level && lr_long(d)) {
            s->previous_bit = 1;
            return lr_add_bit(s, 1);
        }
        if(level && lr_short(d)) {
            s->pending_short = 1;
            return 1;
        }
        return 0;
    }
    if(!level && lr_long(d)) {
        s->previous_bit = 0;
        return lr_add_bit(s, 0);
    }
    if(!level && lr_short(d)) {
        s->pending_short = 1;
        return 1;
    }
    return 0;
}

bool flipper_decode_land_rover(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 40) return false;
    const uint32_t* p = buf->durations;
    int n = buf->len;
    int step = 0; /* 0 Reset, 1 PreLow, 2 PreHigh, 3 SyncLow, 4 Data */
    int preamble = 0;
    LrState s;
    memset(&s, 0, sizeof(s));

    for(int i = 0; i < n; i++) {
        int level = ((i & 1) == 0); /* durations[0] is HIGH; parity == level */
        uint32_t d = p[i];
        switch(step) {
        case 0:
            if(level && lr_short(d)) {
                preamble = 0;
                step = 1;
            }
            break;
        case 1:
            if(!level && lr_short(d)) {
                preamble++;
                step = 2;
            } else {
                step = 0;
            }
            break;
        case 2:
            if(level && lr_short(d)) {
                step = 1;
            } else if(level && lr_sync(d) && preamble >= LR_MIN_PREAMBLE) {
                step = 3;
            } else {
                step = 0;
            }
            break;
        case 3:
            if(!level && lr_sync(d)) {
                memset(&s, 0, sizeof(s));
                s.previous_bit = 1;
                lr_add_bit(&s, 1); /* implicit leading '1' carried by the sync */
                step = 4;
            } else {
                step = 0;
            }
            break;
        case 4:
            if(!lr_transition(&s, level, d)) {
                step = 0;
                break;
            }
            if(s.bit_count == 81) {
                uint64_t key = 0;
                for(int k = 0; k < 8; k++) key = (key << 8) | s.raw[k];
                uint16_t tail = (uint16_t)((s.raw[8] << 8) | s.raw[9]);
                uint32_t count = ((uint32_t)s.raw[6] << 1) | ((s.raw[7] >> 7) & 1);
                uint8_t exp_check = lr_calc_check(count);
                uint16_t exp_tail = lr_calc_tail(count);
                int check_ok = ((s.raw[7] & 0x78) == 0) && ((s.raw[7] & 0x07) == exp_check);
                int tail_ok = (tail == exp_tail) && s.extra_bit;
                if(check_ok && tail_ok) {
                    uint32_t sig = ((uint32_t)s.raw[0] << 16) | ((uint32_t)s.raw[1] << 8) | s.raw[2];
                    uint32_t serial =
                        ((uint32_t)s.raw[3] << 16) | ((uint32_t)s.raw[4] << 8) | s.raw[5];
                    uint8_t btn =
                        (sig == LR_SIG_UNLOCK) ? 0x04 : (sig == LR_SIG_LOCK) ? 0x02 : 0x00;
                    r->addr = serial;
                    r->cnt = count;
                    r->hop = (uint32_t)(key & 0xFFFFFFFF);
                    r->btn = btn;
                    r->rolling = true;
                    r->te_us = LR_S;
                    r->bits = 81;
                    r->freq_mhz = buf->freq_mhz;
                    r->predict_window = 256;
                    r->predict_lo = (count + 1) & 0x1FF;
                    r->predict_hi = (count + 8) & 0x1FF;
                    strncpy(r->proto, "LandRover-V0", sizeof(r->proto) - 1);
                    snprintf(
                        r->predict_note,
                        sizeof(r->predict_note),
                        "sig=%06lX cnt=%u chk",
                        (unsigned long)sig,
                        (unsigned)count);
                    return true;
                }
                step = 0; /* not valid -> keep scanning for a later repeat */
            }
            break;
        default: step = 0; break;
        }
    }
    return false;
}

/* ── VAG — Manchester, 0xAF3F/0xAF1C preamble gate, AUT64/XTEA ───────────── */
bool flipper_decode_vag(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 64) return false;
    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 150 || te > 900) continue;
        for(int phase = 0; phase < 2; phase++) {
            if(oem_manch_extract(buf, te, 0, phase, 64, raw) < 64) continue;
            VagFrame f;
            if(!vag_parse_frame(raw, 64, &f)) continue;  /* preamble-gated */
            r->addr = f.serial;  r->cnt = f.counter;  r->hop = f.counter;
            r->btn = f.button;  r->rolling = true;  r->te_us = te;  r->bits = 64;
            r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
            r->predict_lo = (f.counter + 1) & 0xFFF;
            r->predict_hi = (f.counter + 8) & 0xFFF;
            strncpy(r->proto, "VAG", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "%s", f.brand ? f.brand : vag_type_name(f.type));
            return true;
        }
    }

    /* Second path: pre-2004 ID48 64-bit PWM rolling. */
    return vag_pwm_id48(buf, r);
}

/* BMW CAS3/CAS4 PPM is classified structurally, not decrypted. Marks are
   roughly 250 µs; spaces near 500 µs encode 0 and 1500 µs encode 1. Real X5
   captures place a sync of at least 10 ms before the data, often across the
   inter-burst gap that FlipperPulseBuf omits. The decoder therefore looks for
   a contiguous run of at least 64 bits. This timing pattern is not a protocol
   authentication check, and the AES payload remains opaque from RF alone. */
bool flipper_decode_bmw(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 130) return false;
    const uint32_t* B = buf->durations;
    int cnt = buf->len;
    int best_nb = 0;
    uint8_t best_frame[32];
    memset(best_frame, 0, sizeof(best_frame));

    /* Require two sync intervals of at least 10 ms before the 64-bit run.
       Without them, constant-mark PPM has falsely matched VW Polo and similar. */
    for(int si = 0; si + 4 < cnt; si++) {
        if(B[si] < 10000 || B[si + 1] < 10000) continue;
        int di = si + 2;
        uint8_t frame[32];
        memset(frame, 0, sizeof(frame));
        int nb = 0, j = di;
        while(j + 1 < cnt && nb < 256) {
            uint32_t hi = B[j], lo = B[j + 1];
            if(!sf_inr(hi, 250, 60)) break;
            int bit;
            if(sf_inr(lo, 500, 40)) bit = 0;
            else if(sf_inr(lo, 1500, 30)) bit = 1;
            else break;
            frame[nb / 8] |= (uint8_t)(bit << (7 - (nb % 8)));
            nb++;
            j += 2;
        }
        if(nb >= 64 && nb > best_nb) {
            best_nb = nb;
            memcpy(best_frame, frame, sizeof(frame));
        }
    }
    if(best_nb < 64) return false;

    r->addr = ((uint32_t)best_frame[0] << 24) | ((uint32_t)best_frame[1] << 16) |
              ((uint32_t)best_frame[2] << 8) | best_frame[3];
    r->cnt = 0;
    r->hop = ((uint32_t)best_frame[4] << 24) | ((uint32_t)best_frame[5] << 16) |
             ((uint32_t)best_frame[6] << 8) | best_frame[7];
    r->btn = 0;
    r->rolling = true;
    r->te_us = 250;
    r->bits = best_nb;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "BMW-CAS3-PPM", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "structural %d-bit PPM (AES opaque)", best_nb);
    return true;
}

/* ── PSA (Peugeot/Citroen) — Manchester, XOR mode 0x23 + checksum ─────────── */
bool flipper_decode_psa(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 128) return false;
    uint32_t tes[3];
    int ntes = oem_te_candidates(buf, tes, 3);
    uint8_t raw[OEM_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 120 || te > 900) continue;
        for(int phase = 0; phase < 2; phase++) {
            if(oem_manch_extract(buf, te, 0, phase, 128, raw) < 128) continue;
            PsaFrame f;
            /* Decode Mode 0x23 only: its XOR and checksum are fast enough for
               this path. Mode 0x36 requires a 2^24 TEA search, too expensive
               for a per-capture callback. */
            if(!psa_decrypt_mode23(raw, 16, &f)) continue;
            if(f.serial == 0 && f.counter == 0) continue;
            r->addr = f.serial;  r->cnt = f.counter;  r->hop = f.counter;
            r->btn = f.button;  r->rolling = true;  r->te_us = te;  r->bits = 128;
            r->freq_mhz = buf->freq_mhz;  r->predict_window = 256;
            r->predict_lo = (f.counter + 1) & 0xFFFF;
            r->predict_hi = (f.counter + 8) & 0xFFFF;
            strncpy(r->proto, "PSA", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "%s", f.function ? f.function : "PSA");
            return true;
        }
    }
    return false;
}
