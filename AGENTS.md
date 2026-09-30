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
    (listening has no timeout), hold-to-send TX with a 3 s frame-progress watchdog; polled from the
    dispatcher tick.
  - `acun_remote.c`, `acun_remote_i.h` — app struct, view dispatcher, scene
    manager, shared view callbacks, result-popup helper.
  - `scenes/` — one file per screen, X-macro registered in `acun_scene_config.h`.
  - `application.fam` — FAP manifest; `fap_version` must match the top
    `CHANGELOG.md` entry. About uses the SDK-provided `FAP_VERSION` from this
    manifest; do not add a separate hardcoded version string.
  - `dist/` — ignored local build output. Compiled `.fap` files and build
    metadata are distributed as GitHub Release assets, never committed.
- `docs/user-guide.md`, `docs/screenshots/` — user documentation and screenshots.
  Technical detail goes in `docs/acun_remote.md`; version history is in
  `CHANGELOG.md`. There are no catalog-specific Markdown restrictions.
- `.github/workflows/release.yml` — on every pushed tag, test, build official and
  Unleashed variants, then publish a GitHub Release with binaries and metadata.
- `scripts/install_acun_remote.py` — bootstrap a repository-local uFBT setup,
  build for selected firmware, and install over USB (or `--build-only`).
- `tests/scripts/` — host tests for installer behaviour; no network or hardware.
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
python3 -m unittest discover -s tests/scripts -v
python3 scripts/install_acun_remote.py --firmware official --build-only
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
```

The local SDK build helper needs the sibling `unleashed-firmware` checkout.
The installer downloads its dependencies; the tests need nothing outside this repo.

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
- Rebuild locally when C sources or `application.fam` change, and re-run the
  tests. Never commit `dist/`, `.install/`, or release assets.
  `hardware_tested` in `build_info.json` stays `false` until someone
  confirms reception and transmission on a device; do not claim otherwise.
- Radio behaviour on hardware has not been validated. Say so when relevant.

## Status

Version 1.0: main menu Read / Saved / About, Sync from file, hold-to-send, named remotes with numbered
buttons in one sorted list, single-press sync when a heard button is already
saved, Send / Info / Rename / Delete per entry. Built on `ViewDispatcher` +
`SceneManager` with stock views. Hardware behaviour is still unverified.
