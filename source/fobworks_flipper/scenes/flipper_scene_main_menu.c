#include "../flipper_fobscan_app.h"

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
    MainMenuLibrary,
    MainMenuAdvSettings,
    MainMenuFobpwn,
} MainMenuItem;

static void main_menu_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    switch((MainMenuItem)idx) {
    case MainMenuFobscan:
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
    /* Release guided-flow memory when returning to the menu. */
    flipper_guided_release(app);
    submenu_reset(app->submenu);
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
