#include "../protocol/flipper_decoders.h"
#include "../protocol/flipper_keeloq.h"
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void expect(int ok, const char* label) {
    checks++;
    if(!ok) {
        printf("  FAIL: %s\n", label);
        failures++;
    }
}

/* ── KeeLoq eavesdrop-only clone path ─────────────────────────────────────── */
/* Prove that kl_derive_device_key + flipper_kl_clone_next recover the counter
   and synthesize a valid NEXT frame from only a capture and the manufacturer
   key — the two-message, no-physical-access clone. */
static void test_keeloq_clone(void) {
    uint64_t mfr = 0x1122334455667788ULL;
    uint32_t sn = 0x0ABCDEF;
    uint8_t btn = 0x2;

    /* Build the device key the way AN1064 secure-learning would, then encrypt
       a known counter to form a captured frame. */
    uint64_t seed = (uint64_t)(sn) | ((uint64_t)(sn) << 28);
    uint64_t dev = kl_encrypt((uint32_t)(seed & 0xFFFFFFFFu), mfr) |
                   ((uint64_t)kl_encrypt((uint32_t)(seed >> 32), mfr) << 32);

    uint32_t cnt = 0x1234;
    /* Real HCS3xx plaintext: [31:28]=button, [27:26]=reserved, [25:16]=10-bit
       discriminator (== SN[9:0]), [15:0]=counter. The decoder's kl_try_one()
       gate requires the discriminator to equal sn & 0x3FF. */
    uint32_t plain = ((uint32_t)(btn & 0xF) << 28) |
                     ((sn & 0x3FFu) << 16) | cnt;
    uint32_t enc = kl_encrypt(plain, dev);

    KLFrame captured;
    memset(&captured, 0, sizeof(captured));
    captured.enc = enc;
    captured.sn = sn;
    captured.btn = btn;

    /* Derivation round-trips. */
    uint64_t recovered_dev = 0;
    expect(kl_derive_device_key(mfr, sn, &recovered_dev),
           "device key derives from manufacturer key + serial");
    expect(recovered_dev == dev, "derived device key matches ground truth");

    /* Clone-next synthesizes the counter+1 frame and recovers the fields. */
    FlipperPulseBuf out;
    FlipperDecodeResult dr;
    expect(flipper_kl_clone_next(&captured, mfr, 400, 433.92f, &out, &dr),
           "clone-next synthesizes a frame");
    expect(out.len > 0 && out.len < FLIPPER_PULSE_MAX,
           "clone frame has a bounded pulse count");
    expect(dr.cnt == ((cnt + 1) & 0xFFFF), "clone frame advances the counter");
    expect(dr.btn == btn, "clone frame preserves the button");
    expect(dr.hop == kl_encrypt(((uint32_t)(btn & 0xF) << 28) |
                                ((sn & 0x3FFu) << 16) |
                                ((cnt + 1) & 0xFFFF), dev),
           "clone hop decrypts to next counter under the device key");

    /* Wrong button fails. */
    captured.btn = 0x4;
    expect(!flipper_kl_clone_next(&captured, mfr, 400, 433.92f, &out, &dr),
           "clone rejects a button mismatch");
}

static void make_honda_vector(FlipperPulseBuf* buf, bool bad_checksum) {
    uint8_t bytes[8] = {0xFF, 0x56, 0x34, 0x12, 0x34, 0x12, 0x02, 0xE3};
    if(bad_checksum) bytes[7] ^= 0x01;

    memset(buf, 0, sizeof(*buf));
    buf->te_us = 200;
    buf->freq_mhz = 313.55f;
    for(int i = 0; i < 8; i++) {
        buf->durations[buf->len++] = 100;
        buf->durations[buf->len++] = 100;
    }
    for(int b = 0; b < 64; b++) {
        int bit = (bytes[b >> 3] >> (b & 7)) & 1;
        buf->durations[buf->len++] = bit ? 400 : 100;
        buf->durations[buf->len++] = 100;
    }
}

