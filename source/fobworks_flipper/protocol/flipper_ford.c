#include "flipper_ford.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Ford frame parsers and builder.                                            */
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

/* V0 checksum: add the serial bytes, counter bytes, and button shifted by 3. */
static uint8_t ford_v0_checksum(const FordV0Frame* f) {
    uint8_t sum = 0;
    /* I add the three bytes of the 24-bit serial. */
    sum += (f->serial >> 16) & 0xFF;
    sum += (f->serial >> 8) & 0xFF;
    sum += f->serial & 0xFF;
    /* I add the two bytes of the 16-bit counter. */
    sum += (f->counter >> 8) & 0xFF;
    sum += f->counter & 0xFF;
    /* I add the shifted button value. */
    sum += (f->button << 3) & 0xFF;
    return sum & 0xFF;
}

/* I calculate the V0 CRC-8 from the GF(2) matrix. */
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

    /* I read the 64-bit frame as serial(24) | counter(16) | button(4) | checksum(8) | CRC(8) | pad(4). */
    uint64_t frame = 0;
    for(int i = 0; i < 64 && i < raw_bits; i++) {
        frame = (frame << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    out->serial    = (frame >> 40) & 0xFFFFFF;
    out->counter   = (frame >> 24) & 0xFFFF;
    out->button    = (frame >> 20) & 0x0F;
    out->checksum  = (frame >> 12) & 0xFF;
    out->crc       = (frame >> 4) & 0xFF;

    /* I check the additive checksum before the CRC. */
    uint8_t expected_cksum = ford_v0_checksum(out);
    if(out->checksum != expected_cksum) return false;

    /* The CRC covers the 52 bits before its 8-bit field. Including any CRC bits in the calculation would make the value self-referential. */
    uint8_t expected_crc = ford_v0_crc8(raw, 52);
    if(out->crc != expected_crc) return false;

    /* Both checksums evaluate to zero for an all-zero input; I reject it so an empty demodulation is not reported as a valid capture. */
    if(out->serial == 0) return false;

    /* I convert known button codes to their function names. */
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

    /* I fill in the checksum before packing the frame. */
    f->checksum = ford_v0_checksum(f);

    /* I assemble the non-CRC fields into the 64-bit frame. */
    uint64_t frame = 0;
    frame |= ((uint64_t)f->serial & 0xFFFFFF) << 40;
    frame |= ((uint64_t)f->counter & 0xFFFF) << 24;
    frame |= ((uint64_t)f->button & 0x0F) << 20;
    frame |= ((uint64_t)f->checksum & 0xFF) << 12;
    /* The CRC is calculated after the packed bytes are available. */

    /* I write the frame bytes in most-significant-byte-first order. */
    for(int i = 0; i < 8; i++) {
        out[i] = (frame >> (56 - i * 8)) & 0xFF;
    }

    /* I calculate CRC over the 52 bits before its field, matching the parser. */
    f->crc = ford_v0_crc8(out, 52);
    out[6] = (out[6] & 0xF0) | (f->crc >> 4);
    out[7] = (out[7] & 0x0F) | (f->crc << 4);

    *out_bits = 64;
    return true;
}

/* ── Ford V1: 136-bit Manchester, 65/130μs, 6-burst ─────────────────────── */
/* V1 CRC-16 uses polynomial 0x1021 and initial value 0x0000. */
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

    /* This partial parser reads the two 28-bit serial portions directly. I do not decode the air-to-plain XOR flags, counter, or button. */
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

    /* The final 16 bits carry the CRC. */
    uint16_t received_crc = 0;
    for(int i = 120; i < 136; i++) {
        received_crc = (received_crc << 1) | ((raw[i / 8] >> (7 - (i % 8))) & 1);
    }

    /* I compare against the CRC over the first 120 bits. */
    uint16_t expected_crc = ford_v1_crc16(raw, 15);
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

    /* I validate the additive checksum across serial, counter, and button. */
    uint8_t sum = 0;
    sum += (out->serial >> 24) & 0xFF;
    sum += (out->serial >> 16) & 0xFF;
    sum += (out->serial >> 8) & 0xFF;
    sum += out->serial & 0xFF;
    sum += (out->counter >> 8) & 0xFF;
    sum += out->counter & 0xFF;
    sum += out->button;

    if((sum & 0xFF) != out->checksum) return false;
    /* An empty frame also passes the zero-valued additive checksum. */
    if(out->serial == 0 || out->serial == 0xFFFFFFFFu) return false;
    /* Known Ford remotes use sparse button function codes. */
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
