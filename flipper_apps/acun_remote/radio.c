#include "radio.h"
#include "tx_sequence.h"
#include <furi.h>
#include <furi_hal.h>
#include <subghz/devices/devices.h>
#include <subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/subghz_worker.h>
#include <stdlib.h>
#include <string.h>

#define PULSE_QUEUE_DEPTH 512
#define TICK_BUDGET 512
/* A held button repeats the same frame; this many consecutive identical
 * decodes confirm a press. Two was not enough: a mid-transmission RF fade
 * can leave a decoded frame with its trailing bits reading as a stable run
 * of zero (the noise floor happens to look like valid low pulses), and that
 * corrupted result can repeat identically across two consecutive repeats of
 * the same physical press. Three makes that far less likely to coincide. */
#define RADIO_PRESS_CONFIRM_FRAMES 3
/* Below this, an edge is RF ringing/glitch, not a real transition; SubGhzWorker
 * merges it into the pulse it interrupted instead of splitting that pulse in
 * two, before we ever see it. Same default the firmware's own protocol
 * decoders run behind. */
#define RADIO_GLITCH_FILTER_US 30

typedef struct {
    bool level;
    uint32_t duration;
} Pulse;

struct Radio {
    const SubGhzDevice* device;
    SubGhzWorker* worker; /* hardware capture -> glitch filter -> our pair callback */
    FuriMessageQueue* pulses;
    volatile bool overflow;
    bool rx_on;
    bool tx_on;
    volatile uint32_t tx_progress; /* Last frame queued by the async TX callback. */
    SeqDecoder decoder;
    SeqTimingDecoder timing_decoder;
    SeqFrame timing_frame;
    uint8_t timing_repeats;
    SeqFrame candidate;
    uint8_t repeats;
    SeqFrame last_press;
    bool has_last;
    SeqProfile tx_profile;
    AcunTxSequence tx_sequence;
    volatile bool tx_held;
};

/* SubGhzWorker's own thread, already past its glitch filter: just queue it for
 * radio_tick() to decode on the GUI thread. */
static void radio_pair_callback(void* context, bool level, uint32_t duration) {
    Radio* radio = context;
    Pulse pulse = {.level = level, .duration = duration};
    if(furi_message_queue_put(radio->pulses, &pulse, 0) != FuriStatusOk) radio->overflow = true;
}

static void radio_overrun_callback(void* context) {
    Radio* radio = context;
    radio->overflow = true;
}

static LevelDuration radio_tx_callback(void* context) {
    Radio* radio = context;
    bool level;
    uint32_t duration;
    if(!acun_tx_sequence_next(
           &radio->tx_sequence, &radio->tx_profile.frame, radio->tx_held, &level, &duration))
        return level_duration_reset();
    if(radio->tx_sequence.position == 0) radio->tx_progress = furi_get_tick();
    return level_duration_make(level, duration);
}

static void radio_prepare(Radio* radio) {
    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_load_preset(radio->device, FuriHalSubGhzPresetOok270Async, NULL);
    subghz_devices_set_frequency(radio->device, SEQ_FREQUENCY);
}

Radio* radio_alloc(void) {
    Radio* radio = malloc(sizeof(Radio));
    memset(radio, 0, sizeof(*radio));
    radio->pulses = furi_message_queue_alloc(PULSE_QUEUE_DEPTH, sizeof(Pulse));
    radio->worker = subghz_worker_alloc();
    subghz_worker_set_context(radio->worker, radio);
    subghz_worker_set_pair_callback(radio->worker, radio_pair_callback);
    subghz_worker_set_overrun_callback(radio->worker, radio_overrun_callback);
    subghz_worker_set_filter(radio->worker, RADIO_GLITCH_FILTER_US);
    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(radio->device);
    subghz_devices_begin(radio->device);
    return radio;
}

void radio_free(Radio* radio) {
    radio_stop(radio);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    subghz_worker_free(radio->worker);
    furi_message_queue_free(radio->pulses);
    free(radio);
}

