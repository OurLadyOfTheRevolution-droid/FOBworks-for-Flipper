#include "flipper_hitag2.h"
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Hitag2 cipher helpers.                                                     */
/*   Fiat V1 BCM uses a custom filter and LFSR feedback.                      */
/*   Renault V1 uses standard Hitag2 with a limited key-search helper.        */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Hitag2 Filter Functions (Fiat V1 BCM) ──────────────────────────────── */
/* Source: FOBworks protocol database                                         */
static uint32_t hitag2_fiat_filter(uint64_t state) {
    /* I apply the fa=0x2C79 and fb=0x6671 filters, then mask with fc=0x7907287B. */
    uint32_t fa = 0, fb = 0;

    /* fa: bits 0,1,4,5,6,17,21,24,25,31,39,40,41,44,45,47 */
    fa = ((state >>  0) & 1) << 0  |
         ((state >>  1) & 1) << 1  |
         ((state >>  4) & 1) << 2  |
         ((state >>  5) & 1) << 3  |
         ((state >>  6) & 1) << 4  |
         ((state >> 17) & 1) << 5  |
         ((state >> 21) & 1) << 6  |
         ((state >> 24) & 1) << 7  |
         ((state >> 25) & 1) << 8  |
         ((state >> 31) & 1) << 9  |
         ((state >> 39) & 1) << 10 |
         ((state >> 40) & 1) << 11 |
         ((state >> 41) & 1) << 12 |
         ((state >> 44) & 1) << 13 |
         ((state >> 45) & 1) << 14 |
         ((state >> 47) & 1) << 15;

    fa = (fa & 0x2C79) ^ ((fa >> 1) & 0x2C79);

    /* fb uses a separate set of state-bit positions. */
    fb = ((state >>  2) & 1) << 0  |
         ((state >>  3) & 1) << 1  |
         ((state >>  7) & 1) << 2  |
         ((state >>  8) & 1) << 3  |
         ((state >>  9) & 1) << 4  |
         ((state >> 10) & 1) << 5  |
         ((state >> 11) & 1) << 6  |
         ((state >> 12) & 1) << 7  |
         ((state >> 13) & 1) << 8  |
         ((state >> 14) & 1) << 9  |
         ((state >> 15) & 1) << 10 |
         ((state >> 16) & 1) << 11 |
         ((state >> 18) & 1) << 12 |
         ((state >> 19) & 1) << 13 |
         ((state >> 20) & 1) << 14 |
         ((state >> 22) & 1) << 15;

    fb = (fb & 0x6671) ^ ((fb >> 1) & 0x6671);

    return (fa ^ fb) & 0x7907287B;
}

/* ── Hitag2 LFSR Feedback (Fiat V1 BCM) ─────────────────────────────────── */
__attribute__((unused))
static uint64_t hitag2_fiat_feedback(uint64_t state) {
    /* LFSR taps: {0, 1, 4, 5, 6, 17, 21, 24, 25, 31, 39, 40, 41, 44, 45, 47} */
    uint64_t fb = 0;
    static const int taps[] = {0, 1, 4, 5, 6, 17, 21, 24, 25, 31, 39, 40, 41, 44, 45, 47};
    for(int i = 0; i < 16; i++) {
        fb ^= (state >> taps[i]) & 1;
    }
    return (state >> 1) | (fb << 47);
}

/* ── Hitag2 Step (single clock cycle) ───────────────────────────────────── */
static uint64_t hitag2_step(uint64_t state, uint64_t key) {
    uint64_t fb = 0;

    /* I calculate the standard Hitag2 LFSR feedback bit. */
    fb = ((state >>  0) & 1) ^
         ((state >>  1) & 1) ^
         ((state >>  2) & 1) ^
         ((state >>  3) & 1) ^
         ((state >> 12) & 1) ^
         ((state >> 13) & 1) ^
         ((state >> 17) & 1) ^
         ((state >> 19) & 1) ^
         ((state >> 20) & 1) ^
         ((state >> 25) & 1) ^
         ((state >> 26) & 1) ^
         ((state >> 30) & 1) ^
         ((state >> 33) & 1) ^
         ((state >> 36) & 1) ^
         ((state >> 37) & 1) ^
         ((state >> 42) & 1) ^
         ((state >> 43) & 1) ^
         ((state >> 47) & 1);

    /* I mix in the least-significant key bit. */
    fb ^= (key >> 0) & 1;

    return (state >> 1) | (fb << 47);
}

/* ── Hitag2 Generate Keystream ──────────────────────────────────────────── */
void hitag2_keystream(uint64_t key, uint32_t uid, uint32_t* out, int words) {
    /* I seed the state with the key and the low 16 bits of the UID. */
    uint64_t state = (key << 16) | (uid & 0xFFFF);

    /* I advance 32 cycles before collecting output bits. */
    for(int i = 0; i < 32; i++) {
        state = hitag2_step(state, key);
    }

    /* I collect the requested number of 32-bit keystream words. */
    for(int i = 0; i < words; i++) {
        uint32_t word = 0;
        for(int j = 0; j < 32; j++) {
            state = hitag2_step(state, key);
            uint32_t bit = hitag2_fiat_filter(state) & 1;
            word = (word << 1) | bit;
        }
        out[i] = word;
    }
}

