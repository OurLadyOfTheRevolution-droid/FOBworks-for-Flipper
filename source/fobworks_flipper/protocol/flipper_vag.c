#include "flipper_vag.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* This module parses the VAG AUT64 and Type 2/XTEA formats. It contains three AUT64 keys and the fixed Type 2 TEA key schedule. VW-2 and VW-3 use global master keys without key diversification. A parser match is not proof that a paired vehicle receiver will accept a frame. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* AUT64's 12-round SPN uses 64-bit blocks and a 120-bit key. Packed key layout: { index, key[8], pbox[8], sbox[16] }. */

static const uint8_t vag_keys_packed[VAG_KEYS_COUNT][AUT64_KEY_STRUCT_PACKED_SIZE] = {
    /* Key 1: VW-2, 434.4 MHz Manchester, approximately 2004–2009. */
    { 0x01, 0x37, 0x6C, 0x86, 0xAD, 0xAB, 0xCC, 0x43,
      0x07, 0x4D, 0xE8, 0x59, 0xC1, 0x2F, 0x36, 0xAB },
    /* Key 2: VW-3, 434.4 MHz Manchester, approximately 2006–2009. */
    { 0x02, 0x37, 0x7C, 0x65, 0xCE, 0xDC, 0x42, 0xEA,
      0xA4, 0x53, 0xE8, 0x61, 0xD9, 0xB7, 0x20, 0xFC },
    /* Key 3: VW-4, 434.4 MHz Manchester, approximately 2008–2009. */
    { 0x03, 0x8A, 0xA3, 0x7B, 0x1E, 0x56, 0x1F, 0x83,
      0x84, 0xB6, 0x19, 0xC5, 0x2E, 0x0A, 0x3F, 0xD7 },
};

/* VAG Type 2 uses this fixed 128-bit XTEA key schedule. The TEA delta is 0x9E3779B9, the golden-ratio constant scaled by 2^32. */
static const uint32_t vag_tea_key_schedule[4] = {
    0x0B46502D, 0x5E253718, 0x2BF93A19, 0x622C1206
};

/* AUT64 substitution table: 16 entries, mapping 4-bit values to 4-bit values. */
static const uint8_t aut64_sbox[16] = {
    0x0E, 0x04, 0x0D, 0x01, 0x02, 0x0F, 0x0B, 0x08,
    0x03, 0x0A, 0x06, 0x0C, 0x05, 0x09, 0x00, 0x07
};

/* AUT64 permutation table. */
__attribute__((unused))
static const uint8_t aut64_pbox[8] = {
    0x03, 0x06, 0x07, 0x00, 0x05, 0x02, 0x01, 0x04
};

/* The full VAG firmware has 256 round offsets. This implementation uses a simplified 12-round sequence, so these are the offsets I actually apply. */
static const uint8_t aut64_offsets[12] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B
};

/* I apply the simplified 12-round AUT64 transform used by this implementation. Kept only as a placeholder until the full 256-offset SPN is restored; the current decrypt path does not call it (its result check is vacuous). */
__attribute__((unused))
static uint64_t aut64_encrypt_block(uint64_t plaintext, const uint8_t* key) {
    uint8_t left = (uint8_t)(plaintext >> 32);
    uint8_t right = (uint8_t)(plaintext >> 40);
    uint8_t mid[6];
    for(int i = 0; i < 6; i++)
        mid[i] = (uint8_t)(plaintext >> (8 + i * 8));

    for(int round = 0; round < 12; round++) {
        /* I combine the S-box, key byte, round offset, and Feistel XOR. */
        uint8_t f = aut64_sbox[right & 0x0F] ^
                    (aut64_sbox[(right >> 4) & 0x0F] << 4);
        f ^= key[round % 8];
        f ^= aut64_offsets[round];

        uint8_t new_left = right;
        uint8_t new_right = left ^ f;
        left = new_left;
        right = new_right;
    }

    uint64_t result = ((uint64_t)left << 32) | ((uint64_t)right << 40);
    for(int i = 0; i < 6; i++)
        result |= ((uint64_t)mid[i] << (8 + i * 8));
    return result;
}

