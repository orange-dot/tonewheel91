# MA3-OPT4 — split the 31-tap FIR traversal

Implementation: 2026-10-02; handoff: 2026-10-03.
Status: candidate implemented and functionally validated. The operator later
authorized a [matched performance screen](ma3-opt4-measure-20261003.md);
the mixed result does not establish a speedup or realtime readiness.

2026-11-06: **reverted.** At the operator's request, `src/ma_voice.c` went
back to OPT3 (`3f99f5d7…`) before the
[8-slot baseline](ma3-perf-20261106.md). The description below records the
candidate, which survives in commit `3881094` and in
`build/ma3-opt4-fir-20261002-01/`. `test/ma_fir.c` (`check-ma3-opt4`) stays in
`make test` as an exact-bit guard for any later change to the FIR traversal.

`src/ma_voice.c:decimate_oversample` now visits the circular history in two
descending spans instead of checking for wrap at every tap. For a position
`p` in 0–30, the spans are `p-1..0` and `30..p`. Together they visit exactly
the original 31 indices, in the same order, with one `+0.0f` accumulator.
Every coefficient multiplication and addition remains, including zero taps.
The seven-tap output filter, coefficients and state layout are unchanged.

This is the first candidate selected by the
[P0–P2 baseline and attribution](ma3-perf-20261002.md). It targets generated
wrap/index work; its whole-chain performance benefit remains unknown.
The P3 Raster regression investigation is still pending.

## Correctness evidence

`test/ma_fir.c` compares the actual static function with the original loop
using exact float bits and verifies input-history immutability. It covers
664 history patterns at every circular position: **20,584 comparisons**,
including impulses, signed zero, subnormals, extremes, cancellation and
deterministic finite bit patterns. The white-box test includes `ma_voice.c`
without adding an exported test hook; its target omits `ma_voice.o` to avoid
duplicate definitions.

Independent source review confirmed that the first loop performs `p` taps
and the second `31-p` taps, preserving every history index, coefficient,
product and ordered accumulation. Integer promotion makes the empty first
span at `p=0` safe. No persistent state is written by either traversal.

GCC and Clang native functional suites and freestanding-symbol audits pass,
including the pinned OPT1/OPT2 PCM and hidden-state fixtures at
44.1/48/96/192 kHz, pool/MIDI/effects regressions, the new FIR test and seven
renderer CLI tests. The numerical oscillator referee passes all 16 cases
in both builds. The complete Clang ASan/UBSan/float-cast-overflow functional
suite and all 16 numerical cases also pass; its separate completion log and
successful exit are recorded in the run manifest.

The benchmark CLI target was deliberately excluded, including its short
timed functional checks. Correctness commands, from the repository root:

```sh
make -o check-ma3-perf CC=/usr/bin/gcc \
    BUILD=build/ma3-opt4-fir-20261002-01/after test exhibit-ma1-osc
make -o check-ma3-perf CC=/usr/bin/clang \
    BUILD=build/ma3-opt4-fir-20261002-01/clang test exhibit-ma1-osc
ASAN_OPTIONS=detect_leaks=0 make -o check-ma3-perf CC=/usr/bin/clang \
    BUILD=build/ma3-opt4-fir-20261002-01/sanitize TEST_EXTRA= \
    CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
    LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test exhibit-ma1-osc
```

## Preserved comparison and stop point

Run directory: `build/ma3-opt4-fir-20261002-01/`. The baseline snapshot was
taken before editing the voice source; it retains earlier uncommitted work.
The baseline and candidate benchmark executables were **built only**, with
the same GCC release flags. Neither executable was run in this batch.

| Identity | SHA256 |
| --- | --- |
| Baseline voice | `3f99f5d79d77659676e632ffe5f91e3a9bc69b9448946ef08084226b3c6a0e82` |
| Candidate voice | `aea2d36598bc3dd08f3da2544c84d0237e56b741fe3eacf546c1125dfea67902` |
| Baseline benchmark | `9b5790ce7ef9e6cffc3e0dafd43315cd874a3763d1867cbce8d017ed122ff23b` |
| Candidate benchmark | `bb0d1467456a8b2a646e9885bdcd6af2c21a7a2c8067f618d6a79cda9b5f880c` |

The run preserves source snapshots, diffs, compiler identities, build/test
logs, commands, source/binary hashes and the stop-before-measurement boundary.
The original sanitizer log was interrupted; the completion run is recorded
separately as `sanitize-finish-tests.log`.

This was the original stop point before the later measurement authorization.
The preserved executables were used in the separately named
[A/B screen](ma3-opt4-measure-20261003.md). That screen did not meet the
[performance plan](ma3-perf-plan.md)'s retention gate. Listening and live
acceptance remain pending.
