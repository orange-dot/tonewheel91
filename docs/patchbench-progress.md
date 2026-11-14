# Patchbench progress

Updated: 2026-11-12. Plan: `docs/patchbench-plan.md`. Everything below is
uncommitted.

Patchbench is `build/patchbench` (`make patchbench`), sources in
`workbench/pb/`, build rules in `workbench/pb/patchbench.mk`. It opens a
`.mapatch` or `.wbpatch` given on the command line, else the patch named in
the bench file, else an init patch.

## Milestones

| | State |
| --- | --- |
| PB0 spikes | Tap route chosen: the hosted mirror (below). `set_patch` cost measured. Controls seen in headless screenshots at 1920×1080 and 2560×1080; not yet seen on the real displays |
| PB1 `.mapatch` v4, display table | done |
| PB2 engine and shell | done: mailboxes, pool swap without an ALSA restart, keyboard, latch, level, safety |
| PB3 control kit, layout, undo, A/B, chains, library, save, autosave | done |
| PB4 sequencer, built-in patterns, on-screen keyboard, `.pbench` | done |
| PB5 probe, oscillator pictures, compass sheet, effective marks | done |
| PB6 Raster, Mozaik, BCS, Glas pictures | done; not checked against the golden vectors of `ma-constants.md` §4.3 |
| PB7 filter response, envelopes with handles, output, macros, identity frame | done |
| PB8 scope (stage and live), spectrum with alias marks, note view | done; pitch lock not tested on the pitch-ladder pattern. The second-monitor view was removed 2026-11-11 (operator): one window on the largest display, `--single` dropped |
| PB9 snapshots and morph, module init/copy/paste/reset, bounce with manifest, "open in Patchbench" | done |
| PB10 organ and ep73 voicing pages | not started (operator decision 5) |
| PB11 MIDI adapter, panels in the Workbench | not started |

## Gates met

| Gate | Result |
| --- | --- |
| `.mapatch` v4 | v1, v2 and v3 load with Glas bypassed; v3 refuses Glas fields; v4 round-trips a Glas patch; the factory files still decode to the compiled bank (`build/test_hosted`, 120 checks) |
| Display table | every catalog field has an entry and a module; scale, format and parse round trips (`build/test_patchbench`) |
| Probe identity | `pb_probe_tick` equals `ma_synth_tick` bit for bit over whole notes (hold 0.6 s, release to idle): six factory patches, a Glas+BCS patch, a Raster+Mozaik patch, notes 36/60/84 at velocities 40/100/127; 2,943,369 frames |
| Probe pass time | phase A (taps, spectra, effective values, curves) 22 ms at C3, under the 50 ms target; phase B (whole note, filter response, components) 170 ms; with the compass sheet 232 ms in all |
| Tap hashes | factory bank at C3 v100, deterministic run to run: tepih `fb4c513d4da25bf4`, lead `040d2531c82dd793`, dubina `060dbc698dfe839c`, raster `8a080e35170d5677`, prizma `5e6d63f6e86bf9be`, granica `204a4f13f821b409` (recorded before the settle and capture windows were shortened; `build/test_patchbench` prints the current ones) |
| Filter marker (PB7) | at −26 dBFS input with resonance, drive, envelope and key tracking at zero, the measured response at the effective-cutoff marker is −12.08, −12.08 and −12.00 dB for 300 Hz, 1 kHz and 4 kHz cutoffs (four poles, −3 dB each) |
| Derive | no monitor and solo of every source both equal the authored patch; solo VCO1 keeps VCO2's waveform, sync and cross-mod; solo VCO2 zeroes VCO1's levels; the authored patch is never written |
| Morph | t = 0 and t = 1 are bit-exact; floats interpolate; the interval switches at the midpoint |
| Sequencer timing | step frames exact after 10⁶ steps for six tempi × four swings; tie and gate event order; a tempo change re-origins at the boundary |
| Engine parity | 900 periods through `pb_rt_process` (sequencer, held notes, three patch updates in a row, a live voice change, a tempo change, a pool swap, panic, restart) equal a pool ticked frame by frame with the same calls: PCM `cfa5a2329dacba61` |
| Bounce parity | two loops plus tail (unison 2, detune, character, glide, ties, swing) equal the same reference: 106,368 frames, PCM `f2b0813223e7cf07` |
| Audio-thread audit | `make check-patchbench-symbols`: `pb_rt.o` and `pb_seq.o` linked together reach only `ma_` engine functions and `sqrt` |
| UI smoke | `make smoke-patchbench`: both themes at 1920 and 2560 wide, every scope tap, note view, peak hold, compass sheet, library, find, every solo; no edit without input; a scripted drag on the filter picture and a keyboard step each make one undo step; undo restores the patch; the file on disk is unchanged |
| Vertex budget | the SDL3 backend draws with 16-bit indices (65,535 vertices a frame per window). Traces keep at most one point per pixel column (two where a column spans a range), scatter-lines 400 points; the smoke records the largest frame, 44,923 vertices on Granica at 2560 wide, and fails above 50,000. Before the caps an ASan run hit Nuklear's assertion |
| Full suite | `make test` (gcc) green, with `test-patchbench` in it: core 111,378 checks, Workbench 393 + 50 + CLI, Patchbench 770 + 8 |
| clang, sanitizers | `test-patchbench` green with clang (engine and bounce PCM hashes identical to gcc) and with ASan+UBSan; `test_hosted` green on both; the UI smoke runs clean under ASan+UBSan on Granica, Prizma and Tepih; `build/clang/patchbench` and `build/clang/workbench` build without warnings |

