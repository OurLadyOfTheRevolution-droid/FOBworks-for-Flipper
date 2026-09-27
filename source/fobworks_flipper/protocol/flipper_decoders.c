#include "flipper_decoders.h"
#include "flipper_scratch.h"
#include "flipper_keeloq.h"
#include "flipper_honda.h"
#include "flipper_subaru.h"
#include "flipper_suzuki.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── TE estimator — k=2 coherence pass ──────────────────────────────────── */
uint32_t flipper_estimate_te(const uint32_t* buf, int n) {
    if(!buf || n < 8) return 0;

    /* 16-bit counts: a capture cannot fill one bucket past 65535. */
    uint16_t* hist = flipper_scratch_b(sizeof(uint16_t) * 512);
    if(!hist) return 0;
    memset(hist, 0, sizeof(uint16_t) * 512);

    for(int i = 0; i < n; i++) {
        uint32_t v = buf[i];
        if(v < 75 || v > 16383) continue;
        uint32_t bucket = v >> 5;
        if(bucket < 512) hist[bucket]++;
    }

    /* Find peak */
    uint32_t peak_buck = 0;
    uint16_t peak_cnt = 0;
    for(int b = 1; b < 512; b++) {
        if(hist[b] > peak_cnt) { peak_cnt = hist[b]; peak_buck = b; }
    }
    if(peak_cnt < 4) return 0;

    /* TE estimate = centre of peak bucket */
    uint32_t te = (peak_buck << 5) + 16;

    /* Reject implausible values */
    if(te < 100 || te > 4000) return 0;
    return te;
}

/* ── KeeLoq decoder ──────────────────────────────────────────────────────── */
bool flipper_decode_keeloq(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->te_us == 0) return false;

    /* Try up to 3 TE candidates (te, te±15%) */
    uint32_t te_candidates[3] = {
        buf->te_us,
        buf->te_us * 85 / 100,
        buf->te_us * 115 / 100,
    };

    for(int c = 0; c < 3; c++) {
        uint32_t te = te_candidates[c];
        if(te < 100 || te > 4000) continue;

        char* bits = flipper_scratch_a(0, 600);
        if(!bits) continue;
        uint16_t blen = kl_pwm(buf->durations, buf->len, te, bits);
        if(blen < 66) continue;

        KLFrame f;
        int got = 0;
        int hcs300 = 0;
        if(kl_parse(bits, blen, &f)) {
            /* Reject all-ones / all-zeros serial — common PWM-noise artefact. */
            if(f.sn != 0 && f.sn != 0x0FFFFFFFu && f.enc != 0xFFFFFFFFu) {
                got = 1;
                uint8_t btn4  = f.btn & 0xF;
                uint8_t disc4 = f.disc & 0xF;
                uint32_t expected_lo = (btn4 << 8) | (disc4 << 4) | (f.ovf << 3);
                hcs300 = ((f.enc & 0xFFF) == expected_lo);
            }
        }
        if(!got) {
            /* Manchester-decode attempt (some clones emit Manchester KeeLoq). */
            char* klm = flipper_scratch_a(600, 600);
            if(!klm) continue;
            uint16_t ml = 0;
            for(int i = 0; i + 1 < blen; i += 2) {
                if     (bits[i]=='0' && bits[i+1]=='1') klm[ml++]='1';
                else if(bits[i]=='1' && bits[i+1]=='0') klm[ml++]='0';
            }
            klm[ml] = '\0';
            if(ml >= 66 && kl_parse(klm, ml, &f) &&
               f.sn != 0 && f.sn != 0x0FFFFFFFu && f.enc != 0xFFFFFFFFu) {
                got = 1;
                uint32_t expected_lo =
                    ((f.btn & 0xF) << 8) | ((f.disc & 0xF) << 4) | (f.ovf << 3);
                hcs300 = ((f.enc & 0xFFF) == expected_lo);
            }
        }
        /* kl_parse already required 66 bits, a nonzero hop, and a nonzero
           button. The HCS300 low-12 compare is against ciphertext, so a real
           Honda frame misses it; use it only to upgrade the label. A single
           function button (lock/unlock/trunk/panic) is the Auto gate. */
        if(!got) continue;

        uint8_t btn4 = f.btn & 0xF;
        int one_btn = (btn4 == 1 || btn4 == 2 || btn4 == 4 || btn4 == 8);
        if(!hcs300 && !one_btn && !flipper_decode_forced()) continue;

        kl_recover_key(&f);
        const char* label = hcs300 ? "KeeLoq-HCS300" : "KeeLoq";

        r->addr   = f.sn;
        r->cnt    = f.cnt;
        r->hop    = f.enc;
        r->btn    = f.btn;
        r->te_us  = te;
        r->bits   = blen;
        r->rolling = true;
        r->freq_mhz = buf->freq_mhz;
        strncpy(r->proto, label, sizeof(r->proto) - 1);
        strncpy(r->mfr_name, f.mfr_name, sizeof(r->mfr_name) - 1);
        strncpy(r->device_key_hex, f.device_key_hex, sizeof(r->device_key_hex) - 1);
        r->predict_window = f.predict_window;
        r->predict_lo     = f.predict_lo;
        r->predict_hi     = f.predict_hi;
        if(f.mfr_name[0] && f.device_key_hex[0]) {
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "OK=next hop  Key:%s", f.mfr_name);
        } else if(r->rolling) {
            /* Structural decode only — counter window is advisory, not synth. */
            r->predict_window = 256;
            r->predict_lo = (f.cnt + 1) & 0xFFFF;
            r->predict_hi = (f.cnt + 16) & 0xFFFF;
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "window only — need key");
            r->device_key_hex[0] = '\0';
            r->mfr_name[0] = '\0';
        }
        return true;
    }
    return false;
}

