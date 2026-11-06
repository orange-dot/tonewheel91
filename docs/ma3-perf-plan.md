# MA3-PERF execution plan

Date: 2026-10-02. Status: **first P0–P2 batch measured and reviewed**.
The [fresh baseline evidence](ma3-perf-20261002.md) records the result and
limitations for `build/ma3-perf-20261002-01/`. The first DSP candidate,
[OPT4 FIR traversal](ma3-opt4.md), is implemented. The operator later
authorized its [matched screen](ma3-opt4-measure-20261003.md); the mixed result
does not yet justify retaining OPT4. Full release/tail/control-stress coverage
and final acceptance remain later work.


Owner: the tonewheel91 project. This plan expands MA3-PERF in
[analog-backlog.md](analog-backlog.md). Task progress and decisions belong in
[ma-dev-journal.md](ma-dev-journal.md); measured results remain separate from
the plan. The current product target is x86-64 Linux/ALSA.

## Objective and boundaries

Reduce the complete Mamut pool render cost enough to attempt MA3-LIVE with
32 active slots, 48 kHz, and 128-frame blocks, preserving the instrument's
current sound and state semantics. Test all five patches with BCS off/on and
shared wet effects. A slot is one rendered voice; unison consumes multiple
slots, so 32 slots does not mean 32 independent eight-way-unison notes.

The block deadline is 2.667 ms. Proposed engineering headroom for this work
is CPU p99 at or below 80% of the deadline (2.133 ms), wall p99 below the
deadline, and no observed wall deadline misses in the final controlled
measurement windows. This is a proposed readiness target for MA3-LIVE, not
an existing measured result or a replacement for its no-xrun listening gate.
Report each patch/BCS case; a good aggregate cannot hide a failing case.

Preserve 8x oscillator and 2x ladder processing, filter iteration/tap counts,
patch defaults, 32-slot target, and freestanding C23 constraints. Keep
`-ffp-contract=off`; retain the existing floating-point operation order and
PCM/state signatures for this optimization track. Organ and EP cores remain
frozen. Any proposed quality, numeric-equivalence, target, or architecture
change needs its own explicit decision and evidence; it is not an implicit
fallback when this target is difficult.

MA3-PERF can end with a well-supported **budget not met** result. It cannot
close performance acceptance by silently lowering polyphony or selecting only
easy presets. Live driver development, GFM, and the MA5 soak are later tasks.

## Verified starting point

- Clean starting commit: `d7bf7b0ba009c96e30b5988be2285b47068cd063`.
- Current `src/ma_voice.c` SHA256:
  `3f99f5d79d77659676e632ffe5f91e3a9bc69b9448946ef08084226b3c6a0e82`,
  matching the recorded OPT3 implementation.
- Historical OPT3 full-32/wet/128-frame aggregate CPU mean: 8.320 ms;
  aggregate improvement over its OPT2 baseline: 2.12%, with Raster 6.40%
  slower. This is one historical cohort, not a current worst-case result.
- The gap from that aggregate to the raw deadline is about 3.12x; reaching
  80% of the deadline would require about 3.90x. Small local gains alone do
  not establish feasibility. Use measured stage shares and Amdahl's law to
  decide which experiments can materially affect the total.
- Current host: Intel i7-4600U; logical CPUs 0/1 share a physical core and
  2/3 share the other. Affinity allows all four CPUs. Choose one physical
  core and avoid work on its SMT sibling during measurement.
- Current compilers: `/usr/bin/gcc` 16.2.1 and `/usr/bin/clang` 22.1.8.
  Historical measurements used GCC 16.1.1. Rebuild baseline and candidate
  with one current compiler/flag set; do not splice their times into the
  old comparison. GCC/Clang comparisons are separate experiments.
- `taskset` and `gprof` are available. `perf` and `valgrind` are not on PATH.
  Gprof is an available initial attribution path; any later profiler install
  or privileged host tuning follows the execution environment's permissions.
- Preserved OPT1/OPT2/OPT3 builds and OPT2/OPT3 measurement directories exist
  under `build/`. Existence is not verification: check manifests, hashes,
  compiler identity, and source completeness before reusing them.

Source documents: [baseline](ma3-perf.md), [OPT1](ma3-opt1.md),
[OPT2](ma3-opt2.md), [OPT3](ma3-opt3.md).

## Agent allocation and recommended models

