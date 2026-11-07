# MA3-PERF raster in lanes and BCS inputs in registers, 2026-11-07

**Result.** Raster now runs eight voices wide. Both raster and the vector BCS
build their per-lane inputs directly in vector registers instead of filling
small arrays and reloading them. The A/B is against `d1e76c0` on a quiet
host, with identical PCM in all ten cases and no CPU or wall deadline miss in
122,880 measured blocks:

- 1.19–1.23x faster with BCS on in the raster patches
- 1.15x faster with BCS on in the other patches
- 1.07–1.08x faster with BCS off in the raster patches
- unchanged with BCS off in the other patches

The worst case drops from 2.073 to 1.737 M cycles per 8-slot block. This is a
hosted result, not a live acceptance.

## The first attempt was slower

A first `ml_raster` was bit-exact and every gate passed, but it made the
raster patches 5–7% slower (`build/ma3-raster-ab-20261107-01/`), even though
it retired 7% fewer instructions. It wrote each lane's table values,
parameters and phase into small arrays one scalar at a time, then loaded each
array as one 256-bit vector. It also zeroed those arrays on every step.

A Haswell load that spans several in-flight smaller stores cannot be
forwarded, so it waits for the stores to commit.

The PMU probe now has a `store` group: `LD_BLOCKS.STORE_FORWARD` (0x0203),
`LD_BLOCKS_PARTIAL.ADDRESS_ALIAS` (0x0107) and `RESOURCE_STALLS.SB` (0x08a2).
Calibration gave exactly 1.000 blocked forward per trigger (a dword load over
four byte stores). The 4K alias event gave 6.5 per trigger, because it counts
re-dispatches, so it is only used to compare A and B.

Four runs in A, B, B, A order (`build/ma3-raster-pmu-20261107-01/`, vector
engine, mean of the per-block medians):

| Case | Mcycles A → first attempt | Blocked forwards | Store-buffer-full stall cycles |
|---|---|---|---|
| Tepih / BCS off | 1.334 → 1.317 | 4 → 3 | 76 k → 69 k |
| Tepih / BCS on | 1.787 → 1.778 | 3,486 → 3,512 | 76 k → 69 k |
| Raster / BCS off | 1.438 → 1.541 | 0 → 2,760 | 52 k → 71 k |
| Raster / BCS on | 1.779 → 1.886 | 3,490 → 6,324 | 52 k → 72 k |
| Prizma / BCS on | 2.061 → 2.169 | 3,465 → 6,442 | 54 k → 70 k |
| Granica / BCS on | 2.023 → 2.137 | 3,476 → 6,446 | 52 k → 71 k |

The other four cases are in `store-table.md`.

What the counts show:

- **Raster:** the first attempt added about 2,800 blocked forwards per block,
  about 11 per raster step, plus about 19 k cycles of store-buffer stalls.
- **BCS:** the existing vector BCS (`be5f1e8`) already had the same pattern,
  with about 3,500 blocked forwards per BCS-on block, from its per-lane
  parameter and state arrays.
- **4K aliasing:** it fell, so it does not explain the slowdown.

## Change (`src/ma_voice.c`)

- **Lane picks in registers.**
  - `ML_PICK_PS` and `ML_PICK_U64` build a vector from per-lane scalar
    expressions with `_mm256_setr_*`. The values never go through memory.
  - Lanes outside the active set take a benign fallback value, and their
    fields are never read. `ml_lane_mask` builds the active-lane mask.
- **`ml_raster` (AVX2).** It implements `render_raster` for both 2x steps:
  - **Phase.** Phase step and q48 use the exact helpers.
  - **Mip.** `raster_mip` becomes a count of passing harmonic limits, then a
    load and blend. The limits halve, and multiplying by a power of two is
    exact, so the scalar walk stops at the first failing limit and the count
    equals it.
  - **Family and morph.** Both are computed eight lanes wide.
  - **Table reads.** Each table row takes one 32-bit `vpgatherdd` of the int16
    pair (index, index + 1). Index 255 wraps to entry 0 of its row: such a
    lane reads the pair at 254, and when any lane needs it, an extra gather
    reads entry 0. No read leaves `MA_RASTER_TABLE`.
  - **Coarse row.** For a lane that does not blend, the coarse mip index is
    clamped to a valid row.
  - **Output.** Lanes without raster output +0, as the scalar path does.
  - **Write-back.** Phase and mip go back per lane after full-width stores,
    which forward normally.
- **`ml_bcs`.**
  - `bcs_parameters` runs eight lanes wide. Each parameter's four landmarks
    sit in a constant vector, and `vpermps` picks landmark and landmark + 1
    per lane.
  - The frequency clamps, the integration step and the state are built in
    registers.
  - The integrator, the safety check and the reset path are unchanged.
- **Lane tick.** Raster leaves `voice_front_b_lane`. Raster has its own state
  and reads only `base_hz` and the raster controls, so running it after
  mozaik changes nothing. `ma_lanes_tick` calls `ml_raster` for lanes with
  `raster_mix > 0`. The scalar path is untouched.

## Gates

- **New `test/ma_raster_lanes.c`** (`check-ma-raster-lanes`, in `test`) is
  white-box. It checks 224,620 lane-samples against two `render_raster`
  calls, comparing both outputs, the phase and the mip bit for bit, and
  requires lanes outside the set to read 0. The inputs include:
  - random lane sets, sample rates and phases (near 0 and near wrap)
  - zero, ordinary and above-Nyquist base frequencies
  - every family edge (50,274) and warp 0 or 1
  - 155,530 mip blends and 21,496 lanes at the top mip