/* ── Security+ 1.0 decoder ───────────────────────────────────────────────── */
/*
 * LiftMaster / Chamberlain Security+ 1.0 (315 MHz OOK, 40-bit tribit).
 * Preamble: 9 pairs of 1T HIGH + 1T LOW.  Data: tribit encoding
 *   '0' = T HIGH + 2T LOW;  '1' = 2T HIGH + T LOW.
 * Frame: [9-bit sync][10-bit fixed][7-bit button][10-bit rolling][4-bit checksum]
 * TE ≈ 500 µs.
 */
bool flipper_decode_secplus1(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 300 || te > 800) return false;

    const uint32_t* p = buf->durations;
    int n = buf->len;

    /* Look for 9-pair preamble (total ≈ 2T per pair) */
    uint32_t thr_tot = te + (te >> 1);   /* 1.5T */
    int preamble_end = -1;
    for(int i = 0; i + 1 < n - 40; i += 2) {
        int pc = 0, j = i;
        while(j + 1 < n && p[j] + p[j+1] <= thr_tot) { pc++; j += 2; }
        if(pc >= 9) { preamble_end = j; break; }
    }
    if(preamble_end < 0) return false;

    /* Extract tribits */
    uint8_t tribits[40];
    int tb = 0;
    for(int i = preamble_end; i + 1 < n && tb < 40; i += 2) {
        uint32_t hi = p[i]; (void)p[i+1];
        uint32_t hi_t = (hi * 10 / te);
        if     (hi_t <= 15) tribits[tb++] = 0;   /* 1T hi = bit 0 */
        else if(hi_t <= 25) tribits[tb++] = 1;   /* 2T hi = bit 1 */
        else return false;
    }
    if(tb < 40) return false;

    /* Reconstruct 40-bit word */
    uint64_t word = 0;
    for(int i = 0; i < 40; i++)
        if(tribits[i]) word |= (1ULL << i);

    uint32_t fixed   = (uint32_t)((word >>  0) & 0x3FF);
    uint32_t btn     = (uint32_t)((word >> 10) & 0x7F);
    uint32_t rolling = (uint32_t)((word >> 17) & 0x3FF);
    uint32_t csum    = (uint32_t)((word >> 27) & 0xF);

    /* 4-bit rolling-checksum nibble: popcount of
       (rolling XOR fixed XOR btn) over its low 13 bits, masked to 4 bits.
       A mismatch means corruption / wrong framing — reject the frame. */
    uint32_t csum_calc = 0;
    {
        uint32_t x = ((rolling & 0x3FF) ^ (fixed & 0x3FF) ^ (btn & 0x7F)) & 0x1FFF;
        while(x) { csum_calc += (uint32_t)(x & 1u); x >>= 1; }
    }
    if((csum_calc & 0xF) != (csum & 0xF)) {
        /* Checksum mismatch — not a genuine Sec+1.0 frame. */
        return false;
    }

    /* Minimal button validation — at least one bit set */
    if(btn == 0 && rolling == 0) return false;

    r->addr    = fixed;
    r->cnt     = rolling;
    r->hop     = (uint32_t)(word & 0xFFFFFFFF);
    r->btn     = (uint8_t)btn;
    r->rolling = true;
    r->te_us   = te;
    r->bits    = 40;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 256;
    r->predict_lo     = (rolling + 1) & 0x3FF;
    r->predict_hi     = (rolling + 8) & 0x3FF;
    strncpy(r->proto, "Security+1.0", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "LiftMaster Sec+ 1.0  fixed=0x%03X", (unsigned)fixed);
    (void)csum;
    return true;
}

/* ── Security+ 2.0 decoder ───────────────────────────────────────────────── */
/*
 * LiftMaster / Chamberlain Security+ 2.0 (310 / 315 / 390 MHz, ternary).
 * Preamble: 40× short pulse.  Encoding: 3 levels per symbol.
 * Frame: 62 symbols → 39 data bits.
 */
/*
 * Attempt a full Security+ 2.0 ternary decode with one explicit TE guess.
 * Returns true and fills `r` on success.  Kept separate from the public
 * entry so the caller can retry across several TE candidates — the histogram
 * TE estimate can lock ~15% low, which pushes a genuine 3T symbol past the
 * per-symbol break threshold and drops the whole frame.
 */
