# AVX2 as the x86-64 baseline, 2026-11-08

**Decision.** The operator decided that SIMD is first class: x86-64 builds
may require AVX2.

**Change.** Every x86-64 object, core included, compiles with
`-march=x86-64-v3` (AVX2, FMA, BMI1/2, F16C, MOVBE; Haswell 2013 and
later).
- `Makefile`: `ARCH_CFLAGS ?= -march=x86-64-v3` when `$(CC) -dumpmachine`
  names x86_64, added with `override CFLAGS +=`. The `sanitize` and
  `analyze` targets replace `CFLAGS` on the command line, and the override
  keeps the flag in them too.
- `ARCH_CFLAGS=` builds the former baseline, for A/B work.
- `-ffp-contract=off` stays, so the compiler still does not fuse a
  multiply and an add into FMA on its own. The flag only lets it use AVX2
  everywhere, including auto-vectorisation of scalar code. At `-O2` that
  never reassociates floating-point arithmetic.
- `README.md` records the requirement.

**Why the old rule existed.** The MA3-PERF plan and the `ma_voice.c`
refactor plan required correct operation without AVX2:
- for portability of the freestanding core;
- for pre-Haswell x86;
- so that whole-file `-mavx2` could not let auto-vectorised non-lane code
  crash on such hosts despite the runtime dispatch.

The first two are now settled for x86. The third no longer applies,
because every x86 binary requires AVX2.

**Unchanged for now.**
- The lane code keeps its `ML_TARGET` (`target("avx2")`) attributes and the
  CPUID check in `ma_lanes_supported`. Under the new baseline both are
  redundant but harmless.
- The scalar engine stays as the bit-exact reference for every lane gate.
- On a CPU without AVX2 a binary now stops with an illegal instruction
  instead of falling back to scalar.
- Non-x86 builds get no `-march` flag.

## Verification

Every object was rebuilt (`make -B`). The flag appears on every compile
line.

| Gate | Result |
| --- | --- |
| GCC `make test` (with `check-core-symbols`) | pass; core 111378, hosted 116, MIDI map 22, architecture 227, character 8700, stereo 2611; all lane, OPT, Glas and pool gates pass, all pinned signatures unchanged |
| Clang `make test-clang` | pass, same counts |
| Clang ASan/UBSan/float-cast-overflow | all test binaries pass with the same counts and every lane gate bit-identical; see the note below |
| Hurt industrial stems | `--rhythm ma3 --parts kick,pulse` → `de1f65c0459dc3d6`, `--gtr-fat --parts gtr` → `ab0f4ab5ccc91f67`, `--vocal-fat-all --parts vocal` → `7d28100b09ed230f`; all equal to the earlier baseline renders |

Sanitize note: `make sanitize` stops at `check-workbench-symbols`, the
Workbench's realtime-symbol audit (`workbench/workbench.mk`). That audit
sees the sanitizer runtime's own `__asan`/`__ubsan` symbols in any
sanitized build, so it fails with or without this change. The main test
recipe therefore did not run. A `make -B -k` sanitized rebuild followed by
running every test binary directly passes. The Workbench audit should
skip sanitized builds; that file belongs to the Workbench work.

## Speed A/B (quiet host)

Run `build/ma3-avx2-ab-20261108-01/` (`run-ab.sh`, `ab_table.py`,
`binary-sha256.txt`).
- A is `ARCH_CFLAGS=` (`a6cfc68e…`) and B is `-march=x86-64-v3`
  (`cdda4402…`), built from the same source tree, Glas-on-lanes included.
- The protocol repeats the 2026-11-06/07 runs: ten patch/BCS cases, 8 slots
  held, wet, 128-frame blocks, 256 + 2,048 blocks, PMU required, CPU 2
  under `taskset`, three epochs with A/B order alternating, both engines.
- The host was idle: no VS Code, no browser, the compositor at about 2 %.
- Total wall time 10 min 9 s.
- PCM signatures A = B in every case.
- CPU misses are 0 on the lane engine. The wall misses (6–25 per 12,288
  blocks) are preemption, not compute.


#### Engine auto