void radio_stop(Radio* radio) {
    if(radio->rx_on) {
        subghz_worker_stop(radio->worker);
        subghz_devices_stop_async_rx(radio->device);
        radio->rx_on = false;
    }
    if(radio->tx_on) {
        subghz_devices_stop_async_tx(radio->device);
        radio->tx_on = false;
    }
    subghz_devices_idle(radio->device);
    subghz_devices_sleep(radio->device);
}

void radio_rx_start(Radio* radio) {
    radio_stop(radio);
    furi_message_queue_reset(radio->pulses);
    radio->overflow = false;
    radio->repeats = 0;
    radio->has_last = false;
    seq_decoder_reset(&radio->decoder);
    seq_timing_reset(&radio->timing_decoder);
    radio->timing_repeats = 0;
    radio_prepare(radio);
    radio->rx_on = true;
    subghz_devices_start_async_rx(radio->device, subghz_worker_rx_callback, radio->worker);
    subghz_worker_start(radio->worker);
}

bool radio_tx_start(Radio* radio, const SeqProfile* profile) {
    radio_stop(radio);
    radio->tx_profile = *profile;
    acun_tx_sequence_start(&radio->tx_sequence);
    radio->tx_held = true;
    radio_prepare(radio);
    radio->tx_progress = furi_get_tick();
    radio->tx_on = subghz_devices_start_async_tx(radio->device, radio_tx_callback, radio);
    if(!radio->tx_on) radio_stop(radio);
    return radio->tx_on;
}

void radio_tx_release(Radio* radio) {
    radio->tx_held = false;
}

bool radio_is_busy(const Radio* radio) {
    return radio->rx_on || radio->tx_on;
}

RadioEvent radio_tick(Radio* radio, SeqFrame* press) {
    if(radio->tx_on) {
        if(subghz_devices_is_async_complete_tx(radio->device)) return RadioEventTxDone;
        const uint32_t progress = radio->tx_progress;
        if(furi_get_tick() - progress > furi_ms_to_ticks(RADIO_TX_TIMEOUT_MS))
            return RadioEventTxTimeout;
        return RadioEventNone;
    }
    if(!radio->rx_on) return RadioEventNone;
    if(radio->overflow) return RadioEventOverflow;
    Pulse pulse;
    size_t budget = TICK_BUDGET;
    while(budget-- && furi_message_queue_get(radio->pulses, &pulse, 0) == FuriStatusOk) {
        SeqFrame timed;
        if(seq_timing_decode(&radio->timing_decoder, pulse.level, pulse.duration, &timed)) {
            if(radio->timing_repeats && seq_same_frame(&radio->timing_frame, &timed) &&
               radio->timing_frame.suffix_count == timed.suffix_count &&
               radio->timing_frame.suffix == timed.suffix) {
                if(radio->timing_repeats < 2) ++radio->timing_repeats;
            } else {
                radio->timing_repeats = 1;
            }
            radio->timing_frame = timed;
        }
        SeqFrame frame;
        if(!seq_decode(&radio->decoder, pulse.level, pulse.duration, &frame)) continue;
        /* Two complete matching frames establish the playback shape. The
         * immediate decoder still confirms identity, including fast repeats
         * with no long gap; those retain its existing timing fallback. */
        if(radio->timing_repeats >= 2 && seq_same_frame(&frame, &radio->timing_frame))
            frame = radio->timing_frame;
        if(radio->has_last && seq_same_frame(&radio->last_press, &frame)) continue;
        if(radio->repeats && seq_same_frame(&radio->candidate, &frame)) {
            ++radio->repeats;
        } else {
            radio->candidate = frame;
            radio->repeats = 1;
        }
        if(radio->repeats < RADIO_PRESS_CONFIRM_FRAMES) continue;
        radio->last_press = frame;
        radio->has_last = true;
        radio->repeats = 0;
        *press = frame;
        return RadioEventPress;
    }
    return RadioEventNone;
}

float radio_rssi(const Radio* radio) {
    return subghz_devices_get_rssi(radio->device);
}
