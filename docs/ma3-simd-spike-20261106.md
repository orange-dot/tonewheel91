# MA3-PERF SIMD spike — ladder and 31-tap decimator as 8 AVX2 lanes, 2026-11-06

**Question.** Can two per-voice stages run eight voices at once, one voice per
AVX2 lane, bit-exact to the scalar voice, and how much faster? The stages are
the 2x ladder (`render_ladder_2x`) and the 31-tap oversampling decimator
(`push_oversample_history` + `decimate_oversample`). The interim target of
8 slots is exactly one 8-lane vector.

**Answer.** Yes, both stages are bit-exact. The kernels run 5.95x (ladder) and
9.0x (FIR) faster per voice-call. Applied to the 8-slot baseline, these two
stages alone would make the whole chain an estimated 1.22–1.41x faster. That is
less than the ~1.5x the worst case needs at base clock (see
[the baseline](ma3-perf-20261106.md)). Most of the remaining cost lies outside
these two stages. This is a kernel spike: no shipping code changed.

## Method

`driver/spike_ma_simd.c` (target `make spike-ma-simd`, not part of `test`)
includes `src/ma_voice.c` white-box and uses its scalar functions as the
reference. The AVX2 kernels follow the scalar code's float operations in the
same order: `tw_sat` as `x·(27 + x·x) / (27 + (9·x)·x)`, the clamp as
compare-and-blend so NaN passes as it does in the scalar code, the finite check
with ordered compares, every FIR tap including the zero taps, and the
reset/zero path. Only the vector functions carry `target("avx2")`, with no FMA,
so the scalar reference keeps the production flags (`-O2 -ffp-contract=off`)
and its production code generation in the timed scalar trials.

- `--check`: 2,000,000 calls × 8 lanes per kernel. Inputs are mostly ordinary,
  with about 3% edge values (±0, ±3 and its neighbours, ±FLT_MAX, denormals,
  ±Inf, NaN, random bit patterns) in the signal, `g`, drive and resonance. The
  ladder compares output, all four states, `y4_guess` and reset counts
  bit-for-bit; 172,130 resets were exercised. The FIR compares every decimated
  output. GCC 16.2.1, Clang 22.1.8 and Clang ASan/UBSan/float-cast-overflow
  all pass.
- `--bench`: 4,096 calls × 8 lanes per trial, 31 trials, median, with user-space
  cycles and instructions via `perf_event_open`. Inputs are an audio-like walk,
  with `g` from `filter_prewarp` over 60 Hz–12 kHz and fixed drive/resonance per
  lane. The scalar side calls eight separate `ma_synth` voices as the pool
  does. Outputs were compared bit-for-bit in every bench run. There were three
  epochs on CPU 2 (taskset), with sleep inhibited.

Run: `build/ma3-simd-spike-20261106-01/` (source copy, binary and SHA256,
`bench-epoch{1,2,3}.csv`). The spike source SHA256 is `d752f79f…`, and the
voice source is OPT3 (`3f99f5d7…`).

## Results

Cycles and instructions per voice-call, as medians, the same in all three
epochs to within 0.3%:

| Kernel | Scalar cycles | 8-lane cycles | Speedup | Scalar instr. | 8-lane instr. |
|---|---:|---:|---:|---:|---:|
| ladder 2x | 419.6–420.7 | 70.6–70.7 | 5.94–5.96x | 365.9 | 42.0 |
| FIR 31-tap (push + decimate) | 103.8–104.9 | 11.5–11.6 | 8.99–9.04x | 329.2 | 41.0 |

The ladder gains less than 8x because `vdivps` on 8 lanes costs more than
`divss` on one. The FIR gains more than 8x because one shared ring index serves
all eight lanes.

## Whole-chain estimate (Amdahl)

Per output sample, a voice makes 2 ladder calls and 6 decimations: 1,465 scalar
cycles and 211 with 8 lanes. The estimate leaves out the six extra pushes. Set
against the baseline's median cost per voice-sample:

| Case | Cycles / voice-sample | Stage share | After | Whole chain |
|---|---:|---:|---:|---:|
| Raster / BCS off | 4,284 | 34.2% | 3,030 | 1.41x |
| Tepih / BCS off | 5,101 | 28.7% | 3,846 | 1.33x |
| Prizma / BCS off | 5,969 | 24.5% | 4,714 | 1.27x |
| Granica / BCS on | 6,836 | 21.4% | 5,582 | 1.22x |

All ten cases fall between 1.22x and 1.41x. Isolated kernels run with warm
caches and trained predictors, and in the real chain out-of-order overlap with
neighbouring work differs. The shares are therefore estimates, not
measurements. Only an integrated A/B run on the bench decides.

## What integration would take

- **Stage order per output sample.** The FIR cascade and the ladder are
  feed-forward from per-voice inputs. So each voice can first compute its scalar
  part (controls, envelopes, prewarp, BCS, the eight oscillator substeps,
  mozaik, raster), and the eight voices then run the FIR cascade, ladder and
  7-tap output FIR together. Each voice keeps its own float operation sequence.
  BCS reads the previous sample's `ladder.y4_guess`, which stays available.
- **State layout.** Ladder state and FIR rings become lane-major (SoA). Ring
  positions advance only while a voice is awake, so a voice waking out of step
  needs its ring rotated to the shared position. That rotation is
  content-preserving, so it stays bit-exact.
- **Sleeping slots.** Lanes of sleeping slots must not advance their state.
  Either mask the state update or skip the vector step when no lane is awake.
- **Target.** AVX2 (x86-64-v3) becomes a declared requirement for this path, as
  the plan demands for SIMD. A scalar fallback stays the reference.

## Next

The two stages are about a quarter to a third of the per-voice cost. Most of the
rest is outside them, presumably the eight oscillator substeps per sample, but
that is not measured separately yet. The oscillator is the harder stage: q48
fixed-point phases, `uint64`↔`float` conversions that AVX2 cannot do directly,
and sync and polyBLEP branches. The open decision is between two orders: first
integrate these two stages and measure the real gain on the bench, or first run
an oscillator feasibility spike, which decides whether the full target is
reachable with SIMD at all.
