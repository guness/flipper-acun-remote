#include "../acun_remote_i.h"

void acun_scene_name_select_on_enter(void* context) {
    AcunApp* app = context;
    app->names_count = remote_store_names(&app->store, app->names, REMOTE_STORE_MAX);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Remote");
    for(size_t i = 0; i < app->names_count; ++i) {
        submenu_add_item(app->submenu, app->names[i], i, acun_submenu_callback, app);
        if(app->flow == AcunFlowRename && strcasecmp(app->names[i], app->name) == 0)
            submenu_set_selected_item(app->submenu, i);
    }
    submenu_add_item(
        app->submenu, "New remote...", app->names_count, acun_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_name_select_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(app->flow != AcunFlowLearn) return false; /* rename: back to the entry menu */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < app->names_count) {
        snprintf(app->name, sizeof(app->name), "%s", app->names[event.event]);
        scene_manager_next_scene(app->scene_manager, AcunSceneNumberSelect);
        return true;
    }
    if(event.event == app->names_count) {
        if(app->flow == AcunFlowLearn) app->name[0] = '\0';
        scene_manager_next_scene(app->scene_manager, AcunSceneNameInput);
        return true;
    }
    return false;
}

void acun_scene_name_select_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
