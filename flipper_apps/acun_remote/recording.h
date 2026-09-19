#pragma once
#include "sequence_core.h"

/* Incremental .sub reader. The caller supplies complete lines, without LF. */
typedef struct {
    SeqTimingDecoder decoder;
    SeqFrame candidate;
    SeqFrame result;
    uint32_t te;
    uint32_t bit_count;
    uint8_t repeats;
    uint8_t protocol; /* 1 = BinRAW, 2 = RAW */
    bool filetype;
    bool version;
    bool frequency;
    bool preset;
    bool failed;
    bool found;
    bool data_seen;
} AcunRecording;

void acun_recording_init(AcunRecording* recording);
bool acun_recording_line(AcunRecording* recording, const char* line);
bool acun_recording_finish(const AcunRecording* recording, SeqFrame* frame);
