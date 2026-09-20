#pragma once

#include "sequence_core.h"
#include <subghz/protocols/base.h>
#include <subghz/registry.h>

#define ACUN_PROTOCOL_NAME "Acun"

typedef struct AcunTxEncoder AcunTxEncoder;

extern const SubGhzProtocolRegistry acun_tx_registry;

/* Configure only while async TX is stopped, after the caller persists the code. */
void acun_tx_encoder_start(AcunTxEncoder* encoder, const SeqFrame* frame);
void acun_tx_encoder_release(AcunTxEncoder* encoder);
uint32_t acun_tx_encoder_progress(const AcunTxEncoder* encoder);
