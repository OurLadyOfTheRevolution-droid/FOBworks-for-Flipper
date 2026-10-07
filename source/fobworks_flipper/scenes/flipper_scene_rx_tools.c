#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define RX_TOOL_FLOOR_DBM (-100.0f)
#define RX_TOOL_CEIL_DBM  (-30.0f)
/* Timing comparison only, at the TE estimator's 32-us bin resolution. */
#define RX_GROLLBACK_MASK    0xFFFFu
#define RX_GROLLBACK_MIN_SEQ 3
#define RX_GROLLBACK_MAX_DELTA 4u
#define RX_GROLLBACK_FREQ_TOL 0.25f

static int rx_freq_count(void) {
    int count = FOBSCAN_FREQ_COUNT;
    if(count > FOBSCAN_FREQ_MAX) count = FOBSCAN_FREQ_MAX;
    return count > 0 ? count : 1;
}

static int rx_clamp_index(int index) {
    int count = rx_freq_count();
    if(index < 0) return 0;
    if(index >= count) return count - 1;
    return index;
}

static void rx_redraw(FlipperApp* app) {
    view_get_model(app->rx_tool_view);
    view_commit_model(app->rx_tool_view, true);
}

static void rx_edge_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventStatusTick);
}

static void rx_tune(FlipperApp* app, float freq_mhz) {
    flipper_capture_stop(app->capture);
    app->capture->freq_mhz = freq_mhz;
    app->capture->preset = app->adv.preset;
    app->capture->squelch_dbm = app->adv.squelch_dbm;
    app->capture->force_proto = app->adv.force_proto;
    if(app->rx_tool.kind == FlipperRxToolTrack)
        app->capture->force_proto = app->rx_tool.lab.track.tpms_mode ?
                                       FlipperForceTpms : FlipperForceAuto;
    flipper_capture_start(app->capture);
}

static void rx_update_metrics(FlipperRxToolState* state, const FlipperCaptureResult* result) {
    const FlipperPulseBuf* pulses = &result->pulses;
    state->pulse_count = pulses->len;
    state->te_us = pulses->te_us;
    state->decode_valid = result->decode_ok;
    if(result->decode_ok) state->decode = result->decode;

    if(pulses->len <= 0) {
        state->min_us = 0;
        state->max_us = 0;
        state->mean_us = 0;
        return;
    }

    uint32_t min = UINT32_MAX;
    uint32_t max = 0;
    uint64_t total = 0;
    for(int i = 0; i < pulses->len; i++) {
        uint32_t duration = pulses->durations[i];
        if(duration < min) min = duration;
        if(duration > max) max = duration;
        total += duration;
    }
    state->min_us = min;
    state->max_us = max;
    state->mean_us = (uint32_t)(total / (uint32_t)pulses->len);
}

