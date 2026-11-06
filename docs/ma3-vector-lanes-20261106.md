# MA3-PERF vector lanes — eight voices per AVX2 vector, 2026-11-06

**Result.** The pool now renders eight voices per AVX2 vector, and the output is
bit-identical to the scalar voice. At the interim 8-slot target, the whole
chain is 1.79–3.17x faster than the scalar engine in the same binary. The worst
case is Prizma with BCS on, at 2.95 M cycles per 128-frame block. In 122,880
measured vector blocks, none missed the deadline and none exceeded the plan's
80% CPU target. The largest single block took 1.59 ms of the 2.667 ms
deadline. The measurements are hosted and on a turbo clock; this is not a live
or no-xrun claim.

## What changed

1. **Staged voice tick** (`src/ma_voice.c`, normal build). `synth_tick` now
   runs as a front end, an oscillator block, the decimators, a filter stage and
   a back end. Each voice performs the same float operations in the same order.
   Only stages with disjoint state were regrouped: all eight substeps now come
   before the decimators, and mozaik/raster come before the 2x mix and ladder.
   `prepare_source_frame_guarded` takes the guard gains as arguments, because a
   lane bank holds them. Controls are filled in place (`fill_render_controls`),
   so Clang emits no `memcpy` in the freestanding core. The
   `MA_SOURCE_EVIDENCE` build keeps the single-pass tick unchanged.
2. **Vector lanes** (`src/ma_voice.c`, `ma_lanes_*`). The lane-major state of
   eight slots covers the oscillators, both oversampling rings, the ladder and
   the output ring (`ma_voice_lanes`, `src/mamutanalog.h`). The AVX2 kernel
   runs the oscillator block, the 8x→4x→2x decimators, the 2x
   mix/pressure/ladder steps and the 7-tap decimator eight lanes wide. It uses
   the exact conversions and masked paths verified by the
   [oscillator](ma3-simd-osc-spike-20261106.md) and
   [ladder/FIR](ma3-simd-spike-20261106.md) spikes. The front end and back end
   still run per voice: controls, BCS, envelopes, prewarp, mozaik, raster,
   gains. Only the `ml_*` functions carry `target("avx2")`, with no FMA.
   Runtime detection via CPUID and XGETBV uses no external symbols, so the core
   remains freestanding and still runs on any x86-64.
3. **Pool** (`src/ma_pool.c`). The new `ma_voice_pool_config.engine` field
   takes `MA_POOL_ENGINE_AUTO`, the zero default, or `MA_POOL_ENGINE_SCALAR`.
   With AUTO and AVX2 present, `ma_voice_pool_render` does three things:
   - It gathers the awake slots into banks of eight. Ring index j holds the
     slot's own entry (position + j).
   - It computes all gathered voices first, then runs the unchanged per-slot
     transition/pan/sum/sleep loop in slot order.
   - It scatters back in the exact scalar representation, ring positions
     included. That happens at the end of the call, or as soon as a slot falls
     asleep.

   Outside a render call, the slots' `ma_synth` state stays authoritative.
   `ma_voice_pool_tick` stays scalar.
4. **Bench** (`MA3_PERF_ENGINE=auto|scalar`) and tests. `test/ma_pool.c`
   `engine_twin` runs the AUTO and SCALAR engines through random events with
   sustain, three limits (8, 12 as a partial bank, and 32) and unison 1/2/4,
   under short envelopes. After every render call it compares the frames,
   every slot's whole `ma_synth` and the diagnostics bit-for-bit: 73,354
   frames, 59 sleeps. Two deliberate faults were caught, a scatter missing one
   field and a ring offset by one. The bench CLI gained a test that both engines
   render identical PCM.

## Gates

Each gate ran in a scratch build directory, with the vector lanes active:

- GCC 16.2.1 and Clang 22.1.8: full `make test`, 111,378 core checks with 0
  failures, every pinned PCM and hidden-state signature, and 0 warnings. With
  AUTO now the default, every pool render in the suite goes through the lanes.
  That includes the MA3 shows, the pool twins and the effects twins.
- Freestanding core symbol audit on both compilers: only `memset` is
  unresolved.