static void test_honda_force_only(void) {
    expect(FlipperForceHonda > FlipperForceAuto &&
               FlipperForceHonda < FlipperForceCount &&
               FlipperForceToyota > FlipperForceHonda,
           "Honda force id stays distinct from Auto and Toyota");

    FlipperPulseBuf buf;
    FlipperDecodeResult result;
    make_honda_vector(&buf, false);
    expect(flipper_decode_ex(&buf, &result, FlipperForceHonda),
           "forced provisional Honda vector decodes");
    expect(result.addr == 0x123456FFu && result.cnt == 0x1234 &&
               result.btn == 0x02 && result.rolling,
           "Honda source-layout fields extracted");
    expect(strcmp(result.proto, "Honda-RKE?") == 0 &&
               result.predict_window == 0 && result.predict_lo == 0 &&
               result.predict_hi == 0 &&
               strstr(result.predict_note, "unverified") != NULL,
           "Honda result discloses unverified layout and disables prediction");
    flipper_kl_predict(&result, NULL);
    expect(result.predict_window == 0 &&
               strstr(result.predict_note, "unverified") != NULL,
           "prediction helper preserves Honda integrity warning");

    expect(!flipper_decode_ex(&buf, &result, FlipperForceAuto),
           "provisional Honda vector is not claimed in Auto");

    make_honda_vector(&buf, true);
    expect(!flipper_decode_ex(&buf, &result, FlipperForceHonda),
           "provisional Honda checksum mismatch rejected");

    make_honda_vector(&buf, false);
    buf.len = FLIPPER_PULSE_MAX + 1;
    expect(!flipper_decode_ex(&buf, &result, FlipperForceHonda),
           "oversized edge count rejected before pulse reads");
}

static uint32_t secplus1_rev32(uint32_t n) {
    uint32_t r = 0;
    for(int i = 0; i < 32; i++) {
        r = (r << 1) | (n & 1u);
        n >>= 1;
    }
    return r;
}

static void push_dur(FlipperPulseBuf* b, uint32_t d) {
    if(b->len < FLIPPER_PULSE_MAX) b->durations[b->len++] = d;
}

static void make_secplus1_vector(FlipperPulseBuf* buf, uint32_t rolling, uint32_t fixed) {
    uint32_t te = 500;
    memset(buf, 0, sizeof(*buf));
    buf->te_us = te;
    buf->freq_mhz = 315.0f;
    rolling &= 0xFFFFFFFEu;
    uint32_t rr = secplus1_rev32(rolling);
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
    memcpy(bits + nb, pat[0], 4); nb += 4;
    for(int i = 0; i < 20; i++) { memcpy(bits + nb, pat[code[i]], 4); nb += 4; }
    for(int i = 0; i < 40; i++) bits[nb++] = 0;
    memcpy(bits + nb, pat[2], 4); nb += 4;
    for(int i = 20; i < 40; i++) { memcpy(bits + nb, pat[code[i]], 4); nb += 4; }
    for(int i = 0; i < 40; i++) bits[nb++] = 0;

    push_dur(buf, te);
    uint8_t cur = 0;
    uint32_t run = 0;
    for(int i = 0; i < nb; i++) {
        if(bits[i] == cur) run++;
        else {
            if(run) push_dur(buf, run * te);
            cur = bits[i];
            run = 1;
        }
    }
    if(run) push_dur(buf, run * te);
}

static void test_secplus1_ternary(void) {
    FlipperPulseBuf buf;
    FlipperDecodeResult result;
    uint32_t rolling = 0x12345678u & 0xFFFFFFFEu;
    uint32_t fixed = 12345;
    make_secplus1_vector(&buf, rolling, fixed);
    expect(flipper_decode_ex(&buf, &result, FlipperForceSecplus1),
           "ternary Security+1.0 vector decodes");
    expect(result.addr == fixed && result.cnt == rolling,
           "Security+1.0 rolling and fixed recovered");
    expect(strcmp(result.proto, "Security+1.0") == 0, "protocol label");
    expect(!flipper_decode_ex(&buf, &result, FlipperForceAuto),
           "Security+1.0 stays force-only");
}

static uint32_t secplus2_rev28(uint32_t n) {
    uint32_t r = 0;
    for(int i = 0; i < 28; i++) {
        r = (r << 1) | (n & 1u);
        n >>= 1;
    }
    return r;
}

static const uint8_t SP2_ORDER_T[11][3] = {
    {0, 2, 1}, {2, 0, 1}, {0, 1, 2}, {0, 0, 0}, {1, 2, 0}, {1, 0, 2},
    {2, 1, 0}, {0, 0, 0}, {1, 2, 0}, {2, 1, 0}, {0, 1, 2},
};
static const uint8_t SP2_INVERT_T[11][3] = {
    {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}, {1, 1, 1}, {1, 0, 1},
    {0, 1, 1}, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}, {1, 0, 1},
};