static bool secplus2_try_te(const FlipperPulseBuf* buf, FlipperDecodeResult* r, uint32_t te) {
    if(te < 100 || te > 600) return false;

    /* Find a long gap (>= 5T) marking start of data packet */
    int data_start = -1;
    for(int i = 1; i < buf->len; i++) {
        if(buf->durations[i] >= te * 5) { data_start = i + 1; break; }
    }
    if(data_start < 0 || data_start + 80 > buf->len) return false;

    /* Decode ternary symbols: T→0, 2T→1, 3T→2.
     * Classify by nearest ideal boundary (1.5T / 2.5T) and gate the outer
     * range at 3.5T so a slightly-off TE still resolves a real 3T symbol. */
    uint8_t syms[62];
    int sc = 0;
    for(int i = data_start; i + 1 < buf->len && sc < 62; i += 2) {
        uint32_t total = buf->durations[i] + buf->durations[i+1];
        if     (total <= te + (te>>1))              syms[sc++] = 0;
        else if(total <= (te*2) + (te>>1))          syms[sc++] = 1;
        else if(total <= (te*3) + (te>>1))          syms[sc++] = 2;
        else break;
    }
    if(sc < 62) return false;

    /* Convert 62 ternary → 39 binary (base-3 decoding, simplified) */
    uint64_t raw = 0;
    for(int i = 0; i < 39; i++) {
        if(i < sc) raw |= ((uint64_t)(syms[i] & 1)) << i;
    }

    uint32_t addr = (uint32_t)((raw >> 0)  & 0x1FFFF);
    uint32_t cnt  = (uint32_t)((raw >> 17) & 0xFFF);
    uint8_t  btn  = (uint8_t)((raw >> 29) & 0xF);
    /* Reject empty / all-ones / no-button frames — Sec+2 has no checksum, so
       these are the only structural guards against random ternary noise. */
    if(addr == 0 || addr == 0x1FFFF) return false;
    if(btn == 0 || btn == 0xF) return false;
    /* Require some ternary diversity (not a run of identical symbols). */
    {
        int nz = 0;
        for(int i = 1; i < 62; i++) if(syms[i] != syms[0]) nz++;
        if(nz < 8) return false;
    }

    r->addr    = addr;
    r->cnt     = cnt;
    r->hop     = (uint32_t)(raw & 0xFFFFFFFF);
    r->btn     = btn;
    r->rolling = true;
    r->te_us   = te;
    r->bits    = 39;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 512;
    r->predict_lo     = (r->cnt + 1) & 0xFFF;
    r->predict_hi     = (r->cnt + 8) & 0xFFF;
    strncpy(r->proto, "Security+2.0", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note), "LiftMaster Sec+ 2.0");
    return true;
}

bool flipper_decode_secplus2(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;

    /* Candidate TEs, tried in order.  The global histogram estimate is
     * unreliable for a ternary signal: the short (1T) and long (3T) symbol
     * halves can out-vote the true symbol width, so buf->te_us often locks
     * onto ~T/2 or ~1.5T instead of T.  The Sec+ 2.0 preamble is a run of
     * ~40 equal-width pulses at exactly T, so re-derive T from that leading
     * run first, then fall back to the histogram value and scaled variants. */
    uint32_t cands[6];
    int nc = 0;

    /* Preamble-derived TE: mode of the leading run of similar-width pulses. */
    if(buf->len >= 8) {
        uint32_t base = buf->durations[0];
        if(base >= 75) {
            uint64_t sum = 0; int cnt = 0;
            for(int i = 0; i < buf->len && i < 48; i++) {
                uint32_t d = buf->durations[i];
                /* stop at the first pulse well outside the preamble band
                 * (the long start gap or the first data symbol) */
                if(d * 4 < base * 3 || d * 3 > base * 4) break;
                sum += d; cnt++;
            }
            if(cnt >= 6) cands[nc++] = (uint32_t)(sum / cnt);
        }
    }

    uint32_t te0 = buf->te_us;
    if(te0 >= 100 && te0 <= 600) {
        cands[nc++] = te0;                 /* histogram estimate */
        cands[nc++] = (te0 * 115) / 100;   /* estimate locked ~15% low  */
        cands[nc++] = (te0 * 130) / 100;   /* estimate locked ~T/2 high count */
        cands[nc++] = (te0 * 2);           /* estimate locked onto T/2 */
        cands[nc++] = (te0 * 2) / 3;       /* estimate locked onto 1.5T */
    }

    for(int k = 0; k < nc; k++) {
        if(secplus2_try_te(buf, r, cands[k])) return true;
    }
    return false;
}

/* ── CAME 12-bit decoder ─────────────────────────────────────────────────── */
/*
 * CAME 12-bit fixed code, OOK, 433.92 MHz.
 * Preamble: long HIGH ~12T, then 12 Manchester-style bits.
 * TE ≈ 320 µs.  Code word = 12 bits (4095 combinations).
 */
bool flipper_decode_came12(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 200 || te > 500) return false;

    /* Find leader: HIGH ≥ 8T */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i++) {
        /* leader HIGH in 8T..40T window (rejects huge sync pulses) */
        /* TPMS sensor leaders are much longer (~20T at 300 MHz); cap here */
        if(buf->durations[i] >= te * 8 && buf->durations[i] <= te * 40 &&
           buf->durations[i+1] <= te * 2)
            { ds = i + 2; break; }
    }
    if(ds < 0 || ds + 24 > buf->len) return false;

    uint16_t code = 0;
    int ok = 1;
    for(int b = 0; b < 12 && ds + 1 < buf->len; b++, ds += 2) {
        uint32_t hi = buf->durations[ds];
        /* each pair must be a sane 0.5T..3T HIGH / <=3T LOW bit cell */
        if(hi < (te >> 1) || hi > (te * 3)) { ok = 0; break; }
        if(buf->durations[ds + 1] > (te * 3)) { ok = 0; break; }
        if(hi > te + (te >> 1)) code |= (1u << b);  /* 2T HIGH = '1' */
    }
    if(!ok || code == 0) return false;
    /* a real CAME frame ends shortly after bit 12; long bit-tails (TPMS,
       Holtek, ...) must fall through to their own decoders */
    if(buf->len - ds > 8) return false;

    r->addr    = code;
    r->cnt     = 0;
    r->hop     = code;
    r->btn     = 1;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 12;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "CAME-12", sizeof(r->proto) - 1);
    return true;
}

