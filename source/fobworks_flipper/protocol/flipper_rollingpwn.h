#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Sequence-candidate analysis only. Honda frame interpretation remains
   experimental and this module does not establish receiver behavior. */
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

/* Returns true only when all input frames, in capture chronology, are from
   one serial/command/profile frequency and each masked counter step is within
   [1, max_delta]. This is not evidence of a vulnerability or resync. */
bool rollingpwn_analyze(
    const RollingPwnFrame* frames,
    int n,
    uint32_t counter_mask,
    int min_seq,
    uint32_t max_delta,
    float expected_frequency_mhz,
    float frequency_tolerance_mhz,
    RollingPwnPlan* out);