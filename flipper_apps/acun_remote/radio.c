#include "radio.h"
#include <furi.h>
#include <furi_hal.h>
#include <subghz/devices/devices.h>
#include <subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/subghz_worker.h>
#include <stdlib.h>
#include <string.h>

#define PULSE_QUEUE_DEPTH 512
#define TICK_BUDGET 512
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
    uint32_t started; /* tick of RX start / last press / TX start, for timeouts */
    uint32_t edge_count; /* every pulse the worker delivered, after its glitch filter */
    SeqDecoder decoder;
    SeqFrame candidate;
    uint8_t repeats;
    SeqFrame last_press;
    bool has_last;
    uint32_t dequeued_count; /* every pulse handed to seq_decode, decoded or not */
    uint32_t sync_count; /* how many times the decoder found a quiet enough gap to start from */
    uint8_t max_run; /* highest consecutive bit count reached before completing or failing */
    bool fail_was_high; /* the exact pulse that last broke a mid-frame attempt */
    uint32_t fail_duration;
    uint8_t fail_at_count;
    uint32_t raw_count; /* every successfully decoded frame, confirmed or not */
    SeqFrame raw_last;
    SeqProfile tx_profile;
    size_t tx_position;
    uint8_t tx_repeat;
    bool tx_initial_gap;
};

/* SubGhzWorker's own thread, already past its glitch filter: just queue it for
 * radio_tick() to decode on the GUI thread. */
static void radio_pair_callback(void* context, bool level, uint32_t duration) {
    Radio* radio = context;
    ++radio->edge_count;
    Pulse pulse = {.level = level, .duration = duration};
    if(furi_message_queue_put(radio->pulses, &pulse, 0) != FuriStatusOk) radio->overflow = true;
}

static void radio_overrun_callback(void* context) {
    Radio* radio = context;
    radio->overflow = true;
}

static LevelDuration radio_tx_callback(void* context) {
    Radio* radio = context;
    if(radio->tx_initial_gap) {
        radio->tx_initial_gap = false;
        return level_duration_make(false, radio->tx_profile.frame.gap);
    }
    if(radio->tx_repeat >= RADIO_TX_REPEATS) return level_duration_reset();
    bool level;
    uint32_t duration;
    if(!seq_pulse(&radio->tx_profile.frame, radio->tx_position++, &level, &duration))
        return level_duration_reset();
    if(radio->tx_position == 2u * (47u + radio->tx_profile.frame.suffix_count)) {
        radio->tx_position = 0;
        ++radio->tx_repeat;
    }
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
    radio->edge_count = 0;
    radio->repeats = 0;
    radio->has_last = false;
    radio->dequeued_count = 0;
    radio->sync_count = 0;
    radio->max_run = 0;
    radio->fail_was_high = false;
    radio->fail_duration = 0;
    radio->fail_at_count = 0;
    radio->raw_count = 0;
    memset(&radio->raw_last, 0, sizeof(radio->raw_last));
    seq_decoder_reset(&radio->decoder);
    radio_prepare(radio);
    radio->rx_on = true;
    subghz_devices_start_async_rx(radio->device, subghz_worker_rx_callback, radio->worker);
    subghz_worker_start(radio->worker);
}

bool radio_tx_start(Radio* radio, const SeqProfile* profile) {
    radio_stop(radio);
    radio->tx_profile = *profile;
    radio->tx_position = 0;
    radio->tx_repeat = 0;
    radio->tx_initial_gap = true;
    radio_prepare(radio);
    radio->started = furi_get_tick();
    radio->tx_on = subghz_devices_start_async_tx(radio->device, radio_tx_callback, radio);
    if(!radio->tx_on) radio_stop(radio);
    return radio->tx_on;
}

bool radio_is_busy(const Radio* radio) {
    return radio->rx_on || radio->tx_on;
}

RadioEvent radio_tick(Radio* radio, SeqFrame* press) {
    if(radio->tx_on) {
        if(subghz_devices_is_async_complete_tx(radio->device)) return RadioEventTxDone;
        if(furi_get_tick() - radio->started > furi_ms_to_ticks(RADIO_TX_TIMEOUT_MS))
            return RadioEventTxTimeout;
        return RadioEventNone;
    }
    if(!radio->rx_on) return RadioEventNone;
    if(radio->overflow) return RadioEventOverflow;
    Pulse pulse;
    size_t budget = TICK_BUDGET;
    while(budget-- && furi_message_queue_get(radio->pulses, &pulse, 0) == FuriStatusOk) {
        ++radio->dequeued_count;
        bool was_synchronized = radio->decoder.synchronized;
        uint8_t count_before = radio->decoder.count;
        if(count_before > radio->max_run) radio->max_run = count_before;
        SeqFrame frame;
        bool completed = seq_decode(&radio->decoder, pulse.level, pulse.duration, &frame);
        if(!was_synchronized && radio->decoder.synchronized) ++radio->sync_count;
        /* A mid-frame pulse that made count drop back to 0 without completing
         * is the exact pulse that broke validation, below SEQ_GAP_MIN_US so
         * it isn't just the ordinary silence between presses. */
        if(!completed && count_before > 0 && radio->decoder.count == 0 &&
           pulse.duration < SEQ_GAP_MIN_US) {
            radio->fail_was_high = pulse.level;
            radio->fail_duration = pulse.duration;
            radio->fail_at_count = count_before;
        }
        if(!completed) continue;
        ++radio->raw_count;
        radio->raw_last = frame;
        if(radio->has_last && seq_same_frame(&radio->last_press, &frame)) continue;
        if(radio->repeats && seq_same_frame(&radio->candidate, &frame)) {
            ++radio->repeats;
        } else {
            radio->candidate = frame;
            radio->repeats = 1;
        }
        if(radio->repeats < 2) continue; /* a held press repeats; two frames confirm it */
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

uint32_t radio_raw_count(const Radio* radio) {
    return radio->raw_count;
}

SeqFrame radio_raw_last(const Radio* radio) {
    return radio->raw_last;
}

uint32_t radio_edge_count(const Radio* radio) {
    return radio->edge_count;
}

uint32_t radio_dequeued_count(const Radio* radio) {
    return radio->dequeued_count;
}

uint32_t radio_sync_count(const Radio* radio) {
    return radio->sync_count;
}

uint8_t radio_max_run(const Radio* radio) {
    return radio->max_run;
}

bool radio_fail_was_high(const Radio* radio) {
    return radio->fail_was_high;
}

uint32_t radio_fail_duration(const Radio* radio) {
    return radio->fail_duration;
}

uint8_t radio_fail_at_count(const Radio* radio) {
    return radio->fail_at_count;
}
