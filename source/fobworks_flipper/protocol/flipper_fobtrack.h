#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * TPMS <-> RKE cross-correlation (asset fingerprinting without CAN access).
 *
 * Tire-pressure sensors transmit periodically (while parked and while rolling);
 * the RKE fob fires on lock/unlock. Both are captured in the same session. A
 * TPMS sensor ID and an RKE serial that repeatedly co-occur in time and space
 * almost certainly belong to the same vehicle, because the wheel sensor stays
 * physically co-located with the key it unlocks.
 *
 * This lets a researcher associate a fixed wheel-ID with a specific key serial
 * — a poor-man's vehicle fingerprint — from ambient RF alone. No bus access,
 * no pairing, no transmission.
 *
 * This module is a bounded co-occurrence scorer: it stores timestamped sighting
 * pairs within a window and reports which RKE serial each TPMS ID is most
 * strongly co-located with. It is pure C and host-testable.
 */
#define FOBTRACK_WINDOW_MS 3000u  /* max gap between sighting times to correlate */
#define FOBTRACK_MAX_EVENTS 64    /* bounded event log */

typedef enum {
    FobtrackTpms = 0,
    FobtrackRke,
} FobtrackKind;

typedef struct {
    FobtrackKind kind;
    uint32_t id;         /* TPMS sensor id, or RKE serial */
    uint32_t ts_ms;      /* monotonic capture timestamp */
} FobtrackEvent;

typedef struct {
    FobtrackEvent events[FOBTRACK_MAX_EVENTS];
    int count;
} FobtrackLog;

typedef struct {
    uint32_t tpms_id;
    uint32_t rke_serial;
    int      score;      /* number of within-window co-occurrences */
} FobtrackLink;

    /* Reset the log. */
void fobtrack_reset(FobtrackLog* log);

/* Record a sighting, evicting the oldest when the rolling window is full.
   Timestamps must be monotonic non-decreasing within a session. */
void fobtrack_record(FobtrackLog* log, FobtrackKind kind, uint32_t id, uint32_t ts_ms);

/*
 * Score the strongest TPMS->RKE associations in the log. Writes up to max_out
 * links into out[] (sorted by score descending) and returns the number written.
 * A link needs at least one within-window co-occurrence.
 */
int fobtrack_correlate(const FobtrackLog* log, FobtrackLink* out, int max_out);