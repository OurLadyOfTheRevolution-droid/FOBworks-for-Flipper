#pragma once

#include <gui/scene_manager.h>

/* ── Per-scene callback declarations ─────────────────────────────────────── */

void flipper_scene_main_menu_on_enter(void* ctx);
bool flipper_scene_main_menu_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_main_menu_on_exit(void* ctx);

void flipper_scene_fobscan_on_enter(void* ctx);
bool flipper_scene_fobscan_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobscan_on_exit(void* ctx);

void flipper_scene_fobclone_make_on_enter(void* ctx);
bool flipper_scene_fobclone_make_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobclone_make_on_exit(void* ctx);

void flipper_scene_fobclone_model_on_enter(void* ctx);
bool flipper_scene_fobclone_model_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobclone_model_on_exit(void* ctx);

void flipper_scene_fobclone_year_on_enter(void* ctx);
bool flipper_scene_fobclone_year_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobclone_year_on_exit(void* ctx);

void flipper_scene_fobclone_capture_on_enter(void* ctx);
bool flipper_scene_fobclone_capture_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobclone_capture_on_exit(void* ctx);

void flipper_scene_fobcatch_make_on_enter(void* ctx);
bool flipper_scene_fobcatch_make_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobcatch_make_on_exit(void* ctx);

void flipper_scene_fobcatch_model_on_enter(void* ctx);
bool flipper_scene_fobcatch_model_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobcatch_model_on_exit(void* ctx);

void flipper_scene_fobcatch_year_on_enter(void* ctx);
bool flipper_scene_fobcatch_year_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobcatch_year_on_exit(void* ctx);

void flipper_scene_fobcatch_active_on_enter(void* ctx);
bool flipper_scene_fobcatch_active_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobcatch_active_on_exit(void* ctx);

void flipper_scene_fobback_make_on_enter(void* ctx);
bool flipper_scene_fobback_make_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobback_make_on_exit(void* ctx);

void flipper_scene_fobback_model_on_enter(void* ctx);
bool flipper_scene_fobback_model_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobback_model_on_exit(void* ctx);

void flipper_scene_fobback_listen_on_enter(void* ctx);
bool flipper_scene_fobback_listen_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobback_listen_on_exit(void* ctx);

void flipper_scene_adv_settings_on_enter(void* ctx);
bool flipper_scene_adv_settings_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_adv_settings_on_exit(void* ctx);

void flipper_scene_library_on_enter(void* ctx);
bool flipper_scene_library_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_library_on_exit(void* ctx);

void flipper_scene_library_list_on_enter(void* ctx);
bool flipper_scene_library_list_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_library_list_on_exit(void* ctx);

void flipper_scene_library_item_on_enter(void* ctx);
bool flipper_scene_library_item_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_library_item_on_exit(void* ctx);

void flipper_scene_library_info_on_enter(void* ctx);
bool flipper_scene_library_info_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_library_info_on_exit(void* ctx);

void flipper_scene_libscope_on_enter(void* ctx);
bool flipper_scene_libscope_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_libscope_on_exit(void* ctx);

void flipper_scene_fobsweep_on_enter(void* ctx);
bool flipper_scene_fobsweep_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobsweep_on_exit(void* ctx);

void flipper_scene_fobprotos_on_enter(void* ctx);
bool flipper_scene_fobprotos_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobprotos_on_exit(void* ctx);

void flipper_scene_fobloq_on_enter(void* ctx);
bool flipper_scene_fobloq_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobloq_on_exit(void* ctx);


void flipper_scene_info_on_enter(void* ctx);
bool flipper_scene_info_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_info_on_exit(void* ctx);

void flipper_scene_credits_on_enter(void* ctx);
bool flipper_scene_credits_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_credits_on_exit(void* ctx);

void flipper_scene_fobpwn_setup_on_enter(void* ctx);
bool flipper_scene_fobpwn_setup_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobpwn_setup_on_exit(void* ctx);
void flipper_scene_fobpwn_run_on_enter(void* ctx);
bool flipper_scene_fobpwn_run_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobpwn_run_on_exit(void* ctx);

void flipper_scene_fobwatch_on_enter(void* ctx);
bool flipper_scene_fobwatch_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobwatch_on_exit(void* ctx);
void flipper_scene_foblabs_on_enter(void* ctx);
bool flipper_scene_foblabs_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_foblabs_on_exit(void* ctx);
void flipper_scene_fobhunt_on_enter(void* ctx);
bool flipper_scene_fobhunt_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobhunt_on_exit(void* ctx);

