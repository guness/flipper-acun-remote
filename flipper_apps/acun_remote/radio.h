#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sequence_core.h"

#define RADIO_TX_REPEATS 6
#define RADIO_TX_TIMEOUT_MS 3000

typedef enum {
    RadioEventNone,
    RadioEventPress, /* a press confirmed by two identical frames; see *press */
    RadioEventOverflow, /* pulse queue overflowed, capture is unreliable */
    RadioEventTxDone,
    RadioEventTxTimeout, /* RADIO_TX_TIMEOUT_MS without completion */
} RadioEvent;

typedef struct Radio Radio;

Radio* radio_alloc(void);
void radio_free(Radio* radio);
void radio_rx_start(Radio* radio);
bool radio_tx_start(Radio* radio, const SeqProfile* profile);
void radio_stop(Radio* radio);
bool radio_is_busy(const Radio* radio);
/* Poll from the GUI thread every ~10 ms. Repeats of the last confirmed press are dropped. */
RadioEvent radio_tick(Radio* radio, SeqFrame* press);
/* Live receive signal strength in dBm. Only meaningful while RX is running. */
float radio_rssi(const Radio* radio);
/* Diagnostics: every frame that passed pulse/timing validation, confirmed or
 * not (radio_tick only reports a press once two of these agree). Lets a
 * screen show that reception is timing-valid even when nothing is confirmed,
 * and whether what's being decoded is stable or flipping between readings. */
uint32_t radio_raw_count(const Radio* radio);
SeqFrame radio_raw_last(const Radio* radio);
/* Diagnostics: pipeline checkpoints upstream of radio_raw_count, to see where
 * reception stops making progress. edge_count is every pulse SubGhzWorker
 * delivers, i.e. every hardware edge that survived its glitch filter;
 * dequeued_count is every one of those handed to our decoder. edge_count
 * stuck at 0 means the radio isn't receiving (or the worker isn't running);
 * dequeued_count stuck while edge_count climbs means the queue is backed up
 * or overflowing; raw_count stuck while dequeued_count climbs means the
 * decoder itself is rejecting them. */
uint32_t radio_edge_count(const Radio* radio);
uint32_t radio_dequeued_count(const Radio* radio);
/* Diagnostics: how the decoder itself is faring once pulses reach it.
 * sync_count is how many times it found a gap quiet enough to start a fresh
 * attempt from; stuck at 0 despite dequeued_count climbing means the channel
 * never goes quiet long enough (SEQ_GAP_MIN_US) for it to ever begin.
 * max_run is the highest consecutive bit count reached before completing or
 * giving up; low every time (well under 47) means it's resyncing almost
 * immediately after starting, not failing near the finish. */
uint32_t radio_sync_count(const Radio* radio);
uint8_t radio_max_run(const Radio* radio);
/* Diagnostics: the exact pulse that most recently broke a mid-frame attempt
 * (made its bit count drop back to 0 without completing, on a pulse under
 * SEQ_GAP_MIN_US so it wasn't just the ordinary silence between presses).
 * fail_was_high says whether it was a high or a low pulse; fail_duration is
 * its length in microseconds; fail_at_count is how many bits had already
 * been accepted (matches radio_max_run at its peak). */
bool radio_fail_was_high(const Radio* radio);
uint32_t radio_fail_duration(const Radio* radio);
uint8_t radio_fail_at_count(const Radio* radio);
