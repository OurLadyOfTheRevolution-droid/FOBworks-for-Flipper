#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBclone guides make/model/year selection, then captures two presses before
   offering prediction and replay. All pickers reuse FlipperViewMenu. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Shared make list builder ─────────────────────────────────────────────── */
static void build_unique_makes(const char** out, int* count, int max) {
    *count = 0;
    int n = flipper_fc_vehicle_count();
    for(int i = 0; i < n && *count < max; i++) {
        const FlipperFcVehicle* v = flipper_fc_vehicle_at(i);
        if(!v || !v->make) continue;
        bool found = false;
        for(int m = 0; m < *count; m++)
            if(strcmp(out[m], v->make) == 0) { found = true; break; }
        if(!found) out[(*count)++] = v->make;
    }
}

/* Full catalog has ~30 unique makes; keep a little headroom. */
static const char* s_makes[32];
static int         s_make_count = 0;

/* Keep the large pulse buffers lazy. Release old buffers before clearing state. */
static void fobclone_caps_free(FlipperFobcloneState* fc) {
    for(int i = 0; i < 2; i++) {
        if(fc->cap[i]) {
            free(fc->cap[i]);
            fc->cap[i] = NULL;
        }
    }
    fc->cap_count = 0;
    fc->replay_ready = false;
}

/* ── Make picker ─────────────────────────────────────────────────────────── */
static void fobclone_make_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= s_make_count) return;
    app->guided->fobclone.make_idx  = (int)idx;
    app->guided->fobclone.model_idx = -1;
    app->guided->fobclone.year_idx  = -1;
    app->guided->fobclone.profile   = NULL;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobcloneModel);
}

void flipper_scene_fobclone_make_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Allocate full capture buffers only after the pickers close and a decoded
       press arrives. */
    if(!flipper_guided_ensure(app, sizeof(FlipperFobcloneState))) {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "FOBclone: OOM");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }
    fobclone_caps_free(&app->guided->fobclone);
    memset(&app->guided->fobclone, 0, sizeof(app->guided->fobclone));
    build_unique_makes(s_makes, &s_make_count, 32);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBclone: Make");
    for(int i = 0; i < s_make_count; i++)
        submenu_add_item(app->submenu, s_makes[i], (uint32_t)i, fobclone_make_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobclone_make_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobclone_make_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Model picker ────────────────────────────────────────────────────────── */
static int s_fc_model_veh[32];
static int s_fc_model_count = 0;

static void fobclone_model_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= s_fc_model_count) return;
    app->guided->fobclone.model_idx = s_fc_model_veh[idx];
    app->guided->fobclone.year_idx  = -1;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobcloneYear);
}

void flipper_scene_fobclone_model_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBclone: Model");

    const char* make = (app->guided->fobclone.make_idx >= 0 && app->guided->fobclone.make_idx < s_make_count)
                       ? s_makes[app->guided->fobclone.make_idx] : "";
    s_fc_model_count = 0;
    int n = flipper_fc_vehicle_count();
    for(int i = 0; i < n && s_fc_model_count < 32; i++) {
        const FlipperFcVehicle* v = flipper_fc_vehicle_at(i);
        if(!v || !v->make || strcmp(v->make, make) != 0) continue;
        s_fc_model_veh[s_fc_model_count] = i;
        submenu_add_item(app->submenu, v->model,
                         (uint32_t)s_fc_model_count, fobclone_model_cb, app);
        s_fc_model_count++;
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobclone_model_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobclone_model_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Year picker (VariableItemList) ──────────────────────────────────────── */
static void fobclone_year_change_cb(VariableItem* item) {
    FlipperApp* app = (FlipperApp*)variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->guided->fobclone.year_idx = (int)idx;
    int vidx = app->guided->fobclone.model_idx;
    const FlipperFcVehicle* yv = flipper_fc_vehicle_at(vidx);
    if(yv && idx < yv->year_count)
        variable_item_set_current_value_text(item, yv->years[idx]);
}

/* Forward OK as custom event 0 so the year-selection scene can continue to
 * capture. Without this callback, the selector cannot advance. */
static void fobclone_year_enter_cb(void* ctx, uint32_t index) {
    FlipperApp* app = (FlipperApp*)ctx;
    UNUSED(index);
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void flipper_scene_fobclone_year_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    variable_item_list_reset(app->var_list);
    variable_item_list_set_enter_callback(app->var_list, fobclone_year_enter_cb, app);

    int vidx = app->guided->fobclone.model_idx;
    const FlipperFcVehicle* v = flipper_fc_vehicle_at(vidx);
    if(!v) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    VariableItem* item = variable_item_list_add(app->var_list, "Year",
                             (uint8_t)v->year_count, fobclone_year_change_cb, app);
    variable_item_set_current_value_index(item, 0);
    if(v->year_count > 0) variable_item_set_current_value_text(item, v->years[0]);
    app->guided->fobclone.year_idx = 0;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewVarList);
}

