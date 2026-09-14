# Acun Remote UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the four fixed slots and hand-drawn view with a Read / Saved / About menu app over named remotes, with single-press sync, and Send / Info / Rename / Delete per entry.

**Architecture:** Stock firmware views (Submenu, DialogEx, TextInput, NumberInput, Widget, Popup) driven by `ViewDispatcher` + `SceneManager`, one C file per scene. Three new device modules sit under the scenes: `remote_store` (directory-per-remote journal files and an in-memory sorted index), `radio` (RX capture with press confirmation, TX drive, timeouts, polled from the dispatcher tick), and a pure `remote_name` validator. The core math in `sequence_core.c` gains one function, `seq_sync`.

**Tech Stack:** C11, Flipper Zero firmware API 88.11 (Unleashed SDK zip in `../unleashed-firmware/dist/f7-C/`), ufbt via `scripts/build_acun_remote.py`, host tests in Python 3 + ctypes compiling the pure C files with `cc`.

**Spec:** `docs/superpowers/specs/2026-09-14-acun-remote-ui-redesign-design.md`

## Global Constraints

- Tests read nothing outside the repository; fixtures stay under `tests/acun_remote/data`.
- No reverse-engineering / protocol-recovery wording anywhere.
- `sequence_core.c` and `remote_name.c` contain no `furi` includes; both compile on the host.
- Record format `SEQREM01` is unchanged. Never transmit before the advanced index is written, synced and read back; never roll an index back.
- Name limit 12 chars of `A-Z a-z 0-9 space _ -`, no leading/trailing space. Buttons 1–8. Store cap 32 entries.
- Frequency 433.92 MHz, six TX repeats, 25 s receive timeout, 3 s TX watchdog, 1.5 s result popups, 10 ms dispatcher tick.
- Commit messages carry no Claude trailer or co-author line.
- Build with `-Werror`: every unused parameter is wrapped in `UNUSED()`.
- Popup keeps the pointer to header/text it is given; DialogEx, Submenu and Widget copy their strings. Popup text therefore always lives in `app->text` / `app->text2`.
- `qsort` and `sscanf` are not exported to FAPs; do not use them.

---

## File structure

```
flipper_apps/acun_remote/
  application.fam                 modify: version 0.2, description, stack 5120
  acun_remote.c                   rewrite: app alloc/free, dispatcher, shared callbacks, popup helper
  acun_remote_i.h                 create: AcunApp struct, view ids, custom events
  sequence_core.c / .h            modify: add seq_sync
  remote_name.c / .h              create: pure name validation
  remote_store.c / .h             create: paths, index, load/write/create/delete/rename
  radio.c / .h                    create: RX capture + confirmation, TX drive, timeouts
  scenes/acun_scene_config.h      create: X-macro scene list
  scenes/acun_scene.h / .c        create: handler tables
  scenes/acun_scene_main_menu.c   create
  scenes/acun_scene_about.c       create
  scenes/acun_scene_saved_list.c  create
  scenes/acun_scene_entry_menu.c  create
  scenes/acun_scene_info.c        create
  scenes/acun_scene_delete_confirm.c create
  scenes/acun_scene_read_listen.c create
  scenes/acun_scene_read_sync.c   create
  scenes/acun_scene_read_learn.c  create
  scenes/acun_scene_read_retry.c  create
  scenes/acun_scene_name_select.c create
  scenes/acun_scene_name_input.c  create
  scenes/acun_scene_number_select.c create
  scenes/acun_scene_replace_confirm.c create
  scenes/acun_scene_send.c        create
  README.md                       rewrite
tests/acun_remote/run.py          modify: compile remote_name.c too; add sync and name tests
AGENTS.md                         modify: layout and status
```

The FAP build globs `*.c` recursively under the app directory, so `scenes/` needs no manifest entry.

Build command used by every device task (from the repo root, ~1 min):

```sh
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
```

Expected tail: `Target: 7, API: 88.11` then `Built .../dist/acun_remote.fap`.

---

### Task 1: `seq_sync` in the core

**Files:**
- Modify: `flipper_apps/acun_remote/sequence_core.h` (append after `seq_advance`)
- Modify: `flipper_apps/acun_remote/sequence_core.c` (append after `seq_advance`)
- Test: `tests/acun_remote/run.py`

**Interfaces:**
- Consumes: `seq_same_button`, `seq_permute`, `SeqProfile`, `SeqFrame` (existing).
- Produces: `bool seq_sync(SeqProfile* profile, const SeqFrame* heard, int32_t* delta);`

- [ ] **Step 1: Add the ctypes binding and failing tests**

In `tests/acun_remote/run.py`, after the `lib.seq_unpack.restype = c.c_bool` line add:

```python
lib.seq_sync.argtypes = [c.POINTER(Profile), c.POINTER(Frame), c.POINTER(c.c_int32)]
lib.seq_sync.restype = c.c_bool
```

After the `fit()` helper add:

```python
def sync(profile, word, **kwargs):
    delta = c.c_int32()
    ok = lib.seq_sync(c.byref(profile), c.byref(frame(word, **kwargs)), c.byref(delta))
    return ok, delta.value
```

Inside `class CoreTests`, after `test_learn_every_recorded_set`, add:

```python
    def test_sync_to_each_recorded_press(self):
        """Hearing press i of a learned button moves the profile there and replay continues."""
        for name in SETS:
            with self.subTest(set=name):
                w = words(name)
                for i, heard in enumerate(w):
                    _, p = fit(w, prefix=prefix(name))
                    sends = p.sends
                    ok, delta = sync(p, heard, prefix=prefix(name))
                    self.assertTrue(ok)
                    self.assertEqual(delta, i - (PRESSES - 1))
                    self.assertEqual(p.frame.word, heard)
                    self.assertEqual(p.sends, sends)
                    for expected in w[i + 1:]:
                        lib.seq_advance(c.byref(p))
                        self.assertEqual(p.frame.word, expected)

    def test_sync_ahead_and_behind(self):
        for step_, flag in [(0x3762, 0x8000), (0xd1c1, 0), (0xd1c2, 0x8000),
                            (0xd1c3, 0), (0xd1c4, 0x8000)]:
            with self.subTest(step=step_):
                seed = 0x1234
                word_at = lambda i: flag | permute(((seed + i * step_) & 0xffff) >> 1)
                ok, p = fit([word_at(i) for i in range(PRESSES)])
                self.assertTrue(ok)
                ok, delta = sync(p, word_at(7))
                self.assertTrue(ok)
                self.assertEqual(delta, 3)
                lib.seq_advance(c.byref(p))
                self.assertEqual(p.frame.word, word_at(8))
                ok, delta = sync(p, word_at(2))
                self.assertTrue(ok)
                self.assertEqual(delta, -6)
                limit = 65541 if step_ == 0x3762 else 40
                for i in range(3, limit):
                    lib.seq_advance(c.byref(p))
                    self.assertEqual(p.frame.word, word_at(i))

    def test_sync_rejects_other_button(self):
        w = words('remote_a')
        _, p = fit(w, prefix=prefix('remote_a'))
        before = bytes(p)
        self.assertFalse(sync(p, w[0], prefix=prefix('remote_a') ^ 1)[0])
        self.assertFalse(sync(p, w[0], prefix=prefix('remote_a'), suffix=1, count=1)[0])
        self.assertFalse(sync(p, w[0] ^ 0x8000, prefix=prefix('remote_a'))[0])
        self.assertEqual(bytes(p), before)
```

- [ ] **Step 2: Run the tests, expect failure**

Run: `python3 tests/acun_remote/run.py 2>&1 | tail -5`
Expected: `AttributeError: ... undefined symbol: seq_sync` at import time (the binding line fails before any test runs).

- [ ] **Step 3: Implement `seq_sync`**

Append to `sequence_core.h` after the `seq_advance` declaration:

```c
/* Move the profile to the accumulator implied by a heard press of the same
 * button. delta receives presses ahead (>0) or behind (<0) of the saved state.
 * Returns false if the frame is another button. sends is left unchanged. */
bool seq_sync(SeqProfile* profile, const SeqFrame* heard, int32_t* delta);
```

Append to `sequence_core.c` after `seq_advance`:

```c
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
```

- [ ] **Step 4: Run the tests, expect pass**

Run: `python3 tests/acun_remote/run.py 2>&1 | tail -5`
Expected: `Ran 10 tests`, `OK`.

