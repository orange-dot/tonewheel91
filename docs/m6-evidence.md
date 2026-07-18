# M6 evidence — the rotary speaker

Date: 2026-07-18. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, one clean-build run of each binary;
all numbers below are from that run.

## Changes

The section 15 rotary stage, whole: crossover, rotors with per-direction
inertia and the front-parking brake, horn Doppler + AM, band-limited
drum AM, two virtual mics into a stereo field, the amp ceiling, the
control surface and CC map, the stereo driver pipeline, and the exhibit
— M6-1..M6-10 in one change. The signal chain is now the full design.md
line: generator -> contacts -> percussion -> scanner -> swell -> drive
-> rotary.

- `tonewheel.h` — `tw_rotary` + `tw_stereo`; the instrument grows a
  rotary and a stereo tick. `tw_instrument_tick` stays the mono
  pre-rotary chain, bit-stable since M5; `tw_instrument_tick_stereo`
  appends the rotary, whose `bypass` default duplicates the mono chain
  onto both channels bit-identically (the scanner-OFF discipline, so
  every pre-M6 signature survives — verified below).
- `src/rotary.c` — every constant from the section 15.1 working set:
  - **Crossover** (M6-1): 800 Hz 12 dB/oct [HX] as a TPT state-variable
    filter, Butterworth damping, `g = tan(pi fc/fs)` from the repo sine
    kernel — no libm. The horn branch recombines inverted [derived]:
    a 2nd-order crossover's same-polarity sum nulls at fc; `lp - hp`
    is flat within the +3 dB fc bump.
  - **Rotors** (M6-2): rate = target + deviation, the deviation decaying
    multiplicatively with per-rotor, per-direction time constants
    ([RS]'s slip-limited rim drive as a first-order lag; the multiply
    lands exactly on target with no f32 stall). Brake pulls the phase to
    the front stop below 0.5 Hz and snaps exact [LB park-forward].
  - **Horn Doppler** (M6-3): fractional-delay line, per-mic delay
    `base - amp cos(horn - mic)`, 0.6 ms p-p swing inside the [FOLK]
    0.3-0.9 ms window, linear read.
  - **Horn AM** (M6-4): one directivity lobe per revolution, depth 0.40.
  - **Drum AM** (M6-5): depth 0.15 above a 200 Hz one-pole floor
    ([HX]: AM only over ~200-800 Hz), and structurally **no delay line
    on the drum path** — the no-Doppler pin is by construction.
  - **Stereo** (M6-6): virtual mics at +-1/8 turn (parked forward the
    field is exactly symmetric), balance = horn/drum tilt, width =
    mid/side scale.
  - **Amp ceiling** (M6-7): the M5 `tw_drive` stage reused ahead of the
    rotors with its own knob — "a 40 W tube amp pushed toward its
    ceiling" [LB].
- `driver/main.c` (M6-8) — true stereo render loop (f32 -> S32_LE, 2ch
  interleave); CC86 rotary mode (value/32), CC87 the live speed switch
  (chorale/tremolo), CC88 balance, CC89 width, CC90 rotary drive.
- `driver/render_midi.c` (M6-9) — stereo out (the WAV writer already
  took a channel count), the same CC86-90 map, `-m` initial rotary
  mode; FNV now hashes the interleaved stereo buffer.
- `test/test.c` — crossover magnitudes and the recombined flatness
  sweep; rise/fall taus, exact target landing, brake front-park (both
  rotors, both halves of the circle); the Doppler delay trajectory
  point-by-point against the geometry oracle (group-delay measured,
  crossover delay cancelled in differences) and the dynamic pitch swing;
  horn AM depth; drum zero-pitch-shift and band-limited AM; bypass
  identity/state-freeze, parked-front symmetry, width-0 mono collapse,
  decorrelation, balance ends, hostile knobs; amp ceiling boundedness
  and compression; instrument-level bypass identity, mid-note rejoin,
  and two-run FNV determinism of a mode-moving script.

## Test result

    7764 checks, 0 failures

