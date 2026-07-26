# EP2 evidence — dampers, the pedal, and first playable

Date: 2026-07-26. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Flags: `-std=c23 -O2 -Wall -Wextra -Wpedantic -ffp-contract=off`; core TUs
additionally `-ffreestanding`. Commands: `make test`, `make exhibit`.

## Changes

- `src/ep_voice.c` — a second decrement table per (key, mode) and the live
  one that selects between them; `ep_bank_damp` / `ep_bank_undamp`; the
  restrike law behind `ep_bank_set_restrike`, with the amplitude ceiling of
  `ep-constants.md` section 5.4.
- `src/ep_piano.c` — the key, damper and pedal layer over the bank. One
  rule: the damper is off when the key is held or the pedal is down, on
  otherwise.
- `driver/ep73.c` — the live ALSA driver, sharing the organ driver's loop
  shape and flags rather than merging with it.
- `driver/render_midi.c` — an instrument switch, `-I organ` (default) or
  `-I ep73`.
- `driver/exhibit_ep_restrike.c` — the D5 A/B.
- `docs/ep-constants.md` — section 5.4 (restrike, D5), section 8 rewritten
  around the one damper rule, section 10.1 (the CC map, D6 closed).

## Test result

    9322 checks, 0 failures

Up from 9286. New coverage: the damped t60 table against its oracle and its
E1 anchor, and the fact that it carries no per-mode factor; the live
decrement following strike, damp and undamp; the rendered damped decay
within 1 % of the pinned table; every branch of the damper rule, including
catching the pedal late under a note already dying on its damper, releasing
a key with the pedal down, and letting the pedal go while a key is still
held; note-on at velocity 0 acting as a note-off; panic dropping dampers
without hard-muting; poly key pressure counted and silent; the default
restrike law being bit-identical to setting it explicitly; hostile law
values clamping; `replace` never reaching the ceiling and `add` never
passing it; the phase axis doing what it says on a ringing mode; and the
two bank layouts staying bit-identical under **all four** restrike laws,
which is what keeps decision D4 standing.

## The organ is still untouched

The instrument switch is the only change to a file the organ uses, and it
defaults to the organ. Re-rendering the pinned whole-song baseline on this
build:

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.030 -f \
        -o /tmp/regress.wav renders/ke9-emerson-automation.mid
    # peak 0.738, FNV64 6e56f252d97c240c (two runs identical)

which is the signature `docs/renders.md` pinned on 2026-07-23,
bit-for-bit. The 9242 organ checks pass unchanged in the same binary.

## Decision D5 — the ballot, not the count

`./build/exhibit_ep_restrike`, MIDI 64 (E4), sustain pedal down throughout,
48 kHz. The pedal is down because that is when the question exists at all:
let a key up and the damper stops the tine, and a restrike meets almost
nothing.

    1. eight repeats at velocity 80, 125 ms apart

       law                 peak    rms during   ring-out rms
                                   the repeats   at +1.5 s
       replace/continue    1.357       0.4851       0.1252
       replace/reset       1.246       0.4848       0.1251
       add/continue        2.846       0.8358       0.2005
       add/reset           2.639       0.8350       0.2004

    2. velocity 120, then velocity 20 onto its ring at +300 ms

       law                 before     after    change    step in the
                           the blow   50 ms             2 ms before / at
       replace/continue     0.5988    0.1138   -14.4 dB  0.07617 / 0.02323
       replace/reset        0.5997    0.1139   -14.4 dB  0.07797 / 0.02087
       add/continue         0.6401    0.7622    +1.5 dB  0.08041 / 0.12616
       add/reset            0.6411    0.7624    +1.5 dB  0.08235 / 0.11323

    3. velocity 20, then velocity 120 onto its ring at +300 ms

       law                 before     after    change    step in the
                           the blow   50 ms             2 ms before / at
       replace/continue     0.0892    0.8703   +19.8 dB  0.00876 / 0.36309
       replace/reset        0.0893    0.8623   +19.7 dB  0.00875 / 0.23536
       add/continue         0.1445    0.9358   +16.2 dB  0.01231 / 0.39179
       add/reset            0.1447    0.9269   +16.1 dB  0.01225 / 0.24843

WAVs: `build/ep2_d5_{0..3}_*.wav`.

**What the numbers say, and where they stop.**

Figure 2 is the one that separates the amplitude axis, and it does not
look like a matter of taste. Under `replace`, touching a key *gently*
while the note is ringing loudly makes it **14.4 dB quieter** — the soft
blow overwrites the loud ring. A hammer adds energy to a tine; it has no
mechanism for taking energy away. Under `add` the same gesture moves the
level by 1.5 dB, which is what one would expect from adding a small
excursion to a large one.

The cost of `add` is figure 1: eight repeats at one velocity climb to a
peak of 2.85 against 1.36, and an rms 4.7 dB higher, until the section 5.4
ceiling stops them. Whether that reads as a piano responding to insistence
or as an unmusical swell is the ear's call, and it is the reason `add` is
not simply declared correct here.

The phase axis is smaller and points the other way from intuition.
`reset` produces a *smaller* discontinuity at the blow, not a larger one —
0.235 against 0.363 in figure 3 — because restarting a mode at sine zero
starts it from silence, while continuing scales a waveform mid-swing.
Physically, though, a hammer striking a moving tine does not teleport it
to zero displacement, so `continue` is the honest one. In figure 2 neither
phase law produces a step larger than what the waveform was already doing
in the 2 ms before the blow, so no click is in question there; in figure 3
every law steps far above the quiet baseline, which is simply the attack.

**This is where the measurement stops and the ear starts.** The four WAVs
are the ballot. Once one wins, `ep-constants.md` section 5.4 says the
others are deleted rather than left as options.

## First playable

`build/ep73` is the live driver: same one-thread synchronous loop as the
organ's, `snd_pcm_writei` as the clock, the same xrun recovery through
`snd_pcm_recover`, the same flags. It is a second binary on purpose —
there is no merged multi-instrument driver. The instrument is mono until
EP4's tremolo, so the same sample goes to both channels, which keeps the
device configuration identical to the organ's.

**Not yet played on the rig.** The driver builds and its offline twin
renders; live play on the AG03 is the operator's gate and has not
happened, so EP2 is not closed.

The twin: `render_midi -I ep73`. The first whole-song EP render is logged
in `docs/renders.md` — the four-part BWV 565 arrangement, 7588 notes, only
24 folded against the organ's 234 because the compass is wider, peak
0.682, FNV64 `28a8aead10a6895d`, two runs identical. 510 seconds of audio
in 9.8 s wall clock, about 52x realtime, which is the active-gated layout
doing what D4 chose it for.

That render is an organ transcription played on a piano: the wrong music
for the instrument and the right test for the plumbing. It is there to be
listened to for defects — dampers cutting wrong, dropped notes, clicks —
not for tone. The file carries no CC64, so every note is stopped by its
damper on release and the pedal path is exercised only by the tests.

## Caveats

- **D5 is open.** The default stays `replace/continue`, which is the law
  EP1 was pinned on, so nothing has moved yet. The exhibit's own verdict
  is PASS on the measurements being sound, not on a winner.
- **Live play has not happened.** EP2's gate is not met until it does.
- **No drive, tremolo, cabinet or condition.** CC85 and CC91-93 are
  reserved in the map, counted by the driver, and wired to nothing.
- **Half-pedalling is not modelled** and no EP milestone owns it.
- The identity constants are still EP3's, unchanged from EP1: this
  milestone added mechanism, not voicing.