- [ ] **Step 5: Commit**

```bash
git add flipper_apps/acun_remote/sequence_core.c flipper_apps/acun_remote/sequence_core.h tests/acun_remote/run.py
git commit -m "Add seq_sync: move a learned profile to a heard press"
```

---

### Task 2: Pure name validator

**Files:**
- Create: `flipper_apps/acun_remote/remote_name.h`
- Create: `flipper_apps/acun_remote/remote_name.c`
- Test: `tests/acun_remote/run.py`

**Interfaces:**
- Produces: `#define REMOTE_NAME_MAX 12` and `bool remote_name_valid(const char* name);`

- [ ] **Step 1: Compile the new file into the host library and add a failing test**

In `run.py` change the `subprocess.run([...])` compile call so the source list reads:

```python
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                '-O2', str(ROOT / 'flipper_apps/acun_remote/sequence_core.c'),
                str(ROOT / 'flipper_apps/acun_remote/remote_name.c'),
                '-o', str(LIBRARY)], check=True)
```

After the `lib.seq_sync` binding add:

```python
lib.remote_name_valid.argtypes = [c.c_char_p]
lib.remote_name_valid.restype = c.c_bool
```

Add to `CoreTests`:

```python
    def test_remote_name_rules(self):
        for good in ['Garage', 'Gate 2', 'front-door_1', 'A', 'x' * 12]:
            self.assertTrue(lib.remote_name_valid(good.encode()), good)
        for bad in ['', 'x' * 13, ' Gate', 'Gate ', 'a/b', 'a.b', 'a"b', 'café', 'a\tb']:
            self.assertFalse(lib.remote_name_valid(bad.encode()), bad)
```

- [ ] **Step 2: Run, expect failure**

Run: `python3 tests/acun_remote/run.py 2>&1 | tail -3`
Expected: `cc: error: .../remote_name.c: No such file or directory` and `CalledProcessError`.

- [ ] **Step 3: Implement**

`flipper_apps/acun_remote/remote_name.h`:

```c
#pragma once
#include <stdbool.h>

#define REMOTE_NAME_MAX 12

/* 1..REMOTE_NAME_MAX characters of A-Z a-z 0-9 space '_' '-', with no
 * leading or trailing space. Names become SD directory names. */
bool remote_name_valid(const char* name);
```

`flipper_apps/acun_remote/remote_name.c`:

```c
#include "remote_name.h"
#include <string.h>

static bool allowed(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == ' ' || ch == '_' || ch == '-';
}

bool remote_name_valid(const char* name) {
    if(!name) return false;
    size_t n = strlen(name);
    if(n < 1 || n > REMOTE_NAME_MAX) return false;
    if(name[0] == ' ' || name[n - 1] == ' ') return false;
    for(size_t i = 0; i < n; ++i)
        if(!allowed(name[i])) return false;
    return true;
}
```

- [ ] **Step 4: Run, expect pass**

Run: `python3 tests/acun_remote/run.py 2>&1 | tail -3`
Expected: `Ran 11 tests`, `OK`.

- [ ] **Step 5: Commit**

```bash
git add flipper_apps/acun_remote/remote_name.c flipper_apps/acun_remote/remote_name.h tests/acun_remote/run.py
git commit -m "Add remote name validation"
```

---

### Task 3: Remote store module

**Files:**
- Create: `flipper_apps/acun_remote/remote_store.h`
- Create: `flipper_apps/acun_remote/remote_store.c`

**Interfaces:**
- Consumes: `seq_pack`, `seq_unpack`, `seq_same_button`, `SEQ_RECORD_SIZE`, `remote_name_valid`, `REMOTE_NAME_MAX`.
- Produces (all used by scenes):
  - `#define REMOTE_BUTTON_MAX 8`, `#define REMOTE_STORE_MAX 32`, `#define REMOTE_LABEL_MAX 32`
  - `typedef struct { char name[REMOTE_NAME_MAX + 1]; uint8_t button; SeqProfile profile; bool damaged; } RemoteEntry;`
  - `typedef struct { RemoteEntry entries[REMOTE_STORE_MAX]; size_t count; bool truncated; } RemoteStore;`
  - `void remote_store_load(RemoteStore*, Storage*)`
  - `int remote_store_find(const RemoteStore*, const SeqFrame*)` → index or −1 (damaged entries included when their identity is readable)
  - `int remote_store_find_named(const RemoteStore*, const char* name, uint8_t button)` → index or −1, case-insensitive name
  - `uint8_t remote_store_free_button(const RemoteStore*, const char* name)` → lowest unused 1..8, else 1
  - `size_t remote_store_names(const RemoteStore*, const char* names[], size_t max)` → distinct names in sort order
  - `void remote_store_label(const RemoteEntry*, char* out, size_t size)` → `"Garage B2"` or `"Garage B2 (damaged)"`
  - `bool remote_store_write(Storage*, const char* name, uint8_t button, SeqProfile*)` → one journal write with read-back
  - `bool remote_store_create(Storage*, const char* name, uint8_t button, SeqProfile*)` → mkdir + both copies
  - `bool remote_store_delete(Storage*, const char* name, uint8_t button)`
  - `bool remote_store_rename(Storage*, const char* name, uint8_t button, const char* new_name, uint8_t new_button)`

This module talks to the SD card, so it has no host test; Task 5's build is its check. The journal logic is copied line for line from the old `read_profile` / `write_profile`, which the record round-trip tests already cover.

- [ ] **Step 1: Write the header**

`flipper_apps/acun_remote/remote_store.h`:

```c
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
```

- [ ] **Step 2: Write the implementation**

`flipper_apps/acun_remote/remote_store.c`:

```c
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
```

- [ ] **Step 3: Commit (the build check comes with Task 5)**

```bash
git add flipper_apps/acun_remote/remote_store.c flipper_apps/acun_remote/remote_store.h
git commit -m "Add remote store: directory-per-remote journal files and sorted index"
```

---

### Task 4: Radio module

**Files:**
- Create: `flipper_apps/acun_remote/radio.h`
- Create: `flipper_apps/acun_remote/radio.c`

**Interfaces:**
- Consumes: `seq_decode`, `seq_decoder_reset`, `seq_same_frame`, `seq_pulse`, `SEQ_FREQUENCY`, `SeqProfile`, `SeqFrame`.
- Produces:
  - `typedef enum { RadioEventNone, RadioEventPress, RadioEventOverflow, RadioEventRxTimeout, RadioEventTxDone, RadioEventTxTimeout } RadioEvent;`
  - `Radio* radio_alloc(void); void radio_free(Radio*);`
  - `void radio_rx_start(Radio*);` fresh capture, decoder and dedupe reset
  - `bool radio_tx_start(Radio*, const SeqProfile*);` six repeats, false if the chip refuses
  - `void radio_stop(Radio*);` idempotent, idles and sleeps the chip
  - `bool radio_is_busy(const Radio*);`
  - `RadioEvent radio_tick(Radio*, SeqFrame* press);` call every ~10 ms from the GUI thread; on `RadioEventPress` the confirmed frame is in `*press`

Device only; checked by the Task 5 build. The pulse callback, TX callback and prepare sequence are the old `capture_callback`, `transmit_callback` and `radio_prepare` moved here unchanged.

- [ ] **Step 1: Write the header**

`flipper_apps/acun_remote/radio.h`:

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sequence_core.h"

#define RADIO_TX_REPEATS 6
#define RADIO_RX_TIMEOUT_MS 25000
#define RADIO_TX_TIMEOUT_MS 3000

typedef enum {
    RadioEventNone,
    RadioEventPress, /* a press confirmed by two identical frames; see *press */
    RadioEventOverflow, /* pulse queue overflowed, capture is unreliable */
    RadioEventRxTimeout, /* RADIO_RX_TIMEOUT_MS without a confirmed press */
    RadioEventTxDone,
    RadioEventTxTimeout, /* RADIO_TX_TIMEOUT_MS without completion */
} RadioEvent;

typedef struct Radio Radio;

Radio* radio_alloc(void);
void radio_free(Radio* radio);
void radio_rx_start(Radio* radio);
bool radio_tx_start(Radio* radio, const SeqProfile* profile);
void radio_stop(Radio* radio);
bool radio_is_busy(const Radio* radio);
/* Poll from the GUI thread every ~10 ms. Repeats of the last confirmed press are dropped. */
RadioEvent radio_tick(Radio* radio, SeqFrame* press);
```

- [ ] **Step 2: Write the implementation**

`flipper_apps/acun_remote/radio.c`:

```c
#include "radio.h"
#include <furi.h>
#include <furi_hal.h>
#include <subghz/devices/devices.h>
#include <subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <stdlib.h>
#include <string.h>

