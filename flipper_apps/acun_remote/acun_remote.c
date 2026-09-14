#include "sequence_core.h"
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <subghz/devices/devices.h>
#include <subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_COUNT 4
#define TX_REPEATS 6
#define CAPTURE_TIMEOUT_MS 25000

typedef enum { ScreenHome, ScreenReady, ScreenListen, ScreenSave, ScreenInfo } Screen;
typedef struct {
    bool level;
    uint32_t duration;
} Pulse;

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMutex* ui_mutex;
    FuriMessageQueue* inputs;
    FuriMessageQueue* pulses;
    Storage* storage;
    const SubGhzDevice* radio;
    bool rx_on;
    bool tx_on;
    volatile bool overflow;
    bool running;
    Screen screen;
    uint8_t selected;
    SeqProfile profiles[SLOT_COUNT];
    bool valid[SLOT_COUNT];
    bool damaged[SLOT_COUNT];
    SeqFrame captured[SEQ_LEARN_COUNT];
    uint8_t captured_count;
    SeqDecoder decoder;
    SeqFrame candidate;
    uint8_t repeats;
    uint32_t listen_started;
    SeqProfile tx_profile;
    size_t tx_position;
    uint8_t tx_repeat;
    bool tx_initial_gap;
    char title[32];
    char lines[4][32];
    char footer[40];
} App;

static void draw(Canvas* canvas, void* context) {
    App* app = context;
    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, app->title);
    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < 4; ++i) canvas_draw_str(canvas, 2, 21 + 9 * i, app->lines[i]);
    canvas_draw_str(canvas, 2, 62, app->footer);
    furi_mutex_release(app->ui_mutex);
}

static void display(App* app, const char* title, const char* a, const char* b,
                    const char* c, const char* d, const char* footer) {
    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    snprintf(app->title, sizeof(app->title), "%s", title);
    const char* text[] = {a, b, c, d};
    for(size_t i = 0; i < 4; ++i)
        snprintf(app->lines[i], sizeof(app->lines[i]), "%s", text[i]);
    snprintf(app->footer, sizeof(app->footer), "%s", footer);
    furi_mutex_release(app->ui_mutex);
    view_port_update(app->viewport);
}

static void input(InputEvent* event, void* context) {
    App* app = context;
    furi_message_queue_put(app->inputs, event, 0);
}

static void capture_callback(bool level, uint32_t duration, void* context) {
    App* app = context;
    Pulse pulse = {.level = level, .duration = duration};
    if(furi_message_queue_put(app->pulses, &pulse, 0) != FuriStatusOk) app->overflow = true;
}

static void radio_stop(App* app) {
    if(app->rx_on) {
        subghz_devices_stop_async_rx(app->radio);
        app->rx_on = false;
    }
    if(app->tx_on) {
        subghz_devices_stop_async_tx(app->radio);
        app->tx_on = false;
    }
    subghz_devices_idle(app->radio);
    subghz_devices_sleep(app->radio);
}

static void radio_prepare(App* app) {
    subghz_devices_reset(app->radio);
    subghz_devices_idle(app->radio);
    subghz_devices_load_preset(app->radio, FuriHalSubGhzPresetOok270Async, NULL);
    subghz_devices_set_frequency(app->radio, SEQ_FREQUENCY);
}

static void record_path(char* path, size_t size, uint8_t slot, uint8_t copy) {
    snprintf(path, size, APP_DATA_PATH("button_%u_%u.seq"), slot + 1u, copy);
}

/* -1: present but invalid, 0: absent, 1: valid. Never silently fall back from a
 * damaged record to an older counter that may already have been transmitted. */