- **Mutation testing.** It ran on a scratch copy against `ma_raster_lanes`
  and `ma_bcs_lanes`, and the tests caught 18 mutations, including:
  - the index-255 wrap and its extra gather
  - the mip count bounds, the harmonic limit and the family clamp
  - the low/high pair order, the mip write-back and the warp scale
  - the BCS second landmark, upper clamp, step, drive clamp, low clamp and
    state order
  - the lane-mask bit order

  These mutants survived and are equivalent:
  - **Warp wrap guards.** For warp in [0, 1], `p + 0.12 w sin(2πp)` is
    monotone on [0, 1], and `& 255` maps 1.0 to the same index and fraction
    as 0. The guards stay to mirror the scalar code.
  - **Top-mip blend zero.** The coarse row is clamped to the same row, so
    blending changes nothing.
  - **Off-lane output mask.** Off lanes already compute exactly +0.
  - **BCS landmark clamp at regime 1.** For all twelve parameters,
    `L2 + (L3 − L2) == L3` in float.
- **GCC and Clang `make test`:**
  - 111,378 core checks pass, with every pinned signature and `engine_twin`
  - `ma_bcs_lanes` (89,752 lane-steps), `ma_front_lanes` (400,000) and
    `ma_raster_lanes` pass
  - 0 warnings
  - The freestanding symbol audit passes, with only `memset` unresolved.
- **Clang ASan/UBSan/float-cast-overflow `make test`:** no runtime errors.
- **Probes and spikes** build without warnings.

## Measurement

Run: `build/ma3-lanes-sf-ab-20261107-03/`.

| Binary | Source | Bench SHA256 |
|---|---|---|
| A | `HEAD` `d1e76c0` | `8cc44082…`, identical to the front-math A/B's B |
| B | working tree: raster in lanes, BCS inputs in registers | `6983a3a8…` |

The protocol is the earlier one:

- AUTO engine, the ten patch/BCS cases, 8 held slots, wet effects, 128 frames
- 256 + 2,048 blocks × 2 passes per process
- three epochs in A/B, B/A, A/B order, with counters required
- CPU 2, with sleep inhibited, and no browser or editor running (load 1.25)

| Case | A Mcycles | B Mcycles | A/B | B instr. ratio | B CPU p99 ms | B max ms | B CPU / wall misses | PCM A = B |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Tepih / BCS off | 1.345 | 1.324 | 1.02x | 0.997 | 0.557 | 0.770 | 0 / 0 | yes |
| Tepih / BCS on | 1.807 | 1.573 | 1.15x | 0.981 | 0.557 | 0.843 | 0 / 0 | yes |
| Lead / BCS off | 1.414 | 1.408 | 1.00x | 0.997 | 0.488 | 0.758 | 0 / 0 | yes |
| Lead / BCS on | 1.754 | 1.528 | 1.15x | 0.981 | 0.533 | 0.812 | 0 / 0 | yes |
| Raster / BCS off | 1.441 | 1.330 | 1.08x | 0.898 | 0.462 | 0.733 | 0 / 0 | yes |
| Raster / BCS on | 1.785 | 1.456 | 1.23x | 0.893 | 0.515 | 0.806 | 0 / 0 | yes |
| Prizma / BCS off | 1.737 | 1.619 | 1.07x | 0.906 | 0.622 | 0.905 | 0 / 0 | yes |
| Prizma / BCS on | 2.069 | 1.737 | 1.19x | 0.900 | 0.608 | 0.910 | 0 / 0 | yes |
| Granica / BCS off | 1.695 | 1.584 | 1.07x | 0.904 | 0.575 | 0.864 | 0 / 0 | yes |
| Granica / BCS on | 2.033 | 1.708 | 1.19x | 0.899 | 0.617 | 0.889 | 0 / 0 | yes |

The medians are the median per-block cycles over all three epochs. B's
per-epoch medians agree to within 1.3%. "Misses" counts blocks over the
2.667 ms deadline. The table script is `ab_table.py` in the run directory.

There are two discarded runs:

- **`build/ma3-lanes-sf-ab-20261107-01/`:** an editor started during epoch 3
  and moved cycles by up to 60%. Its clean epochs 1–2 agree with the table
  above.
- **`-02`:** stopped by the operator, so its files are partial.

The blocked-forward count of the final version was not measured again. The
cycle and instruction changes are consistent with the stalls being gone.

## Reading

- **Size of the gain.**
  - In the raster patches, raster saves about 0.11 M cycles per block. That
    is close to its 9–12% share in the front-end attribution.
  - The BCS input change saves about 0.22 M cycles per BCS-on block. That is
    more than the blocked forwards alone would explain (about 3,500 × 13
    cycles), so the 21 array fills and 24 reloads it removed also cost
    issue bandwidth.
- **Cumulative.**
  - Against the first vector-lane commit, the worst case has gone from 2.947
    to 1.737 M cycles, and the worst BCS-off case from 1.909 to 1.619 M.
  - The scalar engine's worst case is 6.442 M.
- **Lesson for later lane work.** Do not build a vector from per-lane scalar
  stores and then load it right away. Build it in registers, or keep the
  value in lane state. `ml_front_gather` uses the same fill pattern, but it
  showed no blocked forwards: its fills complete long before the vector
  loads.