/* ── Nice FLO decoder ────────────────────────────────────────────────────── */
/*
 * Nice FLO 12-bit fixed code, OOK, 433.92 MHz.
 * Similar leader structure to CAME but inverted polarity.
 * TE ≈ 500 µs.
 */
bool flipper_decode_nice_flo(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 300 || te > 700) return false;

    /* Leader: LOW ≥ 24T followed by a short HIGH */
    int ds = -1;
    for(int i = 1; i + 1 < buf->len; i++) {
        if(buf->durations[i] >= te * 20 && buf->durations[i+1] <= te * 2)
            { ds = i + 2; break; }
    }
    if(ds < 0 || ds + 24 > buf->len) return false;

    uint16_t code = 0;
    for(int b = 0; b < 12 && ds + 1 < buf->len; b++, ds += 2) {
        uint32_t hi = buf->durations[ds];
        if(hi < te - (te >> 2)) code |= (1u << b);  /* short HIGH = '1' */
    }

    r->addr    = code;
    r->cnt     = 0;
    r->hop     = code;
    r->btn     = 1;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 12;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "Nice-FLO", sizeof(r->proto) - 1);
    return true;
}

/* ── FAAC SLH decoder ────────────────────────────────────────────────────── */
bool flipper_decode_faac_slh(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 250 || te > 380) return false;

    /* FAAC "SLH" (self-learning hopping): a fixed 22-bit code following a
       preamble of ~2T equal-width pairs.  A bit is '1' when its HIGH exceeds
       ~1.5T, so anchor the read on a preamble run (each pair's HIGH and LOW
       both <= 1.5T) instead of trusting an absolute edge index. */
    uint32_t thr = te + (te >> 1);   /* ~1.5T bit threshold */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i += 2) {
        int run = 0;
        while(i + run + 1 < buf->len &&
              buf->durations[i + run]     <= thr &&
              buf->durations[i + run + 1] <= thr)
            run += 2;
        if(run / 2 >= 8) { ds = i + run; break; }   /* >=8-pair preamble */
    }
    if(ds < 0 || ds + 44 > buf->len) return false;   /* need room for 22 bits */

    /* 22-bit SLH word, LSB-first, TE-classified */
    uint32_t addr = 0;
    for(int b = 0; b < 22; b++) {
        int idx = ds + 2 * b;
        if(idx + 1 >= buf->len) return false;
        if(buf->durations[idx] > thr) addr |= (1u << b);
    }
    if(addr == 0) return false;

    r->addr    = addr;
    r->cnt     = 0;
    r->hop     = addr;
    r->btn     = 1;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 22;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "FAAC-SLH", sizeof(r->proto) - 1);
    return true;
}

/* ── DoorHan / AN-Motors 2FSK decoder ───────────────────────────────────── */
bool flipper_decode_doorhan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 500 || te > 1500) return false;
    if(buf->len < 80) return false;

    /* DoorHan Rolling: 64-bit KeeLoq-variant at 433.92 MHz 2FSK.
       Simplified: extract upper 32 bits as addr, lower 32 as hop. */
    uint32_t addr = 0, hop = 0;
    for(int i = 0; i < 32 && i + 1 < buf->len; i += 2)
        if(buf->durations[i] > te + (te>>1)) addr |= (1u << (i>>1));
    for(int i = 32; i < 64 && i + 1 < buf->len; i += 2)
        if(buf->durations[i] > te + (te>>1)) hop  |= (1u << ((i-32)>>1));

    if(addr == 0 && hop == 0) return false;

    r->addr    = addr;
    r->cnt     = hop & 0xFFFF;
    r->hop     = hop;
    r->btn     = 1;
    r->rolling = true;
    r->te_us   = te;
    r->bits    = 64;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 256;
    r->predict_lo = (r->cnt + 1) & 0xFFFF;
    r->predict_hi = (r->cnt + 8) & 0xFFFF;
    strncpy(r->proto, "DoorHan-Rolling", sizeof(r->proto) - 1);
    return true;
}

/* ── Ansonic / clones decoder ────────────────────────────────────────────── */
bool flipper_decode_ansonic(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 400 || te > 800) return false;
    if(buf->len < 72) return false;

    /* Ansonic-12 / Prastel / clones: 12-bit OOK fixed code.
       Bit encoding: '0'=1T HIGH + 2T LOW; '1'=2T HIGH + 1T LOW. */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i++)
        if(buf->durations[i] >= te * 10) { ds = i + 1; break; }
    if(ds < 0 || ds + 36 > buf->len) return false;

    uint16_t code = 0;
    for(int b = 0; b < 12 && ds + 1 < buf->len; b++, ds += 3) {
        if(buf->durations[ds] > te + (te>>1)) code |= (1u << b);
    }

    r->addr    = code;
    r->cnt     = 0;
    r->hop     = code;
    r->btn     = 1;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 12;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "Ansonic-12", sizeof(r->proto) - 1);
    return true;
}