These are model IDs available in this session. The effort choices below are
recommendations for this task, not measured model-quality comparisons.
Use at most four live agents, including the coordinator.

| Role | Model | Reasoning effort | Ownership and responsibility |
| --- | --- | --- | --- |
| Coordinator | `gpt-6-astra` | `high` | This plan, backlog/journal integration, run scheduling, scope decisions, final evidence synthesis. Escalate a difficult numerical-contract decision to `xhigh`. |
| Benchmark/evidence engineer | `gpt-6.1-sol` | `high` for harness/protocol design; `medium` for executing a frozen run recipe | `driver/bench_ma3_perf.c`, focused benchmark tests, measurement utilities, raw artifacts and result tables. Sole owner of timed runs. |
| C/DSP implementer | `gpt-6.1-sol` | `high`; `xhigh` only for difficult state/cache or FIR equivalence work | One approved candidate at a time in `src/ma_voice.c`, `src/ma_pool.c`, `src/ma_output.c`, or `src/ma_effects.c`, with corresponding focused tests. |
| Independent reviewer | `gpt-6-astra` | `xhigh` | Read-only review of numerical equivalence, hidden oscillator/state progression, benchmark validity, regressions, and acceptance claims. Review the actual final diff and matching evidence. |

The coordinator allocates Makefile/header edits explicitly before work;
benchmark and DSP workers do not edit those shared files concurrently.
Every worker receives exact file ownership and is told that others share the
workspace: preserve their edits, do not revert them, and adapt to current state.
Use isolated candidate worktrees or source/build snapshots when needed.

Parallel work is useful for code inspection, harness design, and independent
review. During release timings or profiling, the evidence engineer owns the
host: other agents pause local tool work, builds, tests, renders, and benchmarks.
Only one timed process runs at a time. Build everything before acquiring that
measurement window. Separate build directories are required for every variant.

## Ordered work packages

| ID | Work | Owner | Exit evidence |
| --- | --- | --- | --- |
| P0 | Freeze current source and host baseline; audit preserved OPT2/OPT3 artifacts. | Evidence engineer + coordinator | Manifest, source/build hashes, compiler flags, CPU topology/affinity, baseline functional result; historical artifacts marked verified or unavailable. |
| P1 | Audit the benchmark contract and strengthen reporting where needed. | Evidence engineer; reviewer checks | Case-count/filter validation, raw block samples, observed occupancy, deadline-miss reporting, and a documented timing boundary. Baseline/candidate use the same harness. |
| P2 | Establish current release cost and attribute hotspots. | Evidence engineer + DSP implementer; reviewer challenges | Per-case baseline table, profiler output, source/assembly attribution, uncertainty, and a ranked candidate list with projected whole-chain benefit. |
| P3 | Resolve OPT3's Raster regression using matched rebuilt sources. | DSP implementer + evidence engineer | Controlled OPT2/OPT3 or local shared-preview toggle comparison under the same toolchain; retain/revise/revert recommendation supported per patch. |
| P4 | Implement and assess the best supported optimization, then repeat if justified. | DSP implementer; independent review | Small candidate diff, unchanged PCM/state and numerical gates, paired release evidence, explicit keep/reject decision. |
| P5 | Validate the retained composition across workloads and prepare listening evidence. | Evidence engineer + reviewer | Broad functional coverage, final release timings, measured supported envelope, listening artifacts/status, and complete artifact manifest. |
| P6 | Record MA3-PERF verdict and handoff to MA3-LIVE. | Coordinator + reviewer | Budget met/not met/measurement inconclusive, named remaining limits, and exact source/binary identities. No live acceptance claim from hosted timings. |

P0–P2 are the first execution batch. They produce enough evidence to select
P4's actual code change; a speculative rewrite is not assigned in advance.
P3 is a bounded regression investigation, not a prerequisite to every unrelated
candidate, but it must be resolved before the final composition is accepted.

### P1: benchmark truth checks

The current C harness measures `ma_voice_pool_render`, with initialization,
event dispatch, hashing, and file output outside the timed block. Its hash
includes warmup samples. Preserve and state that boundary; end-to-end MIDI/I/O
cost belongs in the live handoff. CPU and wall brackets have different clock
overheads; measure the empty-bracket floor and retain raw times rather than
silently subtracting an estimated overhead.

Audit these details before changing DSP:

- Default 32 measured blocks make the current nearest-rank p99 effectively
  the maximum of a tiny sample. Treat old p99s as descriptive observations.
