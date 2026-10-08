#include "../flipper_fobscan_app.h"

/* The main menu opens this screen on Back instead of exiting immediately. A key press stops the dispatcher and closes the app. */

void flipper_credits_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignCenter,
                            "FOBworks for Flipper");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter,
                            "See the full suite & tools");
    canvas_draw_str_aligned(canvas, 64, 43, AlignCenter, AlignCenter,
                            "at FOBworks.org");

    canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignCenter,
                            "Press any key to exit");
}

bool flipper_credits_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    /* I stop the dispatcher to exit; scene exit handlers still run during teardown. */
    if(e->type == InputTypeShort) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    return false;
}

void flipper_scene_credits_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewCredits);
}

bool flipper_scene_credits_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx); UNUSED(e);
    return false;
}

void flipper_scene_credits_on_exit(void* ctx) {
    UNUSED(ctx);
}
