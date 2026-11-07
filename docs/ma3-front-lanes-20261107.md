# MA3-PERF front-end math in lanes, 2026-11-07

**Result.** The [front-end attribution](ma3-frontend-stages-20261107.md) found
three large stages: the source frame, the cutoff and prewarp, and the lane
fill. The first two now run eight voices wide and write straight into the lane
inputs, and the lane fill is gone. Against the previous commit, 8-slot blocks
are 1.08–1.12x faster, and the PCM is identical in all ten cases. The worst
case is Prizma with BCS on, at 2.073 M cycles per block. In this run the new
engine had no CPU or wall deadline miss in 122,880 measured blocks, on a quiet
host. This is a hosted result, not a live acceptance.

## Change (`src/ma_voice.c`)

- **`ml_front_math` (AVX2).** It computes `filter_envelope_amount`,
  `effective_filter_cutoff`, `filter_prewarp` and
  `prepare_source_frame_guarded` per lane, in the scalar operation order:
  - the envelope amount, keytrack and the poly-pressure and character
    cutoff ratios
  - the Chebyshev prewarp
  - the active waveforms and divisors, sync, pulse-width modulation, envelope
    ratio and mixer divisor
  - both phase steps, through the exact q48 helpers, with the VCO1 step
    computed only when cross-mod is zero

  The per-lane scalar inputs are gathered in `ml_front_gather`, and
  `interval_ratio` stays scalar per lane.
- **`voice_front_b_lane`.** It keeps the scalar parts of front part B (the BCS
  effect, envelopes, noise, mozaik, raster) and writes the plain lane inputs
  directly. Cutoff, prewarp and the source frame read only the envelope
  level, the controls and the guards, so running noise, mozaik and raster
  before them changes nothing. The lane tick stores each voice's
  `filter_cutoff_effective_hz` from the lane result.
- **Unchanged parts.** The scalar path (`voice_front_b`, `synth_tick`) is
  untouched. `ml_fill` stays for white-box probes only, as
  `[[maybe_unused]]`.

## Gates

- **New `test/ma_front_lanes.c`** (`check-ma-front-lanes`, in `test`) is
  white-box. It checks 400,000 lane-frames of random voices and controls
  against the scalar functions, comparing every output field bit-for-bit. The
  inputs include cross-mod off (99,300), poly pressure (300,065), card
  character (300,652), every interval from −30 to +30, extreme base
  frequencies, zero levels and zero guards. It caught both deliberate
  mutations, a keytrack constant and a reordered Chebyshev step.
- **GCC and Clang `make test`:** 111,378 core checks, every pinned signature,
  `engine_twin`, `ma_bcs_lanes` and `ma_front_lanes`, with 0 warnings. The
  freestanding symbol audit passes, with only `memset` unresolved.
- **Clang ASan/UBSan/float-cast-overflow `make test`:** no runtime errors.

## Measurement

Run: `build/ma3-front-ab-20261107-01/`.

| Binary | Source | Bench SHA256 |
|---|---|---|
| A | `HEAD` `4106152`: lanes, vector BCS | `5d2ae775…`, identical to the BCS A/B's B |
| B | working tree, plus vector front math | `8cc44082…` |

The protocol is the earlier one: AUTO engine, ten patch/BCS cases, 8 held
slots, wet effects, 128 frames, 256 + 2,048 blocks × 2 passes per process,
three epochs in A/B, B/A, A/B order, counters required. CPU 2 on a quiet
host (no browser, load 1.0), with sleep inhibited.

| Case | A Mcycles | B Mcycles | A/B | B CPU p99 ms | B max ms | B CPU / wall misses | PCM A = B |
|---|---:|---:|---:|---:|---:|---:|---|
| Tepih / BCS off | 1.489 | 1.343 | 1.11x | 0.728 | 1.281 | 0 / 0 | yes |
| Tepih / BCS on | 1.959 | 1.799 | 1.09x | 0.908 | 1.465 | 0 / 0 | yes |
| Lead / BCS off | 1.580 | 1.413 | 1.12x | 0.732 | 1.250 | 0 / 0 | yes |
| Lead / BCS on | 1.920 | 1.753 | 1.10x | 0.881 | 1.282 | 0 / 0 | yes |
| Raster / BCS off | 1.605 | 1.442 | 1.11x | 0.771 | 1.415 | 0 / 0 | yes |
| Raster / BCS on | 1.959 | 1.804 | 1.09x | 0.920 | 1.816 | 0 / 0 | yes |
| Prizma / BCS off | 1.877 | 1.730 | 1.08x | 0.892 | 1.455 | 0 / 0 | yes |
| Prizma / BCS on | 2.231 | 2.073 | 1.08x | 1.052 | 1.629 | 0 / 0 | yes |
| Granica / BCS off | 1.855 | 1.691 | 1.10x | 0.879 | 1.540 | 0 / 0 | yes |
| Granica / BCS on | 2.203 | 2.040 | 1.08x | 0.992 | 1.566 | 0 / 0 | yes |

The medians are the median per-block cycles over all three epochs. B's
per-epoch medians agree to within about 1.5%. "Misses" count blocks over the
2.667 ms deadline.

## Reading

- **Size of the gain.** The saving is 0.15–0.17 M cycles per block. The
  stages moved held 15–24% of an instrumented block. Moving them saved
  roughly half of that cost, and the rest is the per-lane gather
  (`ml_front_gather`) and the vector work.
- **Cumulative.** Against the first vector-lane commit, the worst case has
  gone from 2.947 to 2.073 M cycles, and the worst BCS-off case from 1.909 to
  1.730 M. The scalar engine's worst case is 6.442 M.
- **What is left in front of the kernel.** The scalar front end still covers
  controls, pitch, envelopes, noise, mozaik and raster. Raster, at 9–12% in
  the patches that use it, is the largest remaining candidate.
