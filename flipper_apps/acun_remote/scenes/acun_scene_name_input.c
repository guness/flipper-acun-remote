#include "../acun_remote_i.h"

void acun_scene_name_input_on_enter(void* context) {
    AcunApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Remote name");
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_validator(app->text_input, acun_name_validator, app);
    text_input_set_result_callback(
        app->text_input,
        acun_text_callback,
        app,
        app->name,
        sizeof(app->name),
        app->flow == AcunFlowLearn);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewTextInput);
}

bool acun_scene_name_input_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != AcunEventTextDone) return false;
    scene_manager_next_scene(app->scene_manager, AcunSceneNumberSelect);
    return true;
}

void acun_scene_name_input_on_exit(void* context) {
    AcunApp* app = context;
    text_input_reset(app->text_input);
}
