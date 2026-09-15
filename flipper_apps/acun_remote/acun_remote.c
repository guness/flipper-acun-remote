#include "acun_remote_i.h"

void acun_submenu_callback(void* context, uint32_t index) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void acun_dialog_callback(DialogExResult result, void* context) {
    AcunApp* app = context;
    uint32_t event = AcunEventDialogCenter;
    if(result == DialogExResultLeft) event = AcunEventDialogLeft;
    if(result == DialogExResultRight) event = AcunEventDialogRight;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void acun_popup_callback(void* context) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventPopupDone);
}

void acun_text_callback(void* context) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventTextDone);
}

void acun_number_callback(void* context, int32_t number) {
    AcunApp* app = context;
    app->button = (uint8_t)number;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventNumberDone);
}

bool acun_name_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);
    if(remote_name_valid(text)) return true;
    furi_string_set_str(error, "1-12 letters, digits,\nspace, - or _");
    return false;
}

void acun_popup_show(AcunApp* app, const char* header, const char* text, AcunAfter after) {
    app->after = after;
    snprintf(app->text, sizeof(app->text), "%s", header);
    snprintf(app->text2, sizeof(app->text2), "%s", text);
    popup_reset(app->popup);
    popup_set_header(app->popup, app->text, 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->text2, 64, 36, AlignCenter, AlignCenter);
    popup_set_timeout(app->popup, ACUN_POPUP_MS);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, acun_popup_callback);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

void acun_popup_done(AcunApp* app) {
    SceneManager* sm = app->scene_manager;
    switch(app->after) {
    case AcunAfterMainMenu:
        scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneMainMenu);
        break;
    case AcunAfterSavedList:
        if(scene_manager_has_previous_scene(sm, AcunSceneSavedList))
            scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneSavedList);
        else
            scene_manager_search_and_switch_to_another_scene(sm, AcunSceneSavedList);
        break;
    case AcunAfterEntryMenu:
        scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneEntryMenu);
        break;
    case AcunAfterNumberInput:
        view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewNumberInput);
        break;
    }
}

const char* acun_selected_label(AcunApp* app) {
    if(app->selected >= 0 && (size_t)app->selected < app->store.count)
        remote_store_label(&app->store.entries[app->selected], app->label, sizeof(app->label));
    else
        snprintf(app->label, sizeof(app->label), "?");
    return app->label;
}

void acun_draw_signal_bar(Widget* widget, uint8_t y, float rssi) {
    float clamped = rssi < -90.0f ? -90.0f : (rssi > -30.0f ? -30.0f : rssi);
    uint8_t fill = (uint8_t)((clamped + 90.0f) / 60.0f * 88.0f);
    char label[8];
    snprintf(label, sizeof(label), "%d", (int)rssi);
    widget_add_rect_element(widget, 2, y, 90, 7, 0, false);
    if(fill) widget_add_rect_element(widget, 3, y + 1, fill, 5, 0, true);
    widget_add_string_element(widget, 96, y, AlignLeft, AlignTop, FontSecondary, label);
}

void acun_blink_on_new_frame(AcunApp* app) {
    uint32_t count = radio_raw_count(app->radio);
    if(count == app->blinked_raw_count) return;
    app->blinked_raw_count = count;
    notification_message(app->notifications, &sequence_blink_green_10);
}

void acun_draw_diagnostics(Widget* widget, uint8_t y, const Radio* radio) {
    /* Reception up to the decoder (edge/dequeued counts) is already known
     * healthy; this narrows down what the decoder itself is doing with it.
     * s: how many times it found a gap quiet enough to start counting from.
     * m: its best run of consecutive bits before giving up or completing,
     * out of the 47 a frame needs. r: frames actually completed. H/L + a
     * number: the exact pulse that most recently broke a run at that count -
     * a high pulse's or a low pulse's duration in microseconds. */
    char line[32];
    snprintf(
        line,
        sizeof(line),
        "s%lu m%u r%lu %c%lu",
        (unsigned long)radio_sync_count(radio),
        radio_max_run(radio),
        (unsigned long)radio_raw_count(radio),
        radio_fail_was_high(radio) ? 'H' : 'L',
        (unsigned long)radio_fail_duration(radio));
    widget_add_string_element(widget, 2, y, AlignLeft, AlignTop, FontSecondary, line);
}

static bool acun_custom_event_callback(void* context, uint32_t event) {
    AcunApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool acun_navigation_event_callback(void* context) {
    AcunApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* Runs in the dispatcher thread, so radio events go straight to the scene. */
static void acun_tick_event_callback(void* context) {
    AcunApp* app = context;
    uint32_t event = 0;
    switch(radio_tick(app->radio, &app->heard)) {
    case RadioEventPress:
        event = AcunEventPress;
        break;
    case RadioEventOverflow:
        event = AcunEventRxOverflow;
        break;
    case RadioEventTxDone:
        event = AcunEventTxDone;
        break;
    case RadioEventTxTimeout:
        event = AcunEventTxTimeout;
        break;
    case RadioEventNone:
        break;
    }
    if(event) scene_manager_handle_custom_event(app->scene_manager, event);
    scene_manager_handle_tick_event(app->scene_manager);
}

static AcunApp* acun_alloc(void) {
    AcunApp* app = malloc(sizeof(AcunApp));
    memset(app, 0, sizeof(*app));
    app->selected = -1;
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&acun_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, acun_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, acun_navigation_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, acun_tick_event_callback, furi_ms_to_ticks(ACUN_TICK_MS));

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewSubmenu, submenu_get_view(app->submenu));
    app->dialog = dialog_ex_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewDialog, dialog_ex_get_view(app->dialog));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AcunViewTextInput, text_input_get_view(app->text_input));
    app->number_input = number_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AcunViewNumberInput, number_input_get_view(app->number_input));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewPopup, popup_get_view(app->popup));

    app->radio = radio_alloc();
    remote_store_load(&app->store, app->storage);
    return app;
}

static void acun_free(AcunApp* app) {
    radio_free(app->radio);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewDialog);
    dialog_ex_free(app->dialog);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewTextInput);
    text_input_free(app->text_input);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewNumberInput);
    number_input_free(app->number_input);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewPopup);
    popup_free(app->popup);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t acun_remote_app(void* argument) {
    UNUSED(argument);
    AcunApp* app = acun_alloc();
    furi_hal_power_insomnia_enter();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, AcunSceneMainMenu);
    view_dispatcher_run(app->view_dispatcher);
    furi_hal_power_insomnia_exit();
    acun_free(app);
    return 0;
}
