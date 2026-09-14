#include "../acun_remote_i.h"

void acun_scene_delete_confirm_on_enter(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, acun_selected_label(app), 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, "Delete this button?", 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Delete");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_delete_confirm_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case AcunEventDialogRight: {
        const RemoteEntry* entry = &app->store.entries[app->selected];
        if(remote_store_delete(app->storage, entry->name, entry->button))
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, AcunSceneSavedList);
        else
            acun_popup_show(app, "Delete failed", "Check the SD card.", AcunAfterSavedList);
        return true;
    }
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_delete_confirm_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
