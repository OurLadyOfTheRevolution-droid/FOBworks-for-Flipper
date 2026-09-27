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

int main(void) {
    test_honda_force_only();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}