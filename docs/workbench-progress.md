# Workbench progress

Updated: 2026-11-08. Plan: `docs/workbench-plan.md`. W0/W1 evidence:
`docs/workbench-w0-w1.md`. Everything below is uncommitted.

## Gates met

| Gate (plan §9) | Result |
| --- | --- |
| W2 render parity, Hurt v2 | `wb render` of `workbench/test/fixtures/hurt-v2.mws` (+ the exhibit's patches exported as `.mapatch`): six stems and the mix equal `exhibit_ma3_hurt_orch --edition 2` from the same tree; mix `96d601778ccf8305` as logged in `renders/mamut-analog/ma3_hurt_orch_20261107.md` |
| W2 render parity, Hurt v1 | full render built from the session rules (no `--midi`): the six logged stem FNVs and mix `bac34975b9f912ec` |
| W3 orchestration parity | `wb build-midi` gives byte-identical `hurt-orch-20261107.mid` (`3a16982b…`) and `-v2.mid` (`d4f7691c…`); unchanged after overlays, controller forwarding and the skyline split were added |
| W3 tempo maps | integer tick→frame equals `o_frame`; equals render_midi's double path except at exact half frames (unit test) |
| W3b import round trip | `wb import hurt-orch-20261107-v2.mid --split track` → 10 parts, rebuilt MIDI byte-identical (`d4f7691c…`); synthetic fixtures for every split strategy |
| W5 patch chains | `.wbpatch` (parent + overrides): zero overrides flatten bit-identical to the parent; overrides across three levels, factory parents, cycle refusal (unit tests) |
| W6 listening copy | `wb listening` on the v2 mix reproduces the logged listening file byte for byte (`72fe120c…`, +19.42 dB) |
| W9 engines (desk) | `compat=render_midi` sessions: ep73 on `ep73-d3.mid` = render_midi `ed84d0f52e07b8f5`; organ on `toccata-full-automation.mid` = render_midi `1333c439d911b731` |
| W12 live-play parity | one recorded take (`wb_take_write`) through the desk renderer and through the live engine in non-real time: organ, ep73 and Mamut bit-identical (`build/test_live`). The organ and ep73 desk path equals render_midi, and both old live drivers use the same MIDI maps; their usage text now says "superseded by build/workbench" |
| Real-time audit | `make check-workbench-symbols`: the audio-thread object references only engine functions and pure libm |
| Tests | `build/test_workbench` 393 checks, `build/test_live` 50 checks, `wb_cli.sh` — green on gcc, clang and ASan+UBSan+LSan; `make fuzz-mws` 455k runs clean on the extended format; `make test` now runs `test-workbench` |

Operator checks: `make parity-hurt` (MIDI + 30 s prefixes) and
`make parity-hurt-full` (~20 min).

## Live audio measurements (this host, 2026-11-08, VS Code running)

| Run | Result |
| --- | --- |
| ALSA `default`, 128-frame periods ×3, silence | 374 periods/s, DSP 0.02 ms, 0 xruns |
| 60 s, 8 held Mamut voices (gain 0), normal priority | worst period 6.2 ms, 2 xruns |
| same with 4 periods | worst 10.3 ms, 6 xruns |
| same with real time through RealtimeKit (SDL) | worst 4.6 ms, **0 xruns** |
| W7 gate: 30 min, 8 held Mamut voices, RealtimeKit | 675 055 periods, **0 xruns**; worst period 7.4 ms (one spike, absorbed by the 3-period buffer) |
| W8 gate: Hurt v2 arrangement, 9 parts from the cache + live `vocal` from the score, 283 s | worst 2.84 ms, **0 xruns** |

The user has `rtprio 0`, so `SCHED_FIFO` is refused; the audio thread asks
RealtimeKit (as PipeWire clients do) and the console reports what it got.
The W7 gate asks for 30 minutes; run it with
`WB_LIVE_DEVICE=default WB_LIVE_SOAK=1800 build/test_live` (silent).

## What exists

- Desk: `wb_render` (single pass, lanes in the exhibits' f32 order,
  controllers, desk pan, probe pre-roll, part levels, peak windows, cancel,
  organ/ep73, `compat=render_midi`, part-solo cache mode), `wb_orch` (rules,
  grooves, skyline split, controller forwarding, overlays), `wb_smf`,
  `wb_import`, `wb_analysis`, `wb_ledger`, `wb_engine` (`.twreg`/`.epset`),
  `wb_patch` (`.wbpatch` chains), `wb_cache` (part cache keys), `wb_take`.
- CLI: `render`, `probe`, `build-midi`, `import`, `reimport`, `cache`,
  `listening`, `ledger`, `verdict`, `diff`, `reproduce`, `export`, plus the
  W1 commands.
- Live: `audio/wb_live_rt.c` (process, SPSC ring, seqlock meters, safety
  mute, score playback) and `audio/wb_live.c` (instruments, ALSA thread,
  RealtimeKit fallback).
- UI (`build/workbench`): every view of §5 draws real data. Sources (track
  table, density heatmap, pitch classes), Import (preview, import into the
  session or as a new one), Orchestrate (rule matrix, per-part line), Roll
  (zoom, sections, click/Del/Shift+click overlays), Groove (step grid),
  Form, Patch (Mamut grid with A/B and parent chains, organ and ep73
  panels), Mixer, Probe, Render (job queue, cancel), Listen (waveform,
  loudness lane, A/B playback at matched RMS), Ledger (verdicts, diff,
  reproduce, export to clipboard), Play (four-slot rack, two keyboard rows,
  organ console, arrangement preview from part caches with live parts,
  take recording to `takes/` and as a session source). A generic inspector
  edits any object through the journal; Ctrl+Z / Ctrl+Y undo and redo.
- Session root: `/data/tonewheel91-sessions/hurt` with v1 and v2 as Git
  checkpoints; `workbench/run-hurt.sh` opens it.

## Open

1. W7 and W8 gates passed (table above). Rerun the soak with
   `WB_LIVE_DEVICE=default WB_LIVE_SOAK=1800 build/test_live` and the
   arrangement gate with
   `WB_LIVE_DEVICE=default WB_PLAY_SESSION=/data/tonewheel91-sessions/hurt/hurt.mws build/test_live`.
2. Patchlab (Mamut live) parity against the old map path (`ma_midi_map`)
   is not proven; its usage text is unchanged.
3. MA3 Hurt/Blues exhibits as sessions (part of the W2 gate): not done, and
   not expressible as they stand. Their controls are `sinf`/`cosf` arcs over
   time (`.5f - .5f*cosf(2π t)`), and the Blues lead gives every note its own
   channel with its own pitch-bend vibrato. Lanes are breakpoint curves over
   bars on channel 0, so bit parity would need exhibit-specific lane shapes
   and per-note channels. A decision for the operator: add those to the
   format, or accept the Hurt orchestra v1/v2 as the render-parity evidence.
4. Not built: fuzzy search and the `:` command line (§8), the key-rollover
   test, the latency loopback, LUFS readout, spectrogram lane, budget-based
   refusal of live parts (the meter is shown, nothing is refused).
5. The GUI was checked headless (smoke, also under ASan) and on screen at
   W0; the views added since were not seen on a real display (the screen
   was locked).