## Measurements (this host, 2026-11-11, a browser running)

`make check-patchbench-live` (opens the card at gain 0, silent):

| Run | Result |
| --- | --- |
| `ma_voice_pool_set_patch`, Granica, 8 held voices | mean 0.013 ms, worst 0.039 ms per call; render 0.49 ms per 128 frames |
| the same, 32 held voices | mean 0.046 ms, worst 0.109 ms; render 1.96 ms per 128 frames |
| live 20 s on `default`, normal priority (no RealtimeKit in the tool), 6 held notes + arpeggio, cutoff swept through the mailbox at 60 Hz, one pool swap 8 → 16 | 7500 periods, 1193 patch updates, 1 swap, worst DSP 1.51 ms of 2.67, 0 xruns |

`set_patch` once per period fits with a wide margin at 8 and 32 voices; no
spreading over periods is needed.

## Decisions taken while implementing

- **Tap route: the hosted mirror** (plan §4.2, route 2). Every stage of the
  scalar tick is already exported through `ma_voice_impl.h`, including the
  pieces of the static `voice_front_end` (`ma_voice_front_a`,
  `ma_voice_render_bcs`, `ma_voice_front_b_shared`, the cutoff and source-frame
  functions), so `pb_probe.c` restates the order of the calls and nothing in
  `src/` changed. The identity test above is the drift guard; no perf gate
  applies. Route 1 stays available if the operator prefers it.
- **Glas bypass for v1–v3 files is all zeros**, vowel included, not the
  plan's vowel .25. The compiled bank has vowel 0 and the v3 reader produced 0;
  .25 would have changed the authored value of every existing file. At mix 0
  the vowel is not heard.
- **Keys.** Tab swaps A/B only (focus moves with ← → and `[` `]`). `/` is on
  the lower keyboard row, so find is Ctrl+F. Digits play notes, so snapshot
  recall is Alt+1..8 (store Ctrl+1..8, or the buttons in the macro strip).
  Esc cancels text entry, then closes an overlay, then panics. F12 toggles
  audio, Space the sequencer, F7/F8 the octave.
- **`.pbench` details:** tempo in centi-BPM, swing and gate in percent, steps
  as repeated `[step]` sections, a per-step `octave` (−4..4) so the 25-semitone
  grid can play the C1..C6 pitch ladder, and a `gain` key (0..64 linear,
  default 16 = +24 dB, because the pool runs near −40 dBFS).
- **Probe phases.** The filter response (≈ 150 ms of ladder runs) moved to
  phase B so the oscillator pictures follow a drag. The pressure tap is the
  second 2x step of each frame, the one the ladder keeps. VCO2 at level 0 is
  drawn at unit level, labelled.
- **Filter "mute"** opens the filter (cutoff 20 kHz, no resonance, drive,
  envelope or key tracking); the ladder has no exact bypass.
- **Saving.** A factory file is a read-only parent: Ctrl+S writes a
  `.wbpatch` child (`parent=factory:<name>`) into the library and continues
  there. A save under the session root commits only that file
  (`git add -- file`, `git commit -- file`); nothing else is staged. Autosave
  every 15 s while dirty into `<library>/.autosave/`.
- **Workbench Patch view** keeps its grid and gains "open in Patchbench"
  (operator decision 6, the "keep until PB11" option). Patchlab is unchanged
  and not marked superseded (decision 7 waits for PB11).

