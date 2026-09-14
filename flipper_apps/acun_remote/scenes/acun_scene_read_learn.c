#include "../acun_remote_i.h"

static void read_learn_draw(AcunApp* app) {
    char line[32];
    snprintf(line, sizeof(line), "Press %u of %u", app->captured_count + 1u, SEQ_LEARN_COUNT);
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "New remote");
    widget_add_string_element(app->widget, 64, 18, AlignCenter, AlignTop, FontPrimary, line);
    widget_add_string_multiline_element(
        app->widget,
        64,
        36,
        AlignCenter,
        AlignTop,
        FontSecondary,
        app->learn_hint ? "Different button, ignored.\nPress the same button again." :
                          "Release, then press\nthe same button again.");
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

static void read_learn_retry(AcunApp* app, const char* text) {
    app->retry_text = text;
    app->retry_allowed = true;
    scene_manager_next_scene(app->scene_manager, AcunSceneReadRetry);
}

void acun_scene_read_learn_on_enter(void* context) {
    AcunApp* app = context;
    app->learn_hint = false;
    radio_rx_start(app->radio);
    read_learn_draw(app);
}

bool acun_scene_read_learn_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventPress:
        /* The radio restarted on entry, so re-check against the last accepted press. */
        if(seq_same_frame(&app->captured[app->captured_count - 1], &app->heard)) return true;
        if(!seq_same_button(&app->captured[0], &app->heard)) {
            app->learn_hint = true;
            read_learn_draw(app);
            return true;
        }
        app->captured[app->captured_count++] = app->heard;
        app->learn_hint = false;
        if(app->captured_count < SEQ_LEARN_COUNT) {
            read_learn_draw(app);
            return true;
        }
        if(seq_fit(app->captured, &app->pending)) {
            app->flow = AcunFlowLearn;
            app->selected = -1;
            scene_manager_next_scene(app->scene_manager, AcunSceneNameSelect);
        } else {
            read_learn_retry(app, "Presses don't fit.\nStart over?");
        }
        return true;
    case AcunEventRxOverflow:
        read_learn_retry(app, "Receive overflow.\nStart over?");
        return true;
    case AcunEventRxTimeout:
        read_learn_retry(app, "No press heard.\nStart over?");
        return true;
    }
    return false;
}

void acun_scene_read_learn_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    widget_reset(app->widget);
}
