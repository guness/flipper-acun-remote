#pragma once
/* Minimal host stand-ins for the firmware encoder interface. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SubGhzEnvironment SubGhzEnvironment;
typedef struct SubGhzProtocol SubGhzProtocol;
typedef struct {
    bool level;
    uint32_t duration;
} LevelDuration;
static inline LevelDuration level_duration_reset(void) {
    return (LevelDuration){0};
}
static inline LevelDuration level_duration_make(bool level, uint32_t duration) {
    return (LevelDuration){level, duration};
}
uint32_t furi_get_tick(void);
typedef struct {
    const SubGhzProtocol* protocol;
} SubGhzProtocolEncoderBase;
typedef struct {
    void* (*alloc)(SubGhzEnvironment*);
    void (*free)(void*);
    void (*stop)(void*);
    LevelDuration (*yield)(void*);
} SubGhzProtocolEncoder;
enum {
    SubGhzProtocolTypeDynamic,
    SubGhzProtocolFlag_433 = 8,
    SubGhzProtocolFlag_AM = 32,
    SubGhzProtocolFlag_Send = 512,
};
struct SubGhzProtocol {
    const char* name;
    unsigned type;
    unsigned flag;
    const SubGhzProtocolEncoder* encoder;
};
