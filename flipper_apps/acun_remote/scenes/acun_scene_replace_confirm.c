#include "../acun_remote_i.h"

void acun_scene_replace_confirm_on_enter(void* context) {
    AcunApp* app = context;
    snprintf(app->label, sizeof(app->label), "%s B%u", app->name, app->button);
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, app->label, 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog, "Already saved.\nReplace it?", 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Back");
    dialog_ex_set_right_button_text(app->dialog, "Replace");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_replace_confirm_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case AcunEventDialogRight:
        remote_store_delete(app->storage, app->name, app->button);
        if(remote_store_create(app->storage, app->name, app->button, &app->pending))
            acun_popup_show(app, "Saved", app->label, AcunAfterSavedList);
        else
            acun_popup_show(app, "Save failed", "Check the SD card.", AcunAfterMainMenu);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_replace_confirm_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
