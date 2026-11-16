# The circuit's frame in the lanes, 2026-11-15

**Result.** The circuit-derived oscillators (`docs/ma-vco-plan.md`, V1 to V4)
ran their once-a-frame part per voice, in scalar code, also when the pool
rendered eight voices per vector: the pitch law for both cores, the shapes at
each core's pitch and the coupling. That part is now one kernel, run at width
one by the one-voice tick and at width eight by the lanes. With the law on,
8-slot blocks are 1.25–1.30x faster than `1e94fa1` on the vector engine; what
the law adds to a block falls from about 0.58 M cycles to about 0.23 M. With
the law off the vector engine is 1.07–1.10x faster and the scalar engine is
unchanged (0.997–1.001x). The PCM is identical in all 40 cases. This is a
hosted result, not a live acceptance.

## Before

Measured on `1e94fa1` before the change (a one-off probe, not kept): the law
at full amount added 0.58 M cycles to a 128-frame block of 8 lane voices
(Tepih 1.40 to 1.99 M). Front end part A went from about 135 to about 590
cycles per voice and frame, which is about 80 % of the addition: each frame
recomputed two pitch laws, six exponentials (two rises, four ramp bows) and
a 23-row search for the coupling row, per voice. No factory patch sets
`vco.analog`, so `bench_ma3_perf` never ran this path.

## Change

- **`src/ma_vco_kernel.h`** (new). The pitch law (`ml_vco_law_hz`), the
  shapes at a pitch (`ml_vco_rise`, `ml_vco_ramp_bow`, `ml_vco_swell`, their
  exponential) and one frame of both cores with the coupling
  (`ml_vco_frame_of`), written once against `ma_lane_ops.h`. Each lane
  performs the operations of the former scalar code in the same order. A
  lane with the amount at zero keeps its pitch bit for bit and gets no
  shape.
- **`src/ma_lane_ops.h`.** `ml_exp2_int`: a power of two from a lane's whole
  exponent.
- **One voice (`src/ma_osc.c`).** `ma_voice_vco_frame` runs the kernel at
  width one and leaves the frame in the synth (`vco2_bend`, `vco_skew` and
  the fields after it), as before. `ma_voice_vco_law_hz`, the referee's
  hook, is the kernel's law. The scalar functions in `src/ma_voice.c` and
  `vco_law_hz` in `src/ma_voice_impl.h` are gone.
- **Front end part A (`src/ma_voice.c`).** `ma_voice_front_a_pitch` is part A
  up to the pitch the law starts from; it leaves the bend and LFO sum and
  the detune in the front. `ma_voice_front_a` is that plus the frame for one
  voice, so its callers (the scalar tick, the Patchbench probe) see no
  change.
- **Eight voices (`src/ma_lanes.c`).** The lane tick runs
  `ma_voice_front_a_pitch` per lane, then `ml_front_vco` once: the inputs go
  into registers (`ML_PICK_PS`), the law's pitch is written back to each
  voice's front, and the shapes, the coupling and VCO2's ratio are stored
  straight into the front inputs that `ml_front_math` loads.
  `ml_front_gather` no longer copies them from the synth. A bank with the
  law off in every voice skips the kernel. Each voice's synth still gets
  the frame the one-voice tick would have left in it (see the correction
  below).
- **`VCO_COUPLE`** is indexed by VCO2's interval (49 rows, zeros where
  nothing couples), read through `vco_couple_for`.
- **`driver/bench_ma3_perf.c`.** `MA3_PERF_ANALOG=0|1` (default 0, output
  unchanged): 1 runs every case with `vco_analog` 1 and `vco_supply` 1.

## Gates

- **`test/ma_front_lanes.c`** (`check-ma-front-lanes`) now also compares
  `ml_front_vco` with `ma_voice_vco_frame`, bit for bit: the pitch after the
  law and, through the source frame, both skews, both swells, the four ramp
  bows, the saw's bow, the buffer bend and the coupling. Of 400,000
  lane-frames the law is on in 199,574 (149,576 with a card, 49,998
  without, which a pool voice never is; 75,147 coupling) and off in
  200,426. One bank in eight has it off in every voice, one in eight on in
  every voice, the rest are mixed. Four deliberate mutations of the lane
  adapter were caught: the card flag ignored, VCO2's dead time taken from
  VCO1, the rise and fall bows swapped, a law-off voice's ratio dropped in a
  mixed bank.
- **GCC `make all test`, `make test-clang`:** 111,378 core checks, every
  pinned signature, the pool's engine twin over 223,603 frames,
  `check-ma-vco` 68, `check-ma-vco-shape` 68, `check-ma-vco-couple` 31, with
  0 warnings. The freestanding symbol audit passes.
- **`make sanitize`** (ASan, UBSan, float-cast-overflow): no report.

## Measurement

Run: `build/ma-vco-lanes-ab-20261115-01/` (`run-ab.sh`, `ab_table.py`,
`ab-summary.md`, `host.txt`, `source-rev.txt`).

| Binary | Source | Bench SHA256 |
|---|---|---|
| A | `1e94fa1`, with this change's `driver/bench_ma3_perf.c` | `ef2de203…` |
| B | working tree | `c6c8feab…` |

The protocol is the earlier one: ten patch/BCS cases, 8 held slots, wet
effects, 128 frames, 256 + 2,048 blocks × 2 passes per process, three
epochs in A/B, B/A, A/B order, counters required, both engines; here each
with the law off and on (240 processes). CPU 2, no browser or editor
running, load 0.5 at the start, sleep inhibited, `balanced` profile.

