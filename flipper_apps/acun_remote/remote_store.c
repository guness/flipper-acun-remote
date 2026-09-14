#include "remote_store.h"
#include <furi.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define STORE_ROOT STORAGE_APP_DATA_PATH_PREFIX

static void dir_path(char* out, size_t size, const char* name) {
    snprintf(out, size, STORE_ROOT "/%s", name);
}

static void record_path(char* out, size_t size, const char* name, uint8_t button, uint8_t copy) {
    snprintf(out, size, STORE_ROOT "/%s/%u.%u.seq", name, button, copy);
}

/* -1: present but invalid, 0: absent, 1: valid. */
static int read_record(Storage* storage, const char* path, SeqProfile* profile) {
    if(!storage_file_exists(storage, path)) return 0;
    File* file = storage_file_alloc(storage);
    uint8_t data[SEQ_RECORD_SIZE];
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) ok = storage_file_size(file) == sizeof(data) &&
                storage_file_read(file, data, sizeof(data)) == sizeof(data);
    storage_file_close(file);
    storage_file_free(file);
    return ok && seq_unpack(data, profile) ? 1 : -1;
}

/* "N.C.seq" with N in 1..REMOTE_BUTTON_MAX and C in 0..1. */
static bool parse_record_name(const char* file, uint8_t* button, uint8_t* copy) {
    if(strlen(file) != 7) return false;
    if(file[0] < '1' || file[0] > '0' + REMOTE_BUTTON_MAX) return false;
    if(file[1] != '.' || (file[2] != '0' && file[2] != '1')) return false;
    if(strcmp(file + 3, ".seq") != 0) return false;
    *button = file[0] - '0';
    *copy = file[2] - '0';
    return true;
}

static void load_entry(Storage* storage, RemoteStore* store, const char* name, uint8_t button) {
    size_t name_length = strlen(name);
    if(name_length > REMOTE_NAME_MAX) return; /* remote_name_valid already rejects this */
    if(remote_store_find_named(store, name, button) >= 0) return; /* other copy already seen */
    if(store->count >= REMOTE_STORE_MAX) {
        store->truncated = true;
        return;
    }
    char path[96];
    SeqProfile a, b;
    record_path(path, sizeof(path), name, button, 0);
    int sa = read_record(storage, path, &a);
    record_path(path, sizeof(path), name, button, 1);
    int sb = read_record(storage, path, &b);
    if(sa == 0 && sb == 0) return;
    RemoteEntry* entry = &store->entries[store->count++];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, name_length + 1);
    entry->button = button;
    /* Never fall back silently from a damaged copy; keep a readable profile only
     * so the entry can still be matched and labelled. */
    entry->damaged = sa < 0 || sb < 0;
    if(sa == 1 && (sb != 1 || a.generation >= b.generation)) entry->profile = a;
    else if(sb == 1) entry->profile = b;
}

static int compare_entries(const RemoteEntry* x, const RemoteEntry* y) {
    int c = strcasecmp(x->name, y->name);
    if(c) return c;
    return (int)x->button - (int)y->button;
}

static void sort_entries(RemoteStore* store) {
    for(size_t i = 1; i < store->count; ++i) {
        RemoteEntry key = store->entries[i];
        size_t j = i;
        while(j > 0 && compare_entries(&store->entries[j - 1], &key) > 0) {
            store->entries[j] = store->entries[j - 1];
            --j;
        }
        store->entries[j] = key;
    }
}

void remote_store_load(RemoteStore* store, Storage* storage) {
    memset(store, 0, sizeof(*store));
    File* root = storage_file_alloc(storage);
    if(storage_dir_open(root, STORE_ROOT)) {
        FileInfo info;
        char name[64];
        while(storage_dir_read(root, &info, name, sizeof(name))) {
            if(!file_info_is_dir(&info) || !remote_name_valid(name)) continue;
            char path[96];
            dir_path(path, sizeof(path), name);
            File* dir = storage_file_alloc(storage);
            if(storage_dir_open(dir, path)) {
                FileInfo file_info;
                char file[32];
                uint8_t button, copy;
                while(storage_dir_read(dir, &file_info, file, sizeof(file))) {
                    if(file_info_is_dir(&file_info)) continue;
                    if(!parse_record_name(file, &button, &copy)) continue;
                    load_entry(storage, store, name, button);
                }
            }
            storage_dir_close(dir);
            storage_file_free(dir);
        }
    }
    storage_dir_close(root);
    storage_file_free(root);
    sort_entries(store);
}

