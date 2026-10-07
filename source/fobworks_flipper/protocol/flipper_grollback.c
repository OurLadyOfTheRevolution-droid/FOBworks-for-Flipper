#include "flipper_grollback.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void grollback_analyze(const GrollbackFrame* frames, int n,
                       uint32_t counter_mask, int min_seq,
                       uint32_t max_delta, float freq_tol_mhz,
                       GrollbackPlan* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    out->min_seq = min_seq;
    out->counter_mask = counter_mask;

    if(!frames || n < 1 || n > GROLLBACK_MAX_CAPS) {
        snprintf(out->note, sizeof(out->note), "capture count invalid");
        return;
    }
    if(min_seq < 1 || min_seq > GROLLBACK_MAX_CAPS || counter_mask == 0 ||
       max_delta == 0 || !isfinite(freq_tol_mhz) || freq_tol_mhz < 0.0f) {
        snprintf(out->note, sizeof(out->note), "analyzer settings invalid");
        return;
    }

    const uint32_t serial = frames[0].serial;
    const uint8_t command = frames[0].command;
    strncpy(out->proto, frames[0].proto, sizeof(out->proto) - 1);

    for(int i = 0; i < n; i++) {
        if(!isfinite(frames[i].freq_mhz) ||
           fabsf(frames[i].freq_mhz - frames[0].freq_mhz) > freq_tol_mhz) {
            snprintf(out->note, sizeof(out->note), "frequency mismatch");
            return;
        }
        if((frames[i].counter & ~counter_mask) != 0) {
            snprintf(out->note, sizeof(out->note),
                     "counter exceeds mask");
            return;
        }
        if(frames[i].serial != serial || frames[i].command != command) {
            snprintf(out->note, sizeof(out->note), "mixed frame identity");
            return;
        }
        if(strncmp(frames[i].proto, out->proto, sizeof(out->proto) - 1) != 0) {
            snprintf(out->note, sizeof(out->note), "mixed protocol");
            return;
        }
        if(i > 0) {
            uint32_t delta = (frames[i].counter - frames[i - 1].counter) & counter_mask;
            if(delta == 0) {
                snprintf(out->note, sizeof(out->note), "duplicate counter");
                return;
            }
            if(delta > max_delta) {
                snprintf(out->note, sizeof(out->note),
                         "counter gap exceeds max_delta");
                return;
            }
            if(delta > out->max_delta_seen) out->max_delta_seen = delta;
        }
        out->order[i] = i;
    }

    out->seq_len = n;
    out->base_counter = frames[0].counter;
    out->top_counter = frames[n - 1].counter;
    out->span = (out->top_counter - out->base_counter) & counter_mask;
    /* Enciphered when the frames did not expose a forgeable plaintext counter;
       the caller sets this from the decode result, so keep it as reported. */

    if(n < min_seq) {
        snprintf(out->note, sizeof(out->note), "need %d frames; have %d", min_seq, n);
        return;
    }

    out->candidate = true;
    snprintf(out->note, sizeof(out->note),
             "rollback candidate: %d frames, span %lu", n, (unsigned long)out->span);
}

void grollback_analyze_results(const FlipperDecodeResult* results, int n,
                               uint32_t counter_mask, int min_seq,
                               uint32_t max_delta, float freq_tol_mhz,
                               GrollbackPlan* out) {
    if(!out) return;
    if(!results || n < 1 || n > GROLLBACK_MAX_CAPS) {
        grollback_analyze(NULL, n, counter_mask, min_seq, max_delta,
                          freq_tol_mhz, out);
        return;
    }
    GrollbackFrame frames[GROLLBACK_MAX_CAPS];
    memset(frames, 0, sizeof(frames));
    for(int i = 0; i < n && i < GROLLBACK_MAX_CAPS; i++) {
        frames[i].counter = results[i].cnt & counter_mask;
        frames[i].serial = results[i].addr;
        frames[i].command = results[i].btn;
        frames[i].freq_mhz = results[i].freq_mhz;
        strncpy(frames[i].proto, results[i].proto, sizeof(frames[i].proto) - 1);
        out->enciphered = out->enciphered ||
                          (results[i].hop != results[i].cnt) ||
                          results[i].device_key_hex[0];
    }
    grollback_analyze(frames, n, counter_mask, min_seq, max_delta,
                      freq_tol_mhz, out);
}