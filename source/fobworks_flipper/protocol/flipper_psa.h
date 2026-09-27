#pragma once
#include <stdint.h>
#include <stdbool.h>

/* PSA (Peugeot/Citroen): Mode 0x23 XOR plus checksum. Mode 0x36 is not in
   this image. Manchester 250/500 us. */

typedef struct {
    uint32_t     serial;    /* 32-bit serial number */
    uint16_t     counter;   /* 16-bit rolling counter */
    uint8_t      button;    /* button code */
    uint8_t      mode;      /* 0x23 = XOR */
    const char*  function;  /* decoded button function */
} PsaFrame;

/* Mode 0x23: Direct XOR decryption (fast, no search) */
bool psa_decrypt_mode23(const uint8_t* encrypted, int enc_len, PsaFrame* out);

/* Build Mode 0x23 frame for transmission */
bool psa_build_mode23(const PsaFrame* f, uint8_t* out, int* out_len);
