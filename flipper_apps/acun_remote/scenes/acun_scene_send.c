#include "../acun_remote_i.h"

static void acun_send_stop(AcunApp* app) {
    app->send_held = false;
    radio_stop(app->radio);
    notification_message(app->notifications, &sequence_blink_stop);
}

static void acun_send_draw(AcunApp* app, bool sending) {
    const SeqProfile* profile = &app->store.entries[app->selected].profile;
    snprintf(
        app->text2,
        sizeof(app->text2),
        "%s\nIndex %04X  Word %04X\n433.92 MHz",
        sending ? (app->send_held ? "Sending - hold OK" : "Finishing repeats...") :
                  "Ready - hold OK to send",
        profile->accumulator,
        profile->frame.word);
    dialog_ex_set_text(app->dialog, app->text2, 64, 31, AlignCenter, AlignCenter);
    dialog_ex_set_center_button_text(
        app->dialog, sending ? (app->send_held ? "Sending" : "Finishing") : "Hold to send");
}

static void acun_send_callback(DialogExResult result, void* context) {
    AcunApp* app = context;
    if(result == DialogExPressCenter)
        view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventSendPress);
    else if(result == DialogExReleaseCenter)
        view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventSendRelease);
    /* Short/long/repeat events must never reserve another index. */
}

void acun_scene_send_on_enter(void* context) {
    AcunApp* app = context;
    app->send_failed = false;
    app->send_held = false;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_send_callback);
    dialog_ex_enable_extended_events(app->dialog);
    dialog_ex_set_header(app->dialog, acun_selected_label(app), 64, 3, AlignCenter, AlignTop);
    acun_send_draw(app, false);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_send_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        acun_send_stop(app);
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventSendPress: {
        if(app->send_failed || app->send_held) return true;
        RemoteEntry* entry = &app->store.entries[app->selected];
        if(entry->damaged) return true;
        /* A fresh press supersedes the previous release tail, as in Sub-GHz. */
        if(radio_is_busy(app->radio)) acun_send_stop(app);
        /* Reserve once per physical press, before any RF. Stopping or failing
         * consumes the index; repeats during this hold use the same value. */
        app->pending = entry->profile;
        seq_advance(&app->pending);
        if(!remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
            entry->damaged = true;
            app->send_failed = true;
            acun_popup_show(app, "Save failed", "Nothing sent.", AcunAfterSavedList);
            return true;
        }
        entry->profile = app->pending;
        /* The SDK checks region restrictions in the transmit-start call. */
        if(!radio_tx_start(app->radio, &app->pending)) {
            app->send_failed = true;
            acun_popup_show(
                app, "TX failed", "Region may block 433MHz.\nIndex advanced.", AcunAfterEntryMenu);
            return true;
        }
        app->send_held = true;
        notification_message(app->notifications, &sequence_blink_start_blue);
        acun_send_draw(app, true);
        return true;
    }
    case AcunEventSendRelease:
        if(app->send_held && radio_is_busy(app->radio)) {
            app->send_held = false;
            radio_tx_release(app->radio);
            acun_send_draw(app, true);
        }
        return true;
    case AcunEventTxDone:
        if(!app->send_held) {
            acun_send_stop(app);
            acun_send_draw(app, false);
            return true;
        }
        /* Completion while held is unexpected, just like a stalled encoder. */
        // fall through
    case AcunEventTxTimeout:
        app->send_failed = true;
        acun_send_stop(app);
        acun_popup_show(
            app, "TX stopped", "Index advanced.\nCheck the receiver.", AcunAfterEntryMenu);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_send_on_exit(void* context) {
    AcunApp* app = context;
    acun_send_stop(app);
    dialog_ex_disable_extended_events(app->dialog);
    dialog_ex_reset(app->dialog);
    popup_reset(app->popup);
}
