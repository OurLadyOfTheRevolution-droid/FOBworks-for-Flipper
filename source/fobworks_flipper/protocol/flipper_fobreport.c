#include "flipper_fobreport.h"
#include <string.h>
#include <stdio.h>

/* A counter window wider than this is treated as a weak (grade C) resync. */
#define FOBREPORT_WIDE_WINDOW 64u

void fobreport_reset(FobReportReport* rep) {
    if(!rep) return;
    memset(rep, 0, sizeof(*rep));
    rep->grade = FobReportGradeU;
    rep->min_counter = UINT32_MAX;
}

bool fobreport_add(FobReportReport* rep, const FlipperDecodeResult* r) {
    if(!rep || !r || !r->proto[0]) return false;

    /* First press captures the remote identity; later presses must match it. */
    if(!rep->has_press) {
        rep->serial = r->addr;
        rep->has_press = true;
        rep->presses = 1;
        rep->min_counter = r->cnt;
        rep->max_counter = r->cnt;
    } else {
        if(r->addr != rep->serial) return false; /* different remote */
        rep->presses++;
        if(rep->presses > FLIPPER_FOBREPORT_MAX_PRESSES)
            rep->presses = FLIPPER_FOBREPORT_MAX_PRESSES;
        if(r->cnt < rep->min_counter) rep->min_counter = r->cnt;
        if(r->cnt > rep->max_counter) rep->max_counter = r->cnt;
    }

    strncpy(rep->proto, r->proto, sizeof(rep->proto) - 1);
    rep->proto[sizeof(rep->proto) - 1] = '\0';

    if(r->rolling) rep->rolling = true;
    /* Enciphered hop is only meaningful for rolling codes: for a fixed code, hop is the whole (plaintext) payload and "enciphered" is meaningless. I mark enciphered when a rolling decode carries a recovered device key or a hop word that is not the plaintext counter. */
    if(r->rolling && (r->hop != r->cnt || r->device_key_hex[0]))
        rep->enciphered = true;
    if(r->predict_window > rep->window) rep->window = r->predict_window;

    /* Monotonicity / identity: an identical hop or counter is a duplicate press from a fixed code (or a repeat of the same rolling step). */
    if(rep->presses > 1) {
        rep->advances = (rep->max_counter != rep->min_counter);
        rep->identical = (rep->max_counter == rep->min_counter);
    }
    return true;
}

void fobreport_finalize(FobReportReport* rep) {
    if(!rep || !rep->has_press) {
        if(rep) {
            memset(rep, 0, sizeof(*rep));
            rep->grade = FobReportGradeU;
            snprintf(rep->summary, sizeof(rep->summary),
                     "no decodable capture — press a fob and retry");
        }
        return;
    }

    /* A remote is fixed when the decoder says so and the counter never moved across presses. It is rolling when the decoder flags rolling OR the counter advanced OR the hop is enciphered. */
    bool fixed = (!rep->rolling && !rep->advances && !rep->enciphered &&
                  rep->window == 0);
    rep->kind = fixed ? FobReportFixed : FobReportRolling;

    if(fixed) {
        rep->counter_bits = 0;
        rep->grade = FobReportGradeD;
        snprintf(rep->summary, sizeof(rep->summary),
                 "%s fixed code — recordable and replayable; clone exposed",
                 rep->proto);
        return;
    }

    /* A short observed span cannot establish the protocol counter width. */
    rep->counter_bits = 0;

    if(rep->window > FOBREPORT_WIDE_WINDOW) {
        rep->grade = FobReportGradeC;
        snprintf(rep->summary, sizeof(rep->summary),
                 "%s rolling; predicted range %lu (receiver untested)",
                 rep->proto, (unsigned long)rep->window);
    } else if(!rep->enciphered) {
        rep->grade = FobReportGradeB;
        snprintf(rep->summary, sizeof(rep->summary),
                 "%s rolling fields observed; receiver untested",
                 rep->proto);
    } else {
        rep->grade = FobReportGradeA;
        snprintf(rep->summary, sizeof(rep->summary),
                 "%s encrypted rolling observed; receiver untested",
                 rep->proto);
    }
}