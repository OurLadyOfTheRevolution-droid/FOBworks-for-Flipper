#include "../flipper_fobscan_app.h"
#include "../protocol/flipper_plugin.h"

typedef enum {
    MainMenuFobscan = 0,
    MainMenuFobclone,
    MainMenuFobcatch,
    MainMenuFobback,
    MainMenuFobsweep,
    MainMenuFobprotos,
    MainMenuFobLoq,
    MainMenuFobwatch,
    MainMenuFoblabs,
    MainMenuFobhunt,
    MainMenuFobcrack,
    MainMenuFobreport,
    MainMenuFobfreq,
    MainMenuFobtrack,
    MainMenuGrollback,
    MainMenuLibrary,
    MainMenuAdvSettings,
    MainMenuFobpwn,
#ifdef FOBSCAN_STARTUP_PROBE
    MainMenuProbeDisplayOnly,
    MainMenuProbeRxOnly,
#endif
} MainMenuItem;

static void main_menu_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    switch((MainMenuItem)idx) {
#ifdef FOBSCAN_STARTUP_PROBE
    case MainMenuProbeDisplayOnly:
        app->fobscan.probe_mode = FobscanProbeDisplayOnly;
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobscan);
        break;
    case MainMenuProbeRxOnly:
        app->fobscan.probe_mode = FobscanProbeRxOnly;
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobscan);
        break;
#endif
    case MainMenuFobscan:
#ifdef FOBSCAN_STARTUP_PROBE
        app->fobscan.probe_mode = FobscanProbeFull;
#endif
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobscan);
        break;
    case MainMenuFobclone:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobcloneMake);
        break;
    case MainMenuFobcatch:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobcatchMake);
        break;
    case MainMenuFobback:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobbackMake);
        break;
    case MainMenuFobsweep:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobsweep);
        break;
    case MainMenuFobprotos:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobprotos);
        break;
    case MainMenuFobLoq:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobLoq);
        break;
    case MainMenuFobwatch:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobwatch);
        break;
    case MainMenuFoblabs:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFoblabs);
        break;
    case MainMenuFobhunt:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobhunt);
        break;
    case MainMenuFobcrack:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobcrack);
        break;
    case MainMenuFobreport:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobreport);
        break;
    case MainMenuFobfreq:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobfreq);
        break;
    case MainMenuFobtrack:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobtrack);
        break;
    case MainMenuGrollback:
        scene_manager_next_scene(app->scene_manager, FlipperSceneGrollback);
        break;
    case MainMenuLibrary:
        scene_manager_next_scene(app->scene_manager, FlipperSceneLibrary);
        break;
    case MainMenuAdvSettings:
        scene_manager_next_scene(app->scene_manager, FlipperSceneAdvSettings);
        break;
    case MainMenuFobpwn:
        scene_manager_next_scene(app->scene_manager, FlipperSceneFobpwnSetup);
        break;
    default:
        break;
    }
}

void flipper_scene_main_menu_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Clear borrowed catalog labels before the mapping is released. */
    submenu_reset(app->submenu);
    /* Release guided-flow memory and unmap catalog/force FALs when idle. */
    flipper_guided_release(app);
    flipper_plugin_unload_all();
#ifdef FOBSCAN_STARTUP_PROBE
    /* A receive-only probe build: no links/settings or transmit-mode menu. Keep normal production navigation unchanged when the define is absent. */
    submenu_set_header(app->submenu, "FOBscan startup probe");
    submenu_add_item(app->submenu, "1. Display only (no RX)",
                     MainMenuProbeDisplayOnly, main_menu_cb, app);
    submenu_add_item(app->submenu, "2. RX only (standard UI)",
                     MainMenuProbeRxOnly, main_menu_cb, app);
    submenu_add_item(app->submenu, "3. Normal FOBscan",
                     MainMenuFobscan, main_menu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
    return;
#endif
    submenu_set_header(app->submenu, "FOBworks  v" FLIPPER_VERSION);
    submenu_add_item(app->submenu, "FOBscan   — Live capture",    MainMenuFobscan,   main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBclone  — Guided clone",    MainMenuFobclone,  main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBcatch  — Jam & replay",    MainMenuFobcatch,  main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBback   — RollBack resync", MainMenuFobback,   main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBsweep  — Signal levels",   MainMenuFobsweep,  main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBprotos — Protocol ref",    MainMenuFobprotos, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBLoq    — KeeLoq keys",     MainMenuFobLoq,    main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBwatch — Receive & save",  MainMenuFobwatch, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBlabs  — Timing metrics",  MainMenuFoblabs,  main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBhunt  — RSSI range sweep", MainMenuFobhunt, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBcrack  — KeeLoq search", MainMenuFobcrack, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBreport — Health grade",  MainMenuFobreport, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBfreq  — Crystal fingerprint", MainMenuFobfreq, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBtrack — TPMS↔RKE link", MainMenuFobtrack, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBroll  — RollBack analyzer", MainMenuGrollback, main_menu_cb, app);
    submenu_add_item(app->submenu, "Library",                     MainMenuLibrary,   main_menu_cb, app);
    submenu_add_item(app->submenu, "Advanced Settings",           MainMenuAdvSettings, main_menu_cb, app);
    submenu_add_item(app->submenu, "FOBpwn — Honda RollBack", MainMenuFobpwn, main_menu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_main_menu_on_event(void* ctx, SceneManagerEvent e) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* Show credits on Back; a key press there exits the app. */
    if(e.type == SceneManagerEventTypeBack) {
        scene_manager_next_scene(app->scene_manager, FlipperSceneCredits);
        return true;
    }
    return false;
}

void flipper_scene_main_menu_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}
