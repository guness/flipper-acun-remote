# AGENTS.md

Guidance for AI coding agents working in this repository.

## What this is

Acun Remote, a Flipper Zero external app (FAP). It learns one button of a
modular-accumulator remote from five consecutive presses and then transmits the
next value on demand. Sub-GHz, 433.92 MHz, internal CC1101 radio. The repo also
holds a host test suite for the C core and a build script.

## Layout

- `flipper_apps/acun_remote/` — the app.
  - `sequence_core.c/.h` — pure C, no `furi` includes. Bit permutation, five-press
    fit, advance, single-press sync, pulse decoder and encoder, 64-byte journal
    record with CRC32. Host tests exercise this file.
  - `remote_name.c/.h` — pure C name validation, also host-tested.
  - `remote_store.c/.h` — SD layout (`<name>/<button>.<copy>.seq`), sorted
    in-memory index, journal write with read-back, create, delete, rename.
  - `radio.c/.h` — Sub-GHz RX capture with three-frame press confirmation
    (listening has no timeout), TX drive with a 3 s watchdog; polled from the
    dispatcher tick.
  - `acun_remote.c`, `acun_remote_i.h` — app struct, view dispatcher, scene
    manager, shared view callbacks, result-popup helper.
  - `scenes/` — one file per screen, X-macro registered in `acun_scene_config.h`.
  - `application.fam` — FAP manifest.
  - `dist/acun_remote.fap`, `dist/build_info.json` — the shipped build and
    its SDK/FAP hashes. `dist/debug/` and `.vscode/` are ignored.
- `scripts/build_acun_remote.py` — builds against an SDK zip and toolchain
  in a temp dir without modifying the firmware checkout.
- `tests/acun_remote/run.py` — compiles `sequence_core.c` with `cc` and
  drives it through `ctypes`. Helpers are inlined; there is no separate module.
- `tests/acun_remote/data/` — fixtures: one directory per recorded set
  (`remote_a`, `remote_b`, `button_1`..`button_4`) with `press_1.sub` to
  `press_5.sub`, plus `expected.json` holding each set's prefix, step and words.

## Commands

```sh
python3 tests/acun_remote/run.py
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
```

The build needs the sibling `unleashed-firmware` checkout; the tests need nothing
outside this repo.

## Rules

- Tests read nothing outside the repository. No environment-variable escape
  hatches to sibling directories. New recorded sets get exactly five consecutive
  presses, named `press_N.sub`, and an entry in `expected.json`.
- Wording is functional: the app *learns* a remote button. Do not describe the
  protocol as reverse-engineered, recovered or analysed in docs, the manifest,
  comments or fixture names.
- Any change to `sequence_core.*` gets a host test. Keep `seq_unpack` strict;
  bump the `SEQREM01` magic if the record layout changes.
- Never start a transmission before the next index has been written to the SD
  card, synced and read back. A cancelled or failed transmission consumes an
  index; never roll one back.
- Rebuild `dist/` when C sources or `application.fam` change, and re-run the
  tests. `hardware_tested` in `build_info.json` stays `false` until someone
  confirms reception and transmission on a device; do not claim otherwise.
- Radio behaviour on hardware has not been validated. Say so when relevant.

## Status

Version 0.2: main menu Read / Saved / About, named remotes with numbered
buttons in one sorted list, single-press sync when a heard button is already
saved, Send / Info / Rename / Delete per entry. Built on `ViewDispatcher` +
`SceneManager` with stock views. Hardware behaviour is still unverified.
