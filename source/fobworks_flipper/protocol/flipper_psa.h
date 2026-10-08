#pragma once
#include <stdint.h>
#include <stdbool.h>

/* This build supports PSA Mode 0x23, which uses XOR and a checksum. Mode 0x36
   is not implemented here. Captures use Manchester timing near 250/500 µs. */

typedef struct {
    uint32_t     serial;    /* 32-bit serial number */
    uint16_t     counter;   /* 16-bit rolling counter */
    uint8_t      button;    /* button code */
    uint8_t      mode;      /* 0x23 = XOR */
    const char*  function;  /* decoded button function */
} PsaFrame;

/* I decode Mode 0x23 with its direct XOR operation; no key search is needed. */
bool psa_decrypt_mode23(const uint8_t* encrypted, int enc_len, PsaFrame* out);

/* I build a frame using the Mode 0x23 layout. */
bool psa_build_mode23(const PsaFrame* f, uint8_t* out, int* out_len);
