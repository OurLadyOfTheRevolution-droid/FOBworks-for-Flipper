#include "../flipper_fobscan_app.h"
#include "../protocol/flipper_keeloq.h"
#include <stdio.h>

/* FOBLoq — KeeLoq manufacturer key store.  Paged to 8 names per screen so
 * the Flipper heap doesn't OOM on launch (73 items at once is too many). */

#define FOBLOQ_PAGE 8
#define FOBLOQ_MORE 0xF000
#define FOBLOQ_BACK 0xF001

static int s_page = 0;

static void fobloq_show(FlipperApp* app);

static void fobloq_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(idx == FOBLOQ_MORE) {
        s_page++;
        fobloq_show(app);
        return;
    }
    if(idx == FOBLOQ_BACK) {
        s_page--;
        fobloq_show(app);
        return;
    }
    if((int)idx >= N_MFR_KEYS) return;
    const MfrKey* k = &FLIPPER_MFR_KEYS[idx];
    snprintf(app->info_title, sizeof(app->info_title), "%s", k->name);
    /* The table stores each key masked, so this panel shows the STORED value, not the key
       itself. Labelled as such: an unqualified "HEX:" would read as the real key and invite
       someone to copy it down, which is exactly what masking is meant to stop. */
    snprintf(app->info_body, sizeof(app->info_body),
             "Manufacturer key\n\nSTORED (masked):\n%08lX%08lX\n\nUsed by KeeLoq decode\n+ key recovery.",
             (unsigned long)(uint32_t)(k->key >> 32),
             (unsigned long)(uint32_t)(k->key & 0xFFFFFFFFu));
    scene_manager_next_scene(app->scene_manager, FlipperSceneInfo);
}

static void fobloq_show(FlipperApp* app) {
    submenu_reset(app->submenu);
    int pages = (N_MFR_KEYS + FOBLOQ_PAGE - 1) / FOBLOQ_PAGE;
    if(s_page < 0) s_page = 0;
    if(s_page >= pages) s_page = pages - 1;
    int start = s_page * FOBLOQ_PAGE;
    int end = start + FOBLOQ_PAGE;
    if(end > N_MFR_KEYS) end = N_MFR_KEYS;
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "FOBLoq  %d/%d", s_page + 1, pages);
    submenu_set_header(app->submenu, hdr);
    for(int i = start; i < end; i++)
        submenu_add_item(app->submenu, FLIPPER_MFR_KEYS[i].name, (uint32_t)i, fobloq_cb, app);
    if(end < N_MFR_KEYS)
        submenu_add_item(app->submenu, "More \xe2\x80\x94>", FOBLOQ_MORE, fobloq_cb, app);
    if(start > 0)
        submenu_add_item(app->submenu, "\xe2\x80\x94< Previous", FOBLOQ_BACK, fobloq_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

void flipper_scene_fobloq_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    s_page = 0;
    fobloq_show(app);
}

bool flipper_scene_fobloq_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_fobloq_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}
