#include "sequence_core.h"
#include <string.h>

uint16_t seq_permute(uint16_t word) {
    if(((word >> 0) ^ (word >> 13)) & 1) word ^= (1u << 0) | (1u << 13);
    if(((word >> 5) ^ (word >> 8)) & 1) word ^= (1u << 5) | (1u << 8);
    return word;
}

bool seq_same_button(const SeqFrame* a, const SeqFrame* b) {
    return a->prefix == b->prefix && ((a->word ^ b->word) & 0x8000) == 0 &&
           a->suffix == b->suffix && a->suffix_count == b->suffix_count;
}

bool seq_same_frame(const SeqFrame* a, const SeqFrame* b) {
    return seq_same_button(a, b) && a->word == b->word;
}

static uint16_t seq_word(uint16_t accumulator, uint16_t flag) {
    return flag | seq_permute(accumulator >> 1);
}

bool seq_fit(const SeqFrame frames[SEQ_LEARN_COUNT], SeqProfile* result) {
    uint16_t z[SEQ_LEARN_COUNT];
    for(size_t i = 0; i < SEQ_LEARN_COUNT; ++i) {
        if(!seq_same_button(&frames[0], &frames[i])) return false;
        if(i && frames[i].word == frames[i - 1].word) return false;
        z[i] = seq_permute(frames[i].word & 0x7FFF);
    }
    /* First three presses fit; the last two are independent validation. */
    uint16_t d0 = (z[1] - z[0]) & 0x7FFF;
    uint16_t d1 = (z[2] - z[1]) & 0x7FFF;
    uint16_t step = d0 + d1;
    if(!step) return false;
    bool found = false;
    uint16_t seed = 0;
    for(uint16_t low = 0; low < 2; ++low) {
        uint16_t candidate = (z[0] << 1) | low;
        bool valid = true;
        for(size_t i = 0; i < SEQ_LEARN_COUNT; ++i) {
            uint16_t u = candidate + (uint32_t)i * step;
            if((u >> 1) != z[i]) valid = false;
        }
        if(valid) {
            /* Two candidates for an even step are permanently output-equivalent. */
            if(found && (step & 1)) return false;
            if(!found) seed = candidate;
            found = true;
        }
    }
    if(!found) return false;
    memset(result, 0, sizeof(*result));
    result->frame = frames[SEQ_LEARN_COUNT - 1];
    result->step = step;
    result->accumulator = seed + (SEQ_LEARN_COUNT - 1u) * step;
    return true;
}

void seq_advance(SeqProfile* profile) {
    profile->accumulator += profile->step;
    profile->frame.word = seq_word(profile->accumulator, profile->frame.word & 0x8000);
    ++profile->sends;
}

bool seq_sync(SeqProfile* profile, const SeqFrame* heard, int32_t* delta) {
    if(!seq_same_button(&profile->frame, heard)) return false;
    uint16_t z = seq_permute(heard->word & 0x7FFF);
    uint16_t acc = profile->accumulator;
    bool found = false;
    int32_t best = 0;
    uint16_t best_acc = 0;
    /* Two accumulators fit a 15-bit word; keep the one nearest in signed presses. */
    for(uint32_t k = 0; k < 65536u; ++k, acc += profile->step) {
        if((acc >> 1) != z) continue;
        int32_t d = k < 32768u ? (int32_t)k : (int32_t)k - 65536;
        int32_t mag = d < 0 ? -d : d;
        int32_t best_mag = best < 0 ? -best : best;
        if(!found || mag < best_mag || (mag == best_mag && d > best)) {
            best = d;
            best_acc = acc;
            found = true;
        }
    }
    if(!found) return false;
    profile->accumulator = best_acc;
    profile->frame.word = heard->word;
    *delta = best;
    return true;
}

void seq_decoder_reset(SeqDecoder* decoder) {
    memset(decoder, 0, sizeof(*decoder));
}

