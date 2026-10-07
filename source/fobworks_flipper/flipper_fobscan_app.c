#include "flipper_fobscan_app.h"
#include "protocol/flipper_plugin.h"
#include "protocol/flipper_radio_loader.h"
#include <furi.h>
#include <furi_hal_power.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Frequencies used by FOBscan and Advanced Settings. All fall within the
 * CC1101 bands: 300-348, 387-464, and 779-928 MHz. */
const float FOBSCAN_FREQS[] = {
    300.00f, 303.87f, 310.00f, 315.00f, 318.00f, 330.00f,
    390.00f, 418.00f, 433.42f, 433.92f, 434.42f, 868.35f, 915.00f,
};
const int FOBSCAN_FREQ_COUNT = (int)(sizeof(FOBSCAN_FREQS) / sizeof(FOBSCAN_FREQS[0]));

#define FLIPPER_SETTINGS_PATH EXT_PATH("flipper_fobscan/settings.conf")

/* ── Advanced Settings persistence ────────────────────────────────────────── */
void flipper_adv_settings_load(FlipperApp* app) {
    /* Start from known defaults, then replace them with saved values if present. */
    app->adv.freq_idx         = FOBSCAN_FREQ_DEFAULT;
    app->adv.preset           = FlipperPresetOOK650;
    app->adv.squelch_dbm      = -90.0f;
    app->adv.force_proto      = FlipperForceAuto;
    app->adv.autosave_decoded = true;
    app->adv.autosave_raw     = true;
    app->adv.lib_evict_oldest = true;   /* default: drop oldest to make room */
    app->adv.dashboard_link   = false;  /* default OFF: saves ~9KB heap at launch */
    app->adv.access_code[0]   = '\0';
    app->adv.prefer_external  = false;

    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FLIPPER_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t sz = storage_file_size(f);
        if(sz > 0 && sz < 2048) {
            char* t = malloc((size_t)sz + 1);
            if(t) {
                size_t rd = storage_file_read(f, t, (size_t)sz);
                t[rd] = '\0';
                char* p;
                if((p = strstr(t, "freq_idx=")))  app->adv.freq_idx    = atoi(p + 9);
                if((p = strstr(t, "preset=")))     app->adv.preset      = (FlipperPreset)atoi(p + 7);
                if((p = strstr(t, "squelch=")))    app->adv.squelch_dbm = (float)atoi(p + 8);
                if((p = strstr(t, "force=")))      app->adv.force_proto = (FlipperForceProto)atoi(p + 6);
                if((p = strstr(t, "save_dec=")))   app->adv.autosave_decoded = atoi(p + 9) != 0;
                if((p = strstr(t, "save_raw=")))   app->adv.autosave_raw     = atoi(p + 9) != 0;
                if((p = strstr(t, "evict=")))      app->adv.lib_evict_oldest = atoi(p + 6) != 0;
                if((p = strstr(t, "link=")))       app->adv.dashboard_link   = atoi(p + 5) != 0;
                if((p = strstr(t, "ext=")))        app->adv.prefer_external  = atoi(p + 4) != 0;
                if((p = strstr(t, "code="))) {
                    /* Digits only, max 6. Empty after code= clears auth. */
                    p += 5;
                    size_t i = 0;
                    while(i + 1 < sizeof(app->adv.access_code) && p[i] >= '0' && p[i] <= '9') {
                        app->adv.access_code[i] = p[i];
                        i++;
                    }
                    app->adv.access_code[i] = '\0';
                }
                free(t);
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);

    /* Reject stored indices and enum values outside their valid ranges. */
    if(app->adv.freq_idx < 0 || app->adv.freq_idx >= FOBSCAN_FREQ_COUNT)
        app->adv.freq_idx = FOBSCAN_FREQ_DEFAULT;
    /* preset/force_proto are unsigned enums (can't be < 0); an out-of-range or
       corrupted value reads as a large unsigned and is caught by the upper bound. */
    if((unsigned)app->adv.preset > 3)
        app->adv.preset = FlipperPresetOOK650;
    if((unsigned)app->adv.force_proto >= FlipperForceCount)
        app->adv.force_proto = FlipperForceAuto;

    /* Apply the preferred radio after settings load. External stays selected
       only when OTG comes up and the SPI VERSION probe passes. */
    if(app->adv.prefer_external) {
        if(!radio_loader_set(RadioDeviceExternal) || !radio_loader_is_connected()) {
            radio_loader_set(RadioDeviceInternal);
            app->adv.prefer_external = false;
        }
    } else {
        radio_loader_set(RadioDeviceInternal);
    }
}

void flipper_adv_settings_save(FlipperApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FLIPPER_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[288];
        int n = snprintf(buf, sizeof(buf),
            "freq_idx=%d\npreset=%d\nsquelch=%d\nforce=%d\nsave_dec=%d\nsave_raw=%d\nevict=%d\nlink=%d\next=%d\ncode=%s\n",
            app->adv.freq_idx, (int)app->adv.preset, (int)app->adv.squelch_dbm,
            (int)app->adv.force_proto, app->adv.autosave_decoded ? 1 : 0,
            app->adv.autosave_raw ? 1 : 0, app->adv.lib_evict_oldest ? 1 : 0,
            app->adv.dashboard_link ? 1 : 0, app->adv.prefer_external ? 1 : 0,
            app->adv.access_code);
        storage_file_write(f, buf, n);
    }
    storage_file_close(f);
    storage_file_free(f);
}