#define PULSE_QUEUE_DEPTH 512
#define TICK_BUDGET 512

typedef struct {
    bool level;
    uint32_t duration;
} Pulse;

struct Radio {
    const SubGhzDevice* device;
    FuriMessageQueue* pulses;
    volatile bool overflow;
    bool rx_on;
    bool tx_on;
    uint32_t started; /* tick of RX start / last press / TX start, for timeouts */
    SeqDecoder decoder;
    SeqFrame candidate;
    uint8_t repeats;
    SeqFrame last_press;
    bool has_last;
    SeqProfile tx_profile;
    size_t tx_position;
    uint8_t tx_repeat;
    bool tx_initial_gap;
};

static void radio_pulse_callback(bool level, uint32_t duration, void* context) {
    Radio* radio = context;
    Pulse pulse = {.level = level, .duration = duration};
    if(furi_message_queue_put(radio->pulses, &pulse, 0) != FuriStatusOk) radio->overflow = true;
}

static LevelDuration radio_tx_callback(void* context) {
    Radio* radio = context;
    if(radio->tx_initial_gap) {
        radio->tx_initial_gap = false;
        return level_duration_make(false, radio->tx_profile.frame.gap);
    }
    if(radio->tx_repeat >= RADIO_TX_REPEATS) return level_duration_reset();
    bool level;
    uint32_t duration;
    if(!seq_pulse(&radio->tx_profile.frame, radio->tx_position++, &level, &duration))
        return level_duration_reset();
    if(radio->tx_position == 2u * (47u + radio->tx_profile.frame.suffix_count)) {
        radio->tx_position = 0;
        ++radio->tx_repeat;
    }
    return level_duration_make(level, duration);
}

static void radio_prepare(Radio* radio) {
    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_load_preset(radio->device, FuriHalSubGhzPresetOok270Async, NULL);
    subghz_devices_set_frequency(radio->device, SEQ_FREQUENCY);
}

Radio* radio_alloc(void) {
    Radio* radio = malloc(sizeof(Radio));
    memset(radio, 0, sizeof(*radio));
    radio->pulses = furi_message_queue_alloc(PULSE_QUEUE_DEPTH, sizeof(Pulse));
    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(radio->device);
    subghz_devices_begin(radio->device);
    return radio;
}

void radio_free(Radio* radio) {
    radio_stop(radio);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    furi_message_queue_free(radio->pulses);
    free(radio);
}

void radio_stop(Radio* radio) {
    if(radio->rx_on) {
        subghz_devices_stop_async_rx(radio->device);
        radio->rx_on = false;
    }
    if(radio->tx_on) {
        subghz_devices_stop_async_tx(radio->device);
        radio->tx_on = false;
    }
    subghz_devices_idle(radio->device);
    subghz_devices_sleep(radio->device);
}

void radio_rx_start(Radio* radio) {
    radio_stop(radio);
    furi_message_queue_reset(radio->pulses);
    radio->overflow = false;
    radio->repeats = 0;
    radio->has_last = false;
    seq_decoder_reset(&radio->decoder);
    radio_prepare(radio);
    radio->started = furi_get_tick();
    radio->rx_on = true;
    subghz_devices_start_async_rx(radio->device, radio_pulse_callback, radio);
}

bool radio_tx_start(Radio* radio, const SeqProfile* profile) {
    radio_stop(radio);
    radio->tx_profile = *profile;
    radio->tx_position = 0;
    radio->tx_repeat = 0;
    radio->tx_initial_gap = true;
    radio_prepare(radio);
    radio->started = furi_get_tick();
    radio->tx_on = subghz_devices_start_async_tx(radio->device, radio_tx_callback, radio);
    if(!radio->tx_on) radio_stop(radio);
    return radio->tx_on;
}

bool radio_is_busy(const Radio* radio) {
    return radio->rx_on || radio->tx_on;
}

RadioEvent radio_tick(Radio* radio, SeqFrame* press) {
    if(radio->tx_on) {
        if(subghz_devices_is_async_complete_tx(radio->device)) return RadioEventTxDone;
        if(furi_get_tick() - radio->started > furi_ms_to_ticks(RADIO_TX_TIMEOUT_MS))
            return RadioEventTxTimeout;
        return RadioEventNone;
    }
    if(!radio->rx_on) return RadioEventNone;
    if(radio->overflow) return RadioEventOverflow;
    Pulse pulse;
    size_t budget = TICK_BUDGET;
    while(budget-- && furi_message_queue_get(radio->pulses, &pulse, 0) == FuriStatusOk) {
        SeqFrame frame;
        if(!seq_decode(&radio->decoder, pulse.level, pulse.duration, &frame)) continue;
        if(radio->has_last && seq_same_frame(&radio->last_press, &frame)) continue;
        if(radio->repeats && seq_same_frame(&radio->candidate, &frame)) {
            ++radio->repeats;
        } else {
            radio->candidate = frame;
            radio->repeats = 1;
        }
        if(radio->repeats < 2) continue; /* a held press repeats; two frames confirm it */
        radio->last_press = frame;
        radio->has_last = true;
        radio->repeats = 0;
        radio->started = furi_get_tick();
        *press = frame;
        return RadioEventPress;
    }
    if(furi_get_tick() - radio->started > furi_ms_to_ticks(RADIO_RX_TIMEOUT_MS))
        return RadioEventRxTimeout;
    return RadioEventNone;
}
```

- [ ] **Step 3: Commit**

```bash
git add flipper_apps/acun_remote/radio.c flipper_apps/acun_remote/radio.h
git commit -m "Add radio module: press confirmation, TX drive and timeouts"
```

---

### Task 5: App skeleton and the Saved side (Main menu, About, Saved list, Entry menu, Info, Delete)

**Files:**
- Create: `flipper_apps/acun_remote/acun_remote_i.h`
- Rewrite: `flipper_apps/acun_remote/acun_remote.c`
- Create: `flipper_apps/acun_remote/scenes/acun_scene_config.h`, `acun_scene.h`, `acun_scene.c`
- Create: `scenes/acun_scene_main_menu.c`, `acun_scene_about.c`, `acun_scene_saved_list.c`, `acun_scene_entry_menu.c`, `acun_scene_info.c`, `acun_scene_delete_confirm.c`
- Modify: `flipper_apps/acun_remote/application.fam`

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces (used by every later scene):
  - `AcunApp` struct fields listed in the header below.
  - View ids `AcunViewSubmenu, AcunViewDialog, AcunViewTextInput, AcunViewNumberInput, AcunViewWidget, AcunViewPopup`.
  - Custom events `AcunEventPress, AcunEventRxOverflow, AcunEventRxTimeout, AcunEventTxDone, AcunEventTxTimeout, AcunEventPopupDone, AcunEventDialogLeft, AcunEventDialogCenter, AcunEventDialogRight, AcunEventTextDone, AcunEventNumberDone`. Submenu items send their index (0..32) as the event.
  - Callbacks `acun_submenu_callback`, `acun_dialog_callback`, `acun_popup_callback`, `acun_text_callback`, `acun_number_callback`, `acun_name_validator`.
  - `void acun_popup_show(AcunApp*, const char* header, const char* text, AcunAfter after)` and `void acun_popup_done(AcunApp*)`.
  - `const char* acun_selected_label(AcunApp*)` writes `app->label`.
  - Scene ids from `acun_scene_config.h`. This task lists only the six scenes below; Tasks 6–8 append their lines.

- [ ] **Step 1: Write `acun_remote_i.h`**

```c
#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "sequence_core.h"
#include "remote_name.h"
#include "remote_store.h"
#include "radio.h"
#include "scenes/acun_scene.h"

#define ACUN_VERSION "0.2"
#define ACUN_POPUP_MS 1500
#define ACUN_TICK_MS 10

typedef enum {
    AcunViewSubmenu,
    AcunViewDialog,
    AcunViewTextInput,
    AcunViewNumberInput,
    AcunViewWidget,
    AcunViewPopup,
} AcunView;

/* Custom events. Submenu item indexes (0..REMOTE_STORE_MAX) are sent as-is,
 * so app events start well above them. */
