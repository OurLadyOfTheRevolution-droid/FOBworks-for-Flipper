/* test_sim.c — host checks for on-device paths that can trigger furi_check().
 *
 * These tests cannot drive the Flipper GUI. They mirror scene index handling
 * and input parsing against the real vehicle and key tables, then check that
 * every access stays within bounds.
 *
 * Build: see tools/Makefile target `test_sim`.
 */
#include "../protocol/flipper_vehicles.h"
#include "../protocol/flipper_keyvault.h"
#include "../protocol/flipper_rollingpwn.h"
#include "../protocol/flipper_honda.h"
#include "../protocol/flipper_subaru.h"
#include "../protocol/flipper_decoders.h"
#include "../protocol/flipper_keeloq.h"
#include "../protocol/flipper_verdict.h"
#include "../protocol/flipper_saved_check.h"
#include "../protocol/flipper_gm.h"
#include "../protocol/flipper_ford.h"
#include "../protocol/flipper_chrysler.h"
#include "../protocol/flipper_kia.h"
#include "../protocol/flipper_vag.h"
#include "../protocol/flipper_psa.h"
#include "../protocol/flipper_vehrke.h"
#include "../protocol/flipper_mazda.h"
#include "../protocol/flipper_fiat.h"
#include "../protocol/flipper_toyota.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        g_checks++;                                                             \
        if(!(cond)) { g_fail++; printf("  FAIL: %s\n", (msg)); }                \
    } while(0)

/* ── Sim A: guided make→model→year navigation (FOBclone + FOBcatch) ──────── */
/* Mirrors the make/model/year selection in the FOBclone and FOBcatch scenes:
 *   - build unique make list (first-seen order, cap 24)
 *   - per make, collect matching vehicle indices (cap 32)
 *   - per model, the year picker reads v->year_count years[] entries
 * Check each vehicle index and make sure its year_count is valid for
 * VariableItemList (>0 and <=MAX). */
static void sim_guided_nav(void) {
    printf("== Sim A: guided make/model/year index safety ==\n");

    const char* makes[24];
    int make_count = 0;
    for(int i = 0; i < FLIPPER_FC_VEHICLE_COUNT && make_count < 24; i++) {
        bool seen = false;
        for(int m = 0; m < make_count; m++)
            if(strcmp(makes[m], FLIPPER_FC_VEHICLES[i].make) == 0) { seen = true; break; }
        if(!seen) makes[make_count++] = FLIPPER_FC_VEHICLES[i].make;
    }
    CHECK(make_count > 0, "at least one make");

    int total_models = 0, total_years = 0;
    for(int mk = 0; mk < make_count; mk++) {
        int model_veh[32];
        int model_count = 0;
        for(int i = 0; i < FLIPPER_FC_VEHICLE_COUNT && model_count < 32; i++) {
            if(strcmp(FLIPPER_FC_VEHICLES[i].make, makes[mk]) == 0)
                model_veh[model_count++] = i;
        }
        CHECK(model_count > 0, "each make has >=1 model");

        for(int md = 0; md < model_count; md++) {
            int vidx = model_veh[md];               /* what model_cb stores */
            CHECK(vidx >= 0 && vidx < FLIPPER_FC_VEHICLE_COUNT,
                  "model global index in bounds");
            const FlipperFcVehicle* v = &FLIPPER_FC_VEHICLES[vidx];

            /* The scene adds year_count items, then selects index 0. An empty
               list makes that selection fail an on-device check. */
            CHECK(v->year_count >= 1, "year_count >= 1 (VariableItemList safe)");
            CHECK(v->year_count <= FC_YEARS_MAX, "year_count <= FC_YEARS_MAX");
            for(int y = 0; y < v->year_count; y++)
                CHECK(v->years[y] != NULL, "year string non-NULL");

            total_models++;
            total_years += v->year_count;
        }
    }
    printf("  (%d makes, %d models, %d year entries walked)\n",
           make_count, total_models, total_years);
}

/* ── Sim B: FOBLoq key-entry parser ("name, hex") ────────────────────────── */
/* Mirrors the input parser in flipper_scene_fobloq.c. Keep both copies in sync. */
static bool parse_kv(const char* text_buf, char name_out[32], uint64_t* key_out) {
    char name[32]; memset(name, 0, sizeof(name));
    char hex[17];  memset(hex, 0, sizeof(hex));

    const char* src   = text_buf;
    const char* comma = strchr(src, ',');
    const char* nend  = comma ? comma : (src + strlen(src));

    while(*src == ' ') src++;
    size_t nlen = 0;
    for(const char* p = src; p < nend && nlen < sizeof(name) - 1; p++) {
        if(*p == ' ' && (p + 1 >= nend)) break;
        name[nlen++] = *p;
    }
    while(nlen > 0 && name[nlen - 1] == ' ') name[--nlen] = '\0';

    size_t hlen = 0;
    if(comma) {
        for(const char* p = comma + 1; *p && hlen < 16; p++) {
            char c = *p;
            bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                          (c >= 'A' && c <= 'F');
            if(is_hex) hex[hlen++] = c;
        }
    }
    while(strlen(hex) < 16) memmove(hex + 1, hex, strlen(hex) + 1), hex[0] = '0';

    strncpy(name_out, name, 31); name_out[31] = '\0';
    *key_out = strtoull(hex, NULL, 16);
    return nlen > 0;
}

static void sim_kv_parser(void) {
    printf("== Sim B: FOBLoq key-entry parser ==\n");
    char name[32]; uint64_t key;

    CHECK(parse_kv("Ford1, 0123456789ABCDEF", name, &key), "well-formed accepted");
    CHECK(strcmp(name, "Ford1") == 0, "name parsed");
    CHECK(key == 0x0123456789ABCDEFULL, "full 16-nibble key parsed");

    CHECK(parse_kv("  Spaced  ,  DEADBEEF  ", name, &key), "spaces trimmed accepted");
    CHECK(strcmp(name, "Spaced") == 0, "leading/trailing name spaces trimmed");
    CHECK(key == 0x00000000DEADBEEFULL, "short hex left-padded to 64-bit");

    CHECK(parse_kv("k, 12:34-56 78", name, &key), "punctuation in hex tolerated");
    CHECK(key == 0x0000000012345678ULL, "non-hex chars skipped, digits kept");

    CHECK(!parse_kv("", name, &key), "empty line rejected");
    CHECK(!parse_kv(", ABCDEF", name, &key), "empty name rejected");
    CHECK(parse_kv("NoHexKey", name, &key), "name-only accepted (key=0)");
    CHECK(key == 0ULL, "name-only yields zero key");

    /* Over-long hex: only first 16 nibbles kept. */
    CHECK(parse_kv("big, 0123456789ABCDEF0000", name, &key), "over-long hex accepted");
    CHECK(key == 0x0123456789ABCDEFULL, "hex truncated to 16 nibbles");

    /* Over-long name: truncated to 31 chars, still valid. */
    CHECK(parse_kv("ThisNameIsWayTooLongToFitInThirtyOneChars, AA", name, &key),
          "over-long name accepted");
    CHECK(strlen(name) == 31, "name truncated to 31 chars");
}

/* ── Sim C: vault upsert + persistence round-trip ────────────────────────── */
static void sim_vault_roundtrip(void) {
    printf("== Sim C: vault upsert + keys.txt round-trip ==\n");
    FlipperVaultKey keys[FLIPPER_KEYVAULT_MAX];
    memset(keys, 0, sizeof(keys));

    /* Fill to capacity via the parser+upsert, as the scene does. */
    int accepted = 0;
    for(int i = 0; i < FLIPPER_KEYVAULT_MAX + 4; i++) {
        char line[48]; snprintf(line, sizeof(line), "key_%02d, %016X", i, i * 0x1111);
        char name[32]; uint64_t key;
        if(parse_kv(line, name, &key) && flipper_keyvault_upsert(keys, name, key))
            accepted++;
    }
    CHECK(accepted == FLIPPER_KEYVAULT_MAX, "upsert caps at vault capacity");
    CHECK(flipper_keyvault_count(keys) == FLIPPER_KEYVAULT_MAX, "count == capacity");

    /* Same-name upsert replaces, does not grow. */
    CHECK(flipper_keyvault_upsert(keys, "key_00", 0xCAFEBABEULL), "same-name upsert ok");
    CHECK(flipper_keyvault_count(keys) == FLIPPER_KEYVAULT_MAX, "count unchanged on replace");
    const FlipperVaultKey* k0 = flipper_keyvault_find(keys, "key_00");
    CHECK(k0 && k0->key == 0xCAFEBABEULL, "replacement value wins");

    /* Serialize → parse back → identical count. */
    char buf[2048];
    size_t n = flipper_keyvault_to_text(keys, buf, sizeof(buf));
    CHECK(n > 0, "to_text produced output");
    FlipperVaultKey keys2[FLIPPER_KEYVAULT_MAX];
    memset(keys2, 0, sizeof(keys2));
    int count2 = 0;
    CHECK(flipper_keyvault_from_text(buf, keys2, FLIPPER_KEYVAULT_MAX, &count2),
          "from_text parsed");
    CHECK(count2 == FLIPPER_KEYVAULT_MAX, "round-trip preserves key count");
    const FlipperVaultKey* r0 = flipper_keyvault_find(keys2, "key_00");
    CHECK(r0 && r0->key == 0xCAFEBABEULL, "round-trip preserves replaced value");

    /* Invalid names rejected by upsert (charset guard). */
    CHECK(!flipper_keyvault_upsert(keys, "bad name!", 1), "invalid charset rejected");
    CHECK(!flipper_keyvault_upsert(keys, "", 1), "empty name rejected by vault");
}