bool seq_decode(SeqDecoder* d, bool level, uint32_t duration, SeqFrame* frame) {
    if(!level && duration >= 6000) {
        bool valid = d->synchronized && d->pending_low && d->count >= 47 &&
                     d->count <= SEQ_MAX_BITS && duration <= 60000;
        if(valid) {
            uint8_t tail = d->count - 47;
            uint64_t value = d->bits >> tail;
            memset(frame, 0, sizeof(*frame));
            frame->prefix = value >> 16;
            frame->word = value & 0xFFFF;
            frame->suffix_count = tail;
            frame->suffix = d->bits & ((1u << tail) - 1u);
            frame->te = d->units_sum / d->count;
            frame->gap = duration;
            valid = frame->te >= 250 && frame->te <= 550;
        }
        seq_decoder_reset(d);
        d->synchronized = true;
        return valid;
    }
    if(!d->synchronized) return false;
    if(level) {
        bool short_pulse = duration >= 200 && duration <= 650;
        bool long_pulse = duration >= 800 && duration <= 1650;
        if(d->pending_low || d->count == SEQ_MAX_BITS || (!short_pulse && !long_pulse)) {
            seq_decoder_reset(d);
            return false;
        }
        d->bits = (d->bits << 1) | long_pulse;
        d->units_sum += duration / (long_pulse ? 3u : 1u);
        ++d->count;
        d->pending_low = true;
    } else {
        bool high_long = d->bits & 1;
        bool valid_low = high_long ? duration >= 200 && duration <= 750 :
                                    duration >= 800 && duration <= 1800;
        if(!d->pending_low || !valid_low) {
            seq_decoder_reset(d);
            return false;
        }
        d->pending_low = false;
    }
    return false;
}

bool seq_pulse(const SeqFrame* frame, size_t index, bool* level, uint32_t* duration) {
    size_t count = 47u + frame->suffix_count;
    if(frame->suffix_count > 8 || index >= 2u * count) return false;
    uint64_t bits = ((((uint64_t)frame->prefix << 16) | frame->word) <<
                     frame->suffix_count) | frame->suffix;
    bool bit = (bits >> (count - 1u - index / 2u)) & 1;
    *level = !(index & 1);
    *duration = frame->te * (*level ? (bit ? 3u : 1u) : (bit ? 1u : 3u));
    if(index == 2u * count - 1u) *duration = frame->gap;
    return true;
}

static void put32(uint8_t* b, uint32_t v) {
    for(size_t i = 0; i < 4; ++i) b[i] = v >> (8u * i);
}

static uint32_t get32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static uint32_t checksum(const uint8_t* b, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for(size_t i = 0; i < n; ++i) {
        crc ^= b[i];
        for(size_t j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void seq_pack(const SeqProfile* p, uint8_t b[SEQ_RECORD_SIZE]) {
    memset(b, 0, SEQ_RECORD_SIZE);
    memcpy(b, "SEQREM01", 8);
    put32(b + 8, p->generation);
    put32(b + 12, SEQ_FREQUENCY);
    put32(b + 16, p->frame.prefix);
    put32(b + 20, p->frame.word);
    put32(b + 24, p->step);
    put32(b + 28, p->accumulator);
    put32(b + 32, p->sends);
    put32(b + 36, p->frame.te);
    put32(b + 40, p->frame.gap);
    put32(b + 44, p->frame.suffix);
    put32(b + 48, p->frame.suffix_count);
    put32(b + 60, checksum(b, 60));
}

bool seq_unpack(const uint8_t b[SEQ_RECORD_SIZE], SeqProfile* p) {
    if(memcmp(b, "SEQREM01", 8) || get32(b + 60) != checksum(b, 60) ||
       get32(b + 12) != SEQ_FREQUENCY || get32(b + 16) > 0x7FFFFFFF ||
       get32(b + 20) > 0xFFFF || !get32(b + 24) || get32(b + 24) > 0xFFFF ||
       get32(b + 28) > 0xFFFF || get32(b + 36) < 250 || get32(b + 36) > 550 ||
       get32(b + 40) < 6000 || get32(b + 40) > 60000 || get32(b + 48) > 8 ||
       get32(b + 44) >= (1u << get32(b + 48))) return false;
    memset(p, 0, sizeof(*p));
    p->generation = get32(b + 8);
    p->frame.prefix = get32(b + 16);
    p->frame.word = get32(b + 20);
    p->step = get32(b + 24);
    p->accumulator = get32(b + 28);
    p->sends = get32(b + 32);
    p->frame.te = get32(b + 36);
    p->frame.gap = get32(b + 40);
    p->frame.suffix = get32(b + 44);
    p->frame.suffix_count = get32(b + 48);
    return seq_word(p->accumulator, p->frame.word & 0x8000) == p->frame.word;
}
