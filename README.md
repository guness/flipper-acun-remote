# Flipper Acun Remote

A Flipper Zero app that learns a button of your own Acun gate or garage remote
and sends the next code on demand, so the Flipper can act as a spare remote.
Sub-GHz, 433.92 MHz, internal radio.

These remotes don't repeat a fixed code: each press sends a new value from a
counter. The app listens to five presses of one button to learn how that
counter moves, stores it on the SD card, and advances it on every send.

## Features

- **Read**: learn a new button from five presses, or bring a saved button up
  to date from a single press when the original remote has been used since.
- **Saved**: named remotes with numbered buttons ("Garage B1", "Garage B2").
  Send, Info, Sync from a `.sub` file, Rename, Delete.
- **Send**: hold OK to transmit. Each hold sends one new code.
- The next counter value is written to the SD card and read back before the
  radio starts, so an interrupted send never reuses a code.

See the [user guide](flipper_apps/acun_remote/README.md) for step-by-step use.

## Install

Download the `.fap` for your firmware from
[Releases](https://github.com/guness/flipper-acun-remote/releases) and copy it
to `apps/Sub-GHz/` on the SD card. Builds are provided for the official
firmware and for Unleashed. If the app reports an API mismatch, build it for
your firmware as below.

## Build

The app builds with [uFBT](https://pypi.org/project/ufbt/):

```sh
python3 -m pip install --upgrade ufbt
ufbt update                     # official release SDK
cd flipper_apps/acun_remote
ufbt                            # writes dist/acun_remote.fap
ufbt launch                     # optional: install and start over USB
```

For Unleashed, fetch its SDK instead:

```sh
ufbt update --index-url=https://up.unleashedflip.com/directory.json --channel=release
```

Close qFlipper before `ufbt launch` so the serial port is free.

`dist/acun_remote.fap` in this repo is the Unleashed build. It is rebuilt with
`scripts/build_acun_remote.py`, which uses an SDK zip and toolchain from a local
firmware checkout without modifying it and records the SDK hash in
`dist/build_info.json`:

```sh
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
```

## Test

```sh
python3 tests/acun_remote/run.py
```

Needs Python 3 and a C compiler (`cc`), nothing else. The core logic
(`sequence_core.c`, `remote_name.c`) has no firmware dependencies, so the tests
compile it for your computer and run it against recorded presses in
`tests/acun_remote/data/`: five consecutive presses per remote and button, with
the expected values in `expected.json`.

Before submitting changes, also run `ufbt format` in `flipper_apps/acun_remote`.
The Flipper Apps Catalog rejects code that isn't clang-formatted.

## Layout

| Path | Contents |
|---|---|
| `flipper_apps/acun_remote/` | App source, manifest, icon, catalog README, changelog, screenshots |
| `flipper_apps/acun_remote/scenes/` | One file per screen |
| `docs/acun_remote.md` | Technical notes: storage format, framing, timing, test coverage |
| `scripts/` | Build script |
| `tests/acun_remote/` | Host tests and recorded fixtures |

## Status

Tested on a limited number of remotes. Behaviour with other receivers may
differ. Use it only with remotes and receivers you own.

## License

Copyright (C) 2026 guness. Licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE).