/* ── Sim C2: KeeLoq predict-next synth ───────────────────────────────────── */
static void sim_predict_next(void) {
    printf("== Sim C2: KeeLoq predict-next ==\n");
    uint64_t key = 0x0123456789ABCDEFULL;
    uint32_t sn = 0x1234567;
    uint8_t btn = 2;
    uint32_t cnt = 0x10;
    uint32_t plain = ((uint32_t)btn << 28) | cnt;
    uint32_t enc = kl_encrypt(plain, key);

    FlipperDecodeResult r;
    memset(&r, 0, sizeof(r));
    strncpy(r.proto, "KeeLoq-HCS300", sizeof(r.proto) - 1);
    r.addr = sn; r.cnt = cnt; r.hop = enc; r.btn = btn;
    r.te_us = 400; r.freq_mhz = 433.92f; r.rolling = true;
    snprintf(r.device_key_hex, sizeof(r.device_key_hex), "%016llX",
             (unsigned long long)key);
    strncpy(r.mfr_name, "test", sizeof(r.mfr_name) - 1);

    CHECK(flipper_predict_can_synth(&r), "can_synth with key");
    FlipperPulseBuf out;
    FlipperDecodeResult nd;
    CHECK(flipper_predict_keeloq_next(&r, 1, &out, &nd), "synth cnt+1");
    CHECK(out.len > 64, "synth produced pulses");
    CHECK(nd.cnt == ((cnt + 1) & 0xFFFF), "next counter");
    /* Cipher-level check: encrypt(cnt+1) matches metadata hop. */
    uint32_t expect = kl_encrypt(((uint32_t)btn << 28) | nd.cnt, key);
    CHECK(nd.hop == expect, "synth hop matches encrypt");
    r.device_key_hex[0] = '\0';
    CHECK(!flipper_predict_can_synth(&r), "no synth without key");
    CHECK(!flipper_predict_keeloq_next(&r, 1, &out, &nd), "synth fails without key");
}

static void sim_verdict(void) {
    printf("== Sim C4: one-burst verdict ==\n");
    FlipperDecodeResult r;
    memset(&r, 0, sizeof(r));
    strncpy(r.proto, "KeeLoq", sizeof(r.proto) - 1);
    r.addr = 0x111; r.cnt = 10; r.rolling = true; r.btn = 1;
    snprintf(r.device_key_hex, sizeof(r.device_key_hex), "%016llX",
             0x0123456789ABCDEFULL);
    FlipperSession s; flipper_session_reset(&s);
    flipper_session_push(&s, &r);
    FlipperVerdict v;
    flipper_verdict_build(&v, &r, true, true, &s, NULL);
    CHECK(v.conf == FlipperConfAuto, "auto confidence");
    CHECK(v.act == FlipperActPredict, "key → predict");
    r.device_key_hex[0] = '\0';
    r.cnt = 11; flipper_session_push(&s, &r);
    r.cnt = 12; flipper_session_push(&s, &r);
    flipper_verdict_build(&v, &r, true, true, &s, NULL);
    CHECK(v.act == FlipperActResync, "3 stepping counters → resync");
    r.rolling = false; r.replay_vuln = true;
    strncpy(r.proto, "PT2262", sizeof(r.proto) - 1);
    flipper_verdict_build(&v, &r, true, false, &s, NULL);
    CHECK(v.conf == FlipperConfForce && v.act == FlipperActReplay, "fixed → replay");
    flipper_verdict_build(&v, &r, false, false, &s, NULL);
    CHECK(v.conf == FlipperConfNone && v.act == FlipperActNone, "no decode");
}

/* ── Sim D: RollingPWN consecutive-burst analyzer ────────────────────────── */
/* Mirrors the FOBpwn run scene: build RollingPwnFrame[] from captured counters,
 * analyze for a resync burst, and assert readiness, ordering, wrap handling and
 * rejection of gaps / duplicates / short runs. */
static void mk_frames(RollingPwnFrame* f, const uint32_t* ctrs, int n) {
    for(int i = 0; i < n; i++) { f[i].counter = ctrs[i]; f[i].cmd = 1; }
}

static void sim_rollingpwn(void) {
    printf("== Sim D: RollingPWN analyzer ==\n");
    RollingPwnFrame f[ROLLINGPWN_MAX_CAPS];
    RollingPwnPlan p;

    /* Clean consecutive run, in order. */
    { uint32_t c[] = {100,101,102}; mk_frames(f,c,3);
      CHECK(rollingpwn_analyze(f,3,0xFFFF,3,1,&p), "3 consecutive → ready");
      CHECK(p.seq_len == 3, "seq_len 3");
      CHECK(p.base_counter == 100 && p.top_counter == 102, "base/top bounds");
      CHECK(p.target_index >= 0 && f[p.target_index].counter == 100, "target = oldest"); }

    /* Out-of-order input still resolves ascending. */
    { uint32_t c[] = {102,100,101}; mk_frames(f,c,3);
      CHECK(rollingpwn_analyze(f,3,0xFFFF,3,1,&p), "unordered burst → ready");
      CHECK(p.base_counter == 100 && p.top_counter == 102, "unordered base/top"); }

    /* Gap breaks a delta-1 run. */
    { uint32_t c[] = {100,101,105}; mk_frames(f,c,3);
      CHECK(!rollingpwn_analyze(f,3,0xFFFF,3,1,&p), "gap → not ready (Δ=1)");
      CHECK(p.seq_len == 2, "best run 2 across gap"); }

    /* Same gap accepted when max_delta widens. */
    { uint32_t c[] = {100,101,105}; mk_frames(f,c,3);
      CHECK(rollingpwn_analyze(f,3,0xFFFF,3,4,&p), "gap ok when Δ≤4"); }

    /* Wrap-around burst. */
    { uint32_t c[] = {0xFFFE,0xFFFF,0x0000}; mk_frames(f,c,3);
      CHECK(rollingpwn_analyze(f,3,0xFFFF,3,1,&p), "wrap burst → ready");
      CHECK(p.base_counter == 0xFFFE && p.span == 2, "wrap base/span"); }

    /* Duplicate counter is not a new code. */
    { uint32_t c[] = {100,100,101}; mk_frames(f,c,3);
      CHECK(!rollingpwn_analyze(f,3,0xFFFF,3,1,&p), "duplicate → not enough"); }

    /* Too few captures. */
    { uint32_t c[] = {100}; mk_frames(f,c,1);
      CHECK(!rollingpwn_analyze(f,1,0xFFFF,3,1,&p), "single capture → not ready"); }

    /* NULL / zero guards never crash. */
    CHECK(!rollingpwn_analyze(NULL,3,0xFFFF,3,1,&p), "NULL frames rejected");
    CHECK(!rollingpwn_analyze(f,0,0xFFFF,3,1,&p), "n=0 rejected");
}

/* ── Sim E: FOBback make/model/profile navigation (incl. Subaru) ─────────── */
static void sim_fbk_nav(void) {
    printf("== Sim E: FOBback navigation + Subaru profiles ==\n");
    CHECK(FLIPPER_FBK_MAKE_COUNT > 0, "FBK make table non-empty");

    for(int mi = 0; mi < FLIPPER_FBK_MAKE_COUNT; mi++) {
        int mc = FLIPPER_FBK_MAKES[mi].model_count;
        CHECK(mc >= 1 && mc <= FBK_MODELS_MAX, "model_count in range");
        for(int mo = 0; mo < mc; mo++) {
            const char* name = flipper_fbk_model_name(mi, mo);
            const FlipperFbkProfile* pr = flipper_fbk_model_profile(mi, mo);
            CHECK(name != NULL, "model name resolves");
            CHECK(pr != NULL, "model profile resolves");
            if(pr) {
                CHECK(pr->freq_count >= 1 && pr->freq_count <= FBK_FREQS_MAX, "freq_count sane");
                CHECK(pr->n_captures >= 1, "n_captures >= 1");
                /* The storage-ceiling bug: every profile must fit the caps array. */
                CHECK(pr->n_captures <= FLIPPER_FBK_CAPS_MAX,
                      "n_captures fits FLIPPER_FBK_CAPS_MAX");
            }
        }
        /* out-of-range indices must return NULL, never crash */
        CHECK(flipper_fbk_model_profile(mi, mc) == NULL, "OOB model → NULL");
        CHECK(flipper_fbk_model_name(mi, -1) == NULL, "negative model → NULL");
    }
    CHECK(flipper_fbk_model_profile(FLIPPER_FBK_MAKE_COUNT, 0) == NULL, "OOB make → NULL");

    /* Subaru make must be present with force-decode profiles. */
    int subaru = -1;
    for(int mi = 0; mi < FLIPPER_FBK_MAKE_COUNT; mi++)
        if(strcmp(FLIPPER_FBK_MAKES[mi].make, "Subaru") == 0) { subaru = mi; break; }
    CHECK(subaru >= 0, "Subaru make present");
    if(subaru >= 0) {
        const FlipperFbkProfile* pr = flipper_fbk_model_profile(subaru, 0);
        CHECK(pr && pr->force == FlipperForceSubaru, "Subaru profile forces Subaru decoder");
    }

    /* Provisional (force-only) makes MUST pin their decoder, or FOBback can
       never reach decode_ok for them (their decoders are not in Auto). */
    const FlipperFbkProfile* pm = flipper_fbk_profile_by_key("maz_315");
    CHECK(pm && pm->force == FlipperForceMazda, "Mazda profile forces Mazda decoder");
    const FlipperFbkProfile* pt = flipper_fbk_profile_by_key("toy_rush");
    CHECK(pt && pt->force == FlipperForceToyota, "Toyota profile forces Toyota decoder");
    const FlipperFbkProfile* pn = flipper_fbk_profile_by_key("nis_latio");
    CHECK(pn && pn->force == FlipperForceNissan, "Nissan profile forces Nissan decoder");

    /* Every FBK profile that pins a decoder must name a real registry id. */
    static const char* fbk_keys[] = {
        "hy_kia_315","hy_kia_433","hy_ix20","nis_latio","nis_sylphy","nis_navara",
        "toy_rush","toy_wigo","maz_315","maz_433","hon_310","hon_433",
        "suz_315","suz_433","genesis_fsk","sub_na_312","sub_433",
    };
    for(int i = 0; i < (int)(sizeof(fbk_keys)/sizeof(fbk_keys[0])); i++) {
        const FlipperFbkProfile* p = flipper_fbk_profile_by_key(fbk_keys[i]);
        CHECK(p != NULL, "FBK key resolves");
        if(p && p->force != FlipperForceAuto) {
            bool found = false;
            for(int d = 0; d < FLIPPER_DECODER_COUNT; d++)
                if(FLIPPER_DECODERS[d].id == p->force) { found = true; break; }
            CHECK(found, "FBK forced id exists in decoder registry");
        }
    }
}

