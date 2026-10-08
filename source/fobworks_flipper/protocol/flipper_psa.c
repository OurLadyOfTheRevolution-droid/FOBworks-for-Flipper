#include "flipper_psa.h"

/* This build supports PSA Mode 0x23, which uses XOR and a checksum. Mode 0x36
   is not implemented here. Captures use Manchester timing near 250/500 µs. */

/* I decode a PSA Mode 0x23 frame with its fixed XOR pattern. */
bool psa_decrypt_mode23(const uint8_t* encrypted, int enc_len, PsaFrame* out) {
    if(!encrypted || !out || enc_len < 16) return false;

    /* I undo Mode 0x23 by XORing each byte with its fixed pattern. */
    uint8_t decrypted[16];
    for(int i = 0; i < 16; i++) {
        decrypted[i] = encrypted[i] ^ (i & 0xFF);
    }

    /* I read the decoded fields. */
    out->serial = ((uint32_t)decrypted[0] << 24) |
                  ((uint32_t)decrypted[1] << 16) |
                  ((uint32_t)decrypted[2] << 8) |
                  decrypted[3];
    out->counter = ((uint16_t)decrypted[4] << 8) | decrypted[5];
    out->button = decrypted[6];
    out->mode = 0x23;

    /* I check the sum of the decrypted bytes modulo 256. */
    uint8_t cksum = 0;
    for(int i = 0; i < 15; i++) cksum += decrypted[i];
    if(decrypted[15] != cksum) return false;

    /* I convert the button code to its display label. */
    switch(out->button) {
    case 0x01: out->function = "Lock"; break;
    case 0x02: out->function = "Unlock"; break;
    case 0x04: out->function = "Trunk"; break;
    case 0x08: out->function = "Panic"; break;
    default:   out->function = "Unknown"; break;
    }

    return true;
}

/* I build a PSA Mode 0x23 frame. */
bool psa_build_mode23(const PsaFrame* f, uint8_t* out, int* out_len) {
    if(!f || !out || !out_len) return false;

    uint8_t decrypted[16] = {0};
    decrypted[0] = (f->serial >> 24) & 0xFF;
    decrypted[1] = (f->serial >> 16) & 0xFF;
    decrypted[2] = (f->serial >> 8) & 0xFF;
    decrypted[3] = f->serial & 0xFF;
    decrypted[4] = (f->counter >> 8) & 0xFF;
    decrypted[5] = f->counter & 0xFF;
    decrypted[6] = f->button;

    /* I add the checksum. */
    uint8_t cksum = 0;
    for(int i = 0; i < 15; i++) cksum += decrypted[i];
    decrypted[15] = cksum;

    /* I apply the Mode 0x23 XOR pattern. */
    for(int i = 0; i < 16; i++) {
        out[i] = decrypted[i] ^ (i & 0xFF);
    }

    *out_len = 16;
    return true;
}
