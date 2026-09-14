#include "../acun_remote_i.h"

void acun_scene_read_sync_on_enter(void* context) {
    AcunApp* app = context;
    long delta = app->sync_delta;
    if(delta == 0)
        snprintf(app->text2, sizeof(app->text2), "Remote is in sync.");
    else if(delta > 0)
        snprintf(
            app->text2,
            sizeof(app->text2),
            "Remote is %ld press%s ahead.",
            delta,
            delta == 1 ? "" : "es");
    else
        snprintf(
            app->text2,
            sizeof(app->text2),
            "Remote is %ld press%s behind.\nReceiver may reject.",
            -delta,
            delta == -1 ? "" : "es");
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, acun_selected_label(app), 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->text2, 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Sync");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_read_sync_on_event(void* context, SceneManagerEvent event) {
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
    case AcunEventDialogRight: {
        RemoteEntry* entry = &app->store.entries[app->selected];
        if(remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
            entry->profile = app->pending;
            acun_popup_show(app, "Synced", acun_selected_label(app), AcunAfterMainMenu);
        } else {
            entry->damaged = true;
            acun_popup_show(app, "Save failed", "Entry marked damaged.", AcunAfterMainMenu);
        }
        return true;
    }
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_read_sync_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
