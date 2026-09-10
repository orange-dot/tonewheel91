# MA3-OPT3 — share the saw/pulse BLEP preview

Within each oscillator preview, saw and pulse now reuse one
`poly_blep(phase, step)` result. Pulse retains its separate width-shifted
edge. Full and sparse waveform previews both use the shared result; sparse
previews evaluate it only when saw or pulse is active. The activity mask
continues to come from the smoothed controls.

The BLEP kernel, divisions, clamps and waveform mixing order are unchanged.
Triangle integration and sync-reset integration keep their existing pulse
calculations. Oscillator state progression, sine, filters, FIRs and
oversampling are unchanged. This is a local common-calculation refactor;
whether it improves generated code and host cost remains to be measured.

## Reference and validation

The baseline is the final OPT2 worktree, including earlier uncommitted MA3
changes, rather than repository HEAD. The ignored run folder is
`build/ma3-opt3-1d3Rwv/`:

- `before-ma_voice.c`, `before-sources.sha256`, `worktree-before.diff` retain
  the starting voice source, source identities and tracked changes.
- `before/` preserves the OPT2 GCC build from
  `build/ma3-opt2-1OlEaY/final/`, including benchmark and effects exhibit.
- `after/`, `clang/`, `sanitize/` contain OPT3 verification builds.

Voice source SHA256:

- OPT2: `abd7237f81686ebb7e317d9df4130589a89ea5b422ad7f017b6e18483dec9d8c`.
- OPT3: `3f99f5d79d77659676e632ffe5f91e3a9bc69b9448946ef08084226b3c6a0e82`.

The existing OPT1 and OPT2 fixtures are the regression checks for this
refactor. They pin PCM and oscillator state across all sixteen waveform
combinations, control smoothing, pulse-width extremes, signed zero,
cross-mod, sync, high-note guards and note transitions at 44.1/48/96/192 kHz.
Each fixture scenario runs twice. Expected signatures are not regenerated.

GCC and Clang complete test suites and freestanding symbol audits pass.
Clang ASan/UBSan/float-cast-overflow also passes the complete suite. Existing
OPT1/OPT2 PCM/state signatures, pool/unison, MIDI and effects checks pass;
the numerical alias/harmonic referee passes in all three builds. Logs are
`gcc-tests.log`, `clang-tests.log` and `sanitize-tests.log` in the run folder.
Commands from the repository root:

```sh
make CC=/usr/bin/gcc BUILD=build/ma3-opt3-1d3Rwv/after test exhibit-ma1-osc
make CC=/usr/bin/clang BUILD=build/ma3-opt3-1d3Rwv/clang test exhibit-ma1-osc
ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang \
    BUILD=build/ma3-opt3-1d3Rwv/sanitize TEST_EXTRA= \
    CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
    LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test exhibit-ma1-osc
```

`after-ma_voice.c` and `after-sources.sha256` retain the final source and
source/executable identities. `after-build.log` records the benchmark and
effects-exhibit build; those executables were built without running them.

## Measurement and listening handoff

OPT3 was measured after the functional handoff. The benchmark used GCC
16.1.1, 48 kHz, 128 frames, limit 32, scenario 3, wet effects, CPU 0,
32 warmup blocks and 128 measured blocks. Each patch was run in both orders:
baseline then OPT3, followed by OPT3 then baseline. The two orders and all
raw rows are retained in `build/ma3-opt3-measure-20260910/`.

| Patch | Baseline CPU mean | OPT3 CPU mean | Reduction |
| --- | ---: | ---: | ---: |
| Tepih | 8608.9 us | 7890.0 us | 8.35% |
| Lead | 8847.9 us | 8721.1 us | 1.43% |
| Raster | 6646.1 us | 7071.6 us | -6.40% |
| Prizma | 9388.3 us | 9196.4 us | 2.04% |
| Granica | 9009.0 us | 8719.3 us | 3.22% |
| Aggregate | 8500.0 us | 8319.7 us | 2.12% |

Each patch has eight observations across BCS off/on and both run orders.
Every paired row retained the same PCM hash. p50 and maximum values remain
in the CSVs; Raster's regression and the small Lead/Prizma effects mean this
is a mixed, host-sensitive result rather than a uniform speedup. The full
32-slot wet path remains above the 48 kHz block budget.

No listening render has been run yet. Operator listening remains a separate
step and should compare the preserved effects-exhibit baseline and OPT3
executables after the code decision.

Listening remains a separate operator instruction. The preserved before and
after effects-exhibit executables can supply matched listening pairs when
requested. If OPT3 is retained, a longer rerun should focus on Raster and
the two small-gain patches before making a general performance claim.
