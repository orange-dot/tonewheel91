# MA3-PERF front-end sub-stages of the vector engine, 2026-11-07

**Question.** The [stage attribution](ma3-stages-20261106.md) put 35–64% of an
8-slot vector block in the per-voice front end. Which parts of that front end
carry the cost, now that [BCS runs in lanes](ma3-bcs-lanes-20261106.md)? Are
the control smoothers and envelopes the right next target?

**Answer.** They are not the largest part. With BCS off, controls and
envelopes together take 6.5–11% of a block. The largest front-end items are:

- the source frame (`prepare_source_frame_guarded`), at 8–12%
- raster, at 9–12%, but only in the patches that use it
- cutoff and prewarp, at 5–8%
- mozaik, at 4–6%
- the lane fill, at 3–4%

Vector BCS is 13.6–16.2% of a BCS-on block. The vector oscillator block
remains the single largest stage, at 20–31%.

## Method

`driver/probe_ma3_stages.c` now mirrors the current `ma_lanes_tick`: front
part A, vector BCS, front part B, lane fill, kernel, back end. It splits the
front end into ten sub-stages. Each sub-stage runs for every gathered voice
before the next sub-stage starts. Within a voice the operation order is
unchanged, and voices share no state. The one reordering is that mozaik's two
2x steps run before raster's two; their states are disjoint. A twin pool
renders through the real API, and the probe requires bit-identical frames and
slot state. They matched in all ten cases in both runs.

The workload is the bench's 8 held slots, wet effects, 128-frame blocks, 256
warm-up and 1,024 measured blocks. The host was quiet (no browser, load
0.5) on CPU 2, with sleep inhibited. The instrumented render took 7–11%
longer than the untimed twin. Between the two runs every share agrees to
within about 0.7 percentage points.

Run: `build/ma3-stages-20261107-01/` (`run1.csv`, `run2.csv`, `sha256.txt`,
`working-tree.diff`). The base commit is `be5f1e8`.

## Shares of the instrumented block, run 1 (%)

| Case | Controls | Pitch | BCS | Envelopes | Cutoff + prewarp | Noise | Source frame | Mozaik | Raster | Lane fill | Osc block | Decimators | Filter + FIR7 | Output + FX | Other |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Tepih / BCS off | 6.9 | 4.7 | 0.6 | 4.5 | 7.7 | 1.1 | 12.2 | 5.8 | 0.8 | 4.3 | 24.4 | 6.2 | 11.0 | 5.6 | 4.2 |
| Tepih / BCS on | 5.5 | 3.7 | 15.6 | 4.2 | 6.1 | 0.9 | 9.3 | 4.6 | 0.6 | 3.4 | 24.8 | 4.9 | 8.6 | 4.4 | 3.3 |
| Lead / BCS off | 6.5 | 4.5 | 0.6 | 2.0 | 7.4 | 1.1 | 10.9 | 5.6 | 0.8 | 4.1 | 30.3 | 6.0 | 10.6 | 5.4 | 3.9 |
| Lead / BCS on | 5.5 | 3.8 | 16.1 | 2.1 | 6.2 | 0.9 | 9.2 | 4.7 | 0.6 | 3.5 | 25.4 | 5.1 | 8.9 | 4.6 | 3.4 |
| Raster / BCS off | 6.3 | 4.3 | 0.6 | 1.9 | 7.1 | 1.0 | 10.4 | 5.4 | 12.1 | 4.0 | 24.3 | 5.8 | 9.0 | 4.0 | 3.9 |
| Raster / BCS on | 5.4 | 3.7 | 15.2 | 2.1 | 5.9 | 0.9 | 9.2 | 4.5 | 10.2 | 3.4 | 20.4 | 4.9 | 7.5 | 3.6 | 3.3 |
| Prizma / BCS off | 5.5 | 3.7 | 0.5 | 3.6 | 6.2 | 0.9 | 9.0 | 4.6 | 10.6 | 3.5 | 30.2 | 5.0 | 8.8 | 4.5 | 3.3 |
| Prizma / BCS on | 4.7 | 3.2 | 13.6 | 3.7 | 5.3 | 0.8 | 8.1 | 4.0 | 9.0 | 3.1 | 25.8 | 4.3 | 7.5 | 3.9 | 2.9 |
| Granica / BCS off | 5.6 | 3.8 | 0.5 | 1.7 | 6.2 | 0.9 | 9.3 | 4.7 | 10.8 | 3.5 | 30.7 | 5.1 | 9.1 | 4.6 | 3.4 |
| Granica / BCS on | 4.8 | 3.3 | 13.9 | 1.9 | 5.4 | 0.8 | 8.2 | 4.1 | 9.2 | 3.1 | 26.3 | 4.4 | 7.7 | 4.0 | 3.0 |

"Other" is the sum of gather/scatter, control/pan, back end and the slot
loop. Run 2 and the column definitions are in the CSVs.

## Reading

- **Controls and envelopes are cheap in the held state.** Controls take
  4.7–6.9% and envelopes 1.7–4.5%. A settled smoother returns its current
  value, and an envelope in sustain only assigns. Moving them into lanes would
  need lane-major state for 41 smoothers and two envelopes per voice, for
  little gain while notes are held. They could matter during control moves or
  attack/release, which this held workload does not measure.
- **The source frame is the largest front-end item in every case.** It covers
  pitch ratios (`octave_ratio_small`, `interval_ratio`), the phase-step
  division and q48 conversions, waveform divisors, and the sync and
  pulse-width terms. All of these are lane-ready with the existing exact
  helpers. Writing its results straight into the lane inputs also removes the
  lane fill, so the bundle covers 12–16% of a block.
- **Cutoff and prewarp come next, at 5–8%.** They are pure arithmetic: the
  envelope amount, keytrack, a pitch ratio, and the Chebyshev series.
- **Raster dominates where it is used.** It takes 9–12% in Raster, Prizma
  and Granica, and under 1% in Tepih and Lead. It reads a wavetable, which
  makes it the hardest to put in lanes (gathers or a per-lane table read).
- **Mozaik (4–6%) and noise (about 1%) are small.** Noise needs a 64-bit
  multiply, which AVX2 lacks, and is not worth that.

## Next

1. In lanes: the source frame, cutoff and prewarp, with direct lane-input
   writes. That bundle is about 18–24% of a block.
2. Raster in lanes. Measure first whether gathers beat per-lane table reads.
3. Controls and envelopes only if a moving-control or attack/release workload
   shows them to be costly.
