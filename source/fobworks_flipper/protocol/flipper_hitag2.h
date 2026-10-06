#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Hitag2 Cipher Core — FOBworks implementation.                              */
/*   Fiat V1 BCM uses Hitag2 with custom filter + LFSR feedback.              */
/*   Renault V1 uses standard Hitag2 with brute-force support.                */
/* ─────────────────────────────────────────────────────────────────────────── */

#define HITAG2_KNOWN_KEY_COUNT 16

/* Generate keystream words from key + UID. */
void hitag2_keystream(uint64_t key, uint32_t uid, uint32_t* out, int words);

/* Authenticate: generate 32-bit response to a challenge. */
uint32_t hitag2_authenticate(uint64_t key, uint32_t uid, uint32_t challenge);

/* Brute-force 48-bit key space (limited to 2^24 for on-device feasibility). */
bool hitag2_brute_force(uint32_t uid, uint32_t challenge, uint32_t expected,
                        uint64_t* found_key, int max_keys, uint64_t* found_keys,
                        int* found_count);

/* Invert init phase for Fiat V1 BCM (listed Hitag2 invert helper). */
uint64_t hitag2_fiat_invert_init(uint32_t uid, uint32_t authenticator);

/* Known Hitag2 keys dictionary (factory defaults + ASCII patterns + weak). */
typedef struct {
    const char* label;
    uint64_t    key;  /* 48-bit key in lower 48 bits */
} Hitag2KnownKey;

const Hitag2KnownKey* hitag2_get_known_key(int index);
int hitag2_known_key_count(void);

/* Serial permutation for Renault V1 authentication. */
uint32_t hitag2_serial_permute(uint32_t serial);
