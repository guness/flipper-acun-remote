#include "../acun_remote_i.h"

enum {
    EntrySend,
    EntryInfo,
    EntryRename,
    EntryDelete,
};

void acun_scene_entry_menu_on_enter(void* context) {
    AcunApp* app = context;
    const RemoteEntry* entry = &app->store.entries[app->selected];
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, acun_selected_label(app));
    if(!entry->damaged) {
        submenu_add_item(app->submenu, "Info", EntryInfo, acun_submenu_callback, app);
    }
    submenu_add_item(app->submenu, "Delete", EntryDelete, acun_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneEntryMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_entry_menu_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneEntryMenu, event.event);
    switch(event.event) {
    case EntryInfo:
        scene_manager_next_scene(app->scene_manager, AcunSceneInfo);
        return true;
    case EntryDelete:
        scene_manager_next_scene(app->scene_manager, AcunSceneDeleteConfirm);
        return true;
    }
    return false;
}

void acun_scene_entry_menu_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
