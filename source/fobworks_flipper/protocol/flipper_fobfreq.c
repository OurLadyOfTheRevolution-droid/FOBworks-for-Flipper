#include "flipper_fobfreq.h"
#include <math.h>
#include <string.h>

void fobfreq_reset(FobTimingProfile* p) {
    if(p) memset(p, 0, sizeof(*p));
}

void fobfreq_add(FobTimingProfile* p, float te_us) {
    if(!p || p->count >= FOBFREQ_SAMPLES_MAX || !(te_us > 0) || !isfinite(te_us))
        return;
    if(p->count == 0) {
        p->min_us = p->max_us = te_us;
    } else {
        if(te_us < p->min_us) p->min_us = te_us;
        if(te_us > p->max_us) p->max_us = te_us;
    }
    p->total_us += te_us;
    p->count++;
}

bool fobfreq_finalize(FobTimingProfile* p) {
    if(!p || p->count == 0) return false;
    p->mean_us = p->total_us / (float)p->count;
    return true;
}

float fobfreq_mean_delta(const FobTimingProfile* a, const FobTimingProfile* b) {
    return a && b ? fabsf(a->mean_us - b->mean_us) : 0;
}

bool fobfreq_similar_timing(const FobTimingProfile* a, const FobTimingProfile* b,
                           float tolerance_us) {
    if(!a || !b || a->count < FOBFREQ_SAMPLES_MIN ||
       b->count < FOBFREQ_SAMPLES_MIN || !(tolerance_us >= 0))
        return false;
    return fobfreq_mean_delta(a, b) <= tolerance_us;
}
