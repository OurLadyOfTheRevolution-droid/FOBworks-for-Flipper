#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define RX_TOOL_FLOOR_DBM (-100.0f)
#define RX_TOOL_CEIL_DBM  (-30.0f)

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
    if(state->kind != FlipperRxToolWatch) return true;

    /* Keep the same configured RSSI gate and library policy as FOBscan. */
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
    app->capture->on_edge = rx_edge_cb;
    app->capture->on_edge_ctx = app;

    /* Activate the canvas before touching CC1101 and claim radio ownership
       before starting RX, matching FOBscan's safe lifecycle ordering. */
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
    /* Default to the common 433 MHz band; bounds are selectable only from
       the shared, CC1101-valid FOBscan frequency table. */
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
            canvas_draw_str(canvas, 0, 42, "Listening / decoding");
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

void flipper_scene_fobhunt_on_enter(void* ctx) {
    rx_hunt_enter((FlipperApp*)ctx);
}
bool flipper_scene_fobhunt_on_event(void* ctx, SceneManagerEvent event) {
    return rx_scene_event((FlipperApp*)ctx, event);
}
void flipper_scene_fobhunt_on_exit(void* ctx) {
    rx_scene_exit((FlipperApp*)ctx);
}