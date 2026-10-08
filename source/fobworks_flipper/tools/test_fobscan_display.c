#include "../scenes/flipper_fobscan_display.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void terminated(const FlipperFobscanDisplay* d) {
    assert(memchr(d->header, 0, sizeof(d->header)));
    assert(memchr(d->badge, 0, sizeof(d->badge)));
    for(unsigned i = 0; i < 5; ++i) {
        assert(memchr(d->rows[i], 0, sizeof(d->rows[i])));
        assert(d->y[i] <= 63);
    }
}

int main(void) {
    FlipperFobscanDisplay d;
    FlipperFobscanDisplayInput in = {
        .freq_mhz = 433.92f, .rssi_dbm = -99.5f, .squelch = -90,
        .cursor_mhz = 315.0f, .range_min_mhz = 300.0f, .range_max_mhz = 928.0f,
    };
    flipper_fobscan_display_build(&d, &in);
    assert(!strcmp(d.rows[0], "433.92 MHz  OOK 650k"));
    assert(!strcmp(d.rows[1], "RSSI -100  Sq -90"));
    in.capture_count = UINT32_MAX;
    flipper_fobscan_display_build(&d, &in);
    assert(!strcmp(d.badge, "#4294967295"));
    float invalid[] = {NAN, INFINITY, -INFINITY, 1e30f};
    for(unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        in.rssi_dbm = invalid[i];
        in.freq_mhz = invalid[i];
        flipper_fobscan_display_build(&d, &in);
        assert(strstr(d.rows[0], "-- MHz"));
        assert(strstr(d.rows[1], "RSSI --"));
        terminated(&d);
    }
    FlipperDecodeResult r;
    memset(&r, 'A', sizeof(r)); /* Deliberately unterminated input strings. */
    r.addr = UINT32_MAX;
    r.cnt = UINT32_MAX;
    in.decode = &r;
    in.decode_valid = true;
    in.flash = true;
    in.flash_decoded = true;
    flipper_fobscan_display_build(&d, &in);
    assert(strlen(d.rows[2]) == 31);
    assert(strlen(d.rows[4]) == 36);
    assert(d.highlight && d.decoded_highlight);
    terminated(&d);
    /* Snapshot text must survive subsequent changes to the live result. */
    memset(&r, 0, sizeof(r));
    assert(d.rows[2][0] == 'A' && d.rows[4][5] == 'A');
    in.mode = 1;
    for(in.range_step = 0; in.range_step <= 2; ++in.range_step) {
        flipper_fobscan_display_build(&d, &in);
        assert(!d.badge_visible && !d.divider && !d.highlight);
        assert(!strcmp(d.rows[0], "Cursor 315.00 MHz"));
        terminated(&d);
    }
    in.mode = 2;
    in.decode_valid = false;
    in.flash = false;
    flipper_fobscan_display_build(&d, &in);
    assert(!strcmp(d.header, "FOBscan Sweep"));
    assert(!strcmp(d.rows[4], "[OK]=stop here"));
    puts("FOBscan display snapshot: PASS (bounds, nonfinite values, owned text, scan/range/sweep)");
    return 0;
}
