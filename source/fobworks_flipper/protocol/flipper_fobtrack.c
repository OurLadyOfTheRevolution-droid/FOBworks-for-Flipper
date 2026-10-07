#include "flipper_fobtrack.h"
#include <string.h>
#include <stdlib.h>

void fobtrack_reset(FobtrackLog* log) {
    if(!log) return;
    memset(log, 0, sizeof(*log));
}

void fobtrack_record(FobtrackLog* log, FobtrackKind kind, uint32_t id, uint32_t ts_ms) {
    if(!log || log->count >= FOBTRACK_MAX_EVENTS) return;
    log->events[log->count].kind = kind;
    log->events[log->count].id = id;
    log->events[log->count].ts_ms = ts_ms;
    log->count++;
}

typedef struct {
    uint32_t tpms_id;
    uint32_t rke_serial;
    int      score;
} _Pair;

static int _find_pair(_Pair* pairs, int n, uint32_t tpms, uint32_t rke) {
    for(int i = 0; i < n; i++)
        if(pairs[i].tpms_id == tpms && pairs[i].rke_serial == rke) return i;
    return -1;
}

int fobtrack_correlate(const FobtrackLog* log, FobtrackLink* out, int max_out) {
    if(!log || !out || max_out <= 0) return 0;

    _Pair* pairs = malloc(sizeof(_Pair) * FOBTRACK_MAX_EVENTS * 2u);
    if(!pairs) return 0;
    int np = 0;

    /* For every TPMS sighting, find RKE sightings within the window and bump
       the (tpms, rke) co-occurrence score. */
    for(int i = 0; i < log->count; i++) {
        if(log->events[i].kind != FobtrackTpms) continue;
        for(int j = 0; j < log->count; j++) {
            if(log->events[j].kind != FobtrackRke) continue;
            uint32_t a = log->events[i].ts_ms < log->events[j].ts_ms
                             ? log->events[i].ts_ms : log->events[j].ts_ms;
            uint32_t b = log->events[i].ts_ms > log->events[j].ts_ms
                             ? log->events[i].ts_ms : log->events[j].ts_ms;
            if(b - a <= FOBTRACK_WINDOW_MS) {
                int idx = _find_pair(pairs, np, log->events[i].id,
                                     log->events[j].id);
                if(idx < 0 && np < FOBTRACK_MAX_EVENTS * 2) {
                    pairs[np].tpms_id = log->events[i].id;
                    pairs[np].rke_serial = log->events[j].id;
                    pairs[np].score = 1;
                    np++;
                } else if(idx >= 0) {
                    pairs[idx].score++;
                }
            }
        }
    }

    /* Sort descending by score (simple insertion sort; np is small). */
    for(int i = 1; i < np; i++) {
        _Pair key = pairs[i];
        int k = i - 1;
        while(k >= 0 && pairs[k].score < key.score) {
            pairs[k + 1] = pairs[k];
            k--;
        }
        pairs[k + 1] = key;
    }

    int n = np < max_out ? np : max_out;
    for(int i = 0; i < n; i++) {
        out[i].tpms_id = pairs[i].tpms_id;
        out[i].rke_serial = pairs[i].rke_serial;
        out[i].score = pairs[i].score;
    }
    free(pairs);
    return n;
}