/* ── Sim F: Honda / Subaru decoder synth round-trip ──────────────────────── */
static void emit_bit(FlipperPulseBuf* b, int one, uint32_t te) {
    b->durations[b->len++] = one ? te * 2 : te;   /* HIGH */
    b->durations[b->len++] = te;                   /* LOW  */
}

/* Mirror of sub_csum() in flipper_subaru.c: XOR of every nibble B0..B8 and the
   B9 high nibble, +1, &0xF.  Kept in lockstep with that decoder. */
static uint8_t test_sub_csum(const uint8_t* p) {
    uint8_t cs = 0;
    for(int i = 0; i < 9; i++) { cs ^= (p[i] & 0x0F); cs ^= ((p[i] >> 4) & 0x0F); }
    cs ^= ((p[9] >> 4) & 0x0F);
    return (uint8_t)((cs + 1u) & 0x0F);
}

/* Build the canonical Subaru wire form: 25 alternating preamble halves (starts
   HIGH), a ~6×TE LOW sync gap that absorbs the 0x55 start byte's leading LOW
   half, then the 80-bit frame as Manchester ('1'={H,L}, '0'={L,H}), RLE'd. */
static void emit_subaru2(FlipperPulseBuf* b, const uint8_t pkt[10], uint32_t te) {
    uint8_t lv[400]; int ln = 0;
    for(int k = 0; k < 25; k++) lv[ln++] = (uint8_t)((k % 2 == 0) ? 1 : 0);
    for(int k = 0; k < 5; k++) lv[ln++] = 0;          /* +payload's first LOW → 6×TE */
    for(int bit = 0; bit < 80; bit++) {
        int v = (pkt[bit >> 3] >> (7 - (bit & 7))) & 1;
        if(v) { lv[ln++] = 1; lv[ln++] = 0; } else { lv[ln++] = 0; lv[ln++] = 1; }
    }
    int i = 0;
    while(i < ln) {
        int j = i; while(j < ln && lv[j] == lv[i]) j++;
        b->durations[b->len++] = te * (uint32_t)(j - i);
        i = j;
    }
}

/* Mirror of hk_crc8() in flipper_honda.c: OpenSafety CRC-8, poly 0x2F init 0x00. */
static uint8_t test_hk_crc8(const uint8_t* d, int n) {
    uint8_t c = 0;
    for(int i = 0; i < n; i++) {
        c ^= d[i];
        for(int b = 0; b < 8; b++)
            c = (uint8_t)((c & 0x80) ? ((c << 1) ^ 0x2F) : (c << 1));
    }
    return c;
}

/* KR5V2X wire form: EC 0F preamble + 15 payload bytes (B0=0x62), Manchester
   '1'={HIGH,LOW} '0'={LOW,HIGH}, RLE'd.  Starts HIGH (EC MSB=1) so phase 0
   aligns.  No training run is emitted to keep the frame inside 256 edges. */
static void emit_honda_kr5(FlipperPulseBuf* b, const uint8_t B[15], uint32_t te) {
    uint8_t bits[16 + 120]; int nbit = 0;
    uint8_t pre[2] = {0xEC, 0x0F};
    for(int i = 0; i < 16; i++) bits[nbit++] = (pre[i >> 3] >> (7 - (i & 7))) & 1;
    for(int by = 0; by < 15; by++)
        for(int k = 0; k < 8; k++) bits[nbit++] = (B[by] >> (7 - k)) & 1;
    uint8_t lv[(16 + 120) * 2]; int ln = 0;
    for(int i = 0; i < nbit; i++) {
        if(bits[i]) { lv[ln++] = 1; lv[ln++] = 0; } else { lv[ln++] = 0; lv[ln++] = 1; }
    }
    int i = 0;
    while(i < ln && b->len < FLIPPER_PULSE_MAX - 1) {
        int j = i; while(j < ln && lv[j] == lv[i]) j++;
        b->durations[b->len++] = te * (uint32_t)(j - i);
        i = j;
    }
}

/* Mirror of sz_crc8()/sz_calc_crc() in flipper_suzuki.c: CRC-8 poly 0x7F over
   the six payload bytes at frame bits [59:12]. */
static uint8_t test_sz_calc_crc(uint64_t data) {
    uint8_t cd[6];
    cd[0]=(uint8_t)((data>>52)&0xFF); cd[1]=(uint8_t)((data>>44)&0xFF);
    cd[2]=(uint8_t)((data>>36)&0xFF); cd[3]=(uint8_t)((data>>28)&0xFF);
    cd[4]=(uint8_t)((data>>20)&0xFF); cd[5]=(uint8_t)((data>>12)&0xFF);
    uint8_t c = 0;
    for(int i=0;i<6;i++){ c^=cd[i]; for(int j=0;j<8;j++) c=(uint8_t)((c&0x80)?((c<<1)^0x7F):(c<<1)); }
    return c;
}

/* Suzuki wire form: short/short preamble pairs, then 64 PWM bits MSB-first
   (HIGH 2×TE = '1', 1×TE = '0'), each followed by a 1×TE LOW.  Starts HIGH. */
static void emit_suzuki(FlipperPulseBuf* b, uint64_t data, uint32_t te) {
    for(int i = 0; i < 8; i++) {                 /* 8 short/short preamble pairs */
        b->durations[b->len++] = te;
        b->durations[b->len++] = te;
    }
    for(int k = 0; k < 64 && b->len + 2 <= FLIPPER_PULSE_MAX; k++) {
        int bit = (data >> (63 - k)) & 1;
        b->durations[b->len++] = bit ? te * 2 : te;   /* HIGH carries the bit */
        b->durations[b->len++] = te;                   /* short LOW */
    }
}

static void sim_oem_decoders(void) {
    printf("== Sim F: Honda/Subaru decoder round-trip ==\n");
    FlipperDecodeResult r;

    /* Honda: 10 preamble pairs + 64 LSB-first bits [ser32][cnt16][cmd8][sum8]. */
    {
        uint32_t te = 350;
        uint32_t serial = 0x00A1B2C3;   /* bit0 (LSB) = 1 → data starts with '1' */
        uint32_t counter = 0x1234;
        uint8_t  cmd = 0x02;
        uint8_t by[8];
        by[0]=serial&0xFF; by[1]=(serial>>8)&0xFF; by[2]=(serial>>16)&0xFF; by[3]=(serial>>24)&0xFF;
        by[4]=counter&0xFF; by[5]=(counter>>8)&0xFF; by[6]=cmd;
        uint8_t sum=0; for(int i=0;i<7;i++) sum=(uint8_t)(sum+by[i]); by[7]=sum;

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.92f;
        for(int i=0;i<10;i++){ b.durations[b.len++]=te; b.durations[b.len++]=te; } /* preamble */
        for(int bit=0; bit<64; bit++) emit_bit(&b, (by[bit>>3]>>(bit&7))&1, te);

        CHECK(flipper_decode_honda(&b,&r), "Honda frame decodes");
        CHECK(r.addr == serial, "Honda serial matches");
        CHECK(r.cnt == counter, "Honda counter matches");
        CHECK(r.btn == cmd, "Honda command matches");
        CHECK(r.rolling, "Honda flagged rolling");

        /* Corrupt checksum → reject. */
        b.durations[ (10*2) + 63*2 ] += te; /* flip a high bit in the checksum byte */
        CHECK(!flipper_decode_honda(&b,&r), "Honda bad checksum rejected");
    }

    /* Subaru: canonical 80-bit Manchester — preamble + sync gap + 10-byte frame
       [0x55][ser24][B4][cmd|cmd][ctr20|chk4].  Command nibble is sent twice and
       the checksum is a strong gate, so this decoder is Auto-safe. */
    {
        uint32_t te = 1000;
        uint32_t serial = 0x123456, ctr = 0x54321;
        uint8_t  cmd = 0x2;                            /* Unlock */
        uint8_t pkt[10];
        pkt[0]=0x55;
        pkt[1]=(serial>>16)&0xFF; pkt[2]=(serial>>8)&0xFF; pkt[3]=serial&0xFF;
        pkt[4]=0x00;
        pkt[5]=cmd; pkt[6]=cmd;
        pkt[7]=(ctr>>12)&0xFF; pkt[8]=(ctr>>4)&0xFF; pkt[9]=(uint8_t)((ctr&0xF)<<4);
        pkt[9]=(uint8_t)((pkt[9]&0xF0) | test_sub_csum(pkt));

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.92f;
        emit_subaru2(&b, pkt, te);

        CHECK(flipper_decode_subaru(&b,&r), "Subaru frame decodes");
        CHECK(r.addr == serial, "Subaru serial matches");
        CHECK(r.cnt == ctr, "Subaru counter matches");
        CHECK(r.btn == cmd, "Subaru command matches");
        CHECK(r.rolling, "Subaru flagged rolling");
        CHECK(strcmp(r.proto,"Subaru")==0, "Subaru proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Subaru")==0,
              "Subaru found by Auto chain");

        /* Corrupt the checksum nibble → reject. */
        pkt[9] ^= 0x0F;
        FlipperPulseBuf b2; memset(&b2,0,sizeof(b2)); b2.te_us=te; b2.freq_mhz=433.92f;
        emit_subaru2(&b2, pkt, te);
        CHECK(!flipper_decode_subaru(&b2,&r), "Subaru bad checksum rejected");
    }

    /* Honda KR5V2X: Manchester EC 0F 62 preamble + 15 bytes, OpenSafety CRC-8.
       Documented layout + real CRC gate → Auto-safe (distinct from RollingPWN). */
    {
        uint32_t te = 60;                          /* 60 µs half-symbol */
        uint8_t B[15];
        B[0] = 0x62; B[1] = 0x08;                  /* trailing preamble byte, pkt index */
        uint32_t dev = 0x1A2B3C4D;
        B[2]=(uint8_t)(dev>>24); B[3]=(uint8_t)(dev>>16);
        B[4]=(uint8_t)(dev>>8);  B[5]=(uint8_t)dev;
        B[6] = 0x22;                               /* Unlock */
        uint32_t ctr = 0x00ABCD;
        B[7]=(uint8_t)(ctr>>16); B[8]=(uint8_t)(ctr>>8); B[9]=(uint8_t)ctr;
        uint32_t roll = 0x11223344;
        B[10]=(uint8_t)(roll>>24); B[11]=(uint8_t)(roll>>16);
        B[12]=(uint8_t)(roll>>8);  B[13]=(uint8_t)roll;
        B[14] = test_hk_crc8(B, 14);

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.6f;
        emit_honda_kr5(&b, B, te);
        CHECK(b.len > 0 && b.len < FLIPPER_PULSE_MAX, "Honda KR5 frame fits pulse buffer");

        CHECK(flipper_decode_honda_kr5(&b,&r), "Honda KR5 frame decodes");
        CHECK(r.addr == dev,  "Honda KR5 device id matches");
        CHECK(r.cnt  == ctr,  "Honda KR5 counter matches");
        CHECK(r.hop  == roll, "Honda KR5 rolling code matches");
        CHECK(r.btn  == 0x22, "Honda KR5 event matches");
        CHECK(r.rolling, "Honda KR5 flagged rolling");
        CHECK(strcmp(r.proto,"Honda-KR5V2X")==0, "Honda KR5 proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Honda-KR5V2X")==0,
              "Honda KR5 found by Auto chain");

        /* Corrupt CRC → reject. */
        uint8_t Bc[15]; memcpy(Bc, B, 15); Bc[14] ^= 0xFF;
        FlipperPulseBuf bc; memset(&bc,0,sizeof(bc)); bc.te_us=te; bc.freq_mhz=433.6f;
        emit_honda_kr5(&bc, Bc, te);
        CHECK(!flipper_decode_honda_kr5(&bc,&r), "Honda KR5 bad CRC rejected");
    }

    /* Suzuki: 64-bit PWM rolling code with an 8-bit CRC (poly 0x7F). Auto-safe. */
    {
        uint32_t te = 250;
        uint32_t cnt = 0x5A3C7, serial = 0x1234567; uint8_t btn = 4;   /* Unlock */
        uint64_t data = ((uint64_t)(cnt & 0xFFFFF) << 44) |
                        ((uint64_t)(serial & 0x0FFFFFFF) << 16) |
                        ((uint64_t)(btn & 0xF) << 12);
        data |= ((uint64_t)test_sz_calc_crc(data) << 4);

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.92f;
        emit_suzuki(&b, data, te);
        CHECK(b.len > 0 && b.len < FLIPPER_PULSE_MAX, "Suzuki frame fits pulse buffer");

        CHECK(flipper_decode_suzuki(&b,&r), "Suzuki frame decodes");
        CHECK(r.addr == serial, "Suzuki serial matches");
        CHECK(r.cnt  == cnt,    "Suzuki counter matches");
        CHECK(r.btn  == btn,    "Suzuki button matches");
        CHECK(r.rolling, "Suzuki flagged rolling");
        CHECK(strcmp(r.proto,"Suzuki")==0, "Suzuki proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Suzuki")==0,
              "Suzuki found by Auto chain");

        /* Corrupt the CRC → reject. */
        uint64_t bad = data ^ (0xFFULL << 4);
        FlipperPulseBuf bs; memset(&bs,0,sizeof(bs)); bs.te_us=te; bs.freq_mhz=433.92f;
        emit_suzuki(&bs, bad, te);
        CHECK(!flipper_decode_suzuki(&bs,&r), "Suzuki bad CRC rejected");
    }
}

