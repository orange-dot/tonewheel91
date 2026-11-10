# Sync step and triangle integrator, 2026-11-10

**Decision.** The operator approved two corrections in the Mamut Analog
oscillators, found on 2026-11-09 while tracing the source code. Both change
PCM. Baseline: `e3d2ce2`.

## What was wrong

**Hard sync lifted the output after a reset.** The correction after an edge
is read off the phase (`ma-constants.md` 3.1). A sync reset that landed
within one edge zone of phase zero therefore also got the correction of a
wrap that had not happened, on top of the sync residual. Measured on
`ma_voice_render_source_substep`, VCO1 600 Hz, saw slave at ratio 2.37,
full sync, 8x substeps 638 to 643:

| | 638 | 639 | 640 | 641 | 642 | 643 |
|---|---:|---:|---:|---:|---:|---:|
| before | −0.275 | −0.267 | +0.370 | −0.225 | −0.435 | −0.618 |
| after | −0.275 | −0.267 | −0.630 | −0.993 | −0.985 | −0.978 |

**The triangle lost level and gained an offset with pitch.** It integrates
the smoothed 50% pulse, and smoothing takes `1.25 e` of each half period's
area (`e = 8 d`). Triangle alone, steady pitch, 10 s of 8x source:

| Pitch | high before | mean before | high after | mean after |
|---|---:|---:|---:|---:|
| 110 Hz | +0.9943 | −0.0029 | +1.0000 | −0.0000 |
| 440 Hz | +0.9771 | −0.0115 | +1.0000 | −0.0001 |
| 1760 Hz | +0.9082 | −0.0462 | +0.9998 | −0.0004 |
| 3520 Hz | +0.8163 | −0.0924 | +0.9997 | −0.0008 |

The low end was −1.0000 in every case. The offset entered the nonlinear
ladder ahead of the DC block.

## Change

The contract is in `ma-constants.md` 3.1 and 3.2.

- **Triangle.** The integrator runs at `4 d / (1 − 1.25 e)`. Its state
  starts on `seed(p, d)`, the steady trajectory at that phase, both for a
  new voice and after a sync reset. Under cross-mod VCO1 keeps the gain of
  its unmodulated step.
- **Sync.** A reset moves the phase, not the edges VCO2 has already passed.
  The half of a correction that follows an edge runs on the distance since
  that edge was really crossed (`sync_run_q48`, `sync_carry[3]`). The step
  that the two-substep residual spreads is measured on the smoothed output,
  not on the raw waveform.
- **A reset is tracked only when it touches an edge zone**, and for one zone
  of run. Other resets leave the ordinary code path exactly as it was.
- A voice's triangle is seeded once per frame, before its substeps
  (`ma_voice_seed_triangles`), instead of being tested in every substep.
- Code: `src/ma_osc.c`, `src/ma_lanes.c`, `src/ma_voice_impl.h`,
  `src/mamutanalog.h` (one `uint64_t` and three `float` per oscillator, the
  same per lane for VCO2). The held and reset paths are out of line
  (`[[gnu::noinline, gnu::cold]]`): inlined, their presence alone cost the
  scalar substep about 11 instructions.

`ma_synth` grows from 2,608 to 2,656 bytes and `ma_voice_pool` from 880,680
to 885,080.

## Evidence

**Gate.** `make check-ma-sync` (`test/ma_sync.c`, part of `make test`):
- 39,732 sync events (20,157 tracked) over 528 cases: after a reset the 8x
  output never rises above both its value before the event and the raw saw.
  The same test built against `e3d2ce2` fails this check.
- A weakly synced slave that resets just after its own wrap moves at most
  0.229 per substep, the same as an unsynced one.
- The triangle spans −1 to 1 with a mean under 0.004 at four pitches and
  five start phases.

**Scalar and lanes.** `test/ma_pool.c` now also runs a hard-sync variant and
a cross-mod, soft-sync variant with every shape sounding: bit-identical PCM,
slot state and diagnostics over 223,603 frames. `test/ma_front_lanes.c`
compares the new source-frame fields.

**Suites.** `make test`, `make test-clang` and `make sanitize` pass.

**Error against the ideal synced waveform.** Slave under a silent 600 Hz
master, one master period of 8x source, harmonics 1 to 33 (to 19.8 kHz),
complex error relative to the signal in dB
(`build/ma-source-fix-20261110/probes/`). About −25 to −30 dB is the floor
that the edge smoothing itself leaves.

| Ratio, sync | saw before | saw after | pulse before | pulse after |
|---|---:|---:|---:|---:|
| 2.37, 1.0 | −21.7 | −26.1 | −23.9 | −26.6 |
| 1.61, 1.0 | −22.0 | −28.5 | −25.8 | −29.2 |
| 3.3, 1.0 | −21.0 | −24.1 | −22.2 | −24.6 |
| 1.5, 1.0 | −22.3 | −28.8 | −25.5 | −28.6 |
| 2.0, 1.0 | −25.4 | −26.3 | −26.8 | −27.2 |
| 1.99, 1.0 | −23.1 | −24.7 | −25.6 | −26.6 |
| 2.01, 1.0 | −21.3 | −26.3 | −24.2 | −27.3 |
| 1.004, 1.0 | −22.9 | −29.2 | −26.1 | −30.4 |
| 0.996, 1.0 | −24.9 | −27.5 | −27.7 | −29.4 |
| 2.37, 0.5 | −25.3 | −25.3 | −27.0 | −27.0 |
| 1.004, 0.25 | −29.1 | −29.2 | −30.3 | −30.3 |

Every full-sync case improves. The two partial-sync cases, whose resets
touch no zone or only continue a real wrap, are unchanged.