/* ── Hitag2 Authenticate (Fiat V1 BCM) ──────────────────────────────────── */
uint32_t hitag2_authenticate(uint64_t key, uint32_t uid, uint32_t challenge) {
    uint64_t state = (key << 16) | (uid & 0xFFFF);

    /* I mix the challenge into the state, one bit per cycle. */
    for(int i = 0; i < 32; i++) {
        uint64_t fb = 0;
        fb = ((state >>  0) & 1) ^
             ((state >>  1) & 1) ^
             ((state >>  2) & 1) ^
             ((state >>  3) & 1) ^
             ((state >> 12) & 1) ^
             ((state >> 13) & 1) ^
             ((state >> 17) & 1) ^
             ((state >> 19) & 1) ^
             ((state >> 20) & 1) ^
             ((state >> 25) & 1) ^
             ((state >> 26) & 1) ^
             ((state >> 30) & 1) ^
             ((state >> 33) & 1) ^
             ((state >> 36) & 1) ^
             ((state >> 37) & 1) ^
             ((state >> 42) & 1) ^
             ((state >> 43) & 1) ^
             ((state >> 47) & 1);
        fb ^= (key >> 0) & 1;
        fb ^= (challenge >> (31 - i)) & 1;
        state = (state >> 1) | (fb << 47);
    }

    /* I generate a 32-bit response from the resulting state. */
    uint32_t response = 0;
    for(int i = 0; i < 32; i++) {
        state = hitag2_step(state, key);
        uint32_t bit = hitag2_fiat_filter(state) & 1;
        response = (response << 1) | bit;
    }
    return response;
}

/* ── Hitag2 Brute-Force (Renault V1 style) ──────────────────────────────── */
/* Source: FOBworks protocol database                                         */
/* I check whether candidate keys produce the expected response. My current loop tries only keys 0 through 2^24-1; I do not search the full 48-bit space or split the search across a larger key range. */

bool hitag2_brute_force(uint32_t uid, uint32_t challenge, uint32_t expected,
                        uint64_t* found_key, int max_keys, uint64_t* found_keys,
                        int* found_count) {
    if(!found_key && !found_keys) return false;

    int count = 0;
    uint64_t key = 0;

    /* I limit this on-device pass to 2^24 candidates (16M trials). A complete 2^48 search requires substantially more compute. */
    uint32_t max_trials = 1 << 24;

    for(uint32_t i = 0; i < max_trials; i++) {
        key = (uint64_t)i;  /* Low 24 bits of 48-bit key */

        uint32_t response = hitag2_authenticate(key, uid, challenge);
        if(response == expected) {
            if(found_keys && count < max_keys) {
                found_keys[count] = key;
            }
            count++;
            if(count >= max_keys) break;
        }
    }

    if(found_count) *found_count = count;
    if(found_key && count > 0) *found_key = found_keys ? found_keys[0] : 0;

    return count > 0;
}

/* ── Hitag2 Fiat Invert Init — FOBworks implementation ───────────────────── */
/* I assemble a candidate state from the authenticator and low UID bits. This helper does not invert the full initialization or recover a complete key. */

uint64_t hitag2_fiat_invert_init(uint32_t uid, uint32_t authenticator) {
    /* I retain the low UID bits and place the authenticator in the upper half. */
    uint64_t state = 0;

    /* My current implementation assembles fields directly; it does not invert the cipher's initialization sequence. */
    state = ((uint64_t)authenticator << 16) | (uid & 0xFFFF);

    return state;
}

/* ── Hitag2 Known Keys (Fiat V1 BCM dictionary) ─────────────────────────── */
/* Source: FOBworks Hitag2 key database */
/* This table lists known Fiat V1 Hitag2 defaults and recognizable key patterns. */

static const Hitag2KnownKey hitag2_known_keys[] = {
    /* Listed factory keys. */
    { "B79280AECC37", 0xB79280AECC37 },
    { "D42428F7D966", 0xD42428F7D966 },
    { "4D343FD4E7B6", 0x4D343FD4E7B6 },
    /* Keys formed from ASCII patterns. */
    { "MIKRON",       0x4D494B524F4E },
    { "DELPHI",       0x44454C504849 },
    { "MARELLI",      0x4D4152454C4C },
    { "CONTIN",       0x434F4E54494E },
    { "SIEMEN",       0x5349454D454E },
    { "BOSCH!",       0x424F53434821 },
    { "VDOCAR",       0x56444F434152 },
    { "FIAT00",       0x464941543030 },
    { "ALFARO",       0x414C4641524F },
    { "LANCIA",       0x4C414E434941 },
    /* Default and weak keys. */
    { "ZERO",         0x000000000000 },
    { "ONES",         0xFFFFFFFFFFFF },
    { "DEADBEEFCAFE", 0xDEADBEEFCAFE },
    { "123456789ABC", 0x123456789ABC },
};

int hitag2_known_key_count(void) {
    return (int)(sizeof(hitag2_known_keys) / sizeof(hitag2_known_keys[0]));
}

const Hitag2KnownKey* hitag2_get_known_key(int index) {
    int n = hitag2_known_key_count();
    if(index < 0 || index >= n) return NULL;
    return &hitag2_known_keys[index];
}

/* ── Hitag2 Serial Permutation (Renault V1) ─────────────────────────────── */
/* Source: FOBworks protocol database */
uint32_t hitag2_serial_permute(uint32_t serial) {
    /* I reorder the 28 serial bits using the permutation table below. */
    uint32_t result = 0;
    static const int perm[28] = {
        27, 26, 25, 24, 23, 22, 21, 20,
        19, 18, 17, 16, 15, 14, 13, 12,
        11, 10,  9,  8,  7,  6,  5,  4,
         3,  2,  1,  0
    };
    for(int i = 0; i < 28; i++) {
        if(serial & (1u << perm[i]))
            result |= (1u << i);
    }
    return result;
}