/* ── Sim G: decoder registry consistency ─────────────────────────────────── */
/* The registry is the single source of truth for the force switch, the Auto
 * chain and the menu labels.  Assert it stays internally consistent so the two
 * old hand-maintained lists can never silently drift back. */
static void sim_registry(void) {
    printf("== Sim G: decoder registry consistency ==\n");

    /* Each force id except Auto should have one registry row. */
    CHECK(FLIPPER_DECODER_COUNT == (int)FlipperForceCount - 1,
          "registry covers every force id (minus Auto)");
    for(int id = 1; id < (int)FlipperForceCount; id++) {
        int hits = 0;
        for(int d = 0; d < FLIPPER_DECODER_COUNT; d++)
            if((int)FLIPPER_DECODERS[d].id == id) hits++;
        CHECK(hits == 1, "each force id appears exactly once");
    }

    /* Names non-empty, fn non-NULL; Auto label is "Auto". */
    for(int d = 0; d < FLIPPER_DECODER_COUNT; d++) {
        CHECK(FLIPPER_DECODERS[d].name && FLIPPER_DECODERS[d].name[0], "row has a name");
        CHECK(FLIPPER_DECODERS[d].fn != NULL, "row has a decode fn");
        CHECK(strcmp(flipper_force_proto_name(FLIPPER_DECODERS[d].id),
                     FLIPPER_DECODERS[d].name) == 0, "name lookup matches row");
    }
    CHECK(strcmp(flipper_force_proto_name(FlipperForceAuto), "Auto") == 0, "Auto label");

    /* Strong OEM parsers are auto_safe; provisional layouts are force-only. */
    struct { FlipperForceProto id; bool want_auto; } tier[] = {
        { FlipperForceKeeloq,   true  }, { FlipperForceGm,     true  },
        { FlipperForceFord,     true  }, { FlipperForceChrysler,true },
        { FlipperForceKia,      true  }, { FlipperForceVag,    true  },
        { FlipperForcePsa,      false }, { FlipperForceMazda,  true  },
        { FlipperForceSubaru,   true  }, { FlipperForceHondaKr5, true },
        { FlipperForceSuzuki,   true  }, { FlipperForceLandRover, true },
        { FlipperForceBmw,      true  },
        { FlipperForceHonda,    false }, { FlipperForceFiat,   true  },
        { FlipperForceScherKhan,false },
        { FlipperForceHoltek,   false }, { FlipperForcePt2262, false },
        { FlipperForceEv1527,   false }, { FlipperForceTpms,   false },
        { FlipperForceFaacSlh,  false }, { FlipperForceDoorhan,false },
        { FlipperForceSecplus2, false }, { FlipperForceAnsonic,false },
        { FlipperForceLinear10, false },
        { FlipperForceCame12,   false }, { FlipperForceNiceFlo, false },
        { FlipperForceToyota,   false }, { FlipperForceNissan, false },
    };
    for(int i = 0; i < (int)(sizeof(tier)/sizeof(tier[0])); i++) {
        for(int d = 0; d < FLIPPER_DECODER_COUNT; d++)
            if(FLIPPER_DECODERS[d].id == tier[i].id)
                CHECK(FLIPPER_DECODERS[d].auto_safe == tier[i].want_auto,
                      "decoder is in the expected tier");
    }

    /* Dispatcher NULL guards never crash. */
    FlipperDecodeResult r;
    CHECK(!flipper_decode(NULL, &r), "flipper_decode NULL buf rejected");
    FlipperPulseBuf b; memset(&b, 0, sizeof(b));
    CHECK(!flipper_decode(&b, NULL), "flipper_decode NULL result rejected");
}

/* ── PWM / Manchester encoders matching the OEM front-ends ───────────────── */
static void emit_pwm_msb(FlipperPulseBuf* b, const uint8_t* by, int nbits,
                         uint32_t te, int leader) {
    if(leader) { b->durations[b->len++] = te * 16; b->durations[b->len++] = te; }
    for(int i = 0; i < nbits; i++) {
        int bit = (by[i >> 3] >> (7 - (i & 7))) & 1;
        b->durations[b->len++] = bit ? te * 2 : te;   /* HIGH */
        b->durations[b->len++] = te;                   /* LOW  */
    }
}

/* Manchester: bit '1' → half-bits {HIGH,LOW}; '0' → {LOW,HIGH}; RLE to runs.
   Requires the first data bit == 1 so durations[0] is HIGH (capture convention). */
static void emit_manch_msb(FlipperPulseBuf* b, const uint8_t* by, int nbits, uint32_t te) {
    uint8_t lv[600]; int ln = 0;
    for(int i = 0; i < nbits; i++) {
        int bit = (by[i >> 3] >> (7 - (i & 7))) & 1;
        if(bit) { lv[ln++] = 1; lv[ln++] = 0; } else { lv[ln++] = 0; lv[ln++] = 1; }
    }
    int i = 0;
    while(i < ln) {
        int j = i; while(j < ln && lv[j] == lv[i]) j++;
        b->durations[b->len++] = te * (uint32_t)(j - i);
        i = j;
    }
}

/* Land Rover V0 differential Manchester.  raw[10] holds frame bits 0..79
   (bit0 is the implicit '1' carried by the sync); extra is bit 80.  The data
   region encodes bits 1..80.  Rule (prev,bit): 0,0={SH,SL} 0,1={LH} 1,0={LL}
   1,1={SL,SH}.  Levels land at the right parity because the differential code
   self-aligns (see decoder). */
static void emit_land_rover(FlipperPulseBuf* b, const uint8_t raw[10], int extra,
                            uint32_t S, uint32_t L, uint32_t SYNC) {
    for(int p = 0; p < 8; p++) { b->durations[b->len++] = S; b->durations[b->len++] = S; }
    b->durations[b->len++] = SYNC; b->durations[b->len++] = SYNC;
    b->durations[b->len++] = S;                       /* boundary short-HIGH */
    int prev = 1;                                     /* bit0 == 1 */
    for(int k = 1; k <= 80; k++) {
        int bit = (k < 80) ? ((raw[k >> 3] >> (7 - (k & 7))) & 1) : (extra & 1);
        if(!prev && !bit)      { b->durations[b->len++] = S; b->durations[b->len++] = S; }
        else if(!prev && bit)  { b->durations[b->len++] = L; }
        else if(prev && !bit)  { b->durations[b->len++] = L; }
        else                   { b->durations[b->len++] = S; b->durations[b->len++] = S; }
        prev = bit;
    }
}