static int read_profile(App* app, uint8_t slot, uint8_t copy, SeqProfile* profile) {
    char path[96];
    record_path(path, sizeof(path), slot, copy);
    if(!storage_file_exists(app->storage, path)) return 0;
    File* file = storage_file_alloc(app->storage);
    uint8_t data[SEQ_RECORD_SIZE];
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) ok = storage_file_size(file) == sizeof(data) &&
                storage_file_read(file, data, sizeof(data)) == sizeof(data);
    storage_file_close(file);
    storage_file_free(file);
    return ok && seq_unpack(data, profile) ? 1 : -1;
}

static bool write_profile(App* app, uint8_t slot, SeqProfile* profile) {
    if(profile->generation == UINT32_MAX) return false;
    ++profile->generation;
    uint8_t copy = profile->generation & 1;
    char path[96];
    record_path(path, sizeof(path), slot, copy);
    uint8_t data[SEQ_RECORD_SIZE];
    seq_pack(profile, data);
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, data, sizeof(data)) == sizeof(data);
    if(ok) ok = storage_file_sync(file);
    bool closed = storage_file_close(file);
    ok = ok && closed;
    storage_file_free(file);
    SeqProfile check;
    uint8_t packed_check[SEQ_RECORD_SIZE];
    if(ok) ok = read_profile(app, slot, copy, &check) == 1;
    if(ok) {
        seq_pack(&check, packed_check);
        ok = memcmp(data, packed_check, sizeof(data)) == 0;
    }
    return ok;
}

static void load_profiles(App* app) {
    for(uint8_t i = 0; i < SLOT_COUNT; ++i) {
        SeqProfile a, b;
        int sa = read_profile(app, i, 0, &a), sb = read_profile(app, i, 1, &b);
        if(sa < 0 || sb < 0) {
            app->damaged[i] = true;
            continue;
        }
        if(sa == 1 || sb == 1) {
            app->profiles[i] = (sa == 1 && (sb != 1 || a.generation >= b.generation)) ? a : b;
            app->valid[i] = true;
        }
    }
}

static void home(App* app, const char* message) {
    app->screen = ScreenHome;
    char title[32], state[32], count[32];
    snprintf(title, sizeof(title), "Button %u / 4", app->selected + 1u);
    snprintf(state, sizeof(state), "%s", app->valid[app->selected] ? "Learned: ready to send" :
        app->damaged[app->selected] ? "Save damaged: relearn" : "Not learned");
    snprintf(count, sizeof(count), "Sends: %lu", app->valid[app->selected] ?
             (unsigned long)app->profiles[app->selected].sends : 0ul);
    display(app, title, state, count, "Up/Down: choose button", message,
            "OK Send > Learn < Info");
}

static void ready(App* app, const char* message) {
    app->screen = ScreenReady;
    char progress[32];
    snprintf(progress, sizeof(progress), "Press %u of %u", app->captured_count + 1u, SEQ_LEARN_COUNT);
    display(app, "Learn same button", progress, "Release the remote first.",
            "OK, then hold the button.", message, "OK Listen   Back Cancel");
}

static void start_capture(App* app) {
    furi_message_queue_reset(app->pulses);
    app->overflow = false;
    app->repeats = 0;
    seq_decoder_reset(&app->decoder);
    radio_prepare(app);
    app->rx_on = true;
    subghz_devices_start_async_rx(app->radio, capture_callback, app);
    app->listen_started = furi_get_tick();
    app->screen = ScreenListen;
    char progress[32];
    snprintf(progress, sizeof(progress), "Waiting for press %u / %u", app->captured_count + 1u, SEQ_LEARN_COUNT);
    display(app, "Listening 433.92 MHz", progress, "Hold SAME remote button",
            "until capture completes.", "", "Back Cancel");
}

