#include "../flipper_fobscan_app.h"
#include "../protocol/flipper_radio_loader.h"
#include <furi_hal_random.h>
#include <stdio.h>

/* Advanced Settings uses the shared VariableItemList for FOBscan's frequency,
 * modulation, squelch, forced-protocol, and auto-save options. Changes update
 * app->adv and are saved when the screen closes. */

#define SQUELCH_COUNT 11   /* -50 .. -100 dBm, 5 dB steps */
static int squelch_from_index(int i) { return -50 - i * 5; }
static int squelch_to_index(float dbm) {
    int idx = (int)((-50 - (int)dbm) / 5);
    if(idx < 0) idx = 0;
    if(idx >= SQUELCH_COUNT) idx = SQUELCH_COUNT - 1;
    return idx;
}

static const char* preset_short(FlipperPreset p) {
    switch(p) {
    case FlipperPresetOOK270:     return "OOK 270k";
    case FlipperPreset2FSKDev238: return "2FSK 24k";
    case FlipperPreset2FSKDev476: return "2FSK 48k";
    case FlipperPresetOOK650:
    default:                      return "OOK 650k";
    }
}

/* ── Change callbacks ─────────────────────────────────────────────────────── */
static void on_freq(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->adv.freq_idx = i;
    char t[16];
    snprintf(t, sizeof(t), "%.2f", (double)FOBSCAN_FREQS[i]);
    variable_item_set_current_value_text(item, t);
}

static void on_mod(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->adv.preset = (FlipperPreset)i;
    variable_item_set_current_value_text(item, preset_short(app->adv.preset));
}

static void on_squelch(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->adv.squelch_dbm = (float)squelch_from_index(i);
    char t[12];
    snprintf(t, sizeof(t), "%d dBm", squelch_from_index(i));
    variable_item_set_current_value_text(item, t);
}

static void on_force(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->adv.force_proto = (FlipperForceProto)i;
    variable_item_set_current_value_text(item, flipper_force_proto_name(app->adv.force_proto));
}

static void on_save_dec(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    app->adv.autosave_decoded = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item, app->adv.autosave_decoded ? "On" : "Off");
}

static void on_save_raw(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    app->adv.autosave_raw = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item, app->adv.autosave_raw ? "On" : "Off");
}

static void on_lib_full(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    app->adv.lib_evict_oldest = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item,
        app->adv.lib_evict_oldest ? "Evict Oldest" : "No Vacancy");
}

static void on_link(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    app->adv.dashboard_link = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item, app->adv.dashboard_link ? "On" : "Off");
}

static void on_link_auth(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    if(i == 0) {
        app->adv.access_code[0] = '\0';
        variable_item_set_current_value_text(item, "Off");
    } else {
        /* Fresh 4-digit gate each time Auth is turned on. Never install the old
           lab default 0000. Rotate later over the link with
           {"cmd":"auth","code":"<current>","new_code":"...."}. */
        uint32_t n = 1000u + (furi_hal_random_get() % 9000u);
        snprintf(app->adv.access_code, sizeof(app->adv.access_code), "%04lu",
                 (unsigned long)n);
        variable_item_set_current_value_text(item, app->adv.access_code);
    }
}

static void on_ext_radio(VariableItem* item) {
    FlipperApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    if(i == 0) {
        radio_loader_set(RadioDeviceInternal);
        app->adv.prefer_external = false;
        variable_item_set_current_value_text(item, "Internal");
        return;
    }
    if(radio_loader_set(RadioDeviceExternal) && radio_loader_is_connected()) {
        app->adv.prefer_external = true;
        variable_item_set_current_value_text(item, "External");
    } else {
        radio_loader_set(RadioDeviceInternal);
        app->adv.prefer_external = false;
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "No EXT");
    }
}

/* ── Scene lifecycle ──────────────────────────────────────────────────────── */
void flipper_scene_adv_settings_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    VariableItemList* vl = app->var_list;
    variable_item_list_reset(vl);

    VariableItem* it;
    char t[16];

    it = variable_item_list_add(vl, "Frequency", FOBSCAN_FREQ_COUNT, on_freq, app);
    variable_item_set_current_value_index(it, app->adv.freq_idx);
    snprintf(t, sizeof(t), "%.2f", (double)FOBSCAN_FREQS[app->adv.freq_idx]);
    variable_item_set_current_value_text(it, t);

    it = variable_item_list_add(vl, "Modulation", 4, on_mod, app);
    variable_item_set_current_value_index(it, (uint8_t)app->adv.preset);
    variable_item_set_current_value_text(it, preset_short(app->adv.preset));

    it = variable_item_list_add(vl, "RSSI Squelch", SQUELCH_COUNT, on_squelch, app);
    variable_item_set_current_value_index(it, squelch_to_index(app->adv.squelch_dbm));
    snprintf(t, sizeof(t), "%d dBm", (int)app->adv.squelch_dbm);
    variable_item_set_current_value_text(it, t);

    it = variable_item_list_add(vl, "Force Proto", FlipperForceCount, on_force, app);
    variable_item_set_current_value_index(it, (uint8_t)app->adv.force_proto);
    variable_item_set_current_value_text(it, flipper_force_proto_name(app->adv.force_proto));

    it = variable_item_list_add(vl, "Save Decoded", 2, on_save_dec, app);
    variable_item_set_current_value_index(it, app->adv.autosave_decoded ? 1 : 0);
    variable_item_set_current_value_text(it, app->adv.autosave_decoded ? "On" : "Off");

    it = variable_item_list_add(vl, "Save Raw", 2, on_save_raw, app);
    variable_item_set_current_value_index(it, app->adv.autosave_raw ? 1 : 0);
    variable_item_set_current_value_text(it, app->adv.autosave_raw ? "On" : "Off");

    it = variable_item_list_add(vl, "Library Full", 2, on_lib_full, app);
    variable_item_set_current_value_index(it, app->adv.lib_evict_oldest ? 1 : 0);
    variable_item_set_current_value_text(it,
        app->adv.lib_evict_oldest ? "Evict Oldest" : "No Vacancy");

    it = variable_item_list_add(vl, "Dashboard Link", 2, on_link, app);
    variable_item_set_current_value_index(it, app->adv.dashboard_link ? 1 : 0);
    variable_item_set_current_value_text(it, app->adv.dashboard_link ? "On" : "Off");

    it = variable_item_list_add(vl, "Link Auth", 2, on_link_auth, app);
    variable_item_set_current_value_index(it, app->adv.access_code[0] ? 1 : 0);
    variable_item_set_current_value_text(
        it, app->adv.access_code[0] ? app->adv.access_code : "Off");

    it = variable_item_list_add(vl, "CC1101", 2, on_ext_radio, app);
    variable_item_set_current_value_index(it, app->adv.prefer_external ? 1 : 0);
    variable_item_set_current_value_text(it, app->adv.prefer_external ? "External" : "Internal");

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewVarList);
}

bool flipper_scene_adv_settings_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_adv_settings_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    variable_item_list_reset(app->var_list);
    flipper_adv_settings_save(app);
    /* Match the allocated links to the final toggle value. */
    if(app->adv.dashboard_link) flipper_links_ensure(app);
    else                        flipper_links_release(app);
}