## Open

- Look at every module on both real displays (2560×1080 and 1920×1080); the
  layout has only been seen in headless screenshots.
- Live play with RealtimeKit through the UI; 30 minutes with the sequencer
  running and a control swept, 0 xruns (operator check).
- By ear: a sweep on a held note has no steps beyond the core's smoothing
  (PB2 gate); A/B, morph and the bounce as heard.
- PB6: check the Raster and Mozaik pictures against the golden vectors of
  `ma-constants.md` §4.3 and the mip rule of §10A. The "harmonics kept"
  readout assumes mip m keeps 128 >> m harmonics.
- PB8: pitch-lock test on the pitch-ladder pattern.
- Operator decisions 1 (name and form), 4 (library location, now
  `$WB_HOME/patchbench`), 5 (organ/ep73), 6 and 7 are still open; the
  implementation follows the plan's proposals.

## Live path (2026-11-11)

`docs/live-path-20261111.md` covers memory lock, CPU placement, the
performance-profile hold (`PB_LIVE_LOCK`, `PB_LIVE_PIN` and `PB_LIVE_HOLD`,
all on by default), RealtimeKit called directly, the CPU-overload guard and
the SIGXCPU demotion (an overloaded real-time thread was being SIGKILLed).
It also has the soak tool (`make check-patchbench-soak`) and measurements
on the internal card and the AG03.

## Fit on a short window, reset, pitch controls (2026-11-12)

Operator: "neke grupe se ne vide do kraja" and a reset button for the whole
bench.

- **Cause.** A maximized window on the 2560×1080 display is about 1000 high,
  and the smoke shot at the full 1080, where everything fits. At 1000 the
  VCO2 module lost its `int` and `fine` rows and VOICE its `gain` row.
- **Fit.** `pb_draw_module` measures how far a module's last row reaches past
  its frame, and the module's first picture is that much lower on the next
  frame (and higher again when there is room). VOICE has no picture, so its
  twelve rows get lower instead (14 to 18 px). Looked at in off-screen
  screenshots at 2560×1000 and 2560×940, dark theme: every module row is in
  view. `PB_SMOKE_HEIGHT` sets the smoke's window height
  (`PB_SMOKE_HEIGHT=1000 PB_SHOTS=dir build/patchbench --smoke …`).
- **Reset.** `reset` in the bar, beside undo and redo (`pb_reset`): the patch
  goes to `pb_init_patch()`, the rack to empty, the bench voice to
  `pb_voice_default()`, and solo and mute are cleared. The name, the parent
  and the file stay; the document is marked edited. One undo step restores
  the patch and the rack; the voice settings are not in the undo history and
  stay reset. The smoke checks all of it.
- **Pitch controls.** `analog` and `dead time` (docs/ma-vco-plan.md) are in
  the module now titled "SYNC · XMOD · PITCH".

Gates: `make test-patchbench smoke-patchbench` with GCC, Clang and the
sanitizer build: 795 + 15 checks, smoke passes. Not looked at on a display.
At 1920 wide the mixer module's title is cut short by its buttons; that was
so before.

## Sequencer channels (2026-11-12)

Operator: different patches on different channels of the sequencer.

- **Model.** `PB_CHANNELS` = 4. A channel is a patch document (file, parent,
  A and B, undo history, insert rack), a voice setup and a pattern. Tempo
  and swing are one for all channels; step count, root and notes are the
  channel's. One channel is on the desk (`bench.selected`): the modules, the
  probe, the notes grid, the VOICE module and the keyboard belong to it. The
  other channels' documents are parked (`pb_app.parked`) and play their
  current slot as authored; solo and mute of sources, rack mutes and morph
  are desk state and clear on a switch.
- **Engine.** One `ma_voice_pool` per channel with its own patch, voice
  setup and rack mailboxes and its own rack state; one sequence mailbox for
  all, adopted at a step boundary. The mix is the sum of the channels in
  order, each at its gain (`gain` of its voice setup). Notes carry their
  channel. The core is untouched.
- **Two threads.** With the worker up and two or more channels sounding,
  the channels go to the two threads by voice count; with one channel
  sounding its voice banks are spread, as before. A pool of 8 voices is one
  bank, so before this the worker had nothing to take at the default limit.
