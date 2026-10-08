#include "../flipper_fobscan_app.h"
#include "flipper_fobscan_display.h"
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

/* I share FOBSCAN_FREQS[] with Advanced Settings so both screens use the same channel table. Scanning starts when I open this screen and runs until I leave it; OK does not pause. I set modulation in Advanced Settings. I use Up/Down to tune the frequency, Right to open the last saved capture, and Left to open range setup. In RangeSet, I use Up/Down to position the cursor, OK to set MAX then MIN, and Left to start the sweep. The sweep pauses briefly on a signal; OK returns to Scan at the current frequency. */

/* Ticks (500 ms each) to dwell on a frequency after a hit during a sweep. */
#define FOBSCAN_SWEEP_LINGER 4

/* Ticks (500 ms each) to hold the on-screen "SIGNAL CAPTURED" banner. */
#define FOBSCAN_FLASH_TICKS 4

void flipper_fobscan_view_init(FlipperApp* app) {
    view_allocate_model(
        app->fobscan_view, ViewModelTypeLocking, sizeof(FlipperFobscanDisplay));
    FlipperFobscanDisplay* model = view_get_model(app->fobscan_view);
    memset(model, 0, sizeof(*model));
    view_commit_model(app->fobscan_view, false);
}

static float fobscan_display_freq(int index) {
    return index >= 0 && index < FOBSCAN_FREQ_COUNT ? FOBSCAN_FREQS[index] : -1.0f;
}

/* App-thread publication only. SDK view_draw holds the same model lock while the GUI consumes owned text; it never dereferences live app/decode state. */
static void fobscan_redraw(FlipperApp* app) {
    const FlipperFobscanState* s = &app->fobscan;
    FlipperFobscanDisplayInput input = {
        .mode = s->ui_mode,
        .range_step = s->range_step,
        .preset = s->preset,
        .squelch = app->adv.squelch_dbm,
        .freq_mhz = s->freq_mhz,
        .cursor_mhz = fobscan_display_freq(s->cursor_idx),
        .range_min_mhz = fobscan_display_freq(s->range_min_idx),
        .range_max_mhz = fobscan_display_freq(s->range_max_idx),
        .rssi_dbm = s->rssi_dbm,
        .capture_count = s->cap_count,
        .decode_valid = s->last_decode_valid,
        .flash = s->flash_ticks > 0,
        .flash_decoded = s->flash_decoded,
        .decode = &s->last_decode,
    };
    FlipperFobscanDisplay* model = view_get_model(app->fobscan_view);
    flipper_fobscan_display_build(model, &input);
    view_commit_model(app->fobscan_view, true);
}

static int fobscan_clamp_idx(int i) {
    if(i < 0) i = 0;
    if(i >= FOBSCAN_FREQ_COUNT) i = FOBSCAN_FREQ_COUNT - 1;
    return i;
}

/* Stop the radio, apply the current frequency and preset, then optionally restart it. Read squelch and force-protocol from Advanced Settings each time so changes take effect immediately. */
static void fobscan_retune(FlipperApp* app, bool arm) {
    FlipperFobscanState* s = &app->fobscan;
    flipper_capture_stop(app->capture);
    app->capture->freq_mhz    = s->freq_mhz;
    app->capture->preset      = s->preset;
    app->capture->squelch_dbm = app->adv.squelch_dbm;
    app->capture->force_proto = app->adv.force_proto;
    if(arm) flipper_capture_start(app->capture);
    notification_message(app->notifications, &sequence_blink_blue_100);
}

/* ── Canvas draw ─────────────────────────────────────────────────────────── */
#ifdef FOBSCAN_STARTUP_PROBE
static void fobscan_probe_ignore(void* ctx, uint32_t index) {
    UNUSED(ctx);
    UNUSED(index);
}
#endif

void flipper_fobscan_draw_cb(Canvas* canvas, void* model) {
    const FlipperFobscanDisplay* display = model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, display->header);
    canvas_set_font(canvas, FontSecondary);
    if(display->badge_visible)
        canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, display->badge);
    if(display->divider) canvas_draw_line(canvas, 0, 35, 127, 35);
    for(unsigned i = 0; i < 5; ++i)
        if(display->rows[i][0]) canvas_draw_str(canvas, 0, display->y[i], display->rows[i]);
    if(display->highlight) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, 128, 13);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 10,
                        display->decoded_highlight ? "SIGNAL CAPTURED" : "BURST CAPTURED");
        /* Restore normal draw color for anything after this. */
        canvas_set_color(canvas, ColorBlack);
    }
}

/* ── Input ───────────────────────────────────────────────────────────────── */
bool flipper_fobscan_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
#ifdef FOBSCAN_STARTUP_PROBE
    /* Back is handled by the dispatcher. Do not tune, save, sweep or jump to other utilities during an isolation test. */
    UNUSED(e);
    UNUSED(app);
    return false;
