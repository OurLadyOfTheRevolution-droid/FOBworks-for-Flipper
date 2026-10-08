#pragma once
#include "flipper_decoders.h"
#include <stdbool.h>
#include <stdint.h>

/* FOBreport — read-only "is my remote secure?" health check. I sample up to FLIPPER_FOBREPORT_MAX_PRESSES decoded presses for one remote and grade it without ever transmitting, replaying, or cloning. The grade is derived from observable properties the decoders already compute: - rolling vs. fixed code (the decisive first split) - whether the hopping word is enciphered or plaintext - counter width in bits - whether the counter advances monotonically between presses - the estimated resynchronization window (number of steps the receiver accepts ahead of its expected value, when the decoder reports one). This is a classification of what a capture reveals, not a proof that a given receiver will accept a replay or rollback. I never emit radio. */
#define FLIPPER_FOBREPORT_MAX_PRESSES 8

typedef enum {
    FobReportRolling = 0, /* code changes every press; replay-resistant */
    FobReportFixed,       /* same code every press; trivially replayable */
} FobReportKind;

typedef enum {
    FobReportGradeA = 0, /* strong: enciphered rolling, wide counter, tight window */
    FobReportGradeB,     /* rolling but plaintext hop or narrower counter */
    FobReportGradeC,     /* rolling but weak/wide resync window */
    FobReportGradeD,     /* fixed code (replay/clone exposed) */
    FobReportGradeU,     /* unknown: no decodable captures */
} FobReportGrade;

typedef struct {
    FobReportKind kind;
    FobReportGrade grade;
    int     presses;              /* captures actually classified */
    uint32_t serial;              /* transmitter serial / ID */
    char    proto[32];            /* protocol label of the last decode */
    bool    rolling;              /* any decoder flagged rolling */
    bool    enciphered;           /* hopping word is ciphertext, not a counter */
    int     counter_bits;         /* observed counter width (0 = fixed) */
    uint32_t min_counter;         /* lowest counter seen */
    uint32_t max_counter;         /* highest counter seen */
    bool    advances;             /* counter strictly advanced between presses */
    bool    identical;            /* an identical press was seen (fixed/dup) */
    uint32_t window;              /* largest advertised resync window (0 = none) */
    bool    has_press;            /* at least one press was recorded */
    char    summary[96];          /* one-line human verdict */
} FobReportReport;

/* I reset an in-progress report for a new sampling run. */
void fobreport_reset(FobReportReport* rep);

/* I feed one decoded press. I return true if accepted (same remote, or first press); false if it belongs to a different serial and I ignored it. */
bool fobreport_add(FobReportReport* rep, const FlipperDecodeResult* r);

/* I derive kind/grade/summary from what I have collected. This is safe on an empty report (yields FobReportGradeU). */
void fobreport_finalize(FobReportReport* rep);