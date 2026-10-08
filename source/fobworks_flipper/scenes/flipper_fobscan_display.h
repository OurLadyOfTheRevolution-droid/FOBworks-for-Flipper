#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../protocol/flipper_decoders.h"

/* Owned text only: no pointers into live capture state or key material. */
typedef struct {
    char header[16];
    char badge[12];
    char rows[5][48];
    uint8_t y[5];
    bool badge_visible;
    bool divider;
    bool highlight;
    bool decoded_highlight;
} FlipperFobscanDisplay;

typedef struct {
    int mode; /* Scan=0, RangeSet=1, Sweep=2; matches FobscanUiMode. */
    int range_step;
    int preset;
    int squelch;
    float freq_mhz;
    float cursor_mhz;
    float range_min_mhz;
    float range_max_mhz;
    float rssi_dbm;
    uint32_t capture_count;
    bool decode_valid;
    bool flash;
    bool flash_decoded;
    const FlipperDecodeResult* decode;
} FlipperFobscanDisplayInput;

/* I run this on the app thread, while holding the SDK's locking view model. */
void flipper_fobscan_display_build(
    FlipperFobscanDisplay* out, const FlipperFobscanDisplayInput* in);