#endif
    FlipperFobscanState* s = &app->fobscan;

    if(e->type != InputTypeShort && e->type != InputTypeRepeat) return false;

    /* ── Range-set mode: cursor picking ──────────────────────────────────── */
    if(s->ui_mode == FobscanModeRangeSet) {
        switch(e->key) {
        case InputKeyUp:
            s->cursor_idx = fobscan_clamp_idx(s->cursor_idx + 1);
            fobscan_redraw(app);
            return true;
        case InputKeyDown:
            s->cursor_idx = fobscan_clamp_idx(s->cursor_idx - 1);
            fobscan_redraw(app);
            return true;
        case InputKeyOk:
            if(e->type != InputTypeShort) return false;
            if(s->range_step == 0) {
                s->range_max_idx = s->cursor_idx;
                s->range_step = 1;
            } else if(s->range_step == 1) {
                s->range_min_idx = s->cursor_idx;
                s->range_step = 2;
            }
            notification_message(app->notifications, &sequence_success);
            fobscan_redraw(app);
            return true;
        case InputKeyLeft:
            /* Confirm only after both bounds are set; otherwise return to Scan. */
            view_dispatcher_send_custom_event(app->view_dispatcher,
                s->range_step == 2 ? FlipperEventRangeConfirm : FlipperEventRangeCancel);
            return true;
        default:
            return false;
        }
    }

    /* ── Sweep mode ──────────────────────────────────────────────────────── */
    if(s->ui_mode == FobscanModeSweep) {
        switch(e->key) {
        case InputKeyOk:
            if(e->type != InputTypeShort) return false;
            /* Stop on the current frequency and return to Scan. */
            view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventSweepStop);
            return true;
        case InputKeyRight:
            if(app->fobscan_has_saved)
                view_dispatcher_send_custom_event(app->view_dispatcher,
                                                  FlipperEventViewInLibrary);
            return true;
        default:
            return false;   /* Up/Down/Left ignored while sweeping */
        }
    }

    /* ── Normal single-frequency Scan mode ───────────────────────────────── */
    switch(e->key) {
    case InputKeyUp:
        s->freq_idx = fobscan_clamp_idx(s->freq_idx + 1);
        s->freq_mhz = FOBSCAN_FREQS[s->freq_idx];
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventRetune);
        return true;

    case InputKeyDown:
        s->freq_idx = fobscan_clamp_idx(s->freq_idx - 1);
        s->freq_mhz = FOBSCAN_FREQS[s->freq_idx];
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventRetune);
        return true;

    case InputKeyRight:
        /* Open the most recently auto-saved capture. */
        if(app->fobscan_has_saved)
            view_dispatcher_send_custom_event(app->view_dispatcher,
                                              FlipperEventViewInLibrary);
        return true;

    case InputKeyLeft:
        /* Enter custom-range setup. */
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventRangeSetup);
        return true;

    default:
        return false;   /* OK does nothing in Scan mode */
    }
}

/* ── Capture edge callback (ISR — must be minimal) ──────────────────────── */
/* Send a status tick for each RF edge, but keep the ISR away from radio or HAL state changes. It can fire more than 100 times per press; changing modes here could call the SubGHz state machine repeatedly and trigger a furi_check. The FOBscan handler flushes the capture ring on every event, so the screen still updates promptly. */
static void fobscan_edge_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventStatusTick);
}

/* ── Capture flush → decode/save; returns true if a burst was accepted ───── */
static bool fobscan_consume_flush(FlipperApp* app) {
    FlipperFobscanState* s = &app->fobscan;
    if(!flipper_capture_flush(app->capture)) return false;

    FlipperCaptureResult* cr = &app->capture->result;

    /* RSSI squelch gate (the Advanced Settings "Squelch" value).  The plain flush path does not gate on RSSI, so I enforce it here — this is what makes the setting actually take effect in FOBscan. */
    s->rssi_dbm = flipper_capture_rssi(app->capture);
    if(s->rssi_dbm <= app->adv.squelch_dbm) return false;

    bool accepted = false;

    if(cr->decode_ok) {
        s->last_decode       = cr->decode;
        s->last_decode_valid = true;
        s->cap_count++;
        s->flash_ticks       = FOBSCAN_FLASH_TICKS;
        s->flash_decoded     = true;
        accepted = true;

        furi_mutex_acquire(app->library_mutex, FuriWaitForever);
        int slot = app->library_count % FLIPPER_SIGNAL_LIB_MAX;
        app->library[slot] = cr->decode;
        if(app->library_count < FLIPPER_SIGNAL_LIB_MAX) app->library_count++;
        furi_mutex_release(app->library_mutex);

        /* Stream the on-device decode to any connected dashboard. */
        char sbuf[384];
        size_t slen = flipper_proto_emit_signal(
            sbuf, sizeof(sbuf), &cr->decode, s->rssi_dbm);
        flipper_app_broadcast(app, sbuf, slen);

        /* Auto-save the decoded capture (with pulses) to the SD library, remembering its name so Right/OK-long can jump straight to it. */
        if(app->adv.autosave_decoded) {
            if(flipper_lib_save(app->storage, cr, s->preset, true,
                                app->adv.lib_evict_oldest, app->fobscan_last_saved)) {
                app->fobscan_last_saved_decoded = true;
                app->fobscan_has_saved = true;
            }
        }

        notification_message(app->notifications, &sequence_success);
    } else if(app->adv.autosave_raw && cr->pulses.len >= 16) {
        /* Undecoded burst — save raw for later inspection/replay. */
        if(flipper_lib_save(app->storage, cr, s->preset, false,
                            app->adv.lib_evict_oldest, app->fobscan_last_saved)) {
            app->fobscan_last_saved_decoded = false;
            app->fobscan_has_saved = true;
        }
        s->cap_count++;
        s->flash_ticks   = FOBSCAN_FLASH_TICKS;
        s->flash_decoded = false;
        accepted = true;
        notification_message(app->notifications, &sequence_blink_magenta_100);
    }

    return accepted;
}

