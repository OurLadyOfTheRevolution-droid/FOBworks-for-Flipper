#include "../flipper_fobscan_app.h"

/* Scrollable detail view shared by FOBprotos and FOBLoq. The caller fills
 * app->info_title and app->info_body, with each displayed line separated by
 * '\n', then opens FlipperSceneInfo. */

void flipper_info_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, app->info_title);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    canvas_set_font(canvas, FontSecondary);
    /* Draw each info_body line in a 10-pixel row, starting at y=24. */
    const char* p = app->info_body;
    int y = 24;
    char line[48];
    while(*p && y <= 64) {
        int n = 0;
        while(p[n] && p[n] != '\n' && n < (int)sizeof(line) - 1) n++;
        memcpy(line, p, n);
        line[n] = '\0';
        canvas_draw_str(canvas, 0, y, line);
        y += 10;
        p += n;
        if(*p == '\n') p++;
    }
}

bool flipper_info_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(e->type == InputTypeShort && e->key == InputKeyBack) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void flipper_scene_info_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewInfo);
}

bool flipper_scene_info_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_info_on_exit(void* ctx) {
    UNUSED(ctx);
}
