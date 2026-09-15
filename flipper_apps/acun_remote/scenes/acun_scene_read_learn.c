#include "../acun_remote_i.h"

#define READ_LEARN_SIGNAL_BAR_Y 44
#define READ_LEARN_SIGNAL_REDRAW_TICKS 10 /* ~100ms at the 10ms dispatcher tick */

static void read_learn_draw(AcunApp* app) {
    char line[32];
    snprintf(line, sizeof(line), "Press %u of %u", app->captured_count + 1u, SEQ_LEARN_COUNT);
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "New remote");
    widget_add_string_element(app->widget, 64, 11, AlignCenter, AlignTop, FontPrimary, line);
    widget_add_string_multiline_element(
        app->widget,
        64,
        22,
        AlignCenter,
        AlignTop,
        FontSecondary,
        app->learn_hint ? app->text2 : "Release, then press\nthe same button again.");
    acun_draw_signal_bar(app->widget, READ_LEARN_SIGNAL_BAR_Y, radio_rssi(app->radio));
    acun_draw_diagnostics(app->widget, READ_LEARN_SIGNAL_BAR_Y + 9, app->radio);
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
    app->signal_tick = 0;
    app->blinked_raw_count = 0;
    notification_message_block(app->notifications, &sequence_display_backlight_enforce_on);
    radio_rx_start(app->radio);
    read_learn_draw(app);
}

bool acun_scene_read_learn_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type == SceneManagerEventTypeTick) {
        acun_blink_on_new_frame(app);
        if(++app->signal_tick % READ_LEARN_SIGNAL_REDRAW_TICKS == 0) read_learn_draw(app);
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventPress: {
        /* The radio restarted on entry, so re-check against the last accepted press. */
        if(seq_same_frame(&app->captured[app->captured_count - 1], &app->heard)) return true;
        const SeqFrame* base = &app->captured[0];
        const SeqFrame* now = &app->heard;
        if(!seq_same_button(base, now)) {
            /* Only one press captured so far: nothing depends on it yet, and an
             * anchor that was itself a stray reception would otherwise reject
             * every real press after it for the rest of the session. Re-anchor
             * on the newest press instead of getting stuck. */
            if(app->captured_count == 1) {
                app->captured[0] = *now;
                read_learn_draw(app);
                return true;
            }
            /* Two or more presses already agreed, so the anchor is trustworthy;
             * name which field disagreed (seq_same_button checks only these two)
             * instead of guessing at it. */
            if(base->prefix != now->prefix)
                snprintf(
                    app->text2,
                    sizeof(app->text2),
                    "Different button:\nprefix %08lX not %08lX",
                    (unsigned long)now->prefix,
                    (unsigned long)base->prefix);
            else
                snprintf(
                    app->text2,
                    sizeof(app->text2),
                    "Different button:\nflag %04X not %04X",
                    now->word & 0x8000,
                    base->word & 0x8000);
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
    }
    case AcunEventRxOverflow:
        read_learn_retry(app, "Receive overflow.\nStart over?");
        return true;
    }
    return false;
}

void acun_scene_read_learn_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    widget_reset(app->widget);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}
