# MA2-PERF — five-card render cost

Date: 2026-09-05. Status: done. This slice precedes MA2-4; it does not close MA2 or
change the oscillator/filter algorithms, voice lifetime or factory patches.

## Task ledger

| Task | Status | Closing check |
| --- | --- | --- |
| PERF-1 | done | Hosted C benchmark: fixed scripts, separate CPU/wall timing, two-run PCM identity and pre-change signatures. |
| PERF-2 | done | Profile the current five-card path and identify a bounded optimization. |
| PERF-3 | done | Reuse VCO2 preview and prepare invariant source controls once per public sample; all five pre-change PCM signatures preserved. |
| PERF-4 | done | GCC/Clang regression, sanitizers, oscillator alias referee and before/after cost. |
| PERF-5 | done | Results, limitations and next task recorded in the development journal. |

Timing is host evidence, not Raspberry Pi certification. The 48 kHz,
128-frame deadline is 2666.67 us; the eventual target reserves half of it
for rendering. No machine-dependent timing threshold belongs in unit tests.

## Benchmark contract

```sh
make CC=/usr/bin/gcc bench-ma
```

`driver/bench_ma.c` runs five fixed scenarios twice, with fresh caller-owned
state: idle Tepih, five-note Tepih, five-note Granica with BCS off/on, and
Lead with expression, LFO, glide, sustain, stealing, unison and panic events.
Each pass warms up for 256 blocks and measures 1024 blocks of 128 frames at
48 kHz. All five cards continue ticking, including idle and released cards.

The timed region contains bank rendering and writes to a fixed frame array.
Event dispatch, PCM validation/hashing, sorting and printing are outside it.
CPU time uses `CLOCK_THREAD_CPUTIME_ID`; elapsed time uses `CLOCK_MONOTONIC`.
Clock-call overhead is included; this is a whole-bank benchmark, not isolated
primitive timing or end-to-end ALSA latency. P99 is the sorted observation
at index `1024 * 99 / 100`. Both passes are printed, never best-run selected.
Warmup PCM is included in the signature, but warmup timing is discarded.

The benchmark exits unsuccessfully on non-finite PCM, a clock error, a
two-pass mismatch or a mismatch with the pre-slice signature. FNV hashes
cover left/right f32 bytes in frame/slot order; the recorded hashes use
little-endian hosts. Timing has no pass/fail threshold and `bench-ma` is
explicitly separate from `make test`.

## Baseline provenance

The starting worktree already contained uncommitted MA2-2/3, Raster and BCS
work over HEAD `caa0065`. HEAD alone cannot reconstruct this baseline.
Before changing core code, source copies and objects were preserved under
ignored `build/perf-before/`; source SHA-256 values were:

| File | SHA-256 |
| --- | --- |
| `src/ma_voice.c` | `e02ac3c5e535ff268b4ba593a500f617ec1792f8ab9cbe03479c150133b80516` |
| `src/ma_bank.c` | `3af8be72f7d2dee0d947840a66674f2faff8817a75c4e3452d8029c4d2036c55` |
| `src/mamutanalog.h` | `081aeb3a6111fc49ea6cabffddc4af91b91b4c530e25e44992469b9ac7b65d70` |
| `src/drive.c` | `a9833be73d3a77049687e8011e70e931ac42b4a3cdc1d3e51121f5b32c6dae05` |
| `src/ma_raster_table.h` | `d2a2710798aa538d3a5e6539067ab14dfc44d6ca1b6b8f487895a18549cc0d14` |
| `src/tonewheel.h` | `b4a0c496fffd918ad2d6113003366d2748d30844cfb8874f03112bc264237cbc` |

The baseline binary first checked two-run identity; its signatures were then
pinned in the benchmark before the core changed:

| Scenario | FNV-64 |
| --- | --- |
| Tepih idle | `c4c2a0b9a1f22325` |
| Tepih five | `8b94b7d526875ec1` |
| Granica BCS off | `fc137a48a372ec55` |
| Granica BCS on | `3e5cf262b15baaf1` |
| Lead gesture | `83b5c4826511163d` |

## Profile and implementation

A separate GCC `-O2 -g -pg -ffp-contract=off` build ran the same scenarios.
`gprof` attributed 19.39% self time to `render_source_substep`, 5.12% to
`render_oscillator`, and substantial additional time to their waveform and
sine helpers. Another 34.21% was inside `ma_synth_tick`, including inlined
work. Inlining and compiler function merging limit primitive attribution;
instrumented timing is not compared with release timing.

Profile recipe (run before changing the source for baseline attribution):

```sh
make CC=/usr/bin/gcc BUILD=build/perf-profile \
  CFLAGS='-std=c23 -O2 -g -pg -Wall -Wextra -Wpedantic -ffp-contract=off' \
  LDFLAGS=-pg build/perf-profile/bench_ma
GMON_OUT_PREFIX=build/perf-profile/gmon build/perf-profile/bench_ma
gprof build/perf-profile/bench_ma build/perf-profile/gmon.*
```

Use one profile run per output directory when inspecting this recipe.

Source inspection confirmed two redundant computations in that hot path:

- Cross-mod previews VCO2 before sync, then its renderer recomputed that
  same preview. Both oscillators now pass a precomputed preview into the
  state-advancing renderer. Triangle initialization/advancement, sync
  residuals, clamp ordering and phase updates retain their previous rules.
