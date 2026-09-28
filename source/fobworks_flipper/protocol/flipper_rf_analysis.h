#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Timing, encoding, entropy, and RF link-budget helpers used by the app. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* Entropy buckets describe sample variation only; they do not assess cipher
   strength. */
typedef enum {
    RfEntropyLow,    /* entropy < 2.0 */
    RfEntropyMedium, /* 2.0 <= entropy < 6.0 */
    RfEntropyHigh,   /* entropy >= 6.0 */
} RfEntropyClass;

float rf_analysis_shannon_entropy(const uint8_t* data, size_t len);
RfEntropyClass rf_analysis_entropy_class(float entropy);
const char* rf_analysis_entropy_label(RfEntropyClass cls);

/* Classify a likely encoding from the estimated symbols-per-bit ratio. */
typedef enum {
    RfEncodingUnknown,
    RfEncodingNRZ,         /* symbols_per_bit == 1 */
    RfEncodingManchester,  /* symbols_per_bit == 2 */
    RfEncodingPWM3,        /* symbols_per_bit == 3 */
    RfEncodingPWM4,        /* symbols_per_bit == 4 */
} RfEncoding;

RfEncoding rf_analysis_classify_encoding(uint32_t te_us, uint32_t baud);
const char* rf_analysis_encoding_label(RfEncoding enc);
uint32_t rf_analysis_estimate_baud(int32_t min_pulse_us);

/* ── RF Physics ──────────────────────────────────────────────────────────── */
float rf_analysis_fspl(float freq_mhz, float distance_m);
float rf_analysis_wavelength_mm(float freq_mhz);
float rf_analysis_quarter_wave_cm(float freq_mhz);

/* ── TX Timing ───────────────────────────────────────────────────────────── */
uint32_t rf_analysis_tx_time_us(uint32_t bits, uint32_t baud);
uint32_t rf_analysis_total_tx_ms(uint32_t tx_time_ms, uint32_t repeat, uint32_t delay_ms);
float rf_analysis_duty_cycle(uint32_t tx_time_ms, uint32_t repeat, uint32_t total_ms);

/* ── Protocol Timing Database (FOBworks, 23 protocols) ───────────────────── */
#define RF_PROTO_TIMING_COUNT 23

typedef struct {
    const char*  name;
    uint32_t     te_short;   /* short pulse duration (μs) */
    uint32_t     te_long;    /* long pulse duration (μs) */
    uint32_t     te_delta;   /* tolerance for pulse matching */
    uint32_t     min_bits;   /* minimum valid frame bits */
    const char*  alias;      /* alternate name (NULL if none) */
} ProtoTimingEntry;

const ProtoTimingEntry* rf_analysis_get_timing(const char* proto_name);
const char* rf_analysis_get_alias(const char* proto_name);
int rf_analysis_timing_count(void);
const ProtoTimingEntry* rf_analysis_timing_by_index(int idx);
