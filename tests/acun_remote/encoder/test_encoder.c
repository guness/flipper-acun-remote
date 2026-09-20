#include "tx_encoder.h"
#include "tx_sequence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
uint32_t furi_get_tick(void) {
    return tick;
}

int main(void) {
    assert(acun_tx_registry.size == 1);
    const SubGhzProtocol* protocol = acun_tx_registry.items[0];
    assert(strcmp(protocol->name, ACUN_PROTOCOL_NAME) == 0);
    const SubGhzProtocolEncoder* api = protocol->encoder;
    AcunTxEncoder* encoder = api->alloc(NULL);
    assert(((SubGhzProtocolEncoderBase*)encoder)->protocol == protocol);
    assert(api->yield(encoder).duration == 0);
    for(unsigned tail = 0; tail <= 3; ++tail) {
        SeqFrame frame = {.prefix = 0x1f50394, .word = 0x81ab,
                          .suffix_count = 2, .suffix = tail, .te = 407, .gap = 9598};
        const size_t size = 98;
        for(size_t release = 0; release < size; ++release) {
            tick = 1;
            acun_tx_encoder_start(encoder, &frame);
            assert(acun_tx_encoder_progress(encoder) == tick);
            LevelDuration pulse = api->yield(encoder);
            assert(!pulse.level && pulse.duration == frame.gap);
            /* Verify the adapter preserves every pulse through a long hold. */
            for(size_t i = 0; i < 300 * size + release; ++i) {
                bool level;
                uint32_t duration;
                ++tick;
                pulse = api->yield(encoder);
                assert(seq_pulse(&frame, i % size, &level, &duration));
                assert(pulse.level == level && pulse.duration == duration);
                if((i + 1) % size == 0) assert(acun_tx_encoder_progress(encoder) == tick);
            }
            acun_tx_encoder_release(encoder);
            size_t count = 0;
            while((pulse = api->yield(encoder)).duration) {
                bool level;
                uint32_t duration;
                assert(seq_pulse(&frame, (release + count) % size, &level, &duration));
                assert(pulse.level == level && pulse.duration == duration);
                assert(++count <= ACUN_TX_RELEASE_REPEATS * size);
            }
            assert(count == ACUN_TX_RELEASE_REPEATS * size - release);
            acun_tx_encoder_start(encoder, &frame);
            acun_tx_encoder_release(encoder);
            count = 0;
            while(api->yield(encoder).duration) assert(++count <= 1 + 6 * size);
            assert(count == 1 + 6 * size);
            acun_tx_encoder_start(encoder, &frame);
            assert(api->yield(encoder).duration == frame.gap);
            api->stop(encoder);
            assert(api->yield(encoder).duration == 0);
        }
    }
    api->free(encoder);
    puts("Native encoder adapter: waveform, hold, release, cancel and restart passed");
}
