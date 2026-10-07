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
        if(v < FLIPPER_MIN_PULSE_US || v > 16383) continue;
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
        /* All-zero and all-one serials commonly come from PWM noise. */
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
        /* kl_parse checks frame length and rejects an empty hop or button. The
           HCS300 low-12 test compares ciphertext, so Honda frames may not pass;
           use it only to refine the label. Auto also requires a single known
           function button (lock, unlock, trunk, or panic). */
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
            /* Without a recovered key, this window is advisory; it cannot
               synthesize a valid next code. */
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

/* ── Security+ 1.0 / 2.0 ─────────────────────────────────────────────────── */
/*
 * Full parsers live in flipper_secplus.c. The device host FAP keeps only thin
 * force-plugin stubs so that .text stays under the loader cap; fw_force.fal
 * (and host tests) link the real file. Auto stays off either way.
 */
#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
bool flipper_decode_secplus1(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceSecplus1);
}
bool flipper_decode_secplus2(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceSecplus2);
}
#endif

/* ── CAME 12-bit decoder ─────────────────────────────────────────────────── */
/*
 * CAME's 12-bit fixed code uses OOK at 433.92 MHz, with a long HIGH leader
 * followed by 12 Manchester-style bits. Typical TE is about 320 µs; the
 * 12-bit word has 4095 nonzero values.
 */
bool flipper_decode_came12(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 200 || te > 500) return false;

    /* Find leader: HIGH ≥ 8T */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i++) {
        /* Bound the leader to 8T..40T to reject very long sync pulses, including
           the longer leaders used by some TPMS frames. */
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
    /* CAME frames end shortly after bit 12. Let longer TPMS and Holtek frames
       continue to their own decoders. */
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
 * Nice FLO is a 12-bit OOK fixed code at 433.92 MHz. Its leader resembles
 * CAME's but uses the opposite polarity; typical TE is about 500 µs.
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

    /* FAAC SLH carries a fixed 22-bit word after a preamble of roughly 2T
       equal-width pairs. A HIGH longer than about 1.5T represents a 1. Find
       the preamble by checking both pulse widths rather than relying on a
       fixed edge index. */
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

    /* This simplified 64-bit 2FSK parser treats the first 32 bits as the
       address and the next 32 as the hop. It does not decrypt the KeeLoq
       variant used by DoorHan. */
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

    /* Ansonic-12, Prastel, and related remotes use a 12-bit OOK fixed code:
       '0' = 1T HIGH + 2T LOW; '1' = 2T HIGH + 1T LOW. */
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
/* Reject degenerate words and implausible pulse sequences to avoid matching
   unrelated signals as a Linear-10 frame. */
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
    /* Require each pulse to fit a bit or filler cell. A pulse outside 0.25T..4T
       at an even index, or any very long leader, indicates another protocol. */
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

    /* HT6P20 frames carry a 20-bit address and 4-bit data over OOK at about
       315 or 433 MHz: '0' = 1T HIGH + 2T LOW; '1' = 2T HIGH + 1T LOW. */
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
 * Beninca and compatible remotes use KeeLoq with XOR-Type-1 manufacturer-key
 * diversification (per @li0ard/keeloq analysis). Their pulse format is handled
 * by the KeeLoq decoder and its key sweep; this wrapper only assigns the
 * Beninca label when key recovery identifies that variant.
 */
bool flipper_decode_beninca(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!flipper_decode_keeloq(buf, r)) return false;
    /* Change the label only when recovery identified an XOR-Type-1 key. */
    if(strstr(r->mfr_name, "xor-type1"))
        strncpy(r->proto, "KeeLoq-Beninca", sizeof(r->proto) - 1);
    return true;
}

/* ── PT2262 / PT2272 fixed-code decoder ──────────────────────────────────── */
/*
 * PT2262 and compatible PT2272 remotes send unencrypted fixed codes over OOK
 * at 315 or 433.92 MHz, so captured codes can be replayed or cloned. The frame
 * starts with a HIGH sync of at least 16T, followed by 12–24 data bits; a bit
 * is 1 when HIGH exceeds 1.5T. The decoder reports the received word unchanged.
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
 * EV1527-family gate and garage remotes (including HS1527, RT1527, and SC1527)
 * use OOK fixed codes at 315 or 433 MHz. Their pulse shape resembles PT2262,
 * but the sync is shorter (at least 4T) and has no long leader. Keep this
 * decoder separate so captures retain the EV1527 label; fixed codes can be
 * replayed or cloned.
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

    /* Check each pair's shape: HIGH must be 0.75T..3T and LOW no longer than
       2.5T. This excludes short preamble pulses and oversized leaders. */
    for(int b = 0; b < bits && ds + 2 * b + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        uint32_t lo = buf->durations[ds + 2 * b + 1];
        if(hi < (te * 3 / 4) || hi > (te * 3) || lo > (te * 5 / 2)) return false;
    }
    /* Reject the Security+ 1.0 preamble: it has at least five pairs totaling
       <=1.7T, while EV1527 data pairs are at least 2T. */
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
 * This generic parser handles 315/433 MHz TPMS-style frames with a HIGH sync
 * of at least 24T, a 20-bit sensor ID, and four status bits. It does not
 * decrypt the frame; it reports the raw ID for correlation. A listener that
 * trusts that ID may accept a replay, so Auto uses this decoder only when the
 * frame structure is unambiguous.
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
#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
/* Device FAP keeps Scher-Khan, Toyota, and Nissan out of the host image.
   Force-decode maps fw_force.fal and runs the parsers there. Host tests
   compile the real protocol/*.c files instead of these stubs. */
bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceScherKhan);
}
bool flipper_decode_toyota(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceToyota);
}
bool flipper_decode_nissan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceNissan);
}
#endif

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
 * Keep the Auto priority order in the registry below. Prefer parsers with
 * stronger checks ahead of broader structural matches to limit false positives.
 */
/* ── Decoder registry — single source of truth ───────────────────────────── */
/* This array is the Auto priority chain: parsers with OEM checks first, then
 * KeeLoq and other structured protocols. Broad fixed-code parsers are force-
 * only. Entries with auto_safe == false run only when explicitly selected. */
const FlipperDecoderReg FLIPPER_DECODERS[] = {
    /* Run OEM parsers before KeeLoq. Their CRC, checksum, or preamble checks
       reduce false matches; the shared OOK-PWM encoding could otherwise let a
       GM, Ford, or similar frame look like a KeeLoq hop. */
    { FlipperForceGm,       "GM",         flipper_decode_gm,       true  },
    { FlipperForceFord,     "Ford",       flipper_decode_ford,     true  },
    { FlipperForceChrysler, "Chrysler",   flipper_decode_chrysler, true  },
    /* Check Suzuki first: the Kia-V0 preamble and CRC can also match Suzuki
       PWM, so this order gives the Suzuki CRC-8 parser first chance. */
    { FlipperForceSuzuki,   "Suzuki",     flipper_decode_suzuki,   true  },
    { FlipperForceKia,      "KIA/Hyundai",flipper_decode_kia,      true  },
    { FlipperForceVag,      "VAG",        flipper_decode_vag,      true  },
    /* PSA Mode 0x23 has only XOR and an 8-bit sum, which also matched unrelated
       Buick/VW Manchester captures in testing. Keep it force-only until its
       checks are stronger; it did not match the available Groupe PSA captures. */
    { FlipperForcePsa,      "PSA",        flipper_decode_psa,      false },
    { FlipperForceMazda,    "Mazda",      flipper_decode_mazda,    true  },
    { FlipperForceSubaru,   "Subaru",     flipper_decode_subaru,   true  },
    { FlipperForceHondaKr5, "Honda KR5",  flipper_decode_honda_kr5,true  },
    { FlipperForceLandRover,"Land Rover", flipper_decode_land_rover,true },
    /* BMW CAS3/CAS4 uses PPM with ~250 µs marks and 500/1500 µs spaces. The
       >=10 ms sync may be an inter-burst gap outside the pulse buffer, so the
       parser accepts data-only runs. This is structural; the AES payload is
       not decrypted. */
    { FlipperForceBmw,      "BMW CAS",    flipper_decode_bmw,      true  },
    /* V1 uses an XOR checksum and V2 has header 0001; V0 is force-only. Run
       these before KeeLoq so FCA Pacifica/Jeep Manchester frames get their
       parser before the shared 66-bit hop shape is considered. */
    { FlipperForceFiat,      "Fiat",       flipper_decode_fiat,       true  },
    /* KeeLoq follows the OEM parsers; it can also recover keys across frames. */
    { FlipperForceKeeloq,   "KeeLoq",     flipper_decode_keeloq,   true  },
    /* Sec+ 1.0 now matches the public ternary/OOK format (argilo/secplus,
       Flipper secplus_v1, rtl_433). It still has no transmitted checksum, so
       it stays force-only until a live capture set measures false positives. */
    { FlipperForceSecplus1, "Sec+ 1.0",   flipper_decode_secplus1, false },
    /* These parsers lack a checksum or rely on weaker structural checks, so
       they run only when explicitly selected. */
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
    /* These OEM layouts are provisional or have no checksum; keep them out of
       Auto mode. */
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

    /* Forced mode runs only the decoder the caller selected. */
    if(force != FlipperForceAuto) {
        for(int i = 0; i < FLIPPER_DECODER_COUNT; i++)
            if(FLIPPER_DECODERS[i].id == force)
                return FLIPPER_DECODERS[i].fn(buf, result);
        return false;
    }

    /* Auto mode tries eligible decoders in registry order. */
    for(int i = 0; i < FLIPPER_DECODER_COUNT; i++) {
        if(!FLIPPER_DECODERS[i].auto_safe) continue;
        if(FLIPPER_DECODERS[i].fn(buf, result)) return true;
        /* A failed parser may have partially filled the result. Clear it before
           trying the next decoder. */
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
    /* Firmware integration registers protocols through
       subghz_environment_add_protocol(). The SubGhzProtocol wrappers live in
       separate translation units and must be instantiated from the firmware
       protocol template when building inside the firmware tree. */
    (void)env;
}
#endif