typedef enum {
    AcunEventPress = 100,
    AcunEventRxOverflow,
    AcunEventRxTimeout,
    AcunEventTxDone,
    AcunEventTxTimeout,
    AcunEventPopupDone,
    AcunEventDialogLeft,
    AcunEventDialogCenter,
    AcunEventDialogRight,
    AcunEventTextDone,
    AcunEventNumberDone,
} AcunEvent;

typedef enum {
    AcunFlowLearn,
    AcunFlowRename,
} AcunFlow;

/* Where a result popup goes when its 1.5 s timeout fires. */
typedef enum {
    AcunAfterMainMenu,
    AcunAfterSavedList,
    AcunAfterEntryMenu,
    AcunAfterNumberInput,
} AcunAfter;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    DialogEx* dialog;
    TextInput* text_input;
    NumberInput* number_input;
    Widget* widget;
    Popup* popup;
    Radio* radio;
    RemoteStore store;

    SeqFrame heard; /* last confirmed press from the radio */
    SeqFrame captured[SEQ_LEARN_COUNT];
    uint8_t captured_count;
    bool learn_hint; /* a different button was heard while learning */
    SeqProfile pending; /* fitted, synced or advanced profile awaiting a write */
    int32_t sync_delta;
    int selected; /* index into store, or -1 */
    AcunFlow flow;
    char name[REMOTE_NAME_MAX + 1]; /* name being chosen or typed */
    uint8_t button; /* number being chosen */
    const char* names[REMOTE_STORE_MAX];
    size_t names_count;
    const char* retry_text;
    bool retry_allowed;
    AcunAfter after;
    char label[REMOTE_LABEL_MAX]; /* "Garage B2" of the entry in play */
    char text[48]; /* popup header: Popup keeps the pointer */
    char text2[96]; /* popup or dialog body */
} AcunApp;

/* Shared view callbacks; each forwards to the scene manager as a custom event. */
void acun_submenu_callback(void* context, uint32_t index);
void acun_dialog_callback(DialogExResult result, void* context);
void acun_popup_callback(void* context);
void acun_text_callback(void* context);
void acun_number_callback(void* context, int32_t number);
bool acun_name_validator(const char* text, FuriString* error, void* context);

/* Show a 1.5 s result popup; on AcunEventPopupDone call acun_popup_done(). */
void acun_popup_show(AcunApp* app, const char* header, const char* text, AcunAfter after);
void acun_popup_done(AcunApp* app);
/* Label of store entry app->selected, written to app->label. */
const char* acun_selected_label(AcunApp* app);
```

- [ ] **Step 2: Write the scene tables**

`flipper_apps/acun_remote/scenes/acun_scene_config.h` (Task 5 version; later tasks append):

```c
ADD_SCENE(acun, main_menu, MainMenu)
ADD_SCENE(acun, about, About)
ADD_SCENE(acun, saved_list, SavedList)
ADD_SCENE(acun, entry_menu, EntryMenu)
ADD_SCENE(acun, info, Info)
ADD_SCENE(acun, delete_confirm, DeleteConfirm)
```

`flipper_apps/acun_remote/scenes/acun_scene.h`:

```c
#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) AcunScene##id,
typedef enum {
#include "acun_scene_config.h"
    AcunSceneNum,
} AcunScene;
#undef ADD_SCENE

extern const SceneManagerHandlers acun_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "acun_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "acun_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "acun_scene_config.h"
#undef ADD_SCENE
```

`flipper_apps/acun_remote/scenes/acun_scene.c`:

```c
#include "acun_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const acun_on_enter_handlers[])(void*) = {
#include "acun_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const acun_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "acun_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const acun_on_exit_handlers[])(void* context) = {
#include "acun_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers acun_scene_handlers = {
    .on_enter_handlers = acun_on_enter_handlers,
    .on_event_handlers = acun_on_event_handlers,
    .on_exit_handlers = acun_on_exit_handlers,
    .scene_num = AcunSceneNum,
};
```

- [ ] **Step 3: Rewrite `acun_remote.c`**

Replace the whole file with:

```c
#include "acun_remote_i.h"

void acun_submenu_callback(void* context, uint32_t index) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void acun_dialog_callback(DialogExResult result, void* context) {
    AcunApp* app = context;
    uint32_t event = AcunEventDialogCenter;
    if(result == DialogExResultLeft) event = AcunEventDialogLeft;
    if(result == DialogExResultRight) event = AcunEventDialogRight;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void acun_popup_callback(void* context) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventPopupDone);
}

void acun_text_callback(void* context) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventTextDone);
}

void acun_number_callback(void* context, int32_t number) {
    AcunApp* app = context;
    app->button = (uint8_t)number;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventNumberDone);
}

bool acun_name_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);
    if(remote_name_valid(text)) return true;
    furi_string_set_str(error, "1-12 letters, digits,\nspace, - or _");
    return false;
}

void acun_popup_show(AcunApp* app, const char* header, const char* text, AcunAfter after) {
    app->after = after;
    snprintf(app->text, sizeof(app->text), "%s", header);
    snprintf(app->text2, sizeof(app->text2), "%s", text);
    popup_reset(app->popup);
    popup_set_header(app->popup, app->text, 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->text2, 64, 36, AlignCenter, AlignCenter);
    popup_set_timeout(app->popup, ACUN_POPUP_MS);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, acun_popup_callback);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

void acun_popup_done(AcunApp* app) {
    SceneManager* sm = app->scene_manager;
    switch(app->after) {
    case AcunAfterMainMenu:
        scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneMainMenu);
        break;
    case AcunAfterSavedList:
        if(scene_manager_has_previous_scene(sm, AcunSceneSavedList))
            scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneSavedList);
        else
            scene_manager_search_and_switch_to_another_scene(sm, AcunSceneSavedList);
        break;
    case AcunAfterEntryMenu:
        scene_manager_search_and_switch_to_previous_scene(sm, AcunSceneEntryMenu);
        break;
    case AcunAfterNumberInput:
        view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewNumberInput);
        break;
    }
}

const char* acun_selected_label(AcunApp* app) {
    if(app->selected >= 0 && (size_t)app->selected < app->store.count)
        remote_store_label(&app->store.entries[app->selected], app->label, sizeof(app->label));
    else
        snprintf(app->label, sizeof(app->label), "?");
    return app->label;
}

static bool acun_custom_event_callback(void* context, uint32_t event) {
    AcunApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool acun_navigation_event_callback(void* context) {
    AcunApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* Runs in the dispatcher thread, so radio events go straight to the scene. */
static void acun_tick_event_callback(void* context) {
    AcunApp* app = context;
    uint32_t event = 0;
    switch(radio_tick(app->radio, &app->heard)) {
    case RadioEventPress: event = AcunEventPress; break;
    case RadioEventOverflow: event = AcunEventRxOverflow; break;
    case RadioEventRxTimeout: event = AcunEventRxTimeout; break;
    case RadioEventTxDone: event = AcunEventTxDone; break;
    case RadioEventTxTimeout: event = AcunEventTxTimeout; break;
    case RadioEventNone: break;
    }
    if(event) scene_manager_handle_custom_event(app->scene_manager, event);
    scene_manager_handle_tick_event(app->scene_manager);
}

static AcunApp* acun_alloc(void) {
    AcunApp* app = malloc(sizeof(AcunApp));
    memset(app, 0, sizeof(*app));
    app->selected = -1;
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&acun_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, acun_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, acun_navigation_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, acun_tick_event_callback, furi_ms_to_ticks(ACUN_TICK_MS));

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewSubmenu, submenu_get_view(app->submenu));
    app->dialog = dialog_ex_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewDialog, dialog_ex_get_view(app->dialog));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AcunViewTextInput, text_input_get_view(app->text_input));
    app->number_input = number_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AcunViewNumberInput, number_input_get_view(app->number_input));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AcunViewPopup, popup_get_view(app->popup));

    app->radio = radio_alloc();
    remote_store_load(&app->store, app->storage);
    return app;
}

