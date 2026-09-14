#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SEQ_LEARN_COUNT 5
#define SEQ_MAX_BITS 55
#define SEQ_RECORD_SIZE 64
#define SEQ_FREQUENCY 433920000u

typedef struct {
    uint32_t prefix;
    uint16_t word;
    uint8_t suffix;
    uint8_t suffix_count;
    uint16_t te;
    uint32_t gap;
} SeqFrame;

typedef struct {
    SeqFrame frame;
    uint16_t step;
    uint16_t accumulator; /* Last observed/reserved value, including hidden bit. */
    uint32_t sends;
    uint32_t generation;
} SeqProfile;

typedef struct {
    bool synchronized;
    bool pending_low;
    uint8_t count;
    uint64_t bits;
    uint32_t units_sum;
} SeqDecoder;

uint16_t seq_permute(uint16_t word);
bool seq_same_button(const SeqFrame* a, const SeqFrame* b);
bool seq_same_frame(const SeqFrame* a, const SeqFrame* b);
bool seq_fit(const SeqFrame frames[SEQ_LEARN_COUNT], SeqProfile* result);
void seq_advance(SeqProfile* profile);
void seq_decoder_reset(SeqDecoder* decoder);
bool seq_decode(SeqDecoder* decoder, bool level, uint32_t duration, SeqFrame* frame);
/* Sequence of high/low pulses, final low is the learned inter-frame gap. */
bool seq_pulse(const SeqFrame* frame, size_t index, bool* level, uint32_t* duration);
void seq_pack(const SeqProfile* profile, uint8_t bytes[SEQ_RECORD_SIZE]);
bool seq_unpack(const uint8_t bytes[SEQ_RECORD_SIZE], SeqProfile* profile);
