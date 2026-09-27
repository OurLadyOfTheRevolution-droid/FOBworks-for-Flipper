#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Provisional vehicle RKE extractor.                                           */
/*                                                                              */
/* Mazda / Toyota / Nissan do not have a published, in-repo frame parser, so    */
/* these decoders use a *provisional* generic OOK-PWM layout: a preamble run     */
/* followed by 64 bits carved as [serial 32][counter 16][command 8][check 8].    */
/* The field offsets and checksum kind are best-effort guesses — they have NOT   */
/* been validated against real captures, so these decoders are force-only        */
/* (never in the Auto chain) and a real fob will only decode once the layout is  */
/* calibrated.  See the follow-up research task.                                 */
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

/* Shared 64-bit OOK-PWM extractor used by the provisional decoders below. */
bool vehrke_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* r,
                   const VehRkeSpec* s);
