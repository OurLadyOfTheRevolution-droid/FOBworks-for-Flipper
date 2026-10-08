#include "flipper_fobscan_display.h"
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(FlipperFobscanDisplay) <= 320,
               "display payload exceeds its reserved RAM budget");

/* All callers pass fixed UI literals that fit their destination. Sharing this copy avoids duplicating long literal stores in the small host FAP. */
static __attribute__((noinline)) void copy_literal(char* out, const char* text) {
    strcpy(out, text);
}

static void format_freq(char* out, size_t capacity, float value) {
    /* Negated positive bounds reject NaN/Inf before an integer conversion. */
    if(!(value >= 0.0f && value <= 1000.0f)) {
        snprintf(out, capacity, "--");
        return;
    }
    unsigned centi = (unsigned)(value * 100.0f + 0.5f);
    snprintf(out, capacity, "%u.%02u", centi / 100, centi % 100);
}

static const char* preset_name(int preset) {
    switch(preset) {
    case 0: return "OOK 650k";
    case 1: return "OOK 270k";
    case 2: return "2FSK 24k";
    case 3: return "2FSK 48k";
    default: return "?";
    }
}

void flipper_fobscan_display_build(
    FlipperFobscanDisplay* out, const FlipperFobscanDisplayInput* in) {
    memset(out, 0, sizeof(*out));
    char freq[12], lo[12], hi[12];
    format_freq(freq, sizeof(freq), in->freq_mhz);
    if(in->mode == 1) {
        copy_literal(out->header, "Set Range");
        format_freq(freq, sizeof(freq), in->cursor_mhz);
        format_freq(lo, sizeof(lo), in->range_min_mhz);
        format_freq(hi, sizeof(hi), in->range_max_mhz);
        out->y[0] = 24;
        out->y[1] = 36;
        out->y[2] = 46;
        out->y[3] = 62;
        snprintf(out->rows[0], sizeof(out->rows[0]), "Cursor %s MHz", freq);
        if(in->range_step == 0) {
            copy_literal(out->rows[1], "Up/Dn: move   OK: set MAX");
        } else if(in->range_step == 1) {
            snprintf(out->rows[1], sizeof(out->rows[1]), "MAX %s", hi);
            copy_literal(out->rows[2], "Up/Dn: move   OK: set MIN");
        } else {
            snprintf(out->rows[1], sizeof(out->rows[1]), "MIN %s  MAX %s", lo, hi);
            copy_literal(out->rows[2], "Left: confirm & sweep");
        }
        copy_literal(out->rows[3], "Back: cancel");
        return;
    }
    out->badge_visible = true;
    out->divider = true;
    out->highlight = in->flash;
    out->decoded_highlight = in->flash_decoded;
    out->y[0] = 22;
    out->y[1] = 32;
    out->y[2] = 45;
    out->y[3] = 55;
    out->y[4] = 63;
    copy_literal(out->header, in->mode == 2 ? "FOBscan Sweep" : "FOBscan");
    snprintf(out->badge, sizeof(out->badge), "#%lu", (unsigned long)in->capture_count);
    snprintf(out->rows[0], sizeof(out->rows[0]), "%s MHz  %s", freq, preset_name(in->preset));
    if(in->rssi_dbm >= -200.0f && in->rssi_dbm <= 0.0f) {
        int rssi = (int)(in->rssi_dbm - 0.5f);
        snprintf(out->rows[1], sizeof(out->rows[1]), "RSSI %d  Sq %d", rssi, in->squelch);
    } else {
        snprintf(out->rows[1], sizeof(out->rows[1]), "RSSI --  Sq %d", in->squelch);
    }
    if(in->decode_valid && in->decode) {
        const FlipperDecodeResult* r = in->decode;
        snprintf(out->rows[2], sizeof(out->rows[2]), "%.*s",
                 (int)sizeof(r->proto) - 1, r->proto);
        snprintf(out->rows[3], sizeof(out->rows[3]), "Addr %08lX  Cnt %lu",
                 (unsigned long)r->addr, (unsigned long)r->cnt);
        if(r->mfr_name[0]) {
            snprintf(out->rows[4], sizeof(out->rows[4]), "Key: %.*s",
                     (int)sizeof(r->mfr_name) - 1, r->mfr_name);
        } else if(r->predict_window > 0) {
            snprintf(out->rows[4], sizeof(out->rows[4]), "Next: %lu-%lu",
                     (unsigned long)r->predict_lo, (unsigned long)r->predict_hi);
        }
    } else if(in->flash) {
        out->y[2] = 46;
        copy_literal(out->rows[2], "Undecoded burst saved");
        copy_literal(out->rows[3], "R=view in library");
    } else {
        out->y[2] = 46;
        copy_literal(out->rows[2], in->mode == 2 ? "Sweeping range..." : "Waiting for signal...");
        copy_literal(out->rows[3], in->mode == 2 ? "R=library" : "Up/Dn=freq  R=library");
        copy_literal(out->rows[4], in->mode == 2 ? "[OK]=stop here" : "L=set range");
    }
}
