#include "flipper_ford.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Ford Protocol Decoders — FOBworks implementation.                          */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Ford V0: 64-bit Manchester, 250/500μs, 6-burst ─────────────────────── */
/* CRC matrix (64-byte GF(2) matrix) */
static const uint8_t ford_v0_crc_matrix[64] = {
    0xDA, 0xB5, 0x55, 0x6A, 0xAA, 0xAA, 0x55, 0x6A,
    0xB5, 0xDA, 0x6A, 0x55, 0xAA, 0x55, 0x6A, 0xAA,
    0x55, 0x6A, 0xAA, 0xAA, 0xDA, 0xB5, 0x55, 0x6A,
    0xAA, 0x55, 0x6A, 0xAA, 0xB5, 0xDA, 0x6A, 0x55,
    0xAA, 0xAA, 0x55, 0x6A, 0xB5, 0xDA, 0x6A, 0x55,
    0x6A, 0xAA, 0xAA, 0x55, 0xDA, 0xB5, 0x55, 0x6A,
    0x55, 0x6A, 0xAA, 0xAA, 0xB5, 0xDA, 0x6A, 0x55,
    0xAA, 0x55, 0x6A, 0xAA, 0xDA, 0xB5, 0x55, 0x6A,
};

/* Ford V0 checksum: sum of all serial bytes + counter bytes + (button << 3) */
static uint8_t ford_v0_checksum(const FordV0Frame* f) {
    uint8_t sum = 0;
    /* Serial bytes (3 bytes from 24-bit serial) */
    sum += (f->serial >> 16) & 0xFF;
    sum += (f->serial >> 8) & 0xFF;
    sum += f->serial & 0xFF;
    /* Counter bytes (2 bytes from 16-bit counter) */
    sum += (f->counter >> 8) & 0xFF;
    sum += f->counter & 0xFF;
    /* Button contribution */
    sum += (f->button << 3) & 0xFF;
    return sum & 0xFF;
}

/* Ford V0 CRC-8 using the GF(2) matrix */
static uint8_t ford_v0_crc8(const uint8_t* data, int bits) {
    uint8_t crc = 0;
    for(int i = 0; i < bits && i < 64; i++) {
        uint8_t bit = (data[i / 8] >> (7 - (i % 8))) & 1;
        if(bit) {
            crc ^= ford_v0_crc_matrix[i];
        }
    }
    return crc;
}