/* ── Linear-10 (gate/barrier) decoder ───────────────────────────────────── */
/* Guarded: reject degenerate words and cap bit count to prevent catch-all.  */
bool flipper_decode_linear10(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 700 || te > 1300) return false;
    if(buf->len < 24 || buf->len > 80) return false;

    uint16_t word = 0;
    for(int i = 0; i < 10 && i + 1 < buf->len; i++) {
        if(buf->durations[i * 2] > te + (te >> 1)) word |= (1u << i);
    }
    /* Reject all-zero and all-one (degenerate noise) */
    if(word == 0 || word == 0x3FF) return false;
    /* strict shape: every pulse must be a sane bit/filler cell; anything
       far outside 0.25T..4T at an even index (or any huge leader) means this
       is another protocol — refuse rather than mis-tag. */
    for(int i = 0; i < buf->len; i++) {
        uint32_t v = buf->durations[i];
        if(v > te * 4 && (i % 2 == 0 || i > 19)) return false;
    }

    r->addr    = word;
    r->cnt     = 0;
    r->hop     = word;
    r->btn     = 1;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 10;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "Linear-10", sizeof(r->proto) - 1);
    return true;
}

/* ── Holtek HT6P20 decoder ───────────────────────────────────────────────── */
bool flipper_decode_holtek(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 100 || te > 600) return false;
    if(buf->len < 80) return false;

    /* HT6P20: 20-bit address + 4-bit data, OOK, ~315/433 MHz.
       Encoding: '0'=1T HIGH + 2T LOW; '1'=2T HIGH + 1T LOW. */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i++)
        if(buf->durations[i] >= te * 24) { ds = i + 1; break; }
    if(ds < 0 || ds + 72 > buf->len) return false;

    uint32_t addr = 0;
    uint8_t  data = 0;
    for(int b = 0; b < 20 && ds + 1 < buf->len; b++, ds += 3)
        if(buf->durations[ds] > te + (te>>1)) addr |= (1u << b);
    for(int b = 0; b < 4 && ds + 1 < buf->len; b++, ds += 3)
        if(buf->durations[ds] > te + (te>>1)) data |= (1u << b);

    if(addr == 0) return false;

    r->addr    = addr;
    r->cnt     = 0;
    r->hop     = addr;
    r->btn     = data;
    r->rolling = false;
    r->te_us   = te;
    r->bits    = 24;
    r->freq_mhz = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "Holtek-HT6P20", sizeof(r->proto) - 1);
    return true;
}

/* ── Beninca XOR Type-1 decoder ─────────────────────────────────────────── */
/*
 * Beninca and compatible brands use KeeLoq with an XOR-Type-1 diversification
 * on the manufacturer key (per @li0ard/keeloq analysis).  The physical bit
 * encoding is identical to HCS300 — kl_pwm + kl_parse + kl_recover_key with
 * the xor-type1 derived keys in the sweep table handles these automatically.
 * This decoder is an alias for keeloq with a renamed proto label.
 */
bool flipper_decode_beninca(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!flipper_decode_keeloq(buf, r)) return false;
    /* Only relabel if key recovery identified a xor-type1 variant */
    if(strstr(r->mfr_name, "xor-type1"))
        strncpy(r->proto, "KeeLoq-Beninca", sizeof(r->proto) - 1);
    return true;
}

/* ── PT2262 / PT2272 fixed-code decoder ──────────────────────────────────── */
/*
 * Holtek PT2262 (433.92/315 MHz OOK) and the ubiquitous Chinese PT2272 clones:
 * unencrypted fixed code → replay & clone vulnerable (flipper_decode result
 * carries replay_vuln).  Frame: sync HIGH ≥16T, then fixed + data bits at
 * bit time ≈ T with bit='1' when HIGH > 1.5T.  Accept 12–24 data bits (4-bit,
 * 8-bit, 12-bit and 20-bit+ buttons) — the word is reported as-is.
 */
bool flipper_decode_pt2262(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 40 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = flipper_estimate_te(buf->durations, buf->len);
    uint32_t te = base;
    if(te < 70 || te > 2200) return false;
    if(buf->len >= 8) {          /* refine on leading-pair mean */
        uint32_t mean = ((buf->durations[0] + buf->durations[1]) + (te ? te : buf->durations[0])) / 2;
        if(mean >= 70 && mean <= 2200) te = mean;
    }

    int ds = -1;                  /* sync HIGH ≥16T */
    for(int i = 0; i < buf->len && ds < 0; i++)
        if(buf->durations[i] >= (te << 4)) ds = i + 1;
    if(ds < 0) return false;

    int avail = (buf->len - ds + 1) / 2;
    int bits = avail > 24 ? 24 : avail;
    if(bits < 12) return false;   /* minimum plausible fixed code */

    uint32_t code = 0;
    for(int b = 0; b < bits && ds + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        if(hi > te + (te >> 1)) code |= (1u << b);
    }
    if(code == 0) return false;   /* all-zero is noise */

    r->addr        = code & 0xFFFFFF;
    r->cnt         = 0;
    r->hop         = code;
    r->btn         = (uint8_t)(code & 0xF);
    r->rolling     = false;
    r->replay_vuln = true;        /* fixed, unencrypted → trivially replayable */
    r->te_us       = te;
    r->bits        = bits;
    r->freq_mhz    = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "PT2262", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "fixed code — clone/replay exposed");
    return true;
}

/* ── EV1527 / HS1527 / RT1527 fixed-code decoder ──────────────────────────── */
/*
 * EV1527 (and HS1527 / RT1527 / SC1527) gate & garage fixed code, 315/433 MHz
 * OOK.  Same shape as PT2262 with a shorter sync (≥4T, no huge leader) — kept
 * distinct so captures of EV1527 fobs are labelled honestly.  Fixed code →
 * replay & clone vulnerable.
 */