int remote_store_find(const RemoteStore* store, const SeqFrame* frame) {
    for(size_t i = 0; i < store->count; ++i) {
        const RemoteEntry* entry = &store->entries[i];
        if(entry->profile.step == 0) continue; /* damaged with no readable copy */
        if(seq_same_button(&entry->profile.frame, frame)) return (int)i;
    }
    return -1;
}

int remote_store_find_named(const RemoteStore* store, const char* name, uint8_t button) {
    for(size_t i = 0; i < store->count; ++i)
        if(store->entries[i].button == button && strcasecmp(store->entries[i].name, name) == 0)
            return (int)i;
    return -1;
}

uint8_t remote_store_free_button(const RemoteStore* store, const char* name) {
    for(uint8_t button = 1; button <= REMOTE_BUTTON_MAX; ++button)
        if(remote_store_find_named(store, name, button) < 0) return button;
    return 1;
}

size_t remote_store_names(const RemoteStore* store, const char* names[], size_t max) {
    size_t n = 0;
    for(size_t i = 0; i < store->count && n < max; ++i) {
        if(n && strcasecmp(names[n - 1], store->entries[i].name) == 0) continue;
        names[n++] = store->entries[i].name;
    }
    return n;
}

void remote_store_label(const RemoteEntry* entry, char* out, size_t size) {
    snprintf(out, size, "%s B%u%s", entry->name, entry->button, entry->damaged ? " (damaged)" : "");
}

bool remote_store_write(Storage* storage, const char* name, uint8_t button, SeqProfile* profile) {
    if(profile->generation == UINT32_MAX) return false;
    ++profile->generation;
    uint8_t copy = profile->generation & 1;
    char path[96];
    record_path(path, sizeof(path), name, button, copy);
    uint8_t data[SEQ_RECORD_SIZE];
    seq_pack(profile, data);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, data, sizeof(data)) == sizeof(data);
    if(ok) ok = storage_file_sync(file);
    bool closed = storage_file_close(file);
    ok = ok && closed;
    storage_file_free(file);
    SeqProfile check;
    uint8_t packed[SEQ_RECORD_SIZE];
    if(ok) ok = read_record(storage, path, &check) == 1;
    if(ok) {
        seq_pack(&check, packed);
        ok = memcmp(data, packed, sizeof(data)) == 0;
    }
    return ok;
}

bool remote_store_create(Storage* storage, const char* name, uint8_t button, SeqProfile* profile) {
    char path[96];
    dir_path(path, sizeof(path), name);
    if(!storage_simply_mkdir(storage, path)) return false;
    profile->generation = 0;
    return remote_store_write(storage, name, button, profile) &&
           remote_store_write(storage, name, button, profile);
}

bool remote_store_delete(Storage* storage, const char* name, uint8_t button) {
    bool ok = true;
    char path[96];
    for(uint8_t copy = 0; copy < 2; ++copy) {
        record_path(path, sizeof(path), name, button, copy);
        if(!storage_file_exists(storage, path)) continue;
        if(storage_common_remove(storage, path) != FSE_OK) ok = false;
    }
    dir_path(path, sizeof(path), name);
    storage_common_remove(storage, path); /* fails harmlessly while other buttons remain */
    return ok;
}

bool remote_store_rename(
    Storage* storage,
    const char* name,
    uint8_t button,
    const char* new_name,
    uint8_t new_button) {
    char from[96], to[96];
    dir_path(to, sizeof(to), new_name);
    if(!storage_simply_mkdir(storage, to)) return false;
    bool ok = true;
    for(uint8_t copy = 0; copy < 2; ++copy) {
        record_path(from, sizeof(from), name, button, copy);
        record_path(to, sizeof(to), new_name, new_button, copy);
        if(!storage_file_exists(storage, from)) continue;
        if(storage_common_rename(storage, from, to) != FSE_OK) ok = false;
    }
    dir_path(from, sizeof(from), name);
    storage_common_remove(storage, from);
    return ok;
}
