/* Host substitutes for the send scene's UI, storage and radio dependencies. */
#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sequence_core.h"

typedef enum {
    DialogExPressCenter, DialogExReleaseCenter, DialogExResultCenter,
} DialogExResult;
typedef enum { SceneManagerEventTypeBack, SceneManagerEventTypeCustom } EventType;
typedef struct { EventType type; unsigned event; } SceneManagerEvent;
enum {
    AcunEventSendPress, AcunEventSendRelease, AcunEventTxDone,
    AcunEventTxTimeout, AcunEventPopupDone,
    AcunAfterSavedList, AcunAfterEntryMenu, AcunViewDialog,
    AlignCenter, AlignTop,
};
typedef struct { SeqProfile profile; char name[13]; unsigned button; bool damaged; } RemoteEntry;
typedef struct { bool busy; bool released; unsigned starts; } Radio;
typedef struct {
    void (*callback)(DialogExResult, void*);
    void* context;
    bool extended;
} Dialog;
typedef struct {
    struct { RemoteEntry entries[1]; } store;
    int selected;
    SeqProfile pending;
    Radio* radio;
    Dialog* dialog;
    void *storage, *popup, *view_dispatcher, *notifications;
    bool send_held;
    bool send_failed;
    char text2[96];
} AcunApp;

extern bool save_ok, tx_ok;
extern unsigned writes, events, last_event, popups;
extern SeqProfile saved;
static inline void dialog_ex_reset(Dialog* d) { d->callback = NULL; }
static inline void dialog_ex_set_context(Dialog* d, void* c) { d->context = c; }
static inline void dialog_ex_set_result_callback(Dialog* d, void (*cb)(DialogExResult, void*)) { d->callback = cb; }
static inline void dialog_ex_enable_extended_events(Dialog* d) { d->extended = true; }
static inline void dialog_ex_disable_extended_events(Dialog* d) { d->extended = false; }
#define dialog_ex_set_header(...) ((void)0)
#define dialog_ex_set_text(...) ((void)0)
#define dialog_ex_set_center_button_text(...) ((void)0)
#define view_dispatcher_switch_to_view(...) ((void)0)
#define popup_reset(...) ((void)0)
#define acun_popup_done(...) ((void)0)
#define acun_popup_show(...) (++popups)
#define remote_store_write(storage, name, button, profile) test_write(profile)
static inline bool test_write(const SeqProfile* p) { ++writes; saved = *p; return save_ok; }
static inline void view_dispatcher_send_custom_event(void* dispatcher, unsigned event) {
    (void)dispatcher;
    ++events;
    last_event = event;
}
static inline bool radio_is_busy(Radio* r) { return r->busy; }
static inline void radio_tx_release(Radio* r) { r->released = true; }
static inline void radio_stop(Radio* r) { r->busy = false; }
static inline bool radio_tx_start(Radio* r, const SeqProfile* p) {
    /* A successful durable reservation must precede each attempted start. */
    assert(save_ok && writes == r->starts + 1);
    assert(memcmp(p, &saved, sizeof(saved)) == 0);
    ++r->starts;
    r->released = false;
    r->busy = tx_ok;
    return tx_ok;
}
void acun_scene_send_on_enter(void*);
bool acun_scene_send_on_event(void*, SceneManagerEvent);
void acun_scene_send_on_exit(void*);

extern bool blinking;
static const unsigned sequence_blink_start_blue = 1, sequence_blink_stop = 0;
static inline void notification_message(void* context, const unsigned* sequence) {
    (void)context;
    blinking = *sequence != 0;
}
