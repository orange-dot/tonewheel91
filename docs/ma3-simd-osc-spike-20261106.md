# MA3-PERF oscillator SIMD spike — the source block as 8 AVX2 lanes, 2026-11-06

**Question.** The first [SIMD spike](ma3-simd-spike-20261106.md) showed that
the ladder and the 31-tap decimator alone cannot reach the target. Can the
oscillator block also run eight voices as eight AVX2 lanes, bit-exact, and
does that close the gap? The oscillator block is everything `synth_tick`
does with the two oscillators per output sample: `set_guard_target` for VCO2,
eight `render_source_substep` calls and two `next_guard_gain` calls.

**Answer.** Yes, it is bit-exact on every path, and the block is 3.8–5.7x
faster per voice-sample. Together with the first spike, the estimated
whole-chain speedup is 1.86–2.72x. The worst case (Tepih, BCS on) would fall
from 6.53 to about 3.35 M cycles per 8-voice block. That is 60% of the
deadline at the 2.1 GHz base clock and under the plan's 80% target there. The
first spike's ladder and FIR estimates are part of these numbers. Integration
has not been done, and no shipping code changed.

## Method

`driver/spike_ma_osc_simd.c` (target `make spike-ma-osc-simd`, not part of
`test`) includes `src/ma_voice.c` white-box. Its scalar functions are the
reference. Only the vector functions are `target("avx2")`, without FMA.

- **Every path is lane-masked:** sparse and full previews, the sine
  (`mamut_sine`, `tw_sin_turns`, `tw_sat`), cross-mod, hard-sync events and the
  VCO2 phase reset, guard-target transitions, triangle initialisation, and
  `poly_blep`'s two edge branches. A path runs only if some lane needs it, and
  its result is masked either way.
- **Exact conversions AVX2 lacks:**
  - `(float)(uint32_t)x` for `phase_turns`: two exact 16-bit halves summed, one
    rounding.
  - `(float)x` for q48 values ≤ 2^48: two exact 24-bit halves.
  - `(uint64_t)f` truncation for non-negative floats: the mantissa shifted by
    the exponent in 64-bit lanes (`vpsllvq`/`vpsrlvq`).
- **`--check`:** 200,000 samples × 8 lanes × 8 substeps. The lanes cycle
  through all five patches. A third of the samples get a perturbation of
  their controls, within sanitized ranges: waveform levels, often exactly 0;
  pulse widths; VCO2 level and fine tune; noise; sync amount and softness;
  cross-mod, often 0. On top of that come random note jumps, occasional base
  frequencies up to 40x, filter modulation of ±2, random start phases and
  re-armed triangle initialisation. After every sample, the substep outputs
  and the full state of both oscillators are compared bit-for-bit: phase,
  triangle, sync residual, guard gain, target, step and frames, and the
  initialised flag. Coverage in lane-samples: 1,200,062 with cross-mod, 43,546
  VCO1 wraps with sync on, 14,236 guard-target moves, 971,392 sparse previews
  and 1,299,667 with sine. GCC 16.2.1, Clang 22.1.8 and Clang
  ASan/UBSan/float-cast-overflow all pass.
- **Mutation check of the comparison.** Five deliberately wrong lanes were
  each caught within a few samples. They changed the cross-mod scale by one
  ulp, the sync `before` factor, the guard-step sign, the 24-bit split constant
  of the u48 conversion, and the saw mask. The saw-mask mutation was not
  caught, because it is an equivalent mutant: an inactive waveform's level is
  exactly 0, and `0 × finite` differs only in the sign of zero, which the
  following sums absorb. The scalar sparse preview relies on the same fact.
- **`--bench`:** for each patch, eight voices hold an 8-note chord and settle
  for 256 samples. The scalar path then records 1,024 samples of inputs
  (controls, source frame, noise, guards). Scalar and 8-lane replays of
  identical inputs start from identical oscillator state, with 15 trials and
  the median. Outputs and final state matched in every run. Three epochs on
  CPU 2, with sleep inhibited.

Run: `build/ma3-simd-spike-20261106-02/` (source copy, binary,
`bench-epoch{1,2,3}.csv`, SHA256). The spike source SHA256 is `eac9a4af…`, and
the voice source is OPT3 (`3f99f5d7…`).