static bool rx_consume_capture(FlipperApp* app) {
    FlipperRxToolState* state = &app->rx_tool;
    if(!flipper_capture_flush(app->capture)) return false;

    FlipperCaptureResult* capture = &app->capture->result;
    state->rssi_dbm = flipper_capture_rssi(app->capture);
    rx_update_metrics(state, capture);

    /* FOBreport: feed every decode into the read-only health grade. A press
       that belongs to a different remote is ignored and the run keeps waiting
       for FLIPPER_FOBREPORT_MAX_PRESSES of the same fob. */
    if(state->kind == FlipperRxToolReport) {
        if(capture->decode_ok) {
            if(fobreport_add(&state->lab.report, &capture->decode) == false) {
                /* A different serial interrupted the run; restart grading. */
                fobreport_reset(&state->lab.report);
                fobreport_add(&state->lab.report, &capture->decode);
            }
            if(state->lab.report.presses >= 4)
                fobreport_finalize(&state->lab.report);
        }
        return true;
    }

    if(state->kind == FlipperRxToolFreq) {
        if(!capture->decode_ok) return true;
        FlipperRxFreqState* fq = &state->lab.freq;
        if(fq->proto[0] &&
           strncmp(fq->proto, capture->decode.proto, sizeof(fq->proto) - 1) != 0)
            return true; /* require one protocol for A vs B */
        if(!fq->proto[0]) {
            strncpy(fq->proto, capture->decode.proto, sizeof(fq->proto) - 1);
            fq->proto[sizeof(fq->proto) - 1] = '\0';
        }
        FobTimingProfile* p = fq->active ? &fq->b : &fq->a;
        fobfreq_add(p, (float)capture->pulses.te_us);
        fobfreq_finalize(p);
        if(fq->a.count >= FOBFREQ_SAMPLES_MIN && fq->b.count >= FOBFREQ_SAMPLES_MIN) {
            fq->compared = true;
            fq->delta_us = fobfreq_mean_delta(&fq->a, &fq->b);
            fq->similar_timing = fobfreq_similar_timing(
                &fq->a, &fq->b, FOBFREQ_TIMING_TOL_US);
        }
        return true;
    }

    if(state->kind == FlipperRxToolTrack) {
        if(!capture->decode_ok) return true;
        FlipperRxTrackState* tr = &state->lab.track;
        uint32_t now = furi_get_tick();
        uint32_t ts = now - tr->start_ms;
        /* Manual receive modes; the structural TPMS parser stays force-only.
           Co-occurrence is not proof that two identifiers share a vehicle. */
        bool is_tpms = (strncmp(capture->decode.proto, "TPMS", 4) == 0);
        if(tr->tpms_mode ? !is_tpms : (is_tpms || !capture->decode.rolling))
            return true;
        fobtrack_record(
            &tr->log,
            is_tpms ? FobtrackTpms : FobtrackRke,
            capture->decode.addr,
            ts);
        tr->link_count = fobtrack_correlate(
            &tr->log, tr->links, FLIPPER_RX_TRACK_LINKS);
        return true;
    }

    if(state->kind == FlipperRxToolGrollback) {
        if(!capture->decode_ok || !capture->decode.rolling) return true;
        FlipperRxGrollbackState* gr = &state->lab.grollback;
        if(gr->count > 0 &&
           gr->frames[0].addr != capture->decode.addr) {
            /* New serial — restart the run. */
            gr->count = 0;
            memset(&gr->plan, 0, sizeof(gr->plan));
        }
        if(gr->count < GROLLBACK_MAX_CAPS) {
            gr->frames[gr->count++] = capture->decode;
            grollback_analyze_results(
                gr->frames, gr->count, RX_GROLLBACK_MASK,
                RX_GROLLBACK_MIN_SEQ, RX_GROLLBACK_MAX_DELTA,
                RX_GROLLBACK_FREQ_TOL, &gr->plan);
        }
        return true;
    }

    if(state->kind != FlipperRxToolWatch) return true;

    /* Use FOBscan's RSSI threshold and library save settings. */
    if(state->rssi_dbm <= app->adv.squelch_dbm) return true;
    state->captures++;

    bool should_save = capture->decode_ok ? app->adv.autosave_decoded :
                       (app->adv.autosave_raw && capture->pulses.len >= 16);
    if(should_save) {
        char saved_name[FLIPPER_LIB_NAME_MAX];
        bool saved = flipper_lib_save(
            app->storage, capture, app->adv.preset, capture->decode_ok,
            app->adv.lib_evict_oldest, saved_name);
        if(saved) {
            state->saved++;
            state->save_failed = false;
        } else {
            state->save_failed = true;
        }
    }
    return true;
}

static void rx_watch_enter(FlipperApp* app, FlipperRxToolKind kind) {
    FlipperRxToolState* state = &app->rx_tool;
    memset(state, 0, sizeof(*state));
    state->kind = kind;
    state->rssi_dbm = RX_TOOL_FLOOR_DBM;
    state->peak_idx = -1;
    if(kind == FlipperRxToolReport) {
        fobreport_reset(&state->lab.report);
    } else if(kind == FlipperRxToolFreq) {
        fobfreq_reset(&state->lab.freq.a);
        fobfreq_reset(&state->lab.freq.b);
        state->lab.freq.active = 0;
    } else if(kind == FlipperRxToolTrack) {
        fobtrack_reset(&state->lab.track.log);
        state->lab.track.start_ms = furi_get_tick();
    } else if(kind == FlipperRxToolGrollback) {
        memset(&state->lab.grollback, 0, sizeof(state->lab.grollback));
    }
    app->capture->on_edge = rx_edge_cb;
    app->capture->on_edge_ctx = app;

    /* Show the canvas and claim the radio before starting RX, as FOBscan does. */
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewRxTool);
    flipper_app_gui_radio_acquire(app);
    rx_tune(app, FOBSCAN_FREQS[rx_clamp_index(app->adv.freq_idx)]);
    state->running = true;
    notification_message(app->notifications, &sequence_blink_blue_100);
}

