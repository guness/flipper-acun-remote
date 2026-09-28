# Acun Remote: technical notes

Flipper Zero external app for a modular-accumulator remote protocol. It learns
a remote button from five consecutive presses, keeps that button's counter on
the SD card, and sends the next value on demand. Internal radio at
**433.92 MHz**. This is not a generic KeeLoq learner.

## Install

Copy `flipper_apps/acun_remote/dist/acun_remote.fap` to the Flipper SD card under `apps/Sub-GHz/`. The
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
  press and refreshes its observed playback timing and trailing symbols.
  Syncing to a remote that is behind is allowed, but the receiver may
  reject those codes and the dialog says so.
- **New button.** Otherwise the app asks for the same button five times in a
  row. Release between presses and do not press other buttons; a different
  button is ignored with a hint. Three presses fit the counter model and two
  validate it. Then choose a suggested remote name or **New remote...** (1 to 12
  letters, digits, spaces, `-` or `_`), choose a button number from 1 to 8,
  and the entry is saved. Suggestions require a saved button whose fixed code
  and counter step differ together by fewer than eight positions, matching
  the observed sibling-button pattern. Unrelated names are hidden; multiple
  candidates remain selectable. This is not a unique-device guarantee.
  Rename still lists all names. If that name and number already exist you can
  replace them.
- Back leaves Read at any step. Listening has no timeout; a receive overflow,
  or five presses that do not fit, offer Retry or Cancel.

### Saved

One list of every learned button, sorted by remote name and then number, for
example "Garage B1", "Garage B2", "Gate B1". Open an entry for:

- **Send** opens a ready screen showing the current index and word in hex.
  Hold the center (OK) button to transmit continuously. On release, the app
  shows "Finishing" while the encoder finishes its six-repeat tail, then
  returns to Ready. This matches the local Unleashed Sub-GHz hold/release
  behavior: held frames do not consume the repeat budget, and release starts
  counting it down at frame boundaries. Six is Acun's configured repeat count;
  other firmware protocols can use different counts. The blue LED keeps
  blinking until RF completes. Back or an error stops transmission immediately.
  Pressing OK again during Finishing starts a new press with a new counter.
  Each new press advances the counter once, then repeats that same code for
  the entire hold. The counter is written to the SD card, synced and read back
  before the radio starts. Back also stops transmission; a stopped or failed
  send keeps its reserved index. The 3-second watchdog detects stalled frame
  generation rather than limiting how long you can hold the button.
- **Info** shows only this button's send attempts, counter, step, word,
  prefix, TE, gap and saved tail. Scroll with Up/Down. Shared format and
  operating information is in **About**.
- **Sync from file** opens the SD browser at `/ext/subghz`. Select a RAW or
  BinRAW `.sub` recording of the same button at 433.92 MHz / AM270. The app
  checks the file and previews its counter offset before any write. **Sync**
  uses the recorded counter and timing; **Cancel** changes nothing. Neither
  option transmits. Older recordings can put the saved counter behind the
  gate, so prefer Read for a live press when the file is old.
  Files must represent one press: conflicting confirmed codes are rejected.
  RAW needs three matching complete frames; BinRAW needs a complete block.
  Files are limited to 1 MiB, lines to 4095 characters and BinRAW blocks to
  4096 bits. Unsupported formats or recordings without a usable frame are
  rejected. Successful updates use the existing synced/read-back journal.
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

Transmission uses Flipper's native `SubGhzTransmitter` framework with an
app-local Acun protocol registry. The Acun encoder supplies the learned pulse,
gap and tail through the native transmitter's yield callback. Its typed start
method accepts the already-reserved frame; the encoder never advances counters.
These framework APIs are shared with official firmware, though this shipped
build is checked against the local Unleashed SDK.

## Framing

Live decoding reads the first 47 symbols as the prefix/word layout and emits
that frame the instant they complete, rather than waiting for a gap first. A
held button can repeat far faster than any gap threshold could safely wait
for, so decoding no longer depends on one: the very next high pulse is read
as the first bit of the next frame, with no silence required in between.
Genuine silence between separate button presses still resynchronizes the
decoder and is remembered for a learned button's own playback gap, but it no
longer gates whether a frame is considered complete. A parallel timing capture watches complete silence-delimited frames of 47–55
symbols. After two complete frames agree on the word and trailing symbols,
that observed suffix, gap and pulse timing enrich the confirmed press used
for learning or Sync. TE is estimated from complete high/low pairs, excluding
the final silence, so receive duty-cycle distortion does not bias it toward
short high pulses. If no consistent complete frame is available, the immediate
decoder's zero-suffix fallback remains in use. Its TE estimate also uses
high/low pair sums, excluding the final low, rather than averaging only highs. Highs
must belong to the short/long pulse clusters, lows must complement them, and
mean TE must be 250–550 microseconds. Raw pulse edges shorter than 30
microseconds are treated as RF ringing and merged into the pulse they
interrupted, the same filter the firmware's own Sub-GHz reader applies.
Transmission regenerates PWM with the learned TE and gap. For two-symbol
trailers, the first low includes an extra TE, matching the observed five-TE
trailer pair. The complete-frame timing decoder accepts that extended low and
accounts for it when estimating TE.

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
Complete-frame timing tests cover trailing symbols, receive duty-cycle
distortion, invalid frame lengths and timing refresh on Sync.
Sync is tested by moving a learned profile to every recorded press and to
synthetic presses ahead of and behind it. A host harness also runs the send
scene with UI, radio and storage substitutes to check press/release handling,
one reservation per hold, save-before-TX ordering, failure handling and exit.
The TX sequence harness checks a 300-frame hold, a quick tap and release at
every pulse position, including suffix-bearing frames.
The native encoder adapter harness checks pulse output, frame-progress updates,
release, cancellation and restart through the protocol callbacks with host
substitutes for firmware types.
It does not validate physical button timing or RF transmission.

Fixtures live under `tests/acun_remote/data`: one directory per recorded set
(`remote_a`, `remote_b`, `button_1`–`button_4`) holding `press_1.sub` to
`press_5.sub`, plus `expected.json` with each set's prefix, step and decoded
words. The tests read nothing outside the repository.

## Hardware validation still required

The FAP builds and passes its firmware API/import check. The user reported
EC B1 operating its gate on 2026-09-19, while DH B1 did not. This is partial
hardware feedback, not validation of all profiles or generated RF timing.
`hardware_tested` remains false pending broader reception/transmission checks.
The UI reports transmission completion, not confirmation that the receiver acted.

Original-remote use can advance the receiver beyond the saved state; relearn the
affected button when needed. Whether the receiver shares a counter across buttons
is not established, so interleaving independently learned buttons also needs a
hardware check. This version does not invent a shared-counter synchronization
rule or claim that playback is guaranteed to operate the receiver.
