# Mamut Analog voice refactor plan (`src/ma_voice.c` split)

Status: executed 2026-11-07 (`b6c0426` … `30cd135`). `ma_sources.c` was
measured and rejected; the raster, BCS and oscillator code stays in
`ma_voice.c`. See the 2026-11-07 split entry in `docs/ma-dev-journal.md`.
This plan was written for an agent who starts after the next MA3-PERF commit
(raster in lanes, or whatever lands in its place) is on `main`. Use the operator's language in chat. This file, code
comments and commit messages stay in English.

## Why

`src/ma_voice.c` is 3,651 lines with about 230 functions. One translation unit
holds all of these:

- the patch presets
- control smoothing and identity resolution
- the public `ma_synth_*` API
- every scalar DSP block
- the scalar tick
- the ~1,000-line AVX2 lane engine

It grew this way because the lane engine must share `static` scalar helpers to
stay bit-exact. Two symptoms follow:

- **White-box includes.** Seven files `#include "../src/ma_voice.c"`:
  - `test/ma_fir.c`, `test/ma_bcs_lanes.c`, `test/ma_front_lanes.c`,
    `test/ma_raster_lanes.c`
  - `driver/probe_ma3_stages.c`, `driver/spike_ma_simd.c`,
    `driver/spike_ma_osc_simd.c`
- **Three copies of the front end that must agree:**
  - `voice_front_b` (scalar)
  - `voice_front_b_lane` (lanes)
  - the mirror inside `driver/probe_ma3_stages.c`

## Goal and non-goals

The goal is to split the file into cohesive modules by **moving code only**.
After every step, these must be unchanged:

- bits: PCM, pinned signatures and state
- the public API in `src/mamutanalog.h`
- the freestanding symbol surface
- measured performance, within noise

Not in scope; each needs a separate operator decision:

- merging `voice_front_b` and `voice_front_b_lane`
- changing any arithmetic, operation order, constant or compiler flag
- changing the 8-slot interim target or the 32 default
- touching the organ or EP lines
- reformatting moved code (no clang-format; the diff must be move-only)

## Hard constraints

1. **Bit-exactness is the contract.** Keep `-ffp-contract=off`. Add no FMA and
   no `-mavx2` or `-march` flag. Lane functions keep their per-function
   `__attribute__((target("avx2")))` (`ML_TARGET`). Compiling a whole file with
   `-mavx2` would let the compiler auto-vectorize non-lane code with AVX2.
   That code would crash on non-AVX2 hosts despite runtime dispatch
   (`ma_lanes_supported`).
2. **Freestanding core.** `make check-core-symbols` must still pass, with only
   `memset` unresolved. Clang emits `memcpy` for struct copies, and the fix
   used so far is field-by-field fills (see `fill_render_controls`).
   - Watch any struct passed or returned by value across a new boundary.
   - Every new MA object goes into `MA_OBJS` in the `Makefile`, which feeds
     both `MA_RUNTIME_OBJS` and `check-core-symbols`.
3. **The `MA_SOURCE_EVIDENCE` variant.**
   - `$(BUILD)/ma_voice_source.o` is `ma_voice.c` compiled with
     `-DMA_SOURCE_EVIDENCE`. It changes the `ma_synth` layout:
     `oversample_history[3][31]` instead of `[2][31]`
     (`src/mamutanalog.h:339`). It also selects a different `synth_tick`.
   - `driver/exhibit_ma_osc.c` links it (`make exhibit-ma1-osc`).
   - After the split, **every** module that touches `ma_synth` must be
     compiled with the same define in the evidence build. Add an evidence
     object set, for example `$(BUILD)/evidence/%.o`, and link the exhibit
     against all of it.
   - Never mix evidence and non-evidence objects. The layouts differ and the
     linker will not notice.
   - The lane section is already stubbed out under the define.