void flipper_scene_fobcrack_on_enter(void* ctx);
bool flipper_scene_fobcrack_on_event(void* ctx, SceneManagerEvent e);
void flipper_scene_fobcrack_on_exit(void* ctx);

/* ── Handler tables (typed correctly for SceneManagerHandlers) ───────────── */

static const AppSceneOnEnterCallback flipper_on_enter_handlers[FlipperSceneCount] = {
    [FlipperSceneMainMenu]        = flipper_scene_main_menu_on_enter,
    [FlipperSceneFobscan]         = flipper_scene_fobscan_on_enter,
    [FlipperSceneFobcloneMake]    = flipper_scene_fobclone_make_on_enter,
    [FlipperSceneFobcloneModel]   = flipper_scene_fobclone_model_on_enter,
    [FlipperSceneFobcloneYear]    = flipper_scene_fobclone_year_on_enter,
    [FlipperSceneFobcloneCapture] = flipper_scene_fobclone_capture_on_enter,
    [FlipperSceneFobcatchMake]    = flipper_scene_fobcatch_make_on_enter,
    [FlipperSceneFobcatchModel]   = flipper_scene_fobcatch_model_on_enter,
    [FlipperSceneFobcatchYear]    = flipper_scene_fobcatch_year_on_enter,
    [FlipperSceneFobcatchActive]  = flipper_scene_fobcatch_active_on_enter,
    [FlipperSceneFobbackMake]     = flipper_scene_fobback_make_on_enter,
    [FlipperSceneFobbackModel]    = flipper_scene_fobback_model_on_enter,
    [FlipperSceneFobbackListen]   = flipper_scene_fobback_listen_on_enter,
    [FlipperSceneAdvSettings]     = flipper_scene_adv_settings_on_enter,
    [FlipperSceneLibrary]         = flipper_scene_library_on_enter,
    [FlipperSceneLibraryList]     = flipper_scene_library_list_on_enter,
    [FlipperSceneLibraryItem]     = flipper_scene_library_item_on_enter,
    [FlipperSceneLibraryInfo]     = flipper_scene_library_info_on_enter,
    [FlipperSceneLibScope]        = flipper_scene_libscope_on_enter,
    [FlipperSceneFobsweep]        = flipper_scene_fobsweep_on_enter,
    [FlipperSceneFobprotos]       = flipper_scene_fobprotos_on_enter,
    [FlipperSceneFobLoq]          = flipper_scene_fobloq_on_enter,
    [FlipperSceneInfo]            = flipper_scene_info_on_enter,
    [FlipperSceneCredits]         = flipper_scene_credits_on_enter,
    [FlipperSceneFobpwnSetup]     = flipper_scene_fobpwn_setup_on_enter,
    [FlipperSceneFobpwnRun]       = flipper_scene_fobpwn_run_on_enter,
    [FlipperSceneFobwatch]        = flipper_scene_fobwatch_on_enter,
    [FlipperSceneFoblabs]         = flipper_scene_foblabs_on_enter,
    [FlipperSceneFobhunt]         = flipper_scene_fobhunt_on_enter,
    [FlipperSceneFobcrack]        = flipper_scene_fobcrack_on_enter,
};

static const AppSceneOnEventCallback flipper_on_event_handlers[FlipperSceneCount] = {
    [FlipperSceneMainMenu]        = flipper_scene_main_menu_on_event,
    [FlipperSceneFobscan]         = flipper_scene_fobscan_on_event,
    [FlipperSceneFobcloneMake]    = flipper_scene_fobclone_make_on_event,
    [FlipperSceneFobcloneModel]   = flipper_scene_fobclone_model_on_event,
    [FlipperSceneFobcloneYear]    = flipper_scene_fobclone_year_on_event,
    [FlipperSceneFobcloneCapture] = flipper_scene_fobclone_capture_on_event,
    [FlipperSceneFobcatchMake]    = flipper_scene_fobcatch_make_on_event,
    [FlipperSceneFobcatchModel]   = flipper_scene_fobcatch_model_on_event,
    [FlipperSceneFobcatchYear]    = flipper_scene_fobcatch_year_on_event,
    [FlipperSceneFobcatchActive]  = flipper_scene_fobcatch_active_on_event,
    [FlipperSceneFobbackMake]     = flipper_scene_fobback_make_on_event,
    [FlipperSceneFobbackModel]    = flipper_scene_fobback_model_on_event,
    [FlipperSceneFobbackListen]   = flipper_scene_fobback_listen_on_event,
    [FlipperSceneAdvSettings]     = flipper_scene_adv_settings_on_event,
    [FlipperSceneLibrary]         = flipper_scene_library_on_event,
    [FlipperSceneLibraryList]     = flipper_scene_library_list_on_event,
    [FlipperSceneLibraryItem]     = flipper_scene_library_item_on_event,
    [FlipperSceneLibraryInfo]     = flipper_scene_library_info_on_event,
    [FlipperSceneLibScope]        = flipper_scene_libscope_on_event,
    [FlipperSceneFobsweep]        = flipper_scene_fobsweep_on_event,
    [FlipperSceneFobprotos]       = flipper_scene_fobprotos_on_event,
    [FlipperSceneFobLoq]          = flipper_scene_fobloq_on_event,
    [FlipperSceneInfo]            = flipper_scene_info_on_event,
    [FlipperSceneCredits]         = flipper_scene_credits_on_event,
    [FlipperSceneFobpwnSetup]     = flipper_scene_fobpwn_setup_on_event,
    [FlipperSceneFobpwnRun]       = flipper_scene_fobpwn_run_on_event,
    [FlipperSceneFobwatch]        = flipper_scene_fobwatch_on_event,
    [FlipperSceneFoblabs]         = flipper_scene_foblabs_on_event,
    [FlipperSceneFobhunt]         = flipper_scene_fobhunt_on_event,
    [FlipperSceneFobcrack]        = flipper_scene_fobcrack_on_event,
};

