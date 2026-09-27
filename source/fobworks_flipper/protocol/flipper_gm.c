#include "flipper_gm.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* GM Protocol — FOBworks implementation.                                     */
/*   Source: lib/subghz/protocols/gm.c                                        */
/*   112-bit (14-byte) frames, PPM encoding.                                  */
/*   Frame: 0xFF wake | unknown | btn+cksum | 32-bit ID | 24-bit seq |        */
/*          24-bit encrypted | checksum                                       */
/*   Additive mod-256 checksums (NOT XOR/CRC).                                */
/*   REPLAY only — cannot forge (undisclosed cipher).                         */
/* ─────────────────────────────────────────────────────────────────────────── */

bool gm_parse(const uint8_t* raw, int raw_bits, GmFrame* out) {
    if(!raw || !out || raw_bits < 112) return false;

    /* Extract 14-byte frame */
    uint8_t frame[14];
    for(int i = 0; i < 14; i++) {
        frame[i] = raw[i];
    }

    /* Validate wake byte */
    if(frame[0] != 0xFF) return false;

    /* Button nibble checksum (additive mod-16): nibble_sum(b2) must be non-zero
       with its low nibble == 0, i.e. high nibble == (-low nibble) mod 16.  This
       also forces a non-zero button. */
    uint8_t btn_ck = (uint8_t)((frame[2] >> 4) + (frame[2] & 0x0F));
    if(btn_ck == 0 || (btn_ck & 0x0F) != 0) return false;

    /* Full payload checksum (additive mod-256): byte_sum(b1..b13) must be
       non-zero with its low byte == 0.  b0 (0xFF wake) is excluded. */
    uint32_t full_ck = 0;
    for(int i = 1; i < 14; i++) full_ck += frame[i];
    if(full_ck == 0 || (full_ck & 0xFF) != 0) return false;

    /* Extract fields */
    out->wake     = frame[0];
    out->unknown  = frame[1];
    out->btn_sum  = frame[2];
    out->id       = ((uint32_t)frame[3] << 24) | ((uint32_t)frame[4] << 16) |
                    ((uint32_t)frame[5] << 8) | frame[6];
    out->seq      = ((uint32_t)frame[7] << 16) | ((uint32_t)frame[8] << 8) | frame[9];
    out->encrypted[0] = frame[10];
    out->encrypted[1] = frame[11];
    out->encrypted[2] = frame[12];
    out->checksum = frame[13];

    /* Button is the LOW nibble of b2 (the high nibble is its checksum). */
    out->button = out->btn_sum & 0x0F;
    switch(out->button) {
    case 0x1: out->function = "Lock"; break;
    case 0x2: out->function = "Unlock"; break;
    case 0x4: out->function = "Trunk"; break;
    case 0x8: out->function = "Panic"; break;
    default:  out->function = "Unknown"; break;
    }

    return true;
}

bool gm_build(const GmFrame* f, uint8_t* out, int* out_bits) {
    if(!f || !out || !out_bits) return false;

    out[0] = 0xFF;  /* wake byte */
    out[1] = f->unknown;
    /* b2 = [high nibble: -button mod 16 checksum][low nibble: button code]. */
    uint8_t btn = f->button & 0x0F;
    uint8_t btn_ck = (uint8_t)((16U - btn) & 0x0FU);
    out[2] = (uint8_t)((btn_ck << 4) | btn);
    out[3] = (f->id >> 24) & 0xFF;
    out[4] = (f->id >> 16) & 0xFF;
    out[5] = (f->id >> 8) & 0xFF;
    out[6] = f->id & 0xFF;
    out[7] = (f->seq >> 16) & 0xFF;
    out[8] = (f->seq >> 8) & 0xFF;
    out[9] = f->seq & 0xFF;
    out[10] = f->encrypted[0];
    out[11] = f->encrypted[1];
    out[12] = f->encrypted[2];

    /* b13 chosen so byte_sum(b1..b13) & 0xFF == 0 (additive mod-256). */
    uint32_t sum = 0;
    for(int i = 1; i < 13; i++) sum += out[i];
    out[13] = (uint8_t)((256U - (sum & 0xFFU)) & 0xFFU);

    *out_bits = 112;
    return true;
}