/* ── Optional dashboard links ─────────────────────────────────────────────── */
void flipper_links_ensure(FlipperApp* app) {
    if(app->usb_link && app->uart_link) return;

    /* Both links are one feature. Tear down either worker if its partner
       cannot be created. */
    if(!app->usb_link) app->usb_link = flipper_link_alloc(app, FlipperLinkUsb);
    if(!app->usb_link) goto failed;
    if(!app->uart_link) app->uart_link = flipper_link_alloc(app, FlipperLinkUart);
    if(!app->uart_link) goto failed;
    return;

failed:
    flipper_links_release(app);
    /* Avoid repeating a failed allocation in this settings session. The user
       can try again after returning to the menu or freeing heap elsewhere. */
    app->adv.dashboard_link = false;
}

void flipper_links_release(FlipperApp* app) {
    if(app->usb_link)  { flipper_link_free(app->usb_link);  app->usb_link  = NULL; }
    if(app->uart_link) { flipper_link_free(app->uart_link); app->uart_link = NULL; }
}

/* ── Tick timer (500 ms) ─────────────────────────────────────────────────── */
static void flipper_tick_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperEventStatusTick);

    /* During a headless remote scan, flush captured edges and stream every
       decoded signal to the dashboard. The remote path runs with on_edge
       NULL, so without this the link advertised scan:true while emitting no
       signal events; the dashboard could only poll `capture` blind. */
    if(app->remote_scanning && !app->gui_radio_active) {
        furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
        bool has_scan = app->remote_scanning && !app->gui_radio_active;
        furi_mutex_release(app->radio_mutex);
        if(has_scan && flipper_capture_flush(app->capture)) {
            app->remote_last       = app->capture->result;
            app->remote_last_valid = true;
            char sbuf[384];
            size_t slen = flipper_proto_emit_signal(
                sbuf, sizeof(sbuf), &app->remote_last.decode,
                flipper_capture_rssi(app->capture));
            flipper_app_broadcast(app, sbuf, slen);
        }
    }

    /* Send a heartbeat about once per second to dashboard clients on either
       link. */
    uint32_t now = furi_get_tick();
    if(now - app->hb_last_tick >= furi_ms_to_ticks(1000)) {
        app->hb_last_tick = now;
        char buf[160];
        uint32_t uptime_s = (now - app->boot_tick) / furi_ms_to_ticks(1000);
        uint8_t batt_pct = furi_hal_power_get_pct();
        size_t len = flipper_proto_emit_heartbeat(
            buf, sizeof(buf),
            app->capture->freq_mhz > 0 ? app->capture->freq_mhz : 433.92f,
            app->remote_scanning || app->gui_radio_active,
            batt_pct, uptime_s, true, 0);
        flipper_app_broadcast(app, buf, len);
    }
}

/* ── ViewDispatcher navigation / event callbacks ─────────────────────────── */
static bool flipper_nav_event_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Cancel pending TX before navigation. The scene's on_exit stops the radio
       and releases ownership in order. */
    flipper_capture_tx_cancel(app->capture, 0, FlipperTxCancelBack);
    return scene_manager_handle_back_event(app->scene_manager);
}

