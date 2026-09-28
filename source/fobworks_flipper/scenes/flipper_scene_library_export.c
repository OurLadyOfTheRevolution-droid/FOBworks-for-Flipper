#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Export decoded signals as CSV or JSON, raw signals as CSV, or only
   favorites as CSV. */
/* ─────────────────────────────────────────────────────────────────────────── */

enum { ExportActCsvDecoded = 0, ExportActJsonDecoded, ExportActCsvRaw, ExportActCsvFavorites, ExportActDone };

static void export_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    bool ok = false;

    switch(idx) {
    case ExportActCsvDecoded:
        ok = flipper_lib_export_csv(app->storage, false, NULL, NULL);
        break;
    case ExportActJsonDecoded:
        ok = flipper_lib_export_json(app->storage, false, NULL, NULL);
        break;
    case ExportActCsvRaw:
        /* The raw menu item also uses the CSV export handler. */
        ok = flipper_lib_export_csv(app->storage, false, NULL, NULL);
        break;
    case ExportActCsvFavorites:
        ok = flipper_lib_export_csv(app->storage, true, NULL, NULL);
        break;
    case ExportActDone:
        scene_manager_previous_scene(app->scene_manager);
        return;
    default:
        break;
    }

    notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
}

void flipper_scene_library_export_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_ensure_submenu(app); submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Library Export");
    submenu_add_item(app->submenu, "CSV (decoded)", ExportActCsvDecoded, export_cb, app);
    submenu_add_item(app->submenu, "JSON (decoded)", ExportActJsonDecoded, export_cb, app);
    submenu_add_item(app->submenu, "CSV (raw)", ExportActCsvRaw, export_cb, app);
    submenu_add_item(app->submenu, "CSV (favorites only)", ExportActCsvFavorites, export_cb, app);
    submenu_add_item(app->submenu, "Back", ExportActDone, export_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_library_export_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_library_export_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    flipper_ensure_submenu(app); submenu_reset(app->submenu);
}
