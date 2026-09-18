# Original remote timing check

Captured file: `/ext/subghz/Raw.sub`, pulled on 2026-09-16. Local capture copy:
`/private/tmp/acun-Raw.sub`. This single hold is not a five-press learning fixture.

The RAW capture contains 16,745 pulse durations covering 16.226881 seconds,
at 433.92 MHz with the AM270 preset.

Among its silence-delimited intervals, 166 contain 49 high/low pulse pairs.
All 166 have the same 47-symbol core and two trailing zero symbols. Their
median start-to-start period is 87.959 ms, including a median final low of
9.588 ms. There are no longer pauses within this regular portion. Longer,
irregular intervals near the end do not establish a deliberate burst pattern;
button-release timing was not recorded independently.

The complete-frame timing decoder yields a median TE of 406 microseconds.
The high pulses alone underestimate TE because the captured lows are longer
than ideal PWM complements. Pair sums give a better timing estimate. One
trailing pair is also longer than an ideal four-TE symbol; the app's uniform
PWM encoder remains an approximation, not a pulse-for-pulse reproduction.

The new timing path finds the repeated 49-symbol shape before the first
three-frame press confirmation. A damaged interval near the end produces a
different prefix and is not evidence for changing the repeated frame shape.
Playback metadata is accepted only after two complete frames agree on the
core and suffix. Immediate identity decoding remains available independently
for captures with short or absent gaps.

Existing saved entries need a new **Read → original button press → Sync** to
refresh timing and suffix data. The existing journal already stores these
fields; its layout and magic are unchanged. Transmission still reserves and
verifies the next counter before sending. Receiver acceptance and physical
RF timing have not been validated.
