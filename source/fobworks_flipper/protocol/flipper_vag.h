#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* VAG AUT64 and Type 2/XTEA helpers. This implementation carries three AUT64 keys and the fixed Type 2 key schedule; VW-2 and VW-3 use global keys without diversification. A successful parse or decrypt is not receiver validation. */
/* ─────────────────────────────────────────────────────────────────────────── */

#define VAG_KEYS_COUNT              3
#define AUT64_KEY_STRUCT_PACKED_SIZE 16

/* Cipher family identified by the frame preamble. */
typedef enum {
    VagType_AUT64_300us = 0,  /* Types 1/3/4: AUT64 encrypted, 300/500μs Manchester */
    VagType_XTEA,              /* Type 2: XTEA encrypted, 300μs Manchester */
} VagType;

/* Fields recovered from a VAG frame. */
typedef struct {
    VagType      type;
    uint32_t     serial;       /* 28-bit serial number */
    uint8_t      button;       /* 4-bit button code */
    uint16_t     counter;      /* 12-bit rolling counter */
    uint8_t      type_byte;    /* brand identifier (0x00/0xC0/0xC1/0xC2/0xC3) */
    const char*  brand;        /* "VW Passat", "VW", "Audi", "Seat", "Skoda" */
    int          key_index;    /* key index selected by the decrypt routine */
} VagFrame;

/* I parse a raw bitstream. A matching preamble identifies a supported layout. */
bool vag_parse_frame(const uint8_t* raw, int raw_bits, VagFrame* out);

/* I decrypt the payload. The current AUT64 result check cannot distinguish a valid key from an incorrect one, so a successful return is not verification. */
bool vag_decrypt(const uint8_t* encrypted, int enc_bytes, VagFrame* frame,
                 uint8_t* out_plain, int* out_plain_len);

/* I advance the counter by multiplier, wrapping at 0xFFFFFF. */
uint32_t vag_next_counter(uint32_t current, int multiplier);

/* I return a bundled VAG key by index (0–2). */
const uint8_t* vag_get_key(int index, int* out_len);
int vag_key_count(void);

/* I return the display name for a frame type. */
const char* vag_type_name(VagType type);
