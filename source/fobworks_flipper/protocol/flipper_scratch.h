#pragma once

#include <stddef.h>
#include <stdint.h>

/* Bounded workspace I share between decoders. I keep region B separate so TE
   estimation can run while a decoder still holds pointers into region A. */
#define FLIPPER_SCRATCH_A 2560
#define FLIPPER_SCRATCH_B 1024

void* flipper_scratch_a(size_t off, size_t n);
void* flipper_scratch_b(size_t n);