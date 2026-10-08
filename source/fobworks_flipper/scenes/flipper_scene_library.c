#include "../flipper_fobscan_app.h"
#include "../protocol/flipper_saved_check.h"
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

/* Browse captures stored as .sub files on the SD card. Choose Decoded or Raw, then select a capture to send, inspect, predict from, export, or delete. */

/* ── Category select (Decoded / Raw) ──────────────────────────────────────── */
enum { LibCatDecoded = 0, LibCatRaw = 1 };

static void library_cat_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    app->lib_browse_raw = (idx == LibCatRaw);
    scene_manager_next_scene(app->scene_manager, FlipperSceneLibraryList);
}

void flipper_scene_library_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    int nd = flipper_lib_count(app->storage, true);
    int nr = flipper_lib_count(app->storage, false);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Library");

    char label[32];
    snprintf(label, sizeof(label), "Decoded (%d)", nd);
    submenu_add_item(app->submenu, label, LibCatDecoded, library_cat_cb, app);
    snprintf(label, sizeof(label), "Raw (%d)", nr);
    submenu_add_item(app->submenu, label, LibCatRaw, library_cat_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_library_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_library_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── File list ────────────────────────────────────────────────────────────── */
static void library_list_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= app->lib_entry_count) return;

    strncpy(app->lib_sel_name, app->lib_entries[idx].name, FLIPPER_LIB_NAME_MAX - 1);
    app->lib_sel_name[FLIPPER_LIB_NAME_MAX - 1] = '\0';
    app->lib_sel_decoded = !app->lib_browse_raw;

    if(flipper_lib_load_with_protocol(
           app->storage, app->lib_sel_decoded, app->lib_sel_name,
           &app->lib_sel, &app->lib_sel_preset, app->lib_sel_protocol,
           sizeof(app->lib_sel_protocol))) {
        scene_manager_next_scene(app->scene_manager, FlipperSceneLibraryItem);
    } else {
        notification_message(app->notifications, &sequence_error);
    }
}

void flipper_scene_library_list_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    app->lib_entry_count = flipper_lib_list(
        app->storage, !app->lib_browse_raw, app->lib_entries, FLIPPER_LIB_LIST_MAX);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->lib_browse_raw ? "Raw signals" : "Decoded signals");

    if(app->lib_entry_count == 0) {
        submenu_add_item(app->submenu, "(empty)", 0, NULL, app);
    } else {
        for(int i = 0; i < app->lib_entry_count; i++)
            submenu_add_item(app->submenu, app->lib_entries[i].name, i, library_list_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_library_list_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_library_list_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Item actions (Send / Info / Predict / Delete) ────────────────────────── */
enum {
    LibActSend = 0, LibActInfo, LibActCheck, LibActScope, LibActPredict,
    LibActExport, LibActDelete
};

static void library_item_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    switch(idx) {
    case LibActSend: {
        /* Replay the saved pulses with their captured frequency and preset. */
        flipper_app_gui_radio_acquire(app);
        flipper_capture_tx_set_done_cb(
            app->capture, flipper_app_gui_tx_done, app);
        bool ok = flipper_capture_tx(app->capture, &app->lib_sel.pulses,
                                     app->lib_sel.pulses.freq_mhz,
                                     app->lib_sel_preset, 3000);
        if(!ok) {
            flipper_capture_tx_set_done_cb(
                app->capture, flipper_app_tx_done, app);
            flipper_app_gui_radio_release(app);
        }
        notification_message(app->notifications,
                             ok ? &sequence_success : &sequence_error);
        break;
    }
    case LibActInfo:
        scene_manager_next_scene(app->scene_manager, FlipperSceneLibraryInfo);
        break;
    case LibActCheck: {
        FlipperSavedCheck check;
        flipper_saved_judge(&app->lib_sel.pulses, app->lib_sel_protocol, &check);
        snprintf(app->info_title, sizeof(app->info_title), "Saved check");
        snprintf(app->info_body, sizeof(app->info_body), "%s\n%s\n%s%s",
                 check.line1, check.line2, check.line3,
                 check.kind == FlipperSavedForce ?
                     "\nForced parse is not proof" : "");
        scene_manager_next_scene(app->scene_manager, FlipperSceneInfo);
        break;
    }
    case LibActScope:
        scene_manager_next_scene(app->scene_manager, FlipperSceneLibScope);
        break;
    case LibActPredict:
        /* Try the KeeLoq next-code predictor on this saved frame. */
        flipper_kl_predict(&app->lib_sel.decode, NULL);
        scene_manager_next_scene(app->scene_manager, FlipperSceneLibraryInfo);
        break;
    case LibActExport: {
        /* Copy the capture to the stock SubGHz Saved browser for access outside this app. */
        bool ok = flipper_lib_export_subghz(app->storage, app->lib_sel_decoded,
                                            app->lib_sel_name);
        notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
        break;
    }
    case LibActDelete: {
        bool ok = flipper_lib_delete(app->storage, app->lib_sel_decoded, app->lib_sel_name);
        notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
        /* Return to the updated file list. */
        scene_manager_previous_scene(app->scene_manager);
        break;
    }
    default:
        break;
    }
}

void flipper_scene_library_item_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->lib_sel_name);

    submenu_add_item(app->submenu, "Send (replay)", LibActSend, library_item_cb, app);
    submenu_add_item(app->submenu, "Info", LibActInfo, library_item_cb, app);
    submenu_add_item(app->submenu, "Read-only saved check", LibActCheck, library_item_cb, app);
    submenu_add_item(app->submenu, "LibScope waveform", LibActScope, library_item_cb, app);
    /* The on-device predictor supports KeeLoq only. */
    if(app->lib_sel_decoded && app->lib_sel.decode.rolling &&
       strncmp(app->lib_sel.decode.proto, "KeeLoq", 6) == 0)
        submenu_add_item(app->submenu, "Predict Next", LibActPredict, library_item_cb, app);
    submenu_add_item(app->submenu, "Export to SubGHz", LibActExport, library_item_cb, app);
    submenu_add_item(app->submenu, "Delete", LibActDelete, library_item_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_library_item_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_library_item_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}

