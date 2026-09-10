# MA3-OPT1 — prepare invariant source controls once per sample

OPT1 extends the existing `ma_source_frame` in `src/ma_voice.c`. Waveform
normalization divisors and source mixer weights are prepared once for the
eight oscillator substeps. VCO2's guard target is set once before those
substeps. When effective cross-mod is exactly zero, VCO1's frequency and
quantized Q48 phase step are also prepared once; nonzero cross-mod still
recomputes pitch at every substep.

All divisions, oscillator state advances, waveform evaluations, filter taps
and oversampling ratios retain their previous semantics. Public state sizes
and patch formats are unchanged. This slice does not implement OPT2, FIR
changes, coefficient caches across samples, or SIMD.

## Reference and verification

The pre-change worktree already contained MA3 MIDI, effects and exhibit work;
HEAD alone is not the baseline. The preserved release objects, executable,
voice source, source checksums and starting diff are in ignored
`build/ma3-opt1-yHOVSa/`. The old/new voice source SHA256 values are:

- Before: `bd26b5162381e95e2ba3f2ffa43fa199cdbfd52fd5b6df0de951a4b9a2340e8d`.
- After: `fb829581ecb942e0ea5316ad31fd2b1d3a64839f145957e3af019f1b1986f915`.

`make check-ma3-opt1` checks eight signatures captured from the preserved
pre-change object. The test covers cross-mod zero/nonzero/zero, sync, waveform
normalization changes, high-note guard transitions, VCO2 intervals, bend,
BCS enable/disable and note releases at 44.1/48/96/192 kHz. Hashes include PCM
and both oscillators' phase, triangle and guard gain, so inaudible state drift
also fails the test. Each scenario renders twice. The check is included in
`make test`.

GCC and Clang release builds pass the complete test suite and freestanding
symbol audits. Clang ASan/UBSan/float-cast-overflow passes the complete suite.
The oscillator alias/harmonic referee passes in all three builds. Existing
pool, unison, MIDI and effects signatures remain unchanged. Logs are
`gcc-tests.log`, `clang-tests.log` and `sanitize-tests.log` in the run folder.
The preserved five-card benchmark signatures also pass in the optimized
release build; `legacy-signatures.log` retains both passes of every case.

## Release timing

Intel i7-4600U, Linux `7.1.8-200.fc44.x86_64`, GCC 16.1.1,
`-std=c23 -O2 -ffp-contract=off`, no profiling instrumentation or LTO.
The four runs used logical CPU 0 in before/after/after/before order with no
concurrent tests or renders. Affinity does not lock CPU frequency or isolate
the physical core from other host activity.

Each run covers five patches, BCS off/on, 32 held slots, unison 1, 48 kHz,
128 frames and the wet arm. Each case has 64 warmup and 256 measured blocks
and renders twice. BCS-on uses Granica's amount/regime, as in the existing
benchmark. The command sequence was:

```sh
for run in before-1 after-1 after-2 before-2; do
    MA3_PERF_WARMUP=64 MA3_PERF_BLOCKS=256 MA3_PERF_LIMIT=32 \
    MA3_PERF_FRAMES=128 MA3_PERF_SCENARIO=3 MA3_PERF_ARM=3 \
        taskset -c 0 "build/ma3-opt1-yHOVSa/${run%-*}/bench_ma3_perf" --run \
        > "build/ma3-opt1-yHOVSa/${run}.csv" || exit
done
```

Means below retain all four observations per implementation/case, including
both render passes. Positive reduction means less CPU time; negative means
more. CSVs retain individual CPU/wall means, p50/p99/max and memory fields.
All 80 rows retain 32 active slots and match the corresponding before/after
PCM hash.

| Patch | BCS | Before CPU ms | After CPU ms | CPU reduction |
| --- | --- | ---: | ---: | ---: |
| Tepih | off | 7.953 | 7.317 | 8.00% |
| Tepih | on | 9.323 | 9.201 | 1.31% |
| Lead | off | 7.732 | 7.802 | -0.90% |
| Lead | on | 9.409 | 9.333 | 0.81% |
| Raster | off | 7.084 | 7.126 | -0.60% |
| Raster | on | 8.539 | 8.353 | 2.18% |
| Prizma | off | 9.333 | 9.690 | -3.82% |
| Prizma | on | 10.680 | 10.351 | 3.08% |
| Granica | off | 9.258 | 9.211 | 0.51% |
| Granica | on | 10.663 | 10.431 | 2.18% |

Tepih without BCS improves 8.87% and 7.15% in the two before/after pairs.
Prizma with BCS improves 2.94% and 3.23%; Granica with BCS improves 1.57%
and 2.78%. Other small differences often change sign between pairs. Prizma
without BCS measures -8.18% in one pair and +0.54% in the other; its aggregate
regression is retained above rather than selecting the favorable run.
The matrix-wide CPU mean decreases 1.29%, which is not evidence of a uniform
speedup across patches. Further focused measurement would be needed to
resolve the noisy cases. No machine-dependent timing threshold is added to
tests. The 2.667 ms block budget is still exceeded at full 32-slot occupancy.

## Listening handoff

The existing MA3-2 exhibit supplies eight-second Tepih and Granica examples,
each with dry/chorus/reverb/wet variants, limit 16, unison 2, overlapping
chords, sustain and panic. Each executable renders every variant twice.
Before and after files use the same session seed and score. Listening gain
uses the existing common -22 dBFS RMS target over 0–4 seconds, subject to
the exhibit's peak ceiling; raw files are retained alongside listening files.

```sh
build/ma3-opt1-yHOVSa/before/exhibit_ma_effects --render build/ma3-opt1-yHOVSa/before/listen
build/ma3-opt1-yHOVSa/after/exhibit_ma_effects --render build/ma3-opt1-yHOVSa/after/listen
```

All sixteen before/after WAV pairs (eight raw and eight level-matched) compare
identically byte for byte. Both render logs also match exactly. Start with:

| Patch | Before | After |
| --- | --- | --- |
| Tepih wet | [listen](../build/ma3-opt1-yHOVSa/before/listen_tepih_wet_listen.wav) | [listen](../build/ma3-opt1-yHOVSa/after/listen_tepih_wet_listen.wav) |
| Granica wet | [listen](../build/ma3-opt1-yHOVSa/before/listen_granica_wet_listen.wav) | [listen](../build/ma3-opt1-yHOVSa/after/listen_granica_wet_listen.wav) |
| Tepih dry | [listen](../build/ma3-opt1-yHOVSa/before/listen_tepih_dry_listen.wav) | [listen](../build/ma3-opt1-yHOVSa/after/listen_tepih_dry_listen.wav) |
| Granica dry | [listen](../build/ma3-opt1-yHOVSa/before/listen_granica_dry_listen.wav) | [listen](../build/ma3-opt1-yHOVSa/after/listen_granica_dry_listen.wav) |

The SHA256 values for both sides of each wet pair are:

- Tepih: `c05d992db8116d9fbc1d20f568285b57e5307d6488f5518f7b6dae1a7e5c75f9`.
- Granica: `6a63465bf3d0579e8697bba8929c62ce09c0060aa7d255cc7696e3f2fb099e62`.

Operator listening acceptance is pending. OPT2 and FIR work remain separate
decisions after this handoff.
