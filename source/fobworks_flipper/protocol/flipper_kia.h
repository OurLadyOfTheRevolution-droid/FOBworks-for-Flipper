#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* KIA/Hyundai Protocol Suite (V0-V7) — FOBworks implementation. */
/*   V0: PWM 250/500μs, CRC8 (also Suzuki/Honda/Mitsubishi) */
/*   V1: Manchester 800/1600μs, CRC4 */
/*   V2: Manchester 500/1000μs, Custom CRC */
/*   V3/V4: Manchester 400/800μs, KeeLoq + CRC brute-force */
/*   V5: Manchester 400/800μs, Custom Mixer + Kia V5 key */
/*   V6: Manchester 200/400μs, AES-128 (144-bit frames) */
/*   V7: PWM 250/500μs, CRC8 */
/* ─────────────────────────────────────────────────────────────────────────── */

/* KIA V0 frame */
typedef struct {
    uint32_t serial;
    uint16_t counter;
    uint8_t  button;
    uint8_t  crc;
} KiaV0Frame;

/* KIA V1 frame */
typedef struct {
    uint32_t serial;
    uint16_t counter;
    uint8_t  button;
    uint8_t  crc;
} KiaV1Frame;

/* KIA V2 frame */
typedef struct {
    uint32_t serial;
    uint16_t counter;
    uint8_t  button;
    uint8_t  crc;
} KiaV2Frame;

/* KIA V3/V4 frame (KeeLoq-based) */
typedef struct {
    uint32_t enc;     /* 32-bit encrypted hop */
    uint32_t serial;  /* 28-bit serial */
    uint8_t  button;  /* 4-bit button */
    uint8_t  disc;    /* 4-bit discriminant */
    uint8_t  crc;     /* 2-bit CRC */
} KiaV3V4Frame;

/* KIA V5 frame (Mixer-based) */
typedef struct {
    uint32_t serial;
    uint16_t counter;
    uint8_t  button;
    uint8_t  crc;
} KiaV5Frame;

/* KIA V6 frame (AES-128) */
typedef struct {
    uint8_t  encrypted[16]; /* 128-bit AES ciphertext */
    uint16_t crc;           /* 16-bit CRC */
    int      key_index;     /* which key decrypted (-1 = unknown) */
} KiaV6Frame;

/* KIA V7 frame */
typedef struct {
    uint32_t serial;
    uint16_t counter;
    uint8_t  button;
    uint8_t  crc;
} KiaV7Frame;

/* Parse functions */
bool kia_v0_parse(const uint8_t* raw, int raw_bits, KiaV0Frame* out);
bool kia_v1_parse(const uint8_t* raw, int raw_bits, KiaV1Frame* out);
bool kia_v2_parse(const uint8_t* raw, int raw_bits, KiaV2Frame* out);
bool kia_v3_v4_parse(const uint8_t* raw, int raw_bits, KiaV3V4Frame* out);
bool kia_v5_parse(const uint8_t* raw, int raw_bits, const uint8_t* key, KiaV5Frame* out);
bool kia_v6_parse(const uint8_t* raw, int raw_bits, const uint8_t* key, KiaV6Frame* out);
bool kia_v7_parse(const uint8_t* raw, int raw_bits, KiaV7Frame* out);

/* Version name lookup */
const char* kia_version_name(int version);