bool ford_v0_parse(const uint8_t* raw, int raw_bits, FordV0Frame* out) {
    if(!raw || !out || raw_bits < 64) return false;

    /* Extract fields from 64-bit frame */
    /* Layout: serial(24) | counter(16) | button(4) | checksum(8) | crc(8) | pad(4) */
    uint64_t frame = 0;
    for(int i = 0; i < 64 && i < raw_bits; i++) {
        frame = (frame << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    out->serial    = (frame >> 40) & 0xFFFFFF;
    out->counter   = (frame >> 24) & 0xFFFF;
    out->button    = (frame >> 20) & 0x0F;
    out->checksum  = (frame >> 12) & 0xFF;
    out->crc       = (frame >> 4) & 0xFF;

    /* Validate checksum */
    uint8_t expected_cksum = ford_v0_checksum(out);
    if(out->checksum != expected_cksum) return false;

    /* Validate CRC.  The CRC covers the 52 bits *preceding* the CRC field only:
       bits 52-55 hold the CRC nibble itself, so including them (as an earlier
       revision did with 56) made the value self-referential and unverifiable. */
    uint8_t expected_crc = ford_v0_crc8(raw, 52);
    if(out->crc != expected_crc) return false;

    /* Reject the all-zero frame: CRC-8 and checksum of zeros are both zero, so
       an empty demodulation otherwise passes both gates and steals captures. */
    if(out->serial == 0) return false;

    /* Map button to function */
    switch(out->button) {
    case 0x1: out->function = "Lock"; break;
    case 0x2: out->function = "Unlock"; break;
    case 0x4: out->function = "Trunk"; break;
    case 0x8: out->function = "Panic"; break;
    default:  out->function = "Unknown"; break;
    }

    return true;
}

bool ford_v0_build(FordV0Frame* f, uint8_t* out, int* out_bits) {
    if(!f || !out || !out_bits) return false;

    /* Calculate checksum and CRC */
    f->checksum = ford_v0_checksum(f);

    /* Pack into 64-bit frame */
    uint64_t frame = 0;
    frame |= ((uint64_t)f->serial & 0xFFFFFF) << 40;
    frame |= ((uint64_t)f->counter & 0xFFFF) << 24;
    frame |= ((uint64_t)f->button & 0x0F) << 20;
    frame |= ((uint64_t)f->checksum & 0xFF) << 12;
    /* CRC will be calculated over the packed data */

    /* Pack bytes */
    for(int i = 0; i < 8; i++) {
        out[i] = (frame >> (56 - i * 8)) & 0xFF;
    }

    /* Calculate and insert CRC over the 52 bits preceding the CRC field so the
       value is stable across build/parse (bits 52-55 are the CRC nibble). */
    f->crc = ford_v0_crc8(out, 52);
    out[6] = (out[6] & 0xF0) | (f->crc >> 4);
    out[7] = (out[7] & 0x0F) | (f->crc << 4);

    *out_bits = 64;
    return true;
}

/* ── Ford V1: 136-bit Manchester, 65/130μs, 6-burst ─────────────────────── */
/* CRC-16 (polynomial 0x1021, init 0x0000) */
static uint16_t ford_v1_crc16(const uint8_t* data, int len) {
    uint16_t crc = 0x0000;
    for(int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for(int j = 0; j < 8; j++) {
            if(crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

bool ford_v1_parse(const uint8_t* raw, int raw_bits, FordV1Frame* out) {
    if(!raw || !out || raw_bits < 136) return false;

    /* Extract fields from 136-bit frame */
    /* Complex air-to-plain decoding with XOR flag byte selection */
    /* Simplified parser for common Ford V1 structure */
    uint32_t serial_hi = 0, serial_lo = 0;
    for(int i = 0; i < 28; i++) {
        serial_hi = (serial_hi << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }
    for(int i = 28; i < 56; i++) {
        serial_lo = (serial_lo << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    out->serial = ((uint64_t)serial_hi << 28) | serial_lo;
    out->counter = 0;
    out->button = 0;

    /* Extract CRC-16 from last 16 bits */
    uint16_t received_crc = 0;
    for(int i = 120; i < 136; i++) {
        received_crc = (received_crc << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    /* Validate CRC */
    uint16_t expected_crc = ford_v1_crc16(raw, 15);  /* CRC over first 120 bits */
    if(received_crc != expected_crc) return false;

    out->function = "Ford V1";
    return true;
}

/* ── Ford V2: 72-bit PWM, 200/400μs ─────────────────────────────────────── */
bool ford_v2_parse(const uint8_t* raw, int raw_bits, FordV2Frame* out) {
    if(!raw || !out || raw_bits < 72) return false;

    uint64_t frame = 0;
    for(int i = 0; i < 64 && i < raw_bits; i++) {
        frame = (frame << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    out->serial   = (frame >> 32) & 0xFFFFFFFF;
    out->counter  = (frame >> 16) & 0xFFFF;
    out->button   = (frame >> 8) & 0xFF;
    out->checksum = frame & 0xFF;

    /* Validate: simple additive checksum */
    uint8_t sum = 0;
    sum += (out->serial >> 24) & 0xFF;
    sum += (out->serial >> 16) & 0xFF;
    sum += (out->serial >> 8) & 0xFF;
    sum += out->serial & 0xFF;
    sum += (out->counter >> 8) & 0xFF;
    sum += out->counter & 0xFF;
    sum += out->button;

    if((sum & 0xFF) != out->checksum) return false;
    /* Reject empty frame: additive checksum of zeros is zero. */
    if(out->serial == 0 || out->serial == 0xFFFFFFFFu) return false;
    /* Button is a sparse function code on known Ford remotes. */
    if(out->button == 0 || out->button == 0xFF) return false;
    return true;
}

/* ── Ford V3: Manchester, no crypto (decoder only) ──────────────────────── */
bool ford_v3_parse(const uint8_t* raw, int raw_bits, FordV3Frame* out) {
    if(!raw || !out || raw_bits < 64) return false;

    uint64_t frame = 0;
    for(int i = 0; i < 64 && i < raw_bits; i++) {
        frame = (frame << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    out->serial  = (frame >> 32) & 0xFFFFFFFF;
    out->counter = (frame >> 16) & 0xFFFF;
    out->button  = frame & 0xFFFF;
    out->function = "Ford V3 (fixed)";

    return true;
}

/* ── Ford Button Mapping ────────────────────────────────────────────────── */
const char* ford_button_name(int version, uint8_t btn) {
    if(version == 0) {
        switch(btn) {
        case 0x1: return "Lock";
        case 0x2: return "Unlock";
        case 0x4: return "Trunk";
        case 0x8: return "Panic";
        default:  return "Unknown";
        }
    }
    return "Unknown";
}
