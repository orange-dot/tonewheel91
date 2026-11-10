# One source for the scalar and lane DSP, 2026-11-10

**Decision.** The operator chose to stop writing each Mamut Analog DSP stage
twice, once for one voice and once for eight AVX2 lanes. Every stage is now
written once, against a lane-width layer, and compiled at both widths. PCM
does not change. Baseline: `c2bcf1e`. Branch: `ma-single-source`.

## Shape

`src/ma_lane_ops.h` is the width layer. A translation unit defines `ML_W`
as 1 or 8 before including it.

| | `ML_W 1` | `ML_W 8` |
|---|---|---|
| `ml_f` | `float` | `__m256` |
| `ml_m` (mask) | `bool` | `int32_t` × 8, 0 or −1 |
| `ml_int` | `int32_t` | `int32_t` × 8 |
| `ml_u64` | `uint64_t` | two `__m256i` |

Float arithmetic is ordinary C at both widths. GCC and clang accept
`+ − * /` and comparisons on `__m256`, with a float operand on either side,
and emit the same instructions as the intrinsics; `-ffp-contract=off` still
forbids implicit FMA. What C cannot spell the same way at both widths is
named: `ml_if(m, a, b)`, `ml_keep(m, a)`, `ml_any`, `ml_all`, `ml_fmadd`,
`ml_fnmadd`, `ml_u64_*`, `ml_trunc`, `ml_int_to_f`, `ml_load`, `ml_store`,
`ml_tally`, `ml_pick4` and `ml_i16_pair` (the raster table read: a gather at
width 8). The layer also holds the small helpers both widths share:
`ml_clamp`, `ml_smooth5`, `ml_wrap`, `ml_sin_turns`, `ml_sat`,
`ml_octave_ratio_small`, `ml_prewarp`, `ml_phase_step`, the ring push and
the halfband.

The stages are four kernels:

| Kernel | Contents |
|---|---|
| `ma_osc_kernel.h` | BLEP waveforms, triangle integrator, guards, sync, cross-mod, the source frame, the 8x block and its 8x → 2x decimation |
| `ma_filter_kernel.h` | filter envelope amount, effective cutoff, 2x mix, mixer pressure, ladder, 7-tap output decimator |
| `ma_sources_kernel.h` | raster plan and step, BCS parameters and RK4 sample |
| `ma_glas_kernel.h` | grain step, formant bank |

`ma_osc.c`, `ma_filter.c`, `ma_sources.c` and `ma_glas.c` include them at
width one and keep only adapters between `ma_synth` and the kernels' state.
`ma_lanes.c` includes them at width eight and keeps CPU detection, gather and
scatter, the per-lane front and back ends, and the adapters between the lane
bank and the kernels. Where a lane may take either side of a choice, both
sides are computed and `ml_if` picks one; a branch on `ml_any` is an ordinary
branch at width one.

The one-voice API is unchanged: `ma_synth_tick`, the card bank, patchlab and
the white-box entry points (`ma_voice_render_source_substep`,
`ma_voice_prepare_source_frame_guarded`, `ma_voice_render_raster`,
`ma_voice_render_bcs`, `ml_front_math`, `ml_raster`, `ml_bcs`) keep their
signatures. `MA_POOL_ENGINE_SCALAR` stays; it now runs the same kernels one
voice at a time and remains the reference the pool gates compare the lanes
against.

Removed: the scalar twins in `ma_osc.c` (waveforms, previews, sync paths,
substep), `ma_filter.c` (pressure, ladder, output decimator), `ma_sources.c`
(raster read, BCS derivative and RK4), `ma_glas.c` (grain loop body, formant
bank), the lane twins in `ma_lanes.c`, `ma_voice_set_guard_target`,
`ma_voice_next_guard_gain`, and the scalar helpers no code calls any more
(`raw_triangle`, `triangle_gain_for`, `triangle_step_for`, `triangle_seed`,
`smoothstep5`, `ma_sin_turns`, `ma_sat`, `sync_run_limit`).
`octave_ratio_small` and `filter_prewarp` became `ma_octave_ratio_small` and
`ma_filter_prewarp`, the kernels' forms at width one, for the code outside
the kernels (front A, controls, the Orbita tick). The evidence tick
(`MA_SOURCE_EVIDENCE`) lost its dead non-evidence branches and runs the
shared oscillator block and decimator.