static void make_secplus2_vector(
    FlipperPulseBuf* buf, uint32_t rolling, uint64_t fixed) {
    uint32_t te = 250;
    memset(buf, 0, sizeof(*buf));
    buf->te_us = te;
    buf->freq_mhz = 315.0f;
    rolling &= 0x0FFFFFFFu;
    fixed &= 0xFFFFFFFFFFull;

    uint32_t rr = secplus2_rev28(rolling);
    uint8_t rb[18];
    for(int i = 17; i >= 0; i--) {
        rb[i] = (uint8_t)(rr % 3u);
        rr /= 3u;
    }
    uint8_t roll1[9], roll2[9];
    memcpy(roll1, rb + 14, 4);
    memcpy(roll1 + 4, rb + 6, 4);
    roll1[8] = rb[1];
    memcpy(roll2, rb + 10, 4);
    memcpy(roll2 + 4, rb + 2, 4);
    roll2[8] = rb[0];

    uint8_t fb[40];
    for(int i = 0; i < 40; i++)
        fb[i] = (uint8_t)((fixed >> (39 - i)) & 1u);

    uint8_t halves[2][40];
    for(int h = 0; h < 2; h++) {
        const uint8_t* rolling_h = h ? roll2 : roll1;
        const uint8_t* fixed_h = h ? fb + 20 : fb;
        uint8_t ind[8];
        for(int i = 0; i < 4; i++) {
            ind[i * 2] = (uint8_t)(rolling_h[i] >> 1);
            ind[i * 2 + 1] = (uint8_t)(rolling_h[i] & 1u);
        }
        uint8_t parts[3][18];
        memcpy(parts[0], fixed_h, 10);
        memcpy(parts[1], fixed_h + 10, 10);
        for(int i = 0; i < 5; i++) {
            parts[2][i * 2] = (uint8_t)(rolling_h[4 + i] >> 1);
            parts[2][i * 2 + 1] = (uint8_t)(rolling_h[4 + i] & 1u);
        }
        uint8_t okey =
            (uint8_t)((ind[0] << 3) | (ind[1] << 2) | (ind[2] << 1) | ind[3]);
        uint8_t ikey =
            (uint8_t)((ind[4] << 3) | (ind[5] << 2) | (ind[6] << 1) | ind[7]);
        const uint8_t* order = SP2_ORDER_T[okey];
        const uint8_t* invert = SP2_INVERT_T[ikey];
        uint8_t pp[3][18];
        for(int i = 0; i < 3; i++) {
            memcpy(pp[i], parts[order[i]], 10);
            if(invert[i])
                for(int j = 0; j < 10; j++) pp[i][j] ^= 1;
        }
        uint8_t* out = halves[h];
        out[0] = 0;
        out[1] = 0;
        memcpy(out + 2, ind, 8);
        for(int i = 0; i < 10; i++) {
            out[10 + i * 3] = pp[0][i];
            out[11 + i * 3] = pp[1][i];
            out[12 + i * 3] = pp[2][i];
        }
    }

    uint8_t bits[80];
    uint8_t chips[400];
    int nc = 0;
    for(int p = 0; p < 2; p++) {
        int nb = 0;
        for(int i = 0; i < 16; i++) bits[nb++] = 0;
        for(int i = 0; i < 4; i++) bits[nb++] = 1;
        bits[nb++] = 0;
        bits[nb++] = (uint8_t)p;
        memcpy(bits + nb, halves[p], 40);
        nb += 40;
        for(int i = 0; i < nb && nc + 2 <= (int)sizeof(chips); i++) {
            if(bits[i] == 0) {
                chips[nc++] = 1;
                chips[nc++] = 0;
            } else {
                chips[nc++] = 0;
                chips[nc++] = 1;
            }
        }
        if(p == 0)
            for(int i = 0; i < 33 && nc < (int)sizeof(chips); i++) chips[nc++] = 0;
    }
    uint8_t cur = chips[0];
    uint32_t run = 1;
    for(int i = 1; i < nc; i++) {
        if(chips[i] == cur) run++;
        else {
            push_dur(buf, run * te);
            cur = chips[i];
            run = 1;
        }
    }
    if(run) push_dur(buf, run * te);
}

static void test_secplus2_manchester(void) {
    FlipperPulseBuf buf;
    FlipperDecodeResult result;
    uint32_t rolling = 0x123456u;
    /* bits 35..32 = button; 0x2A… → button 0xA. Keep out of {0, 0xF}. */
    uint64_t fixed = 0x2A2B3C4D5Eull;
    make_secplus2_vector(&buf, rolling, fixed);
    expect(flipper_decode_ex(&buf, &result, FlipperForceSecplus2),
           "Manchester Security+2.0 vector decodes");
    expect(result.cnt == rolling && result.btn == 0xAu,
           "Security+2.0 rolling and button recovered");
    expect(strcmp(result.proto, "Security+2.0") == 0, "Sec+2.0 protocol label");
    expect(result.predict_window == 0, "Sec+2.0 prediction stays off");
    /* Registry keeps Sec+2.0 force-only. Another Auto decoder (e.g. KeeLoq)
       may still claim the manchester edges; that is not a Sec+2.0 Auto path. */
    bool auto_hit = flipper_decode_ex(&buf, &result, FlipperForceAuto);
    expect(!auto_hit || strcmp(result.proto, "Security+2.0") != 0,
           "Security+2.0 stays force-only");
}

int main(void) {
    test_honda_force_only();
    test_secplus1_ternary();
    test_secplus2_manchester();
    test_keeloq_clone();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}