#include "flipper_chrysler.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Chrysler Protocol — FOBworks implementation.                               */
/*   80-bit frames, dual-packet (Plain_A + Plain_B), 300/3700μs PWM.          */
/*   16-entry XOR table for transform.                                        */
/* ─────────────────────────────────────────────────────────────────────────── */

/* XOR table (16 entries) */
static const uint8_t chrysler_xor_table[16] = {
    0x0F, 0x02, 0x40, 0x0C, 0x30, 0x0E, 0x70, 0x08,
    0x10, 0x0A, 0x50, 0xF4, 0x2F, 0xF6, 0x6F, 0xF0
};

/* Button detection via b1^b6 XOR patterns */
static const char* chrysler_button_from_xor(uint8_t xor_val) {
    switch(xor_val) {
    case 0x01: return "Lock";
    case 0x02: return "Unlock";
    case 0x04: return "Trunk";
    case 0x08: return "Panic";
    default:   return "Unknown";
    }
}

bool chrysler_parse(const uint8_t* raw, int raw_bits, ChryslerFrame* out) {
    if(!raw || !out || raw_bits < 80) return false;

    /* Extract Plain_A (first 40 bits) and Plain_B (next 40 bits) */
    uint64_t plain_a = 0, plain_b = 0;
    for(int i = 0; i < 40 && i < raw_bits; i++) {
        plain_a = (plain_a << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    for(int i = 40; i < 80 && i < raw_bits; i++) {
        plain_b = (plain_b << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    /* Apply XOR transform to recover original data */
    out->plain_a = plain_a;
    out->plain_b = plain_b;

    /* Extract serial (28 bits from Plain_A) */
    out->serial = (plain_a >> 12) & 0xFFFFFFF;

    /* Extract counter (6 bits) */
    out->counter = plain_a & 0x3F;

    /* Extract button via XOR of b1 and b6 */
    uint8_t b1 = (plain_a >> 8) & 0xFF;
    uint8_t b6 = (plain_b >> 8) & 0xFF;
    uint8_t btn_xor = b1 ^ b6;
    out->button = btn_xor & 0x0F;
    out->function = chrysler_button_from_xor(out->button);

    /* Validate the Plain_A → Plain_B diversification exactly: each of the five
       payload bytes must satisfy plain_b[k] == plain_a[k] ^ table[3+k].  This is
       the inverse of the frame construction, so a genuine frame always passes
       while an unrelated 80-bit air pattern is rejected with 2^-40 leakage —
       without it this parser would claim any Manchester/PWM frame of the right
       length and shadow the later OEM decoders in the Auto chain. */
    for(int k = 0; k < 5; k++) {
        uint8_t a = (uint8_t)((plain_a >> (8 * (4 - k))) & 0xFF);
        uint8_t b = (uint8_t)((plain_b >> (8 * (4 - k))) & 0xFF);
        if(b != (uint8_t)(a ^ chrysler_xor_table[3 + k])) return false;
    }
    return true;
}

bool chrysler_build(const ChryslerFrame* f, uint8_t* out, int* out_bits) {
    if(!f || !out || !out_bits) return false;

    /* Pack Plain_A */
    uint64_t plain_a = 0;
    plain_a |= ((uint64_t)f->serial & 0xFFFFFFF) << 12;
    plain_a |= (uint64_t)f->counter & 0x3F;

    /* Build Plain_B using XOR table transform */
    uint64_t plain_b = 0;
    for(int i = 0; i < 8; i++) {
        uint8_t byte_a = (plain_a >> (8 * (7 - i))) & 0xFF;
        uint8_t byte_b = byte_a ^ chrysler_xor_table[i];
        plain_b |= ((uint64_t)byte_b << (8 * (7 - i)));
    }

    /* Pack both into output buffer */
    for(int i = 0; i < 5; i++) {
        out[i] = (plain_a >> (8 * (4 - i))) & 0xFF;
        out[i + 5] = (plain_b >> (8 * (4 - i))) & 0xFF;
    }

    *out_bits = 80;
    return true;
}

const char* chrysler_function_name(uint8_t btn) {
    return chrysler_button_from_xor(btn);
}
