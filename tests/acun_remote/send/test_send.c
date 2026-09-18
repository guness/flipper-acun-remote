#include "acun_remote_i.h"

bool save_ok, tx_ok, blinking;
unsigned writes, events, last_event, popups;
SeqProfile saved;
static AcunApp app;
static Radio radio;
static Dialog dialog;

static void setup(void) {
    memset(&app, 0, sizeof(app));
    memset(&radio, 0, sizeof(radio));
    memset(&dialog, 0, sizeof(dialog));
    app.radio = &radio;
    app.dialog = &dialog;
    app.store.entries[0].profile.step = 7;
    save_ok = tx_ok = true;
    blinking = false;
    writes = events = popups = 0;
    acun_scene_send_on_enter(&app);
    assert(!blinking && !radio.busy && writes == 0 && radio.starts == 0);
    assert(dialog.extended && dialog.callback);
    assert(strstr(app.text2, "Ready"));
}
static void event(unsigned value) {
    assert(acun_scene_send_on_event(&app, (SceneManagerEvent){SceneManagerEventTypeCustom, value}));
}
static void key(DialogExResult result) {
    unsigned before = events;
    dialog.callback(result, dialog.context);
    if(events != before) event(last_event);
}
int main(void) {
    setup();
    key(DialogExReleaseCenter); /* Opening menu's release cannot send. */
    key(DialogExResultCenter);
    assert(writes == 0);
    key(DialogExPressCenter);
    assert(blinking && radio.busy && writes == 1 && saved.sends == 1);
    assert(strstr(app.text2, "Sending") && strstr(app.text2, "Index 0007"));
    key(DialogExPressCenter); /* Duplicate start cannot consume a second index. */
    key(DialogExResultCenter);
    assert(writes == 1);
    key(DialogExReleaseCenter);
    assert(blinking && radio.busy && radio.released && writes == 1);
    assert(strstr(app.text2, "Finishing"));
    key(DialogExReleaseCenter); /* Repeated release cannot reset the tail. */
    event(AcunEventTxDone);
    assert(!blinking && !radio.busy && writes == 1 && popups == 0);
    assert(strstr(app.text2, "Ready"));
    key(DialogExPressCenter);
    assert(blinking && radio.busy && writes == 2 && saved.sends == 2 && saved.accumulator == 14);
    assert(!acun_scene_send_on_event(&app, (SceneManagerEvent){SceneManagerEventTypeBack, 0}));
    assert(!blinking && !radio.busy && app.store.entries[0].profile.accumulator == 14);
    acun_scene_send_on_exit(&app);
    assert(!dialog.extended && !dialog.callback);

    setup();
    key(DialogExPressCenter);
    key(DialogExReleaseCenter);
    key(DialogExPressCenter); /* New press replaces the draining code. */
    assert(blinking && radio.busy && !radio.released && writes == 2 && saved.sends == 2);
    acun_scene_send_on_exit(&app);
    assert(!blinking && !radio.busy);

    setup();
    save_ok = false;
    key(DialogExPressCenter);
    assert(!blinking && app.store.entries[0].damaged && !radio.busy && radio.starts == 0 && popups == 1);
    key(DialogExPressCenter);
    assert(writes == 1);

    setup();
    tx_ok = false;
    key(DialogExPressCenter);
    assert(!blinking && !radio.busy && writes == 1 && app.store.entries[0].profile.accumulator == 7);
    key(DialogExReleaseCenter);
    key(DialogExPressCenter);
    assert(writes == 1 && popups == 1);

    setup();
    key(DialogExPressCenter);
    event(AcunEventTxTimeout);
    assert(!blinking && !radio.busy && saved.accumulator == 7 && popups == 1);
    key(DialogExPressCenter);
    assert(writes == 1);

    setup();
    key(DialogExPressCenter);
    acun_scene_send_on_exit(&app);
    assert(!blinking && !radio.busy && saved.accumulator == 7);
    puts("Send scene: hold/release, reservation order, repeat presses, failures and exit passed");
}