static void acun_free(AcunApp* app) {
    radio_free(app->radio);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewDialog);
    dialog_ex_free(app->dialog);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewTextInput);
    text_input_free(app->text_input);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewNumberInput);
    number_input_free(app->number_input);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, AcunViewPopup);
    popup_free(app->popup);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t acun_remote_app(void* argument) {
    UNUSED(argument);
    AcunApp* app = acun_alloc();
    furi_hal_power_insomnia_enter();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, AcunSceneMainMenu);
    view_dispatcher_run(app->view_dispatcher);
    furi_hal_power_insomnia_exit();
    acun_free(app);
    return 0;
}
```

- [ ] **Step 4: Write the six scenes**

`scenes/acun_scene_main_menu.c` (Task 5 version; Task 6 adds the Read item):

```c
#include "../acun_remote_i.h"

enum {
    MainMenuRead,
    MainMenuSaved,
    MainMenuAbout,
};

void acun_scene_main_menu_on_enter(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Acun Remote");
    submenu_add_item(app->submenu, "Saved", MainMenuSaved, acun_submenu_callback, app);
    submenu_add_item(app->submenu, "About", MainMenuAbout, acun_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneMainMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneMainMenu, event.event);
    switch(event.event) {
    case MainMenuSaved:
        scene_manager_next_scene(app->scene_manager, AcunSceneSavedList);
        return true;
    case MainMenuAbout:
        scene_manager_next_scene(app->scene_manager, AcunSceneAbout);
        return true;
    }
    return false;
}

void acun_scene_main_menu_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
```

`scenes/acun_scene_about.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_about_on_enter(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Acun Remote " ACUN_VERSION);
    widget_add_string_element(
        app->widget, 64, 17, AlignCenter, AlignTop, FontSecondary, "433.92 MHz, internal radio");
    widget_add_string_multiline_element(
        app->widget,
        64,
        31,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Read learns a button in five\npresses or re-syncs a saved one.\nSaved sends, renames, deletes.");
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

bool acun_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void acun_scene_about_on_exit(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
}
```

`scenes/acun_scene_saved_list.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_saved_list_on_enter(void* context) {
    AcunApp* app = context;
    remote_store_load(&app->store, app->storage);
    app->selected = -1;
    if(app->store.count == 0) {
        widget_reset(app->widget);
        widget_add_string_multiline_element(
            app->widget, 64, 26, AlignCenter, AlignCenter, FontSecondary,
            "Nothing saved yet.\nUse Read.");
        view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
        return;
    }
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->store.truncated ? "Saved (32 max)" : "Saved");
    for(size_t i = 0; i < app->store.count; ++i) {
        char label[REMOTE_LABEL_MAX];
        remote_store_label(&app->store.entries[i], label, sizeof(label));
        submenu_add_item(app->submenu, label, i, acun_submenu_callback, app);
    }
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneSavedList));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= app->store.count) return false;
    app->selected = (int)event.event;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneSavedList, event.event);
    scene_manager_next_scene(app->scene_manager, AcunSceneEntryMenu);
    return true;
}

void acun_scene_saved_list_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
```

`scenes/acun_scene_entry_menu.c` (Task 5 version; Task 7 adds Rename, Task 8 adds Send):

```c
#include "../acun_remote_i.h"

enum {
    EntrySend,
    EntryInfo,
    EntryRename,
    EntryDelete,
};

void acun_scene_entry_menu_on_enter(void* context) {
    AcunApp* app = context;
    const RemoteEntry* entry = &app->store.entries[app->selected];
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, acun_selected_label(app));
    if(!entry->damaged) {
        submenu_add_item(app->submenu, "Info", EntryInfo, acun_submenu_callback, app);
    }
    submenu_add_item(app->submenu, "Delete", EntryDelete, acun_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, AcunSceneEntryMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_entry_menu_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, AcunSceneEntryMenu, event.event);
    switch(event.event) {
    case EntryInfo:
        scene_manager_next_scene(app->scene_manager, AcunSceneInfo);
        return true;
    case EntryDelete:
        scene_manager_next_scene(app->scene_manager, AcunSceneDeleteConfirm);
        return true;
    }
    return false;
}

void acun_scene_entry_menu_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
```

`scenes/acun_scene_info.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_info_on_enter(void* context) {
    AcunApp* app = context;
    const SeqProfile* p = &app->store.entries[app->selected].profile;
    char line[40];
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 1, AlignCenter, AlignTop, FontPrimary, acun_selected_label(app));
    snprintf(line, sizeof(line), "Prefix %08lX", (unsigned long)p->frame.prefix);
    widget_add_string_element(app->widget, 2, 14, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Step %04X  Word %04X", p->step, p->frame.word);
    widget_add_string_element(app->widget, 2, 24, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Index %04X  Sends %lu", p->accumulator, (unsigned long)p->sends);
    widget_add_string_element(app->widget, 2, 34, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "%u pulses  TE %u us", 47u + p->frame.suffix_count, p->frame.te);
    widget_add_string_element(app->widget, 2, 44, AlignLeft, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Gap %lu us", (unsigned long)p->frame.gap);
    widget_add_string_element(app->widget, 2, 54, AlignLeft, AlignTop, FontSecondary, line);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

bool acun_scene_info_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void acun_scene_info_on_exit(void* context) {
    AcunApp* app = context;
    widget_reset(app->widget);
}
```

`scenes/acun_scene_delete_confirm.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_delete_confirm_on_enter(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, acun_selected_label(app), 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, "Delete this button?", 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Delete");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_delete_confirm_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case AcunEventDialogRight: {
        const RemoteEntry* entry = &app->store.entries[app->selected];
        if(remote_store_delete(app->storage, entry->name, entry->button))
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, AcunSceneSavedList);
        else
            acun_popup_show(app, "Delete failed", "Check the SD card.", AcunAfterSavedList);
        return true;
    }
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_delete_confirm_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
```

- [ ] **Step 5: Update the manifest**

`flipper_apps/acun_remote/application.fam` becomes:

```python
App(
    appid="acun_remote",
    name="Acun Remote",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="acun_remote_app",
    requires=["gui", "storage"],
    stack_size=5120,
    fap_category="Sub-GHz",
    fap_icon="icon.png",
    fap_version="0.2",
    fap_author="Repository contributors",
    fap_description="Learn, sync and send modular-accumulator remote buttons",
)
```

- [ ] **Step 6: Build**

Run the build command from the file-structure section.
Expected: no warnings (they are errors), `Target: 7, API: 88.11`, `Built .../dist/acun_remote.fap`. If the compiler reports an unknown `number_input.h`, the SDK is older than 88.11; stop and report.

- [ ] **Step 7: Run host tests (unchanged code paths, sanity)**

Run: `python3 tests/acun_remote/run.py 2>&1 | tail -3`
Expected: `Ran 11 tests`, `OK`.

- [ ] **Step 8: Commit**

```bash
git add flipper_apps/acun_remote
git commit -m "Rebuild UI on scene manager: main menu, about, saved list, entry menu, info, delete"
```

---

### Task 6: Read flow (Listen, Sync, Learn, Retry)

**Files:**
- Modify: `scenes/acun_scene_config.h` (append four lines)
- Modify: `scenes/acun_scene_main_menu.c` (add the Read item and case)
- Create: `scenes/acun_scene_read_listen.c`, `acun_scene_read_sync.c`, `acun_scene_read_learn.c`, `acun_scene_read_retry.c`

**Interfaces:**
- Consumes: `radio_rx_start`, `radio_stop`, `remote_store_find`, `remote_store_load`, `remote_store_write`, `seq_sync`, `seq_fit`, `seq_same_button`, `seq_same_frame`, `acun_popup_show/done`, `acun_selected_label`, events `AcunEventPress/RxOverflow/RxTimeout/PopupDone/DialogLeft/DialogRight`.
- Produces: scene ids `AcunSceneReadListen, AcunSceneReadSync, AcunSceneReadLearn, AcunSceneReadRetry`. ReadLearn hands a fitted profile to Task 7 in `app->pending` with `app->flow = AcunFlowLearn`, `app->selected = -1`, and enters `AcunSceneNameSelect` — until Task 7 exists, ReadLearn temporarily goes to the main menu (see Step 4 note).

- [ ] **Step 1: Register the scenes**

Append to `scenes/acun_scene_config.h`:

```c
ADD_SCENE(acun, read_listen, ReadListen)
ADD_SCENE(acun, read_sync, ReadSync)
ADD_SCENE(acun, read_learn, ReadLearn)
ADD_SCENE(acun, read_retry, ReadRetry)
```

- [ ] **Step 2: Add Read to the main menu**

In `scenes/acun_scene_main_menu.c`, insert before the `"Saved"` item:

```c
    submenu_add_item(app->submenu, "Read", MainMenuRead, acun_submenu_callback, app);
```

and add to the switch in `on_event`, before `case MainMenuSaved:`:

```c
    case MainMenuRead:
        scene_manager_next_scene(app->scene_manager, AcunSceneReadListen);
        return true;
```

- [ ] **Step 3: Write ReadListen**

`scenes/acun_scene_read_listen.c`:

```c
#include "../acun_remote_i.h"

static void read_listen_retry(AcunApp* app, const char* text, bool allowed) {
    app->retry_text = text;
    app->retry_allowed = allowed;
    scene_manager_next_scene(app->scene_manager, AcunSceneReadRetry);
}

void acun_scene_read_listen_on_enter(void* context) {
    AcunApp* app = context;
    remote_store_load(&app->store, app->storage);
    app->captured_count = 0;
    app->learn_hint = false;
    app->selected = -1;
    radio_rx_start(app->radio);
    popup_reset(app->popup);
    popup_set_header(app->popup, "Listening 433.92 MHz", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, "Hold a remote button", 64, 36, AlignCenter, AlignCenter);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

bool acun_scene_read_listen_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventPress: {
        int found = remote_store_find(&app->store, &app->heard);
        if(found < 0) {
            app->captured[0] = app->heard;
            app->captured_count = 1;
            scene_manager_next_scene(app->scene_manager, AcunSceneReadLearn);
            return true;
        }
        app->selected = found;
        if(app->store.entries[found].damaged) {
            snprintf(
                app->text2,
                sizeof(app->text2),
                "%s is damaged.\nDelete it in Saved,\nthen read again.",
                acun_selected_label(app));
            read_listen_retry(app, app->text2, false);
            return true;
        }
        app->pending = app->store.entries[found].profile;
        if(seq_sync(&app->pending, &app->heard, &app->sync_delta))
            scene_manager_next_scene(app->scene_manager, AcunSceneReadSync);
        else
            read_listen_retry(app, "Could not sync\nto this press.", true);
        return true;
    }
    case AcunEventRxOverflow:
        read_listen_retry(app, "Receive overflow.\nTry again?", true);
        return true;
    case AcunEventRxTimeout:
        read_listen_retry(app, "No press heard.\nTry again?", true);
        return true;
    }
    return false;
}

void acun_scene_read_listen_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    popup_reset(app->popup);
}
```

- [ ] **Step 4: Write ReadSync**

`scenes/acun_scene_read_sync.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_read_sync_on_enter(void* context) {
    AcunApp* app = context;
    long delta = app->sync_delta;
    if(delta == 0)
        snprintf(app->text2, sizeof(app->text2), "Remote is in sync.");
    else if(delta > 0)
        snprintf(
            app->text2, sizeof(app->text2), "Remote is %ld press%s ahead.", delta,
            delta == 1 ? "" : "es");
    else
        snprintf(
            app->text2, sizeof(app->text2), "Remote is %ld press%s behind.\nReceiver may reject.",
            -delta, delta == -1 ? "" : "es");
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, acun_selected_label(app), 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->text2, 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Sync");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_read_sync_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    case AcunEventDialogRight: {
        RemoteEntry* entry = &app->store.entries[app->selected];
        if(remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
            entry->profile = app->pending;
            acun_popup_show(app, "Synced", acun_selected_label(app), AcunAfterMainMenu);
        } else {
            entry->damaged = true;
            acun_popup_show(app, "Save failed", "Entry marked damaged.", AcunAfterMainMenu);
        }
        return true;
    }
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_read_sync_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
```

- [ ] **Step 5: Write ReadLearn**

`scenes/acun_scene_read_learn.c`. Until Task 7 adds `AcunSceneNameSelect`, the fitted branch must compile: write it with the `AcunSceneNameSelect` line as shown and complete Task 7 before building, **or** build Task 6 alone by temporarily replacing that one line with `scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);`. The commit at the end of Task 7 must contain the `AcunSceneNameSelect` version.

```c
#include "../acun_remote_i.h"

