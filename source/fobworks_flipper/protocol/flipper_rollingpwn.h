#pragma once

#include <stdbool.h>
#include <stdint.h>

/* This module only checks whether captures form a possible counter sequence.
   Honda's frame interpretation is still experimental; the result does not show
   that the protocol is vulnerable or that a receiver will resynchronize. */
#define ROLLINGPWN_MAX_CAPS 3

typedef struct {
    uint32_t counter;
    uint32_t serial;
    uint8_t command;
    float frequency_mhz;
} RollingPwnFrame;

typedef struct {
    bool sequence_candidate;
    int sequence_len;
    int order[ROLLINGPWN_MAX_CAPS];
    uint32_t base_counter;
    uint32_t top_counter;
    uint32_t span;
    char note[64];
} RollingPwnPlan;

/* I return true only if every frame, in capture order, has the same serial,
   command, and profile frequency, and each masked counter step is in
   [1, max_delta]. This identifies a candidate sequence, not a vulnerability
   or a successful resynchronization. */
bool rollingpwn_analyze(
    const RollingPwnFrame* frames,
    int n,
    uint32_t counter_mask,
    int min_seq,
    uint32_t max_delta,
    float expected_frequency_mhz,
    float frequency_tolerance_mhz,
    RollingPwnPlan* out);