Vector engine, median cycles per block (M):

| Case | Law off: A | B | A/B | Law on: A | B | A/B | The law adds: A | B |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Tepih / BCS off | 1.447 | 1.322 | 1.095x | 2.025 | 1.555 | 1.302x | 0.578 | 0.233 |
| Tepih / BCS on | 1.669 | 1.544 | 1.081x | 2.269 | 1.808 | 1.255x | 0.600 | 0.264 |
| Lead / BCS off | 1.518 | 1.399 | 1.085x | 2.115 | 1.655 | 1.278x | 0.597 | 0.256 |
| Lead / BCS on | 1.634 | 1.507 | 1.084x | 2.232 | 1.769 | 1.261x | 0.598 | 0.262 |
| Raster / BCS off | 1.454 | 1.334 | 1.090x | 2.067 | 1.623 | 1.274x | 0.613 | 0.289 |
| Raster / BCS on | 1.570 | 1.444 | 1.087x | 2.194 | 1.736 | 1.264x | 0.624 | 0.292 |
| Prizma / BCS off | 1.731 | 1.604 | 1.079x | 2.260 | 1.810 | 1.248x | 0.529 | 0.206 |
| Prizma / BCS on | 1.843 | 1.712 | 1.077x | 2.397 | 1.923 | 1.246x | 0.554 | 0.211 |
| Granica / BCS off | 1.700 | 1.574 | 1.080x | 2.235 | 1.778 | 1.257x | 0.535 | 0.204 |
| Granica / BCS on | 1.814 | 1.689 | 1.074x | 2.356 | 1.892 | 1.245x | 0.542 | 0.203 |

| Engine, law | Geometric mean A/B | Range | B/A instructions | PCM A = B |
|---|---:|---:|---:|---|
| vector, off | 1.083x | 1.074–1.095x | 0.904–0.919 | 10 of 10 |
| vector, on | 1.263x | 1.245–1.302x | 0.761–0.788 | 10 of 10 |
| scalar, off | 0.999x | 0.997–1.001x | 1.001–1.002 | 10 of 10 |
| scalar, on | 1.017x | 1.012–1.022x | 0.980–0.984 | 10 of 10 |

Per-epoch medians agree within 1.5 % for B in every vector case (A: within
3.0 %). On the vector engine B had no CPU or wall deadline miss in 245,760
measured blocks (law off and on); its largest block took 1.049 ms of the
2.667 ms period.

## Reading

- **The law's own cost.** What the law adds to a vector block falls from
  0.53–0.62 M to 0.20–0.29 M cycles. The rest is the eight-wide frame
  itself (six exponentials, two laws and their divisions per bank) and the
  kernel's per-substep work when shapes are on: the ramp bows, the buffer
  bend and the coupling nudge.
- **With the law off the vector engine is also faster**, 0.12–0.13 M cycles
  a block. Part A no longer writes twelve zeros into each synth and the
  gather no longer copies fifteen fields per lane and frame; the lane
  inputs keep the zeros they start with.
- **The scalar engine** runs the same kernel at width one. With the law off
  it is inside the refactor gate (median at most +1 %, no case above +2 %:
  the worst is +0.3 %). With the law on it is 1.2–2.2 % faster; the
  coupling row is indexed, not searched.
- **Not measured here.** The shapers' own levels (`tri_sine`, `tri_saw`)
  were at zero, as in every factory patch; the one-off probe before the
  change put both at about 0.15 M cycles a block. Live play was not
  repeated.

## Correction, the same day

As first committed (`ca8f6b4`), the lanes did not write the frame back into
each voice's `ma_synth` (`vco2_bend`, `vco_skew` and the fields after it),
on the grounds that only the one-voice tick reads those. That broke a
contract of the pool: both engines leave the same state in every slot, and
`test/ma_pool.c` compares the slots byte for byte after each render. The
test did not notice, because none of its patches had the law on. PCM was
not affected.

- **Fix.** `ml_front_vco` writes the frame back for every voice with the
  law on. A voice with the law off has its copy at zero already, unless
  the law was on in its last frame; the buffer's amount tells, and then it
  is cleared (`vco_frame_clear`, shared with the one-voice tick).
- **Gate.** The engine twin has two more variants on Kolo, the factory
  patch with the law on: as it is, and at a fifth with partial sync and
  with the law switched off and on again by patch edits. It failed on
  `ca8f6b4` at the slot comparison and passes now: 370,101 frames (223,603
  before), 298 sleeps.
- **Cost.** Per block of 8 voices, against `ca8f6b4`, on the probe used
  before the change: +1.2 % instructions and about +1 % cycles with the law
  on (Tepih 1.518 to 1.535 M, Granica 1.845 to 1.860 M), +0.1 %
  instructions with it off. The A/B table above was not rerun; it is
  about 1 % optimistic for the law-on vector cases.

## Open

- `driver/probe_ma3_stages.c` does not build, here or on `1e94fa1`: it
  still names lane helpers from before the shared kernels
  (`ml_osc_ld`, `ml_sel`, `ml_lt`). It is not part of `make all`.
- The default bench matrix does not run the circuit-derived oscillators;
  `MA3_PERF_ANALOG=1` does. Kolo, the one factory patch that has them on
  (`docs/ma-vco-plan.md` section 5d), is in the engine twin and the patch
  lists, not in the bench matrix.
