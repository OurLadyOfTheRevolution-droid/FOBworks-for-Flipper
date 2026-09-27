#include "flipper_fobscan_app.h"
#include <furi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ── Shared FOBscan tuning table ──────────────────────────────────────────────
 * Common key-fob / gate / TPMS frequencies, all inside the CC1101 valid bands
 * (300-348, 387-464, 779-928 MHz).  Shared by FOBscan on-device Up/Down tuning
 * and the Advanced Settings screen. */
const float FOBSCAN_FREQS[] = {
    300.00f, 303.87f, 310.00f, 315.00f, 318.00f, 330.00f,
    390.00f, 418.00f, 433.42f, 433.92f, 434.42f, 868.35f, 915.00f,
};
const int FOBSCAN_FREQ_COUNT = (int)(sizeof(FOBSCAN_FREQS) / sizeof(FOBSCAN_FREQS[0]));

#define FLIPPER_SETTINGS_PATH EXT_PATH("flipper_fobscan/settings.conf")

/* ── Advanced Settings persistence ────────────────────────────────────────── */
void flipper_adv_settings_load(FlipperApp* app) {
    /* Defaults. */
    app->adv.freq_idx         = FOBSCAN_FREQ_DEFAULT;
    app->adv.preset           = FlipperPresetOOK650;
    app->adv.squelch_dbm      = -90.0f;
    app->adv.force_proto      = FlipperForceAuto;
    app->adv.autosave_decoded = true;
    app->adv.autosave_raw     = true;
    app->adv.lib_evict_oldest = true;   /* default: drop oldest to make room */
    app->adv.dashboard_link   = false;  /* default OFF: saves ~9KB heap at launch */

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
                free(t);
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);

    /* Clamp. */
    if(app->adv.freq_idx < 0 || app->adv.freq_idx >= FOBSCAN_FREQ_COUNT)
        app->adv.freq_idx = FOBSCAN_FREQ_DEFAULT;
    /* preset/force_proto are unsigned enums (can't be < 0); an out-of-range or
       corrupted value reads as a large unsigned and is caught by the upper bound. */
    if((unsigned)app->adv.preset > 3)
        app->adv.preset = FlipperPresetOOK650;
    if((unsigned)app->adv.force_proto >= FlipperForceCount)
        app->adv.force_proto = FlipperForceAuto;
}

void flipper_adv_settings_save(FlipperApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FLIPPER_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
            "freq_idx=%d\npreset=%d\nsquelch=%d\nforce=%d\nsave_dec=%d\nsave_raw=%d\nevict=%d\nlink=%d\n",
            app->adv.freq_idx, (int)app->adv.preset, (int)app->adv.squelch_dbm,
            (int)app->adv.force_proto, app->adv.autosave_decoded ? 1 : 0,
            app->adv.autosave_raw ? 1 : 0, app->adv.lib_evict_oldest ? 1 : 0,
            app->adv.dashboard_link ? 1 : 0);
        storage_file_write(f, buf, n);
    }
    storage_file_close(f);
    storage_file_free(f);
}

/* ── Dashboard links (opt-in) ─────────────────────────────────────────────── */
void flipper_links_ensure(FlipperApp* app) {
    if(app->usb_link && app->uart_link) return;

    /* Treat the pair as one feature: never leave a USB worker running when
       the UART worker could not be created (or vice versa). */
    if(!app->usb_link) app->usb_link = flipper_link_alloc(app, FlipperLinkUsb);
    if(!app->usb_link) goto failed;
    if(!app->uart_link) app->uart_link = flipper_link_alloc(app, FlipperLinkUart);
    if(!app->uart_link) goto failed;
    return;

failed:
    flipper_links_release(app);
    /* Do not repeatedly retry a known failed allocation on this settings
       session.  The user can explicitly try again after returning to the
       menu or freeing heap elsewhere. */
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

    /* Emit a dashboard heartbeat about once per second so the connected
       React dashboard sees the device as alive across both links. */
    uint32_t now = furi_get_tick();
    if(now - app->hb_last_tick >= furi_ms_to_ticks(1000)) {
        app->hb_last_tick = now;
        char buf[160];
        uint32_t uptime_s = (now - app->boot_tick) / furi_ms_to_ticks(1000);
        size_t len = flipper_proto_emit_heartbeat(
            buf, sizeof(buf),
            app->capture->freq_mhz > 0 ? app->capture->freq_mhz : 433.92f,
            app->remote_scanning || app->gui_radio_active,
            100, uptime_s, true, 0);
        flipper_app_broadcast(app, buf, len);
    }
}

