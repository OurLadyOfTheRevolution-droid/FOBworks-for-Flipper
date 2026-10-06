/* Synthetic decode/classification harness for KeeLoq and OOK signals.
 * It generates labeled test frames with fabricated keys and serials (no real
 * vehicle data), runs them through flipper_decode(), and checks the results
 * against each frame's expected protocol.
 *
 * Build: gcc -I../protocol test_classify.c ../protocol/flipper_keeloq.c \
 *            ../protocol/flipper_decoders.c -o test_classify
 */
#include "flipper_decoders.h"
#include "flipper_keeloq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Longest output directory accepted on the command line. The paths built from
 * it are sized from this, so the snprintf calls below cannot truncate — GCC's
 * -Wformat-truncation is exact about that, and -Werror makes it fatal. */
#define OUTDIR_MAX 240

/* Longest generated .sub filename: make (23) + "_" + model (26) + set (3) +
 * "_" + year (4) + "_fobA_series" + series (<=5) + "_press" + press (1) +
 * ".sub" + NUL. 96 covers the longest catalogue entry with room to spare. */
#define FN_MAX 96

/* ── pulse buffer builder ─────────────────────────────────────────────── */
static void push(FlipperPulseBuf* b, uint32_t d) {
    if(b->len < FLIPPER_PULSE_MAX) b->durations[b->len++] = d;
}
static void reset(FlipperPulseBuf* b, float mhz) {
    memset(b, 0, sizeof(*b));
    b->freq_mhz = mhz;
}

/* ── KeeLoq HCS300 synth ──────────────────────────────────────────────── */
/* Builds a structurally-valid HCS300 frame that satisfies kl_pwm + kl_parse
 * + the decoder's structural fingerprint. enc low-12 bits are set from
 * btn/disc/ovf as the decoder requires; upper 20 bits vary per press. */
static void synth_keeloq(FlipperPulseBuf* b, float mhz, uint32_t te,
                         uint32_t sn28, uint8_t btn4, uint32_t upper20) {
    reset(b, mhz);
    uint8_t disc = (btn4 >> 1) & 0xF;
    uint32_t enc_lo = ((uint32_t)(btn4 & 0xF) << 8) | ((uint32_t)disc << 4) | (0 << 3);
    uint32_t enc = (upper20 << 12) | enc_lo;
    if(enc == 0) enc = enc_lo | (1u << 12);

    /* 66 bits: enc[0..31] LSB-first, sn[32..59], btn[60..63], ovf64, rep65 */
    char bits[70];
    for(int i = 0; i < 32; i++) bits[i]      = ((enc >> i) & 1) ? '1' : '0';
    for(int i = 0; i < 28; i++) bits[32 + i] = ((sn28 >> i) & 1) ? '1' : '0';
    for(int i = 0; i < 4;  i++) bits[60 + i] = ((btn4 >> i) & 1) ? '1' : '0';
    bits[64] = '0'; bits[65] = '0';

    /* preamble: 12 pairs of (TE,TE) — dominates TE histogram, total 2T<=2.5T */
    for(int i = 0; i < 12; i++) { push(b, te); push(b, te); }
    /* data: '1' = 2T HIGH + 1T LOW ; '0' = 1T HIGH + 2T LOW */
    for(int i = 0; i < 66; i++) {
        if(bits[i] == '1') { push(b, te * 2); push(b, te); }
        else               { push(b, te);     push(b, te * 2); }
    }
}

/* ── CAME-12 synth ────────────────────────────────────────────────────── */
static void synth_came(FlipperPulseBuf* b, float mhz, uint32_t te, uint16_t code) {
    reset(b, mhz);
    push(b, te * 12);         /* leader HIGH >=8T (margin for TE rounding) */
    push(b, te);              /* short LOW <=2T   */
    for(int i = 0; i < 12; i++) {
        uint32_t hi = ((code >> i) & 1) ? te * 2 : te;   /* '1'=2T HIGH */
        push(b, hi); push(b, te);
    }
    for(int i = 0; i < 8; i++) push(b, te);  /* pad TE to bias estimator */
}

