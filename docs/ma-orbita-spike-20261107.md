# Orbita offline spike — resonance capture, escape and breakup, 2026-11-07

**Question.** Is the Orbita dynamic deterministic, bounded and audible, and
where could it enter the Mamut voice? In this dynamic, frequency ratios drift
toward small-integer ratios, are captured there, escape under perturbation and
break up into a ring pair. The backlog lists Orbita as a post-MA5 candidate
(MA5 promotion gate 1: "VCO2/AUX resonance capture, hysteretic escape and
breakup, offline before engine integration"). The operator pulled this
offline experiment forward on 2026-11-07.

**Answer.**
- The primitive works as specified. It is deterministic (two-run and
  GCC/Clang PCM signatures are equal), bounded under a 200,000-tick hostile
  stress, and captures land bit-exactly on the table value.
- On the real voice, one moon driving the VCO2/VCO1 ratio through the public
  setter is enough to hear a detuned fifth beat slower and lock. At
  dissipation 0 it is PCM-identical to no Orbita.
- The donor design has three properties that integration has to decide on:
  - the kick scale is absolute, so moons at 7/2 and above can never escape;
  - escape from a strong entry is undone within tens of milliseconds;
  - there are gaps in window coverage where a moon stays free forever.
- The additive moon bank costs about 145–200 ns per sample per voice on this
  host. The VCO2 insertion costs one control tick per 32 samples.
- No engine code changed.

## Method

`driver/spike_ma_orbita.c` is hosted only. It uses the core through its
public API and is not part of `make test`.

- `make spike-ma-orbita` runs the `--check` table.
- `make audition-ma-orbita` writes the WAVs and `metrics.txt` to
  `build/ma-orbita-spike-20261107/`.

**Part A** ports the donor's SET5-1 v0.1 primitive
(`mamut-sint-sw/docs/EPM1_BACKLOG_SET5_ORBITA_MOZAIK_KOSAVA.md`, read at donor
`d767291`; the file last changed in `db61200`).
It has five moons, each a ratio to the fundamental with three always-running
sines (centre plus ring pair). The control tick is every 32 samples, and the
ratio is linearly interpolated per sample. Every render adds a planet sine at
f0 (`.5 planet + .5 moons`). The donor primitive is moons only. The planet is
the reference that makes capture audible as beating.

**Part B** puts one moon on the MA voice. Starting from the Lead or Tepih
patch, sync and cross-mod are set to 0, VCO2 level to `.80` and amp sustain
to `.80`. Every 32 samples the spike ticks the moon and calls
`ma_synth_set_vco2` with the resulting fine tune. The core applies the
integer interval unsmoothed, so the interval is fixed per scenario at the
semitone nearest the start, and only the 6 ms-smoothed fine (±50 cents)
moves. Every scenario stays inside that range (`fine-clamps 0`). The
cents mapping uses hosted `log2f`/`exp2f`.

Environment: x86-64 Fedora 44, GCC 16.2.1, Clang 22.1.8, tree at `54f4a17`
plus this spike (spike source SHA256 `21b59e7d…`, GCC binary `9f01f2b5…`).
`-std=c23 -O2 -ffp-contract=off`.

## Constants

`[DONOR]` is taken unchanged from SET5-1. `[DECISION]` was decided in this
spike, where the donor leaves a gap.

| Constant | Value | Tag |
| --- | --- | --- |
| Resonance table | 1/1 5/4 4/3 3/2 8/5 5/3 7/4 2/1 9/4 7/3 5/2 8/3 3/1 7/2 4/1, weight `1/(pq)` | [DONOR] |
| Tick | 32 samples | [DONOR] |
| Attraction window | `|r−v|/v < .06`, nearest entry by relative distance | [DONOR] |
| Force | `r −= sign · d² · .12 · w · (r − v)` per tick | [DONOR] |
| Kirkwood | entry repels when `w < .12 · kirkwood` | [DONOR] |
| Capture / exit | `< .002` for 8 ticks / `> .015` | [DONOR] |
| Ratio clamp | `[0.5, 6]` | [DONOR] |
| Home | stratified seeded, one stratum of `[1.2, 3.8]` per moon, kept `≥ 1e-4` relative from any entry | span [DONOR], strata [DECISION] |
| Captured motion | `r += .25 (v − r)` per tick, exact snap within `1e-6 v` | [DECISION] |
| Repelling capture | released, nudged `±1e-3 v` toward the home side | [DECISION] |
| Kick | `Δr = .05 · amount · u`, `u ∈ [−1, 1)` seeded per moon | [DONOR] |
| Roche | `roche_k = .55 + .08k`, smoothstep over `.15`, ring attack 30 ms / release 300 ms | [DONOR] |
| Ring pair | `f0·r·(1 ± δ)`, `δ = .02 + .10 ring`, gains `1 − 2/3 ring` and `ring/3` each | [DONOR] |
| Moon amplitude | `1/(k+1)`, normalized | [DONOR] |
| Per-sample ratio | linear interpolation across the tick | [DECISION] |
| Nyquist guard | per partial, linear fade from `.40 fs` to zero at `.45 fs` | [DECISION] |
| Seed | `0x4f52424954413031` | [DECISION] |

## Checks

All pass on GCC, on Clang and under Clang ASan/UBSan/float-cast-overflow:

| Check | Result |
| --- | --- |
| Capture time from 3/2 +3 % strictly falls with dissipation | ticks 3391 / 852 / 382 / 218 / 142 at d = .2 / .4 / .6 / .8 / 1 |
| Captured ratio equals the table value | bit-exact (`1.5f`, `2.0f`) |
| Dissipation 0 | never moves, never captures |
| +1.4 % kick on a 2/1 capture | stays captured, snaps back bit-exactly |
| +1.6 % kick | escapes; recaptured after 60 ticks (40 ms) at d = .8 |
| Kirkwood 0 / 1 on 5/4 | attracts / repels |
| Capture on an entry that turns repulsive | released and ejected beyond 1 % |
| NaN/±Inf/out-of-range controls | sanitize to 0 or clamp to [0, 1] |
| Non-finite moon state | resets to its seeded home |
| 200,000 ticks of random controls, NaN and kicks | finite, ratio in [0.5, 6], ring in [0, 1], `|y| ≤ 1` (1429 captures, 1399 escapes, 25 releases) |
| Partials near Nyquist (f0 = 6 kHz) | fade in the guard band, output finite |
| Same seed and script | identical PCM and identical state (`memcmp`) |
| VCO2 moon at d = 0 vs no Orbita | `711fc4506158ab29` both |

## Part A results

f0 220 Hz, 48 kHz, mono. GCC and Clang signatures are equal for every
scenario, and the two `capture_slow` runs are equal.

| Scenario | Script | FNV-64 | RMS | Peak |
| --- | --- | --- | ---: | ---: |
| `capture_slow` | d = .25, 24 s | `0687fbedf28f2224` | .4000 | .9358 |
| `capture_fast` | d = .90, 8 s | `ed0575e15a8905af` | .3998 | .8776 |
| `escape_kick` | d = .60; kicks .1…1.0 every 1.5 s from 4 s | `4057d1f19d0ad089` | .4000 | .9777 |
| `kirkwood_sweep` | d = .60; kirkwood 0→1 over 2–16 s | `cb739d96453f5664` | .4000 | .9614 |
| `roche_sweep` | d = .60; proximity 0→1 over 3–8 s, hold, 1→0 over 10–15 s | `97c834ddd7308521` | .3903 | .9556 |

The seeded homes are 1.53904, 1.77500, 2.53115, 3.24625 and 3.45311.

| Moon | Home | `capture_slow` | `capture_fast` | After `kirkwood_sweep` |
| --- | ---: | --- | --- | --- |
| 0 | 1.539 | 3/2 at 1.373 s | 3/2 at 0.110 s | 3/2 (strong, stays) |
| 1 | 1.775 | 7/4 at 4.900 s | 7/4 at 0.382 s | released, parked at 1.8551 |
| 2 | 2.531 | 5/2 at 1.631 s | 5/2 at 0.130 s | released, parked at 2.5808 |
| 3 | 3.246 | free | free | free |
| 4 | 3.453 | 7/2 at 2.371 s | 7/2 at 0.187 s | released, parked at 3.2900 |

- `escape_kick`: only kicks 7 and 10 (amount .7 and 1.0) caused an escape,
  one moon each. Moon 3 was kicked into the 3/1 window and captured there at
  13.161 s.
- `roche_sweep`: moons 0–3 reach ring 1.0. Moon 4 peaks at .9514, because
  `roche_k = .87` plus the `.15` width ends above proximity 1.

## Part B results

MIDI note 48, velocity 100, 48 kHz stereo. Note-off comes 1 s before the end.
Every twin render is equal, and the GCC and Clang signatures are equal. These
renders peak near −26 dBFS (master level from the patch), so raise the
playback gain.

| Scenario | Patch, d, start | FNV-64 | Final | First capture |
| --- | --- | --- | --- | --- |
| `vco2_fifth_static` | Lead, 0, 3/2 +30 c | `f435494948253f19` | +731.955 c (unchanged) | — |
| `vco2_fifth_capture_slow` | Lead, .20, 3/2 +30 c | `8dc40ae6069e2b2d` | 701.955 c | 1.811 s |
| `vco2_fifth_capture_fast` | Lead, .60, 3/2 +30 c | `dedad54665012d29` | 701.955 c | 0.205 s |
| `vco2_fifth_kicks` | Lead, .35; +1 % at 5 s, +2.5 % at 9 s, −2.5 % at 14 s | `7233abb77afe2ab5` | 701.955 c | 0.594 s; 3 captures, 2 escapes |
| `vco2_unison_tepih` | Tepih, .10, its own +7 c | `7f0405e83848f82d` | 0.000 c | 0.397 s |

In `vco2_fifth_kicks` the +1 % kick bends and returns inside the detent. Both
2.5 % kicks escape and glide back.

## Findings for integration

1. **The kick scale is absolute.** `|Δr| ≤ .05 · amount` in ratio units,
   while the exit threshold is relative (`.015 v`).
   - At 7/2 and 4/1 an escape needs `Δr > .0525` and `.06`, which no kick can
     reach. At 5/2 it needs `amount · |u| > .75`.
   - This is why `escape_kick` produced only 2 escapes in 50 moon-kicks.
   - A relative kick (`r *= 1 + .05 · amount · u`) makes escape equally
     reachable at every ratio. This is a proposed change to the donor
     semantics and needs an operator decision.
2. **Escape is short at useful dissipation.** A moon that escapes a strong
   entry is still inside its 6 % window, so the same attractor recaptures
   it, in 40 ms at d = .8 on 2/1. Escape only lasts audibly at low
   dissipation or on weak entries. Making escape last would need a model
   change, such as a refractory time or a wider kick.
3. **Window coverage has gaps.** Over the home span `[1.2, 3.8]` the free
   ranges are 1.855–1.880, 3.18–3.29 and 3.71–3.76, about 7 % of the span.
   A moon homed there stays free until kicked (moon 3).
4. **Kirkwood parks moons where the force stops.** Ejected moons stop at a
   window edge (1.8551 = 1.06 · 7/4, 3.2900 = .94 · 7/2), or at the
   nearest-entry boundary between two repelling entries (2.5808, between 5/2
   and 8/3). The second resting point comes from the nearest-entry rule; the
   donor did not specify it.
5. **Cost.**
   - Moon bank: 143–200 ns/sample per voice in two noisy runs. That is
     15 `tw_sin_turns` per sample plus the planet sine, with no optimization
     and no host isolation. This is a rough single-host figure, not an
     MA3-PERF measurement.
   - For 8 slots that is roughly 0.5–0.7 M cycles per 128-frame block at
     ~3.3 GHz, against the current worst case of 1.737 M.
   - VCO2 insertion: one `moon_tick`, one `log2f` and one setter call per
     32 samples. Not measured; expected to be negligible.
6. **The VCO2 insertion needs a continuous ratio path in the core.** The
   public setter moves the VCO2 ratio only within ±50 cents of a fixed
   semitone, because the integer interval is unsmoothed. The attraction
   window is ±6 % (about ±100 cents), so integration needs an internal
   continuous VCO2 ratio input instead of the setter. Breakup (ring pair) has
   no counterpart on VCO2 alone.
7. **Unison capture removes Tepih's beating.** Tepih's +7 cents locks to
   exact 1/1 in 0.4 s at d = .1, which removes the slow beating that is part
   of the pad. Whether 1/1 should attract on VCO2 is a listening question.

## Insertion options for v0.2

| Option | Fit with the backlog | Cost | Breakup |
| --- | --- | --- | --- |
| A. VCO2/VCO1 ratio is the moon | matches "VCO2/AUX resonance capture"; no new source | negligible | none on VCO2 alone |
| B. Mozaik/AUX pitch ratio is the moon | matches; Mozaik stays the third source | negligible | none |
| C. Donor moon bank as a voice layer | an additive source; conflicts with "no additional spectral/additive sources through MA5" | 0.5–0.7 M cycles per 8-slot block | yes |

Option A is the smallest step that uses the existing spine. Option C keeps the
donor's full concept, but it needs an operator decision against the
out-of-scope list and its own performance budget.

## Nearest relatives

Established prior art, not claimed here:
- mode locking and Arnold tongues of the circle map;
- injection locking of oscillators (Adler 1946);
- Kuramoto synchronization;
- adaptive tuning that moves partials toward consonance (Sethares, *Tuning,
  Timbre, Spectrum, Scale*, and his 2002 adaptive tuning work).

The orbital vocabulary (capture, Kirkwood gaps, Roche limit) is a metaphor
for the control semantics, not a physical model. What this line would add is
narrow: capture as a hysteretic detent with exact lock, used as a performance
control on an analog-voice interval. No novelty claim is made at spike stage.

## Listening copies

`build/ma-orbita-spike-20261107/` holds the WAVs (`a_*` for the moon bank,
`b_*` for the MA voice) and `metrics.txt`.