/* ── Scene lifecycle ──────────────────────────────────────────────────────── */
void flipper_scene_fobscan_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobscanState* s = &app->fobscan;

    /* Seed on-device tuning from Advanced Settings. */
    if(app->adv.freq_idx < 0 || app->adv.freq_idx >= FOBSCAN_FREQ_COUNT)
        app->adv.freq_idx = FOBSCAN_FREQ_DEFAULT;

    s->ui_mode           = FobscanModeScan;
    s->scanning          = true;
    s->cap_count         = 0;
    s->last_decode_valid = false;
    s->flash_ticks       = 0;
    s->flash_decoded     = false;
    s->freq_idx          = app->adv.freq_idx;
    s->freq_mhz          = FOBSCAN_FREQS[s->freq_idx];
    s->preset            = app->adv.preset;
    s->rssi_dbm          = -100.0f;
    s->range_step        = 0;
    s->cursor_idx        = s->freq_idx;
    s->range_min_idx     = 0;
    s->range_max_idx     = FOBSCAN_FREQ_COUNT - 1;
    s->sweep_idx         = 0;
    s->linger            = 0;
    /* Publish initialized state before the first draw, not at the first tick. */
    fobscan_redraw(app);

#ifdef FOBSCAN_STARTUP_PROBE
    /* Runtime only: a probe must not create captures or write settings. No persistent setting is changed, including on scene exit. */
    app->adv.autosave_decoded = false;
    app->adv.autosave_raw = false;
    if(s->probe_mode == FobscanProbeDisplayOnly) {
        s->scanning = false;
        app->capture->on_edge = NULL;
        app->capture->on_edge_ctx = NULL;
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobscan);
        return;
    }
#endif

    /* Switch to my view FIRST, before touching the radio.  Starting subghz async RX (HW timer + DMA + high-rate ISR) before the view is active is my suspected cause of the switch fault — I do the GUI switch while the hardware is still idle, following the conventional Flipper ordering. */
    app->capture->on_edge      = fobscan_edge_cb;
    app->capture->on_edge_ctx  = app;

#ifdef FOBSCAN_STARTUP_PROBE
    if(s->probe_mode == FobscanProbeRxOnly) {
        /* The same receive/decoder event path, with the SDK's standard Submenu instead of the custom FOBscan draw/input callbacks. */
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "RX-only probe active");
        submenu_add_item(app->submenu, "Listening. Back exits.", 0,
                         fobscan_probe_ignore, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
    } else
#endif
    {
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobscan);
    }

    app->capture->freq_mhz     = s->freq_mhz;
    app->capture->preset       = s->preset;
    app->capture->squelch_dbm  = app->adv.squelch_dbm;
    app->capture->force_proto  = app->adv.force_proto;
    /* Claim the radio (stops any remote scan, publishes ownership) BEFORE I touch the subghz HAL, so the RX thread can't drive it concurrently. */
    flipper_app_gui_radio_acquire(app);

    flipper_capture_start(app->capture);

    notification_message(app->notifications, &sequence_blink_blue_100);
}

bool flipper_scene_fobscan_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobscanState* s = &app->fobscan;
    bool consumed = false;

#ifdef FOBSCAN_STARTUP_PROBE
    if(s->probe_mode == FobscanProbeDisplayOnly) return false;
