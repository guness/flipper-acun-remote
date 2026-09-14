# Acun Remote UI redesign

Date: 2026-09-14. Status: approved in discussion, awaiting implementation.

## Goal

Replace the four fixed button slots and the hand-drawn view with a menu-driven
app: a main menu of Read, Saved and About; named remotes with numbered buttons
in one list; single-press re-sync when a heard button is already saved; and
Send, Info, Rename and Delete for every saved entry. The learned-code math in
`sequence_core.c` is unchanged apart from one new sync function.

## Non-goals

- No migration of the old `button_N_M.seq` slot files. They are ignored.
- No shared-counter logic across buttons of one remote. Each entry is independent.
- No change to the 64-byte `SEQREM01` record, the frame decoder, the encoder,
  the fit, or the transmit timing.
- No hardware validation from this repository; that remains a manual step.

## Navigation

Stock firmware views on `ViewDispatcher` and `SceneManager`. One scene per
screen. Back is handled by the scene manager and always returns to the
previous scene unless a scene says otherwise below.

### Main menu (Submenu)

Items: Read, Saved, About. Back exits the app.

### Read

1. **ReadListen** (Popup). Header "Listening 433.92 MHz", text "Hold a remote
   button". The radio is receiving from the moment the scene shows. A press is
   confirmed as today: two identical complete frames. Back stops the radio and
   returns to the main menu.
2. When a press is confirmed the store is searched with `seq_same_button`
   (prefix, word bit 15, trailing symbols).
   - **Match, entry readable** → **ReadSync**.
   - **Match, entry damaged** → **ReadRetry** with text "Garage B2 is damaged.
     Delete it in Saved, then read again." and only a Cancel button.
   - **No match** → **ReadLearn** with the press counted as 1 of 5.
3. **ReadSync** (DialogEx). Header "Garage · B2". Text is one of
   "Remote is 3 presses ahead", "Remote is in sync", or "Remote is 5 presses
   behind. Receiver may reject." Buttons: left Cancel, right Sync. Sync writes
   the journal, shows Popup "Synced" for 1.5 s, and returns to the main menu.
   Cancel returns to the main menu.
4. **ReadLearn** (Widget). Line 1 "New remote", line 2 "Press 2 of 5", line 3
   "Release, then press the same button again." The radio keeps receiving
   between presses. Repeats of a held press are dropped because they equal the
   last captured frame. A confirmed press from a different identity is ignored
   and line 3 reads "Different button, ignored" until the next accepted press.
   After the fifth press `seq_fit` runs. Success → **NameSelect**. Failure →
   **ReadRetry** with "Presses don't fit. Start over?" Back stops the radio
   and returns to the main menu, discarding captured presses.
5. **ReadRetry** (DialogEx). Also reached from ReadListen and ReadLearn on
   receive-queue overflow ("Receive overflow") and on 25 s without a confirmed
   press ("No press heard"). Buttons: left Cancel → main menu, right Retry →
   ReadListen with the learn state reset. The 25 s timer restarts on every
   accepted press.
6. **NameSelect** (Submenu). Header "Remote". Items: every distinct saved remote
   name, then "New remote…". Picking a name → **NumberSelect**. "New remote…"
   → **NameInput**.
7. **NameInput** (TextInput). Header "Remote name". 1–12 characters from
   letters, digits, space, underscore, hyphen; no leading or trailing space.
   The validator rejects anything else with "Letters, digits, space, - _ only".
   OK → **NumberSelect**.
8. **NumberSelect** (NumberInput). Header "Button number", range 1–8. Default
   is the lowest number that remote does not use yet, or the current number
   when renaming. OK → collision check.
   - Name and number free → save both journal copies → Popup "Saved" 1.5 s →
     **SavedList**.
   - Name and number taken → **ReplaceConfirm** (DialogEx) "Garage B2 exists.
     Replace it?" with Back and Replace. Replace deletes the existing pair,
     saves the new one, then continues as above.
9. Back during naming: NameInput → NameSelect; NumberSelect → NameSelect;
   NameSelect → main menu when learning, discarding the fit, or → EntryMenu
   when renaming. No radio is on during these scenes.

