# EC / DH saved-profile comparison

Profiles downloaded read-only from the connected Flipper on 2026-09-19.
The highest-generation journal copy was selected for each button; downloaded
records passed their CRC checks. No saved counters or timing were changed.

| Saved field | EC B1 | DH B1 |
| --- | --- | --- |
| Prefix | 01F550C4 | 01F50394 |
| Step | D1C1 | 3762 |
| Saved word | 1A2B | 81AB |
| Saved accumulator | 7614 | 4354 |
| Send attempts | 26 | 6 |
| Base timing / TE | 406 us | 370 us |
| Final low / gap | 9.588 ms | 9.598 ms |
| Trailing symbols | 2, value 00 | None stored |
| Encoded frame period for the saved word | 87.946 ms | 78.788 ms |

These periods are calculated playback durations, not measured RF. For frames
without a stored suffix, the final high depends on the changing code word,
so the next frame's period can differ by two TE units.

The prefixes and steps match the existing EC (`remote_b`) and DH (`remote_a`)
five-press fixtures. The counter model passes the recorded sequences, but that
does not establish the current gate counter or acceptance window. Send attempts
count reserved app presses, including cancelled or failed transmissions; they
are not receiver acknowledgements and are not expected to match between remotes.

The original DH B1 fixtures store TE values 411, 411, 413, 412 and 408 us.
Its current saved 370 us is about 10% shorter. A concrete cause of biased
capture timing was found in the immediate decoder: it averaged normalized high
pulses only. RX duty-cycle distortion can shorten highs while lengthening lows.
The decoder now estimates TE from complete high/low pairs and the final high,
excluding the final low/gap. Regression tests cover both final-bit values and
high/low skew in either direction.

DH B1 has no stored trailing symbols. That may indicate fallback capture;
it does not prove how many symbols the physical remote transmits. The earlier claim that its BinRAW fixtures contain only 47 highs was incorrect:
valid bits are right-aligned within their bytes. Correct alignment retains the
49-symbol frames and their leading low. Other DH buttons cannot supply B1's tail: stored suffixes differ.
Do not copy EC timing, its tail, or another DH button's counter into DH B1.

Next hardware check: use the original DH B1 button that operates its gate,
then Read and Sync that button to refresh the current counter and timing.
Check its updated Info and test one app press. If it still fails, obtain a
continuous RAW recording of the original DH B1 (long hold and short tap),
and ideally capture the Flipper's output with a second receiver. This separates
remaining frame-shape differences from counter/receiver-acceptance questions.

The user reported EC B1 successfully operating a gate, while DH B1 failed.
That is partial hardware feedback. The DH failure is not yet conclusively
attributed to one cause, and the new timing correction has not been gate-tested.

## Follow-up: autosaved DH B1 captures

The user confirmed the original DH B1 still operates its gate and the failed
Flipper test followed Read + Sync. Read-only inspection of the supplied
`/Users/guness/workspace/flipper/subghz/autosave` folder found 1,124 files:
1,101 BinRAW and 23 Princeton. These files are not dependencies of host tests.

After correcting byte alignment, 1,197 blocks in 1,079 files match DH B1's
prefix and fixed flag. Of these, 1,194 have 49 high pulses: 1,066 end in `10`
and 128 in `00`. The other three are irregular/truncated. For the `10` trailer,
1,033 blocks encode high/low/high as 3/2/1 TE; for `00`, all 128 encode 1/4/1 TE.
Thus the first trailer pair takes five TE, unlike four-TE core pairs.

This explains a concrete distinction from EC B1: the timing decoder previously
rejected a two-TE low after a long high in DH's common `10` trailer. It could
therefore fall back to 47 symbols even after Sync. The encoder also omitted
that extra TE in its trailer low. Both paths now handle it; TE estimation
accounts for the extra unit. Existing repository recordings test the corrected
alignment and trailer, without adding external paths to the test suite.

The earlier sequence inspection found 1,048 single-step, 23 double-step and two
triple-step transitions among same-date files, with no other jumps. That supports
the existing step model; it does not establish the current gate's counter.

These findings supersede the earlier suggestion that a missing tail was only
speculative. They establish a playback mismatch, but not yet gate acceptance
of the fix. A fresh original DH B1 press and Sync is required to replace the
saved zero-length trailer. Do not synthesize a fixed trailer from the majority:
both observed tail values occur in these recordings.