static void read_learn_draw(AcunApp* app) {
    char line[32];
    snprintf(line, sizeof(line), "Press %u of %u", app->captured_count + 1u, SEQ_LEARN_COUNT);
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "New remote");
    widget_add_string_element(app->widget, 64, 18, AlignCenter, AlignTop, FontPrimary, line);
    widget_add_string_multiline_element(
        app->widget,
        64,
        36,
        AlignCenter,
        AlignTop,
        FontSecondary,
        app->learn_hint ? "Different button, ignored.\nPress the same button again." :
                          "Release, then press\nthe same button again.");
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewWidget);
}

static void read_learn_retry(AcunApp* app, const char* text) {
    app->retry_text = text;
    app->retry_allowed = true;
    scene_manager_next_scene(app->scene_manager, AcunSceneReadRetry);
}

void acun_scene_read_learn_on_enter(void* context) {
    AcunApp* app = context;
    app->learn_hint = false;
    radio_rx_start(app->radio);
    read_learn_draw(app);
}

bool acun_scene_read_learn_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventPress:
        /* The radio restarted on entry, so re-check against the last accepted press. */
        if(seq_same_frame(&app->captured[app->captured_count - 1], &app->heard)) return true;
        if(!seq_same_button(&app->captured[0], &app->heard)) {
            app->learn_hint = true;
            read_learn_draw(app);
            return true;
        }
        app->captured[app->captured_count++] = app->heard;
        app->learn_hint = false;
        if(app->captured_count < SEQ_LEARN_COUNT) {
            read_learn_draw(app);
            return true;
        }
        if(seq_fit(app->captured, &app->pending)) {
            app->flow = AcunFlowLearn;
            app->selected = -1;
            scene_manager_next_scene(app->scene_manager, AcunSceneNameSelect);
        } else {
            read_learn_retry(app, "Presses don't fit.\nStart over?");
        }
        return true;
    case AcunEventRxOverflow:
        read_learn_retry(app, "Receive overflow.\nStart over?");
        return true;
    case AcunEventRxTimeout:
        read_learn_retry(app, "No press heard.\nStart over?");
        return true;
    }
    return false;
}

void acun_scene_read_learn_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    widget_reset(app->widget);
}
```

- [ ] **Step 6: Write ReadRetry**

`scenes/acun_scene_read_retry.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_read_retry_on_enter(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Read", 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->retry_text, 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    if(app->retry_allowed) dialog_ex_set_right_button_text(app->dialog, "Retry");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_read_retry_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    case AcunEventDialogRight:
        /* Re-enters ReadListen, which resets the learn state and restarts the radio. */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneReadListen);
        return true;
    }
    return false;
}

void acun_scene_read_retry_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
```

- [ ] **Step 7: Build (with Task 7 done, or with the temporary line from Step 5)**

Run the build command. Expected: `Built .../dist/acun_remote.fap`.

- [ ] **Step 8: Commit**

```bash
git add flipper_apps/acun_remote/scenes
git commit -m "Add Read flow: listen, sync a known button, learn a new one, retry dialog"
```

---

### Task 7: Naming flow (NameSelect, NameInput, NumberSelect, ReplaceConfirm) and Rename

**Files:**
- Modify: `scenes/acun_scene_config.h` (append four lines)
- Modify: `scenes/acun_scene_entry_menu.c` (add Rename)
- Create: `scenes/acun_scene_name_select.c`, `acun_scene_name_input.c`, `acun_scene_number_select.c`, `acun_scene_replace_confirm.c`

**Interfaces:**
- Consumes: `app->pending` (fitted profile), `app->flow`, `app->selected`, `remote_store_names`, `remote_store_find_named`, `remote_store_free_button`, `remote_store_create`, `remote_store_delete`, `remote_store_rename`, `acun_text_callback`, `acun_number_callback`, `acun_name_validator`, `acun_popup_show/done`, events `AcunEventTextDone/NumberDone/DialogLeft/DialogRight/PopupDone`.
- Produces: scene ids `AcunSceneNameSelect, AcunSceneNameInput, AcunSceneNumberSelect, AcunSceneReplaceConfirm`.

- [ ] **Step 1: Register the scenes**

Append to `scenes/acun_scene_config.h`:

```c
ADD_SCENE(acun, name_select, NameSelect)
ADD_SCENE(acun, name_input, NameInput)
ADD_SCENE(acun, number_select, NumberSelect)
ADD_SCENE(acun, replace_confirm, ReplaceConfirm)
```

- [ ] **Step 2: Add Rename to the entry menu**

In `scenes/acun_scene_entry_menu.c`, inside `if(!entry->damaged) { ... }` after the Info item add:

```c
        submenu_add_item(app->submenu, "Rename", EntryRename, acun_submenu_callback, app);