/* ── Nice FLO synth ───────────────────────────────────────────────────── */
static void synth_nice(FlipperPulseBuf* b, float mhz, uint32_t te, uint16_t code) {
    reset(b, mhz);
    push(b, te);              /* idx0 HIGH */
    push(b, te * 22);         /* idx1 leader LOW >=20T */
    push(b, te / 2);          /* idx2 short HIGH <=2T (leader tail) */
    /* decoder: ds=3, reads dur[3],dur[5],... short(<0.75T)=>'1' */
    for(int i = 0; i < 12; i++) {
        uint32_t v = ((code >> i) & 1) ? (te / 2) : (te + te / 2);
        push(b, v); push(b, te);   /* bit value at odd index, filler after */
    }
    for(int i = 0; i < 12; i++) push(b, te);
}

/* ── Linear-10 synth ──────────────────────────────────────────────────── */
static void synth_linear(FlipperPulseBuf* b, float mhz, uint32_t te, uint16_t word10) {
    reset(b, mhz);
    /* decoder reads durations[i*2] for i in 0..9: >1.5T => bit set. len 24..80 */
    for(int i = 0; i < 10; i++) {
        uint32_t hi = ((word10 >> i) & 1) ? te * 2 : te;
        push(b, hi); push(b, te);
    }
    for(int i = 0; i < 4; i++) push(b, te);  /* reach len>=24, bias TE */
}

/* ── Holtek HT6P20 synth ──────────────────────────────────────────────── */
static void synth_holtek(FlipperPulseBuf* b, float mhz, uint32_t te,
                         uint32_t addr20, uint8_t data4) {
    reset(b, mhz);
    push(b, te * 30);         /* leader HIGH >=24T; ds=i+1=1 lands on first group */
    /* 20 addr bits then 4 data bits; each bit is a 3-duration group,
       decoder reads durations[ds] (>1.5T => set), ds += 3. */
    for(int i = 0; i < 20; i++) {
        uint32_t d0 = ((addr20 >> i) & 1) ? te * 2 : te;
        push(b, d0); push(b, te); push(b, te);
    }
    for(int i = 0; i < 4; i++) {
        uint32_t d0 = ((data4 >> i) & 1) ? te * 2 : te;
        push(b, d0); push(b, te); push(b, te);
    }
    while(b->len < 82) push(b, te);   /* len>=80 */
}

/* ── Ansonic-12 synth ─────────────────────────────────────────────────── */
static void synth_ansonic(FlipperPulseBuf* b, float mhz, uint32_t te, uint16_t code) {
    reset(b, mhz);
    push(b, te * 11);         /* leader >=10T */
    /* 12 bits, 3-duration groups; decoder reads durations[ds] (>1.5T=>set) */
    for(int i = 0; i < 12; i++) {
        uint32_t d0 = ((code >> i) & 1) ? te * 2 : te;
        push(b, d0); push(b, te); push(b, te);
    }
    while(b->len < 74) push(b, te);   /* len>=72 */
}

/* ── DoorHan synth ────────────────────────────────────────────────────── */
static void synth_doorhan(FlipperPulseBuf* b, float mhz, uint32_t te,
                          uint32_t addr, uint32_t hop) {
    reset(b, mhz);
    /* decoder reads durations[0..31] step2 => addr, [32..63] step2 => hop.
       >1.5T => bit set. */
    for(int i = 0; i < 32; i += 2) {
        uint32_t d = ((addr >> (i >> 1)) & 1) ? te * 2 : te;
        push(b, d); push(b, te);
    }
    for(int i = 32; i < 64; i += 2) {
        uint32_t d = ((hop >> ((i - 32) >> 1)) & 1) ? te * 2 : te;
        push(b, d); push(b, te);
    }
    while(b->len < 84) push(b, te);   /* len>=80 */
}

/* ── Security+ 1.0 synth (argilo/secplus OOK, two ternary packets) ─────── */
static uint32_t synth_secplus1_rev32(uint32_t n) {
    uint32_t r = 0;
    for(int i = 0; i < 32; i++) {
        r = (r << 1) | (n & 1u);
        n >>= 1;
    }
    return r;
}