static const AppSceneOnExitCallback flipper_on_exit_handlers[FlipperSceneCount] = {
    [FlipperSceneMainMenu]        = flipper_scene_main_menu_on_exit,
    [FlipperSceneFobscan]         = flipper_scene_fobscan_on_exit,
    [FlipperSceneFobcloneMake]    = flipper_scene_fobclone_make_on_exit,
    [FlipperSceneFobcloneModel]   = flipper_scene_fobclone_model_on_exit,
    [FlipperSceneFobcloneYear]    = flipper_scene_fobclone_year_on_exit,
    [FlipperSceneFobcloneCapture] = flipper_scene_fobclone_capture_on_exit,
    [FlipperSceneFobcatchMake]    = flipper_scene_fobcatch_make_on_exit,
    [FlipperSceneFobcatchModel]   = flipper_scene_fobcatch_model_on_exit,
    [FlipperSceneFobcatchYear]    = flipper_scene_fobcatch_year_on_exit,
    [FlipperSceneFobcatchActive]  = flipper_scene_fobcatch_active_on_exit,
    [FlipperSceneFobbackMake]     = flipper_scene_fobback_make_on_exit,
    [FlipperSceneFobbackModel]    = flipper_scene_fobback_model_on_exit,
    [FlipperSceneFobbackListen]   = flipper_scene_fobback_listen_on_exit,
    [FlipperSceneAdvSettings]     = flipper_scene_adv_settings_on_exit,
    [FlipperSceneLibrary]         = flipper_scene_library_on_exit,
    [FlipperSceneLibraryList]     = flipper_scene_library_list_on_exit,
    [FlipperSceneLibraryItem]     = flipper_scene_library_item_on_exit,
    [FlipperSceneLibraryInfo]     = flipper_scene_library_info_on_exit,
    [FlipperSceneLibScope]        = flipper_scene_libscope_on_exit,
    [FlipperSceneFobsweep]        = flipper_scene_fobsweep_on_exit,
    [FlipperSceneFobprotos]       = flipper_scene_fobprotos_on_exit,
    [FlipperSceneFobLoq]          = flipper_scene_fobloq_on_exit,
    [FlipperSceneInfo]            = flipper_scene_info_on_exit,
    [FlipperSceneCredits]         = flipper_scene_credits_on_exit,
    [FlipperSceneFobpwnSetup]     = flipper_scene_fobpwn_setup_on_exit,
    [FlipperSceneFobpwnRun]       = flipper_scene_fobpwn_run_on_exit,
    [FlipperSceneFobwatch]        = flipper_scene_fobwatch_on_exit,
    [FlipperSceneFoblabs]         = flipper_scene_foblabs_on_exit,
    [FlipperSceneFobhunt]         = flipper_scene_fobhunt_on_exit,
    [FlipperSceneFobcrack]        = flipper_scene_fobcrack_on_exit,
};

static const SceneManagerHandlers flipper_scene_handlers = {
    .on_enter_handlers = flipper_on_enter_handlers,
    .on_event_handlers = flipper_on_event_handlers,
    .on_exit_handlers  = flipper_on_exit_handlers,
    .scene_num         = FlipperSceneCount,
};