```

and in the switch add before `case EntryDelete:`:

```c
    case EntryRename: {
        const RemoteEntry* entry = &app->store.entries[app->selected];
        app->flow = AcunFlowRename;
        snprintf(app->name, sizeof(app->name), "%s", entry->name);
        app->button = entry->button;
        scene_manager_next_scene(app->scene_manager, AcunSceneNameSelect);
        return true;
    }
```

- [ ] **Step 3: Write NameSelect**

`scenes/acun_scene_name_select.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_name_select_on_enter(void* context) {
    AcunApp* app = context;
    app->names_count = remote_store_names(&app->store, app->names, REMOTE_STORE_MAX);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Remote");
    for(size_t i = 0; i < app->names_count; ++i) {
        submenu_add_item(app->submenu, app->names[i], i, acun_submenu_callback, app);
        if(app->flow == AcunFlowRename && strcasecmp(app->names[i], app->name) == 0)
            submenu_set_selected_item(app->submenu, i);
    }
    submenu_add_item(
        app->submenu, "New remote...", app->names_count, acun_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
}

bool acun_scene_name_select_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(app->flow != AcunFlowLearn) return false; /* rename: back to the entry menu */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneMainMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < app->names_count) {
        snprintf(app->name, sizeof(app->name), "%s", app->names[event.event]);
        scene_manager_next_scene(app->scene_manager, AcunSceneNumberSelect);
        return true;
    }
    if(event.event == app->names_count) {
        if(app->flow == AcunFlowLearn) app->name[0] = '\0';
        scene_manager_next_scene(app->scene_manager, AcunSceneNameInput);
        return true;
    }
    return false;
}

void acun_scene_name_select_on_exit(void* context) {
    AcunApp* app = context;
    submenu_reset(app->submenu);
}
```

- [ ] **Step 4: Write NameInput**

`scenes/acun_scene_name_input.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_name_input_on_enter(void* context) {
    AcunApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Remote name");
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_validator(app->text_input, acun_name_validator, app);
    text_input_set_result_callback(
        app->text_input,
        acun_text_callback,
        app,
        app->name,
        sizeof(app->name),
        app->flow == AcunFlowLearn);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewTextInput);
}

bool acun_scene_name_input_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != AcunEventTextDone) return false;
    scene_manager_next_scene(app->scene_manager, AcunSceneNumberSelect);
    return true;
}

void acun_scene_name_input_on_exit(void* context) {
    AcunApp* app = context;
    text_input_reset(app->text_input);
}
```

- [ ] **Step 5: Write NumberSelect**

`scenes/acun_scene_number_select.c`:

```c
#include "../acun_remote_i.h"

static const char* chosen_label(AcunApp* app) {
    snprintf(app->label, sizeof(app->label), "%s B%u", app->name, app->button);
    return app->label;
}

void acun_scene_number_select_on_enter(void* context) {
    AcunApp* app = context;
    int32_t current = remote_store_free_button(&app->store, app->name);
    if(app->flow == AcunFlowRename && app->selected >= 0)
        current = app->store.entries[app->selected].button;
    number_input_set_header_text(app->number_input, "Button number");
    number_input_set_result_callback(
        app->number_input, acun_number_callback, app, current, 1, REMOTE_BUTTON_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewNumberInput);
}

static void number_select_learn(AcunApp* app) {
    if(remote_store_find_named(&app->store, app->name, app->button) >= 0) {
        scene_manager_next_scene(app->scene_manager, AcunSceneReplaceConfirm);
        return;
    }
    if(remote_store_create(app->storage, app->name, app->button, &app->pending))
        acun_popup_show(app, "Saved", chosen_label(app), AcunAfterSavedList);
    else
        acun_popup_show(app, "Save failed", "Check the SD card.", AcunAfterMainMenu);
}

static void number_select_rename(AcunApp* app) {
    const RemoteEntry* entry = &app->store.entries[app->selected];
    int existing = remote_store_find_named(&app->store, app->name, app->button);
    if(existing == app->selected) { /* same name and number: nothing to do */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, AcunSceneSavedList);
        return;
    }
    if(existing >= 0) {
        acun_popup_show(app, "Already exists", chosen_label(app), AcunAfterNumberInput);
        return;
    }
    if(remote_store_rename(app->storage, entry->name, entry->button, app->name, app->button))
        acun_popup_show(app, "Renamed", chosen_label(app), AcunAfterSavedList);
    else
        acun_popup_show(app, "Rename failed", "Check the SD card.", AcunAfterSavedList);
}

bool acun_scene_number_select_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventNumberDone:
        if(app->flow == AcunFlowLearn) number_select_learn(app);
        else number_select_rename(app);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_number_select_on_exit(void* context) {
    UNUSED(context);
}
```

- [ ] **Step 6: Write ReplaceConfirm**

`scenes/acun_scene_replace_confirm.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_replace_confirm_on_enter(void* context) {
    AcunApp* app = context;
    snprintf(app->label, sizeof(app->label), "%s B%u", app->name, app->button);
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, app->label, 64, 6, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, "Already saved.\nReplace it?", 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Back");
    dialog_ex_set_right_button_text(app->dialog, "Replace");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_replace_confirm_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventDialogLeft:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case AcunEventDialogRight:
        remote_store_delete(app->storage, app->name, app->button);
        if(remote_store_create(app->storage, app->name, app->button, &app->pending))
            acun_popup_show(app, "Saved", app->label, AcunAfterSavedList);
        else
            acun_popup_show(app, "Save failed", "Check the SD card.", AcunAfterMainMenu);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_replace_confirm_on_exit(void* context) {
    AcunApp* app = context;
    dialog_ex_reset(app->dialog);
}
```

- [ ] **Step 7: Build**

Run the build command. Expected: `Built .../dist/acun_remote.fap`. Confirm `acun_scene_read_learn.c` now enters `AcunSceneNameSelect` (no temporary line left).

- [ ] **Step 8: Commit**

```bash
git add flipper_apps/acun_remote/scenes
git commit -m "Add naming flow: pick or type a remote name, choose a button, replace or rename"
```

---

### Task 8: Send

**Files:**
- Modify: `scenes/acun_scene_config.h` (append one line)
- Modify: `scenes/acun_scene_entry_menu.c` (add Send)
- Create: `scenes/acun_scene_send.c`

**Interfaces:**
- Consumes: `radio_tx_start`, `radio_stop`, `radio_is_busy`, `remote_store_write`, `seq_advance`, `furi_hal_subghz_is_tx_allowed`, `acun_popup_show/done`, events `AcunEventTxDone/TxTimeout/PopupDone`.
- Produces: scene id `AcunSceneSend`.

- [ ] **Step 1: Register and wire**

Append to `scenes/acun_scene_config.h`:

```c
ADD_SCENE(acun, send, Send)
```

In `scenes/acun_scene_entry_menu.c`, inside `if(!entry->damaged) {` insert as the first item:

```c
        submenu_add_item(app->submenu, "Send", EntrySend, acun_submenu_callback, app);
```

and add to the switch before `case EntryInfo:`:

```c
    case EntrySend:
        scene_manager_next_scene(app->scene_manager, AcunSceneSend);
        return true;
```

- [ ] **Step 2: Write Send**

`scenes/acun_scene_send.c`:

```c
#include "../acun_remote_i.h"

void acun_scene_send_on_enter(void* context) {
    AcunApp* app = context;
    RemoteEntry* entry = &app->store.entries[app->selected];
    acun_selected_label(app);
    if(!furi_hal_subghz_is_tx_allowed(SEQ_FREQUENCY)) {
        acun_popup_show(app, "Region blocks TX", "433.92 MHz not allowed here.", AcunAfterEntryMenu);
        return;
    }
    /* Reserve the next index durably before the radio starts. A stopped or failed
     * transmission consumes it; it is never rolled back. */
    app->pending = entry->profile;
    seq_advance(&app->pending);
    if(!remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
        entry->damaged = true;
        acun_popup_show(app, "Save failed", "Nothing sent.", AcunAfterSavedList);
        return;
    }
    entry->profile = app->pending;
    if(!radio_tx_start(app->radio, &app->pending)) {
        acun_popup_show(app, "TX failed", "Index advanced.", AcunAfterEntryMenu);
        return;
    }
    snprintf(app->text2, sizeof(app->text2), "%s\nBack stops", app->label);
    popup_reset(app->popup);
    popup_set_header(app->popup, "Sending", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->text2, 64, 36, AlignCenter, AlignCenter);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
}

bool acun_scene_send_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(!radio_is_busy(app->radio)) return false; /* result popup: plain back */
        radio_stop(app->radio);
        acun_popup_show(app, "Stopped", "Index advanced.", AcunAfterEntryMenu);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case AcunEventTxDone:
        radio_stop(app->radio);
        acun_popup_show(app, "Sent", "Check the receiver.", AcunAfterEntryMenu);
        return true;
    case AcunEventTxTimeout:
        radio_stop(app->radio);
        acun_popup_show(app, "Stopped", "Index advanced.", AcunAfterEntryMenu);
        return true;
    case AcunEventPopupDone:
        acun_popup_done(app);
        return true;
    }
    return false;
}

