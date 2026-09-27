#include "../flipper_fobscan_app.h"

/* Credits screen shown when the user backs out of the main menu (i.e. on the
 * way out of the app).  The main menu intercepts its Back event and pushes this
 * scene instead of exiting directly; any key press here stops the view
 * dispatcher, which ends the app run loop. */

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
    /* Any short press exits the app.  Stopping the dispatcher unwinds the run
       loop; scene on_exit handlers still fire during teardown. */
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