#endif
    if(e.type != SceneManagerEventTypeCustom) return false;

    /* ── Enter custom-range setup ────────────────────────────────────────── */
    if(e.event == FlipperEventRangeSetup) {
        flipper_capture_stop(app->capture);
        s->ui_mode    = FobscanModeRangeSet;
        s->range_step = 0;
        s->cursor_idx = s->freq_idx;
        fobscan_redraw(app);
        return true;
    }

    /* ── Cancel range setup → back to single-frequency Scan ───────────────── */
    if(e.event == FlipperEventRangeCancel) {
        s->ui_mode = FobscanModeScan;
        fobscan_retune(app, true);
        fobscan_redraw(app);
        return true;
    }

    /* ── Confirm range → start the ranged sweep ──────────────────────────── */
    if(e.event == FlipperEventRangeConfirm) {
        /* Normalise so min <= max. */
        if(s->range_min_idx > s->range_max_idx) {
            int t = s->range_min_idx;
            s->range_min_idx = s->range_max_idx;
            s->range_max_idx = t;
        }
        s->ui_mode   = FobscanModeSweep;
        s->sweep_idx = s->range_min_idx;
        s->linger    = 0;
        s->freq_idx  = s->sweep_idx;
        s->freq_mhz  = FOBSCAN_FREQS[s->freq_idx];
        fobscan_retune(app, true);
        fobscan_redraw(app);
        return true;
    }

    /* ── Stop sweep → stay on the landed frequency in Scan mode ───────────── */
    if(e.event == FlipperEventSweepStop) {
        s->ui_mode  = FobscanModeScan;
        s->freq_idx = fobscan_clamp_idx(s->sweep_idx);
        s->freq_mhz = FOBSCAN_FREQS[s->freq_idx];
        fobscan_retune(app, true);
        fobscan_redraw(app);
        return true;
    }

    /* ── Re-tune (Up/Down in Scan mode) ──────────────────────────────────── */
    if(e.event == FlipperEventRetune) {
        fobscan_retune(app, s->scanning);
        /* Keep Advanced Settings in sync with on-device tuning. */
        app->adv.freq_idx = s->freq_idx;
        fobscan_redraw(app);
        return true;
    }

    /* ── Jump to the last auto-saved capture in the Library ───────────────── */
    if(e.event == FlipperEventViewInLibrary) {
        if(app->fobscan_has_saved &&
           flipper_lib_load_with_protocol(
               app->storage, app->fobscan_last_saved_decoded,
               app->fobscan_last_saved, &app->lib_sel,
               &app->lib_sel_preset, app->lib_sel_protocol,
               sizeof(app->lib_sel_protocol))) {
            strncpy(app->lib_sel_name, app->fobscan_last_saved,
                    FLIPPER_LIB_NAME_MAX - 1);
            app->lib_sel_name[FLIPPER_LIB_NAME_MAX - 1] = '\0';
            app->lib_sel_decoded = app->fobscan_last_saved_decoded;
            app->lib_browse_raw  = !app->fobscan_last_saved_decoded;
            flipper_capture_stop(app->capture);
            s->scanning = false;
            scene_manager_next_scene(app->scene_manager, FlipperSceneLibraryItem);
        } else {
            notification_message(app->notifications, &sequence_error);
        }
        return true;
    }

    /* ── Status tick / edge flush ────────────────────────────────────────── */
    if(e.event == FlipperEventStatusTick) {
        /* Range setup has no active capture to process. */
        if(s->ui_mode == FobscanModeRangeSet) return true;

        bool hit = fobscan_consume_flush(app);

        if(s->ui_mode == FobscanModeSweep) {
            /* Pause briefly on a hit, then move to the next selected frequency. */
            if(hit) {
                s->linger = FOBSCAN_SWEEP_LINGER;
            } else if(s->linger > 0) {
                s->linger--;
            } else {
                s->sweep_idx++;
                if(s->sweep_idx > s->range_max_idx) s->sweep_idx = s->range_min_idx;
                s->freq_idx = s->sweep_idx;
                s->freq_mhz = FOBSCAN_FREQS[s->freq_idx];
                fobscan_retune(app, true);
            }
        }

        /* Count down the on-screen capture toast (500 ms per tick). */
        if(s->flash_ticks > 0) s->flash_ticks--;

        /* Refresh the live RSSI reading for the meter/header. */
        s->rssi_dbm = flipper_capture_rssi(app->capture);
        fobscan_redraw(app);
        consumed = true;
    }

    /* ViewDispatcher redraws the current view automatically after event. */
    return consumed;
}

void flipper_scene_fobscan_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_capture_stop(app->capture);
    app->capture->on_edge     = NULL;
    app->capture->on_edge_ctx = NULL;
    app->fobscan.scanning = false;
#ifndef FOBSCAN_STARTUP_PROBE
    flipper_adv_settings_save(app);       /* persist on-device tuning changes */
#endif
    flipper_app_gui_radio_release(app);   /* release the CC1101 for remote control */
}