bool flipper_decode_ev1527(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 32 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = flipper_estimate_te(buf->durations, buf->len);
    uint32_t te = base;
    if(te < 70 || te > 2200) return false;
    if(buf->len >= 8) {
        uint32_t mean = ((buf->durations[0] + buf->durations[1]) + te) / 2;
        if(mean >= 70 && mean <= 2200) te = mean;
    }

    int ds = -1;                  /* first pulse after any long gap (≥8T) */
    for(int i = 1; i + 2 < buf->len && ds < 0; i++) {
        if(buf->durations[i - 1] >= (te << 3)) ds = i;
    }
    if(ds < 0) ds = 0;

    int avail = (buf->len - ds + 1) / 2;
    int bits = avail > 20 ? 20 : avail;
    if(bits < 10) return false;

    /* every consumed pair must be a sane bit cell: HIGH >=0.75T and <=3T,
       LOW <=2.5T — tiny preamble pulses and giant leaders are refused */
    for(int b = 0; b < bits && ds + 2 * b + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        uint32_t lo = buf->durations[ds + 2 * b + 1];
        if(hi < (te * 3 / 4) || hi > (te * 3) || lo > (te * 5 / 2)) return false;
    }
    /* Sec+1.0-shaped preamble fingerprint: >=5 consecutive pairs whose total
       <=1.7T (its preamble is 1.5T/pair; EV1527 bit pairs are >=2T). */
    {
        int pc = 0;
        for(int b = 0; b < bits && ds + 2 * b + 1 < buf->len; b++) {
            uint32_t tot = buf->durations[ds + 2 * b] + buf->durations[ds + 2 * b + 1];
            if(tot <= (te * 17 / 10)) pc++; else break;
        }
        if(pc >= 5) return false;
    }
    uint32_t code = 0;
    for(int b = 0; b < bits && ds + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        if(hi > te + (te >> 1)) code |= (1u << b);
    }
    if(code == 0) return false;

    r->addr        = code;
    r->cnt         = 0;
    r->hop         = code;
    r->btn         = (uint8_t)(code & 0xF);
    r->rolling     = false;
    r->replay_vuln = true;
    r->te_us       = te;
    r->bits        = bits;
    r->freq_mhz    = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "EV1527", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "HS1527/RT1527-family fixed — clone/replay exposed");
    return true;
}

/* ── Tire Pressure Monitoring System (TPMS) decoder ──────────────────────── */
/*
 * Generic 20-bit-class TPMS frame (Hitag-AES / ID-style), 315/433 MHz:
 * sync HIGH ≥24T, then 20-bit sensor id + 4 status bits.  No crypto here —
 * the ID is enough for vehicle→sensor correlation, and the frame is raw
 * (replayable to a listener that trusts the ID).  FOBtpms streams these; the
 * auto dispatcher only claims them when the structure is unambiguous.
 */
bool flipper_decode_tpms(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 56 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = flipper_estimate_te(buf->durations, buf->len);
    uint32_t te = base;
    if(te < 120 || te > 1600) return false;

    int ds = -1;
    for(int i = 0; i + 40 < buf->len; i++) {
        if(buf->durations[i] >= (te << 4) &&
           buf->durations[i + 1] <= (te << 2)) { ds = i + 2; break; }
    }
    if(ds < 0) return false;

    uint32_t word = 0;
    for(int b = 0; b < 20 && ds + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        if(hi > te + (te >> 1)) word |= (1u << b);
    }
    if(word == 0) return false;

    uint8_t status = 0;
    for(int b = 0; b < 4 && ds + 40 + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 40 + 2 * b];
        if(hi > te + (te >> 1)) status |= (1u << b);
    }

    r->addr        = word;
    r->cnt         = 0;
    r->hop         = (word << 4) | status;
    r->btn         = status;
    r->rolling     = false;
    r->replay_vuln = true;        /* raw id frame → spoofable to listeners */
    r->te_us       = te;
    r->bits        = 24;
    r->freq_mhz    = buf->freq_mhz;
    r->predict_window = 0;
    strncpy(r->proto, "TPMS", sizeof(r->proto) - 1);
    snprintf(r->predict_note, sizeof(r->predict_note),
             "sensor id 0x%05X status %u", (unsigned)word, (unsigned)status);
    return true;
}
/* ── Scher-Khan / Magicar PWM car-alarm remote ───────────────────────────── */
/*
 * Scher-Khan (Magicar) alarm fobs, 433.92 MHz FM.  Symmetric PWM: after a
 * header of >=2 long HIGH pulses (~1500 µs = 2T) and a short start bit, each
 * data bit is a HIGH+LOW pair that is BOTH short (~750 µs = 0) or BOTH long
 * (~1100 µs = 1); a long HIGH (>=~1420 µs) is the stop bit.  There is no
 * transmitted checksum, so this decoder is force-only (never in the Auto chain).
 * The 51-bit "MAGIC CODE, Dynamic" carries serial/button/rolling-counter; other
 * recognized lengths (35 static, 57/63/64/81/82 responses) are reported by
 * length only.
 */
static int sk_near(uint32_t v, uint32_t ref) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d <= 160;                              /* te_delta */
}

bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    const uint32_t S = 750, L = 1100;            /* te_short, te_long */
    const uint32_t* p = buf->durations;
    int n = buf->len;

    for(int h = 0; h + 1 < n; h += 2) {
        /* Header: run of long HIGH (~2T) pulses at even (HIGH) indices. */
        int hc = 0, j = h;
        while(j + 1 < n && sk_near(p[j], S * 2)) { hc++; j += 2; }
        if(hc < 2) continue;
        /* Start bit: a short HIGH after the header. */
        if(!(j + 1 < n && sk_near(p[j], S))) continue;

        int k = j + 2;                            /* first data cell */
        uint64_t data = 0;
        int count = 1;                            /* start bit counts as bit 1 */
        bool ok = true, stop = false;
        while(k < n) {
            uint32_t hi = p[k];
            if(hi >= L + 320) { stop = true; break; }   /* long HIGH = stop bit */
            if(k + 1 >= n) { ok = false; break; }       /* need a LOW to pair */
            uint32_t lo = p[k + 1];
            if(sk_near(hi, S) && sk_near(lo, S)) {
                data = (data << 1);               /* both short → 0 */
                count++;
            } else if(sk_near(hi, L) && sk_near(lo, L)) {
                data = (data << 1) | 1ULL;        /* both long → 1 */
                count++;
            } else {
                ok = false;
                break;
            }
            k += 2;
            if(count > 64) { ok = false; break; }
        }
        if(!ok || !stop || count < 35) continue;

        memset(r, 0, sizeof(*r));
        r->freq_mhz = buf->freq_mhz;
        r->te_us = S;
        r->bits = count;
        r->hop = (uint32_t)data;
        strncpy(r->proto, "Scher-Khan", sizeof(r->proto) - 1);

        if(count == 51) {                         /* MAGIC CODE, Dynamic */
            r->addr = (uint32_t)(((data >> 24) & 0xFFFFFF0) | ((data >> 20) & 0x0F));
            r->btn = (uint8_t)((data >> 24) & 0x0F);
            r->cnt = (uint32_t)(data & 0xFFFF);
            r->rolling = true;
            r->predict_window = 256;
            r->predict_lo = (r->cnt + 1) & 0xFFFF;
            r->predict_hi = (r->cnt + 8) & 0xFFFF;
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "Magicar Dynamic  sn=0x%07lX", (unsigned long)r->addr);
        } else {
            r->replay_vuln = (count == 35);       /* static code → replayable */
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "Magicar %d-bit", count);
        }
        return true;
    }
    return false;
}

/* ── Counter-delta prediction ────────────────────────────────────────────── */
void flipper_kl_predict(FlipperDecodeResult* r, const FlipperDecodeResult* r2) {
    if(!r) return;
    if(!r->rolling || r->predict_window == 0) {
        if(!r->predict_note[0])
            snprintf(r->predict_note, sizeof(r->predict_note), "Fixed code — no prediction");
        return;
    }
    if(r2 && r2->rolling && r2->addr == r->addr) {
        /* Two-capture delta */
        uint32_t delta = (r2->cnt - r->cnt) & 0xFFFF;
        if(delta > 0 && delta < 256) {
            r->predict_lo = (r2->cnt + 1) & 0xFFFF;
            r->predict_hi = (r2->cnt + delta * 2) & 0xFFFF;
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "Delta=%u  next=%u..%u", (unsigned)delta, (unsigned)r->predict_lo, (unsigned)r->predict_hi);
            return;
        }
    }
    r->predict_lo = (r->cnt + 1) & 0xFFFF;
    r->predict_hi = (r->cnt + 16) & 0xFFFF;
    snprintf(r->predict_note, sizeof(r->predict_note),
             "Single capture  next~%u..%u", (unsigned)r->predict_lo, (unsigned)r->predict_hi);
}

/* ── Top-level decoder dispatcher ────────────────────────────────────────── */
/*
 * Priority order matters:
 * 1. KeeLoq — must run first; its strict structural fingerprint means it
 *    should not false-positive on other protocols.
 * 2. Security+ — also has tight structural constraints.
 * 3. Everything else — looser guards; ordered from most-constrained to least.
 */
/* ── Decoder registry — single source of truth ───────────────────────────── */
/* Array order is the Auto priority chain (strong OEM CRC parsers first;
 * KeeLoq next; structured gate remotes; fixed-code catch-alls are force-only).
 * auto_safe == false entries are reachable only via a forced selection. */