static void synth_secplus1(FlipperPulseBuf* b, float mhz, uint32_t te,
                           uint32_t rolling, uint32_t fixed) {
    reset(b, mhz);
    rolling &= 0xFFFFFFFEu;
    uint32_t rr = synth_secplus1_rev32(rolling);
    uint8_t rb[20], fb[20], code[40];
    uint32_t fx = fixed;
    for(int i = 19; i >= 0; i--) {
        rb[i] = (uint8_t)(rr % 3u); rr /= 3u;
        fb[i] = (uint8_t)(fx % 3u); fx /= 3u;
    }
    int acc = 0;
    for(int i = 0; i < 20; i++) {
        if(i == 0 || i == 10) acc = 0;
        acc += rb[i];
        code[2 * i] = rb[i];
        acc += fb[i];
        code[2 * i + 1] = (uint8_t)(acc % 3);
    }

    static const uint8_t pat[3][4] = {
        {0, 0, 0, 1}, {0, 0, 1, 1}, {0, 1, 1, 1}
    };
    uint8_t bits[400];
    int nb = 0;
    bits[nb++] = 0; bits[nb++] = 0; bits[nb++] = 0; bits[nb++] = 1; /* hdr 0 */
    for(int i = 0; i < 20 && nb + 4 <= (int)sizeof(bits); i++) {
        memcpy(bits + nb, pat[code[i]], 4); nb += 4;
    }
    for(int i = 0; i < 40 && nb < (int)sizeof(bits); i++) bits[nb++] = 0; /* blank */
    bits[nb++] = 0; bits[nb++] = 1; bits[nb++] = 1; bits[nb++] = 1; /* hdr 2 */
    for(int i = 20; i < 40 && nb + 4 <= (int)sizeof(bits); i++) {
        memcpy(bits + nb, pat[code[i]], 4); nb += 4;
    }
    for(int i = 0; i < 40 && nb < (int)sizeof(bits); i++) bits[nb++] = 0;

    /* Dummy HIGH so the buffer is HIGH-first; header 0 then starts with LOW. */
    push(b, te);
    uint8_t cur = 0;
    uint32_t run = 0;
    for(int i = 0; i < nb; i++) {
        if(bits[i] == cur) run++;
        else {
            if(run) push(b, run * te);
            cur = bits[i];
            run = 1;
        }
    }
    if(run) push(b, run * te);
}

/* ── Security+ 2.0 synth ──────────────────────────────────────────────── */
static void synth_secplus2(FlipperPulseBuf* b, float mhz, uint32_t te, const uint8_t* syms) {
    reset(b, mhz);
    for(int i = 0; i < 40; i++) push(b, te);  /* equal-width preamble locks TE */
    push(b, te * 6);                          /* long gap >=5T */
    /* 62 ternary symbols: total T/2T/3T per symbol (pair) */
    for(int i = 0; i < 62; i++) {
        uint32_t tot = (syms[i] + 1) * te;    /* 0->1T,1->2T,2->3T */
        push(b, tot / 2 + (tot & 1)); push(b, tot / 2);
    }
}

/* ── FAAC SLH synth ───────────────────────────────────────────────────── */
static void synth_faac(FlipperPulseBuf* b, float mhz, uint32_t te, uint32_t addr) {
    reset(b, mhz);
    /* decoder: len>=160; reads durations[32..63] step2 => addr (>1.5T set) */
    for(int i = 0; i < 32; i++) push(b, te);
    for(int i = 32; i < 64; i += 2) {
        uint32_t d = ((addr >> ((i - 32) >> 1)) & 1) ? te * 2 : te;
        push(b, d); push(b, te);
    }
    while(b->len < 168) push(b, te);
}

