#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Chrysler Protocol — FOBworks implementation.                               */
/*   80-bit frames, dual-packet (Plain_A + Plain_B), 300/3700μs PWM.          */
/*   16-entry XOR table for transform.                                        */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t     serial;    /* 28-bit serial number */
    uint8_t      counter;   /* 6-bit counter */
    uint8_t      button;    /* 4-bit button (XOR of b1^b6) */
    uint64_t     plain_a;   /* first 40-bit packet */
    uint64_t     plain_b;   /* second 40-bit packet */
    const char*  function;  /* decoded button function */
} ChryslerFrame;

bool chrysler_parse(const uint8_t* raw, int raw_bits, ChryslerFrame* out);
bool chrysler_build(const ChryslerFrame* f, uint8_t* out, int* out_bits);
const char* chrysler_function_name(uint8_t btn);
