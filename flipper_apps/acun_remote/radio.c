#include "radio.h"
#include <furi.h>
#include <furi_hal.h>
#include <subghz/devices/devices.h>
#include <subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <stdlib.h>
#include <string.h>

#define PULSE_QUEUE_DEPTH 512
#define TICK_BUDGET 512

typedef struct {
    bool level;
    uint32_t duration;
} Pulse;

struct Radio {
    const SubGhzDevice* device;
    FuriMessageQueue* pulses;
    volatile bool overflow;
    bool rx_on;
    bool tx_on;
    uint32_t started; /* tick of RX start / last press / TX start, for timeouts */
    SeqDecoder decoder;
    SeqFrame candidate;
    uint8_t repeats;
    SeqFrame last_press;
    bool has_last;
    SeqProfile tx_profile;
    size_t tx_position;
    uint8_t tx_repeat;
    bool tx_initial_gap;
};

static void radio_pulse_callback(bool level, uint32_t duration, void* context) {
    Radio* radio = context;
    Pulse pulse = {.level = level, .duration = duration};
    if(furi_message_queue_put(radio->pulses, &pulse, 0) != FuriStatusOk) radio->overflow = true;
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
    furi_message_queue_free(radio->pulses);
    free(radio);
}

void radio_stop(Radio* radio) {
    if(radio->rx_on) {
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
    radio_prepare(radio);
    radio->started = furi_get_tick();
    radio->rx_on = true;
    subghz_devices_start_async_rx(radio->device, radio_pulse_callback, radio);
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
        SeqFrame frame;
        if(!seq_decode(&radio->decoder, pulse.level, pulse.duration, &frame)) continue;
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
        radio->started = furi_get_tick();
        *press = frame;
        return RadioEventPress;
    }
    if(furi_get_tick() - radio->started > furi_ms_to_ticks(RADIO_RX_TIMEOUT_MS))
        return RadioEventRxTimeout;
    return RadioEventNone;
}
