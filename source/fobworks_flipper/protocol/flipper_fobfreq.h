#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * Oscillator-offset fingerprinting for clone detection.
 *
 * Every CC1101 receive reports the carrier frequency error (FREQEST) between
 * its own crystal and the transmitter's. That offset is dominated by the
 * transmitter's crystal tolerance — a handful of ppm to a few tens of ppm —
 * and it is stable per physical unit but differs between units. Two fobs of
 * the same make/model with the same serial still land on different offsets
 * because their crystals differ.
 *
 * A cloned key programmed from extracted EEPROM data decrypts the same and
 * carries the same serial, but it transmits from a *different* crystal, so its
 * measured offset will not match the genuine key's. This module compares two
 * offset profiles and reports whether they are consistent with the same
 * physical transmitter.
 *
 * Values are in ppm (parts-per-million of the nominal carrier) to be
 * frequency-independent: offset_ppm = 1e6 * freq_err_hz / freq_hz.
 * This is pure arithmetic and host-testable; the radio-acquisition glue that
 * feeds it FREQEST lives in the device layer.
 */
#define FOBFREQ_SAMPLES_MAX 16

typedef struct {
    float total_ppm;                 /* running sum of sample offsets */
    int   count;                     /* number of samples */
    float min_ppm;
    float max_ppm;
    float mean_ppm;                  /* last computed mean */
} FobOffsetProfile;

/* Reset a profile before a sampling run. */
void fobffreq_reset(FobOffsetProfile* p);

/* Add one ppm sample. Bounds-checks the count against FOBFREQ_SAMPLES_MAX. */
void fobffreq_add(FobOffsetProfile* p, float offset_ppm);

/* Finalize mean/min/max. Returns false if there are no samples. */
bool fobffreq_finalize(FobOffsetProfile* p);

/*
 * Compare two finalized profiles. Common-crystal verdict:
 *   - |mean_a - mean_b| within tolerance AND both spreads overlap
 * Returns true when the profiles are consistent with ONE physical transmitter.
 * tolerance_ppm is the comparison band (e.g. 5.0 ppm); a cloned key on a
 * different crystal typically sits well outside it.
 */
bool fobffreq_same_transmitter(const FobOffsetProfile* a,
                               const FobOffsetProfile* b,
                               float tolerance_ppm);

/* Difference between two profiles' means, in ppm. */
float fobffreq_mean_delta(const FobOffsetProfile* a, const FobOffsetProfile* b);