### Saved

- **SavedList** (Submenu). Header "Saved". One item per entry, sorted by remote
  name (case-insensitive) then button number, labelled "Garage B1". A damaged
  entry is labelled "Garage B1 (damaged)". Empty store shows a Widget with
  "Nothing saved yet. Use Read." Selecting an item → **EntryMenu**.
- **EntryMenu** (Submenu). Header is the entry label. Items: Send, Info,
  Rename, Delete. A damaged entry shows only Delete.
- **Send** (Popup). Region check first; if blocked → Popup "Region blocks
  433.92 MHz TX" 1.5 s → EntryMenu. Otherwise advance a copy of the profile,
  write it to the journal, read it back; on failure → Popup "Save failed.
  Nothing sent." and the entry is marked damaged → SavedList. On success the
  radio transmits six repeats while the Popup reads "Sending Garage B2 / Back
  stops". Completion → Popup "Sent. Check receiver." Back or the 3 s watchdog
  → Popup "Stopped; index advanced." Radio start failure → Popup "TX failed;
  index advanced." All result popups last 1.5 s and return to EntryMenu.
- **Info** (Widget). Lines: label, `Prefix %08lX`, `Step %04X  Word %04X`,
  `Index %04X  Sends %lu`, `%u pulses  TE %u us  Gap %lu us`. Back → EntryMenu.
- **Rename**. Reuses NameSelect → NameInput → NumberSelect with a rename flag.
  Collision with a different entry → ReplaceConfirm is not offered; instead
  Popup "Garage B2 exists" 1.5 s → NumberSelect. Same name and number as
  before → SavedList with nothing written. Success moves both files → Popup
  "Renamed" 1.5 s → SavedList.
- **DeleteConfirm** (DialogEx). "Delete Garage B2?" with Cancel and Delete.
  Delete removes both files and the remote's directory if it is now empty →
  SavedList.

### About (Widget)

"Acun Remote 0.2", "433.92 MHz, internal radio", "Read learns a button in five
presses or re-syncs a saved one.", "Saved sends, renames and deletes."

## Storage

- Root: `APP_DATA_PATH("")`, created at startup if missing.
- One directory per remote, named exactly as entered. One journaled pair per
  button: `<name>/<n>.0.seq` and `<name>/<n>.1.seq`, for example
  `Garage/2.0.seq`. Record format is the existing `SEQREM01` 64-byte record;
  name and number live only in the path.
- Journal rules are unchanged: a write increments `generation`, goes to copy
  `generation & 1`, is synced and read back and byte-compared. On load the
  copy with the higher generation wins. If either copy exists but fails to
  parse, the entry is damaged and only Delete is offered. A new entry writes
  both copies (generations 1 and 2).
- In-memory index: up to 32 entries of `{ char name[13]; uint8_t button;
  SeqProfile profile; bool damaged; }`, rebuilt from disk at startup and after
  every save, sync, rename, delete or send. Directories or files that do not
  fit the naming pattern are ignored. If more than 32 entries exist, the first
  32 in sort order load and the Saved list header reads "Saved · 32 max".
- Limits: name 1–12 characters, button 1–8, 32 entries.

## Core addition: single-press sync

```c
/* Move profile to the accumulator implied by a heard press of the same button.
 * delta is presses ahead (>0) or behind (<0) of the saved state. Returns false
 * if the frame is not the same button. sends is unchanged. */
bool seq_sync(SeqProfile* profile, const SeqFrame* heard, int32_t* delta);
```

A word exposes 15 counter bits, so two accumulators fit it. For every k in
0..65535 compute `acc = accumulator + k*step` (mod 2^16); the candidates are the
k where `acc >> 1 == permute(word & 0x7FFF)`. Map k to a signed distance,
`k` if k ≤ 32767 else `k − 65536`, and pick the candidate with the smallest
absolute distance; on a tie prefer the positive one. Set `accumulator` and
`frame.word` accordingly and report the distance. Timing, gap and trailing
symbols keep the saved values. With an even step only one candidate is
reachable and the two are output-equivalent anyway.