4. **Hot or cold matters more than topic.** The current `build/ma_voice.o`
   keeps only these statics out of line:

   ```
   voice_front_a, voice_back_end(.isra), render_mozaik, render_envelope(.isra),
   render_oscillator, preview_oscillator(.isra), preview_sparse_oscillator,
   raster_mip, raster_table_sample, interval_ratio, mamut_sine, poly_blep(.part),
   pulse_sample_with_blep, raw_wave_mix, bcs_parameters, bcs_derivative(.isra),
   fill_render_controls, target_render_controls, update_control_targets,
   initialize_control_smoothers, initialized_vco_smoothers, set_vco_targets,
   resolve_effective_identity, sanitize_*, mozaik_slope_q32, synth_tick,
   ml_bcs, ml_front_math, ml_kernel, ml_preview, ml_raster
   ```

   Everything else is inlined today.
   - A per-sample helper that is inlined now must stay visible to its callers.
     Put it in the private header as `static inline`; do not make it an
     extern function in another file.
   - Making an out-of-line function extern costs a little: it loses `.isra`
     signature specialization. Measure that.
   - Re-check with `nm build/ma_voice.o | grep ' t '` on the starting commit,
     because the list above was taken from the 2026-11-07 working tree.
5. **No static name collisions across MA modules.** The white-box tests will
   include several module `.c` files into one unit (see step 6), so two
   modules must not define `static` functions or constants with the same name.
6. **Commits.**
   - One step per commit, with a short, imperative, capitalized subject.
   - No `Co-Authored-By` trailer and no tool footer. This is lab-wide and
     overrides any harness default.
   - Ask the operator before each commit. Never push unless asked.
7. **Internal docs.** `docs/renders.md` and `docs/ma3-musical-exhibits.md` are
   internal (gitignored). Never re-add them.

## Target layout

All new files are under `src/`. Line ranges refer to the 2026-11-07 working
tree and will shift, so locate code by the function names.

| File | Contents (current anchors) | Temperature |
|---|---|---|
| `ma_voice_impl.h` (private, new) | Constants (lines 6–20, 63–123: `NOISE_SEED` … `FILTER_ADSR_DEFAULT`). Internal types: `mozaik_detent`, `ma_render_controls`, `ma_source_frame`, `ma_bcs_parameters`, `ma_bcs_vector`, `ma_voice_front`. `static inline` primitives shared by scalar and lane code: `finite_or`, `clamp_control`, `clamp_signal`, `raw_triangle`, `wrap_phase`, `phase_turns`, `smoothstep5`, `phase_step`, `phase_step_q48`, `octave_ratio_small`, `smoothing_frames`, `next_noise`, and any helper the lane code calls that is inlined today. Prototypes for the cross-module internal functions below, with an `ma_` prefix once they become extern. | — |
| `ma_patches.c` | `ma_patch_tepih` … `ma_patch_granica` (lines 124–248) | cold data |
| `ma_tables.c` or stays put | `NOTE_HZ`, `VELOCITY_LEVEL`, `VELOCITY_FILTER` with `ma_note_frequency_hz`, `ma_velocity_level`, `ma_velocity_filter`, `ma_safety_curve` (lines 249–305, 1290–1312). Move only if the tables are not read on a hot inlined path; check `note_position_hz`. | mixed |
| `ma_controls.c` | Cold control code: identity (`late_macro`, `resolve_identity`, `resolve_effective_identity`), `target_render_controls`, `initialize_control_smoothers`, `update_control_targets`, `sanitize_vco`, `sanitize_adsr`, `sanitize_patch`, `set_*_target`, the `ma_synth_set_*` API (lines 3464–3622), `ma_voice_tick_controls`, `ma_voice_snap_controls`, `ma_voice_patch_controls`, `ma_synth_init*`, `ma_synth_apply_patch`, `ma_synth_note_on`, `ma_synth_note_off`. **Keep the per-sample `next_render_controls`, `fill_render_controls`, `next_smoother`, `next_vco_controls`, `next_glide_note`, `next_lfo` and `next_character` with the tick, or in the header as `static inline`.** | cold |
| `ma_sources.c` (optional, measured) | Oscillator (`poly_blep` … `advance_phase`, `prepare_source_frame_guarded`, `render_source_substep`, oversample history and decimation), mozaik (`mozaik_slope_q32` … `render_mozaik`), raster (`raster_mip`, `raster_table_sample`, `render_raster`; the only includer of `ma_raster_table.h`), BCS (`BCS_LANDMARK` … `render_bcs`). | hot |
| `ma_filter.c` (optional, measured) | `filter_prewarp`, `mix_source_2x`, `apply_mixer_pressure`, `reset_ladder`, `render_ladder_2x`, filter history and decimation, `render_output`. | hot |
| `ma_voice.c` (what remains) | Envelopes, `character_adsr`, `filter_envelope_amount`, `effective_filter_cutoff`, both `synth_tick` variants, the staged tick (`voice_front_a`, `voice_front_b`, `voice_front_end`, `voice_oscillator_block`, `voice_decimate`, `voice_filter`, `voice_back_end`), `ma_synth_tick`, `ma_voice_tick_raw`, `ma_voice_tick_detuned`. | hot |
| `ma_lanes.c` | The whole `#if !defined(MA_SOURCE_EVIDENCE) && defined(__x86_64__)` section (`ma_lanes_*`, `ml_*`, `voice_front_b_lane`, `ml_front_*`, `ml_raster`, `ml_kernel`) and its `#else` stubs. It calls `voice_front_a`, `voice_back_end`, `render_mozaik`, `render_envelope`, `character_adsr`, `raster_mip` and `next_mozaik_guard_gain`, which are internal functions declared in `ma_voice_impl.h`. | hot |

