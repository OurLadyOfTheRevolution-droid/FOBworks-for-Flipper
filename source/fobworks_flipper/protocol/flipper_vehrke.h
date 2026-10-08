#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Provisional vehicle-RKE extractor for Mazda, Toyota, and Nissan. No parser for these models is available in this source tree, so this code assumes a generic OOK-PWM frame: a preamble followed by 64 bits laid out as [serial 32][counter 16][command 8][checksum 8]. The field positions and checksum type are guesses, not a recovered spec, and have not been checked against real captures. These decoders stay force-only and outside the Auto chain. A genuine frame may not match until its layout is confirmed. */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef enum {
    VehCkAdd,   /* checksum = sum(bytes[0..6]) & 0xFF */
    VehCkXor,   /* checksum = XOR(bytes[0..6])        */
} VehCkType;

typedef struct {
    uint32_t    te_min;      /* accepted chip-period window, µs */
    uint32_t    te_max;
    bool        lsb_first;   /* bit-to-byte packing order */
    VehCkType   ck;
    const char* proto;       /* result label */
} VehRkeSpec;

/* Shared extractor for the provisional 64-bit OOK-PWM layouts below. */
bool vehrke_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* r,
                   const VehRkeSpec* s);