- VCO2 fine/interval pitch and Q48 step, envelope pitch ratio, modulated
  pulse widths and effective sync are constant over eight source substeps.
  A private stack value now prepares them once per public frame, after the
  existing smoother/BCS/envelope updates. VCO1 cross-mod pitch and both
  oscillator states still advance at 8x.

No reciprocal substitution, reassociation, fast-math, skipped idle card,
changed decimator, lower oversampling ratio or new public state is involved.
`sizeof(ma_synth)` stays 1704 bytes and `sizeof(ma_card_bank)` stays 8624
bytes on this host.

## Host measurement, 2026-09-05

Intel i7-4600U, Linux x86_64, GCC 16.1.1 (Fedora build 20260515),
`-std=c23 -O2 -ffp-contract=off`, no LTO or fast-math. Both executables used
the same benchmark script. The comparison below ran the preserved baseline
and then the optimized binary on logical CPU 2 with `taskset -c 2`.
Affinity prevents migration, but does not isolate the CPU, lock frequency,
remove SMT contention or grant realtime scheduling. No tests or profiling
processes were launched alongside this timed pair.

CPU means in microseconds; both passes are shown. The reduction uses the
arithmetic mean of both passes, not the faster pass.

| Scenario | Before, passes 1 / 2 | After, passes 1 / 2 | Mean reduction |
| --- | ---: | ---: | ---: |
| Tepih idle | 1412.02 / 1599.44 | 1073.96 / 1072.90 | 28.7% |
| Tepih five | 1408.41 / 1400.87 | 1114.31 / 1121.00 | 20.4% |
| Granica BCS off | 1675.79 / 1643.74 | 1306.32 / 1346.17 | 20.1% |
| Granica BCS on | 1873.16 / 1837.38 | 1491.36 / 1490.96 | 19.6% |
| Lead gesture | 1383.44 / 1409.52 | 1157.47 / 1160.70 | 17.0% |

Tail observations in microseconds, also retaining both passes:

| Scenario | Before CPU p99 | After CPU p99 | Before wall p99 | After wall p99 |
| --- | ---: | ---: | ---: | ---: |
| Tepih idle | 2257.15 / 2196.81 | 1398.92 / 1316.36 | 2558.59 / 2976.45 | 1915.42 / 1782.19 |
| Tepih five | 1827.24 / 1792.99 | 1324.35 / 1505.58 | 2498.78 / 2548.44 | 1376.60 / 1855.62 |
| Granica BCS off | 2081.92 / 2040.93 | 2023.93 / 2105.63 | 2767.97 / 2809.04 | 2207.18 / 2361.07 |
| Granica BCS on | 2648.60 / 2180.64 | 1828.49 / 1732.43 | 2937.60 / 2711.80 | 2225.72 / 2217.26 |
| Lead gesture | 1678.76 / 2167.75 | 1577.85 / 1604.25 | 2051.86 / 2355.42 | 2136.14 / 2063.59 |

The observed active-case mean improvement is 17–20%; tails remain noisy and
do not improve uniformly. This is a local comparison, not a universal speedup
or worst-case execution-time bound. The earlier discussion's ad hoc timings
were taken under different host conditions and are not the baseline here.

Local ignored artifacts: `build/ma-perf-before.log`,
`build/ma-perf-after.log`, `build/ma-perf-before-pinned.log`,
`build/ma-perf-after-pinned.log`, `build/ma-perf-profile.log` and
`build/ma-perf-profile.txt`. The first unpinned optimized trial was noisier;
the affinity-pinned pair above is the final reported comparison.

## Validation

- GCC 16.1.1 and Clang 22.1.8: aggregate `test` passes core `111378`,
  hosted `116`, MIDI map `22` and architecture `227`, zero failures.
  Both optimized core-symbol audits pass.
- Clang ASan/UBSan/float-cast-overflow, `-O1 -g`, leak detection disabled
  as required by the existing repository sanitizer target: the same
  aggregate counts and all five benchmark signatures pass without sanitizer
  findings. Sanitizer timings are excluded from performance comparisons.
- GCC and Clang `bench-ma`: all five pinned pre-change signatures and
  two-run checks pass. Compiler timing is not compared across sessions.
- GCC and Clang `exhibit-ma1-osc`: all 16 cases pass. The eight ordinary,
  sync and cross-mod cases retain 21.86–30.00 dB alias-energy reduction;
  both sine paths retain their H2/H3/H5 contract at all four rates.

Commands used the native targets with explicit `/usr/bin/gcc` and
`/usr/bin/clang` to avoid the environment's compiler-cache wrapper:

```sh
make CC=/usr/bin/gcc test exhibit-ma1-osc
make CC=/usr/bin/clang BUILD=build/clang test bench-ma exhibit-ma1-osc
ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang BUILD=build/sanitize \
  TEST_EXTRA= \
  CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test bench-ma
git diff --check
```

## Remaining boundary

This slice does not close the 1333.33 us half-deadline budget: Granica's mean
alone is about 1491 us, and even Tepih exceeds that budget in tail samples.
The bank still computes silent cards to preserve their continuous state.
Shared stereo body, chorus, reverb and GFM are not present in this benchmark;
the five-note fixed chord is not a worst-case full-domain sweep. Raspberry
Pi timing and the 30-minute live soak remain open. MA2-4 is the next
functional task, with further cost work required before the MA5 target gate.
