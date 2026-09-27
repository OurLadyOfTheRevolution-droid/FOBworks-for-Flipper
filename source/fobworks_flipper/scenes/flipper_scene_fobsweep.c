#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <stdio.h>

/* FOBsweep — realtime signal-level meter.  This is the web dashboard "Scan"
 * (frequency sweep / spectrum) tab promoted to its own utility: it samples
 * RSSI on the selected frequency every status tick and, when auto-advance is
 * on, walks the shared freq table so the bottom bar row fills into a rolling
 * per-frequency spectrum with peak-hold.  No decode/replay here — those live in
 * the Library. */

#define SWEEP_FLOOR_DBM (-100.0f)
#define SWEEP_CEIL_DBM  (-30.0f)

/* Ticks (500 ms each) to dwell on a frequency after a hit while auto-sweeping —
   matches FOBscan's FOBSCAN_SWEEP_LINGER so both utilities behave identically. */
#define SWEEP_LINGER 4

static int sweep_count(void) {
    int n = FOBSCAN_FREQ_COUNT;
    if(n > FOBSCAN_FREQ_MAX) n = FOBSCAN_FREQ_MAX;
    if(n < 1) n = 1;   /* never let modulo/indexing see a zero count */
    return n;
}

/* Map an RSSI in dBm to a 0..h pixel bar height. */
static int sweep_bar_h(float dbm, int h) {
    if(dbm < SWEEP_FLOOR_DBM) dbm = SWEEP_FLOOR_DBM;
    if(dbm > SWEEP_CEIL_DBM) dbm = SWEEP_CEIL_DBM;
    float frac = (dbm - SWEEP_FLOOR_DBM) / (SWEEP_CEIL_DBM - SWEEP_FLOOR_DBM);
    return (int)(frac * h + 0.5f);
}

/* ── Draw ─────────────────────────────────────────────────────────────────── */
void flipper_fobsweep_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobsweepState* s = &app->fobsweep;
    int n = sweep_count();
    if(s->sel_idx < 0 || s->sel_idx >= n) s->sel_idx = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBsweep");

    canvas_set_font(canvas, FontSecondary);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s", s->auto_advance ? "AUTO" : "MANUAL");
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, hdr);

    /* Selected frequency + live level. */
    char line[48];
    snprintf(line, sizeof(line), "%.2f MHz  %.0f dBm",
             (double)FOBSCAN_FREQS[s->sel_idx], (double)s->rssi[s->sel_idx]);
    canvas_draw_str(canvas, 0, 22, line);

    /* Live meter bar for the selected frequency. */
    int mw = sweep_bar_h(s->rssi[s->sel_idx], 120);
    canvas_draw_frame(canvas, 0, 26, 122, 8);
    if(mw > 0) canvas_draw_box(canvas, 1, 27, mw, 6);

    /* Strongest-signal indicator: the frequency holding the highest peak.  Only
       shown once something has actually risen off the noise floor. */
    if(s->peak_idx < 0 || s->peak_idx >= n) s->peak_idx = 0;
    char peak_line[48];
    if(s->peak[s->peak_idx] > SWEEP_FLOOR_DBM) {
        snprintf(peak_line, sizeof(peak_line), "Peak %.2f MHz %.0f dBm",
                 (double)FOBSCAN_FREQS[s->peak_idx], (double)s->peak[s->peak_idx]);
    } else {
        snprintf(peak_line, sizeof(peak_line), "Peak --");
    }
    canvas_draw_str(canvas, 0, 44, peak_line);

    /* Per-frequency peak-hold spectrum row along the bottom. */
    int base_y = 63;
    int max_h = 16;
    int top_y = base_y - max_h;
    int bw = 122 / (n > 0 ? n : 1);
    if(bw < 2) bw = 2;
    for(int i = 0; i < n; i++) {
        int x = 2 + i * bw;
        int h = sweep_bar_h(s->peak[i], max_h);
        if(h > 0) canvas_draw_box(canvas, x, base_y - h, bw - 1, h);
        if(i == s->sel_idx) {
            /* Marker under the selected column. */
            canvas_draw_line(canvas, x, base_y + 1, x + bw - 2, base_y + 1);
        }
        if(i == s->peak_idx && s->peak[s->peak_idx] > SWEEP_FLOOR_DBM) {
            /* Cap marker above the strongest column (distinct from the selected
               underline), so the peak is visible even as the sweep moves on. */
            canvas_draw_box(canvas, x, top_y - 3, bw - 1, 2);
        }
    }
}