static bool flipper_custom_event_cb(void* ctx, uint32_t event) {
    FlipperApp* app = (FlipperApp*)ctx;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

/* Custom canvas views need a non-null enter callback: this firmware's view
   entry path calls it whenever the view is shown. */
static void flipper_custom_view_enter_cb(void* ctx) {
    UNUSED(ctx);
}

/* Draw callbacks receive the view model, not the context. Store the
   FlipperApp pointer in each custom view's model. */
static void flipper_custom_view_set_model(View* view, FlipperApp* app) {
    view_allocate_model(view, ViewModelTypeLockFree, sizeof(FlipperApp*));
    FlipperApp** m = view_get_model(view);
    *m = app;
    view_commit_model(view, false);
}

/* ── Guided-flow union lifecycle (heap-allocated off the FlipperApp block) ─── */
bool flipper_guided_ensure(FlipperApp* app, size_t bytes) {
    if(app->guided) return true;
    /* Allocate only the active flow's state. Clamp the request to the union's
       size. FOBback's capture block is a separate malloc after the pickers. */
    if(bytes == 0 || bytes > sizeof(FlipperGuided)) bytes = sizeof(FlipperGuided);
    app->guided = malloc(bytes);
    if(!app->guided) return false;
    memset(app->guided, 0, bytes);
    return true;
}

void flipper_guided_release(FlipperApp* app) {
    if(!app->guided) return;
    free(app->guided);
    app->guided = NULL;
}

/* ── Alloc ───────────────────────────────────────────────────────────────── */
static FlipperApp* flipper_app_alloc(void) {
    FlipperApp* app = malloc(sizeof(FlipperApp));
    furi_assert(app);
    memset(app, 0, sizeof(*app));

    app->gui           = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage       = furi_record_open(RECORD_STORAGE);
    app->library_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->capture       = flipper_capture_alloc();
    furi_assert(app->capture);
    flipper_capture_tx_set_done_cb(app->capture, flipper_app_tx_done, app);

    /* Allocate guided-flow state only while a guided scene is active. It is
       released on return to the main menu, keeping launch and FOBscan lean. */
    app->guided        = NULL;

    /* Scene manager */
    app->scene_manager = scene_manager_alloc(&flipper_scene_handlers, app);

    /* View dispatcher */
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, flipper_nav_event_cb);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipper_custom_event_cb);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    /* Reuse the Submenu for the main menu and list screens, and one
       VariableItemList for year selection. Capture screens use custom canvases. */
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewMenu,
                             submenu_get_view(app->submenu));

    app->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewVarList,
                             variable_item_list_get_view(app->var_list));

    app->fobscan_view = view_alloc();
    view_set_draw_callback(app->fobscan_view, flipper_fobscan_draw_cb);
    view_set_input_callback(app->fobscan_view, flipper_fobscan_input_cb);
    view_set_enter_callback(app->fobscan_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobscan_view, app);
    flipper_custom_view_set_model(app->fobscan_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobscan, app->fobscan_view);

    app->fobclone_view = view_alloc();
    view_set_draw_callback(app->fobclone_view, flipper_fobclone_draw_cb);
    view_set_input_callback(app->fobclone_view, flipper_fobclone_input_cb);
    view_set_enter_callback(app->fobclone_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobclone_view, app);
    flipper_custom_view_set_model(app->fobclone_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobclone, app->fobclone_view);

    app->fobcatch_view = view_alloc();
    view_set_draw_callback(app->fobcatch_view, flipper_fobcatch_draw_cb);
    view_set_input_callback(app->fobcatch_view, flipper_fobcatch_input_cb);
    view_set_enter_callback(app->fobcatch_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobcatch_view, app);
    flipper_custom_view_set_model(app->fobcatch_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobcatch, app->fobcatch_view);

    app->fobback_view = view_alloc();
    view_set_draw_callback(app->fobback_view, flipper_fobback_draw_cb);
    view_set_input_callback(app->fobback_view, flipper_fobback_input_cb);
    view_set_enter_callback(app->fobback_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobback_view, app);
    flipper_custom_view_set_model(app->fobback_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobback, app->fobback_view);

    app->libinfo_view = view_alloc();
    view_set_draw_callback(app->libinfo_view, flipper_libinfo_draw_cb);
    view_set_input_callback(app->libinfo_view, flipper_libinfo_input_cb);
    view_set_enter_callback(app->libinfo_view, flipper_custom_view_enter_cb);
    view_set_context(app->libinfo_view, app);
    flipper_custom_view_set_model(app->libinfo_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewLibInfo, app->libinfo_view);

    app->libscope_view = view_alloc();
    view_set_draw_callback(app->libscope_view, flipper_libscope_draw_cb);
    view_set_input_callback(app->libscope_view, flipper_libscope_input_cb);
    view_set_enter_callback(app->libscope_view, flipper_custom_view_enter_cb);
    view_set_context(app->libscope_view, app);
    flipper_custom_view_set_model(app->libscope_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewLibScope, app->libscope_view);

    app->fobsweep_view = view_alloc();
    view_set_draw_callback(app->fobsweep_view, flipper_fobsweep_draw_cb);
    view_set_input_callback(app->fobsweep_view, flipper_fobsweep_input_cb);
    view_set_enter_callback(app->fobsweep_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobsweep_view, app);
    flipper_custom_view_set_model(app->fobsweep_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobsweep, app->fobsweep_view);

    app->info_view = view_alloc();
    view_set_draw_callback(app->info_view, flipper_info_draw_cb);
    view_set_input_callback(app->info_view, flipper_info_input_cb);
    view_set_enter_callback(app->info_view, flipper_custom_view_enter_cb);
    view_set_context(app->info_view, app);
    flipper_custom_view_set_model(app->info_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewInfo, app->info_view);

    app->credits_view = view_alloc();
    view_set_draw_callback(app->credits_view, flipper_credits_draw_cb);
    view_set_input_callback(app->credits_view, flipper_credits_input_cb);
    view_set_enter_callback(app->credits_view, flipper_custom_view_enter_cb);
    view_set_context(app->credits_view, app);
    flipper_custom_view_set_model(app->credits_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewCredits, app->credits_view);

    app->fobpwn_view = view_alloc();
    view_set_draw_callback(app->fobpwn_view, flipper_fobpwn_draw_cb);
    view_set_input_callback(app->fobpwn_view, flipper_fobpwn_input_cb);
    view_set_enter_callback(app->fobpwn_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobpwn_view, app);
    flipper_custom_view_set_model(app->fobpwn_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewFobpwn, app->fobpwn_view);

    app->rx_tool_view = view_alloc();
    view_set_draw_callback(app->rx_tool_view, flipper_rx_tool_draw_cb);
    view_set_input_callback(app->rx_tool_view, flipper_rx_tool_input_cb);
    view_set_enter_callback(app->rx_tool_view, flipper_custom_view_enter_cb);
    view_set_context(app->rx_tool_view, app);
    flipper_custom_view_set_model(app->rx_tool_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewRxTool, app->rx_tool_view);

    app->fobcrack_view = view_alloc();
    view_set_draw_callback(app->fobcrack_view, flipper_fobcrack_draw_cb);
    view_set_input_callback(app->fobcrack_view, flipper_fobcrack_input_cb);
    view_set_enter_callback(app->fobcrack_view, flipper_custom_view_enter_cb);
    view_set_context(app->fobcrack_view, app);
    flipper_custom_view_set_model(app->fobcrack_view, app);
    view_dispatcher_add_view(app->view_dispatcher, FlipperViewLab, app->fobcrack_view);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui,
                                  ViewDispatcherTypeFullscreen);

    /* Tick timer */
    app->tick_timer = furi_timer_alloc(flipper_tick_cb, FuriTimerTypePeriodic, app);

    flipper_lib_init(app->storage);
    flipper_adv_settings_load(app);

    /* Dashboard links each reserve a 4 KB RX-thread stack (~9 KB heap total).
       Keep them opt-in through Advanced Settings and allocate them only after
       the objects their workers use are ready. */
    app->boot_tick          = furi_get_tick();
    app->hb_last_tick       = app->boot_tick;
    app->remote_squelch_dbm = -90.0f;
    app->radio_mutex        = furi_mutex_alloc(FuriMutexTypeNormal);
    app->usb_link           = NULL;
    app->uart_link          = NULL;
    /* Do not start dashboard workers during launch, even if an older settings
       file enables them. The user can turn them on after the main menu appears. */

    return app;
}

