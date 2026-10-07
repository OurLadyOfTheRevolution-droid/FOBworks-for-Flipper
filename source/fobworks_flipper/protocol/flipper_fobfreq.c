#include "flipper_fobfreq.h"
#include <math.h>

void fobffreq_reset(FobOffsetProfile* p) {
    if(!p) return;
    p->total_ppm = 0.0f;
    p->count = 0;
    p->min_ppm = 0.0f;
    p->max_ppm = 0.0f;
    p->mean_ppm = 0.0f;
}

void fobffreq_add(FobOffsetProfile* p, float offset_ppm) {
    if(!p || p->count >= FOBFREQ_SAMPLES_MAX) return;
    if(p->count == 0) {
        p->min_ppm = offset_ppm;
        p->max_ppm = offset_ppm;
    } else {
        if(offset_ppm < p->min_ppm) p->min_ppm = offset_ppm;
        if(offset_ppm > p->max_ppm) p->max_ppm = offset_ppm;
    }
    p->total_ppm += offset_ppm;
    p->count++;
}

bool fobffreq_finalize(FobOffsetProfile* p) {
    if(!p || p->count == 0) return false;
    p->mean_ppm = p->total_ppm / (float)p->count;
    return true;
}

float fobffreq_mean_delta(const FobOffsetProfile* a, const FobOffsetProfile* b) {
    if(!a || !b) return 0.0f;
    return fabsf(a->mean_ppm - b->mean_ppm);
}

bool fobffreq_same_transmitter(const FobOffsetProfile* a,
                               const FobOffsetProfile* b,
                               float tolerance_ppm) {
    if(!a || !b || a->count == 0 || b->count == 0) return false;
    if(tolerance_ppm < 0.0f) tolerance_ppm = 0.0f;

    /* Same crystal: means within the band AND the two spreads overlap. Two
       different crystals can be separated in the mean even when their raw
       ranges overlap, so the mean gap is the primary discriminator. */
    float gap = fobffreq_mean_delta(a, b);
    bool means_close = gap <= tolerance_ppm;
    bool ranges_overlap = !(a->max_ppm < b->min_ppm || b->max_ppm < a->min_ppm);
    return means_close && ranges_overlap;
}