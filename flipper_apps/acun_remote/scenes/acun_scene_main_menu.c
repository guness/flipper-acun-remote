#include "../acun_remote_i.h"

enum {
    MainMenuRead,
    MainMenuSaved,
    MainMenuAbout,
};

void acun_scene_main_menu_on_enter(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Acun Remote");
    submenu_add_item(app->submenu, "Saved", MainMenuSaved, acun_submenu_callback, app);
    submenu_add_item(app->submenu, "About", MainMenuAbout, acun_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneMainMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneMainMenu, event.event);
    switch(event.event) {
    case MainMenuSaved:
        scene_manager_next_scene(app->scene_manager, AcunSceneSavedList);
        return true;
    case MainMenuAbout:
        scene_manager_next_scene(app->scene_manager, AcunSceneAbout);
        return true;
    }
    return false;
}

void acun_scene_main_menu_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