static void rx_hunt_enter(FlipperApp* app) {
    FlipperRxToolState* state = &app->rx_tool;
    memset(state, 0, sizeof(*state));
    state->kind = FlipperRxToolHunt;
    state->rssi_dbm = RX_TOOL_FLOOR_DBM;
    /* Start in the common 433 MHz range. Bounds come from FOBscan's shared
       table of CC1101 frequencies. */
    state->low_idx = rx_clamp_index(8);
    state->high_idx = rx_clamp_index(10);
    state->cursor_idx = state->low_idx;
    state->peak_idx = state->low_idx;
    for(int i = 0; i < FOBSCAN_FREQ_MAX; i++) {
        state->hunt_rssi[i] = RX_TOOL_FLOOR_DBM;
        state->hunt_peak[i] = RX_TOOL_FLOOR_DBM;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewRxTool);
    flipper_app_gui_radio_acquire(app);
    rx_tune(app, FOBSCAN_FREQS[state->low_idx]);
    state->running = false;
}

static void rx_start_hunt(FlipperApp* app) {
    FlipperRxToolState* state = &app->rx_tool;
    state->sweep_idx = state->low_idx;
    state->peak_idx = state->low_idx;
    state->rssi_dbm = RX_TOOL_FLOOR_DBM;
    for(int i = 0; i < FOBSCAN_FREQ_MAX; i++) {
        state->hunt_rssi[i] = RX_TOOL_FLOOR_DBM;
        state->hunt_peak[i] = RX_TOOL_FLOOR_DBM;
    }
    state->running = true;
    rx_tune(app, FOBSCAN_FREQS[state->sweep_idx]);
}

static void rx_hunt_tick(FlipperApp* app) {
    FlipperRxToolState* state = &app->rx_tool;
    if(!state->running) {
        state->rssi_dbm = flipper_capture_rssi(app->capture);
        return;
    }

    int index = state->sweep_idx;
    float rssi = flipper_capture_rssi(app->capture);
    state->rssi_dbm = rssi;
    state->hunt_rssi[index] = rssi;
    if(rssi > state->hunt_peak[index]) state->hunt_peak[index] = rssi;

    state->peak_idx = state->low_idx;
    for(int i = state->low_idx + 1; i <= state->high_idx; i++) {
        if(state->hunt_peak[i] > state->hunt_peak[state->peak_idx])
            state->peak_idx = i;
    }

    state->sweep_idx++;
    if(state->sweep_idx > state->high_idx) state->sweep_idx = state->low_idx;
    rx_tune(app, FOBSCAN_FREQS[state->sweep_idx]);
}

static int rx_bar_height(float dbm, int height) {
    if(dbm < RX_TOOL_FLOOR_DBM) dbm = RX_TOOL_FLOOR_DBM;
    if(dbm > RX_TOOL_CEIL_DBM) dbm = RX_TOOL_CEIL_DBM;
    return (int)(((dbm - RX_TOOL_FLOOR_DBM) /
                  (RX_TOOL_CEIL_DBM - RX_TOOL_FLOOR_DBM)) * height + 0.5f);
}

