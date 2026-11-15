# V4 in SPICE: two VCO-2 cores on one supply

Date: 2026-11-14. Tooling `driver/spice/vco/vco4.py` (bench and analysis),
`vco4_render.py` (listening); every number here is from
`build/vco/vco4/*.json` and `fits.json`. What the core takes from it is in
`docs/ma-vco-plan.md` (V4).

## 1. Bench

Two VCO-2 cores (`vco2.cir`), boards A and B with their step-2 trims, share
the 12 V and 8 V rails. Each rail comes from an ideal source through a series
resistance `Rs` into a bus decoupled by `Cb`; the sources sit above the bus
by the pair's mean draw (12.0 mA from VCC, 21 uA from VTOP), so the bus is
the 12 V and 8 V the boards were trimmed on. CV and the two trims stay ideal
(a separate precision reference). Core B is detuned 8 cents, so the phase
difference `psi = q phi_B - p phi_A` walks round a few times; the rate it
walks at against `psi` is the coupling, `d psi / dt = detune + G(psi)`.

## 2. What the pair does

Lock half-range `L` (Hz) and its share of the lower core's frequency, from a
sawtooth fitted to `G`:

| supply | ratio | note | L (Hz) | L / f | cents (unison) | rail ripple VCC / VTOP |
|---|---|---|---:|---:|---:|---:|
| ideal | 1:1 | A4 | 0.001 | 2e-6 | 0.004 | 0.01 / 0.04 mV |
| 1 ohm, 10 uF | 1:1 | C2 | 0.0070 | 1.07e-4 | 0.18 | 4.3 / 2.1 mV |
| 1 ohm, 10 uF | 1:1 | A4 | 0.0473 | 1.08e-4 | 0.19 | 4.2 / 3.4 mV |
| 1 ohm, 10 uF | 1:1 | C6 | 0.1097 | 1.05e-4 | 0.18 | 4.1 / 3.4 mV |
| 10 ohm, 10 uF | 1:1 | A4 | 0.4427 | 1.01e-3 | 1.7 | 34.6 / 4.5 mV |
| 10 ohm, 100 nF | 1:1 | C2 | 0.0689 | 1.05e-3 | 1.8 | 48.0 / 134 mV |
| 10 ohm, 100 nF | 1:1 | A4 | 0.4599 | 1.04e-3 | 1.8 | 48.7 / 163 mV |
| 10 ohm, 100 nF | 1:1 | C6 | 1.0782 | 1.03e-3 | 1.8 | 50.8 / 178 mV |
| 47 ohm, 100 nF | 1:1 | A4 | 4.39 | 9.8e-3 | 17 | 185 / 248 mV |
| 1 ohm, 10 uF | 2:1 | A4 | 0.0711 | 1.62e-4 | | 4.1 / 3.4 mV |
| 10 ohm, 100 nF | 2:1 | A4 | 0.6868 | 1.56e-3 | | 49.3 / 172 mV |
| 1 ohm, 10 uF | 3:2 | A4 | 0.0581 | 1.32e-4 | | 4.2 / 3.4 mV |
| 10 ohm, 100 nF | 3:2 | A4 | 0.5791 | 1.32e-3 | | 48.0 / 152 mV |

- **The coupling is a sawtooth, not a sine.** `G` rises straight across the
  turn and drops at one point: the pull is strongest just after the other
  core's reset and fades to nothing half a turn later. The fit leaves 0 to
  3 % of `L` (the fifth 6 to 9 %, binned).
- **It locks with the resets together.** In the core's phase (0 at the
  reset; SPICE times cycles mid-ramp, which moves the octave and the fifth by
  half a turn) the drop is at `psi = 0` within 0.03 for every case. Within
  `L` of an exact ratio the pair holds there; a little outside it the beat
  crawls near the resets and hurries between them.
- **It is constant in cents** from C2 to C6 for a given supply: the share of
  the period the reset disturbs does not change.