static void received(App* app, const SeqFrame* frame) {
    if(app->overflow) {
        radio_stop(app);
        ready(app, "RX overflow. Try again.");
        return;
    }
    if(app->captured_count) {
        if(!seq_same_button(&app->captured[0], frame)) return;
        if(seq_same_frame(&app->captured[app->captured_count - 1], frame)) return;
    }
    if(app->repeats && seq_same_frame(&app->candidate, frame)) {
        ++app->repeats;
    } else {
        app->candidate = *frame;
        app->repeats = 1;
    }
    if(app->repeats < 2) return; /* Repeated packets confirm this one press. */
    radio_stop(app);
    app->captured[app->captured_count++] = *frame;
    if(app->captured_count < SEQ_LEARN_COUNT) {
        ready(app, "Captured! Release first.");
        return;
    }
    SeqProfile fitted;
    if(!seq_fit(app->captured, &fitted)) {
        app->captured_count = 0;
        ready(app, "No fit. Start again.");
        return;
    }
    app->tx_profile = fitted;
    app->screen = ScreenSave;
    char slot[32];
    snprintf(slot, sizeof(slot), "Save to Button %u?", app->selected + 1u);
    display(app, "5 presses verified", slot, "Replaces the saved slot.",
            "Next send = next press.", "", "OK Save     Back Cancel");
}

static LevelDuration transmit_callback(void* context) {
    App* app = context;
    if(app->tx_initial_gap) {
        app->tx_initial_gap = false;
        return level_duration_make(false, app->tx_profile.frame.gap);
    }
    if(app->tx_repeat >= TX_REPEATS) return level_duration_reset();
    bool level;
    uint32_t duration;
    if(!seq_pulse(&app->tx_profile.frame, app->tx_position++, &level, &duration))
        return level_duration_reset();
    if(app->tx_position == 2u * (47u + app->tx_profile.frame.suffix_count)) {
        app->tx_position = 0;
        ++app->tx_repeat;
    }
    return level_duration_make(level, duration);
}

static void send_button(App* app) {
    if(!app->valid[app->selected]) {
        home(app, "Learn this button first.");
        return;
    }
    if(!furi_hal_subghz_is_tx_allowed(SEQ_FREQUENCY)) {
        home(app, "Region blocks this TX.");
        return;
    }
    app->tx_profile = app->profiles[app->selected];
    seq_advance(&app->tx_profile);
    /* Reserve the new state durably before starting the radio. A failed/cancelled
     * TX consumes a value; it must never roll back to a potentially used value. */
    if(!write_profile(app, app->selected, &app->tx_profile)) {
        app->valid[app->selected] = false;
        app->damaged[app->selected] = true;
        home(app, "Save failed. Nothing sent.");
        return;
    }
    app->profiles[app->selected] = app->tx_profile;
    app->tx_position = 0;
    app->tx_repeat = 0;
    app->tx_initial_gap = true;
    radio_prepare(app);
    app->tx_on = subghz_devices_start_async_tx(app->radio, transmit_callback, app);
    if(!app->tx_on) {
        radio_stop(app);
        home(app, "TX failed; index advanced.");
        return;
    }
    display(app, "Sending", "One press, six repeats", "Counter saved to SD.",
            "", "", "Back Stop");
    uint32_t start = furi_get_tick();
    bool cancelled = false;
    while(!subghz_devices_is_async_complete_tx(app->radio)) {
        InputEvent event;
        if(furi_message_queue_get(app->inputs, &event, furi_ms_to_ticks(10)) == FuriStatusOk &&
           event.key == InputKeyBack && event.type == InputTypeShort) {
            cancelled = true;
            break;
        }
        if(furi_get_tick() - start > furi_ms_to_ticks(3000)) {
            cancelled = true;
            break;
        }
    }
    radio_stop(app);
    furi_message_queue_reset(app->inputs);
    home(app, cancelled ? "Stopped; index advanced." : "Sent. Check receiver.");
}

