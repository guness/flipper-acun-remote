#include "../acun_remote_i.h"

void acun_scene_info_on_enter(void* context) {
    AcunApp* app = context;
    const SeqProfile* p = &app->store.entries[app->selected].profile;
    char body[256];
    snprintf(
        body,
        sizeof(body),
        "Send attempts: %lu\n"
        "Counter: %04X\n"
        "Step: %04X\n"
        "Word: %04X\n"
        "Prefix: %08lX\n"
        "Pulse (TE): %u us\n"
        "Gap: %lu.%03lu ms\n"
        "Tail: %u bits / %02X",
        (unsigned long)p->sends,
        p->accumulator,
        p->step,
        p->frame.word,
        (unsigned long)p->frame.prefix,
        p->frame.te,
        (unsigned long)(p->frame.gap / 1000),
        (unsigned long)(p->frame.gap % 1000),
        p->frame.suffix_count,
        p->frame.suffix);
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 1, AlignCenter, AlignTop, FontPrimary, acun_selected_label(app));
    widget_add_text_scroll_element(app->widget, 0, 14, 128, 50, body);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

bool acun_scene_info_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void acun_scene_info_on_exit(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
}