**Alias table** (`make exhibit-ma1-osc`, all rows PASS before and after):

| Case | clean before | clean after |
|---|---:|---:|
| hard-sync-saw-n96 | −32.54 dBc | −32.51 dBc |
| hard-sync-pulse-n96 | −38.46 dBc | −40.46 dBc |
| crossmod-low-n96 | −39.38 dBc | −39.33 dBc |
| crossmod-high-n96 | −39.65 dBc | −39.07 dBc |

The other rows are unchanged. The lift was a harmonic error, not an alias,
so this table barely sees it. The cross-mod rows move because the modulating
VCO2 carries a triangle.

**What did not change.** With both triangle levels and `sync_amount` zeroed,
Tepih, Raster, Prizma and Granica render the same PCM as `e3d2ce2`, one
voice and a 16-slot pool (`probes/identity.c`). Lead does not: its Ruin
macro adds sync through `baklja_sync_bias`.

## Speed

Run `build/ma-source-fix-ab-20261110-02/`, the harness of
`docs/refactor-20261109.md`: `bench_ma3_perf`, 8 slots, 128-frame blocks,
2048 measured blocks, user-space PMU, `taskset -c 2`, three epochs with
alternating order. A = `e3d2ce2`, B = this change, both `LTO=1`.

| Engine | Geometric mean A/B | Range | B/A instructions |
|---|---:|---:|---:|
| auto (lanes) | 1.018× | 1.009–1.037× | 0.987–0.991 |
| scalar | 0.973× | 0.955–1.010× | 1.008–1.024 |

- The lanes are faster in every case: seeding once per frame takes the seed
  select and its test out of every substep.
- **The scalar engine is 2.7 % slower, outside the refactor gate** (median
  no worse than +1 %, no case worse than +2 %). It executes 0.8 to 2.4 %
  more instructions: one test per substep for a tracked reset, two
  divisions per frame for the gains, and two smoothed waveform evaluations
  per sync event where there were two raw ones.
- The host was not quiet during this run: the lanes show wall-deadline
  misses that the 2026-11-09 runs did not, and epoch spread reaches 56 %.
  The instruction counts are exact; the cycle ratios are not better than
  about ±1 %.

A first version of the change cost the lanes 3.6 % (run
`build/ma-source-fix-ab-20261110-01/`). It tracked every reset for half a
turn and tested the seed in every substep.

## Pins

Re-pinned, with the former value beside each in the test source:

| Test | Before | After |
|---|---|---|
| OPT1 44.1k | `197bf00474500c71` `7513c44321960fde` | `e999ae4321336917` `6dfd5f8740e18c56` |
| OPT1 48k | `6e79bf558ae579a1` `49eaf39b776f0150` | `c608dafee46ee308` `d3f54225c69ed872` |
| OPT1 96k | `c1d0b84e9a28b3e0` `af0262cd65f4ed48` | `56de8b379780b876` `9537c7b60913e413` |
| OPT1 192k | `0c4310bcbe45c9d6` `08887958c05709b5` | `a272d407f942c0d0` `dd27c0eefa95d9f6` |
| OPT2 44.1k | `5262c9e6c3cc9207` `e6de524d40dc9ab9` | `284d166a4da21025` `6124ae21b44802b4` |
| OPT2 48k | `c2ebb621dc1669aa` `bdaa40ec38669ef4` | `39a48be0acd83655` `0a83b3312f79c7b4` |
| OPT2 96k | `a69a22e603814f20` `1634fa6c7098f2f0` | `dd4718ca7d66ea1a` `aadc5a57fe1b0635` |
| OPT2 192k | `9496fc5eb55954c0` `83a8b889daa8cf8d` | `ffca6acce565b191` `e3c3398a33e309de` |
| pool unison 1, 2 | `4946fd6b8aa8a5d5` `4cf48dbe261e8c40` | `b1f9361a37fc1173` `6dfe4219354a7bb7` |
| pool unison 4, 8 | `a68d44f296ff0a63` `d805ee48df95e9bc` | `6b185eb6af0320f4` `7676e8e9dbb2457d` |
| MA1-5 zero-Mozaik anchor | `b2e59619a2a55cb1` | `75cf9d06e4cc7685` |
| VCO1 sine-zero anchor | `a63f63bdc633854d` | `65eb97a686df561d` |
| MA3-1 SMF twin | `8cbb995144bc5270` | `77b6939786ac9661` |
| MA3-2 wet MIDI twin | `e95149977684d494` | `eca2f01ea356eda1` |

The eight MA3-2 effects hashes and every organ and EP signature are
unchanged. Hashes recorded in earlier evidence documents and in
`docs/renders.md` describe the code of their date.

## Listening pairs

`build/ma-source-fix-20261110/`, 12 s each, `patchlab --render`, same
command on `e3d2ce2` (`old-`) and on this change (`new-`):

| Pair | Patch | Difference re old |
|---|---|---:|
| `tepih` | factory | −47.6 dB |
| `dubina` | factory | −68.2 dB |
| `lead` | factory, sync 0.22 with cross-mod | −24.9 dB |
| `triangleonly` | Lead with VCO1 triangle only | −48.0 dB |
| `syncseven` | Lead with full sync, VCO2 a fifth up | −1.4 dB |

The last figure is not a level change (RMS 0.00633 both): under cross-mod
VCO2's waveform steers VCO1's phase, so the two renders drift apart in
phase.

## Not done

- The sync step still spreads over two substeps, against sixteen for an
  ordinary edge. That is what holds hard-sync-saw at −32.5 dBc. A wider
  kernel needs the reset predicted, which cross-mod prevents.
- The scalar engine's 2.7 % is not recovered.
- No patch was retuned for the fuller triangle.