- Numeric filters currently parse permissively and can select no cases while
  returning success. Require strict values, a nonzero case count, and explicit
  filter reporting; add focused tests for these failure modes if corrected.
- Validate rows against the requested matrix. Current unrestricted enumeration
  is 5 patches × 2 BCS modes × 3 limits × 3 block sizes × 8 scenarios ×
  4 effect arms × 2 passes = 5,760 rows. Fixing limit 32 gives 1,920 rows.
- Record raw per-block CPU/wall times after the render windows, process/run IDs,
  warmup count, sample count, and observed deadline misses. Do not add timing
  arrays, clocks, or file I/O to the shipping DSP core.
- Track active/peak/held/released occupancy and relevant safety diagnostics;
  final active count alone does not describe the whole measured window.
- Scenario 4 releases sustain halfway through the measured window. Duration
  and envelopes therefore change with block count. Compare equal recipes and
  separately identify sustain/release phases; do not pool them into a claimed
  steady-state percentile. Add full release/tail-to-sleep coverage separately.
- Add a bounded event/control workload if needed: repeated notes, pedal/panic,
  cross-mod and sync transitions, waveform/smoother transitions, guards, and
  dense controllers. State whether dispatch is included in each result.

Changing the harness requires a new matched baseline; old CSV columns or
hashes must not be reinterpreted as measurements from the revised harness.

### P2–P4: attribution and candidate order

Profile the full path first, with at least Tepih, Raster, and the measured
worst patch, BCS off/on, full 32-slot wet and a sparse case. Use a separate
`-O2 -g -pg -ffp-contract=off` Gprof build initially, following the local
[MA2 profiling precedent](ma2-perf-evidence.md). Inlining and function merging
limit attribution: inspect generated assembly and report unattributed cost.
Profile timings are never speedup or deadline evidence. Avoid per-sample clock
instrumentation, which can dominate the DSP being measured.

The following are hypotheses, to be reordered or discarded from measured
shares. They are not promises that any particular stage dominates.

| Candidate | Current code location | What to investigate / equivalence risk |
| --- | --- | --- |
| Decimator indexing and loop overhead | `ma_voice.c`: `decimate_oversample`, `push_oversample_history`, `decimate_filter` | Six 31-tap decimations per normal output sample/voice plus the output filter. Investigate wrap/index overhead and loop specialization while preserving tap order. Symmetric pairing, zero-tap omission, or reassociation must not be assumed bit-equivalent. |
| Control/envelope/coefficient preparation | `ma_voice.c`: `next_render_controls`, `render_envelope`, `prepare_source_frame`, `filter_prewarp` | Repeated computations may be reusable only while all dependencies are unchanged. Smoothers, character walks, glide, pressure, BCS, and envelopes continue to evolve. Prove invalidation/state semantics before adding caches. |
| Pool bookkeeping | `ma_pool.c`: `ma_voice_pool_tick`, `update_pan` | Per-sample slot/group walks, clearing and pan work; preserve sum order, release/sleep, transitions, allocation and group ownership. Sparse gains are reported separately from the full-pool target. |
| Source/filter/BCS arithmetic | `ma_voice.c`: source substeps, ladder and BCS helpers | Inspect scalar instruction cost and invariant preparation. Preserve hidden state and exact zero-path behavior; the inaudible VCO2 can still drive cross-mod. |
| Shared output/effects | `ma_output.c`, `ma_effects.c` | Investigate only if the full profile supports it. Keep tail state, bypass/rejoin and safety semantics. |
| Compiler/code-generation experiment | Separate build configuration | Test flags/LTO or SIMD only as isolated later experiments with full evidence and declared target requirements. No fast-math or silent deployment/toolchain change. |

One candidate changes one mechanism. Write the hypothesis, affected cases,
expected invariants, and predicted whole-chain ceiling before coding. Reject
an optimization whose measured benefit does not justify its complexity.
If all credible exact candidates have too little headroom, P6 records the
remaining gap and concrete alternatives for a later product decision.

## Measurement protocol

1. Create a fresh named run directory, for example
   `build/ma3-perf-20261002-01/`, and preserve source snapshots, diffs, compile
   commands, binary hashes, host metadata and the run recipe. Never overwrite
   the historical OPT directories. Call the comparator and candidate A/B
   explicitly; repository HEAD alone is insufficient once changes begin.
