#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pulse-timing profiles only. These are NOT RF carrier-offset measurements
   and cannot establish oscillator, transmitter, or clone identity. */
#define FOBFREQ_SAMPLES_MAX 16
#define FOBFREQ_SAMPLES_MIN 4
#define FOBFREQ_TIMING_TOL_US 32.0f /* resolution of the current TE histogram */

typedef struct {
    float total_us;
    int count;
    float min_us;
    float max_us;
    float mean_us;
} FobTimingProfile;

void fobfreq_reset(FobTimingProfile* p);
void fobfreq_add(FobTimingProfile* p, float te_us);
bool fobfreq_finalize(FobTimingProfile* p);
float fobfreq_mean_delta(const FobTimingProfile* a, const FobTimingProfile* b);
bool fobfreq_similar_timing(const FobTimingProfile* a, const FobTimingProfile* b,
                           float tolerance_us);
