#include "../acun_remote_i.h"

void acun_scene_read_retry_on_enter(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Read", 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->retry_text, 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    if(app->retry_allowed) dialog_ex_set_right_button_text(app->dialog, "Retry");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_read_retry_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    case AcunEventDialogRight:
        /* Re-enters ReadListen, which resets the learn state and restarts the radio. */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneReadListen);
        return true;
    }
    return false;
}

void acun_scene_read_retry_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
