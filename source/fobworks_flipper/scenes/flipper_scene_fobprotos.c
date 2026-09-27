#include "../flipper_fobscan_app.h"
#include <stdio.h>
#include <string.h>

/* FOBprotos — supported-protocol reference.  The web dashboard "Protocols" tab
 * as its own utility: a browsable list of the decoders this build ships, each
 * with a short description shown in the shared info screen.  Decoding itself
 * happens live in FOBscan / in the Library (Force Protocol in Advanced
 * Settings); this screen is the catalog. */

typedef struct {
    const char* name;
    const char* desc;   /* '\n'-separated lines for the detail screen */
} ProtoRef;

static const ProtoRef PROTOS[] = {
    {"KeeLoq-HCS300", "Microchip rolling code.\n66-bit frame, 28-bit SN.\nDecoded via FOBLoq\nkey table."},
    {"Security+ 2.0",  "Chamberlain/LiftMaster.\nTernary rolling code.\nModern garage doors."},
    {"Security+ 1.0",  "Chamberlain/LiftMaster.\nLegacy rolling code.\n40-bit."},
    {"DoorHan-Rolling","DoorHan gate/garage.\nKeeLoq-family rolling."},
    {"CAME-12",        "CAME 12-bit fixed code.\nGates/barriers."},
    {"Nice-FLO",       "Nice FLO fixed code.\n12/24-bit."},
    {"Holtek-HT6P20",  "Holtek encoder.\nFixed-code remotes."},
    {"FAAC-SLH",       "FAAC SLH rolling code.\nSelf-learning."},
    {"Ansonic-12",     "Ansonic 12-bit fixed.\n433/868 remotes."},
    {"Linear-10",      "Linear 10-bit fixed.\nMulti-Code."},
};
#define PROTO_COUNT ((int)(sizeof(PROTOS) / sizeof(PROTOS[0])))

static void protos_cb(void* ctx, uint32_t idx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if((int)idx >= PROTO_COUNT) return;
    snprintf(app->info_title, sizeof(app->info_title), "%s", PROTOS[idx].name);
    snprintf(app->info_body, sizeof(app->info_body), "%s", PROTOS[idx].desc);
    scene_manager_next_scene(app->scene_manager, FlipperSceneInfo);
}

void flipper_scene_fobprotos_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "FOBprotos");
    for(int i = 0; i < PROTO_COUNT; i++)
        submenu_add_item(app->submenu, PROTOS[i].name, i, protos_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewMenu);
}

bool flipper_scene_fobprotos_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_fobprotos_on_exit(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    submenu_reset(app->submenu);
}
