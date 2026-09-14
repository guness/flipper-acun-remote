#include "../acun_remote_i.h"

void acun_scene_about_on_enter(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Acun Remote " ACUN_VERSION);
    widget_add_string_element(
        app->widget, 64, 17, AlignCenter, AlignTop, FontSecondary, "433.92 MHz, internal radio");
    widget_add_string_multiline_element(
        app->widget,
        64,
        31,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Read learns a button in five\npresses or re-syncs a saved one.\nSaved sends, renames, deletes.");
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

bool acun_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void acun_scene_about_on_exit(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
}