bool flipper_scene_fobclone_year_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Event 0 signals that the user confirmed the year. */
    if(e.type == SceneManagerEventTypeCustom && e.event == 0) {
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobcloneCapture);
        return true;
    }
    return false;
}
void flipper_scene_fobclone_year_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    variable_item_list_reset(app->var_list);
}

/* ── Capture / replay screen ─────────────────────────────────────────────── */
/* Input handlers change state directly, so commit the model after visible
   updates or the screen will not repaint until it is switched. */
static void fobclone_redraw(FlipperApp* app) {
    view_get_model(app->fobclone_view);
    view_commit_model(app->fobclone_view, true);
}

void flipper_fobclone_draw_cb(Canvas* canvas, void* model) {
    /* The view model holds the FlipperApp pointer. */
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobcloneState* fc = &app->guided->fobclone;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBclone");
    canvas_set_font(canvas, FontSecondary);

    const FlipperFcVehicle* dv = flipper_fc_vehicle_at(fc->model_idx);
    if(dv) {
        canvas_draw_str(canvas, 0, 20, dv->make);
        canvas_draw_str(canvas, 0, 29, dv->model);
    }
    canvas_draw_line(canvas, 0, 32, 127, 32);

    if(fc->replay_ready) {
        canvas_draw_str(canvas, 0, 42, "Ready to replay");
        char line[40];
        snprintf(line, sizeof(line), "Predict: %lu - %lu",
                 (unsigned long)fc->cap[0]->decode.predict_lo,
                 (unsigned long)fc->cap[0]->decode.predict_hi);
        canvas_draw_str(canvas, 0, 52, line);
        snprintf(line, sizeof(line), "Key: %s",
                 fc->cap[0]->decode.mfr_name[0] ? fc->cap[0]->decode.mfr_name : "unknown");
        canvas_draw_str(canvas, 0, 62, line);
        canvas_draw_str(canvas, 0, 10, "  [OK]=Replay");
    } else if(fc->cap_count == 1) {
        canvas_draw_str(canvas, 0, 42, "Capture 1/2 received");
        char line[32];
        snprintf(line, sizeof(line), "cnt=%lu  btn=%u",
                 (unsigned long)fc->cap[0]->decode.cnt, fc->cap[0]->decode.btn);
        canvas_draw_str(canvas, 0, 52, line);
        canvas_draw_str(canvas, 0, 62, "Press fob again...");
    } else {
        canvas_draw_str(canvas, 0, 42, "Press fob near device.");
        canvas_draw_str(canvas, 0, 52, "Awaiting capture 1/2...");
    }
}

bool flipper_fobclone_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(e->type == InputTypeShort && e->key == InputKeyOk && app->guided->fobclone.replay_ready) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventReplayDone);
        return true;
    }
    return false;
}

/* ── Auto-arm ─────────────────────────────────────────────────────────────── */
static void fobclone_edge_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
}

