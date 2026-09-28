#include "../flipper_fobscan_app.h"
#include "../protocol/flipper_keeloq.h"
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBcrack — listen for one KeeLoq frame and test the built-in manufacturer */
/* keys. A match shows the key name. Otherwise the screen shows the serial.  */
/* ─────────────────────────────────────────────────────────────────────────── */

static bool fobcrack_is_keeloq(const FlipperDecodeResult* d) {
    return d && strncmp(d->proto, "KeeLoq", 6) == 0;
}

static int fobcrack_freq_idx(FlipperApp* app) {
    float f = app->fobscan.freq_mhz;
    for(int i = 0; i < FOBSCAN_FREQ_COUNT; i++) {
        float d = FOBSCAN_FREQS[i] - f;
        if(d < 0.2f && d > -0.2f) return i;
    }
    int idx = app->adv.freq_idx;
    if(idx < 0 || idx >= FOBSCAN_FREQ_COUNT) idx = FOBSCAN_FREQ_DEFAULT;
    return idx;
}

static void fobcrack_edge(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
}

static void fobcrack_listen(FlipperApp* app) {
    FlipperFobcrackState* s = &app->guided->fobcrack;
    int idx = s->bits;
    if(idx < 0 || idx >= FOBSCAN_FREQ_COUNT) idx = FOBSCAN_FREQ_DEFAULT;
    s->bits = idx;
    flipper_capture_stop(app->capture);
    app->capture->freq_mhz = FOBSCAN_FREQS[idx];
    app->capture->preset = app->adv.preset;
    app->capture->squelch_dbm = app->adv.squelch_dbm;
    app->capture->force_proto = FlipperForceAuto;
    app->capture->on_edge = fobcrack_edge;
    app->capture->on_edge_ctx = app;
    flipper_capture_start(app->capture);
    s->armed = true;
    s->running = true;
    s->done = false;
    s->found = false;
    snprintf(s->status, sizeof(s->status), "Listen %.2f", (double)FOBSCAN_FREQS[idx]);
}

static void fobcrack_finish(FlipperApp* app, const FlipperDecodeResult* d) {
    FlipperFobcrackState* s = &app->guided->fobcrack;
    flipper_capture_stop(app->capture);
    app->fobscan.last_decode = *d;
    app->fobscan.last_decode_valid = true;
    s->running = false;
    s->done = true;
    if(d->device_key_hex[0]) {
        s->found = true;
        snprintf(s->status, sizeof(s->status), "Key matched");
        snprintf(s->result, sizeof(s->result), "%s %s", d->mfr_name, d->device_key_hex);
        notification_message(app->notifications, &sequence_success);
    } else {
        s->found = false;
        snprintf(s->status, sizeof(s->status), "Known keys missed");
        snprintf(s->result, sizeof(s->result), "SN %08lX", (unsigned long)d->addr);
        notification_message(app->notifications, &sequence_blink_blue_100);
    }
}

/* ── Canvas draw ─────────────────────────────────────────────────────────── */
void flipper_fobcrack_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    FlipperFobcrackState* s = &app->guided->fobcrack;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "FOBcrack");
    canvas_set_font(canvas, FontSecondary);

    if(!s->armed) {
        canvas_draw_str(canvas, 0, 22, "Press OK, then the fob.");
        canvas_draw_str(canvas, 0, 32, "[OK]=start  [Back]=exit");
        return;
    }

    /* Status line */
    canvas_draw_str(canvas, 0, 22, s->status);

    if(s->running) {
        canvas_draw_str(canvas, 0, 32, "Wait 1s; press fob.");
        canvas_draw_str(canvas, 0, 42, "[Up/Dn] freq  [Back] exit");
    } else if(s->done) {
        if(s->found) {
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 0, 32, "FOUND");
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 0, 42, s->result);
        } else {
            canvas_draw_str(canvas, 0, 32, "No match.");
            canvas_draw_str(canvas, 0, 42, s->result);
        }
        canvas_draw_str(canvas, 0, 54, "OK=again  Back=exit");
    } else {
        canvas_draw_str(canvas, 0, 42, "[OK]=start  [Back]=exit");
    }
}

bool flipper_fobcrack_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    FlipperFobcrackState* s = &app->guided->fobcrack;

    if(e->type == InputTypeShort &&
       (e->key == InputKeyUp || e->key == InputKeyDown) &&
       s->running && !s->done && app->capture) {
        int n = FOBSCAN_FREQ_COUNT;
        if(e->key == InputKeyUp) s->bits = (s->bits + 1) % n;
        else s->bits = (s->bits + n - 1) % n;
        fobcrack_listen(app);
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventCaptureDone);
        return true;
    }

    if(e->type == InputTypeShort && e->key == InputKeyOk) {
        if(!s->running && app->capture) {
            s->bits = fobcrack_freq_idx(app);
            flipper_app_gui_radio_acquire(app);
            fobcrack_listen(app);
            view_dispatcher_send_custom_event(app->view_dispatcher,
                                              FlipperEventCaptureDone);
            return true;
        }
    }

    if(e->type == InputTypeShort && e->key == InputKeyBack) {
        if(s->running) {
            s->running = false;
        }
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

/* ── Scene lifecycle ─────────────────────────────────────────────────────── */
void flipper_scene_fobcrack_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;

    /* Allocate before any use. Main menu frees this block on the way in. */
    if(!flipper_guided_ensure(app, sizeof(FlipperFobcrackState))) {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "FOBcrack: OOM");
        submenu_add_item(app->submenu, "Back", 0, NULL, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
        return;
    }
    FlipperFobcrackState* s = &app->guided->fobcrack;
    memset(s, 0, sizeof(*s));
    s->status[0] = '\0';
    s->result[0] = '\0';

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewLab);
}

bool flipper_scene_fobcrack_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app->guided) return false;
    FlipperFobcrackState* s = &app->guided->fobcrack;

    if(e.type == SceneManagerEventTypeCustom) {
        if(e.event == FlipperEventCaptureDone && s->running && !s->done &&
           app->capture && flipper_capture_flush(app->capture)) {
            FlipperCaptureResult* cr = &app->capture->result;
            if(cr->decode_ok && fobcrack_is_keeloq(&cr->decode))
                fobcrack_finish(app, &cr->decode);
        }
        if(e.event == FlipperEventCaptureDone || e.event == FlipperEventStatusTick) {
            view_get_model(app->fobcrack_view);
            view_commit_model(app->fobcrack_view, true);
            return true;
        }
        return false;
    }
    return false;
}

void flipper_scene_fobcrack_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app->guided) return;
    FlipperFobcrackState* s = &app->guided->fobcrack;
    s->running = false;
    if(app->capture) {
        flipper_capture_stop(app->capture);
        app->capture->on_edge = NULL;
        app->capture->on_edge_ctx = NULL;
    }
}