- **UI.** A channel strip left of the notes: name (selects; `F1`..`F4`),
  `M` mute (from the next step on), `S` solo (not saved), gain, voices over
  a peak meter. The bar shows the channel number before the patch name; the
  library opens onto the channel on the desk. The built-in patterns are a
  menu now (a pick replaces the channel's notes and keeps the swing). Bottom
  row: strip 190 px (170 below 2200 wide), notes to .40 (.42) of the width,
  scope to .62, spectrum .28, level .10.
- **`.pbench` version 2**: `selected`, `[sequence]` with `bpm` and `swing`,
  then per channel `[channel]` (`index`, `patch`, `mute`, `steps`, `root`),
  its `[voice]` and its `[step]` sections. Version 1 files load into
  channel 1; a save writes version 2.
- **Bounce**: all channels as the desk plays them (solo and mute included);
  a pass is the longest pattern that sounds. The manifest is
  `patchbench_bounce=2` with one `channelN_patch_sha256` per channel.
- **Autosave** writes every unsaved channel as `.autosave/chN-<stem>.mapatch`.

Choices made while implementing (not the operator's): four channels; mute
takes effect at a step boundary and lets sounding notes end at their gate;
solo is monitor state; a muted channel does not set the bounce length.

### Gates

| Gate | Result |
| --- | --- |
| One channel, as before | with channels 2..4 idle the scripted run and the bounce give the PCM of the single-pool engine: `cfa5a2329dacba61`, `f2b0813223e7cf07`; rack bounces `d5a43458345a95ab` and `ba28bc8d65fafff9` |
| Channel parity | four channels (Lead, Dubina, Tepih, Prizma; limits 8/4/8/8, unison 2 on one, chorus and reverb on two, gains 16/22/9/12; patterns of 16, 12, 8 and 8 steps, swing 20): live notes, patch and voice edits per channel, three pool swaps (two in one period), mute and unmute, a tempo change, panic and restart, 900 periods. Equals four pools ticked frame by frame and summed: PCM `90a7bc96cc7a1e17` |
| Two threads | the same run with the channels on two threads (933 runs): the same PCM. Removing any one channel changes it |
| Bounce with channels | three channels, one muted, 12 against 16 steps: equals the reference, 147,456 frames, PCM `c1f56d26b168556f`; the pass is 12 steps |
| Rack per channel | a rack on a channel that plays nothing leaves the bounce bit-identical; a mailbox rack reaches its channel only |
| `.pbench` | version 2 round trip with four different channels; a version 1 file loads into channel 1 with channels 2..4 at their defaults on its tempo (the bench file in the session root, version 1, loads this way); eight more refusals |
| Audio-thread audit | unchanged: 18 references, engine and pure libm |
| UI smoke | channel 2 on the desk takes a factory patch, a pattern and an edit; mute and solo reach the sequence; back on channel 1 its patch, rack and history are untouched, channel 2's own undo restores its patch, the file is unchanged |
| Suites | `make test-patchbench smoke-patchbench` with GCC, Clang and the sanitizer build (ASan, UBSan): 822 + 80 checks, smoke passes, the PCM hashes above identical in all three; `check-patchbench-symbols` with GCC. `pb_live_check` and `pb_live_soak` build; not run |

### Measurements (this host, hosted, a browser running)

- An idle channel costs 0.0135 ms per 128 frames (Granica, limit 8), so the
  three channels beside a sounding one add about 0.04 ms to a 2.667 ms
  period. Eight Granica voices on one channel: 0.53 ms.
- Vertex budget: the strip added about 1,300 vertices a frame and Granica
  went over the smoke's gate (50,303). The notes panel now rules its four
  lanes once instead of outlining 64 cells and the keyboard has one backing
  rectangle instead of 25 outlines. Largest frame of the smoke (2560×1080):
  Tepih 46,962, Lead 46,945, Dubina 44,963, Raster 44,510, Prizma 48,285,
  Granica 49,443. The gate has about 550 vertices of room left on Granica;
  the next addition to the frame has to save vertices elsewhere first.

### Open (operator)

- Not heard: no audio was opened. Play four channels live through
  RealtimeKit and watch DSP time with the worker on; `PB_SOAK_CHANNELS=4
  make check-patchbench-soak` measures the split silently.
- Not looked at on a display: the strip and the narrower scope and spectrum
  were seen in headless screenshots at 2560×1000 and 1920×1000 only.
- Four channels at limit 8 can ask for 32 voices; the measured capacity is
  about 20 Granica voices on one core and 40 on two. The overload guard
  releases every channel.