void acun_scene_send_on_exit(void* context) {
    AcunApp* app = context;
    radio_stop(app->radio);
    popup_reset(app->popup);
}
```

- [ ] **Step 3: Build**

Run the build command. Expected: `Built .../dist/acun_remote.fap`.

- [ ] **Step 4: Commit**

```bash
git add flipper_apps/acun_remote/scenes
git commit -m "Add Send: reserve the next index, transmit six repeats, report the result"
```

---

### Task 9: Documentation, spec alignment, final build

**Files:**
- Rewrite: `flipper_apps/acun_remote/README.md` (Install, Use, Storage sections; keep Framing, Build, Hardware sections)
- Modify: `AGENTS.md` (Layout and Status sections)
- Modify: `docs/superpowers/specs/2026-09-14-acun-remote-ui-redesign-design.md` (one sentence)
- Regenerated by the build: `flipper_apps/acun_remote/dist/acun_remote.fap`, `dist/build_info.json`

- [ ] **Step 1: Rewrite the top of the app README**

Replace everything in `flipper_apps/acun_remote/README.md` from the title through the end of the "State and storage" section (the "## Framing" heading and everything after it stay) with:

```markdown
# Acun Remote

Flipper Zero external app for a modular-accumulator remote protocol. It learns
a remote button from five consecutive presses, keeps that button's counter on
the SD card, and sends the next value on demand. Internal radio at
**433.92 MHz**. This is not a generic KeeLoq learner.

## Install

Copy `dist/acun_remote.fap` to the Flipper SD card under `apps/Sub-GHz/`. The
supplied build targets the local Unleashed SDK, hardware f7, API **88.11**.
Firmware with an incompatible API needs a rebuild against its own SDK.

## Use

Open **Apps → Sub-GHz → Acun Remote**. The main menu has Read, Saved and About.

### Read

Read listens as soon as it opens. Hold a button on the remote until the app
reacts. A press counts once two identical complete frames have been heard.

- **Known button.** If the press matches a saved entry, the app names it and
  says how far the physical remote is from the saved state, for example
  "Remote is 3 presses ahead". **Sync** moves the saved counter to the heard
  press. Syncing to a remote that is behind is allowed, but the receiver may
  reject those codes and the dialog says so.
- **New button.** Otherwise the app asks for the same button five times in a
  row. Release between presses and do not press other buttons; a different
  button is ignored with a hint. Three presses fit the counter model and two
  validate it. Then pick an existing remote name or type a new one (1 to 12
  letters, digits, spaces, `-` or `_`), choose a button number from 1 to 8,
  and the entry is saved. If that name and number already exist you can
  replace them.
- Back leaves Read at any step. Twenty-five seconds without a press, a receive
  overflow, or five presses that do not fit offer Retry or Cancel.

### Saved

One list of every learned button, sorted by remote name and then number, for
example "Garage B1", "Garage B2", "Gate B1". Open an entry for:

- **Send** transmits the next value as six repeats of one press. The counter is
  written to the SD card and read back before the radio starts. Back stops the
  radio; the index stays advanced.
- **Info** shows prefix, step, current word, index, send count, pulse count,
  pulse timing and gap.
- **Rename** changes the remote name or button number without relearning.
- **Delete** removes the entry after a confirmation.

An entry whose journal copy failed its checksum is listed as "(damaged)" and
offers only Delete. Delete it, then Read the button again.

## Storage

Each remote is a directory under the app's data folder and each button a pair
of journal files, for example `Garage/2.0.seq` and `Garage/2.1.seq`. Records
are versioned 64-byte little-endian with CRC32. The two files alternate: before
a send or sync the next state is written, synced and read back, and a failure
blocks the action and marks the entry damaged. On load the copy with the higher
generation wins. Files from the earlier fixed-slot version are ignored. Up to
32 entries are listed. Keep the SD card inserted while using the app. The
frequency stays subject to the firmware's normal transmission-region checks.
```

Then in the "## Build and verification" section change the sentence beginning "Host tests compile the same C core" to start with "Host tests compile the same C core and name validator used in the app and exercise" (the rest of the sentence unchanged), and append to the same paragraph: "Sync is tested by moving a learned profile to every recorded press and to synthetic presses ahead and behind it."

- [ ] **Step 2: Update AGENTS.md**

In the Layout section replace the two bullets for `sequence_remote.c`-era files with:

```markdown
  - `sequence_core.c/.h` — pure C, no `furi` includes. Bit permutation, five-press
    fit, advance, single-press sync, pulse decoder and encoder, 64-byte journal
    record with CRC32. Host tests exercise this file.
  - `remote_name.c/.h` — pure C name validation, also host-tested.
  - `remote_store.c/.h` — SD layout (`<name>/<button>.<copy>.seq`), sorted
    in-memory index, journal write with read-back, create, delete, rename.
  - `radio.c/.h` — Sub-GHz RX capture with two-frame press confirmation, TX
    drive, 25 s / 3 s timeouts; polled from the dispatcher tick.
  - `acun_remote.c`, `acun_remote_i.h` — app struct, view dispatcher, scene
    manager, shared view callbacks, result-popup helper.
  - `scenes/` — one file per screen, X-macro registered in `acun_scene_config.h`.
```

Replace the Status section with:

```markdown
## Status

Version 0.2: main menu Read / Saved / About, named remotes with numbered
buttons in one sorted list, single-press sync when a heard button is already
saved, Send / Info / Rename / Delete per entry. Built on `ViewDispatcher` +
`SceneManager` with stock views. Hardware behaviour is still unverified.
```

- [ ] **Step 3: Align the spec with the implementation**

In the spec's Storage section replace "If more than 32 entries exist, the first 32 in sort order load and the Saved list header reads "Saved · 32 max"." with "If more than 32 entries exist, the first 32 found load and the Saved list header reads "Saved (32 max)"."

- [ ] **Step 4: Final build and tests**

Run the build command, then:

```sh
python3 tests/acun_remote/run.py 2>&1 | tail -3
strings flipper_apps/acun_remote/dist/acun_remote.fap | grep -c 'Acun Remote'
grep -rniE 'reverse|recover|sequence remote|sequence_remote' --include='*.md' --include='*.c' --include='*.h' --include='*.fam' flipper_apps README.md AGENTS.md || echo clean
```

Expected: `Built .../acun_remote.fap`; `Ran 11 tests ... OK`; count ≥ 1; `clean`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Document the Read / Saved / About UI and ship the 0.2 build"
```

---

## Self-review notes

- **Spec coverage.** Navigation: Tasks 5–8. Storage: Task 3. Sync math: Task 1. Radio: Task 4 (implemented as a dispatcher tick rather than a separate `FuriTimer`; same 10 ms cadence, same thread-safety property, simpler). Error table: every row maps to a popup or dialog in Tasks 5–8. Testing: Tasks 1–2. Documentation: Task 9.
- **Deviation recorded.** Store truncation loads the first 32 entries found, not the first 32 in sort order; the spec sentence is corrected in Task 9.
- **Type consistency checked.** `acun_popup_show(AcunApp*, const char*, const char*, AcunAfter)`, `remote_store_write(Storage*, const char*, uint8_t, SeqProfile*)`, `radio_tick(Radio*, SeqFrame*)`, `seq_sync(SeqProfile*, const SeqFrame*, int32_t*)` are spelled the same in every task.
