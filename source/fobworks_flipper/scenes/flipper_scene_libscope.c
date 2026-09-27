#include "../flipper_fobscan_app.h"
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

enum {
    ScopePlotLeft = 2,
    ScopePlotRight = 125,
    ScopeHighY = 29,
    ScopeLowY = 50,
};

static const int scope_windows[] = {512, 256, 128, 64, 32, 16};

static int scope_window(const FlipperApp* app) {
    int z = app->lib_scope_zoom;
    if(z < 0) z = 0;
    if(z >= (int)(sizeof(scope_windows) / sizeof(scope_windows[0])))
        z = (int)(sizeof(scope_windows) / sizeof(scope_windows[0])) - 1;
    return scope_windows[z];
}

static void scope_clamp(FlipperApp* app) {
    int len = app->lib_sel.pulses.len;
    int window = scope_window(app);
    int max_offset = len > window ? len - window : 0;
    if(app->lib_scope_offset < 0) app->lib_scope_offset = 0;
    if(app->lib_scope_offset > max_offset) app->lib_scope_offset = max_offset;
}

static void scope_load_index(FlipperApp* app, int index) {
    if(index < 0 || index >= app->lib_entry_count) return;
    const char* name = app->lib_entries[index].name;
    if(!flipper_lib_load_with_protocol(
           app->storage, app->lib_sel_decoded, name, &app->lib_sel,
           &app->lib_sel_preset, app->lib_sel_protocol,
           sizeof(app->lib_sel_protocol))) {
        notification_message(app->notifications, &sequence_error);
        return;
    }
    strncpy(app->lib_sel_name, name, sizeof(app->lib_sel_name) - 1);
    app->lib_sel_name[sizeof(app->lib_sel_name) - 1] = '\0';
    app->lib_scope_index = index;
    app->lib_scope_offset = 0;
    scope_clamp(app);
}

static void scope_step_signal(FlipperApp* app, int delta) {
    if(app->lib_entry_count <= 1) {
        notification_message(app->notifications, &sequence_error);
        return;
    }
    int index = app->lib_scope_index;
    if(index < 0 || index >= app->lib_entry_count)
        index = delta > 0 ? -1 : 0;
    index = (index + delta + app->lib_entry_count) % app->lib_entry_count;
    scope_load_index(app, index);
}

void flipper_libscope_draw_cb(Canvas* canvas, void* model) {
    FlipperApp* app = *(FlipperApp**)model;
    const FlipperPulseBuf* pulses = &app->lib_sel.pulses;
    int len = pulses->len;
    int start;
    int end;
    uint64_t total = 0;
    uint64_t elapsed = 0;
    int level = ScopeHighY;
    char line[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "LibScope");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 65, 10, "L/R files");
    canvas_draw_line(canvas, 0, 13, 127, 13);
    snprintf(line, sizeof(line), "%.2f MHz  %.24s",
             (double)pulses->freq_mhz, app->lib_sel_name);
    canvas_draw_str(canvas, 0, 23, line);

    if(len < 2 || len > FLIPPER_PULSE_MAX) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 32, "No saved waveform");
        canvas_draw_str(canvas, 0, 48, "[Back] to library");
        return;
    }

    scope_clamp(app);
    start = app->lib_scope_offset;
    end = start + scope_window(app);
    if(end > len) end = len;
    for(int i = start; i < end; i++) total += pulses->durations[i];
    if(total == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 32, "Empty pulse window");
        canvas_draw_str(canvas, 0, 48, "[Back] to library");
        return;
    }

    canvas_draw_line(canvas, ScopePlotLeft, ScopeHighY,
                     ScopePlotRight, ScopeHighY);
    canvas_draw_line(canvas, ScopePlotLeft, ScopeLowY,
                     ScopePlotRight, ScopeLowY);
    int x = ScopePlotLeft;
    for(int i = start; i < end; i++) {
        int next_x;
        elapsed += pulses->durations[i];
        next_x = ScopePlotLeft +
            (int)((elapsed * (ScopePlotRight - ScopePlotLeft)) / total);
        if(next_x > ScopePlotRight) next_x = ScopePlotRight;
        canvas_draw_line(canvas, x, level, next_x, level);
        level = (level == ScopeHighY) ? ScopeLowY : ScopeHighY;
        canvas_draw_line(canvas, next_x, level == ScopeHighY ? ScopeLowY : ScopeHighY,
                         next_x, level);
        x = next_x;
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "E%d-%d/%d U/D zoom OK pan",
             start + 1, end, len);
    canvas_draw_str(canvas, 0, 63, line);
}

bool flipper_libscope_input_cb(InputEvent* e, void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(e->type != InputTypeShort) return false;
    switch(e->key) {
    case InputKeyBack:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case InputKeyLeft:
        scope_step_signal(app, -1);
        return true;
    case InputKeyRight:
        scope_step_signal(app, 1);
        return true;
    case InputKeyUp:
        if(app->lib_scope_zoom + 1 <
           (int)(sizeof(scope_windows) / sizeof(scope_windows[0]))) {
            app->lib_scope_zoom++;
            scope_clamp(app);
        }
        return true;
    case InputKeyDown:
        if(app->lib_scope_zoom > 0) app->lib_scope_zoom--;
        scope_clamp(app);
        return true;
    case InputKeyOk:
        app->lib_scope_offset += scope_window(app) / 4;
        if(app->lib_scope_offset >= app->lib_sel.pulses.len)
            app->lib_scope_offset = 0;
        scope_clamp(app);
        return true;
    default:
        return false;
    }
}

void flipper_scene_libscope_on_enter(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    app->lib_entry_count = flipper_lib_list(
        app->storage, app->lib_sel_decoded, app->lib_entries,
        FLIPPER_LIB_LIST_MAX);
    app->lib_scope_index = -1;
    for(int i = 0; i < app->lib_entry_count; i++) {
        if(strcmp(app->lib_entries[i].name, app->lib_sel_name) == 0) {
            app->lib_scope_index = i;
            break;
        }
    }
    app->lib_scope_zoom = 0;
    app->lib_scope_offset = 0;
    scope_clamp(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperViewLibScope);
}

bool flipper_scene_libscope_on_event(void* ctx, SceneManagerEvent e) {
    UNUSED(ctx);
    UNUSED(e);
    return false;
}

void flipper_scene_libscope_on_exit(void* ctx) {
    UNUSED(ctx);
}