/* ── ViewDispatcher navigation / event callbacks ─────────────────────────── */
static bool flipper_nav_event_cb(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Back is a cancellation boundary. The scene's on_exit still performs the
       ordered HAL stop and ownership release after this marker. */
    flipper_capture_tx_cancel(app->capture, 0, FlipperTxCancelBack);
    return scene_manager_handle_back_event(app->scene_manager);
}

static bool flipper_custom_event_cb(void* ctx, uint32_t event) {
    FlipperApp* app = (FlipperApp*)ctx;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

/* No-op enter callback for the custom canvas views.  This firmware's
   view_enter path invokes the view's enter_callback; a raw view_alloc()
   with a NULL enter_callback faults on the first switch, so every custom
   view gets this stub. */
static void flipper_custom_view_enter_cb(void* ctx) {
    UNUSED(ctx);
}

/* A View draw callback is handed the view MODEL (not the context), so each
   custom canvas view needs a model that carries the FlipperApp pointer. */
static void flipper_custom_view_set_model(View* view, FlipperApp* app) {
    view_allocate_model(view, ViewModelTypeLockFree, sizeof(FlipperApp*));
    FlipperApp** m = view_get_model(view);
    *m = app;
    view_commit_model(view, false);
}

/* ── Guided-flow union lifecycle (heap-allocated off the FlipperApp block) ─── */
bool flipper_guided_ensure(FlipperApp* app, size_t bytes) {
    if(app->guided) return true;
    /* Allocate only what the entering flow needs, NOT sizeof(FlipperGuided).
       The union is dominated by FOBback's caps[] (~11 KB); making every guided
       flow demand that contiguous block OOM'd FOBclone (needs only ~4 KB) on
       lower-headroom entries.  bytes is clamped up to the caller's member size
       and down to the union size for safety. */
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

    /* Guided-flow union (FlipperGuided, ~11 KB at its largest) is NOT allocated
       here — it is lazily created only while a guided scene is active and freed
       on return to the main menu, so it is never resident during launch or
       normal FOBscan use. */
    app->guided        = NULL;

    /* Scene manager */
    app->scene_manager = scene_manager_alloc(&flipper_scene_handlers, app);

    /* View dispatcher */
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, flipper_nav_event_cb);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipper_custom_event_cb);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    /*
     * Register views.
     * FlipperViewMenu  — one Submenu reused for main menu + all list-style screens.
     * FlipperViewVarList — one VariableItemList reused for year pickers.
     * Four custom canvas views for the active/capture screens.
     */
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

    /* Remote-control defaults.  The dashboard links each spawn a 4 KB RX-thread
       stack (~9 KB heap total); allocating them unconditionally at launch was a
       major contributor to out-of-memory reboots.  They are now opt-in via the
       Advanced Settings "Dashboard Link" toggle, allocated last so everything
       their RX threads touch (flipper_app_handle_command) is already up. */
    app->boot_tick          = furi_get_tick();
    app->hb_last_tick       = app->boot_tick;
    app->remote_squelch_dbm = -90.0f;
    app->radio_mutex        = furi_mutex_alloc(FuriMutexTypeNormal);
    app->usb_link           = NULL;
    app->uart_link          = NULL;
    /* Never allocate dashboard workers during launch, even when an older
       settings file contains link=1.  Each worker reserves LINK_RX_STACK
       bytes from the app heap; the user can explicitly enable the link from
       Advanced Settings after the main UI is already visible. */

    return app;
}

/* ── Free ────────────────────────────────────────────────────────────────── */
static void flipper_app_free(FlipperApp* app) {
    furi_assert(app);

    /* Stop the periodic tick FIRST — it broadcasts heartbeats through both
       links, so it must not fire once the links are being freed. */
    furi_timer_stop(app->tick_timer);
    furi_timer_free(app->tick_timer);

    /* Cancel and quiesce TX before touching links.  The owner invokes the
       completion callback after publishing tx_work idle; wait_stopped also
       waits for that callback, so broadcasts cannot race link destruction. */
    flipper_capture_tx_cancel(app->capture, 0, FlipperTxCancelAppExit);
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    flipper_capture_tx_set_done_cb(app->capture, NULL, NULL);

    /* Only now stop/free link RX/TX transports; no completion callback can
       broadcast through them after this point. */
    flipper_links_release(app);
    furi_mutex_free(app->radio_mutex);

    flipper_capture_free(app->capture);
    flipper_guided_release(app);

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
