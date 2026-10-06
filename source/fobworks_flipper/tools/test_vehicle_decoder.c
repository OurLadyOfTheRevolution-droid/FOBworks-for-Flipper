#include "../protocol/flipper_decoders.h"
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

int main(void) {
    test_honda_force_only();
    test_secplus1_ternary();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}