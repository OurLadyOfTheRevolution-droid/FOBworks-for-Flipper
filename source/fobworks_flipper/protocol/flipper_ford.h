#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Ford Protocol Decoders — FOBworks implementation.                          */
/*   V0: 64-bit Manchester, CRC matrix, checksum (315/433 MHz, 6-burst)       */
/*   V1: 136-bit Manchester, CRC-16, air-to-plain (433 MHz, 6-burst)          */
/*   V2: 72-bit PWM, sync pattern (433 MHz)                                   */
/*   V3: Manchester, no crypto (decoder only)                                 */
/* ─────────────────────────────────────────────────────────────────────────── */

/* Ford V0 frame */
typedef struct {
    uint32_t serial;       /* 24-bit serial number */
    uint16_t counter;      /* 16-bit rolling counter */
    uint8_t  button;       /* 4-bit button code */
    uint8_t  checksum;     /* additive checksum */
    uint8_t  crc;          /* 8-bit CRC (GF(2) matrix) */
    const char* function;  /* decoded button function */
} FordV0Frame;

/* Ford V1 frame */
typedef struct {
    uint64_t serial;       /* 56-bit serial number */
    uint16_t counter;      /* counter field */
    uint8_t  button;       /* button code */
    const char* function;  /* decoded function */
} FordV1Frame;

/* Ford V2 frame */
typedef struct {
    uint32_t serial;       /* 32-bit serial */
    uint16_t counter;      /* 16-bit counter */
    uint8_t  button;       /* 8-bit button */
    uint8_t  checksum;     /* additive checksum */
} FordV2Frame;

/* Ford V3 frame */
typedef struct {
    uint32_t serial;       /* 32-bit serial */
    uint16_t counter;      /* 16-bit counter */
    uint16_t button;       /* 16-bit button */
    const char* function;  /* decoded function */
} FordV3Frame;

/* Parse/Build Ford V0 */
bool ford_v0_parse(const uint8_t* raw, int raw_bits, FordV0Frame* out);
bool ford_v0_build(FordV0Frame* f, uint8_t* out, int* out_bits);

/* Parse Ford V1/V2/V3 */
bool ford_v1_parse(const uint8_t* raw, int raw_bits, FordV1Frame* out);
bool ford_v2_parse(const uint8_t* raw, int raw_bits, FordV2Frame* out);
bool ford_v3_parse(const uint8_t* raw, int raw_bits, FordV3Frame* out);

/* Button name lookup */
const char* ford_button_name(int version, uint8_t btn);
