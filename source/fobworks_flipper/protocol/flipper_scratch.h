#pragma once

#include <stddef.h>
#include <stdint.h>

/* Shared bounded decode workspace. Region B is kept separate so TE
   estimation can run while a decoder retains scratch pointers. */
#define FLIPPER_SCRATCH_A 2560
#define FLIPPER_SCRATCH_B 1024

void* flipper_scratch_a(size_t off, size_t n);
void* flipper_scratch_b(size_t n);