static void fobclone_arm(FlipperApp* app) {
    /* Select a radio profile from the chosen vehicle name. */
    FlipperFobcloneState* fc = &app->guided->fobclone;
    int vidx = fc->model_idx;
    float freq = 433.92f;
    FlipperPreset preset = FlipperPresetOOK650;

    const FlipperFcVehicle* av = flipper_fc_vehicle_at(vidx);
    if(av) {
        const char* model = av->model;
        /* Check longer frequency strings first to avoid substring collisions
           such as "315" inside "312-315". */
        if     (strstr(model, "915"))                          { freq = 915.00f; }
        else if(strstr(model, "868"))                          { freq = 868.00f; }
        else if(strstr(model, "315"))                          { freq = 315.00f; }
        else if(strstr(model, "318"))                          { freq = 318.00f; }
        else if(strstr(model, "312"))                          { freq = 312.20f; }
        else if(strstr(model, "313") || strstr(model, "310")) { freq = 313.00f; }
        else if(strstr(model, "390"))                          { freq = 390.00f; }
        else if(strstr(model, "418"))                          { freq = 418.00f; }
        else if(strstr(model, "300"))                          { freq = 300.00f; }
        /* else default: 433.92 MHz */

        /* Step 2 — modulation */
        if     (strstr(model, "868"))  { preset = FlipperPresetOOK270; }
        else if(strstr(model, "2FSK")) { preset = FlipperPreset2FSKDev238; }
        /* else default: OOK650 */
    }

    app->capture->freq_mhz    = freq;
    app->capture->preset       = preset;
    app->capture->squelch_dbm  = -90.0f;
    app->capture->on_edge      = fobclone_edge_cb;
    app->capture->on_edge_ctx  = app;
    flipper_capture_start(app->capture);
    fc->armed = true;
}

void flipper_scene_fobclone_capture_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobcloneState* fc = &app->guided->fobclone;
    flipper_app_gui_radio_acquire(app);
    fobclone_caps_free(fc);
    fc->armed        = false;
    fc->tx_pending   = false;
    fobclone_arm(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobclone);
}

bool flipper_scene_fobclone_capture_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobcloneState* fc = &app->guided->fobclone;
    if(e.type != SceneManagerEventTypeCustom) return false;

    if(e.event == FlipperEventCaptureDone && !fc->replay_ready) {
        if(flipper_capture_flush(app->capture)) {
            FlipperCaptureResult* cr = &app->capture->result;
            if(cr->decode_ok && fc->cap_count < 2) {
                FlipperCaptureResult* saved = malloc(sizeof(*saved));
                if(!saved) {
                    notification_message(app->notifications, &sequence_error);
                } else {
                    *saved = *cr;
                    fc->cap[fc->cap_count++] = saved;
                    if(fc->cap_count == 2) {
                        flipper_kl_predict(&fc->cap[0]->decode, &fc->cap[1]->decode);
                        fc->replay_ready = true;
                        notification_message(app->notifications, &sequence_success);
                    } else {
                        notification_message(app->notifications, &sequence_blink_blue_100);
                    }
                }
            }
        }
        fobclone_redraw(app);
        return true;
    }

    if(e.event == FlipperEventReplayDone && fc->replay_ready) {
        if(fc->tx_pending) {
            fc->tx_pending = false;
            flipper_capture_stop(app->capture);
            fobclone_caps_free(fc);
            fobclone_arm(app);
            fobclone_redraw(app);
            return true;
        }
        flipper_capture_stop(app->capture);
        float freq   = app->capture->freq_mhz;
        FlipperPreset pr = app->capture->preset;

        fc->tx_pending = flipper_capture_tx_repeat(
            app->capture, &fc->cap[1]->pulses, 4, freq, pr, 80);
        if(!fc->tx_pending) notification_message(app->notifications, &sequence_error);
        return true;
    }

    return false;
}

void flipper_scene_fobclone_capture_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    app->capture->on_edge     = NULL;
    app->capture->on_edge_ctx = NULL;
    flipper_app_gui_radio_release(app);
    app->guided->fobclone.armed = false;
    fobclone_caps_free(&app->guided->fobclone);
}