## Results

Oscillator block, cycles per voice-sample, as the median of the three epochs.
Instructions are per voice-sample and identical in every epoch.

| Patch | Scalar | 8 lanes | Speedup | Scalar instr. | 8-lane instr. |
|---|---:|---:|---:|---:|---:|
| Tepih | 2,246 | 394 | 5.70x | 5,524 | 648 |
| Lead | 2,691 | 501 | 5.38x | 5,812 | 716 |
| Raster | 1,566 | 412 | 3.81x | 3,632 | 642 |
| Prizma | 2,956 | 612 | 4.83x | 5,382 | 786 |
| Granica | 2,957 | 615 | 4.81x | 5,382 | 785 |

Raster gains least because its scalar previews are already sparse and cheap.
The 8-lane block evaluates the union of the paths the eight voices need.

## Whole-chain estimate (Amdahl, all three stages)

These are savings from the oscillator block (this run), plus the ladder and
31-tap decimator ([first spike](ma3-simd-spike-20261106.md), 1,254 cycles per
voice-sample), set against the [8-slot baseline](ma3-perf-20261106.md) medians:

| Case | Cycles / voice-sample | Oscillator share | Ladder + FIR share | After | Whole chain | Block Mcycles after |
|---|---:|---:|---:|---:|---:|---:|
| Tepih / BCS off | 5,101 | 44% | 29% | 1,994 | 2.56x | 2.04 |
| Tepih / BCS on | 6,375 | 35% | 23% | 3,268 | 1.95x | 3.35 |
| Lead / BCS off | 5,442 | 49% | 27% | 1,997 | 2.72x | 2.05 |
| Lead / BCS on | 6,454 | 42% | 23% | 3,009 | 2.14x | 3.08 |
| Raster / BCS off | 4,284 | 37% | 34% | 1,875 | 2.28x | 1.92 |
| Raster / BCS on | 5,200 | 30% | 28% | 2,791 | 1.86x | 2.86 |
| Prizma / BCS off | 5,969 | 50% | 25% | 2,371 | 2.52x | 2.43 |
| Prizma / BCS on | 6,720 | 44% | 22% | 3,122 | 2.15x | 3.20 |
| Granica / BCS off | 5,927 | 50% | 25% | 2,330 | 2.54x | 2.39 |
| Granica / BCS on | 6,836 | 43% | 21% | 3,240 | 2.11x | 3.32 |

The deadline is 5.6 M cycles at the 2.1 GHz base clock and 8.75 M at the
observed 3.28 GHz turbo. The plan's 80% ceiling is 4.48 M at base clock.
Every estimated block is below that (1.92–3.35 M). Even multiplied by the
observed environmental tail of 1.47x, every case stays under the base-clock
deadline (at most 4.9 M).

The estimate leaves three things out. First, isolated kernels run with warm
caches, trained predictors and different out-of-order overlap than inside the
chain. Second, integration adds cost: lane gather/scatter or an SoA voice
layout, masking of sleeping lanes, and ring rotation on wake. Third, BCS,
envelopes, controls, prewarp, mozaik, raster, mixing, the output and effects
all stay scalar. Only an integrated A/B run on the bench decides.

## What integration needs

A per-sample, stage-major voice pipeline for the 8 lanes, keeping each voice's
float operation sequence:

1. Per voice, scalar: controls, character, glide, LFO, BCS, envelopes,
   prewarp, guards, noise, `prepare_source_frame`.
2. 8 lanes: the oscillator block, writing its eight substep outputs straight
   into lane-major oversampling rings.
3. 8 lanes: the FIR cascade (8x → 4x → 2x), mixing, mixer pressure, the
   ladder, and the 7-tap output FIR. Mozaik and raster stay per voice for now.
4. Per voice: amplitude, velocity and pressure gains into the pool sum, in slot
   order.

The state becomes lane-major: oscillators, rings and the ladder. Sleeping
slots are masked, and a voice waking out of step rotates its rings to the
shared position, which preserves their content. AVX2 (x86-64-v3) becomes a
declared requirement of this path, and the scalar voice stays as the
reference and fallback. The strongest gate already exists: the pinned PCM and
hidden-state signatures in `make test` must not move.