## Radio module

`radio.c/h` wraps the Sub-GHz device for both flows.

- `radio_rx_start(app)` resets the decoder, clears the pulse queue, starts
  async RX with the pulse callback (ISR context, queue put, overflow flag).
- A 10 ms `FuriTimer` drains the queue through `seq_decode` and applies the
  two-identical-frames rule. A confirmed press, an overflow, or the 25 s
  inactivity expiry is delivered as a `view_dispatcher_send_custom_event`.
  Timer callbacks never touch views directly.
- `radio_tx_start(app, profile)` runs the existing level-duration callback for
  six repeats; the same 10 ms timer polls completion and the 3 s watchdog and
  sends TxDone or TxTimeout events.
- `radio_stop(app)` stops RX or TX, idles and sleeps the chip. Every scene that
  turns the radio on calls it in `on_exit`; ReadLearn restarts RX in its
  `on_enter`, and the few milliseconds of gap fall while the user is releasing
  the button.

## Code layout

```
flipper_apps/acun_remote/
  application.fam                 appid acun_remote, version 0.2, icon.png
  acun_remote.c                   alloc/free, view dispatcher, scene manager, entry point
  acun_remote_i.h                 App struct, view ids, custom event ids
  sequence_core.c/.h              unchanged + seq_sync
  remote_name.c/.h                pure name validation, host-testable
  remote_store.c/.h               paths, index, load/save/sync/rename/delete
  radio.c/.h                      RX capture, TX drive, timers
  scenes/scene_config.h           X-macro list of scenes
  scenes/scenes.c/.h              handler tables
  scenes/scene_*.c                one file per scene listed above
```

Manifest `requires` stays `["gui", "storage"]`. `fap_description` becomes
"Learn, sync and send modular-accumulator remote buttons".

## Error handling summary

| Situation | Where | Behaviour |
|---|---|---|
| Damaged journal | load | listed as "(damaged)", Delete only; matched press cannot sync |
| SD write or read-back fails | save, sync, rename, send | dialog or popup names the failure; entry marked damaged; index rebuilt |
| Region blocks TX | Send | popup, nothing written, nothing sent |
| Radio fails to start TX | Send | popup; the reserved index is kept, never rolled back |
| Back or 3 s watchdog during TX | Send | radio stopped; popup says the index advanced |
| Pulse queue overflow | Read | ReadRetry "Receive overflow" |
| 25 s without a confirmed press | Read | ReadRetry "No press heard" |
| Five presses don't fit | ReadLearn | ReadRetry "Presses don't fit" |
| Different button during learn | ReadLearn | ignored, hint on line 3 |
| Name/number collision | NumberSelect | Replace dialog when learning, popup when renaming |

## Testing

Host tests in `tests/acun_remote/run.py`, compiling `sequence_core.c` and
`remote_name.c`:

- `seq_sync` on every fixture set: fit the five presses, then sync with each of
  the five words in turn and assert delta is −4 … 0; after syncing to word 3,
  advance twice and assert word 5.
- Ahead case: build word for press 8 from the reference arithmetic, sync,
  assert delta 3, advance once, assert it equals the reference word for
  press 9. Behind case: sync to press 3 from press 5, assert delta −2.
- Even and odd steps: covered by fixtures (D1C1, D1C2, D1C3, D1C4, 3762).
- Wrong button: a frame with a different prefix or trailing count returns false
  and leaves the profile untouched.
- Full cycle after sync: sync then 65536 advances match the reference.
- `remote_name_valid`: accepts "Garage", "Gate 2", "front-door_1"; rejects
  empty, 13 characters, leading or trailing space, slash, dot, quote.

Device-side behaviour is verified by building with
`scripts/build_acun_remote.py`, which runs the SDK API check. Radio reception
and receiver acceptance still need a manual test on hardware; the README keeps
saying so.

## Documentation

`flipper_apps/acun_remote/README.md` is rewritten around the new flow and
storage layout. The top-level README and AGENTS.md status section are updated.
`build_info.json` keeps `hardware_tested: false`.
