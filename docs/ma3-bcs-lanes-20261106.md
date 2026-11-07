# MA3-PERF vector BCS, 2026-11-06

Eight-lane BCS makes complete BCS-on blocks 1.31–1.37x faster against the
preceding AUTO engine, with identical PCM and effect fingerprints in all ten
patch/BCS cases. This hosted run contains deadline misses; live acceptance
remains open.

## Implementation and verification

`src/ma_voice.c` splits the front end into A (controls and pitch), BCS, and B
(BCS modulation, envelopes and source preparation). The scalar path retains
A → `render_bcs` → B. Lanes run A for active voices, `ml_bcs` for the enabled
mask, then B and the existing lane kernel. State stays in each voice's
`ma_synth`. Parameter interpolation, RK4 operation order and scalar
`reset_bcs` are preserved; floating-point contraction stays disabled.

`test/ma_bcs_lanes.c` is part of `make test` as `check-ma-bcs-lanes`.
It compares output and complete `ma_bcs` state with `render_bcs` after every
active step: **89,752 bit-identical lane-steps, including 304 resets**.
Coverage includes regimes, amounts, frequency/feedback clamps, four sample
rates, partial masks, NaNs, infinities and excessive states. It explicitly
skips without AVX2. The exact final |state| = 32 ceiling is checked by
inspection of the inclusive vector comparison, not covered by random states.

Continuation verification uses `build/ma3-bcs-verify-20261106-01/`:

```sh
CCACHE_DISABLE=1 make -j2 test CC=gcc BUILD=build/ma3-bcs-verify-20261106-01/gcc
CCACHE_DISABLE=1 make -j2 test CC=clang BUILD=build/ma3-bcs-verify-20261106-01/clang
CCACHE_DISABLE=1 make -j2 test CC=clang BUILD=build/ma3-bcs-verify-20261106-01/sanitize \
  CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' TEST_EXTRA=
```

Release builds include the core symbol audit; instrumentation adds runtime
symbols, so the sanitizer run omits that audit.
All three full suites passed, with 111,378 core checks and every pinned
signature intact, no compiler warnings and no sanitizer runtime errors.
GCC/Clang versions were 16.2.1 and 22.1.8. LeakSanitizer required execution
outside the ptrace-based sandbox. Logs are `gcc.log` and `sanitize.log`
under the verification directory, plus `build/ma3-bcs-clang-verify.log`.

## Matched A/B measurement

Run: `build/ma3-bcs-ab-20261106-01/`, completed before this continuation.
`run-ab.sh` preserves the command and `run.log` records completion.
A is `10418a8` (AUTO lanes, scalar BCS); B adds vector BCS, captured in
`B/working-tree.diff`. GCC 16.2.1, `-O2 -ffp-contract=off`, CPU affinity 2,
PMU cycles/instructions required, AUTO lanes active.

Each case holds eight slots at 48 kHz, wet effects, 128-frame blocks.
Three epochs alternate A/B, B/A, A/B. Each invocation has 256 warm-up blocks
and two passes of 2,048 measured blocks: 12,288 measured blocks per variant
per case. Medians below pool raw block samples across epochs and passes.
All six summary rows per case and variant share PCM and post-effect hashes.

| Patch | BCS | A Mcycles p50 | B Mcycles p50 | A/B | B CPU / wall misses |
|---|---|---:|---:|---:|---:|
| Tepih | off | 1.526 | 1.489 | 1.025x | 0 / 0 |
| Tepih | on | 2.688 | 1.963 | 1.369x | 1 / 82 |
| Lead | off | 1.583 | 1.575 | 1.005x | 0 / 3 |
| Lead | on | 2.610 | 1.925 | 1.356x | 0 / 9 |
| Raster | off | 1.623 | 1.602 | 1.013x | 0 / 0 |
| Raster | on | 2.684 | 1.961 | 1.369x | 0 / 2 |
| Prizma | off | 1.907 | 1.906 | 1.001x | 0 / 0 |
| Prizma | on | 2.954 | 2.254 | 1.311x | 0 / 17 |
| Granica | off | 1.887 | 1.863 | 1.013x | 0 / 9 |
| Granica | on | 2.924 | 2.210 | 1.323x | 0 / 12 |

BCS-off medians differ by 0.1–2.5%; bypass receives no vector BCS work.
The worst median falls from 2.954 to 2.254 M cycles: about 1.073 ms at the
2.1 GHz base clock, or 40% of the 2.667 ms deadline.

The tail is not clean: A has 89 CPU and 359 wall misses; B has 1 CPU and
134 wall misses across 122,880 measured blocks each. B's maximum CPU time
is 3.309 ms and maximum wall time 8.995 ms; five B blocks exceed the 2.133 ms
CPU headroom target. `top-after.txt` records Chrome activity on CPU 0/1,
but cannot prove the measured CPU stayed quiet throughout. These data support
cycle improvement, not a no-xrun or isolation claim.

SHA256 identities:

```text
1422eecd4e063192832b309c655d51715f2906b06e82e4f940e7e6a913bf8658  B src/ma_voice.c
902e5dd6a794c70e3e1dadc4d55663de292fb4f893a500440aac6635ef4b4770  src/ma_pool.c
38febbc5ee5d428fe109cd5e84db63fda8555b0005a9d001c8dd9eaec5a919a1  driver/bench_ma3_perf.c
346aa3eaf46fa7123be930cd77474258d40d490fbb6b59aa0e5c9f427e6c10ce  A bench_ma3_perf
5d2ae77555a5146d930fab231d2975b5765fd989ae71f9ad72016f5c07832618  B bench_ma3_perf
```

## Next

The [stage attribution](ma3-stages-20261106.md) predates vector BCS.
Next: controls and envelopes in lanes with direct lane-input writes, then
raster/mozaik. Refresh stage timings before assigning remaining shares.
The original 32-slot objective and MA3-LIVE acceptance remain open in the
[execution plan](ma3-perf-plan.md).
