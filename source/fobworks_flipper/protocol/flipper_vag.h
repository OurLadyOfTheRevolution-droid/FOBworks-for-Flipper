#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* VAG AUT64/XTEA Protocol — FOBworks implementation.                         */
/*   3 hardcoded AUT64 keys + TEA key schedule for VAG Type 2.                */
/*   VW-2 and VW-3 use fixed global master keys (no key diversification).     */
/* ─────────────────────────────────────────────────────────────────────────── */

#define VAG_KEYS_COUNT              3
#define AUT64_KEY_STRUCT_PACKED_SIZE 16

/* VAG frame types */
typedef enum {
    VagType_AUT64_300us = 0,  /* Types 1/3/4: AUT64 encrypted, 300/500μs Manchester */
    VagType_XTEA,              /* Type 2: XTEA encrypted, 300μs Manchester */
} VagType;

/* Parsed VAG frame */
typedef struct {
    VagType      type;
    uint32_t     serial;       /* 28-bit serial number */
    uint8_t      button;       /* 4-bit button code */
    uint16_t     counter;      /* 12-bit rolling counter */
    uint8_t      type_byte;    /* brand identifier (0x00/0xC0/0xC1/0xC2/0xC3) */
    const char*  brand;        /* "VW Passat", "VW", "Audi", "Seat", "Skoda" */
    int          key_index;    /* which of the 3 keys decrypted successfully */
} VagFrame;

/* Parse a raw VAG frame from bitstream. Returns true if preamble matches. */
bool vag_parse_frame(const uint8_t* raw, int raw_bits, VagFrame* out);

/* Decrypt encrypted payload. Tries all 3 known keys for AUT64 types. */
bool vag_decrypt(const uint8_t* encrypted, int enc_bytes, VagFrame* frame,
                 uint8_t* out_plain, int* out_plain_len);

/* Predict next counter value (24-bit, wraps at 0xFFFFFF). */
uint32_t vag_next_counter(uint32_t current, int multiplier);

/* Get a known VAG key by index (0-2). */
const uint8_t* vag_get_key(int index, int* out_len);
int vag_key_count(void);

/* Human-readable type name. */
const char* vag_type_name(VagType type);
