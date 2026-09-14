# Acun Remote

Flipper Zero external app for a modular-accumulator remote protocol. Four
persistent button slots; internal radio at **433.92 MHz**. This is not a
generic KeeLoq learner.

## Install and use

Copy `dist/acun_remote.fap` to the Flipper SD card under `apps/Sub-GHz/`.
The supplied build targets the local Unleashed SDK, hardware f7, API **88.11**.
Firmware with an incompatible API needs a rebuild against its own SDK.

1. Open **Apps → Sub-GHz → Acun Remote**.
2. Use **Up/Down** to select Button 1–4, then **Right** to learn it.
3. Release the original remote. Press **OK** on the Flipper to listen, then
   hold the desired original button until the screen reports a capture.
4. Release it and repeat when prompted, for **five separate presses of the same
   button**. Do not make extra presses between captures. Multiple packets from
   one held press count as one capture; the app requires two identical complete
   frames to confirm each captured press.
5. Three presses fit the accumulator model; two further presses validate it.
   Press **OK Save** after validation. An existing slot is replaced only when
   saving the new result. A failed fit restarts learning.
6. On the home screen, **OK** sends the next value. Six repetitions of that
   same frame represent one press; the counter advances only once.
7. **Left** shows parameters. **Back** cancels learning or exits from home.
   Back during transmission stops the radio, but keeps the reserved index.

Each button is learned independently; the app does not assume that button slot
numbers correspond to particular multipliers. Every remote and button variant is
learned using the same fixed bit permutation.

## State and storage

For each slot the app saves the prefix, word flag, multiplier, current 16-bit
accumulator, trailing symbols, timing, send-reservation count and generation.
The displayed **Index** is the accumulator value, not an asserted factory
counter. Each send adds the learned multiplier modulo 65536.

Data is stored in `apps_data/acun_remote/button_N_0.seq` and
`button_N_1.seq`. These are versioned 64-byte little-endian records with CRC32.
The two files alternate as a journal. Before radio transmission the next state
is written, synced and read back. Failure blocks transmission. A cancelled or
failed transmission may consume one index; it is never rolled back.

On startup, a damaged record blocks the slot instead of reverting to a possibly
already-transmitted older index. Relearning replaces both copies. Keep the SD
card inserted while using the app. The frequency stays subject to the firmware's
normal transmission-region checks.

## Framing

Live decoding uses complete gap-to-gap pulse sequences, not the padded BinRAW
bytes. It decodes the first 47 symbols as the prefix/word layout
and preserves up to eight additional trailing symbols. Prefix, word flag and
trailing symbols must stay constant across learning captures. Highs must belong
to the short/long pulse clusters, lows must complement them, and mean TE must be
250–550 microseconds. Transmission regenerates PWM with the learned TE and gap.

This avoids assuming that the whole protocol is exactly 47 bits. Inconsistent
tails, different buttons, missing presses, and incompatible sequences prevent
learning rather than being guessed. Overflow and timeout allow another attempt.

## Build and verification

From the repository root, with the existing local SDK/toolchain:

```sh
python3 scripts/build_acun_remote.py \
  --sdk ../unleashed-firmware/dist/f7-C/flipper-z-f7-sdk-local.zip \
  --toolchain ../unleashed-firmware/toolchain/current
python3 tests/acun_remote/run.py
```

The build extracts the SDK into a temporary directory and does not change the
firmware checkout. A normal `ufbt` build from this application directory also
works when ufbt is configured with a compatible SDK. The FAP manifest supports
building as an external application in a firmware checkout.

Host tests compile the same C core used in the app and exercise five consecutive
recorded presses from each of two remotes and four buttons, ten complete 16-bit
cycles, missing/corrupted/wrong-button samples, CRC corruption, and waveform
generation with trailing symbols. The live-parser test decodes every recorded
press and matches 45 of the 50 raw BinRAW blocks; noisy trailing pulses cause
the other five to be rejected. BinRAW files lack the following gap, which this
test explicitly restores.

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