/* ── Sim H: OEM front-ends over the real vendor parsers ──────────────────── */
static void sim_oem_wire(void) {
    printf("== Sim H: OEM pulse front-ends (GM/Ford/Chrysler/KIA/VAG/PSA) ==\n");
    FlipperDecodeResult r;
    uint32_t te = 300;

    /* GM: gm_build → 112-bit PPM, nibble + additive-to-zero checksums. */
    {
        GmFrame f; memset(&f, 0, sizeof(f));
        f.unknown = 0x11; f.button = 0x2; f.id = 0x12345678;   /* Unlock */
        f.seq = 0x00ABCD; f.encrypted[0]=1; f.encrypted[1]=2; f.encrypted[2]=3;
        uint8_t raw[14]; int bits = 0;
        CHECK(gm_build(&f, raw, &bits) && bits == 112, "gm_build ok");
        /* b2 = 0xE2 (checksum nibble 0xE + button 0x2); byte_sum(b1..b13)&0xFF==0. */
        CHECK(raw[2] == 0xE2, "GM b2 nibble checksum");
        { uint32_t s=0; for(int i=1;i<14;i++) s+=raw[i]; CHECK((s & 0xFF)==0, "GM full checksum zero"); }
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=315.0f;
        emit_pwm_msb(&b, raw, 112, te, 1);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceGm), "GM force-decodes");
        CHECK(r.addr == f.id, "GM id matches");
        CHECK(r.cnt == (f.seq & 0xFFFF), "GM counter matches");
        CHECK(r.btn == 0x2, "GM button (low nibble)");
        CHECK(flipper_decode(&b, &r) && strcmp(r.proto,"GM")==0, "GM found by Auto chain");

        /* Corrupt the full checksum → reject. */
        uint8_t bad[14]; memcpy(bad, raw, 14); bad[13] ^= 0x01;
        FlipperPulseBuf bb; memset(&bb,0,sizeof(bb)); bb.te_us=te; bb.freq_mhz=315.0f;
        emit_pwm_msb(&bb, bad, 112, te, 1);
        FlipperDecodeResult rg;
        CHECK(!flipper_decode_ex(&bb, &rg, FlipperForceGm), "GM bad checksum rejected");
    }

    /* Ford V0: ford_v0_build → 64-bit Manchester, CRC-8 + checksum. */
    {
        FordV0Frame f; memset(&f, 0, sizeof(f));
        f.serial = 0x80A5B6; f.counter = 0x1002; f.button = 0x2;  /* bit23 set */
        uint8_t raw[8]; int bits = 0;
        CHECK(ford_v0_build(&f, raw, &bits) && bits == 64, "ford_v0_build ok");
        CHECK((raw[0] & 0x80) != 0, "Ford first bit is 1 (serial MSB set)");
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=315.0f;
        emit_manch_msb(&b, raw, 64, te);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceFord), "Ford V0 force-decodes");
        CHECK(r.addr == f.serial, "Ford serial matches");
        CHECK(r.cnt == f.counter, "Ford counter matches");
    }

    /* Chrysler: chrysler_build → 80-bit PWM. */
    {
        ChryslerFrame f; memset(&f, 0, sizeof(f));
        f.serial = 0x0ABCDEF; f.counter = 0x0A;
        uint8_t raw[10]; int bits = 0;
        CHECK(chrysler_build(&f, raw, &bits) && bits == 80, "chrysler_build ok");
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=315.0f;
        emit_pwm_msb(&b, raw, 80, te, 1);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceChrysler), "Chrysler force-decodes");
        CHECK(r.addr == f.serial, "Chrysler serial matches");
        CHECK(r.cnt == f.counter, "Chrysler counter matches");
    }

    /* KIA V0: craft 64-bit frame + CRC-8 (poly 0x07 over first 7 bytes). */
    {
        uint8_t raw[8];
        uint32_t serial = 0x00C0FFEE & 0xFFFFFF; uint16_t counter = 0x0033; uint8_t btn = 0x04;
        raw[0]=(serial>>16)&0xFF; raw[1]=(serial>>8)&0xFF; raw[2]=serial&0xFF;
        raw[3]=(counter>>8)&0xFF; raw[4]=counter&0xFF; raw[5]=btn; raw[6]=0x00;
        uint8_t crc=0; for(int i=0;i<7;i++){ crc^=raw[i];
            for(int j=0;j<8;j++) crc = (crc&0x80)?(uint8_t)((crc<<1)^0x07):(uint8_t)(crc<<1); }
        raw[7]=crc;
        CHECK((raw[0]&0x80)==0 ? 1 : 1, "kia frame crafted");   /* PWM: leader handles start */
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=315.0f;
        emit_pwm_msb(&b, raw, 64, te, 1);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceKia), "KIA force-decodes");
        CHECK(r.addr == serial, "KIA serial matches");
        CHECK(r.cnt == counter, "KIA counter matches");
    }

    /* Hyundai Santa Fe (TRW): 80-bit PWM, ~375µs HIGH, ~12000µs sync, LOW carries
       the bit; CRC-8 poly 0x31.  Reached via the FlipperForceKia entry. */
    {
        uint32_t rolling = 0x11223344, serial = 0x0A1B2C;
        uint8_t  ctr = 0x09, btn = 0x01;
        uint8_t pkt[10]={0};
        pkt[0]=(rolling>>24)&0xFF; pkt[1]=(rolling>>16)&0xFF; pkt[2]=(rolling>>8)&0xFF; pkt[3]=rolling&0xFF;
        pkt[4]=(serial>>16)&0xFF; pkt[5]=(serial>>8)&0xFF; pkt[6]=serial&0xFF;
        pkt[7]=ctr; pkt[8]=btn;
        uint8_t c=0xFF;
        for(int i=0;i<9;i++){ c^=pkt[i]; for(int k=0;k<8;k++) c=(c&0x80)?(uint8_t)((c<<1)^0x31):(uint8_t)(c<<1); }
        pkt[9]=c;

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=375; b.freq_mhz=433.92f;
        b.durations[b.len++]=375; b.durations[b.len++]=12000;     /* HI + sync LO */
        for(int bit=0; bit<80; bit++) {
            int v=(pkt[bit>>3]>>(7-(bit&7)))&1;
            b.durations[b.len++]=375;                 /* constant HIGH */
            b.durations[b.len++]=v?125:375;           /* LOW carries the bit */
        }
        CHECK(flipper_decode_ex(&b, &r, FlipperForceKia), "SantaFe force-decodes");
        CHECK(r.addr==serial && r.hop==rolling && r.cnt==ctr && r.btn==btn, "SantaFe fields");
        CHECK(strcmp(r.proto,"Hyundai-SantaFe")==0, "SantaFe proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Hyundai-SantaFe")==0, "SantaFe via Auto");

        /* Flip the CRC LSB (bit 79 → durations[161]) → reject. */
        b.durations[161] = (b.durations[161]==125) ? 375 : 125;
        FlipperDecodeResult r2;
        CHECK(!flipper_decode_ex(&b, &r2, FlipperForceKia), "SantaFe bad CRC rejected");
    }

    /* Hyundai/Kia RIO (early, fixed code): 64-bit PWM, ~10400µs sync, HIGH
       carries the bit (728µs=1 / 312µs=0), 16-bit inverted checksum. */
    {
        uint32_t serial = 0x0ABBCCDD; uint16_t btnMask = 0x0021;
        uint16_t c = (uint16_t)((serial ^ (serial>>16)) ^ btnMask);
        uint16_t ck = (uint16_t)(~c);
        uint64_t word = ((uint64_t)serial<<32) | ((uint64_t)btnMask<<16) | ck;

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=312; b.freq_mhz=433.92f;
        b.durations[b.len++]=312; b.durations[b.len++]=10400;    /* HI + sync LO */
        for(int bt=63; bt>=0; bt--) {
            int v=(int)((word>>bt)&1ULL);
            b.durations[b.len++]=v?728:312;           /* HIGH carries the bit */
            b.durations[b.len++]=v?312:728;           /* LOW  complementary   */
        }
        CHECK(flipper_decode_ex(&b, &r, FlipperForceKia), "RIO force-decodes");
        CHECK(r.addr==serial && !r.rolling && r.replay_vuln, "RIO fixed-code fields");
        CHECK(strcmp(r.proto,"Hyundai-RIO")==0, "RIO proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Hyundai-RIO")==0, "RIO via Auto");

        b.durations[128] = (b.durations[128]==728) ? 312 : 728;
        b.durations[129] = (b.durations[129]==312) ? 728 : 312;
        FlipperDecodeResult r3;
        CHECK(!flipper_decode_ex(&b, &r3, FlipperForceKia), "RIO bad checksum rejected");
    }

    /* Kia V7: 64-bit Manchester, wire = ~key.  Key bytes: [0]=0x4C header,
       [1..2]=counter, [3..6]=serial(28)|button(4), [7]=CRC-8 (poly 0x7F,
       init 0x4C).  Only the low 60 bits ride as Manchester (the 0x4 high nibble
       is the sync marker, re-attached by the decoder).  16 preamble ones anchor
       the Manchester run.  FlipperForceKia path (labelled Kia-V7). */
    {
        uint32_t serial = 0x0123456 & 0x0FFFFFFF; uint16_t ctr = 0x0ABC; uint8_t btn = 0x02;
        uint8_t by[8];
        by[0]=0x4C; by[1]=(ctr>>8)&0xFF; by[2]=ctr&0xFF;
        by[3]=(serial>>20)&0xFF; by[4]=(serial>>12)&0xFF; by[5]=(serial>>4)&0xFF;
        by[6]=(uint8_t)(((serial&0x0F)<<4)|(btn&0x0F));
        { uint8_t crc=0x4C; for(int i=0;i<7;i++){ crc^=by[i];
            for(int k=0;k<8;k++) crc=(crc&0x80)?(uint8_t)((crc<<1)^0x7F):(uint8_t)(crc<<1); }
          by[7]=crc; }
        uint64_t key=0; for(int i=0;i<8;i++) key=(key<<8)|by[i];
        uint64_t decode_data = ~key;                 /* wire is one's-complement */

        /* Bit buffer: 16 preamble ones then the low 60 bits (MSB=bit59 first). */
        uint8_t frame[10]; memset(frame,0,sizeof(frame));
        int bi=0;
        for(int p=0;p<16;p++){ frame[bi>>3]|=(uint8_t)(0x80>>(bi&7)); bi++; }
        for(int k=59;k>=0;k--){ if((decode_data>>k)&1ULL) frame[bi>>3]|=(uint8_t)(0x80>>(bi&7)); bi++; }
        /* bi == 76 */

        uint32_t mte = 250;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=mte; b.freq_mhz=433.92f;
        emit_manch_msb(&b, frame, 76, mte);
        CHECK(b.len > 0 && b.len < FLIPPER_PULSE_MAX, "Kia-V7 fits pulse buffer");

        CHECK(flipper_decode_ex(&b, &r, FlipperForceKia), "Kia-V7 force-decodes");
        CHECK(r.addr==serial && r.cnt==ctr && r.btn==btn, "Kia-V7 fields");
        CHECK(strcmp(r.proto,"Kia-V7")==0, "Kia-V7 proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Kia-V7")==0, "Kia-V7 via Auto");

        /* Flip one data bit → CRC fails → reject. */
        uint8_t bad[10]; memcpy(bad,frame,10); bad[5]^=0x08;
        FlipperPulseBuf bb; memset(&bb,0,sizeof(bb)); bb.te_us=mte; bb.freq_mhz=433.92f;
        emit_manch_msb(&bb, bad, 76, mte);
        FlipperDecodeResult r4;
        CHECK(!flipper_decode_ex(&bb, &r4, FlipperForceKia), "Kia-V7 bad CRC rejected");
    }

    /* Land Rover V0: 81-bit differential Manchester.  raw[0..2]=signature
       (0xA285E3 → UNLOCK), raw[3..5]=serial, count=(raw[6]<<1)|(raw[7]>>7),
       raw[7] low 3 bits = count-parity check (bits 3..6 zero), raw[8..9]=tail
       (0xFFFF/0x7FFF by count parity), extra bit = 1.  Auto-safe. */
    {
        uint32_t count = 0x123;                        /* 9-bit */
        uint8_t raw[10]; memset(raw,0,sizeof(raw));
        raw[0]=0xA2; raw[1]=0x85; raw[2]=0xE3;         /* SIG_UNLOCK */
        raw[3]=0x0A; raw[4]=0xBC; raw[5]=0xDE;         /* serial 0x0ABCDE */
        raw[6]=(uint8_t)((count>>1)&0xFF);
        /* 3-bit check from count (linear parity taps, matches decoder). */
        uint8_t c0=((count>>1)^(count>>2)^(count>>3)^(count>>4)^(count>>6))&1;
        uint8_t c1=((count>>0)^(count>>2)^(count>>3)^(count>>4)^(count>>5)^(count>>6)^1)&1;
        uint8_t c2=((count>>1)^(count>>3)^(count>>4)^(count>>5)^(count>>6))&1;
        uint8_t chk=(uint8_t)(c0|(c1<<1)|(c2<<2));
        raw[7]=(uint8_t)(((count&1)<<7) | chk);        /* bits 3..6 stay 0 */
        int tail_msb=(((count>>0)^(count>>2)^(count>>4)^(count>>5))&1)!=0;
        uint16_t tail=tail_msb?0xFFFF:0x7FFF;
        raw[8]=(uint8_t)(tail>>8); raw[9]=(uint8_t)(tail&0xFF);

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=250; b.freq_mhz=433.92f;
        emit_land_rover(&b, raw, 1, 250, 500, 750);
        CHECK(b.len > 0 && b.len < FLIPPER_PULSE_MAX, "Land Rover V0 fits pulse buffer");

        CHECK(flipper_decode_ex(&b, &r, FlipperForceLandRover), "Land Rover V0 force-decodes");
        CHECK(r.addr==0x0ABCDE && r.cnt==count && r.btn==0x04, "Land Rover V0 fields");
        CHECK(strcmp(r.proto,"LandRover-V0")==0, "Land Rover V0 proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"LandRover-V0")==0, "Land Rover V0 via Auto");

        /* Flip one tail bit → tail gate fails → reject. */
        uint8_t bad[10]; memcpy(bad,raw,10); bad[8]^=0x40;
        FlipperPulseBuf bb; memset(&bb,0,sizeof(bb)); bb.te_us=250; bb.freq_mhz=433.92f;
        emit_land_rover(&bb, bad, 1, 250, 500, 750);
        FlipperDecodeResult r5;
        CHECK(!flipper_decode_ex(&bb, &r5, FlipperForceLandRover), "Land Rover V0 bad tail rejected");
    }

    /* BMW CAS3/CAS4 PPM: sync pair (≥10 ms) + constant 250 µs mark / 500|1500 spaces. */
    {
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=250; b.freq_mhz=433.92f;
        b.durations[b.len++] = 20000; b.durations[b.len++] = 20000; /* sync */
        for(int i = 0; i < 72; i++) {
            b.durations[b.len++] = 250;
            b.durations[b.len++] = (i & 1) ? 1500 : 500;
        }
        CHECK(flipper_decode_ex(&b, &r, FlipperForceBmw), "BMW CAS3 force-decodes");
        CHECK(r.bits >= 64 && strcmp(r.proto,"BMW-CAS3-PPM")==0, "BMW CAS3 label/bits");
        CHECK(flipper_decode(&b, &r) && strcmp(r.proto,"BMW-CAS3-PPM")==0,
              "BMW CAS3 found by Auto chain");
        /* No sync → reject (avoids VW Polo false positives). */
        FlipperPulseBuf b2; memset(&b2,0,sizeof(b2)); b2.te_us=250; b2.freq_mhz=433.92f;
        for(int i = 0; i < 72; i++) {
            b2.durations[b2.len++] = 250;
            b2.durations[b2.len++] = (i & 1) ? 1500 : 500;
        }
        CHECK(!flipper_decode_ex(&b2, &r, FlipperForceBmw), "BMW CAS3 without sync rejected");
    }

    /* VAG: craft 64-bit Manchester frame with 0xAF3F preamble + counter@48-59. */
    {
        uint8_t raw[8]; memset(raw,0,sizeof(raw));
        raw[0]=0xAF; raw[1]=0x3F;                 /* AUT64 preamble → first bit 1 */
        uint16_t counter = 0x0AB;                 /* 12-bit, bits 48-59 */
        raw[6]=(uint8_t)((counter>>4)&0xFF);
        raw[7]=(uint8_t)(((counter&0x0F)<<4) | 0x00 /* type nibble */);
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.92f;
        emit_manch_msb(&b, raw, 64, te);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceVag), "VAG force-decodes");
        CHECK(r.cnt == counter, "VAG counter matches");
    }

    /* VAG ID48 (pre-2004): 64-bit PWM, ~11000µs sync, HIGH carries the bit
       (550µs=1 / 250µs=0), inverted-byte-sum checksum.  FlipperForceVag path. */
    {
        uint32_t tid = 0x0ABBCCDD; uint16_t ctr = 0x0140; uint8_t btn = 0x08;
        uint8_t s=0;
        s += (tid>>24)&0xFF; s += (tid>>16)&0xFF; s += (tid>>8)&0xFF; s += tid&0xFF;
        s += (ctr>>8)&0xFF; s += ctr&0xFF; s += btn;
        uint8_t ck = (uint8_t)(~s);
        uint64_t word = ((uint64_t)tid<<32) | ((uint64_t)ctr<<16) | ((uint64_t)btn<<8) | ck;

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=550; b.freq_mhz=433.92f;
        b.durations[b.len++]=550; b.durations[b.len++]=11000;     /* HI + sync LO */
        for(int bt=63; bt>=0; bt--) {
            int v=(int)((word>>bt)&1ULL);
            b.durations[b.len++]=v?550:250;           /* HIGH carries the bit */
            b.durations[b.len++]=v?250:550;           /* LOW  complementary   */
        }
        CHECK(flipper_decode_ex(&b, &r, FlipperForceVag), "VAG-ID48 force-decodes");
        CHECK(r.addr==tid && r.cnt==ctr && r.btn==btn, "VAG-ID48 fields");
        CHECK(strcmp(r.proto,"VAG-ID48")==0, "VAG-ID48 proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"VAG-ID48")==0, "VAG-ID48 via Auto");

        /* Flip the checksum LSB (bit 0 → last HIGH at durations[128]) → reject. */
        b.durations[128] = (b.durations[128]==550) ? 250 : 550;
        b.durations[129] = (b.durations[129]==250) ? 550 : 250;
        FlipperDecodeResult r2;
        CHECK(!flipper_decode_ex(&b, &r2, FlipperForceVag), "VAG-ID48 bad checksum rejected");
    }

    /* PSA: psa_build_mode23 → 128-bit Manchester; pick serial with cipher MSB=1. */
    {
        PsaFrame f; memset(&f,0,sizeof(f));
        uint8_t raw[16]; int len=0; bool ready=false;
        for(uint32_t s=0x80000001u; s && !ready; s+=0x01010101u) {
            memset(&f,0,sizeof(f)); f.serial=s; f.counter=0x0044; f.button=0x1; f.mode=0x23;
            if(psa_build_mode23(&f, raw, &len) && len==16 && (raw[0]&0x80)) ready=true;
        }
        CHECK(ready, "psa_build_mode23 produced a frame with cipher MSB set");
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=te; b.freq_mhz=433.92f;
        emit_manch_msb(&b, raw, 128, te);
        CHECK(flipper_decode_ex(&b, &r, FlipperForcePsa), "PSA force-decodes");
        CHECK(r.cnt == 0x0044, "PSA counter matches");
    }

    /* Mazda V0: [0xFF 0xFF preamble][0xD7 sync][8 inverted key bytes], Manchester.
       Calibrated de-obfuscation + additive checksum, so it is Auto-safe. */
    {
        uint32_t serial = 0x00A1B2C3, counter = 0x0ABCD;
        uint8_t button = 0x02;
        uint64_t rawk = mazda_encode_key(serial, button, counter);
        uint8_t frame[11];
        frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xD7;    /* preamble + sync */
        for(int i = 0; i < 8; i++)
            frame[3 + i] = (uint8_t)(~(uint8_t)(rawk >> (56 - i * 8)));  /* air = ~raw */
        uint32_t mte = 250;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=mte; b.freq_mhz=433.92f;
        emit_manch_msb(&b, frame, 88, mte);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceMazda), "Mazda V0 force-decodes");
        CHECK(r.addr == serial, "Mazda serial matches");
        CHECK(r.cnt == (counter & 0xFFFF), "Mazda counter matches");
        CHECK(r.btn == button, "Mazda button matches");
        CHECK(flipper_decode(&b, &r) && strcmp(r.proto,"Mazda")==0, "Mazda found by Auto chain");

        /* Corrupt the checksum byte's air → reject. */
        frame[10] ^= 0xFF;
        FlipperPulseBuf b2; memset(&b2,0,sizeof(b2)); b2.te_us=mte; b2.freq_mhz=433.92f;
        emit_manch_msb(&b2, frame, 88, mte);
        CHECK(!flipper_decode_ex(&b2, &r, FlipperForceMazda), "Mazda bad checksum rejected");
    }

    /* Mazda V1 (Siemens-VDO): 72-bit PWM, ~450µs HIGH, ~14400µs sync, LOW carries
       the bit; 4-bit XOR checksum.  Same FlipperForceMazda entry, second path. */
    {
        uint32_t hop = 0x1A2B3C4D, serial = 0x0A1B2C;
        uint8_t  ctr = 0x07, btn = 0x2;
        uint8_t pkt[9]={0};
        pkt[0]=(hop>>24)&0xFF; pkt[1]=(hop>>16)&0xFF; pkt[2]=(hop>>8)&0xFF; pkt[3]=hop&0xFF;
        pkt[4]=(serial>>16)&0xFF; pkt[5]=(serial>>8)&0xFF; pkt[6]=serial&0xFF;
        pkt[7]=ctr;
        uint8_t c=0;
        for(int i=0;i<4;i++) c^=(uint8_t)((hop>>(i*8))&0xFF);
        for(int i=0;i<3;i++) c^=(uint8_t)((serial>>(i*8))&0xFF);
        c^=(uint8_t)(ctr^(btn&0xF));
        pkt[8]=(uint8_t)((btn<<4)|(c&0x0F));

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=450; b.freq_mhz=433.92f;
        b.durations[b.len++]=450; b.durations[b.len++]=14400;     /* HI + sync LO */
        for(int bit=0; bit<72; bit++) {
            int v=(pkt[bit>>3]>>(7-(bit&7)))&1;
            b.durations[b.len++]=450;                 /* constant HIGH */
            b.durations[b.len++]=v?1350:450;          /* LOW carries the bit */
        }
        CHECK(flipper_decode_ex(&b, &r, FlipperForceMazda), "Mazda-VDO force-decodes");
        CHECK(r.addr==serial && r.hop==hop && r.cnt==ctr && r.btn==btn, "Mazda-VDO fields");
        CHECK(strcmp(r.proto,"Mazda-VDO")==0, "Mazda-VDO proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Mazda-VDO")==0, "Mazda-VDO via Auto");

        /* Flip the checksum LSB (bit 71 → durations[145]) → reject. */
        b.durations[145] = (b.durations[145]==1350) ? 450 : 1350;
        FlipperDecodeResult r2;
        CHECK(!flipper_decode_ex(&b, &r2, FlipperForceMazda), "Mazda-VDO bad checksum rejected");
    }

    /* Mazda Infinity: Manchester FF FF D7 sync + 8 whitened/inverted bytes +
       0x5A trailer, additive checksum.  Third FlipperForceMazda path. */
    {
        /* Logical payload: [serial32][btn][ctrHi][ctrLo][checksum]. */
        uint32_t serial = 0x11223344; uint8_t btn = 0x20; uint32_t ctr = 0x0ABC;
        uint8_t lg[8];
        lg[0]=(serial>>24)&0xFF; lg[1]=(serial>>16)&0xFF; lg[2]=(serial>>8)&0xFF; lg[3]=serial&0xFF;
        lg[4]=btn; lg[5]=(ctr>>8)&0xFF; lg[6]=ctr&0xFF;
        uint32_t sum=0; for(int i=0;i<7;i++) sum+=lg[i]; lg[7]=(uint8_t)(sum&0xFF);

        /* logical → wire: checksum(set), interleave, whiten(parity), invert. */
        uint8_t b8[8]; memcpy(b8,lg,8);
        { uint8_t b5=b8[5],b6=b8[6]; b8[5]=(b5&0xAA)|(b6&0x55); b8[6]=(b5&0x55)|(b6&0xAA); }
        { uint8_t p=b8[7]; p^=p>>4; p^=p>>2; p^=p>>1; p&=1;
          if(p){ for(int k=0;k<6;k++) b8[k]^=b8[6]; }
          else { b8[0]^=b8[5];b8[1]^=b8[5];b8[2]^=b8[5];b8[3]^=b8[5];b8[4]^=b8[5];b8[6]^=b8[5]; } }
        uint8_t wire[8]; for(int i=0;i<8;i++) wire[i]=(uint8_t)(255u-b8[i]);

        uint8_t frame[14];
        frame[0]=0xFF; frame[1]=0xFF;                 /* preamble ones */
        frame[2]=0xFF; frame[3]=0xFF; frame[4]=0xD7;   /* FF FF D7 sync */
        for(int i=0;i<8;i++) frame[5+i]=wire[i];
        frame[13]=0x5A;                                /* trailer */

        uint32_t mte = 250;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=mte; b.freq_mhz=433.92f;
        emit_manch_msb(&b, frame, 14*8, mte);
        CHECK(b.len > 0 && b.len < FLIPPER_PULSE_MAX, "Mazda-Infinity fits pulse buffer");

        CHECK(flipper_decode_ex(&b, &r, FlipperForceMazda), "Mazda-Infinity force-decodes");
        CHECK(r.addr==serial && r.cnt==ctr && r.btn==btn, "Mazda-Infinity fields");
        CHECK(strcmp(r.proto,"Mazda-Infinity")==0, "Mazda-Infinity proto label");
        CHECK(flipper_decode(&b,&r) && strcmp(r.proto,"Mazda-Infinity")==0, "Mazda-Infinity via Auto");

        /* Corrupt a wire byte → checksum fails → reject. */
        uint8_t bad[14]; memcpy(bad,frame,14); bad[6]^=0xFF;
        FlipperPulseBuf bb; memset(&bb,0,sizeof(bb)); bb.te_us=mte; bb.freq_mhz=433.92f;
        emit_manch_msb(&bb, bad, 14*8, mte);
        FlipperDecodeResult r3;
        CHECK(!flipper_decode_ex(&bb, &r3, FlipperForceMazda), "Mazda-Infinity bad checksum rejected");
    }
}

