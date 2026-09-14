#include "../acun_remote_i.h"

void acun_scene_send_on_enter(void* context) {
    AcunApp* app = context;
    RemoteEntry* entry = &app->store.entries[app->selected];
    acun_selected_label(app);
    if(!furi_hal_subghz_is_tx_allowed(SEQ_FREQUENCY)) {
        acun_popup_show(
            app, "Region blocks TX", "433.92 MHz not allowed here.", AcunAfterEntryMenu);
        return;
    }
    /* Reserve the next index durably before the radio starts. A stopped or failed
     * transmission consumes it; it is never rolled back. */
    app->pending = entry->profile;
    seq_advance(&app->pending);
    if(!remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
        entry->damaged = true;
        acun_popup_show(app, "Save failed", "Nothing sent.", AcunAfterSavedList);
        return;
    }
    entry->profile = app->pending;
    if(!radio_tx_start(app->radio, &app->pending)) {
        acun_popup_show(app, "TX failed", "Index advanced.", AcunAfterEntryMenu);
        return;
    }
    snprintf(app->text2, sizeof(app->text2), "%s\nBack stops", app->label);
    popup_reset(app->popup);
    popup_set_header(app->popup, "Sending", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->text2, 64, 36, AlignCenter, AlignCenter);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

bool acun_scene_send_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(!radio_is_busy(app->radio)) return false; /* result popup: plain back */
        radio_stop(app->radio);
        acun_popup_show(app, "Stopped", "Index advanced.", AcunAfterEntryMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventTxDone:
        radio_stop(app->radio);
        acun_popup_show(app, "Sent", "Check the receiver.", AcunAfterEntryMenu);
        return true;
    case AcunEventTxTimeout:
        radio_stop(app->radio);
        acun_popup_show(app, "Stopped", "Index advanced.", AcunAfterEntryMenu);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_send_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    popup_reset(app->popup);
}