void flipper_rx_tool_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    FlipperRxToolState* state = &app->rx_tool;
    char line[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    /* ── FOBreport: rolling/fixed health grade ───────────────────────────── */
    if(state->kind == FlipperRxToolReport) {
        canvas_draw_str(canvas, 0, 10, "FOBreport RX");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "%.2f MHz  RSSI %.0f dBm",
                 (double)app->capture->freq_mhz, (double)state->rssi_dbm);
        canvas_draw_str(canvas, 0, 21, line);
        canvas_draw_line(canvas, 0, 24, 127, 24);

        const FobReportReport* rep = &state->lab.report;
        if(!rep->has_press) {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 0, 34, "Press your fob a few times.");
            canvas_draw_str(canvas, 0, 44, "Read-only: never transmits.");
            canvas_draw_str(canvas, 0, 54, "[Back] = exit");
            return;
        }

        const char* grade = "?";
        switch(rep->grade) {
        case FobReportGradeA: grade = "A"; break;
        case FobReportGradeB: grade = "B"; break;
        case FobReportGradeC: grade = "C"; break;
        case FobReportGradeD: grade = "D"; break;
        case FobReportGradeU: grade = "?"; break;
        }
        canvas_set_font(canvas, FontPrimary);
        snprintf(line, sizeof(line), "Observation %s", grade);
        canvas_draw_str(canvas, 0, 36, line);
        canvas_set_font(canvas, FontSecondary);

        if(rep->presses < 4) {
            snprintf(line, sizeof(line), "Sampling %d press(es)...",
                     rep->presses);
        } else {
            strncpy(line, rep->summary, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            if(strlen(line) > 30) line[30] = '\0';
        }
        canvas_draw_str(canvas, 0, 46, line);

        snprintf(line, sizeof(line), "sn %08lX", (unsigned long)rep->serial);
        canvas_draw_str(canvas, 0, 56, line);
        canvas_draw_str(canvas, 0, 63, "[Back] = exit");
        return;
    }

    /* ── FOBfreq: coarse pulse-timing comparison, not RF fingerprinting ───── */
    if(state->kind == FlipperRxToolFreq) {
        const FlipperRxFreqState* fq = &state->lab.freq;
        canvas_draw_str(canvas, 0, 10, "FOBfreq timing");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "%.2f MHz  profile %c",
                 (double)app->capture->freq_mhz, fq->active ? 'B' : 'A');
        canvas_draw_str(canvas, 0, 21, line);
        canvas_draw_line(canvas, 0, 24, 127, 24);
        snprintf(line, sizeof(line), "A n=%d mean %.1f us",
                 fq->a.count, (double)fq->a.mean_us);
        canvas_draw_str(canvas, 0, 34, line);
        snprintf(line, sizeof(line), "B n=%d mean %.1f us",
                 fq->b.count, (double)fq->b.mean_us);
        canvas_draw_str(canvas, 0, 44, line);
        if(fq->compared) {
            snprintf(line, sizeof(line), "%s d=%.0f us",
                     fq->similar_timing ? "Similar timing" : "Different timing",
                     (double)fq->delta_us);
        } else {
            snprintf(line, sizeof(line), "L=A/B  need 4 each");
        }
        canvas_draw_str(canvas, 0, 54, line);
        canvas_draw_str(canvas, 0, 63, "32us bins; not identity");
        return;
    }

    /* ── FOBtrack: TPMS↔RKE co-occurrence ────────────────────────────────── */
    if(state->kind == FlipperRxToolTrack) {
        const FlipperRxTrackState* tr = &state->lab.track;
        canvas_draw_str(canvas, 0, 10, "FOBtrack RX");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "%s window %d/64",
                 tr->tpms_mode ? "TPMS*" : "RKE", tr->log.count);
        canvas_draw_str(canvas, 0, 21, line);
        canvas_draw_line(canvas, 0, 24, 127, 24);
        if(tr->link_count <= 0) {
            canvas_draw_str(canvas, 0, 36, "No co-occurrences yet.");
            canvas_draw_str(canvas, 0, 46, "Left: RKE/TPMS*, same freq");
            canvas_draw_str(canvas, 0, 56, "*Structural; not vehicle ID");
        } else {
            for(int i = 0; i < tr->link_count && i < 3; i++) {
                snprintf(line, sizeof(line), "T%08lX R%08lX x%d",
                         (unsigned long)tr->links[i].tpms_id,
                         (unsigned long)tr->links[i].rke_serial,
                         tr->links[i].score);
                canvas_draw_str(canvas, 0, 36 + i * 10, line);
            }
        }
        canvas_draw_str(canvas, 0, 63, "L:mode  OK:reset  Back:exit");
        return;
    }

    /* ── FOBroll: generalized rollback candidate analyzer ────────────────── */
    if(state->kind == FlipperRxToolGrollback) {
        const FlipperRxGrollbackState* gr = &state->lab.grollback;
        canvas_draw_str(canvas, 0, 10, "FOBroll RX");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "%.2f MHz  caps %d/%d",
                 (double)app->capture->freq_mhz, gr->count, GROLLBACK_MAX_CAPS);
        canvas_draw_str(canvas, 0, 21, line);
        canvas_draw_line(canvas, 0, 24, 127, 24);
        if(gr->count <= 0) {
            canvas_draw_str(canvas, 0, 36, "Press rolling fob 3+ times.");
            canvas_draw_str(canvas, 0, 46, "Analyzes counters only.");
            canvas_draw_str(canvas, 0, 56, "Read-only: never transmits.");
        } else {
            snprintf(line, sizeof(line), "%s",
                     gr->plan.candidate ? "CANDIDATE" : "collecting");
            canvas_draw_str(canvas, 0, 36, line);
            strncpy(line, gr->plan.note, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            if(strlen(line) > 30) line[30] = '\0';
            canvas_draw_str(canvas, 0, 46, line);
            if(gr->count > 0) {
                snprintf(line, sizeof(line), "sn %08lX  span %lu",
                         (unsigned long)gr->frames[0].addr,
                         (unsigned long)gr->plan.span);
                canvas_draw_str(canvas, 0, 56, line);
            }
        }
        canvas_draw_str(canvas, 0, 63, "[Back] = exit");
        return;
    }

    if(state->kind == FlipperRxToolHunt) {
        canvas_draw_str(canvas, 0, 10, "FOBhunt RSSI");
        canvas_set_font(canvas, FontSecondary);
        snprintf(line, sizeof(line), "%s %.2f-%.2f MHz",
                 state->running ? "RUN" : "STOP",
                 (double)FOBSCAN_FREQS[state->low_idx],
                 (double)FOBSCAN_FREQS[state->high_idx]);
        canvas_draw_str(canvas, 0, 21, line);
        snprintf(line, sizeof(line), "%.2f MHz  %.0f dBm",
                 (double)FOBSCAN_FREQS[state->sweep_idx], (double)state->rssi_dbm);
        canvas_draw_str(canvas, 0, 32, line);
        if(state->hunt_peak[state->peak_idx] > RX_TOOL_FLOOR_DBM) {
            snprintf(line, sizeof(line), "Peak %.2f MHz %.0f dBm",
                     (double)FOBSCAN_FREQS[state->peak_idx],
                     (double)state->hunt_peak[state->peak_idx]);
        } else {
            snprintf(line, sizeof(line), "Peak --");
        }
        canvas_draw_str(canvas, 0, 42, line);

        int range_count = state->high_idx - state->low_idx + 1;
        int bar_width = 122 / range_count;
        if(bar_width < 2) bar_width = 2;
        for(int i = state->low_idx; i <= state->high_idx; i++) {
            int x = 2 + (i - state->low_idx) * bar_width;
            int height = rx_bar_height(state->hunt_peak[i], 14);
            if(height > 0) canvas_draw_box(canvas, x, 62 - height, bar_width - 1, height);
            if(i == state->peak_idx && state->hunt_peak[i] > RX_TOOL_FLOOR_DBM)
                canvas_draw_box(canvas, x, 46, bar_width - 1, 2);
        }
        if(state->running) {
            snprintf(line, sizeof(line), "OK stops sweep");
        } else {
            snprintf(line, sizeof(line), "%s L=bound U/D edit OK run",
                     state->edit_high ? "HIGH" : "LOW");
        }
        canvas_draw_str(canvas, 0, 63, line);
        return;
    }

    canvas_draw_str(canvas, 0, 10,
                    state->kind == FlipperRxToolWatch ? "FOBwatch RX" : "FOBlabs RX");
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "%.2f MHz  RSSI %.0f dBm",
             (double)app->capture->freq_mhz, (double)state->rssi_dbm);
    canvas_draw_str(canvas, 0, 21, line);

    if(state->kind == FlipperRxToolWatch) {
        snprintf(line, sizeof(line), "Captures %lu  Saved %lu",
                 (unsigned long)state->captures, (unsigned long)state->saved);
        canvas_draw_str(canvas, 0, 32, line);
        if(state->save_failed) {
            snprintf(line, sizeof(line), "Save: %.28s",
                     flipper_lib_error_name(flipper_lib_last_error()));
            canvas_draw_str(canvas, 0, 42, line);
        } else if(state->decode_valid) {
            snprintf(line, sizeof(line), "%.22s  %08lX",
                     state->decode.proto, (unsigned long)state->decode.addr);
            canvas_draw_str(canvas, 0, 42, line);
        } else {
            canvas_draw_str(canvas, 0, 42, "Listening and decoding");
        }
    } else if(state->decode_valid) {
        snprintf(line, sizeof(line), "%.22s %d bits TE %luus",
                 state->decode.proto, state->decode.bits,
                 (unsigned long)state->te_us);
        canvas_draw_str(canvas, 0, 32, line);
        snprintf(line, sizeof(line), "Addr %08lX Cnt %lu",
                 (unsigned long)state->decode.addr, (unsigned long)state->decode.cnt);
        canvas_draw_str(canvas, 0, 42, line);
    } else {
        canvas_draw_str(canvas, 0, 32, "Waiting for capture...");
        snprintf(line, sizeof(line), "Pulses %d  TE %luus",
                 state->pulse_count, (unsigned long)state->te_us);
        canvas_draw_str(canvas, 0, 42, line);
    }

    snprintf(line, sizeof(line), "Pulses %d  min/avg/max",
             state->pulse_count);
    canvas_draw_str(canvas, 0, 53, line);
    snprintf(line, sizeof(line), "%lu/%lu/%lu us",
             (unsigned long)state->min_us, (unsigned long)state->mean_us,
             (unsigned long)state->max_us);
    canvas_draw_str(canvas, 0, 63, line);
}

