#pragma once
#include <storage/storage.h>
#include "sequence_core.h"
#include "remote_name.h"

#define REMOTE_BUTTON_MAX 8 /* single digit: parse_record_name relies on it */
#define REMOTE_STORE_MAX 32
#define REMOTE_LABEL_MAX 32

typedef struct {
    char name[REMOTE_NAME_MAX + 1];
    uint8_t button;
    SeqProfile profile;
    bool damaged;
} RemoteEntry;

typedef struct {
    RemoteEntry entries[REMOTE_STORE_MAX];
    size_t count;
    bool truncated;
} RemoteStore;

/* Layout: <app data>/<name>/<button>.<copy>.seq, copy 0 and 1 alternate as a journal. */
void remote_store_load(RemoteStore* store, Storage* storage);
int remote_store_find(const RemoteStore* store, const SeqFrame* frame);
int remote_store_find_named(const RemoteStore* store, const char* name, uint8_t button);
uint8_t remote_store_free_button(const RemoteStore* store, const char* name);
size_t remote_store_names(const RemoteStore* store, const char* names[], size_t max);
void remote_store_label(const RemoteEntry* entry, char* out, size_t size);
bool remote_store_write(Storage* storage, const char* name, uint8_t button, SeqProfile* profile);
bool remote_store_create(Storage* storage, const char* name, uint8_t button, SeqProfile* profile);
bool remote_store_delete(Storage* storage, const char* name, uint8_t button);
bool remote_store_rename(
    Storage* storage,
    const char* name,
    uint8_t button,
    const char* new_name,
    uint8_t new_button);
