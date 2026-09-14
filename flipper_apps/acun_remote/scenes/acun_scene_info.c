#include "../acun_remote_i.h"

void acun_scene_info_on_enter(void* context) {
    AcunApp* app = context;
    const SeqProfile* p = &app->store.entries[app->selected].profile;
    char line[40];
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 1, AlignCenter, AlignTop, FontPrimary, acun_selected_label(app));
    snprintf(line, sizeof(line), "Prefix %08lX", (unsigned long)p->frame.prefix);
    widget_add_string_element(app->widget, 2, 14, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Step %04X  Word %04X", p->step, p->frame.word);
    widget_add_string_element(app->widget, 2, 24, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Index %04X  Sends %lu", p->accumulator, (unsigned long)p->sends);
    widget_add_string_element(app->widget, 2, 34, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "%u pulses  TE %u us", 47u + p->frame.suffix_count, p->frame.te);
    widget_add_string_element(app->widget, 2, 44, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Gap %lu us", (unsigned long)p->frame.gap);
    widget_add_string_element(app->widget, 2, 54, AlignLeft, AlignTop, FontSecondary, line);
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
