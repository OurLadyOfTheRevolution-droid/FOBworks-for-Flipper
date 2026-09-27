#include "flipper_psa.h"

/* PSA (Peugeot/Citroen): Mode 0x23 XOR plus checksum. Mode 0x36 is not in
   this image. Manchester 250/500 us. */

/* ── PSA Mode 0x23: Direct XOR Decryption ───────────────────────────────── */
bool psa_decrypt_mode23(const uint8_t* encrypted, int enc_len, PsaFrame* out) {
    if(!encrypted || !out || enc_len < 16) return false;

    /* Mode 0x23: XOR each byte with a fixed pattern */
    uint8_t decrypted[16];
    for(int i = 0; i < 16; i++) {
        decrypted[i] = encrypted[i] ^ (i & 0xFF);
    }

    /* Extract fields */
    out->serial = ((uint32_t)decrypted[0] << 24) |
                  ((uint32_t)decrypted[1] << 16) |
                  ((uint32_t)decrypted[2] << 8) |
                  decrypted[3];
    out->counter = ((uint16_t)decrypted[4] << 8) | decrypted[5];
    out->button = decrypted[6];
    out->mode = 0x23;

    /* Validate checksum: sum of decrypted bytes mod 256 */
    uint8_t cksum = 0;
    for(int i = 0; i < 15; i++) cksum += decrypted[i];
    if(decrypted[15] != cksum) return false;

    /* Map button */
    switch(out->button) {
    case 0x01: out->function = "Lock"; break;
    case 0x02: out->function = "Unlock"; break;
    case 0x04: out->function = "Trunk"; break;
    case 0x08: out->function = "Panic"; break;
    default:   out->function = "Unknown"; break;
    }

    return true;
}

/* ── PSA Build Frame (Mode 0x23) ────────────────────────────────────────── */
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

    /* Calculate checksum */
    uint8_t cksum = 0;
    for(int i = 0; i < 15; i++) cksum += decrypted[i];
    decrypted[15] = cksum;

    /* XOR encrypt */
    for(int i = 0; i < 16; i++) {
        out[i] = decrypted[i] ^ (i & 0xFF);
    }

    *out_len = 16;
    return true;
}