/* ── Sim I: provisional Mazda/Toyota/Nissan extractor round-trip ──────────── */
static void emit_vehrke(FlipperPulseBuf* b, const uint8_t by[8], int lsb_first, uint32_t te) {
    for(int i = 0; i < 10; i++) { b->durations[b->len++] = te; b->durations[b->len++] = te; }
    for(int bit = 0; bit < 64; bit++) {
        int v = lsb_first ? ((by[bit>>3] >> (bit&7)) & 1)
                          : ((by[bit>>3] >> (7-(bit&7))) & 1);
        b->durations[b->len++] = v ? te*2 : te;
        b->durations[b->len++] = te;
    }
}

static void mk_vehrke_bytes(uint8_t by[8], uint32_t serial, uint16_t counter,
                            uint8_t cmd, int xor_ck) {
    by[0]=(serial>>24)&0xFF; by[1]=(serial>>16)&0xFF;
    by[2]=(serial>>8)&0xFF;  by[3]=serial&0xFF;
    by[4]=(counter>>8)&0xFF; by[5]=counter&0xFF; by[6]=cmd;
    uint8_t ck=0;
    if(xor_ck) { for(int i=0;i<7;i++) ck^=by[i]; }
    else       { for(int i=0;i<7;i++) ck=(uint8_t)(ck+by[i]); }
    by[7]=ck;
}