const FlipperDecoderReg FLIPPER_DECODERS[] = {
    /* Strong OEM parsers first: each gated by a real CRC/checksum/preamble, so
       they never claim a non-OEM frame — and placing them ahead of KeeLoq means
       a genuine GM/Ford/… PWM frame is claimed by its own parser instead of
       being misread as a 66-bit KeeLoq hop (identical OOK-PWM wire encoding). */
    { FlipperForceGm,       "GM",         flipper_decode_gm,       true  },
    { FlipperForceFord,     "Ford",       flipper_decode_ford,     true  },
    { FlipperForceChrysler, "Chrysler",   flipper_decode_chrysler, true  },
    /* Suzuki before KIA: Kia-V0 preamble/CRC can otherwise
       latch onto Suzuki PWM before the Suzuki CRC-8 path runs. */
    { FlipperForceSuzuki,   "Suzuki",     flipper_decode_suzuki,   true  },
    { FlipperForceKia,      "KIA/Hyundai",flipper_decode_kia,      true  },
    { FlipperForceVag,      "VAG",        flipper_decode_vag,      true  },
    /* PSA Mode 0x23 is XOR + 8-bit sum — too weak for Auto (claims Buick/VW
       Manchester garbage).  Real Groupe PSA harness files never
       matched this path; keep force-only until a stronger gate is calibrated. */
    { FlipperForcePsa,      "PSA",        flipper_decode_psa,      false },
    { FlipperForceMazda,    "Mazda",      flipper_decode_mazda,    true  },
    { FlipperForceSubaru,   "Subaru",     flipper_decode_subaru,   true  },
    { FlipperForceHondaKr5, "Honda KR5",  flipper_decode_honda_kr5,true  },
    { FlipperForceLandRover,"Land Rover", flipper_decode_land_rover,true },
    /* BMW CAS3/CAS4 PPM: constant ~250 µs mark + 500/1500 µs spaces, ≥64 bits.
       Sync (≥10 ms) is often the inter-burst gap and not in the pulse buf —
       accept data-only runs.  Structural only (AES payload not decryptable). */
    { FlipperForceBmw,      "BMW CAS",    flipper_decode_bmw,      true  },
    /* Fiat V1 (XOR checksum) / V2 (hdr 0001) — Auto-safe; V0 stays force-only
       inside the same decoder via flipper_decode_forced().  Before KeeLoq so
       FCA Pacifica/Jeep Manchester is not misread as a 66-bit hop. */
    { FlipperForceFiat,      "Fiat",       flipper_decode_fiat,       true  },
    /* KeeLoq after OEM: HCS300 fingerprint, plus multi-frame key recovery. */
    { FlipperForceKeeloq,   "KeeLoq",     flipper_decode_keeloq,   true  },
    { FlipperForceSecplus1, "Sec+ 1.0",   flipper_decode_secplus1, true  },
    /* ── no-checksum / weak structural — force-only (never in Auto) ──────── */
    { FlipperForceSecplus2,  "Sec+ 2.0",   flipper_decode_secplus2,  false },
    { FlipperForceCame12,    "CAME",       flipper_decode_came12,    false },
    { FlipperForceNiceFlo,   "Nice FLO",   flipper_decode_nice_flo,  false },
    { FlipperForceAnsonic,   "Ansonic",    flipper_decode_ansonic,   false },
    { FlipperForceLinear10,  "Linear",     flipper_decode_linear10,  false },
    { FlipperForceHoltek,    "Holtek",     flipper_decode_holtek,    false },
    { FlipperForcePt2262,    "PT2262",     flipper_decode_pt2262,    false },
    { FlipperForceEv1527,    "EV1527",     flipper_decode_ev1527,    false },
    { FlipperForceTpms,      "TPMS",       flipper_decode_tpms,      false },
    { FlipperForceFaacSlh,   "FAAC SLH",   flipper_decode_faac_slh,  false },
    { FlipperForceDoorhan,   "DoorHan",    flipper_decode_doorhan,   false },
    /* ── provisional / no-checksum OEM — force-only, never in Auto ───────── */
    { FlipperForceHonda,     "Honda RKE",  flipper_decode_honda,      false },
    { FlipperForceScherKhan, "Scher-Khan", flipper_decode_scher_khan, false },
    { FlipperForceToyota,    "Toyota RKE", flipper_decode_toyota,     false },
    { FlipperForceNissan,    "Nissan RKE", flipper_decode_nissan,     false },
};
const int FLIPPER_DECODER_COUNT =
    (int)(sizeof(FLIPPER_DECODERS) / sizeof(FLIPPER_DECODERS[0]));

const char* flipper_force_proto_name(FlipperForceProto f) {
    if(f == FlipperForceAuto) return "Auto";
    for(int i = 0; i < FLIPPER_DECODER_COUNT; i++)
        if(FLIPPER_DECODERS[i].id == f) return FLIPPER_DECODERS[i].name;
    return "Auto";
}

static bool s_decode_forced;

bool flipper_decode_forced(void) { return s_decode_forced; }

bool flipper_decode_ex(const FlipperPulseBuf* buf, FlipperDecodeResult* result,
                       FlipperForceProto force) {
    if(!buf || !result) return false;
    memset(result, 0, sizeof(*result));
    result->freq_mhz = buf->freq_mhz;
    s_decode_forced = (force != FlipperForceAuto);

    /* Forced single-protocol mode — parse only as the user's selection. */
    if(force != FlipperForceAuto) {
        for(int i = 0; i < FLIPPER_DECODER_COUNT; i++)
            if(FLIPPER_DECODERS[i].id == force)
                return FLIPPER_DECODERS[i].fn(buf, result);
        return false;
    }

    /* Auto: walk the registry in priority order, auto_safe decoders only. */
    for(int i = 0; i < FLIPPER_DECODER_COUNT; i++) {
        if(!FLIPPER_DECODERS[i].auto_safe) continue;
        if(FLIPPER_DECODERS[i].fn(buf, result)) return true;
        /* a failed decoder may have scribbled the result — reset for the next */
        memset(result, 0, sizeof(*result));
        result->freq_mhz = buf->freq_mhz;
    }
    return false;
}

bool flipper_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* result) {
    return flipper_decode_ex(buf, result, FlipperForceAuto);
}

/* ── Custom firmware protocol registry hooks ─────────────────────────────── */
#ifdef FLIPPER_UNLEASHED_INTEGRATION
#include <lib/subghz/subghz_environment.h>

void flipper_register_unleashed_protocols(SubGhzEnvironment* env) {
    /* Custom firmware uses subghz_environment_add_protocol().
       Flipper decoders wrap into SubGhzProtocol objects defined in separate
       translation units (not shipped here — instantiate from the
       firmware protocol template when building inside the firmware tree). */
    (void)env;
}
#endif
