# V5 in SPICE: sync between two VCO-2 cores

Date: 2026-11-15. Tooling `driver/spice/vco/vco5.py` (bench and analysis),
`vco5_fit.py` (the reset function), `vco5_render.py` (listening); every
number here is from `build/vco/vco5/*.json` and `fits5.json`. Nothing in the
core has changed, and the proposal of section 4 was declined.

## 1. Bench

Two VCO-2 cores (`vco2.cir`), boards A and B with their step-2 trims, on
ideal 12 V and 8 V rails, so the supply coupling of V4 is out of the
picture. A is the master. Each of its resets reaches B through one of two
networks, both study circuits and neither a board design:

- **reset**: a resistor from A's reset pulse (`SQUARE_A`) to the base of
  B's reset driver (`DBASE_B`), beside B's own 100 k. A's pulse turns on B's
  reset switch from outside.
- **thresh**: a capacitor from `SQUARE_A` to B's comparator threshold
  (`VTH_B`). A's pulse lifts the threshold for about a microsecond, and B's
  comparator trips if its ramp is already close enough.

A core's own reset, for scale: `SQUARE` is high for 1.15 us at 11 V, the
ramp is back within 10 % of the top after 1 us, and the reset driver stays
saturated for about 6 us, which is the dead time of step 2.

B runs at a fixed ratio above A. A synced slave repeats one phase at every
master reset, so one ratio shows one point of what a reset does; the ratio
is swept (1.04 to 1.97) and the events of all ratios are gathered per
network and value. An event is B's phase 1 us before A's ramp turns and
12 us after it, with B's own run over that time taken out. Phase is 0 just
after a reset and 1 at B's own.

## 2. What a master reset does to the slave

### 2.1 Through the threshold: a window

Master at A4 unless noted; 52 to 208 events per row.

| C | The slave is left alone up to | It resets from | Window (share of the ramp) | 90 % of the reset after |
|---:|---:|---:|---:|---:|
| 1.0 pF | 0.873 | 0.879 | 0.12 | 1.5 us |
| 1.5 pF | 0.776 | 0.829 | 0.17 to 0.22 | 1.6 us |
| 2.2 pF | 0.713 | 0.761 | 0.24 to 0.29 | 1.5 us |
| 3.3 pF | 0.637 | 0.693 | 0.31 to 0.36 | 1.5 us |
| 3.3 pF, master A2 | 0.627 | 0.717 | 0.28 to 0.37 | 1.5 us |
| 3.3 pF, master A6 | 0.560 | 0.676 | 0.32 to 0.44 | 1.6 us |
| 4.7 pF | 0.497 | 0.565 | 0.44 to 0.50 | 1.6 us |
| 6.8 pF | 0.424 | 0.424 | 0.58 | 1.5 us |
| 10 pF | 0.272 | 0.283 | 0.72 | 1.9 us |
| 15 pF | 0.099 | 0.120 | 0.88 to 0.90 | 3.1 us |
| 22, 47, 100 pF | | every phase (from 0.04) | 1 | 3.9 to 4.0 us |

- **All or nothing.** In every one of the 1,749 events the slave either
  stayed where it was (moved by 0.001 of a turn at most) or went to the top
  of its ramp (0.000 left). There is no partial reset.
- **A sharp edge.** The latest phase left alone and the earliest reset are
  0.006 apart at 1 pF and 0.011 at 10 pF; the wider brackets in the table
  are gaps in the phases sampled, not a soft edge.
- **The capacitor sets the window**, close to `1 - exp(-C / 7.8 pF)` (within
  0.04 up to 15 pF). From 22 pF on the window is the whole ramp: hard sync.
- **The same at three master pitches** within the brackets (A2, A4, A6 at
  3.3 pF).
- **The reset is the slave's own.** Its comparator fires and its own reset
  switch does the work: from 5.04 V it is at 7.31 V after 1 us and at 7.94 V
  (the top is 7.95) after 5 us.
- **The master is not disturbed**: 439.989 Hz free, 439.989 Hz with 1 and
  4.7 pF, 440.015 Hz with 100 pF (0.1 cent).

### 2.2 Through the reset driver: a switch

Master at A4 unless noted, 130 events per row (39 at A2, 87 at A6).

| R | What the slave loses |
|---:|---|
| 270 k, 330 k | everything, from every phase; 90 % after 1.8 us |
| 100 k, 220 k | everything, at the one phase tried (0.38; 22 events each) |
| 390 k | 0.68 to 0.85 of a turn (`0.269 + 0.606 x phase`), which is everything for most phases |
| 470 k | 0.14 to 0.28 of a turn (`0.158 + 0.123 x phase`, within 0.04) |
| 470 k, master A2 | 0.13 to 0.27 (`0.151 + 0.130 x phase`) |
| 470 k, master A6 | 0.18 to 0.28 (`0.167 + 0.116 x phase`) |
| 560 k | 0.01 to 0.03 of a turn |
| 680 k and above | nothing |

- **Below about 350 k it is hard sync**, the same as the threshold network
  with a large capacitor.
- **A partial reset exists only between 390 k and 560 k**, less than a
  factor of 1.5 in resistance. There the slave loses a nearly fixed piece
  of its ramp (the driver conducts for a fixed time), a little more from a
  late phase; it does not lose a share of what it has.
- A pot across that band would be a switch with a touchy middle, so this
  network is read as hard sync only.

## 3. Against the core today

