#include "../acun_remote_i.h"

#define READ_LISTEN_SIGNAL_BAR_Y 48
#define READ_LISTEN_SIGNAL_REDRAW_TICKS 10 /* ~100ms at the 10ms dispatcher tick */

static void read_listen_draw(AcunApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 1, AlignCenter, AlignTop, FontPrimary, "Listening");
    widget_add_string_element(
        app->widget, 64, 15, AlignCenter, AlignTop, FontSecondary, "433.92 MHz");
    widget_add_string_element(
        app->widget, 64, 27, AlignCenter, AlignTop, FontSecondary, "Hold a remote button");
    acun_draw_signal_bar(app->widget, READ_LISTEN_SIGNAL_BAR_Y, radio_rssi(app->radio));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

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
    app->signal_tick = 0;
    notification_message_block(app->notifications, &sequence_display_backlight_enforce_on);
    radio_rx_start(app->radio);
    read_listen_draw(app);
}

bool acun_scene_read_listen_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        if(++app->signal_tick % READ_LISTEN_SIGNAL_REDRAW_TICKS == 0) read_listen_draw(app);
        return false;
    }
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
    }
    return false;
}

void acun_scene_read_listen_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    widget_reset(app->widget);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}