`ma_internal.h` stays the boundary used by `ma_pool.c`, `ma_bank.c` and
`ma_output.c`. `ma_voice_impl.h` is private to the voice modules, the
white-box tests and the probes.

## Steps (one commit each)

### 0. Baseline (no commit)

Start on a clean `main` at the commit after the raster work, and record its
hash. Save the following under `build/ma-voice-refactor-baseline-YYYYMMDD/`:

- **Gates:** the output of `make test`, `make test-clang`, `make sanitize` and
  `make check-core-symbols`.
- **Pinned check counts:** at 2026-11-07, `make test` reported 111,378 core
  checks.
- **White-box tests:** the lane-frame or lane-step counts printed by
  `check-ma-bcs-lanes`, `check-ma-front-lanes` and `check-ma-raster-lanes`.
- **Bench PCM hashes:** the `pcm_hash` for all 10 cases × both engines from
  `bench_ma3_perf`. The env is the same as in
  `build/ma3-raster-ab-20261107-01/run-ab.sh`; add `MA3_PERF_ENGINE=scalar`
  for the second engine. Short runs are enough for hashes.
- **Evidence exhibit:** the SHA-256 of the `make exhibit-ma1-osc` output files.
- **Out-of-line statics:** `nm -S build/ma_voice.o | grep ' t '`.
- **Sizes:** `size build/ma_voice.o`.
- **A/B binary:** `bench_ma3_perf`, built as in the earlier runs.

### 1. Private header

Create `src/ma_voice_impl.h` from the constants, internal types and `static
inline` primitives, and include it from `ma_voice.c`. There is still only one
`.c` file.

Gate:
- `make test`, `make test-clang`, `make sanitize` and
  `make check-core-symbols` all pass.
- Every bench `pcm_hash` is unchanged.
- The `exhibit-ma1-osc` hashes are unchanged.
- `ma_voice.o` code size changes by less than 1%.

Update the `Makefile` dependency lists: every rule that lists
`src/ma_voice.c` gains `src/ma_voice_impl.h`.

### 2. `ma_patches.c`

Move the preset data and add the object to `MA_OBJS` and the evidence object
set. Gate: the same as step 1.

### 3. `ma_lanes.c`

Move the lane section.

- **Internal functions:** turn the scalar functions it calls (listed in the
  table) into internal functions declared in `ma_voice_impl.h`.
  - Use the `ma_voice_` prefix for those that become extern.
  - Most of them are already out of line, so the cost is small. Losing `.isra`
    on `voice_back_end` and `render_envelope` is the main risk.
