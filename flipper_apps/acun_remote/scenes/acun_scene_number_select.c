#include "../acun_remote_i.h"

static const char* chosen_label(AcunApp* app) {
    snprintf(app->label, sizeof(app->label), "%s B%u", app->name, app->button);
    return app->label;
}

void acun_scene_number_select_on_enter(void* context) {
    AcunApp* app = context;
    int32_t current = remote_store_free_button(&app->store, app->name);
    if(app->flow == AcunFlowRename && app->selected >= 0)
        current = app->store.entries[app->selected].button;
    number_input_set_header_text(app->number_input, "Button number");
    number_input_set_result_callback(
        app->number_input, acun_number_callback, app, current, 1, REMOTE_BUTTON_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewNumberInput);
}

static void number_select_learn(AcunApp* app) {
    if(remote_store_find_named(&app->store, app->name, app->button) >= 0) {
        scene_manager_next_scene(app->scene_manager, AcunSceneReplaceConfirm);
        return;
    }
    if(remote_store_create(app->storage, app->name, app->button, &app->pending))
        acun_popup_show(app, "Saved", chosen_label(app), AcunAfterSavedList);
    else
        acun_popup_show(app, "Save failed", "Check the SD card.", AcunAfterMainMenu);
}

static void number_select_rename(AcunApp* app) {
    const RemoteEntry* entry = &app->store.entries[app->selected];
    int existing = remote_store_find_named(&app->store, app->name, app->button);
    if(existing == app->selected) { /* same name and number: nothing to do */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneSavedList);
        return;
    }
    if(existing >= 0) {
        acun_popup_show(app, "Already exists", chosen_label(app), AcunAfterNumberInput);
        return;
    }
    if(remote_store_rename(app->storage, entry->name, entry->button, app->name, app->button))
        acun_popup_show(app, "Renamed", chosen_label(app), AcunAfterSavedList);
    else
        acun_popup_show(app, "Rename failed", "Check the SD card.", AcunAfterSavedList);
}

bool acun_scene_number_select_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventNumberDone:
        if(app->flow == AcunFlowLearn)
            number_select_learn(app);
        else
            number_select_rename(app);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_number_select_on_exit(void* context) {
    UNUSED(context);
}
