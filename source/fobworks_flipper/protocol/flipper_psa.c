#include "flipper_psa.h"
#include "flipper_scratch.h"
#include <string.h>
#include <stdio.h>

/* This build supports PSA Mode 0x23, which uses XOR and a checksum. Mode 0x36
   is not implemented here. Captures use Manchester timing near 250/500 µs.
   The pulse decoder lives here so fw_force.fal can own it without dragging
   the rest of oem_wire into the plugin. */

#define PSA_MAX_BYTES 16

/* I try the estimated chip period plus offsets of ±15%. */
static int psa_te_candidates(const FlipperPulseBuf* buf, uint32_t* out, int max) {
    int n = 0;
    uint32_t te = buf->te_us;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te * 115 / 100;
    if(te >= 60 && te <= 4000 && n < max) out[n++] = te * 85 / 100;
    return n;
}

/* I expand each Manchester run into rounded half-bit levels, then decode pairs
   starting at phase. I store the result MSB first and return the recovered count. */
static int psa_manch_extract(const FlipperPulseBuf* buf, uint32_t te, int start_idx,
                             int phase, int max_bits, uint8_t* out) {
    if(te == 0) return 0;
    uint8_t* half = flipper_scratch_a(0, FLIPPER_PULSE_MAX * 4);
    if(!half) return 0;
    int hn = 0;
    uint8_t level = (start_idx & 1) ? 0 : 1;
    for(int i = start_idx; i < buf->len && hn < FLIPPER_PULSE_MAX * 4; i++) {
        uint32_t d = buf->durations[i];
        int cnt = (int)((d + te / 2) / te);
        if(cnt < 1) cnt = 1;
        if(cnt > 4) break;
        for(int k = 0; k < cnt && hn < FLIPPER_PULSE_MAX * 4; k++) half[hn++] = level;
        level ^= 1;
    }
    int nbytes = (max_bits + 7) / 8;
    if(nbytes > PSA_MAX_BYTES) return 0;
    memset(out, 0, nbytes);
    int nb = 0;
    for(int i = phase; i + 1 < hn && nb < max_bits; i += 2) {
        uint8_t a = half[i], b = half[i + 1];
        int bit;
        if(a == 1 && b == 0) bit = 1;
        else if(a == 0 && b == 1) bit = 0;
        else break;
        if(bit) out[nb >> 3] |= (uint8_t)(0x80 >> (nb & 7));
        nb++;
    }
    return nb;
}

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

/* PSA (Peugeot/Citroen) — Manchester, XOR mode 0x23 + checksum. Force-only:
   Mode 0x23 XOR + sum matched unrelated Manchester captures in testing. */
bool flipper_decode_psa(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->len < 128) return false;
    uint32_t tes[3];
    int ntes = psa_te_candidates(buf, tes, 3);
    uint8_t raw[PSA_MAX_BYTES];
    for(int t = 0; t < ntes; t++) {
        uint32_t te = tes[t];
        if(te < 120 || te > 900) continue;
        for(int phase = 0; phase < 2; phase++) {
            if(psa_manch_extract(buf, te, 0, phase, 128, raw) < 128) continue;
            PsaFrame f;
            /* Mode 0x23 only: XOR + checksum are fast enough for this path.
               Mode 0x36 needs a 2^24 TEA search — too expensive per capture. */
            if(!psa_decrypt_mode23(raw, 16, &f)) continue;
            if(f.serial == 0 && f.counter == 0) continue;
            r->addr = f.serial;
            r->cnt = f.counter;
            r->hop = f.counter;
            r->btn = f.button;
            r->rolling = true;
            r->te_us = te;
            r->bits = 128;
            r->freq_mhz = buf->freq_mhz;
            r->predict_window = 256;
            r->predict_lo = (f.counter + 1) & 0xFFFF;
            r->predict_hi = (f.counter + 8) & 0xFFFF;
            strncpy(r->proto, "PSA", sizeof(r->proto) - 1);
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "%s", f.function ? f.function : "PSA");
            return true;
        }
    }
    return false;
}
