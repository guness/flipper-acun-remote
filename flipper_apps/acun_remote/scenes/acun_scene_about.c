#include "../acun_remote_i.h"

void acun_scene_about_on_enter(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Acun Remote " FAP_VERSION);
    widget_add_text_scroll_element(
        app->widget,
        0,
        15,
        128,
        49,
        "433.92 MHz / AM270\n"
        "Internal CC1101 radio\n\n"
        "Read learns one button\n"
        "from five presses, or\n"
        "syncs a saved button.\n\n"
        "Format: 47 code bits,\n"
        "usually 2 trailer bits\n"
        "(49 symbols total).\n"
        "TE is base pulse time.\n"
        "Gap is the final low\n"
        "between repeat frames.\n\n"
        "Hold OK to send. Release\n"
        "finishes repeat frames.\n"
        "Back stops immediately.\n\n"
        "Send attempts count\n"
        "reserved OK presses,\n"
        "not repeats or successes.\n\n"
        "Sync from file reads a\n"
        "RAW or BinRAW .sub file\n"
        "and uses its code.\n"
        "Old files may be behind\n"
        "the gate's current state.\n"
        "It never sends RF.");
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