`ml_source_substep` (`src/ma_osc_kernel.h`) takes one strength, `sync`
(`sync_amount` scaled down by up to 75 % by `sync_softness`), and at every
master wrap moves VCO2 to `phase x (1 - sync)`: every master cycle resets,
part of the way.

| | The core today | The circuit |
|---|---|---|
| strength 1 | to the top, every master cycle | the same (either network) |
| strength between | every master cycle, a share of the way | to the top or not at all, by the slave's phase (threshold network) |
| a share of the way | the rule | only as a fixed piece, in one narrow band of one network |
| time of the step | smoothed over the core's edge zone, two output samples at 48 kHz | 90 % in 1.5 us, a fourteenth of an output sample |

What that does to the pitch heard: the slave alone above a master at A2
(110 Hz), the rate at which its waveform repeats in the listening set of
section 5 (autocorrelation, to the hertz; "none" is no clean period).

| Slave at | free | hard, SPICE | hard, core | window 0.47, SPICE | core at 0.47 | window 0.26, SPICE | core at 0.26 |
|---|---:|---:|---:|---:|---:|---:|---:|
| a fourth | 147 | 110 | 110 | 55 | 110 | 147 | none |
| a fifth | 165 | 110 | 110 | 165 | none | 165 | 37 |
| an octave | 220 | 220 | 221 | 220 | 221 | 220 | 220 |
| a tenth | 277 | 110 | 110 | 37 | 28 | 277 | 277 |
| a twelfth | 330 | 331 | 331 | 331 | 331 | 331 | 331 |
| two octaves | 440 | 440 | 440 | 440 | 440 | 440 | 440 |

- **Hard sync is the same thing in both**: the master's pitch, the slave's
  interval as timbre.
- **A window is not a weaker hard sync.** At 0.26 the slave keeps its own
  pitch at every step; at 0.47 it keeps it at the fifth, and at the fourth
  and the tenth it falls into a pattern two or three master cycles long,
  an octave or a twelfth below the master. The core's partial reset keeps
  the master's period or has none.

## 4. Proposal for the core (declined)

Operator 2026-11-16: not taken. The core's sync stays as it was before V5,
whatever `vco.analog` is, and this study stands as a finding. The proposal
is kept below as it was put.

| ID | Proposal |
| --- | --- |
| VD28 | **Under `vco.analog`, a sync event is all or nothing.** The slave resets to the top when the master's reset finds it in the last `w` of its ramp, and is left alone otherwise. |
| VD29 | **The window is the strength the core already has**: `w = sync`. One is hard sync and zero is none, as today; in between the two controls open the window where they used to shorten the reset. The capacitor's own taper (`1 - exp(-C / 7.8 pF)`) is not taken; the control is the window itself. |
| VD30 | **The amount blends where the reset lands.** With amount `a`: inside the window the slave keeps `(1 - a)(1 - sync)` of its phase, outside it `1 - (1 - a) sync`. Zero is today's reset bit for bit, one is the circuit; in between it has no circuit meaning, as with the other shapes. A voice at amount one outside the window takes no reset at all (no step, no re-seeded triangle). |
| VD31 | **The reset's own time is not modelled.** 1.5 us is 0.6 of an 8x substep; the core's step stays as it is. |
| VD32 | **The reset-driver network adds nothing**: its hard sync is the window at one, and its narrow partial band is not taken. |
| VD33 | **No new control and no new file version.** `source.sync` and `source.sync_softness` keep their keys and ranges. |

What it would change: nothing for a patch with `vco.analog` at zero, so
every factory patch but Kolo, and Kolo has no sync. The Ruin macro's sync
bias goes through the same strength, so under the law it opens a window
too.

What it would cost: under the law, a sync strength between zero and one
stops being a timbre control. Today it bends the slave's waveform a little
every master cycle; as a window it either leaves the slave alone or locks
it, and the sound in between is the table of section 3. A patch that wants
today's partial sync and the circuit's oscillators could not have both.
Keeping today's sync whatever the amount, and reading this study as a
finding only, is the other choice.

## 5. Listening

`build/vco/vco5/listen/` (not in the repository), 48 kHz mono. The slave
alone above a silent master at A2, stepping through a fourth, a fifth, an
octave, a tenth, a twelfth and two octaves, 0.7 s each. (The runs have a
seventh step, two octaves and a third, which the core's interval cannot
reach; the files leave it out.)

- `sync-hard-AB.wav`: SPICE with 100 pF, then the core at sync 1.
- `sync-window-47-AB.wav`: SPICE with 4.7 pF (window 0.47), then the core
  at sync 0.47.
- `sync-window-26-AB.wav`: SPICE with 2.2 pF (window 0.26), then the core
  at sync 0.26.
- `spice-hard-two-networks.wav`: SPICE with 100 pF, then with 100 k into the
  reset driver.
- `spice-reset-470k.wav`: SPICE with 470 k into the reset driver, the
  partial band.

## 6. Method and limits

- Traces at 20 MHz with a 50 ns step limit for the events (a reset is over
  in a microsecond); the listening runs at 1.92 MHz with a 0.5 us limit,
  like the other studies.
- The slave's phase is read from its ramp voltage between the run's lowest
  point and its 99.9th percentile; the ramp is straight to -42 dB (step 2).
- Ideal rails, nominal parts apart from the two boards' spread, 25 C, no
  noise in the models. Neither network was built or measured.
- The window edge is bracketed by the phases the ratio sweep happened to
  give, not searched for.
- Only a saw core was synced. The triangle core (B1) reverses its ramp
  rather than resetting; what sync does to it was not simulated.