| Case | A Mcycles | B Mcycles | A/B | B/A instr. | B CPU p99 ms | B max ms | B CPU / wall misses | epoch spread A / B | PCM A = B |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Tepih / BCS off | 1.319 | 1.284 | 1.027x | 1.020 | 0.740 | 1.272 | 0 / 9 | 0.80% / 0.45% | yes |
| Tepih / BCS on | 1.549 | 1.531 | 1.012x | 1.015 | 0.826 | 1.502 | 0 / 7 | 0.79% / 1.01% | yes |
| Lead / BCS off | 1.405 | 1.374 | 1.022x | 1.026 | 0.736 | 0.930 | 0 / 6 | 0.53% / 0.26% | yes |
| Lead / BCS on | 1.518 | 1.494 | 1.016x | 1.018 | 0.798 | 1.429 | 0 / 8 | 0.29% / 0.42% | yes |
| Raster / BCS off | 1.328 | 1.319 | 1.007x | 1.029 | 0.659 | 1.161 | 0 / 6 | 1.66% / 0.36% | yes |
| Raster / BCS on | 1.446 | 1.439 | 1.005x | 1.022 | 0.680 | 1.292 | 0 / 7 | 2.10% / 0.35% | yes |
| Prizma / BCS off | 1.625 | 1.584 | 1.026x | 1.021 | 0.852 | 1.413 | 0 / 10 | 2.99% / 0.14% | yes |
| Prizma / BCS on | 1.731 | 1.699 | 1.019x | 1.015 | 0.924 | 1.787 | 0 / 25 | 0.19% / 0.21% | yes |
| Granica / BCS off | 1.574 | 1.556 | 1.012x | 1.026 | 0.831 | 1.628 | 0 / 6 | 0.13% / 0.78% | yes |
| Granica / BCS on | 1.698 | 1.672 | 1.015x | 1.019 | 0.865 | 1.500 | 0 / 8 | 1.84% / 0.40% | yes |

Geometric mean A/B (auto): 1.016x, range 1.005–1.027x

#### Engine scalar

| Case | A Mcycles | B Mcycles | A/B | B/A instr. | B CPU p99 ms | B max ms | B CPU / wall misses | epoch spread A / B | PCM A = B |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Tepih / BCS off | 4.603 | 4.350 | 1.058x | 0.882 | 2.229 | 2.769 | 1 / 84 | 0.17% / 0.17% | yes |
| Tepih / BCS on | 5.901 | 5.455 | 1.082x | 0.874 | 2.583 | 2.986 | 41 / 179 | 0.31% / 0.06% | yes |
| Lead / BCS off | 4.972 | 4.566 | 1.089x | 0.879 | 2.213 | 2.608 | 0 / 81 | 0.12% / 0.10% | yes |
| Lead / BCS on | 6.026 | 5.500 | 1.096x | 0.873 | 2.528 | 2.962 | 36 / 175 | 1.75% / 0.14% | yes |
| Raster / BCS off | 3.865 | 3.684 | 1.049x | 0.908 | 1.821 | 3.119 | 1 / 26 | 0.58% / 0.60% | yes |
| Raster / BCS on | 4.761 | 4.463 | 1.067x | 0.899 | 2.099 | 2.653 | 0 / 33 | 0.21% / 0.35% | yes |
| Prizma / BCS off | 5.503 | 5.044 | 1.091x | 0.898 | 2.488 | 3.058 | 20 / 174 | 0.06% / 0.35% | yes |
| Prizma / BCS on | 6.253 | 5.785 | 1.081x | 0.891 | 2.698 | 3.245 | 139 / 283 | 0.17% / 0.19% | yes |
| Granica / BCS off | 5.484 | 5.017 | 1.093x | 0.898 | 2.477 | 2.935 | 18 / 124 | 0.51% / 0.51% | yes |
| Granica / BCS on | 6.408 | 5.878 | 1.090x | 0.891 | 2.771 | 3.200 | 222 / 374 | 0.48% / 0.05% | yes |

Geometric mean A/B (scalar): 1.079x, range 1.049–1.096x


- **Lanes:** 1.6 % faster on average, with slightly more instructions. The
  hand-written lanes were AVX2 already, so the gain is in the scalar glue
  around them. The worst case is now 1.699 M cycles (Prizma, BCS on),
  against 1.737 M on 2026-11-07.
- **Scalar engine:** 7.9 % faster with 9–13 % fewer instructions. This is
  the compiler using AVX2, FMA-free arithmetic and BMI in ordinary code.