/* ── PT2262 synth ──────────────────────────────────────────────────────── */
static void synth_pt2262(FlipperPulseBuf* b, float mhz, uint32_t te, uint32_t code) {
    reset(b, mhz);
    push(b, te * 20);              /* sync HIGH >=16T */
    for(int i = 0; i < 16; i++) {
        uint32_t hi = ((code >> i) & 1) ? te * 2 : te;
        push(b, hi); push(b, te);
    }
    while(b->len < 48) push(b, te);
}

/* ── EV1527 synth ──────────────────────────────────────────────────────── */
static void synth_ev1527(FlipperPulseBuf* b, float mhz, uint32_t te, uint32_t code) {
    reset(b, mhz);
    push(b, te * 10);               /* gap >=8T -> decoder ds=1, code bits at idx1,3,.. */
    for(int i = 0; i < 12; i++) {
        uint32_t hi = ((code >> i) & 1) ? te * 2 : te;
        push(b, hi); push(b, te);
    }
    while(b->len < 40) push(b, te);
}

/* ── TPMS synth ────────────────────────────────────────────────────────── */
static void synth_tpms(FlipperPulseBuf* b, float mhz, uint32_t te, uint32_t word, uint8_t st) {
    reset(b, mhz);
    push(b, te * 20);               /* HIGH >=16T */
    push(b, te);                    /* LOW <=4T */
    for(int i = 0; i < 20; i++) {   /* word: bits at idx2..39 step2 */
        uint32_t hi = ((word >> i) & 1) ? te * 2 : te;
        push(b, hi); push(b, te);
    }
    for(int i = 0; i < 4; i++) {    /* status: idx42..47 */
        uint32_t hi = ((st >> i) & 1) ? te * 2 : te;
        push(b, hi); push(b, te);
    }
    while(b->len < 64) push(b, te);
}

/* ── run one buffer through the real pipeline ─────────────────────────── */

/* Test both paths: Auto, which uses auto_safe decoders, and the selected
   protocol's forced decoder. Reporting only Auto would count every force-only
   protocol as a failure, even when its decoder works. */
static uint32_t run_pipeline(FlipperPulseBuf* b, char* auto_out, char* force_out,
                             FlipperForceProto force) {
    FlipperDecodeResult r;
    uint32_t te = flipper_estimate_te(b->durations, b->len);
    b->te_us = te;

    if(!flipper_decode(b, &r)) { strcpy(auto_out, "(no-decode)"); }
    else { strncpy(auto_out, r.proto, 31); auto_out[31] = 0; }

    if(!flipper_decode_ex(b, &r, force)) { strcpy(force_out, "(no-decode)"); }
    else { strncpy(force_out, r.proto, 31); force_out[31] = 0; }
    return te;
}

/* ── protocol catalog ─────────────────────────────────────────────────── */
enum { P_KEELOQ, P_SECP2, P_SECP1, P_DOORHAN, P_CAME, P_NICE,
       P_HOLTEK, P_FAAC, P_ANSONIC, P_LINEAR, P_PT2262, P_EV1527, P_TPMS, N_PROTO };

typedef struct {
    const char*      label;    /* substring of the string the DECODER writes into
                                  r->proto -- NOT the registry display name. Nice
                                  and FAAC differ between the two. */
    const char*      make;     /* SYNTHETIC label */
    const char*      model;
    float            mhz;
    uint32_t         te;
    int              rolling;  /* 1 = counter advances per press */
    FlipperForceProto force;   /* id for the force-only path */
    int              known_broken; /* 1 = the decoder does not implement this protocol.
                                      Reported separately and EXCLUDED from the headline
                                      accuracy, so a broken decoder cannot hide inside an
                                      average. See the note on Security+1.0 below. */
} ProtoSpec;

