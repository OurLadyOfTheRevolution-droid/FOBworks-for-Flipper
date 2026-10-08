#include "flipper_gm.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* GM frame parser and builder. The 112-bit PPM frame contains a 0xFF wake byte, an unknown byte, button/checksum byte, 32-bit ID, 24-bit sequence, 24 encrypted bits, and a final checksum. Both checks use additive arithmetic: mod-16 for the button nibble and mod-256 for the frame, not XOR or CRC. I support replay, not forging; the cipher is undisclosed. Source: lib/subghz/protocols/gm.c. */
/* ─────────────────────────────────────────────────────────────────────────── */

bool gm_parse(const uint8_t* raw, int raw_bits, GmFrame* out) {
    if(!raw || !out || raw_bits < 112) return false;

    /* I copy the 14-byte frame for validation and field extraction. */
    uint8_t frame[14];
    for(int i = 0; i < 14; i++) {
        frame[i] = raw[i];
    }

    /* The frame must begin with the 0xFF wake byte. */
    if(frame[0] != 0xFF) return false;

    /* b2's two nibbles must add to a nonzero multiple of 16. In other words, its high nibble is the additive mod-16 checksum for the low-nibble button, and the check also rejects a zero button. */
    uint8_t btn_ck = (uint8_t)((frame[2] >> 4) + (frame[2] & 0x0F));
    if(btn_ck == 0 || (btn_ck & 0x0F) != 0) return false;

    /* The additive checksum covers b1 through b13, excluding the wake byte. Their sum must be a nonzero multiple of 256. */
    uint32_t full_ck = 0;
    for(int i = 1; i < 14; i++) full_ck += frame[i];
    if(full_ck == 0 || (full_ck & 0xFF) != 0) return false;

    /* I copy the validated bytes into the frame structure. */
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

    /* The low nibble of b2 is the button; its high nibble is the check value. */
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
    /* I store the button in b2's low nibble and its additive mod-16 check value in the high nibble. */
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

    /* I choose b13 so the additive mod-256 sum from b1 through b13 is zero. */
    uint32_t sum = 0;
    for(int i = 1; i < 13; i++) sum += out[i];
    out[13] = (uint8_t)((256U - (sum & 0xFFU)) & 0xFFU);

    *out_bits = 112;
    return true;
}
