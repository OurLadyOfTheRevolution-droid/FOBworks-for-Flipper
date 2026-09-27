/* Synthetic KeeLoq/OOK decode+classification test harness.
 * Generates SYNTHETIC labeled signals (fabricated keys/serials — no real
 * vehicle data), runs them through the real flipper_decode() pipeline, and
 * scores how many decode and classify to the expected protocol.
 *
 * Build: gcc -I../protocol test_classify.c ../protocol/flipper_keeloq.c \
 *            ../protocol/flipper_decoders.c -o test_classify
 */
#include "flipper_decoders.h"
#include "flipper_keeloq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── pulse buffer builder ─────────────────────────────────────────────── */
static void push(FlipperPulseBuf* b, uint32_t d) {
    if(b->len < 1024) b->durations[b->len++] = d;
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

/* ── Security+ 1.0 synth ──────────────────────────────────────────────── */
static void synth_secplus1(FlipperPulseBuf* b, float mhz, uint32_t te, uint64_t word40) {
    reset(b, mhz);
    /* preamble: >=9 pairs each total <=1.5T. use (te/3,te/3) ~ 0.66T */
    uint32_t pp = te / 3;
    for(int i = 0; i < 10; i++) { push(b, pp); push(b, pp); }
    /* tribits: bit0 => 1T HIGH + 2T LOW ; bit1 => 2T HIGH + 1T LOW */
    for(int i = 0; i < 40; i++) {
        if((word40 >> i) & 1) { push(b, te * 2); push(b, te); }
        else                  { push(b, te);     push(b, te * 2); }
    }
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

/* ── run one vector through the real pipeline ─────────────────────────── */
/* ── run one buffer through the real pipeline ─────────────────────────── */
static uint32_t run_pipeline(FlipperPulseBuf* b, char* proto_out) {
    FlipperDecodeResult r;
    uint32_t te = flipper_estimate_te(b->durations, b->len);
    b->te_us = te;
    if(!flipper_decode(b, &r)) { strcpy(proto_out, "(no-decode)"); }
    else { strncpy(proto_out, r.proto, 31); proto_out[31] = 0; }
    return te;
}

/* ── protocol catalog ─────────────────────────────────────────────────── */
enum { P_KEELOQ, P_SECP2, P_SECP1, P_DOORHAN, P_CAME, P_NICE,
       P_HOLTEK, P_FAAC, P_ANSONIC, P_LINEAR, N_PROTO };

typedef struct {
    const char* label;    /* expected proto string from decoder */
    const char* make;     /* SYNTHETIC label */
    const char* model;
    float       mhz;
    uint32_t    te;
    int         rolling;  /* 1 = counter advances per press */
} ProtoSpec;

static const ProtoSpec CATALOG[N_PROTO] = {
    { "KeeLoq-HCS300",   "SynMotors",   "RollGuard",  433.92f, 400, 1 },
    { "Security+2.0",    "SynGate",     "SecPlusII",  315.00f, 250, 1 },
    { "Security+1.0",    "SynGate",     "SecPlusI",   315.00f, 400, 1 },
    { "DoorHan-Rolling", "SynPortal",   "HanRoll",    433.92f, 800, 1 },
    { "CAME-12",         "SynBarrier",  "Cam12",      433.92f, 320, 0 },
    { "Nice-FLO",        "SynBarrier",  "FloFix",     433.92f, 600, 0 },
    { "Holtek-HT6P20",   "SynClone",    "HT6",        433.92f, 160, 0 },
    { "FAAC-SLH",        "SynPortal",   "SlhFix",     433.92f, 300, 0 },
    { "Ansonic-12",      "SynClone",    "Anso12",     433.92f, 600, 0 },
    { "Linear-10",       "SynBarrier",  "Multi10",    300.00f, 1000, 0 },
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
                       ((uint64_t)serial << 10) | ((0x2AA + roll) & 0x3FF));
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

static int test_keeloq_derivation_order(void) {
    DerivedKey all[MAX_DERIVED_KEYS];
    int n = kl_derive_all_keys(all);
    if(n != MAX_DERIVED_KEYS) return 0;
    if(strcmp(all[0].name, "HCS200/simple") != 0 || all[0].key != 0) return 0;
    if(strcmp(all[1].name, "HCS200/normal") != 0 || all[1].key != 0) return 0;
    if(strcmp(all[2].name, "HCS200/xor-seed") != 0 ||
       all[2].key != 0xAAAA555500FF00FFULL) return 0;
    if(strcmp(all[13].name, "HCS200/byte-rev-norm") != 0 ||
       all[13].key != 0xFFFFFFFFFFFFFFFFULL) return 0;
    if(strcmp(all[14].name, "HCS300-ref/simple") != 0 ||
       all[14].key != 1) return 0;
    if(strcmp(all[MAX_DERIVED_KEYS - 1].name, "OEM-hi32only/byte-rev-norm") != 0)
        return 0;
    return 1;
}

int main(int argc, char** argv) {
    printf("kl_self_test: %s\n\n", kl_self_test() ? "PASS" : "FAIL");
    printf("kl_derive_order: %s\n\n",
           test_keeloq_derivation_order() ? "PASS" : "FAIL");
    if(!test_keeloq_derivation_order()) return 1;

    int total_sets = (argc > 1) ? atoi(argv[1]) : 500;
    if(total_sets < 1) total_sets = 1;
    if(total_sets > 500) total_sets = 500;
    const int PRESSES = 3;
    const char* outdir = "../../deliverables/fobscan-classification-corpus";
    const char* subdir = "../../deliverables/fobscan-classification-corpus/sub";

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", subdir); if(system(cmd)) {}

    char mpath[600];
    snprintf(mpath, sizeof(mpath), "%s/manifest.csv", outdir);
    FILE* man = fopen(mpath, "w");
    fprintf(man, "file,make,model,year,fob,series,press,freq_mhz,expected_proto,"
                 "decoded_proto,decoded_ok,classified_ok,te_est_us,n_pulses\n");

    int dec_ok[N_PROTO] = {0}, cls_ok[N_PROTO] = {0}, cnt[N_PROTO] = {0};
    int tot_dec = 0, tot_cls = 0, tot = 0;

    FlipperPulseBuf b;
    char proto[40];

    for(int set = 0; set < total_sets; set++) {
        int proto_id = set % N_PROTO;
        const ProtoSpec* s = &CATALOG[proto_id];
        uint32_t serial = 0x1000 + set * 0x2F3 + proto_id * 0x9E37;
        int year = 2016 + (set % 9);
        int series = set / N_PROTO + 1;

        for(int p = 1; p <= PRESSES; p++) {
            synth_set(proto_id, serial, p, &b);
            float mhz = b.freq_mhz;
            uint32_t te = run_pipeline(&b, proto);

            int dok = strcmp(proto, "(no-decode)") != 0;
            int cok = strcmp(proto, s->label) == 0;
            cnt[proto_id]++; tot++;
            if(dok) { dec_ok[proto_id]++; tot_dec++; }
            if(cok) { cls_ok[proto_id]++; tot_cls++; }

            char fname[256];
            snprintf(fname, sizeof(fname), "%s_%s%03d_%d_fobA_series%d_press%d.sub",
                     s->make, s->model, set, year, series, p);
            char fpath[600];
            snprintf(fpath, sizeof(fpath), "%s/%s", subdir, fname);
            write_sub(fpath, &b, mhz);

            fprintf(man, "%s,%s,%s%03d,%d,A,%d,%d,%.2f,%s,%s,%d,%d,%u,%d\n",
                    fname, s->make, s->model, set, year, series, p, mhz,
                    s->label, proto, dok, cok, te, b.len);
        }
    }
    fclose(man);

    /* summary */
    char spath[600];
    snprintf(spath, sizeof(spath), "%s/SUMMARY.txt", outdir);
    FILE* sf = fopen(spath, "w");
    #define OUT(...) do { printf(__VA_ARGS__); fprintf(sf, __VA_ARGS__); } while(0)
    OUT("FOBscan decode + classification accuracy — SYNTHETIC test corpus\n");
    OUT("================================================================\n");
    OUT("All signals are synthetic (fabricated serials/keys). No real vehicle,\n");
    OUT("gate, or third-party captures were used. Ground truth = generator label.\n\n");
    OUT("Sets: %d   Presses/set: %d   Total signals: %d\n\n", total_sets, PRESSES, tot);
    OUT("%-18s %6s %10s %10s\n", "PROTOCOL", "N", "DECODE%", "CLASSIFY%");
    OUT("--------------------------------------------------------\n");
    for(int i = 0; i < N_PROTO; i++) {
        if(!cnt[i]) continue;
        OUT("%-18s %6d %9.1f%% %9.1f%%\n", CATALOG[i].label, cnt[i],
            100.0 * dec_ok[i] / cnt[i], 100.0 * cls_ok[i] / cnt[i]);
    }
    OUT("--------------------------------------------------------\n");
    OUT("%-18s %6d %9.1f%% %9.1f%%\n", "OVERALL", tot,
        100.0 * tot_dec / tot, 100.0 * tot_cls / tot);
    fclose(sf);
    printf("\nWrote %d .sub files + manifest.csv + SUMMARY.txt to\n%s\n", tot, outdir);
    return 0;
}