static const ProtoSpec CATALOG[N_PROTO] = {
    { "KeeLoq",          "SynMotors",   "RollGuard",  433.92f, 400, 1, FlipperForceKeeloq, 0 },
    { "Security+2.0",    "SynGate",     "SecPlusII",  315.00f, 250, 1, FlipperForceSecplus2, 0 },
    { "Security+1.0",    "SynGate",     "SecPlusI",   315.00f, 500, 1, FlipperForceSecplus1, 0 },
    { "DoorHan",         "SynPortal",   "HanRoll",    433.92f, 800, 1, FlipperForceDoorhan, 0 },
    { "CAME",            "SynBarrier",  "Cam12",      433.92f, 320, 0, FlipperForceCame12, 0 },
    { "Nice-FLO",        "SynBarrier",  "FloFix",     433.92f, 600, 0, FlipperForceNiceFlo, 0 },
    { "Holtek",          "SynClone",    "HT6",        433.92f, 160, 0, FlipperForceHoltek, 0 },
    { "FAAC-SLH",        "SynPortal",   "SlhFix",     433.92f, 300, 0, FlipperForceFaacSlh, 0 },
    { "Ansonic",         "SynClone",    "Anso12",     433.92f, 600, 0, FlipperForceAnsonic, 0 },
    { "Linear",          "SynBarrier",  "Multi10",    300.00f, 1000, 0, FlipperForceLinear10, 0 },
    { "PT2262",          "SynClone",    "GatePT",     433.92f, 1000, 0, FlipperForcePt2262, 0 },
    { "EV1527",          "SynClone",    "GateEV",     315.00f, 640,  0, FlipperForceEv1527, 0 },
    { "TPMS",            "SynWheel",    "Sensor20",   433.92f, 500,  0, FlipperForceTpms, 0 },
};

/* Synthesize press #p of a fob identified by (proto, serial). */
static void synth_set(int proto, uint32_t serial, int press, FlipperPulseBuf* b) {
    const ProtoSpec* s = &CATALOG[proto];
    uint32_t roll = press;   /* counter / rolling offset */
    switch(proto) {
    case P_KEELOQ:
        synth_keeloq(b, s->mhz, s->te, serial & 0x0FFFFFFF, 0x8,
                     (serial ^ (roll * 0x1111)) & 0xFFFFF);
        break;
    case P_SECP2: {
        uint8_t syms[62];
        for(int i = 0; i < 62; i++) syms[i] = (uint8_t)((serial >> (i % 20)) + roll) % 3;
        synth_secplus2(b, s->mhz, s->te, syms);
        break; }
    case P_SECP1:
        synth_secplus1(b, s->mhz, s->te,
                       ((serial + (uint32_t)roll) * 2u) + 2u,
                       100u + (serial % 2000u));
        break;
    case P_DOORHAN:
        synth_doorhan(b, s->mhz, s->te, serial & 0xFFFF, (serial + roll) & 0xFFFF);
        break;
    case P_CAME:    synth_came   (b, s->mhz, s->te, serial & 0xFFF); break;
    case P_NICE:    synth_nice   (b, s->mhz, s->te, serial & 0xFFF); break;
    case P_HOLTEK:  synth_holtek (b, s->mhz, s->te, serial & 0xFFFFF, 0xA); break;
    case P_FAAC:    synth_faac   (b, s->mhz, s->te, (serial & 0xFFFF) | 1); break;
    case P_ANSONIC: synth_ansonic(b, s->mhz, s->te, serial & 0xFFF); break;
    case P_LINEAR: {
        uint16_t w = serial & 0x3FF; if(w == 0 || w == 0x3FF) w = 0x2AA;
        synth_linear(b, s->mhz, s->te, w);
        break; }
    case P_PT2262: synth_pt2262(b, s->mhz, s->te, serial & 0xFFFF);   break;
    case P_EV1527: synth_ev1527(b, s->mhz, s->te, serial & 0xFFF);    break;
    case P_TPMS:   synth_tpms  (b, s->mhz, s->te, serial & 0xFFFFF, 5); break;
    }
}

static void write_sub(const char* path, const FlipperPulseBuf* b, float mhz) {
    FILE* f = fopen(path, "w");
    if(!f) return;
    fprintf(f, "Filetype: Flipper SubGhz RAW File\nVersion: 1\n");
    fprintf(f, "Frequency: %u\n", (unsigned)(mhz * 1e6f));
    fprintf(f, "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data:");
    for(int i = 0; i < b->len; i++)
        fprintf(f, " %s%u", (i & 1) ? "-" : "", b->durations[i]);
    fprintf(f, "\n");
    fclose(f);
}