/* XTEA encryption helper retained alongside the VAG decrypt path. */
__attribute__((unused))
static void xtea_encrypt(uint32_t* v, const uint32_t* key) {
    uint32_t v0 = v[0], v1 = v[1];
    uint32_t sum = 0;
    uint32_t delta = 0x9E3779B9;

    for(int i = 0; i < 32; i++) {
        sum += delta;
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

static void xtea_decrypt(uint32_t* v, const uint32_t* key) {
    uint32_t v0 = v[0], v1 = v[1];
    uint32_t sum = 0xC6EF3720;  /* delta * 32 */
    uint32_t delta = 0x9E3779B9;

    for(int i = 0; i < 32; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

/* I parse a 64-bit Manchester VAG frame. The preamble is 0xAF3F for Types 1/3/4 or 0xAF1C for Type 2/XTEA. The fields are a 28-bit serial, 4-bit button, 12-bit rolling counter, encrypted payload, and a type byte: 0x00=VW Passat, 0xC0=VW, 0xC1=Audi, 0xC2=Seat, 0xC3=Skoda. */

bool vag_parse_frame(const uint8_t* raw, int raw_bits, VagFrame* out) {
    if(!raw || !out || raw_bits < 64) return false;

    /* I read the 16-bit preamble. */
    uint16_t preamble = 0;
    for(int i = 0; i < 16; i++) {
        preamble = (preamble << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    /* I use the preamble to select the frame type. */
    if(preamble == 0xAF3F) {
        out->type = VagType_AUT64_300us;  /* Type 1, 3, or 4 */
    } else if(preamble == 0xAF1C) {
        out->type = VagType_XTEA;          /* Type 2 */
    } else {
        return false;  /* Not a VAG frame */
    }

    /* I extract the 28-bit serial from bits 16–43. */
    uint32_t serial = 0;
    for(int i = 16; i < 44; i++) {
        serial = (serial << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    out->serial = serial;

    /* I extract the 4-bit button from bits 44–47. */
    uint8_t btn = 0;
    for(int i = 44; i < 48; i++) {
        btn = (btn << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    out->button = btn;

    /* I extract the 12-bit counter from bits 48–59. */
    uint16_t ctr = 0;
    for(int i = 48; i < 60; i++) {
        ctr = (ctr << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    out->counter = ctr;

    /* I extract the type byte from bits 60–63. */
    uint8_t type_byte = 0;
    for(int i = 60; i < 64; i++) {
        type_byte = (type_byte << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    out->type_byte = type_byte;

    /* I map recognized type-byte values to a brand label. */
    switch(type_byte & 0xC0) {
    case 0x00: out->brand = "VW Passat"; break;
    case 0xC0:
        if(type_byte == 0xC0) out->brand = "VW";
        else if(type_byte == 0xC1) out->brand = "Audi";
        else if(type_byte == 0xC2) out->brand = "Seat";
        else if(type_byte == 0xC3) out->brand = "Skoda";
        else out->brand = "VW-Group";
        break;
    default: out->brand = "VW-Group"; break;
    }

    return true;
}

/* I decrypt the payload with the cipher selected by the parsed frame type. */
bool vag_decrypt(const uint8_t* encrypted, int enc_bytes, VagFrame* frame,
                 uint8_t* out_plain, int* out_plain_len) {
    if(!encrypted || !frame || !out_plain || !out_plain_len) return false;
    if(enc_bytes < 8) return false;  /* I need at least one 64-bit block */

    if(frame->type == VagType_XTEA) {
        /* Type 2 uses XTEA. */
        uint32_t v[2];
        memcpy(v, encrypted, 8);
        xtea_decrypt(v, vag_tea_key_schedule);
        memcpy(out_plain, v, 8);
        *out_plain_len = 8;
        return true;
    } else {
        /* Types 1, 3, and 4 use AUT64. The reduced 12-round transform below cannot verify a candidate key: the only check it can perform is `button <= 0x0F`, which is true for every 4-bit value and therefore cannot distinguish a correct key from an incorrect one. Reporting a chosen key_index here was a vacuous claim. I keep decrypt honest: structural parsing (preamble/serial/counter/type-byte) still drives Auto classification, but I do not claim a key until the full 256-offset AUT64 SPN and a distinguishing response check land. */
        (void)encrypted;
        (void)enc_bytes;
        (void)out_plain;
        (void)aut64_encrypt_block;
        (void)vag_keys_packed;
        *out_plain_len = 0;
        frame->key_index = -1;
        return false;
    }
}

/* I advance the counter by the caller-supplied multiplier (typically 1–4). */
uint32_t vag_next_counter(uint32_t current, int multiplier) {
    if(multiplier <= 0) multiplier = 1;
    return (current + multiplier) & 0xFFFFFF;  /* 24-bit counter */
}

/* I access the VAG key table by index. */
const uint8_t* vag_get_key(int index, int* out_len) {
    if(index < 0 || index >= VAG_KEYS_COUNT) {
        if(out_len) *out_len = 0;
        return NULL;
    }
    if(out_len) *out_len = AUT64_KEY_STRUCT_PACKED_SIZE;
    return vag_keys_packed[index];
}

int vag_key_count(void) {
    return VAG_KEYS_COUNT;
}

const char* vag_type_name(VagType type) {
    switch(type) {
    case VagType_AUT64_300us: return "AUT64 (300μs)";
    case VagType_XTEA:        return "XTEA (Type 2)";
    default:                  return "Unknown";
    }
}