(M5 baseline was 7710.) All pre-M6 signatures verified unchanged against
the recorded evidence: exhibit_phase `b71cbb09b1ecd064` /
`012442c11623cab8` / `96b17679450dec1b` (m1), exhibit_contacts
`3f25ffe656644fd6` (m3), exhibit_taper `0565b81fd82c84a7` (m3),
exhibit_percussion `69ae12dcd88cd2ea` (m3), exhibit_scanner
`079088b2a0394053` (m4), exhibit_drive identity `c5c2f6ce4161ca74` and
driven `bf83b19c31cd7b74` (m5) — rotary bypass is bit-identical to the
mono chain, by construction and by measurement.

## Exhibit result

    crossover (sec 15 [HX], 800 Hz 12 dB/oct):
      |LP(800)| 0.7071, |HP(800)| 0.7071 (pinned -3 dB = 0.707)
      inverted-horn sum over 100..6400 Hz: 1.014..1.414 (flat + the
      2nd-order +3 dB bump at the split)
    rotors ([FOLK] speeds; [RS] drum fall):
      horn rise tau: 15994 samples = 0.33 s (pinned 1/3 s)
      drum fall tau: 104205 samples = 2.17 s (pinned 6.5/3 s)
      brake: horn parks at the front stop, exact: yes
    horn (2 kHz tone, tremolo): pitch swing 50.9 Hz p-p (geometry says
      50.3), AM env floor 0.598 (depth 0.40)
    drum (500 Hz tone, tremolo): pitch residual 0.33 Hz p-p (no Doppler
      by construction), AM env floor 0.874
    drum (100 Hz tone): AM env floor 0.972 (below the 200 Hz floor the
      drum barely modulates)
    bypass identity: stereo == mono chain on both channels: bit-exact
    scripted determinism (transition render): FNV64 00eb4c9bf80cb408
      (two runs identical)

The horn/drum row pair is the section 15 acceptance in one place: the
same machine puts a 25 Hz pitch swing on a 2 kHz tone through the horn
and 0.33 Hz — measurement noise at the crossover seam, exactly the
residual [HX]'s hedge allows — on a 500 Hz tone through the drum, whose
own motion shows up only as level. The 0.874 drum floor at 500 Hz is
the depth-0.15 lobe diluted by the sub-200 Hz leak of its one-pole
split; at 100 Hz the modulation nearly vanishes, which is the [HX]
wavelength argument audible.

## A/B renders

    build/m6_chorale.wav     the passage (16'+5-1/3'+8'+4' chord, drive
                             0.4, vibrato off) at chorale — the slow
                             swirl
    build/m6_tremolo.wav     the same passage at tremolo
    build/m6_transition.wav  chorale -> tremolo at 5 s -> chorale at
                             10 s: the horn changes speed in ~1 s while
                             the drum takes seconds longer in each
                             direction — two audibly different clocks,
                             the [RS] rim-drive mechanics
    build/m6_horn_fm.wav     8' top key (~2.1 kHz) at tremolo: pitch
                             visibly swings (FM + AM)
    build/m6_drum_am.wav     8' low G (~196 Hz) at tremolo: level
                             breathes, pitch holds (AM only)

## The [FOLK] caveat and the by-ear open items

**Every rotor speed in this milestone is folklore.** The hunt recorded
in section 15 found construction, mechanics, and one transition time in
real sources — but no primary source for ~400/~340 rpm tremolo, ~40-50
rpm chorale, or the ~1 s horn rise. They are pinned as [FOLK] working
defaults precisely so this table of open verdicts exists:

- both tremolo and both chorale speeds — judge against reference
  recordings; a type plate or pulley measurement would settle it for
  good (section 15's open hunt);
- horn fall and drum rise taus (pure [decision] — nothing sourced);
- the Doppler swing (0.6 ms p-p sits mid-[FOLK]) and the base delay;
- both AM depths and the mic geometry (+-1/8 turn);
- the balance/width laws and any level trim;
- cabinet early reflections — deliberately not modeled at M6; the
  design.md acoustics doctrine names them as the next structure if the
  ear wants more room.

`make exhibit` re-derives every number above; the by-ear call overrides
all of them, and section 15 records why no derivation can do better
until a pole count or groove diameter surfaces.