Line count, `src/` Mamut Analog files: 3,941 before, 3,804 after, of which
1,287 are the width layer and kernels. The point is not the count: every
oscillator, filter, raster, BCS and Glas formula exists once.

## Exactness

Every kernel performs each voice's float operations in the order of the
scalar code it replaces, fusing only where that code called `ma_fmadd` or
`ma_fnmadd`. Two choices needed an argument rather than a transcription:

- `prepare_source_frame` no longer has an evidence branch. The evidence
  build passes a filter modulation of zero, which leaves the pulse widths
  unchanged (they are already clamped to 0.05–0.95 by the controls) and
  gives an envelope ratio of exactly 1.
- `ma_sat(1.45f)` in the Mamut sine is the literal `0x1.d67b52p-1f`, the
  value the same formula gives under both compilers.

The scalar formant bank skipped a voice when its mix was zero, the lanes
when it was not positive; the mix is clamped to 0–1, so the kernel takes the
lanes' test.

## Evidence

- `make test`, `make test-clang`, `make sanitize` and `make LTO=0 test` pass.
  Every pinned hash is unchanged.
- The white-box gates now compare width eight against width one of the same
  source: `ma_front_lanes` 400,000 lane-frames, `ma_raster_lanes` 224,620
  lane-samples, `ma_bcs_lanes` 89,752 lane-steps (304 reset paths), and
  `ma_pool` over both hard-sync variants, all bit-identical.
- `dump_ma_pcm` (every factory patch × BCS × Glas × both engines, 48 files)
  and the `exhibit_ma_osc` evidence build: 49 of 49 outputs byte-identical
  to `c2bcf1e`, checked after each step.
- `make analyze`: no new warnings.

## Speed

Run `build/ma-single-source-ab-20261110-03/`, the harness of
`docs/refactor-20261109.md` (8 slots, 128-frame blocks, 2,048 measured
blocks, user-space PMU, `taskset -c 2`, three epochs in alternating order).
A = `c2bcf1e`, B = this change, both `LTO=1`. A browser was running, so
cycles are good to about ±2 %; instruction counts are exact.

| Engine | Geometric mean A/B | Range | B/A instructions | PCM A = B |
|---|---:|---:|---:|---|
| auto (lanes) | 1.029× | 1.007–1.049× | 0.916–0.925 | 10/10 |
| scalar | 1.087× | 1.016–1.165× | 0.763–0.923 | 10/10 |

- The lanes run 8 % fewer instructions: the sync state and the shape masks
  are loaded once per frame instead of in every substep.
- The scalar engine runs 8–24 % fewer, most with BCS on: width one turns the
  kernels' selects into branch-free code.
- An intermediate run (`-02`) had the lanes at 0.995× (worst 0.979×) with
  only 0.5 % fewer instructions. As `ma_lanes.c` grew, GCC stopped inlining
  `ml_preview`, which runs twice per substep, and every call spilled the ymm
  state. `ml_preview` is now `[[gnu::always_inline]]`; per-block
  instructions on Tepih went from 2.142 M to 1.974 M (A: 2.153 M).

## Not done

- The per-voice front end (part A, the shared part B, envelopes, mozaik,
  Orbita) and the back end were never duplicated and still run per voice at
  both widths.
- `mix_source_2x` remains for the evidence tick only.
- `clamp_signal`, `wrap_phase`, `phase_turns`, `phase_step` and
  `phase_step_q48` remain as scalar helpers for code outside the kernels and
  for tests.