/* ── Info detail (custom canvas) ──────────────────────────────────────────── */
void flipper_libinfo_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    const FlipperCaptureResult* cap = &app->lib_sel;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, app->lib_sel_decoded ? "Signal Info" : "Raw Info");

    canvas_set_font(canvas, FontSecondary);
    char line[52];

    snprintf(line, sizeof(line), "%.2f MHz  %d edges",
             (double)cap->pulses.freq_mhz, cap->pulses.len);
    canvas_draw_str(canvas, 0, 22, line);

    if(app->lib_sel_decoded) {
        canvas_draw_str(canvas, 0, 32, cap->decode.proto);

        snprintf(line, sizeof(line), "Addr %08lX  Cnt %lu",
                 (unsigned long)cap->decode.addr, (unsigned long)cap->decode.cnt);
        canvas_draw_str(canvas, 0, 42, line);

        if(cap->decode.mfr_name[0]) {
            snprintf(line, sizeof(line), "Key: %s", cap->decode.mfr_name);
            canvas_draw_str(canvas, 0, 52, line);
        }
        if(cap->decode.predict_window > 0) {
            snprintf(line, sizeof(line), "Next: %lu-%lu",
                     (unsigned long)cap->decode.predict_lo,
                     (unsigned long)cap->decode.predict_hi);
            canvas_draw_str(canvas, 0, 62, line);
        } else {
            snprintf(line, sizeof(line), "TE %lu us  %s",
                     (unsigned long)cap->decode.te_us,
                     cap->decode.rolling ? "rolling" : "fixed");
            canvas_draw_str(canvas, 0, 62, line);
        }
    } else {
        snprintf(line, sizeof(line), "TE %lu us", (unsigned long)cap->pulses.te_us);
        canvas_draw_str(canvas, 0, 32, line);
        canvas_draw_str(canvas, 0, 46, "Undecoded burst");
        canvas_draw_str(canvas, 0, 62, "[Back] to return");
    }
}

bool flipper_libinfo_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(e->type == InputTypeShort && e->key == InputKeyBack) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void flipper_scene_library_info_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewLibInfo);
}

bool flipper_scene_library_info_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_library_info_on_exit(void* ctx) {
    UNUSED(ctx);
}
