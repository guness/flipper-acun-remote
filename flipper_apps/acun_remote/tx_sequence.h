#pragma once
#include "sequence_core.h"

/* Same repeat-count semantics as Unleashed's common protocol encoder: holds
 * do not consume repeats; after release, decrement at frame boundaries.
 * Six is Acun's repeat count, not a universal firmware/protocol constant. */
#define ACUN_TX_RELEASE_REPEATS 6

typedef struct {
    size_t position;
    uint8_t remaining;
    bool initial_gap;
} AcunTxSequence;

static inline void acun_tx_sequence_start(AcunTxSequence* tx) {
    tx->position = 0;
    tx->remaining = ACUN_TX_RELEASE_REPEATS;
    tx->initial_gap = true;
}

static inline bool acun_tx_sequence_next(
    AcunTxSequence* tx, const SeqFrame* frame, bool held, bool* level, uint32_t* duration) {
    if(!tx->remaining) return false;
    if(tx->initial_gap) {
        tx->initial_gap = false;
        *level = false;
        *duration = frame->gap;
        return true;
    }
    if(!seq_pulse(frame, tx->position++, level, duration)) return false;
    if(tx->position == 2u * (47u + frame->suffix_count)) {
        tx->position = 0;
        if(!held) --tx->remaining;
    }
    return true;
}
