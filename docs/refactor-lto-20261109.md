# Link-time optimization as the default build (2026-11-09)

Refactor step R0: generated header dependencies, a flags stamp, and an `LTO`
switch (`c2b01d3`). This note records the speed A/B that made `LTO=1` the
default. The point of LTO is the next step: it lets the Mamut voice split
across modules without the code-generation loss that kept raster, BCS and
the oscillators in `ma_voice.c` (`docs/ma-dev-journal.md`, 2026-11-07).

## Identity

`make test` built with GCC `LTO=0`, GCC `LTO=1` and Clang `LTO=1`, and
`make sanitize`, print the same pinned hashes and check counts as the tree
before R0. LTO keeps IEEE semantics: `-ffp-contract=off` holds across the
link, and fusion still happens only where the source calls `ma_fmadd`.

## Speed A/B (quiet host)

Run `build/refactor-r0-lto-ab-20261109-01/` (`run-ab.sh`, `ab_table.py`,
`ab-summary.md`, binary SHA256s). A = `LTO=0`, B = `LTO=1`, same source
(`c2b01d3`). Harness of the 2026-11-08 AVX2 A/B: `bench_ma3_perf`, 8 slots,
128-frame blocks, scenario 3, 2048 measured blocks, user-space PMU cycles,
`taskset -c 2`, three epochs with alternating order, both engines.

| Engine | Geometric mean A/B | Range | B/A instructions | PCM A = B |
|---|---:|---:|---:|---|
| auto (lanes) | 1.030× | 1.021–1.049× | 0.948–0.958 | 20/20 |
| scalar | 0.998× | 0.987–1.008× | 1.004–1.007 | 20/20 |

The lanes are faster in every case. The scalar engine is neutral; the worst
case, Raster with BCS off, is 1.3 % slower, inside the refactor gate
(median no worse than +1 %, no case worse than +2 %). Scalar deadline misses
at 8 slots are where they were: that engine already runs near the 128-frame
deadline.

## Build notes

- `LTO=0` builds without LTO. `sanitize` and `analyze` pin `LTO=0`
  (`-fanalyzer` analyses at compile time).
- The core-symbol audit links with the LTO plugin and
  `-flinker-output=nolto-rel` (GCC), and fails if the combined object
  defines no core code. A plain `-r` over slim LTO objects would pass empty.
- `$(BUILD)/.flags` changes when `CC`, `LTO` or the flags change, and every
  target depends on it, so switching `LTO` in one build directory rebuilds
  everything once.
