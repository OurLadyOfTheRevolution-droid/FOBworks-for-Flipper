/* Host tests for FOBreport, the generalized rollback analyzer, the oscillator
   fingerprint comparator, and the TPMS/RKE correlator. Pure modules only — no
   Furi, no radio, no transmit. */
#include "../protocol/flipper_fobreport.h"
#include "../protocol/flipper_grollback.h"
#include "../protocol/flipper_fobfreq.h"
#include "../protocol/flipper_fobtrack.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int checks;
static int failures;

static void expect(int ok, const char* label) {
    checks++;
    if(!ok) {
        printf("  FAIL: %s\n", label);
        failures++;
    }
}

static FlipperDecodeResult rke(uint32_t addr, uint32_t cnt, uint32_t hop,
                               bool rolling, bool keyed) {
    FlipperDecodeResult r;
    memset(&r, 0, sizeof(r));
    r.addr = addr;
    r.cnt = cnt;
    r.hop = hop;
    r.btn = 1;
    r.rolling = rolling;
    r.freq_mhz = 433.92f;
    r.predict_window = keyed ? 16 : 0;
    strncpy(r.proto, "KeeLoq", sizeof(r.proto) - 1);
    if(keyed) memcpy(r.device_key_hex, "0123456789ABCDEF", sizeof(r.device_key_hex));
    return r;
}

static void test_fobreport(void) {
    FobReportReport rep;

    /* Fixed code: identical presses. */
    fobreport_reset(&rep);
    FlipperDecodeResult f = rke(0x12345678, 0, 0x1111, false, false);
    expect(fobreport_add(&rep, &f), "fixed press 1 accepted");
    expect(fobreport_add(&rep, &f), "fixed press 2 accepted (same serial)");
    FlipperDecodeResult other = rke(0x99999999, 0, 0x1111, false, false);
    expect(!fobreport_add(&rep, &other), "different serial ignored");
    fobreport_finalize(&rep);
    expect(rep.counter_bits == 0, "observed span does not prove counter width");
    expect(rep.kind == FobReportFixed, "fixed code classified fixed");
    expect(rep.grade == FobReportGradeD, "fixed code gets grade D");
    expect(strstr(rep.summary, "replay") != NULL, "fixed summary mentions replay");

    /* Encrypted rolling with a tight window. */
    fobreport_reset(&rep);
    FlipperDecodeResult a = rke(0x12345678, 100, 0xDEAD0000, true, true);
    FlipperDecodeResult b = rke(0x12345678, 101, 0xDEAD0001, true, true);
    expect(fobreport_add(&rep, &a), "rolling press 1");
    expect(fobreport_add(&rep, &b), "rolling press 2");
    fobreport_finalize(&rep);
    expect(rep.kind == FobReportRolling, "rolling classified rolling");
    expect(rep.grade == FobReportGradeA, "encrypted rolling gets grade A");

    /* Rolling with a wide resync window. */
    fobreport_reset(&rep);
    FlipperDecodeResult w = rke(0x12345678, 100, 0xDEAD0000, true, true);
    w.predict_window = 512;
    fobreport_add(&rep, &w);
    fobreport_finalize(&rep);
    expect(rep.grade == FobReportGradeC, "wide window downgrades to C");
}

static void test_grollback(void) {
    GrollbackPlan plan;

    /* A clean 3-step run decodes as a candidate. */
    FlipperDecodeResult rs[3] = {
        rke(0x12345678, 10, 0, true, false),
        rke(0x12345678, 11, 0, true, false),
        rke(0x12345678, 12, 0, true, false),
    };
    grollback_analyze_results(rs, 3, 0xFFFF, 3, 4, 0.1f, &plan);
    expect(plan.candidate, "3-step run is a rollback candidate");
    expect(plan.span == 2, "span base->top is 2");
    expect(strstr(plan.note, "candidate") != NULL, "note names it a candidate");

    /* A gap larger than max_delta is rejected. */
    rs[2].cnt = 20;
    grollback_analyze_results(rs, 3, 0xFFFF, 3, 4, 0.1f, &plan);
    expect(!plan.candidate, "over-limit gap rejected");

    /* Insufficient length. */
    grollback_analyze_results(rs, 2, 0xFFFF, 3, 4, 0.1f, &plan);
    expect(!plan.candidate, "too-short run rejected");
}

static void test_fobfreq(void) {
    FobTimingProfile a, b, c;
    fobfreq_reset(&a);
    fobfreq_reset(&b);
    fobfreq_reset(&c);
    for(int i = 0; i < 3; i++) {
        fobfreq_add(&a, 400);
        fobfreq_add(&b, 432);
    }
    fobfreq_finalize(&a);
    fobfreq_finalize(&b);
    expect(!fobfreq_similar_timing(&a, &b, 32), "insufficient samples stay inconclusive");
    fobfreq_add(&a, 400);
    fobfreq_add(&b, 432);
    fobfreq_finalize(&a);
    fobfreq_finalize(&b);
    expect(fobfreq_similar_timing(&a, &b, 32), "one TE histogram bin is similar timing");
    expect(fobfreq_mean_delta(&a, &b) == 32, "difference is in microseconds");
    for(int i = 0; i < 4; i++) fobfreq_add(&c, 480);
    fobfreq_finalize(&c);
    expect(!fobfreq_similar_timing(&a, &c, 32), "distant timings differ, without identity claim");
    int before = a.count;
    fobfreq_add(&a, 0);
    fobfreq_add(&a, NAN);
    fobfreq_add(&a, INFINITY);
    expect(a.count == before, "invalid timing samples rejected");
    for(int i = 0; i < 40; i++) fobfreq_add(&a, 400);
    expect(a.count == FOBFREQ_SAMPLES_MAX, "profile storage bounded");
}

static void test_fobtrack(void) {
    FobtrackLog log;
    fobtrack_reset(&log);
    /* TPMS 0xAAAA co-occurs with RKE 0x1111 repeatedly within the window. */
    fobtrack_record(&log, FobtrackTpms, 0xAAAA, 1000);
    fobtrack_record(&log, FobtrackRke,  0x1111, 1200);
    fobtrack_record(&log, FobtrackTpms, 0xAAAA, 6000);
    fobtrack_record(&log, FobtrackRke,  0x1111, 6200);
    fobtrack_record(&log, FobtrackTpms, 0xBBBB, 10000); /* unrelated wheel */
    fobtrack_record(&log, FobtrackRke,  0x2222, 10200); /* different car */

    FobtrackLink links[4];
    int n = fobtrack_correlate(&log, links, 4);
    expect(n >= 2, "correlator produced links");
    expect(links[0].tpms_id == 0xAAAA && links[0].rke_serial == 0x1111,
           "top link is the repeated TPMS/RKE pair");
    expect(links[0].score == 2, "top link score counts both co-occurrences");

    fobtrack_reset(&log);
    for(int i = 0; i < FOBTRACK_MAX_EVENTS + 5; i++)
        fobtrack_record(&log, FobtrackRke, (uint32_t)i, (uint32_t)i * 10000);
    expect(log.count == FOBTRACK_MAX_EVENTS, "rolling observation window bounded");
    expect(log.events[0].id == 5, "oldest observations evicted");
    fobtrack_record(&log, FobtrackTpms, 0xAABB, 690000);
    fobtrack_record(&log, FobtrackRke, 0xCCDD, 690100);
    n = fobtrack_correlate(&log, links, 4);
    expect(n == 1 && links[0].rke_serial == 0xCCDD,
           "new observations still correlate after the first 64");
}

int main(void) {
    test_fobreport();
    test_grollback();
    test_fobfreq();
    test_fobtrack();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}