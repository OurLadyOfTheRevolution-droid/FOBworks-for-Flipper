#include "flipper_rf_analysis.h"
#include <math.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Helpers for summarizing captured data: byte entropy, an encoding hint from
   Te and baud, free-space path loss, and timing references for 23 protocols.
   Entropy is a measure of this sample's byte distribution, not a crypto test. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* These thresholds put a sample into a rough entropy bucket. They describe
   byte variation only; they do not identify a cipher or establish its strength:
   below 2.0 = low, below 6.0 = medium, and 6.0 or above = high. */

float rf_analysis_shannon_entropy(const uint8_t* data, size_t len) {
    if(!data || len == 0) return 0.0f;

    uint32_t byte_count[256] = {0};
    for(size_t i = 0; i < len; i++) {
        byte_count[data[i]]++;
    }

    float entropy = 0.0f;
    float inv_len = 1.0f / (float)len;
    for(int i = 0; i < 256; i++) {
        if(byte_count[i] > 0) {
            float p = (float)byte_count[i] * inv_len;
            entropy -= p * log2f(p);
        }
    }
    return entropy;
}

RfEntropyClass rf_analysis_entropy_class(float entropy) {
    if(entropy < 2.0f) return RfEntropyLow;
    if(entropy < 6.0f) return RfEntropyMedium;
    return RfEntropyHigh;
}

const char* rf_analysis_entropy_label(RfEntropyClass cls) {
    switch(cls) {
    case RfEntropyLow:    return "Low (fixed/weak)";
    case RfEntropyMedium: return "Medium (simple rolling)";
    case RfEntropyHigh:   return "High (strong crypto)";
    default:              return "Unknown";
    }
}

/* Estimate symbols per bit as (baud * te) / 1,000,000, then map 1, 2, 3, or
   4 symbols to NRZ, Manchester, 3-PWM, or 4-PWM. This is a timing-based hint. */

RfEncoding rf_analysis_classify_encoding(uint32_t te_us, uint32_t baud) {
    if(te_us == 0 || baud == 0) return RfEncodingUnknown;

    uint32_t symbols_per_bit = (baud * te_us) / 1000000;

    if(symbols_per_bit <= 1) return RfEncodingNRZ;
    if(symbols_per_bit == 2) return RfEncodingManchester;
    if(symbols_per_bit == 3) return RfEncodingPWM3;
    if(symbols_per_bit == 4) return RfEncodingPWM4;
    return RfEncodingUnknown;
}

const char* rf_analysis_encoding_label(RfEncoding enc) {
    switch(enc) {
    case RfEncodingNRZ:         return "NRZ";
    case RfEncodingManchester:  return "Manchester";
    case RfEncodingPWM3:        return "3-PWM";
    case RfEncodingPWM4:        return "4-PWM";
    default:                    return "Unknown";
    }
}

/* Estimate the baud rate from the shortest pulse in a RAW capture. */

uint32_t rf_analysis_estimate_baud(int32_t min_pulse_us) {
    if(min_pulse_us <= 0) return 0;
    return 1000000 / min_pulse_us;
}

/* Free-space path loss in dB: 20*log10(f_MHz) + 20*log10(d_m) - 147.55. */

float rf_analysis_fspl(float freq_mhz, float distance_m) {
    if(freq_mhz <= 0.0f || distance_m <= 0.0f) return 0.0f;
    return 20.0f * log10f(freq_mhz) + 20.0f * log10f(distance_m) - 147.55f;
}

/* Wavelength in millimetres, using the speed of light in metres per second. */
float rf_analysis_wavelength_mm(float freq_mhz) {
    if(freq_mhz <= 0.0f) return 0.0f;
    return 299792458000.0f / (freq_mhz * 1000000.0f);
}

/* Return one quarter of the wavelength in centimetres. */
float rf_analysis_quarter_wave_cm(float freq_mhz) {
    float wl_mm = rf_analysis_wavelength_mm(freq_mhz);
    return (wl_mm / 4.0f) / 10.0f;  /* mm → cm */
}

/* Estimate airtime and duty cycle from the supplied bit, baud, and repeat data. */

uint32_t rf_analysis_tx_time_us(uint32_t bits, uint32_t baud) {
    if(baud == 0) return 0;
    return (bits * 1000000) / baud;
}

uint32_t rf_analysis_total_tx_ms(uint32_t tx_time_ms, uint32_t repeat, uint32_t delay_ms) {
    if(repeat == 0) return 0;
    uint32_t total = tx_time_ms * repeat;
    if(repeat > 1) total += delay_ms * (repeat - 1);
    return total;
}

float rf_analysis_duty_cycle(uint32_t tx_time_ms, uint32_t repeat, uint32_t total_ms) {
    if(total_ms == 0) return 0.0f;
    return (float)(tx_time_ms * repeat * 100) / (float)total_ms;
}

/* Timing reference entries for the 23 protocols listed below. */

static const ProtoTimingEntry proto_timings[RF_PROTO_TIMING_COUNT] = {
    { "KeeLoq",       400,  800,  180, 66, NULL },
    { "Chrysler_V0",  300, 3700,  100, 80, NULL },
    { "Fiat_V0",      350,  700,  150, 64, NULL },
    { "Fiat_V1",      350,  700,  150, 64, NULL },
    { "Fiat_V2",      210,  420,   80, 64, NULL },
    { "Ford_V0",      250,  500,  120, 64, NULL },
    { "Ford_V1",       65,  130,   30, 136, NULL },
    { "Ford_V2",      200,  400,   80, 72, NULL },
    { "Ford_V3",      250,  500,  120, 64, NULL },
    { "Honda_Static",  63,  700,   30, 96, NULL },
    { "Honda_V1",    1000, 2000,  400, 64, NULL },
    { "Honda_V2",     400,  800,  180, 64, NULL },
    { "Kia_V0",       250,  500,  120, 64, "Honda_V0" },
    { "Kia_V1",       800, 1600,  300, 64, NULL },
    { "Kia_V2",       500, 1000,  200, 64, NULL },
    { "Kia_V3_V4",    400,  800,  180, 66, NULL },
    { "Kia_V5",       400,  800,  180, 64, NULL },
    { "Kia_V6",       200,  400,  100, 144, NULL },
    { "Kia_V7",       250,  500,  120, 64, NULL },
    { "PSA",          250,  500,  120, 128, NULL },
    { "Renault_V0",   125,  250,   60, 64, NULL },
    { "Renault_V1",   125,  250,   60, 64, NULL },
    { "VAG",          300,  600,  150, 64, NULL },
};

const ProtoTimingEntry* rf_analysis_get_timing(const char* proto_name) {
    if(!proto_name) return NULL;
    for(int i = 0; i < RF_PROTO_TIMING_COUNT; i++) {
        if(strcmp(proto_timings[i].name, proto_name) == 0)
            return &proto_timings[i];
        if(proto_timings[i].alias && strcmp(proto_timings[i].alias, proto_name) == 0)
            return &proto_timings[i];
    }
    return NULL;
}

const char* rf_analysis_get_alias(const char* proto_name) {
    const ProtoTimingEntry* e = rf_analysis_get_timing(proto_name);
    return e ? e->alias : NULL;
}

int rf_analysis_timing_count(void) {
    return RF_PROTO_TIMING_COUNT;
}

const ProtoTimingEntry* rf_analysis_timing_by_index(int idx) {
    if(idx < 0 || idx >= RF_PROTO_TIMING_COUNT) return NULL;
    return &proto_timings[idx];
}