bool flipper_rx_tool_input_cb(InputEvent* event, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperRxToolState* state = &app->rx_tool;

    if(state->kind == FlipperRxToolTrack && event->type == InputTypeShort) {
        if(event->key == InputKeyLeft) {
            state->lab.track.tpms_mode = !state->lab.track.tpms_mode;
            rx_tune(app, app->capture->freq_mhz);
        } else if(event->key == InputKeyOk) {
            fobtrack_reset(&state->lab.track.log);
            state->lab.track.link_count = 0;
            state->lab.track.start_ms = furi_get_tick();
        } else {
            return false;
        }
        rx_redraw(app);
        return true;
    }

    if(state->kind == FlipperRxToolFreq &&
       event->type == InputTypeShort && event->key == InputKeyLeft) {
        state->lab.freq.active = state->lab.freq.active ? 0 : 1;
        rx_redraw(app);
        return true;
    }

    if(state->kind != FlipperRxToolHunt ||
       (event->type != InputTypeShort && event->type != InputTypeRepeat))
        return false;

    switch(event->key) {
    case InputKeyLeft:
        if(event->type != InputTypeShort || state->running) return false;
        state->edit_high = !state->edit_high;
        rx_redraw(app);
        return true;
    case InputKeyUp:
    case InputKeyDown: {
        if(state->running) return false;
        int delta = event->key == InputKeyUp ? 1 : -1;
        if(state->edit_high) {
            int next = rx_clamp_index(state->high_idx + delta);
            if(next >= state->low_idx) state->high_idx = next;
        } else {
            int next = rx_clamp_index(state->low_idx + delta);
            if(next <= state->high_idx) state->low_idx = next;
        }
        state->cursor_idx = state->edit_high ? state->high_idx : state->low_idx;
        state->sweep_idx = state->low_idx;
        rx_tune(app, FOBSCAN_FREQS[state->sweep_idx]);
        rx_redraw(app);
        return true;
    }
    case InputKeyOk:
        if(event->type != InputTypeShort) return false;
        if(state->running) {
            state->running = false;
            rx_tune(app, FOBSCAN_FREQS[state->sweep_idx]);
        } else {
            rx_start_hunt(app);
        }
        notification_message(app->notifications, &sequence_success);
        rx_redraw(app);
        return true;
    default:
        return false;
    }
}

