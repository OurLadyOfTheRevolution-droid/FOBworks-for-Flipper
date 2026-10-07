#pragma once
#include "flipper_decoders.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Generalized RollBack / resync-window analyzer.
 *
 * FOBpwn is Honda-specific: it hardcodes the RollingPWN 3-code consecutive-run
 * check. This module generalizes that idea to ANY decoded rolling family with
 * an honest, monotonic counter (Subaru, Mazda V0, Suzuki, Kia V7, KeeLoq, …).
 *
 * The model follows Csikor et al. (RollBack, USENIX 2022): capture N codes in
 * order, let the receiver advance, then replay the oldest captured code. If the
 * receiver resynchronizes, every earlier code in the captured run is accepted
 * again. The analyzer only DECIDES that a captured run is a plausible
 * rollback candidate from the counter deltas — it does not, and cannot,
 * confirm that a specific receiver will accept a replay. That confirmation
 * still requires a live, authorized target.
 *
 * No radio is ever keyed by this module.
 */
#define GROLLBACK_MAX_CAPS 8

typedef struct {
    uint32_t counter;   /* decoder-reported counter (masked by counter_mask) */
    uint32_t serial;    /* transmitter serial / ID */
    uint8_t  command;   /* button / command bits */
    float    freq_mhz;  /* capture frequency */
    char     proto[32]; /* decode label */
} GrollbackFrame;

typedef struct {
    bool     candidate;      /* run plausibly forms a rollback sequence */
    int      seq_len;        /* frames in the accepted run */
    int      order[GROLLBACK_MAX_CAPS];
    uint32_t base_counter;
    uint32_t top_counter;
    uint32_t span;           /* masked distance base->top */
    uint32_t max_delta_seen; /* largest single step in the run */
    int      min_seq;        /* minimum run length that was required */
    uint32_t counter_mask;   /* mask applied (e.g. 16-bit for Cherokee) */
    char     proto[32];      /* protocol label */
    bool     enciphered;     /* hop is ciphertext; counter is not forgeable directly */
    char     note[96];
} GrollbackPlan;

/*
 * Analyze a run of decoded frames. All frames must share a serial, command,
 * protocol, and frequency; each masked counter step must be in [1, max_delta].
 * The run is a candidate only when its length reaches min_seq. Rejects a
 * plaintext-hop run from being called "enciphered" (forgeable), which matters
 * for honest framing: a plaintext counter is trivially predictable, so the
 * note distinguishes "rollback candidate" from "predictable plaintext".
 */
void grollback_analyze(const GrollbackFrame* frames, int n,
                       uint32_t counter_mask, int min_seq,
                       uint32_t max_delta, float freq_tol_mhz,
                       GrollbackPlan* out);

/* Convenience: build a plan from decoded results (counters already masked). */
void grollback_analyze_results(const FlipperDecodeResult* results, int n,
                               uint32_t counter_mask, int min_seq,
                               uint32_t max_delta, float freq_tol_mhz,
                               GrollbackPlan* out);