- **The supply sets it.** A well-decoupled board (1 ohm, 10 uF) locks within
  0.19 cents at unison; an RC-filtered rail (10 ohm, 10 uF) or a soft one
  (10 ohm, 100 nF) within 1.7 to 1.8 cents, ten times as far. At 47 ohm the
  rail no longer holds the boards' trim (A runs 7 Hz sharp): that is a
  broken supply, not a softer one, and is not used.
- **The octave and the fifth lock too**, a little more strongly than unison
  for the same supply.

## 2b. The other simple ratios (V4b)

Every ratio with p + q <= 11 that an interval of the core sits near, at A3,
core B 8 cents above the simple ratio, both supplies (`vco4_fit.py`,
`fits4.json`). For p:q the rate is taken over p of A's cycles, one turn of
the pattern, so that B is back where it was in its own cycle and the
interpolation of its phase between resets cancels (per cycle it does not:
a first pass gave residuals up to twice L).

| interval | ratio | ET minus the ratio (cents) | L / f_low, good board | soft rail | jump (good / soft) | residual over L |
|---|---|---:|---:|---:|---|---|
| +3 | 6:5 | -15.6 | 9.792e-05 | 8.470e-04 | 0.06 / 0.04 | 0.28 / 0.64 |
| +4 | 5:4 | +13.7 | 1.160e-04 | 9.395e-04 | 0.00 / 0.98 | 0.11 / 0.36 |
| +5 | 4:3 | +2.0 | 1.232e-04 | 1.218e-03 | 0.02 / 0.06 | 0.08 / 0.19 |
| +9 | 5:3 | +15.6 | 1.456e-04 | 1.349e-03 | 0.04 / 0.98 | 0.29 / 0.10 |
| +10 | 7:4 | +31.2 | 1.228e-04 | 1.216e-03 | 0.02 / 0.98 | 0.19 / 0.15 |
| +16 | 5:2 | +13.7 | 1.733e-04 | 1.582e-03 | 0.02 / 0.94 | 0.09 / 0.17 |
| +17 | 8:3 | +2.0 | 1.302e-04 | 8.071e-04 | 0.02 / 0.17 | 0.34 / 0.67 |
| +19 | 3:1 | -2.0 | 2.108e-04 | 1.963e-03 | 0.98 / 0.02 | 0.01 / 0.11 |
| +24 | 4:1 | +0.0 | 2.689e-04 | 2.444e-03 | 0.96 / 0.92 | 0.01 / 0.10 |

- **Every one locks with the resets together** (the jump within 0.06 of 0;
  8:3 on the soft rail, 0.17, is the noisiest fit, 0.67 of L).
- **The strength barely falls with the order**: 0.8 to 2.4e-3 of the lower
  core on the soft rail, against 1.0e-3 at unison; the twelfth and the two
  octaves pull hardest, the minor third and the eleventh least.
- **They lock at the simple ratio, not at the equal-tempered interval.** On
  the soft rail the lock range in cents of the upper core is a fraction of
  a cent for the thirds and sixths, so an equal-tempered third (14 to 16
  cents off) never locks, only beats a little unevenly; the fine tuning
  brings it there. The fourth and the fifth are 2 cents off: on the soft
  rail they lock once tuned to within about half a cent (fourth) or
  three quarters of a cent (fifth) of just.
- These were simulated at A3 only; unison showed the share constant from C2
  to C6, and the core assumes the same for every ratio.

## 3. Method and limits

- ngspice-47, Gear, reltol 1e-5, 0.5 us maximum step (or period / 4000),
  linearized to 1.92 MHz; 1.0 to 6.0 s per case after 50 ms of settling.
- Only the rails couple here: ground, CV and the trims are ideal. A board
  that derives its pitch trim from the same rail would couple more.
- Two boards, nominal temperature, no noise: nothing here about drift or
  jitter, and how the pull is shared between the two cores is not
  observable from the phase difference; the core splits it evenly.
- Unison, the octave and the fifth at A4 (unison also at C2 and C6); the
  other simple ratios at A3, 1.5 s each (8 to 12 turns of the pattern).
  Ratios with p + q above 11 (seconds, sevenths, the tritone) were not
  simulated and do not couple in the core.