/* A raw view_alloc() view only repaints when its model is committed; input
   handlers mutate app state directly, so force a commit-with-update after a
   visible change or the meter stays frozen until the view is switched. */
static void fobsweep_redraw(FlipperApp* app) {
    view_get_model(app->fobsweep_view);
    view_commit_model(app->fobsweep_view, true);
}

/* ── Input ───────────────────────────────────────────────────────────────── */
bool flipper_fobsweep_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobsweepState* s = &app->fobsweep;
    if(e->type != InputTypeShort && e->type != InputTypeRepeat) return false;
    int n = sweep_count();

    switch(e->key) {
    case InputKeyOk:
        if(e->type != InputTypeShort) return false;
        s->auto_advance = !s->auto_advance;
        fobsweep_redraw(app);
        return true;
    case InputKeyUp:
        s->sel_idx = (s->sel_idx + 1) % n;
        s->auto_advance = false;
        fobsweep_redraw(app);
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventRetune);
        return true;
    case InputKeyDown:
        s->sel_idx = (s->sel_idx + n - 1) % n;
        s->auto_advance = false;
        fobsweep_redraw(app);
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventRetune);
        return true;
    default:
        return false;
    }
}

/* ── Scene lifecycle ──────────────────────────────────────────────────────── */
static void sweep_tune(FlipperApp* app) {
    FlipperFobsweepState* s = &app->fobsweep;
    flipper_capture_stop(app->capture);
    app->capture->freq_mhz    = FOBSCAN_FREQS[s->sel_idx];
    app->capture->preset      = app->adv.preset;
    app->capture->squelch_dbm = SWEEP_FLOOR_DBM;   /* meter, not a burst gate */
    app->capture->force_proto = FlipperForceAuto;
    flipper_capture_start(app->capture);
}

void flipper_scene_fobsweep_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobsweepState* s = &app->fobsweep;

    int n = sweep_count();
    s->sel_idx = app->adv.freq_idx;
    if(s->sel_idx < 0 || s->sel_idx >= n) s->sel_idx = FOBSCAN_FREQ_DEFAULT % n;
    s->auto_advance = true;
    s->linger       = 0;
    s->peak_idx     = s->sel_idx;
    for(int i = 0; i < FOBSCAN_FREQ_MAX; i++) {
        s->rssi[i] = SWEEP_FLOOR_DBM;
        s->peak[i] = SWEEP_FLOOR_DBM;
    }

    app->capture->on_edge = NULL;   /* no decode in the meter */
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobsweep);

    flipper_app_gui_radio_acquire(app);
    sweep_tune(app);
    notification_message(app->notifications, &sequence_blink_blue_100);
}

bool flipper_scene_fobsweep_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobsweepState* s = &app->fobsweep;
    bool consumed = false;

    if(e.type == SceneManagerEventTypeCustom) {
        if(e.event == FlipperEventRetune) {
            sweep_tune(app);
            consumed = true;
        }
        if(e.event == FlipperEventStatusTick) {
            /* Sample the current frequency and hold its peak. */
            float r = flipper_capture_rssi(app->capture);
            int n = sweep_count();
            int i = s->sel_idx;
            s->rssi[i] = r;
            if(r > s->peak[i]) s->peak[i] = r;

            /* Track the frequency holding the strongest peak, for the on-display
               indicator. */
            s->peak_idx = 0;
            for(int k = 1; k < n; k++)
                if(s->peak[k] > s->peak[s->peak_idx]) s->peak_idx = k;

            if(s->auto_advance) {
                /* Mirror FOBscan's ranged-sweep rule: linger on a frequency that
                   shows a signal above the Advanced Settings squelch, then step
                   on once the dwell expires. */
                bool hit = r > app->adv.squelch_dbm;
                if(hit) {
                    s->linger = SWEEP_LINGER;
                } else if(s->linger > 0) {
                    s->linger--;
                } else {
                    s->sel_idx = (s->sel_idx + 1) % n;
                    sweep_tune(app);
                }
            }
            fobsweep_redraw(app);
            consumed = true;
        }
    }
    return consumed;
}

void flipper_scene_fobsweep_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_capture_stop(app->capture);
    app->capture->on_edge = NULL;
    flipper_app_gui_radio_release(app);
}
