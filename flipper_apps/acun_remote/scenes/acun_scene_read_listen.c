#include "../acun_remote_i.h"

static void read_listen_retry(AcunApp* app, const char* text, bool allowed) {
    app->retry_text = text;
    app->retry_allowed = allowed;
    scene_manager_next_scene(app->scene_manager, AcunSceneReadRetry);
}

void acun_scene_read_listen_on_enter(void* context) {
    AcunApp* app = context;
    remote_store_load(&app->store, app->storage);
    app->captured_count = 0;
    app->learn_hint = false;
    app->selected = -1;
    radio_rx_start(app->radio);
    popup_reset(app->popup);
    popup_set_header(app->popup, "Listening 433.92 MHz", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, "Hold a remote button", 64, 36, AlignCenter, AlignCenter);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

bool acun_scene_read_listen_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventPress: {
        int found = remote_store_find(&app->store, &app->heard);
        if(found < 0) {
            app->captured[0] = app->heard;
            app->captured_count = 1;
            scene_manager_next_scene(app->scene_manager, AcunSceneReadLearn);
            return true;
        }
        app->selected = found;
        if(app->store.entries[found].damaged) {
            snprintf(
                app->text2,
                sizeof(app->text2),
                "%s is damaged.\nDelete it in Saved,\nthen read again.",
                acun_selected_label(app));
            read_listen_retry(app, app->text2, false);
            return true;
        }
        app->pending = app->store.entries[found].profile;
        if(seq_sync(&app->pending, &app->heard, &app->sync_delta))
            scene_manager_next_scene(app->scene_manager, AcunSceneReadSync);
        else
            read_listen_retry(app, "Could not sync\nto this press.", true);
        return true;
    }
    case AcunEventRxOverflow:
        read_listen_retry(app, "Receive overflow.\nTry again?", true);
        return true;
    case AcunEventRxTimeout:
        read_listen_retry(app, "No press heard.\nTry again?", true);
        return true;
    }
    return false;
}

void acun_scene_read_listen_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    popup_reset(app->popup);
}
