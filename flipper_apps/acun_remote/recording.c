#include "recording.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void acun_recording_init(AcunRecording* r) {
    memset(r, 0, sizeof(*r));
}

static bool number(const char** text, long* out, int base) {
    const char* start = *text;
    while(isspace((unsigned char)*start)) ++start;
    errno = 0;
    char* end;
    long value = strtol(start, &end, base);
    if(end == start || errno || (*end && !isspace((unsigned char)*end))) return false;
    *text = end;
    *out = value;
    return true;
}

static bool scalar(const char* text, long* out) {
    if(!number(&text, out, 10)) return false;
    while(isspace((unsigned char)*text)) ++text;
    return !*text;
}

static bool same_shape(const SeqFrame* a, const SeqFrame* b) {
    return seq_same_frame(a, b) && a->suffix_count == b->suffix_count && a->suffix == b->suffix;
}

static void accept(AcunRecording* r, const SeqFrame* frame) {
    if(r->found && !same_shape(&r->result, frame)) {
        r->failed = true; /* Never silently choose among multiple recorded presses. */
        return;
    }
    r->result = *frame;
    r->found = true;
}

static void raw_pulse(AcunRecording* r, bool level, uint32_t duration) {
    SeqFrame frame;
    if(!seq_timing_decode(&r->decoder, level, duration, &frame)) return;
    if(r->repeats && same_shape(&r->candidate, &frame)) {
        if(r->repeats < 3) ++r->repeats;
    } else {
        r->repeats = 1;
    }
    r->candidate = frame;
    if(r->repeats >= 3) accept(r, &frame);
}

static bool binraw(AcunRecording* r, const char* text) {
    if(!r->bit_count || r->bit_count > 4096 || r->te < 250 || r->te > 550) return false;
    /* Bytes are right-aligned, as in the firmware BinRAW encoder. */
    unsigned bytes = (r->bit_count + 7) / 8;
    unsigned padding = bytes * 8 - r->bit_count;
    bool last = false;
    bool started = false;
    uint32_t run = 0, leading_gap = 0;
    SeqTimingDecoder decoder;
    SeqFrame frame;
    unsigned hits = 0;
    seq_timing_reset(&decoder);
    for(unsigned i = 0; i < bytes; ++i) {
        long byte;
        if(!number(&text, &byte, 16) || byte < 0 || byte > 255) return false;
        for(unsigned bit = (i == 0 ? padding : 0); bit < 8; ++bit) {
            bool level = (byte >> (7 - bit)) & 1;
            if(!started) {
                if(level) return false; /* No recorded leading silence. */
                started = true;
                last = level;
            }
            if(level == last) {
                run += r->te;
            } else {
                if(!leading_gap) {
                    leading_gap = run;
                    if(leading_gap < SEQ_GAP_MIN_US || leading_gap > 60000) return false;
                }
                if(seq_timing_decode(&decoder, last, run, &frame)) ++hits;
                last = level;
                run = r->te;
            }
        }
    }
    while(isspace((unsigned char)*text)) ++text;
    if(*text || !leading_gap) return false;
    if(last && seq_timing_decode(&decoder, true, run, &frame)) ++hits;
    /* A BinRAW block starts with its inter-frame low; close the last symbol
     * using that same recorded duration, rather than an invented default. */
    if(seq_timing_decode(&decoder, false, leading_gap, &frame)) ++hits;
    if(hits != 1) return false;
    accept(r, &frame);
    r->bit_count = 0;
    return !r->failed;
}

bool acun_recording_line(AcunRecording* r, const char* line) {
    if(r->failed) return false;
    if(!*line || *line == '#') return true;
    const char* value = strstr(line, ": ");
    if(!value) return r->failed = true, false;
    size_t key_len = value - line;
    value += 2;
    long n;
#define KEY(k) (key_len == sizeof(k) - 1 && !strncmp(line, k, key_len))
    bool ok = true;
    if(KEY("Filetype")) {
        ok = !r->data_seen && !r->filetype &&
             (!strcmp(value, "Flipper SubGhz Key File") || !strcmp(value, "Flipper SubGhz RAW File"));
        r->filetype = ok;
    } else if(KEY("Version")) {
        ok = !r->data_seen && !r->version && scalar(value, &n) && n == 1;
        r->version = ok;
    } else if(KEY("Frequency")) {
        ok = !r->data_seen && !r->frequency && scalar(value, &n) && n == SEQ_FREQUENCY;
        r->frequency = ok;
    } else if(KEY("Preset")) {
        ok = !r->data_seen && !r->preset && !strcmp(value, "FuriHalSubGhzPresetOok270Async");
        r->preset = ok;
    } else if(KEY("Protocol")) {
        ok = !r->data_seen && !r->protocol;
        if(!strcmp(value, "BinRAW")) r->protocol = 1;
        else if(!strcmp(value, "RAW")) r->protocol = 2;
        else ok = false;
    } else if(KEY("TE")) {
        ok = !r->data_seen && scalar(value, &n) && n >= 250 && n <= 550;
        if(ok) r->te = n;
    } else if(KEY("Bit_RAW")) {
        ok = r->protocol == 1 && !r->bit_count && scalar(value, &n) && n > 0 && n <= 4096;
        if(ok) r->bit_count = n;
    } else if(KEY("Data_RAW") || KEY("RAW_Data")) {
        ok = r->filetype && r->version && r->frequency && r->preset;
        r->data_seen = true;
        if(ok && KEY("Data_RAW")) ok = r->protocol == 1 && binraw(r, value);
        else if(ok) {
            ok = r->protocol == 2;
            unsigned count = 0;
            while(ok) {
                while(isspace((unsigned char)*value)) ++value;
                if(!*value) break;
                ok = number(&value, &n, 10) && n != 0 && n >= -10000000 && n <= 10000000;
                if(ok) {
                    raw_pulse(r, n > 0, n > 0 ? n : -n);
                    ++count;
                }
            }
            ok = ok && count;
        }
    }
#undef KEY
    r->failed |= !ok;
    return !r->failed;
}

bool acun_recording_finish(const AcunRecording* r, SeqFrame* frame) {
    if(r->failed || !r->found || r->bit_count) return false;
    *frame = r->result;
    return true;
}
