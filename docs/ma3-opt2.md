# MA3-OPT2 — skip inactive waveform previews

OPT2 prepares a saw/pulse/triangle activity mask for each VCO once per output
sample, using the current smoothed controls. Each oscillator substep skips
preview calculations whose waveform level is exactly zero. The full mix
uses the previous preview helper; partial mixes use a separate helper.
The existing sine-level check and arithmetic mixing order are preserved.

Phase advances, triangle integration, guard transitions and sync residuals
still run even when a waveform or the whole oscillator is inaudible. VCO2
preview is independent of its source-mixer level because cross-mod can read
it. A control target of zero does not bypass an unfinished smoothing ramp.
FIRs, oversampling, public state layout and patch format are unchanged.

## Reference and functional checks

The baseline is the OPT1 worktree, including the existing uncommitted MA3
MIDI/effects/exhibit changes. It is not repository HEAD. The ignored run
folder is `build/ma3-opt2-1OlEaY/`:

| Artifact | Meaning |
| --- | --- |
| `before-ma_voice.c`, `before/` | Preserved OPT1 source, release objects and executables |
| `before-sources.sha256`, `worktree-before.diff` | Baseline source identities and tracked starting diff |
| `first-variant-ma_voice.c`, `after/` | Superseded first OPT2 implementation and its binaries |
| `final/` | Revised OPT2 GCC release build for the next measurement/listening pass |
| `clang/`, `sanitize/` | Revised OPT2 functional verification builds |
| `final-ma_voice.c`, `final-sources.sha256` | Revised source snapshot and source/executable identities |

Voice source SHA256 values:

- OPT1 baseline: `fb829581ecb942e0ea5316ad31fd2b1d3a64839f145957e3af019f1b1986f915`.
- Superseded OPT2: `e9cead4a814eed7681e40cdf931e2512437ea5a8b09e02d311fce61d4bf6f4bd`.
- Revised OPT2: `abd7237f81686ebb7e317d9df4130589a89ea5b422ad7f017b6e18483dec9d8c`.

`make check-ma3-opt2`, included in `make test`, replays all sixteen waveform
combinations at 44.1/48/96/192 kHz, with and without sync/cross-mod. Each
scenario runs twice and must match a signature captured from the preserved
OPT1 object. The hash includes stereo PCM, Q48 phases, triangle integrators,
guard gains and sync residuals. Coverage includes smoothing ramps, negative
zero controls, an inaudible VCO2 driving cross-mod, pulse-width extremes,
interval changes, high-note guards and note release/retrigger.

The reference capture and independent baseline replay are retained in
`reference-capture.log` and `reference-pinned.log`. The initial capture had
placeholder expectations and intentionally failed; only the pinned replay
is a passing baseline check.

GCC and Clang release builds pass the complete test suite and freestanding
symbol audits. Clang ASan/UBSan/float-cast-overflow passes the complete suite.
The numerical oscillator alias/harmonic referee passes in all three builds.
Existing OPT1, pool, unison, MIDI and effects signatures remain unchanged.
Logs are `gcc-tests.log`, `clang-tests.log` and `sanitize-tests.log`.
Verification commands, from the repository root:

```sh
make CC=/usr/bin/gcc BUILD=build/ma3-opt2-1OlEaY/final test exhibit-ma1-osc
make CC=/usr/bin/clang BUILD=build/ma3-opt2-1OlEaY/clang test exhibit-ma1-osc
ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang \
    BUILD=build/ma3-opt2-1OlEaY/sanitize TEST_EXTRA= \
    CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
    LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test exhibit-ma1-osc
```

These are functional checks, including short numerical DSP fixtures and
temporary CLI test files. No full musical or listening exhibit was rendered.
The final benchmark and effects-exhibit executables were built without
running them; `final-build.log` records their build.

## Measurement and listening handoff

Final-version performance measurement is complete for the matched scenarios
below. Full musical listening renders remain pending by operator choice;
functional and benchmark PCM hashes are not a listening verdict.

Before that instruction, the first variant was measured in
before/after/after/before order on logical CPU 0: 32 held slots, 48 kHz,
128 frames, wet effects, five patches, BCS off/on, 64 warmup and 256 measured
blocks. All 80 rows matched the corresponding reference PCM hash. That
experiment showed a Lead regression and substantial run variability. The
full preview helper was then restored and the sparse helper separated.
The revised source was measured in the same order. The complete matched
128-frame wet matrix covered 0/1/8 active notes at limits 16/32/64; the full
32-slot run used two repeated 128-block passes. Results and raw CSVs are
retained under `build/ma3-opt2-measure-20260910/`.

| Scenario | Limit | Baseline CPU mean | OPT2 CPU mean | Reduction |
| --- | ---: | ---: | ---: | ---: |
| 0 active notes | 16 | 65.2 us | 57.2 us | 12.23% |
| 0 active notes | 32 | 74.0 us | 62.8 us | 15.15% |
| 0 active notes | 64 | 91.4 us | 68.5 us | 25.02% |
| 1 active note | 16 | 355.8 us | 337.1 us | 5.24% |
| 1 active note | 32 | 362.5 us | 338.5 us | 6.62% |
| 1 active note | 64 | 375.9 us | 360.0 us | 4.23% |
| 8 active notes | 16 | 2564.0 us | 2518.9 us | 1.76% |
| 8 active notes | 32 | 2729.5 us | 2452.9 us | 10.13% |
| 8 active notes | 64 | 2662.8 us | 2500.9 us | 6.08% |
| Full 32-slot wet | 32 | 8447.3 us | 7793.6 us | 7.74% |
| Sustain/release | 16 | 4639.2 us | 4433.6 us | 4.43% |
| Sustain/release | 32 | 9068.8 us | 8312.7 us | 8.34% |

The 32-slot wet probes at 256 and 512 frames measured 7.28% and 14.16%
reductions respectively, using one measured block per case. They are
directional only; full 32-slot wet cost remains above the 48 kHz block budget.

The root-level `before-1.csv`, `after-1.csv`, `after-2.csv`, `before-2.csv`
refer to the **superseded variant**, not `final/`. Additional
`focus-{Tepih,Lead}-{0,1}-{before-1,after-1,after-2,before-2}.csv` runs used
logical CPU 2, 32 warmup and 128 measured blocks. They finished before the
handoff boundary and have not been analyzed. Preserve them as a separate
experiment; do not attribute their timings to the revised implementation.

The recorded host is Intel i7-4600U, Linux `7.1.8-200.fc44.x86_64`, GCC
16.1.1, release `-O2 -ffp-contract=off`. CPUs 0/1 share one physical core and
2/3 the other. Affinity alone does not isolate a core or lock its frequency.

The measurement supports retaining OPT2 for listening review. It does not
establish realtime acceptance for 32 slots; wall/p99 fields and all raw CSVs
remain in the measurement directory above.

For listening, run these sequentially when resuming the deferred work:

```sh
build/ma3-opt2-1OlEaY/before/exhibit_ma_effects --render build/ma3-opt2-1OlEaY/before/listen
build/ma3-opt2-1OlEaY/final/exhibit_ma_effects --render build/ma3-opt2-1OlEaY/final/listen
```

This exhibit provides Tepih/Granica, dry/chorus/reverb/wet, overlapping
chords, sustain and panic. Keep raw and level-matched files and compare
signatures before operator listening. Its existing common alignment is
-22 dBFS RMS over 0–4 seconds, subject to the peak ceiling. Longer musical
renders and further profiling remain part of the deferred handoff.
