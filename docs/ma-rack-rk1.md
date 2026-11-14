# RK1 — insert rack host, EQ and utility

2026-11-11. Plan and decisions: `docs/ma-rack-plan.md` (RKD1..RKD12, section
4 gates). Implementation is uncommitted on top of `4aa1099`, beside the
uncommitted Patchbench (PB0..PB9). Gates 1–7 pass; gate 8 (listening) waits
for the operator.

## What landed

Core (`src/ma_rack.c`, declarations in `src/mamutanalog.h`):

- `ma_rack_spec` / `ma_rack_slot_spec`: 8 slots, each empty or one block
  (`id`, `kind`, `enabled`, `mix`, up to 12 parameters).
  `ma_rack_param_info_at` gives each parameter's range, initial value and
  curve (linear, log, toggle). `ma_rack_spec_valid` rejects duplicate or zero
  ids, unknown kinds, non-finite or out-of-range values, half toggles and
  garbage in empty slots.
- `ma_rack`: caller-owned state. `ma_rack_apply` between frames,
  `ma_rack_tick` per frame, `ma_rack_settle` for a host that starts rendering,
  `ma_rack_moving` for a host that waits. Applying the same id and kind keeps
  the block's state. A structural change keeps the longest run of unchanged
  order, fades the other blocks out over 10 ms, adopts the new chain and fades
  new or moved blocks in. A spec that undoes a pending change cancels it.
- `eq`: high-pass (12 dB/oct Butterworth; on/off is a 6 ms crossfade), low
  shelf, bell, high shelf, all TPT state-variable sections (Simper's shelf
  and bell forms), linked L/R. `tan` comes from the repo sine kernel; dB to
  gain uses a freestanding `exp2` (degree-7 series, below 1e-8 relative).
  A section at 0 dB is skipped exactly and restarts from zero history.
- `utility`: gain, mid/side width, balance; each stage is skipped at its
  neutral value.
- Hook (RKD9): `body -> DC -> rack -> master -> safety -> chorus -> reverb`.
  `ma_output_render` takes the rack pointer; the five-card bank and the
  one-voice path pass none. `ma_voice_pool_attach_rack`; init detaches,
  reset keeps it.

Hosted:

- `.mapatch` version 5 (`driver/ma_patch_file.c`): optional `rack.<n>.*`
  keys. The kind comes first in its slot and every key of an occupied slot is
  required once. An empty rack still writes version 4, byte for byte. The
  key reader and formatter (`ma_rack_read_key`, `ma_rack_read_finish`,
  `ma_rack_format`) are shared with `.wbpatch`.
- `.wbpatch` (`workbench/desk/wb_patch.c`): a child inherits its parent's
  rack. `wbpatch=2` with `rack=1` replaces the parent's rack with the
  child's own keys, and no keys mean an empty rack. Patchbench saves an
  edited factory patch as a child, so without this the rack would be lost on
  save. Workbench flattening carries the rack.
- `render_midi --rack PATH`: the rack of a `.mapatch` v5, settled at the
  start of the render. It is the offline twin.
- `bench_ma3_perf`: `MA3_PERF_RACK=none|empty|eq8`.

Patchbench (`workbench/pb/`):

- A rack row between the macros and the bottom row, 150 px at 1080 lines. The
  bottom row went from 300 to 220 px and the two module rows from 317 to
  282 px.
- Eight cells in chain order. Each cell has insert (+EQ, +Util), a bypass dot
  (lit while the block hands its input on: disabled, mix 0, neutral or
  muted), `<` `>` move, `M` monitor mute and `x` remove.
- An editor for the selected block: the response picture and the parameter
  bars. Dragging on the EQ picture moves the bell (x frequency, y gain).
- The response pictures are measured, not drawn: a private rack holding the
  block alone, settled, driven by an impulse, 8192-point FFT. Results are
  cached per slot spec.
- The rack is authored per A/B slot beside the patch and is in every undo
  step. Save, derive, flatten, autosave, bounce and library audition carry
  it. Mute is monitor-only, like module mute.
- `ma_rack` state lives in `pb_engine`, not in the pool. A Rack mailbox
  publishes the whole spec and the audio thread applies it at the period
  start. A pool swap re-attaches the same rack, so tails survive (RKD4). The
  meters carry rack applies, sanitizations and moving.

## Gates (plan section 4)

All on this host (x86-64-v3; GCC 16.2.1 with LTO, Clang 22.1.8, `make sanitize`), 0 compiler
warnings in all three builds.

| # | Gate | Result |
|---|---|---|
| 1 | Empty and bypassed rack: every pin unchanged | `make test`, `make test-clang`, `make sanitize` green. Core 111378 / hosted 143 / architecture 227 / character 8700 / stereo 2611 / MIDI map 22 checks, pool twins, lane gates, CLI twins, Workbench 408 + live 50, Patchbench 770 + RT 15, all 0 failures. Engine parity `cfa5a2329dacba61` and bounce parity `f2b0813223e7cf07` are the pre-RK1 hashes. `test/ma_rack.c`: Tepih, Lead and Granica, dry and wet, with no rack, an attached rack, an empty spec and a spec of neutral, disabled and mix-0 blocks: all four renders bit-identical. |
| 2 | EQ response against the analytic SVF | Worst deviation **0.0002 dB** over 5 settings × 10 frequencies × 44.1/48/96/192 kHz (tolerance 0.1 dB). HP −3.01 dB at its corner, bell −9 dB at its centre, shelf plateaus +12 and +6 dB, each within its tolerance. |
| 3 | Structural changes without clicks | Insert, reorder, remove and kind change on a 220 Hz sine (one per second, 5 changes, 5 adoptions): the largest frame step is 0.05719, equal to the largest steady-state step (bound: steady + 10 %). No non-finite output. |
| 4 | Sweeps at controller rate and extremes | A random valid spec every 64 frames, 400k frames of full-scale noise at 44.1 and 192 kHz: all finite, 0 sanitizations, peak 25.7 (eight blocks at up to +18 dB each). |
| 5 | Two-run determinism through `render_midi` | `test/ma3_cli.py test_rack_from_patch_file`: a disabled rack equals no rack, an active rack differs, blocks of 1, 17 and 4096 frames are identical, "two runs identical", and a rackless or invalid file is refused with exit 2. |
| 6 | Core symbol audit | `check-core-symbols` passes (only `memset` unresolved); the Patchbench audio-thread audit passes. |
| 7 | `bench_ma3_perf` | Below. |
| 8 | Listening pairs | Below; operator verdict open. |

