#include "flipper_rollingpwn.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool rollingpwn_analyze(
    const RollingPwnFrame* frames,
    int n,
    uint32_t counter_mask,
    int min_seq,
    uint32_t max_delta,
    float expected_frequency_mhz,
    float frequency_tolerance_mhz,
    RollingPwnPlan* out) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));

    if(!frames || n < 1 || n > ROLLINGPWN_MAX_CAPS) {
        snprintf(out->note, sizeof(out->note), "Capture count invalid");
        return false;
    }
    if(min_seq < 1 || min_seq > ROLLINGPWN_MAX_CAPS ||
       max_delta == 0 || counter_mask == 0 ||
       !isfinite(expected_frequency_mhz) ||
       !isfinite(frequency_tolerance_mhz) ||
       frequency_tolerance_mhz < 0.0f) {
        snprintf(out->note, sizeof(out->note), "Analyzer settings invalid");
        return false;
    }

    const uint32_t serial = frames[0].serial;
    const uint8_t command = frames[0].command;
    for(int i = 0; i < n; i++) {
        if((frames[i].counter & ~counter_mask) != 0 ||
           frames[i].serial != serial || frames[i].command != command) {
            snprintf(out->note, sizeof(out->note), "Mixed frame identity");
            return false;
        }
        if(!isfinite(frames[i].frequency_mhz) ||
           fabsf(frames[i].frequency_mhz - expected_frequency_mhz) >
               frequency_tolerance_mhz) {
            snprintf(out->note, sizeof(out->note), "Profile frequency mismatch");
            return false;
        }
        if(i > 0) {
            uint32_t delta = (frames[i].counter - frames[i - 1].counter) & counter_mask;
            if(delta == 0) {
                snprintf(out->note, sizeof(out->note), "Duplicate counter");
                return false;
            }
            if(delta > max_delta) {
                snprintf(out->note, sizeof(out->note), "Gap or counter order mismatch");
                return false;
            }
        }
        out->order[i] = i;
    }

    out->sequence_len = n;
    out->base_counter = frames[0].counter;
    out->top_counter = frames[n - 1].counter;
    out->span = (out->top_counter - out->base_counter) & counter_mask;
    if(n < min_seq) {
        snprintf(out->note, sizeof(out->note), "Need %d frames; have %d", min_seq, n);
        return false;
    }

    out->sequence_candidate = true;
    snprintf(out->note, sizeof(out->note), "Experimental sequence candidate");
    return true;
}