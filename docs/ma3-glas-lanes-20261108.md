# Glas on the vector lanes, and lanes in the MA3 shows, 2026-11-08

Step C of the SIMD-first plan (`ma3-avx2-baseline-20261108.md`). Before
this change, Glas sent a whole pool to the scalar engine. Separately, every
`ma3_show` exhibit ticked its pools one frame at a time through
`ma_voice_pool_tick`, which is always scalar, so no exhibit render had ever
used the lanes.

## Changes

**`src/ma_lanes.c`, `ma_lanes_tick`.** After front end part B and the
front-end math, every active lane runs `ma_glas_prepare` (smoothers, Orbita
tick, two 2x grain samples) when its Glas is active. It stores
`frame_grain_mix` exactly as the scalar tick does, and fills new lane inputs
`grain_mix` and `grain[2]`.
- In `ml_kernel` the grain crossfade follows `ml_mix`, where the scalar path
  does `source += grain_mix · (grain − source)`.
- That crossfade is a lane select on `grain_mix > 0`. The scalar code
  branches, and `x + 0·y` would turn −0 into +0.
- After the vector ladder, every lane with Glas active calls
  `ma_glas_formant` on its filtered sample before the back end.
- The SVF and the grains stay scalar per lane. Vectorising them is a later,
  pure speed step.

**`src/ma_pool.c`.** The Glas fallback to scalar is gone.

**`driver/ma3_show.c`.** `pass` renders each pool through
`ma_voice_pool_render`, in runs between control steps (every 480 frames)
and events. The stem sum, the layer hook, the fade and the mix stay per
frame, in the old order. AUTO pools now use the lanes.
- The aggregate active-slot peak is sampled at run boundaries. It is a
  printed diagnostic only.

**`driver/exhibit_ma3_hurt_industrial.c`.** New `--engine auto|scalar`
option, for A/B renders.

## Verification

- `make test` and `make test-clang` pass with every pinned signature, and
  the pool twin reports vector lanes active.
- `test/ma_glas.c` gains a heavy twin: a fat-vocal-like Glas pool (unison
  4, 16 slots, sub-octave, formants .25, Orbita .55, grains switching on and
  off, eight overlapping notes, vowel edits). Vector lanes match the scalar
  engine bit for bit over 76,800 frames, and both engines are confirmed
  active.
- Blues and the base Hurt are bit-identical over the first 30 s, every stem
  and the mix.
- Hurt industrial stems through the lanes: rhythm `de1f65c0459dc3d6`,
  guitar `ab0f4ab5ccc91f67`, vocal (fat, unison 4, Glas)
  `7d28100b09ed230f`. All are unchanged.

## Rough speed

Noisy host, with VS Code and a browser running. One run each, back to back.

| Render | Scalar | Lanes | Speed-up |
| --- | ---: | ---: | ---: |
| Hurt industrial, `--vocal-fat-all --parts vocal` (283 s of audio) | 238.6 s | 134.3 s | 1.78× |

This is a wall-clock render time, not an MA3-PERF measurement. The quiet-host
A/B with `bench_ma3_perf` is still pending.

## Vectorised grains and formant SVF

After the per-lane hybrid above, the two remaining scalar Glas parts now
run across the eight lanes.

**Data.** `ma_voice_lanes` gains lane-major Glas state:
- formant SVF `s1`/`s2`;
- coefficients `g`/`k` with a per-lane epoch;
- the sixteen grain slots per voice: phase, step, envelope, decay, attack
  position and step, gain, and an active mask.

`ma_lanes_gather` and `ma_lanes_scatter` copy them with the rest of the
voice. Outside a render call the voice's `ma_glas` stays authoritative.

**Code shared with the scalar path.** `ma_glas.c` is split into:
- `ma_glas_prepare_controls`: smoothers and the Orbita tick, which now
  bumps `coefficient_epoch`;
- `ma_glas_pulse`;
- `ma_glas_new_grain`;
- `ma_glas_reset_state`.

The scalar `ma_glas_prepare` and `launch_grains` are built from them, so
their arithmetic is unchanged.

**Lanes.** Per frame:
1. Controls and the Orbita tick run per lane, and coefficients are copied
   into the lane arrays only when the epoch moved, once per 32 frames per
   voice.
2. For each 2x step, the pulse and launch run per lane.
3. `ml_glas_grains` renders all sixteen slots lane-major: attack, sine,
   envelope, phase wrap and floor, each select-masked to the lanes where
   that grain is active.
4. After the ladder, `ml_glas_formant` runs the four TPT SVFs lane-major,
   masked to lanes with a positive formant mix.

Non-finite grain or bank lanes take the scalar `ma_glas_reset_state` and
clear their lane state, exactly as the scalar path resets.

**Verification.**
- `make test` and `make test-clang` pass, Workbench included, as do the
  sanitized `test_ma_glas`, `test_ma_pool` and core tests.
- The heavy Glas twin (76,800 frames, grains switching on and off) is
  bit-identical.
- The fat Hurt vocal stays `7d28100b09ed230f`.
- The non-finite reset path is not reached by any test input. It mirrors
  the scalar code line for line.

| Fat vocal render (noisy host, one run each) | Scalar | Lanes |
| --- | ---: | ---: |
| per-lane Glas (above) | 238.6 s | 134.3 s |
| vectorised grains and SVF | 204.8 s | 107.9 s (1.90×) |

**Workbench build note.**
- `workbench/workbench.mk` builds `WB_AUDIO_OBJS` without a dependency on
  `src/mamutanalog.h`. After this change grew `ma_voice_lanes`, the stale
  `wb_live.o`/`wb_live_rt.o` made `test_live` segfault.
- Rebuilding those two objects fixes it: 50 checks pass with identical
  desk/live parity.
- The rule should list the header. That file belongs to the Workbench
  work.


## Precise Glas speed (quiet host)

Fat Hurt vocal (`--rhythm ma3 --vocal-fat-all --parts vocal --seconds 120`),
`taskset -c 2`, AVX2 baseline binary. Order: scalar, lanes, lanes, scalar.
The PCM is identical in all four runs (`1779c7d9de27e129`).

| Run | Scalar | Lanes |
| --- | ---: | ---: |
| 1 | 93.33 s | 49.89 s |
| 2 | 92.49 s | 50.59 s |
| mean | 92.91 s | 50.24 s |

That is 1.85× for a unison-4 Glas pool with sub-octave, formants and grains.
Before this work the pool rendered scalar whatever the engine setting.