/* ── Free ────────────────────────────────────────────────────────────────── */
static void flipper_app_free(FlipperApp* app) {
    furi_assert(app);

    /* Stop the heartbeat timer before freeing either link. */
    furi_timer_stop(app->tick_timer);
    furi_timer_free(app->tick_timer);

    /* Finish TX and its callback before freeing links, so broadcasts cannot
       race link teardown. */
    flipper_capture_tx_cancel(app->capture, 0, FlipperTxCancelAppExit);
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    flipper_capture_tx_set_done_cb(app->capture, NULL, NULL);

    /* No TX callback can broadcast after this point; release both transports. */
    flipper_links_release(app);
    furi_mutex_free(app->radio_mutex);

    flipper_capture_free(app->capture);
    flipper_guided_release(app);
    flipper_plugin_unload_all();

    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewVarList);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobscan);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobclone);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobcatch);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobback);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewLibInfo);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewLibScope);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobsweep);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewInfo);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewCredits);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewFobpwn);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewRxTool);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperViewLab);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_list);
    view_free(app->fobscan_view);
    view_free(app->fobclone_view);
    view_free(app->fobcatch_view);
    view_free(app->fobback_view);
    view_free(app->libinfo_view);
    view_free(app->libscope_view);
    view_free(app->fobsweep_view);
    view_free(app->info_view);
    view_free(app->credits_view);
    view_free(app->fobpwn_view);
    view_free(app->rx_tool_view);
    view_free(app->fobcrack_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_mutex_free(app->library_mutex);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);

    free(app);
}

/* ── Entry point ─────────────────────────────────────────────────────────── */
int32_t flipper_fobscan_app(void* p) {
    UNUSED(p);
    furi_assert(kl_self_test());

    FlipperApp* app = flipper_app_alloc();
    furi_timer_start(app->tick_timer, furi_ms_to_ticks(500));
    scene_manager_next_scene(app->scene_manager, FlipperSceneMainMenu);
    view_dispatcher_run(app->view_dispatcher);
    flipper_app_free(app);
    return 0;
}
