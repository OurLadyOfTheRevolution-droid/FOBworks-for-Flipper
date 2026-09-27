#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* GM Protocol — FOBworks implementation.                                     */
/*   112-bit (14-byte) frames, PPM encoding.                                  */
/*   Additive mod-256 checksums. REPLAY only — cannot forge.                  */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  wake;        /* 0xFF wake byte */
    uint8_t  unknown;     /* unknown field */
    uint8_t  btn_sum;     /* button + partial checksum */
    uint32_t id;          /* 32-bit device ID */
    uint32_t seq;         /* 24-bit sequence number (stored in 32-bit) */
    uint8_t  encrypted[3];/* 24-bit encrypted payload */
    uint8_t  checksum;    /* additive mod-256 checksum */
    uint8_t  button;      /* decoded button */
    const char* function; /* decoded function */
} GmFrame;

bool gm_parse(const uint8_t* raw, int raw_bits, GmFrame* out);
bool gm_build(const GmFrame* f, uint8_t* out, int* out_bits);
