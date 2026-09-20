#include "tx_encoder.h"
#include "tx_sequence.h"
#include <stdlib.h>

struct AcunTxEncoder {
    SubGhzProtocolEncoderBase base;
    SeqFrame frame;
    AcunTxSequence sequence;
    volatile bool held;
    volatile uint32_t progress;
};

static const SubGhzProtocol acun_protocol;

static void* acun_encoder_alloc(SubGhzEnvironment* environment) {
    (void)environment;
    AcunTxEncoder* encoder = malloc(sizeof(*encoder));
    *encoder = (AcunTxEncoder){.base = {.protocol = &acun_protocol}};
    return encoder;
}

static void acun_encoder_free(void* context) {
    free(context);
}

static void acun_encoder_stop(void* context) {
    AcunTxEncoder* encoder = context;
    encoder->held = false;
    encoder->sequence.remaining = 0;
}

static LevelDuration acun_encoder_yield(void* context) {
    AcunTxEncoder* encoder = context;
    bool level;
    uint32_t duration;
    if(!acun_tx_sequence_next(
           &encoder->sequence, &encoder->frame, encoder->held, &level, &duration))
        return level_duration_reset();
    if(encoder->sequence.position == 0) encoder->progress = furi_get_tick();
    return level_duration_make(level, duration);
}

static const SubGhzProtocolEncoder acun_encoder = {
    .alloc = acun_encoder_alloc,
    .free = acun_encoder_free,
    .stop = acun_encoder_stop,
    .yield = acun_encoder_yield,
};

static const SubGhzProtocol acun_protocol = {
    .name = ACUN_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Send,
    .encoder = &acun_encoder,
};

static const SubGhzProtocol* const acun_protocols[] = {&acun_protocol};

const SubGhzProtocolRegistry acun_tx_registry = {
    .items = acun_protocols,
    .size = 1,
};

void acun_tx_encoder_start(AcunTxEncoder* encoder, const SeqFrame* frame) {
    encoder->frame = *frame;
    acun_tx_sequence_start(&encoder->sequence);
    encoder->held = true;
    encoder->progress = furi_get_tick();
}

void acun_tx_encoder_release(AcunTxEncoder* encoder) {
    encoder->held = false;
}

uint32_t acun_tx_encoder_progress(const AcunTxEncoder* encoder) {
    return encoder->progress;
}