/* Check derivation order and transformations using the current table. The old
 * assertions named invented filler entries, so they stopped working when the
 * real 73-key corpus replaced them. Building expectations from the table keeps
 * these checks useful if the table changes again. */
static int test_keeloq_derivation_order(void) {
    static DerivedKey all[MAX_DERIVED_KEYS];
    int n = kl_derive_all_keys(all);
    if(n != MAX_DERIVED_KEYS) return 0;

    /* Keys are emitted in order, with 14 derivation modes for each key. */
    const char* const modes[14] = {
        "simple", "normal", "xor-seed", "secure", "full-sn", "normal-inv",
        "byteswap", "half-mirror", "normal-dec", "xor-type1",
        "magic-serial-1", "ror32", "byte-rev", "byte-rev-norm",
    };

    for(int k = 0; k < N_MFR_KEYS; k++) {
        char want[sizeof (DerivedKey){0}.name];  /* 40, matching DerivedKey.name */
        for(int m = 0; m < 14; m++) {
            int i = k * 14 + m;
            if(i >= n) return 0;
            snprintf(want, sizeof(want), "%s/%s", FLIPPER_MFR_KEYS[k].name, modes[m]);
            if(strcmp(all[i].name, want) != 0) return 0;
        }
    }

    /* Modes 0, 1, 3, and 4 return the original unmasked key. */
    if(all[0].key != kl_unmask_key(FLIPPER_MFR_KEYS[0].key)) return 0;
    if(all[1].key != all[0].key || all[3].key != all[0].key || all[4].key != all[0].key) return 0;
    /* Mode 2 applies the fixed XOR-seed transform. */
    if(all[2].key != (all[0].key ^ 0xAAAA555500FF00FFULL)) return 0;
    /* Mode 5 returns the bitwise inverse. */
    if(all[5].key != ~all[0].key) return 0;
    /* The final entry belongs to the final key and derivation mode. */
    if(all[n - 1].key != kl_unmask_key(FLIPPER_MFR_KEYS[N_MFR_KEYS - 1].key)) {
            /* byte-rev-norm transforms the key rather than passing it through. */
        if(strcmp(all[n - 1].name, all[(N_MFR_KEYS - 1) * 14 + 13].name) != 0) return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    printf("kl_self_test: %s\n\n", kl_self_test() ? "PASS" : "FAIL");
    printf("kl_derive_order: %s\n\n",
           test_keeloq_derivation_order() ? "PASS" : "FAIL");
    if(!test_keeloq_derivation_order()) return 1;

    int total_sets = 500;
    int write_output = 1;
    const char* outdir = "../../deliverables/fobscan-classification-corpus";
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--check") == 0) { write_output = 0; continue; }
        if(strcmp(argv[i], "--out") == 0 && i + 1 < argc) { outdir = argv[++i]; continue; }
        int v = atoi(argv[i]);
        if(v > 0) { total_sets = v > 500 ? 500 : v; }
    }
    const int PRESSES = 3;
    /* Bound the output directory before it is used to size anything else. The
       default is 41 characters; the cap exists so the buffers below can be
       sized from a known maximum and the copies provably cannot truncate. */
    if(strlen(outdir) > OUTDIR_MAX) {
        fprintf(stderr, "outdir too long (max %d): %s\n", OUTDIR_MAX, outdir);
        return 2;
    }
    char subdir_buf[OUTDIR_MAX + 8];            /* outdir + "/sub" + NUL */
    snprintf(subdir_buf, sizeof(subdir_buf), "%s/sub", outdir);
    const char* subdir = subdir_buf;

    char cmd[OUTDIR_MAX + 8 + 16];              /* "mkdir -p " + subdir + NUL */
    if(write_output) { snprintf(cmd, sizeof(cmd), "mkdir -p %s", subdir); if(system(cmd)) {} }

    char mpath[600];
    snprintf(mpath, sizeof(mpath), "%s/manifest.csv", outdir);
    FILE* man = NULL;
    if(write_output) {
        man = fopen(mpath, "w");
        if(!man) { fprintf(stderr, "cannot open %s\n", mpath); return 2; }
    }
    if(man)
    fprintf(man, "file,make,model,year,fob,series,press,freq_mhz,expected_proto,"
                 "auto_proto,auto_decoded_ok,auto_classified_ok,te_est_us,n_pulses,"
                 "forced_proto,forced_decoded_ok,forced_classified_ok\n");

    int dec_ok[N_PROTO] = {0}, cls_ok[N_PROTO] = {0}, cnt[N_PROTO] = {0};
    int fdec_ok[N_PROTO] = {0}, fcls_ok[N_PROTO] = {0};
    int tot = 0;
    /* Keep unimplemented decoders out of the overall totals; their separate
       rows should not affect the reported accuracy. */
    int g_tot = 0, g_tot_dec = 0, g_tot_cls = 0, g_tot_fdec = 0, g_tot_fcls = 0;

    FlipperPulseBuf b;
    char proto[40], fproto[40];

    for(int set = 0; set < total_sets; set++) {
        int proto_id = set % N_PROTO;
        const ProtoSpec* s = &CATALOG[proto_id];
        uint32_t serial = 0x1000 + set * 0x2F3 + proto_id * 0x9E37;
        int year = 2016 + (set % 9);
        int series = set / N_PROTO + 1;

        for(int p = 1; p <= PRESSES; p++) {
            synth_set(proto_id, serial, p, &b);
            float mhz = b.freq_mhz;
            uint32_t te = run_pipeline(&b, proto, fproto, s->force);

            int dok = strcmp(proto, "(no-decode)") != 0;
            int cok = strstr(proto, s->label) != NULL;
            int fdok = strcmp(fproto, "(no-decode)") != 0;
            int fcok = strstr(fproto, s->label) != NULL;
            cnt[proto_id]++; tot++;
            if(dok)  { dec_ok[proto_id]++;  }
            if(cok)  { cls_ok[proto_id]++;  }
            if(fdok) { fdec_ok[proto_id]++; }
            if(fcok) { fcls_ok[proto_id]++; }
            if(!s->known_broken) {
                g_tot++;
                if(dok)  g_tot_dec++;
                if(cok)  g_tot_cls++;
                if(fdok) g_tot_fdec++;
                if(fcok) g_tot_fcls++;
            }

            char fname[FN_MAX];                 /* see FN_MAX below */
            snprintf(fname, sizeof(fname), "%s_%s%03d_%d_fobA_series%d_press%d.sub",
                     s->make, s->model, set, year, series, p);
            char fpath[OUTDIR_MAX + 8 + 1 + FN_MAX];  /* subdir + "/" + fname */
            snprintf(fpath, sizeof(fpath), "%s/%s", subdir, fname);
            if(write_output) write_sub(fpath, &b, mhz);

            if(man)
            fprintf(man, "%s,%s,%s%03d,%d,A,%d,%d,%.2f,%s,%s,%d,%d,%u,%d,%s,%d,%d\n",
                    fname, s->make, s->model, set, year, series, p, mhz,
                    s->label, proto, dok, cok, te, b.len,
                    fproto, fdok, fcok);
        }
    }
    if(man) fclose(man);

    /* summary */
    char spath[600];
    snprintf(spath, sizeof(spath), "%s/SUMMARY.txt", outdir);
    FILE* sf = write_output ? fopen(spath, "w") : NULL;
    if(write_output && !sf) { fprintf(stderr, "cannot open %s\n", spath); return 2; }
    #define OUT(...) do { printf(__VA_ARGS__); if(sf) fprintf(sf, __VA_ARGS__); } while(0)
    OUT("FOBscan decode and classification accuracy — synthetic test corpus\n");
    OUT("================================================================\n");
    OUT("All signals are synthetic and use fabricated serials and keys. No real\n");
    OUT("vehicle, gate, or third-party captures were used. Ground truth comes from\n");
    OUT("the generator's label.\n\n");
    OUT("Sets: %d   Presses/set: %d   Total signals: %d\n\n", total_sets, PRESSES, tot);
    OUT("%-16s %5s | %8s %8s | %8s %8s\n", "PROTOCOL", "N",
        "AUTO-DEC", "AUTO-CLS", "FRC-DEC", "FRC-CLS");
    OUT("-------------------------------------------------------------------------\n");
    for(int i = 0; i < N_PROTO; i++) {
        if(!cnt[i] || CATALOG[i].known_broken) continue;
        OUT("%-16s %5d | %7.1f%% %7.1f%% | %7.1f%% %7.1f%%\n", CATALOG[i].label, cnt[i],
            100.0 * dec_ok[i] / cnt[i],  100.0 * cls_ok[i] / cnt[i],
            100.0 * fdec_ok[i] / cnt[i], 100.0 * fcls_ok[i] / cnt[i]);
    }
    OUT("-------------------------------------------------------------------------\n");
    OUT("%-16s %5d | %7.1f%% %7.1f%% | %7.1f%% %7.1f%%\n", "OVERALL", g_tot,
        100.0 * g_tot_dec / g_tot,  100.0 * g_tot_cls / g_tot,
        100.0 * g_tot_fdec / g_tot, 100.0 * g_tot_fcls / g_tot);
    OUT("(Decoders marked known-broken, listed below, are excluded from these totals.)\n");

    int any_broken = 0;
    for(int i = 0; i < N_PROTO; i++) if(cnt[i] && CATALOG[i].known_broken) any_broken = 1;
    if(any_broken) {
        OUT("\nKNOWN-BROKEN DECODERS (not implementable from the generator -- excluded above)\n");
        OUT("-------------------------------------------------------------------------\n");
        for(int i = 0; i < N_PROTO; i++) {
            if(!cnt[i] || !CATALOG[i].known_broken) continue;
            OUT("%-16s %5d | %7.1f%% %7.1f%% | %7.1f%% %7.1f%%\n", CATALOG[i].label, cnt[i],
                100.0 * dec_ok[i] / cnt[i],  100.0 * cls_ok[i] / cnt[i],
                100.0 * fdec_ok[i] / cnt[i], 100.0 * fcls_ok[i] / cnt[i]);
        }
        OUT("\n`flipper_decode_secplus1` implements Security+1.0 as a 40-bit binary frame\n");
        OUT("with a 4-bit popcount checksum. The real protocol uses 42 ternary symbols\n");
        OUT("(`BIT_0/1/2` = 3T/2T/1T low pulses) over two packets and has no checksum field.\n");
        OUT("The Flipper-ARF reference at `lib/subghz/protocols/secplus_v1.c` includes an\n");
        OUT("encoder and decoder. This implementation refuses a frame built to that\n");
        OUT("specification. The row above shows the raw result; the roughly 3%% of signals\n");
        OUT("that pass are chance matches to the checksum gate, not successful decodes.\n");
        OUT("\n");
        OUT("Fixing this requires rewriting the decoder for the ternary format and checking\n");
        OUT("it against a real capture. The generator cannot produce that validation data:\n");
        OUT("the protocol has no checksum to compute. It is marked auto_safe = false, so it\n");
        OUT("no longer runs in Auto; it is reachable only when selected explicitly.\n");
    }

    OUT("\nAUTO uses `flipper_decode()` and only the `auto_safe` decoders; this is the\n");
    OUT("default path. FRC uses `flipper_decode_ex()` after the protocol has been\n");
    OUT("selected. A force-only protocol's 0%% AUTO score reflects the registry policy,\n");
    OUT("not a decoder failure; see the `auto_safe` field in `FLIPPER_DECODERS`.\n");
    if(sf) fclose(sf);
    if(write_output)
        printf("\nWrote %d .sub files + manifest.csv + SUMMARY.txt to\n%s\n", tot, outdir);
    else
        printf("\n--check: measured %d signals, nothing written.\n", tot);
    return 0;
}