- **Tests and probes:** the white-box tests and probes that include
  `ma_voice.c` for lane internals now include both `ma_voice.c` and
  `ma_lanes.c` (see step 6).
- **Gate:** the step 1 gate, plus a **quiet-host A/B** against the step 2
  binary.
  - Use the AUTO engine, the 10 cases, 3 epochs in A/B, B/A, A/B order, and
    `taskset -c 2`.
  - Ask the operator to close browsers first. Run under
    `systemd-inhibit --what=sleep:idle`.
  - Tabulate with `build/ma3-raster-ab-20261107-01/ab_table.py <run dir>`.
  - Acceptance: every case median within ±1%, and the per-epoch spread
    reported.
  - If a case regresses by more than 1%, try `static inline` for the helper
    involved first. If that does not help, stop and report.

### 4. `ma_controls.c`

Move only the cold control code; the table in "Target layout" lists what
stays with the tick. Gate: the step 1 gate, plus a scalar-engine A/B, because
the scalar tick calls more of this code.

### 5. `ma_sources.c` and `ma_filter.c` (optional)

These are the hot scalar DSP blocks. Do this step only if steps 1–4 passed
cleanly. Move one module per commit, each with an A/B on both engines. If
either engine regresses by more than 1% and `static inline` in the header
cannot recover it, leave the code in `ma_voice.c` and record why in the
journal. Leaving it there is an acceptable outcome.

### 6. Test and probe includes

The tests now include `ma_voice_impl.h` and the module `.c` files they need,
in dependency order, instead of the single `ma_voice.c`.

- If the include lists get long, a test-only `test/ma_voice_unity.h` that
  includes every voice module is acceptable. Constraint 5 (no static name
  collisions) is what makes that work.
- Check that each white-box test still prints the baseline counts.
- `driver/spike_ma_simd.c` and `driver/spike_ma_osc_simd.c` are historical
  spikes. Keep them building with include changes only, and run
  `make spike-ma-simd`.
- `driver/probe_ma3_stages.c` keeps its mirror for now. Replacing the mirror
  with calls into the real stage functions is a follow-up (see below).

### 7. Docs

- Add a dated entry to `docs/ma-dev-journal.md` with:
  - the steps, the object list and the A/B tables
  - any functions left in `ma_voice.c` for performance reasons
- Mention the new file map where `docs/ma3-perf-plan.md` references
  `ma_voice.c`.
- Write the docs in English: technical, factual, without promotional tone.

## Checks for each step, in short

```sh
make -j2 test && make -j2 test-clang && make -j2 sanitize && make check-core-symbols
make exhibit-ma1-osc            # evidence variant; compare output hashes
git diff --stat; git diff -M --color-moved=dimmed-zebra --color-moved-ws=allow-indentation-change
```

The `--color-moved` review should show moved blocks only. The only new lines
allowed are includes, prototypes, `static` → `static inline` or extern
changes, prefixes and Makefile lines.

## Host and measurement notes

- **Host:** an i7-4600U, 2 cores / 4 threads, 8 GiB. Run heavy jobs serially.
  Timing runs use CPU 2; CPU 3 is its SMT sibling.
- **Browsers:** a browser playing media moved cycles by up to 66%. Always ask
  the operator to close browsers before a timed run, and record `uptime`
  before and `top -bn1` after.
- **Sleep:** wrap long runs (gates, A/B) in
  `systemd-inhibit --what=sleep:idle`. Closing the lid still suspends the
  machine.
- **Reference runs:**
  - Protocol: `build/ma3-front-ab-20261107-01/run-ab.sh`
  - PMU probe: `driver/probe_ma3_pmu.c` (`--calibrate`, `--run`)
  - Stage probe: `driver/probe_ma3_stages.c`

## Follow-ups (operator decision, not part of this plan)

- Have `probe_ma3_stages` call the real stage functions instead of mirroring
  them.
- Find one source of truth for the scalar and lane front-end part B. One
  option is to make the scalar `voice_front_b` the lane function with a
  single lane.
- Reconsider whether `MA_SOURCE_EVIDENCE` should stay a compile-time layout
  switch or become a separate evidence module.
