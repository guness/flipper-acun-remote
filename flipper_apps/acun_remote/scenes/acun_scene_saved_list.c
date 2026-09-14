#include "../acun_remote_i.h"

void acun_scene_saved_list_on_enter(void* context) {
    AcunApp* app = context;
    remote_store_load(&app->store, app->storage);
    app->selected = -1;
    if(app->store.count == 0) {
        widget_reset(app->widget);
        widget_add_string_multiline_element(
            app->widget,
            64,
            26,
            AlignCenter,
            AlignCenter,
            FontSecondary,
            "Nothing saved yet.\nUse Read.");
        view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
        return;
    }
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->store.truncated ? "Saved (32 max)" : "Saved");
    for(size_t i = 0; i < app->store.count; ++i) {
        char label[REMOTE_LABEL_MAX];
        remote_store_label(&app->store.entries[i], label, sizeof(label));
        submenu_add_item(app->submenu, label, i, acun_submenu_callback, app);
    }
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneSavedList));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= app->store.count) return false;
    app->selected = (int)event.event;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneSavedList, event.event);
    scene_manager_next_scene(app->scene_manager, AcunSceneEntryMenu);
    return true;
}

void acun_scene_saved_list_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
