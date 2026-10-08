#include "flipper_decoders.h"
#include "flipper_scratch.h"
#include <stdio.h>
#include <string.h>

/* Force-only fixed-code and structural parsers. On the device these live in
   fw_force.fal so the slim host stays under the loader .text cap. Host tests
   compile this file directly. Beninca stays in flipper_decoders.c because it
   wraps KeeLoq recovery.

   TE estimation is local: FALs cannot call the host flipper_estimate_te(). */

static uint32_t fixed_estimate_te(const uint32_t* buf, int n) {
    if(!buf || n < 8) return 0;
    uint16_t* hist = flipper_scratch_b(sizeof(uint16_t) * 512);
    if(!hist) return 0;
    memset(hist, 0, sizeof(uint16_t) * 512);
    for(int i = 0; i < n; i++) {
        uint32_t v = buf[i];
        if(v < FLIPPER_MIN_PULSE_US || v > 16383) continue;
        uint32_t bucket = v >> 5;
        if(bucket < 512) hist[bucket]++;
    }
    uint32_t peak_buck = 0;
    uint16_t peak_cnt = 0;
    for(int b = 1; b < 512; b++) {
        if(hist[b] > peak_cnt) { peak_cnt = hist[b]; peak_buck = (uint32_t)b; }
    }
    if(peak_cnt < 4) return 0;
    uint32_t te = (peak_buck << 5) + 16;
    if(te < 100 || te > 4000) return 0;
    return te;
}

/* ── CAME 12-bit decoder ─────────────────────────────────────────────────── */
/* * CAME's 12-bit fixed code uses OOK at 433.92 MHz, with a long HIGH leader * followed by 12 Manchester-style bits. Typical TE is about 320 µs; the * 12-bit word has 4095 nonzero values. */
bool flipper_decode_came12(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 200 || te > 500) return false;

    /* I find leader: HIGH ≥ 8T */
    int ds = -1;
    for(int i = 0; i + 1 < buf->len; i++) {
        /* I bound the leader to 8T..40T to reject very long sync pulses, including the longer leaders I see in some TPMS frames. */
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
    /* CAME frames end shortly after bit 12. I let longer TPMS and Holtek frames continue to their own decoders. */
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
/* * Nice FLO is a 12-bit OOK fixed code at 433.92 MHz. Its leader resembles * CAME's but uses the opposite polarity; typical TE is about 500 µs. */
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

    /* FAAC SLH carries a fixed 22-bit word after a preamble of roughly 2T equal-width pairs. A HIGH longer than about 1.5T represents a 1. I find the preamble by checking both pulse widths rather than relying on a fixed edge index. */
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

    /* This simplified 64-bit 2FSK parser treats the first 32 bits as the address and the next 32 as the hop. It does not decrypt the KeeLoq variant I see used by DoorHan. */
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

    /* Ansonic-12, Prastel, and related remotes use a 12-bit OOK fixed code: '0' = 1T HIGH + 2T LOW; '1' = 2T HIGH + 1T LOW. */
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
/* I reject degenerate words and implausible pulse sequences to avoid matching unrelated signals as a Linear-10 frame. */
bool flipper_decode_linear10(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    uint32_t te = buf->te_us;
    if(te < 700 || te > 1300) return false;
    if(buf->len < 24 || buf->len > 80) return false;

    uint16_t word = 0;
    for(int i = 0; i < 10 && i + 1 < buf->len; i++) {
        if(buf->durations[i * 2] > te + (te >> 1)) word |= (1u << i);
    }
    /* I reject all-zero and all-one (degenerate noise) */
    if(word == 0 || word == 0x3FF) return false;
    /* I require each pulse to fit a bit or filler cell. A pulse outside 0.25T..4T at an even index, or any very long leader, indicates another protocol. */
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

    /* HT6P20 frames carry a 20-bit address and 4-bit data over OOK at about 315 or 433 MHz: '0' = 1T HIGH + 2T LOW; '1' = 2T HIGH + 1T LOW. */
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

/* ── PT2262 / PT2272 fixed-code decoder ──────────────────────────────────── */
/* * PT2262 and compatible PT2272 remotes send unencrypted fixed codes over OOK * at 315 or 433.92 MHz, so captured codes can be replayed or cloned. The frame * starts with a HIGH sync of at least 16T, followed by 12–24 data bits; a bit * is 1 when HIGH exceeds 1.5T. I report the received word unchanged. */
bool flipper_decode_pt2262(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 40 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = fixed_estimate_te(buf->durations, buf->len);
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
/* * EV1527-family gate and garage remotes (including HS1527, RT1527, and SC1527) * use OOK fixed codes at 315 or 433 MHz. Their pulse shape resembles PT2262, * but the sync is shorter (at least 4T) and has no long leader. I keep this * decoder separate so captures retain the EV1527 label; fixed codes can be * replayed or cloned. */
bool flipper_decode_ev1527(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 32 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = fixed_estimate_te(buf->durations, buf->len);
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

    /* I check each pair's shape: HIGH must be 0.75T..3T and LOW no longer than 2.5T. This excludes short preamble pulses and oversized leaders. */
    for(int b = 0; b < bits && ds + 2 * b + 1 < buf->len; b++) {
        uint32_t hi = buf->durations[ds + 2 * b];
        uint32_t lo = buf->durations[ds + 2 * b + 1];
        if(hi < (te * 3 / 4) || hi > (te * 3) || lo > (te * 5 / 2)) return false;
    }
    /* I reject the Security+ 1.0 preamble: it has at least five pairs totaling <=1.7T, while EV1527 data pairs are at least 2T. */
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
/* * This generic parser I use handles 315/433 MHz TPMS-style frames with a HIGH sync * of at least 24T, a 20-bit sensor ID, and four status bits. I do not * decrypt the frame; I report the raw ID for correlation. A listener that * trusts that ID may accept a replay, so I use this decoder in Auto only when the * frame structure is unambiguous. */
bool flipper_decode_tpms(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    if(buf->len < 56 || buf->len > FLIPPER_PULSE_MAX) return false;
    uint32_t base = fixed_estimate_te(buf->durations, buf->len);
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