static void handle_input(App* app, const InputEvent* event) {
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyBack) {
        if(app->screen == ScreenHome) app->running = false;
        else {
            radio_stop(app);
            home(app, "");
        }
        return;
    }
    if(app->screen == ScreenHome) {
        if(event->key == InputKeyUp || event->key == InputKeyDown) {
            app->selected = (app->selected + (event->key == InputKeyUp ? 3 : 1)) % SLOT_COUNT;
            home(app, "");
        } else if(event->key == InputKeyRight) {
            app->captured_count = 0;
            ready(app, "Use 5 separate presses.");
        } else if(event->key == InputKeyOk) send_button(app);
        else if(event->key == InputKeyLeft) {
            if(!app->valid[app->selected]) return;
            SeqProfile* p = &app->profiles[app->selected];
            char a[32], b[32], c[32], d[32];
            snprintf(a, sizeof(a), "Prefix %08lX", (unsigned long)p->frame.prefix);
            snprintf(b, sizeof(b), "Step %04X Word %04X", p->step, p->frame.word);
            snprintf(c, sizeof(c), "Index %04X", p->accumulator);
            snprintf(d, sizeof(d), "%u pulses TE %u us", 47u + p->frame.suffix_count, p->frame.te);
            app->screen = ScreenInfo;
            display(app, "Saved parameters", a, b, c, d, "Back Return");
        }
    } else if(app->screen == ScreenReady && event->key == InputKeyOk) start_capture(app);
    else if(app->screen == ScreenSave && event->key == InputKeyOk) {
        /* Replace both journal copies after learning, repairing any damaged copy. */
        app->tx_profile.generation = app->valid[app->selected] ?
            app->profiles[app->selected].generation : 0;
        if(write_profile(app, app->selected, &app->tx_profile) &&
           write_profile(app, app->selected, &app->tx_profile)) {
            app->profiles[app->selected] = app->tx_profile;
            app->valid[app->selected] = true;
            app->damaged[app->selected] = false;
            home(app, "Learned and saved.");
        } else {
            app->valid[app->selected] = false;
            app->damaged[app->selected] = true;
            home(app, "SD save failed. Relearn.");
        }
    }
}

int32_t acun_remote_app(void* argument) {
    UNUSED(argument);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(*app));
    app->running = true;
    app->ui_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->inputs = furi_message_queue_alloc(16, sizeof(InputEvent));
    app->pulses = furi_message_queue_alloc(512, sizeof(Pulse));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->viewport = view_port_alloc();
    view_port_draw_callback_set(app->viewport, draw, app);
    view_port_input_callback_set(app->viewport, input, app);
    subghz_devices_init();
    app->radio = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(app->radio);
    subghz_devices_begin(app->radio);
    furi_hal_power_insomnia_enter();
    load_profiles(app);
    home(app, "433.92 MHz / internal");
    gui_add_view_port(app->gui, app->viewport, GuiLayerFullscreen);
    while(app->running) {
        InputEvent event;
        if(furi_message_queue_get(app->inputs, &event, furi_ms_to_ticks(10)) == FuriStatusOk)
            handle_input(app, &event);
        if(app->rx_on) {
            if(app->overflow) {
                radio_stop(app);
                ready(app, "RX overflow. Try again.");
                continue;
            }
            Pulse pulse;
            size_t budget = 256;
            while(app->rx_on && budget-- &&
                  furi_message_queue_get(app->pulses, &pulse, 0) == FuriStatusOk) {
                SeqFrame frame;
                if(seq_decode(&app->decoder, pulse.level, pulse.duration, &frame)) received(app, &frame);
            }
            if(app->rx_on && furi_get_tick() - app->listen_started > furi_ms_to_ticks(CAPTURE_TIMEOUT_MS)) {
                radio_stop(app);
                ready(app, "No match. Retry button.");
            }
        }
    }
    radio_stop(app);
    subghz_devices_end(app->radio);
    subghz_devices_deinit();
    furi_hal_power_insomnia_exit();
    view_port_enabled_set(app->viewport, false);
    gui_remove_view_port(app->gui, app->viewport);
    view_port_free(app->viewport);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->pulses);
    furi_message_queue_free(app->inputs);
    furi_mutex_free(app->ui_mutex);
    free(app);
    return 0;
}
