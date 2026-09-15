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
reacts. A press counts once three identical complete frames have been heard
in a row; two was not enough to rule out a mid-transmission RF fade landing
the same way on two consecutive repeats of the same physical press.

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
- Back leaves Read at any step. Listening has no timeout; a receive overflow,
  or five presses that do not fit, offer Retry or Cancel.

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

## Framing

Live decoding reads the first 47 symbols as the prefix/word layout and emits
that frame the instant they complete, rather than waiting for a gap first. A
held button can repeat far faster than any gap threshold could safely wait
for, so decoding no longer depends on one: the very next high pulse is read
as the first bit of the next frame, with no silence required in between.
Genuine silence between separate button presses still resynchronizes the
decoder and is remembered for a learned button's own playback gap, but it no
longer gates whether a frame is considered complete. A frame's later pulses
are ignored, and every saved button now carries no trailing symbols. Highs
must belong to the short/long pulse clusters, lows must complement them, and
mean TE must be 250–550 microseconds. Raw pulse edges shorter than 30
microseconds are treated as RF ringing and merged into the pulse they
interrupted, the same filter the firmware's own Sub-GHz reader applies.
Transmission regenerates PWM with the learned TE and gap.

This avoids assuming that the whole protocol is exactly 47 bits, while no
longer assuming repeats are spaced apart either. Different buttons, missing
presses, and incompatible sequences prevent learning rather than being
guessed. Overflow allows another attempt.

## Build and verification

From the repository root, with the existing local SDK/toolchain:

```sh
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
python3 tests/acun_remote/run.py
```

The build extracts the SDK into a temporary directory and does not change the
firmware checkout. Add `--launch` to also copy the FAP to a USB-connected Flipper
and start it; close qFlipper first so the serial port is free. A normal `ufbt` build from this application directory also
works when ufbt is configured with a compatible SDK. The FAP manifest supports
building as an external application in a firmware checkout.

Host tests compile the same C core and name validator used in the app and
exercise five consecutive recorded presses from each of two remotes and four
buttons, ten complete 16-bit cycles, missing/corrupted/wrong-button samples,
CRC corruption, and waveform generation with trailing pulses that no longer
affect a saved button's identity. The live-parser test decodes every one of
the 50 raw BinRAW blocks, back to back with any gap or none between them.
BinRAW files lack the following gap, which this test explicitly restores.
Sync is tested by moving a learned profile to every recorded press and to
synthetic presses ahead of and behind it.

Fixtures live under `tests/acun_remote/data`: one directory per recorded set
(`remote_a`, `remote_b`, `button_1`–`button_4`) holding `press_1.sub` to
`press_5.sub`, plus `expected.json` with each set's prefix, step and decoded
words. The tests read nothing outside the repository.

## Hardware validation still required

The FAP builds and passes its firmware API/import check. Radio reception on the
device, generated RF timing and receiver acceptance have **not** been tested.
The UI reports transmission completion, not confirmation that the receiver acted.

Original-remote use can advance the receiver beyond the saved state; relearn the
affected button when needed. Whether the receiver shares a counter across buttons
is not established, so interleaving independently learned buttons also needs a
hardware check. This version does not invent a shared-counter synchronization
rule or claim that playback is guaranteed to operate the receiver.