/* Toyota/Denso: 4 equal preamble pairs at TE, then 40 MSB-first PWM bits:
   '1' = 2×TE HIGH + 1×TE LOW, '0' = 1×TE HIGH + 2×TE LOW.  Raw word is
   [serial24][btn4][ctr12] (structurally read from the enciphered payload). */
static void emit_toyota(FlipperPulseBuf* b, uint32_t serial, uint8_t btn,
                        uint16_t ctr, uint32_t te) {
    uint64_t word = ((uint64_t)(serial & 0xFFFFFF) << 16) |
                    ((uint64_t)(btn & 0xF) << 12) | (uint64_t)(ctr & 0xFFF);
    for(int k = 0; k < 4; k++) { b->durations[b->len++] = te; b->durations[b->len++] = te; }
    for(int i = 39; i >= 0; i--) {
        int v = (int)((word >> i) & 1ULL);
        b->durations[b->len++] = v ? te * 2 : te;      /* HIGH */
        b->durations[b->len++] = v ? te : te * 2;      /* LOW  */
    }
}

static void sim_vehrke(void) {
    printf("== Sim I: Toyota (Denso 40-bit) + provisional Nissan ==\n");
    FlipperDecodeResult r;

    /* Toyota: structural read of the enciphered Denso payload — force-only. */
    {
        uint32_t serial = 0x0F0F0F; uint8_t btn = 0x2; uint16_t ctr = 0x0F0;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=300; b.freq_mhz=315.0f;
        emit_toyota(&b, serial, btn, ctr, 300);
        CHECK(flipper_decode_toyota(&b,&r), "Toyota (Denso) structural decode");
        CHECK(r.addr==serial && r.btn==btn && r.cnt==ctr, "Toyota fields match");
        CHECK(r.bits==40 && r.rolling, "Toyota 40-bit rolling");
        /* No checksum → must never be claimed by the Auto chain. */
        CHECK(!(flipper_decode(&b,&r) && strcmp(r.proto,"Toyota")==0),
              "Toyota never claimed by Auto");
    }
    /* Toyota entropy gate rejects an all-zero (degenerate) payload. */
    {
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=300; b.freq_mhz=315.0f;
        emit_toyota(&b, 0x000001, 0x0, 0x000, 300);   /* 1 transition → below floor */
        CHECK(!flipper_decode_toyota(&b,&r), "Toyota low-entropy rejected");
    }
    /* Nissan: msb-first, XOR checksum — still provisional. */
    {
        uint8_t by[8]; mk_vehrke_bytes(by, 0x81223344, 0x0789, 0x01, 1);
        CHECK((by[0]&0x80)!=0, "Nissan first MSB bit is 1");
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=300; b.freq_mhz=433.92f;
        emit_vehrke(&b, by, 0, 300);
        CHECK(flipper_decode_nissan(&b,&r), "Nissan provisional decodes");
        CHECK(r.addr==0x81223344 && r.cnt==0x0789 && r.btn==0x01, "Nissan fields match");
    }
    /* Nissan must reject a corrupted checksum. */
    {
        uint8_t by[8]; mk_vehrke_bytes(by, 0x81223344, 0x0789, 0x01, 1);
        by[7] ^= 0xFF;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=300; b.freq_mhz=433.92f;
        emit_vehrke(&b, by, 0, 300);
        CHECK(!flipper_decode_nissan(&b,&r), "Nissan bad checksum rejected");
    }
}