Further checks: `.mapatch` v5 round trip and 13 malformed cases
(`test/hosted.c`), `.wbpatch` inheritance, replacement, empty override,
malformed cases and override round trip (`workbench/test/wb_test_w2w3.c`),
and Patchbench bounces: none, bypassed (bit-identical to none), active
(differs, deterministic). The mailbox reaches the audio thread and the pool
is attached to the engine's rack (`pb_rt_test.c`). The headless smoke draws
the rack row with an EQ and a utility at 1920 and 2560 wide in both themes.
Three undo steps, one of them a rack edit, restore patch and rack, and the
file is unchanged. The largest frame is 47816 vertices against the smoke
gate of 50000; it was 44265 before the rack row.

## Gate 7: performance

Run `build/rk1-perf-20261111-01/` (`run-ab.sh`, `ab_table.py`,
`ab-summary.md`, binary SHA256s, `source-rev.txt`). A = `bench_ma3_perf` from
`4aa1099` (no hook); B = this tree with no rack; E = B with an empty rack
attached; Q = B with eight EQ blocks, every section active. 8 slots, 128-frame
blocks, scenario 3, effects arm 3, 2048 measured blocks, user-space PMU
cycles, `taskset -c 2`, three epochs in rotating order, both engines. The host
was quiet: an earlier start with a browser open was stopped and discarded.

| | auto (lanes): median / worst | scalar: median / worst |
|---|---|---|
| B: hook, no rack | 0.998× / 1.008× | 0.998× / 1.001× |
| E: empty rack | 1.000× / 1.016× | 0.998× / 1.001× |
| Q: eight active EQs | 1.181× / 1.215× | 1.053× / 1.066× |

The PCM of A, B and E is identical in all 20 cases. The worst E case (Tepih,
BCS off, lanes) has a 2.8 % spread between epochs, so it is noise and
inside the refactor gate (median ≤ +1 %, no case above +2 %). Eight active EQs
add about 0.21 M cycles per 128-frame block at 8 slots (32 stereo SVF
sections, scalar code). This is recorded, not gated.

## The level the rack receives

`build/test_ma_rack` prints it (an empty rack attached, pool output divided by
master; the safety knee is not touched). 8 slots, velocity 100, 2 s held.

| Patch | master | one note peak / rms dBFS | C3 E3 G3 C4 peak / rms dBFS |
|---|---:|---:|---:|
| Tepih | 0.18 | −27.9 / −41.2 | −21.6 / −34.7 |
| Lead | 0.18 | −28.6 / −40.9 | −21.8 / −34.6 |
| Dubina | 0.18 | −28.6 / −35.2 | −19.3 / −28.9 |
| Raster | 0.90 | −29.9 / −38.5 | −21.4 / −32.5 |
| Prizma | 0.80 | −28.1 / −39.0 | −20.3 / −33.2 |
| Granica | 0.54 | −28.8 / −37.0 | −19.5 / −30.8 |

The level before master holds within 3 dB across patches whose master varies
by 14 dB. RKD9's reason for the position therefore holds: a saturator or
compressor in RK2 sees the same signal whatever the master setting. RK2's
nonlinear blocks need their input gain in this range (about −28 dBFS for a
note and −20 dBFS for a chord).

## Gate 8: listening pairs

`build/rk1-listen/`. The input SHA256s are in `inputs-sha256.txt`.
`progression.mid` has four four-note chords, 1.8 s each. All four files are
rendered with `render_midi -I mamutanalog --voice-limit 32 -g 16 -t 3`. They
are dry (no chorus or reverb) at the same +24 dB gain, so each pair differs
only by the rack.

| File | Rack | Peak | FNV-64 |
|---|---|---:|---|
| `rk1-tepih-dry.wav` | none | 0.276 | `e060aced1fd290c4` |
| `rk1-tepih-rack.wav` | EQ: HP 40 Hz, low −2 dB @ 150, bell +3 dB @ 2.5 kHz Q .8, high +4 dB @ 8 kHz; utility width 1.3 | 0.283 | `5c9bba2487abd91a` |
| `rk1-lead-dry.wav` | none | 0.260 | `10f0c8096c2a9a20` |
| `rk1-lead-rack.wav` | EQ: bell −4 dB @ 400 Hz Q 1.2, high +3 dB @ 6 kHz | 0.245 | `25589c1053cbc512` |

Question for the ear: does each EQ do what its settings say, with no
artefact (zipper, phase smear, click) the dry file does not have?

## Open

- The operator listening verdict (gate 8), and a look at the rack row on both
  real displays.
- Bench snapshots (`.pbench [snapshot]`) store the patch only. Recalling one
  keeps the current rack. The rack is in undo and in A/B.
- The rack's MIDI control waits for RK3 (CC116..119 as knobs).
