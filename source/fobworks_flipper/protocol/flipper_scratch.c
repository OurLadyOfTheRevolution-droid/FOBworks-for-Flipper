#include "flipper_scratch.h"

static uint8_t s_a[FLIPPER_SCRATCH_A];
static uint8_t s_b[FLIPPER_SCRATCH_B];

void* flipper_scratch_a(size_t off, size_t n) {
    if(n == 0 || off > FLIPPER_SCRATCH_A || n > FLIPPER_SCRATCH_A - off) return NULL;
    return s_a + off;
}

void* flipper_scratch_b(size_t n) {
    if(n == 0 || n > FLIPPER_SCRATCH_B) return NULL;
    return s_b;
}