/* ── Sim J: Fiat V0 (force) + Fiat V1 (Auto, XOR checksum) ───────────────── */
static void sim_ext_decoders(void) {
    printf("== Sim J: Fiat (Manchester) ==\n");
    FlipperDecodeResult r;

    /* Fiat V0: 64-bit Manchester word [hop 32][fix 32]; first bit (hop MSB) = 1.
       No checksum → force-only (V1/V2 Auto paths must not claim it). */
    {
        uint32_t fix = 0x12345678, hop = 0x9ABCDEF0;   /* hop MSB set */
        uint8_t by[8];
        for(int i = 0; i < 4; i++) by[i]     = (uint8_t)(hop >> (24 - i*8));
        for(int i = 0; i < 4; i++) by[4 + i] = (uint8_t)(fix >> (24 - i*8));
        CHECK((by[0] & 0x80) != 0, "Fiat first Manchester bit is 1");
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=200; b.freq_mhz=433.92f;
        emit_manch_msb(&b, by, 64, 200);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceFiat), "Fiat V0 force-decodes");
        CHECK(r.addr == fix, "Fiat V0 fix/serial matches");
        CHECK(r.hop == hop, "Fiat V0 hop matches");
        CHECK(!(flipper_decode(&b, &r) &&
                (strcmp(r.proto,"Fiat")==0 || strcmp(r.proto,"Fiat-V1")==0 ||
                 strcmp(r.proto,"Fiat-V2")==0 || strcmp(r.proto,"Fiat-V2-FCA")==0)),
              "Fiat V0 never claimed by Auto");
    }

    /* Fiat V1: 104-bit Manchester, header 0x0001, XOR checksum, single-bit btn. */
    {
        uint8_t raw[13];
        memset(raw, 0, sizeof(raw));
        raw[0] = 0x00; raw[1] = 0x01;
        raw[2] = 0x00; raw[3] = 0xAB; raw[4] = 0xCD; raw[5] = 0xEF; /* uid */
        raw[6] = 0x10; /* btn=1 in high nibble, cnt high */
        raw[7] = 0x20; raw[8] = 0x34; raw[9] = 0x56; raw[10] = 0x78; raw[11] = 0x9A;
        uint8_t q = 1;
        for(int i = 0; i < 12; i++) q ^= raw[i];
        raw[12] = q;
        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=250; b.freq_mhz=433.92f;
        emit_manch_msb(&b, raw, 104, 250);
        CHECK(flipper_decode_ex(&b, &r, FlipperForceFiat), "Fiat V1 force-decodes");
        CHECK(r.addr == 0x00ABCDEF, "Fiat V1 serial matches");
        CHECK(r.btn == 1, "Fiat V1 button matches");
        CHECK(flipper_decode(&b, &r) && strcmp(r.proto,"Fiat-V1")==0,
              "Fiat V1 found by Auto chain");
        raw[12] ^= 0xFF;
        FlipperPulseBuf b2; memset(&b2,0,sizeof(b2)); b2.te_us=250; b2.freq_mhz=433.92f;
        emit_manch_msb(&b2, raw, 104, 250);
        /* Force may still hit legacy V0 on the same Manchester edges; the
           Auto/V1 path must not accept a bad XOR checksum as Fiat-V1. */
        bool forced = flipper_decode_ex(&b2, &r, FlipperForceFiat);
        CHECK(!(forced && strcmp(r.proto,"Fiat-V1")==0),
              "Fiat V1 bad XOR checksum rejected");
        CHECK(!(flipper_decode(&b2, &r) && strcmp(r.proto,"Fiat-V1")==0),
              "Fiat V1 bad checksum never Auto");
    }

    /* Scher-Khan / Magicar 51-bit "Dynamic": symmetric PWM (both halves short=0
       / long=1), long-HIGH header + stop, no checksum → force-only. */
    {
        uint32_t S = 750, L = 1100;
        uint64_t data = 0x2ABCD1234ULL;            /* 50-bit payload value */
        uint32_t want_cnt = (uint32_t)(data & 0xFFFF);
        uint8_t  want_btn = (uint8_t)((data >> 24) & 0x0F);
        uint32_t want_sn  = (uint32_t)(((data >> 24) & 0xFFFFFF0) | ((data >> 20) & 0x0F));

        FlipperPulseBuf b; memset(&b,0,sizeof(b)); b.te_us=S; b.freq_mhz=433.92f;
        for(int h=0;h<3;h++){ b.durations[b.len++]=S*2; b.durations[b.len++]=S*2; } /* header */
        b.durations[b.len++]=S; b.durations[b.len++]=S;                             /* start bit */
        for(int k=49;k>=0;k--){                     /* 50 data bits, MSB first */
            uint32_t w = ((data>>k)&1ULL) ? L : S;
            b.durations[b.len++]=w; b.durations[b.len++]=w;
        }
        b.durations[b.len++]=S*2;                   /* long HIGH = stop bit */

        CHECK(flipper_decode_ex(&b, &r, FlipperForceScherKhan), "Scher-Khan force-decodes");
        CHECK(r.bits==51, "Scher-Khan 51-bit dynamic");
        CHECK(r.addr==want_sn && r.btn==want_btn && r.cnt==want_cnt, "Scher-Khan fields");
        CHECK(strcmp(r.proto,"Scher-Khan")==0, "Scher-Khan proto label");
        /* No checksum → must never be claimed by the Auto chain. */
        CHECK(!(flipper_decode(&b, &r) && strcmp(r.proto,"Scher-Khan")==0),
              "Scher-Khan never claimed by Auto");
        {
            FlipperSavedCheck sc;
            flipper_saved_judge(&b, "RAW", &sc);
            CHECK(sc.kind == FlipperSavedForce, "force-only waveform stays Force");
            CHECK(strcmp(sc.line1, "Force only") == 0, "force headline");
        }
    }
}

static void sim_saved_check(void) {
    FlipperPulseBuf b;
    FlipperSavedCheck c;
    int i;
    printf("== Sim C5: SubGHz Saved check ==\n");
    flipper_saved_judge(NULL, "KeeLoq", &c);
    CHECK(c.kind == FlipperSavedNoWave, "no waveform cannot be checked");
    CHECK(strcmp(c.line1, "Cannot check") == 0, "no-wave headline");

    memset(&b, 0, sizeof(b));
    b.te_us = 250;
    b.freq_mhz = 433.92f;
    b.durations[b.len++] = 20000;
    b.durations[b.len++] = 20000;
    for(i = 0; i < 72; i++) {
        b.durations[b.len++] = 250;
        b.durations[b.len++] = (i & 1) ? 1500 : 500;
    }
    flipper_saved_judge(&b, "RAW", &c);
    CHECK(c.kind == FlipperSavedAuto, "RAW BMW file is Auto-valid");
    CHECK(strcmp(c.line1, "Valid") == 0, "RAW headline Valid");
    flipper_saved_judge(&b, "BMW", &c);
    CHECK(c.kind == FlipperSavedAuto && strcmp(c.line1, "Valid") == 0,
          "file label BMW agrees with Auto");
    flipper_saved_judge(&b, "KeeLoq", &c);
    CHECK(strcmp(c.line1, "Other label") == 0, "KeeLoq label on a BMW waveform");

    memset(&b, 0, sizeof(b));
    b.len = 2;
    b.durations[0] = 100;
    b.durations[1] = 100;
    flipper_saved_judge(&b, "RAW", &c);
    CHECK(c.kind == FlipperSavedNone, "short noise matches nothing");
    CHECK(strcmp(c.line1, "No match") == 0, "no-match headline");
}

int main(void) {
    sim_guided_nav();
    sim_kv_parser();
    sim_vault_roundtrip();
    sim_predict_next();
    sim_verdict();
    sim_saved_check();
    sim_rollingpwn();
    sim_fbk_nav();
    sim_oem_decoders();
    sim_registry();
    sim_oem_wire();
    sim_vehrke();
    sim_ext_decoders();
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