- Clang ASan/UBSan/float-cast-overflow: full `make test`, with no runtime
  errors.
- The staged scalar refactor alone (step 1) passed the same three gates before
  the lanes were added.

## Measurement

Run: `build/ma3-perf-20261106-02/`. The voice source SHA256 is `f8a90082…`,
the pool `902e5dd6…`, the bench `38febbc5…` and the release binary
`346aa3ea…`. HEAD is `38ab516`, plus `working-tree.diff`. GCC 16.2.1,
`-O2 -ffp-contract=off`. CPU 2, `schedutil`, AC power, sleep inhibited from the
start, and no suspend in the run window.

There were ten patch/BCS cases with 8 slots held, wet effects and 128 frames.
The scalar and AUTO engines of one binary alternated per case, in the order
S/A, A/S, S/A over three epochs. Each process ran 256 + 2,048 blocks × 2
passes, with counters required. Every case's PCM and effect signatures were
identical between the engines.

| Case | Scalar Mcycles p50 | Vector Mcycles p50 | Speedup | Vector CPU p50 ms | p99 | max | > 80% target | CPU / wall misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Tepih / BCS off | 4.652 | 1.517 | 3.07x | 0.510 | 0.735 | 0.877 | 0 | 0 / 0 |
| Tepih / BCS on | 5.957 | 2.668 | 2.23x | 0.895 | 1.268 | 1.467 | 0 | 0 / 0 |
| Lead / BCS off | 5.029 | 1.586 | 3.17x | 0.533 | 0.633 | 0.875 | 0 | 0 / 0 |
| Lead / BCS on | 6.066 | 2.604 | 2.33x | 0.873 | 0.992 | 1.363 | 0 | 0 / 0 |
| Raster / BCS off | 3.900 | 1.620 | 2.41x | 0.544 | 0.716 | 0.896 | 0 | 0 / 0 |
| Raster / BCS on | 4.795 | 2.675 | 1.79x | 0.897 | 0.952 | 1.283 | 0 | 0 / 0 |
| Prizma / BCS off | 5.544 | 1.909 | 2.90x | 0.641 | 0.824 | 1.145 | 0 | 0 / 0 |
| Prizma / BCS on | 6.292 | 2.947 | 2.14x | 0.988 | 1.278 | 1.585 | 0 | 0 / 0 |
| Granica / BCS off | 5.534 | 1.881 | 2.94x | 0.631 | 0.939 | 1.032 | 0 | 0 / 0 |
| Granica / BCS on | 6.442 | 2.907 | 2.22x | 0.975 | 1.338 | 1.533 | 0 | 0 / 0 |

Per-epoch medians agree to within about 1% (`ab-summary.md`). Each engine has
12,288 measured blocks per case.

## Reading

- **Clock headroom.** The worst vector median is 2.95 M cycles, which would be
  1.40 ms at the 2.1 GHz base clock. That is 53% of the deadline and within the
  80% target, even without turbo.
- **Measured against the estimate.** The spikes estimated 1.86–2.72x against
  the earlier OPT3 [8-slot baseline](ma3-perf-20261106.md). Measured against
  that same baseline, the vector engine is 1.99–3.51x faster: Raster with BCS
  on is slowest at 5.325 → 2.675, and Lead with BCS off fastest at 5.573 →
  1.586.
- **The staged refactor alone made the scalar engine faster.** Its medians in
  this run are 8–11% below the earlier baseline: Tepih with BCS off went from
  5.223 to 4.652, Granica with BCS on from 7.000 to 6.442. This compares
  separate runs with the same protocol and a frequency-independent metric, not
  a matched A/B.
- **BCS dominates what is left.** BCS-on cases cost 0.9–1.1 M cycles more than
  BCS-off. The BCS integrator, plus mozaik, raster and the per-voice front end,
  are now the largest scalar parts.
- **The deadline tail is clean in this run.** It is the environmental tail from
  the baseline, not part of the DSP. MA3-LIVE still owns isolation, priority
  and the no-xrun listening gate.
- **32 slots are still out of reach.** They would need about four times the
  8-slot cost, still above the deadline. They remain the original target, not
  the interim one.
