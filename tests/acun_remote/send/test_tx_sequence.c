#include <assert.h>
#include <stdio.h>
#include "tx_sequence.h"

int main(void) {
    for(unsigned suffix = 0; suffix <= 8; suffix += 2) {
        SeqFrame frame = {.prefix = 0x1f550c4, .word = 0x64e6,
                          .suffix_count = suffix, .te = 406, .gap = 9588};
        const size_t size = 2 * (47 + suffix);
        /* Release at every possible point, including an immediate quick tap. */
        for(size_t release_at = 0; release_at < size; ++release_at) {
            AcunTxSequence tx;
            bool level, expected_level;
            uint32_t duration, expected_duration;
            acun_tx_sequence_start(&tx);
            assert(acun_tx_sequence_next(&tx, &frame, true, &level, &duration));
            assert(!level && duration == frame.gap);
            /* A long hold must not spend the release repeat budget. */
            for(size_t i = 0; i < 300 * size + release_at; ++i) {
                assert(acun_tx_sequence_next(&tx, &frame, true, &level, &duration));
                assert(seq_pulse(&frame, i % size, &expected_level, &expected_duration));
                assert(level == expected_level && duration == expected_duration);
            }
            assert(tx.remaining == ACUN_TX_RELEASE_REPEATS);
            size_t count = 0;
            while(acun_tx_sequence_next(&tx, &frame, false, &level, &duration)) {
                assert(seq_pulse(&frame, (release_at + count) % size,
                                 &expected_level, &expected_duration));
                assert(level == expected_level && duration == expected_duration);
                ++count;
                assert(count <= ACUN_TX_RELEASE_REPEATS * size);
            }
            assert(count == ACUN_TX_RELEASE_REPEATS * size - release_at);
            assert(tx.position == 0 && tx.remaining == 0);
            assert(!level && duration == frame.gap); /* Ends after a complete gap. */
        }
        AcunTxSequence tap;
        acun_tx_sequence_start(&tap);
        bool level;
        uint32_t duration;
        size_t count = 0;
        while(acun_tx_sequence_next(&tap, &frame, false, &level, &duration)) ++count;
        assert(count == 1 + ACUN_TX_RELEASE_REPEATS * size);
    }
    puts("TX sequence: long hold, quick tap and release at every pulse boundary passed");
}
