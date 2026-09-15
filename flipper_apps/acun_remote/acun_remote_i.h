#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "sequence_core.h"
#include "remote_name.h"
#include "remote_store.h"
#include "radio.h"
#include "scenes/acun_scene.h"

#define ACUN_VERSION "0.2"
#define ACUN_POPUP_MS 1500
#define ACUN_TICK_MS 10

typedef enum {
    AcunViewSubmenu,
    AcunViewDialog,
    AcunViewTextInput,
    AcunViewNumberInput,
    AcunViewWidget,
    AcunViewPopup,
} AcunView;

/* Custom events. Submenu item indexes (0..REMOTE_STORE_MAX) are sent as-is,
 * so app events start well above them. */
typedef enum {
    AcunEventPress = 100,
    AcunEventRxOverflow,
    AcunEventTxDone,
    AcunEventTxTimeout,
    AcunEventPopupDone,
    AcunEventDialogLeft,
    AcunEventDialogCenter,
    AcunEventDialogRight,
    AcunEventTextDone,
    AcunEventNumberDone,
} AcunEvent;

typedef enum {
    AcunFlowLearn,
    AcunFlowRename,
} AcunFlow;

/* Where a result popup goes when its 1.5 s timeout fires. */
typedef enum {
    AcunAfterMainMenu,
    AcunAfterSavedList,
    AcunAfterEntryMenu,
    AcunAfterNumberInput,
} AcunAfter;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    DialogEx* dialog;
    TextInput* text_input;
    NumberInput* number_input;
    Widget* widget;
    Popup* popup;
    NotificationApp* notifications;
    Radio* radio;
    RemoteStore store;

    SeqFrame heard; /* last confirmed press from the radio */
    SeqFrame captured[SEQ_LEARN_COUNT];
    uint8_t captured_count;
    bool learn_hint; /* a different button was heard while learning */
    SeqProfile pending; /* fitted, synced or advanced profile awaiting a write */
    int32_t sync_delta;
    int selected; /* index into store, or -1 */
    AcunFlow flow;
    char name[REMOTE_NAME_MAX + 1]; /* name being chosen or typed */
    uint8_t button; /* number being chosen */
    const char* names[REMOTE_STORE_MAX];
    size_t names_count;
    const char* retry_text;
    bool retry_allowed;
    AcunAfter after;
    char label[REMOTE_LABEL_MAX]; /* "Garage B2" of the entry in play */
    char text[48]; /* popup header: Popup keeps the pointer */
    char text2[96]; /* popup or dialog body */
    uint32_t signal_tick; /* throttles the signal-bar redraw against the 10ms tick */
    uint32_t blinked_raw_count; /* last radio_raw_count() the LED already blinked for */
} AcunApp;

/* Shared view callbacks; each forwards to the scene manager as a custom event. */
void acun_submenu_callback(void* context, uint32_t index);
void acun_dialog_callback(DialogExResult result, void* context);
void acun_popup_callback(void* context);
void acun_text_callback(void* context);
void acun_number_callback(void* context, int32_t number);
bool acun_name_validator(const char* text, FuriString* error, void* context);

/* Show a 1.5 s result popup; on AcunEventPopupDone call acun_popup_done(). */
void acun_popup_show(AcunApp* app, const char* header, const char* text, AcunAfter after);
void acun_popup_done(AcunApp* app);
/* Label of store entry app->selected, written to app->label. */
const char* acun_selected_label(AcunApp* app);
/* Draw a signal-strength bar and its dBm reading at the given y on a Widget
 * already mid-build (call after the screen's other elements, before switching
 * to it). -90dBm (no signal) to -30dBm (strong) fills the bar 0-100%. */
void acun_draw_signal_bar(Widget* widget, uint8_t y, float rssi);
/* Draw the decoder-side diagnostics from radio.h: how many times it found a
 * quiet gap to start from, its best run of consecutive bits out of 47, frames
 * actually completed, and the exact pulse that most recently broke a run. */
void acun_draw_diagnostics(Widget* widget, uint8_t y, const Radio* radio);
/* Blink the LED green when radio_raw_count() has grown since the last call,
 * i.e. real reception activity, same as the firmware's own Sub-GHz reader. */
void acun_blink_on_new_frame(AcunApp* app);