2. Build matched A/B release binaries with `/usr/bin/gcc`, current default
   `-O2 -ffp-contract=off`, and separate BUILD paths. Use Clang for independent
   functional checks; keep cross-compiler timing experiments separate.
3. Select an allowed logical CPU after checking topology. Record governor,
   frequency/thermal information when readable and competing-load context.
   Do not claim `taskset` isolates a physical core or locks its frequency.
4. Screen affected cases with 64 warmup and 256 measured blocks. Use balanced
   A/B/B/A process order, then repeat in a separate epoch. If the sign changes
   or cost remains far from the target, record uncertainty before spending a
   large confirmation budget.
5. Confirm retained candidates on affected and regression cases with at least
   three independent balanced epochs and 256 warmup / 2,048 measured blocks.
   Inspect convergence; a bigger fixed warmup is not proof of thermal stability.
   Treat process/epoch pairs as replicates, not every autocorrelated block as
   an independent experiment. Extend near the decision boundary.
6. Report paired changes per patch, BCS, scenario and effect arm, including
   CPU/wall p50, p99, max, miss count, sample count, effect/safety counters and
   PCM hashes. Preserve negative and noisy results; do not select favorable
   repetitions or replace per-case tables with one average.
7. Proposed retention screen: a repeatable improvement beyond measured noise
   in its target cases, with no unexplained >3% CPU regression in a required
   case. Any new deadline failure blocks readiness. Small gains can remain
   inconclusive; the 3% screen is an engineering policy for this plan, not a
   statistical confidence interval or a machine-independent test assertion.

The broad correctness matrix can use short runs. Long repeated timing is
focused first; do not multiply the complete 5,760-row matrix by thousands of
blocks for every small edit. Final deadline coverage includes all ten
patch/BCS combinations at 32 slots, 48 kHz, 128 frames, wet; also report
256/512-frame, sparse, sustain/release, unison 2/4/8, and controller-stress
results. Limits 16 and 64 describe scaling, not replacements for the target.

## Correctness and acceptance

For each retained source change:

- GCC and Clang `make test`, including the existing OPT1/OPT2 PCM and hidden
  state anchors, pool/MIDI/effect twins, and frozen organ/EP regressions.
- Clang ASan/UBSan/float-cast-overflow, plus the freestanding symbol audit on
  release builds. Run functional tests before scheduling release measurements.
- `exhibit-ma1-osc` numerical/aliasing referee; 44.1/48/96/192 kHz transition
  fixtures where the changed path is involved. The performance harness itself
  is currently fixed at 48 kHz; do not describe it as a multi-rate benchmark.
- Add only focused tests for a new invalidation/state/benchmark failure mode.
  Never regenerate pinned expectations just to accept an optimization.
- Compare A/B PCM on identical workloads and hash relevant hidden state when
  an optimization can affect future samples. Keep seeds, events, warmup and
  block counts equal; a different-duration hash is not an equivalence test.

At P5, prepare the existing short effects listening pairs from the retained
builds and log their identity. Historical Blues/Hurt renders exist, but they
are not evidence for a later source variant. Full musical renders are a
separate scheduled workload. Human listening verdicts remain human work;
agents can prepare evidence and mark them pending, not mark them accepted.

P6 reports one of:

- **Ready for MA3-LIVE trial:** controlled hosted readiness criteria met,
  correctness/review complete, with listening status and supported host/config
  explicit. The live no-xrun gate remains in MA3-LIVE.
- **Budget not met:** reliable measurements still exceed the target. Record
  accepted exact optimizations and residual cost without closing MA3 acceptance.
- **Inconclusive:** noisy or incomplete evidence prevents a supported decision.
  Name the missing measurement or unavailable baseline, without claiming speedup.

## Artifacts and handoff

Each run owns a manifest, source and binary hashes, source snapshots/diffs,
compiler commands, environment/topology, raw block CSVs, per-case summaries,
profiler output, correctness logs, independent review, and a decision record.
Keep generated artifacts under ignored `build/`; commit concise evidence docs
and meaningful code/tests through the project's normal commit workflow.
Use the lab evidence-pack workflow after runs to collect the named artifacts;
inspect its file selection so raw CSV/profile data are not silently omitted.

Every agent handoff states: exact source/binary identity; files changed;
completed checks; measurements and their limits; pending listening; next task.
Historical scratch notes do not override the source hashes or the latest
explicit run decision.