static bool rx_scene_event(FlipperApp* app, SceneManagerEvent event) {
    if(event.type != SceneManagerEventTypeCustom ||
       event.event != FlipperEventStatusTick)
        return false;

    FlipperRxToolState* state = &app->rx_tool;
    if(state->kind == FlipperRxToolHunt) {
        rx_hunt_tick(app);
    } else {
        (void)rx_consume_capture(app);
        state->rssi_dbm = flipper_capture_rssi(app->capture);
    }
    rx_redraw(app);
    return true;
}

static void rx_scene_exit(FlipperApp* app) {
    flipper_capture_stop(app->capture);
    app->capture->on_edge = NULL;
    app->capture->on_edge_ctx = NULL;
    app->rx_tool.running = false;
    flipper_app_gui_radio_release(app);
}

void flipper_scene_fobwatch_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolWatch);
}
bool flipper_scene_fobwatch_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobwatch_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_foblabs_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolLabs);
}
bool flipper_scene_foblabs_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_foblabs_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_fobreport_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolReport);
}
bool flipper_scene_fobreport_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobreport_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_fobfreq_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolFreq);
}
bool flipper_scene_fobfreq_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobfreq_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_fobtrack_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolTrack);
}
bool flipper_scene_fobtrack_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobtrack_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_grollback_on_enter(void* ctx) {
    rx_watch_enter((FlipperApp*)ctx, FlipperRxToolGrollback);
}
bool flipper_scene_grollback_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_grollback_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}

void flipper_scene_fobhunt_on_enter(void* ctx) {
    rx_hunt_enter((FlipperApp*)ctx);
}
bool flipper_scene_fobhunt_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobhunt_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}