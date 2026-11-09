# Workbench W0 + W1 evidence

Date: 2026-11-07. Plan: `docs/workbench-plan.md` §10, rows W0 and W1.

## What landed

```
workbench/
  desk/   wb_hash  wb_session  wb_format  wb_undo  wb_root     (hosted, no UI, no ALSA)
  cli/    wb_main.c                                              -> build/wb
  ui/     wb_app.c  wb_layout.c  wb_theme.c  wb_nk_impl.c        -> build/workbench
  third_party/  nuklear.h  nuklear_sdl3_renderer.h  (v4.13.4, VENDOR.txt)
  test/   wb_test.c  wb_cli.sh  fuzz_mws.c  fixtures/hurt-v2.mws
  workbench.mk   (one `include` line in the top-level Makefile)
```

Make targets: `wb`, `workbench`, `test-workbench`, `smoke-workbench`,
`fuzz-mws`. `make test` does not run `test-workbench` yet (plan: after W2).
`check-workbench-symbols` arrives with the audio thread in W7.

### W1: desk session model, `.mws`, journal, `wb check`

- `wb_session.h`: fixed-capacity session value (sources, form with sections,
  stems, parts, rules, grooves, curves, lanes, markers). Object order is
  kept; stem and part order are the summation order.
- `wb_format.c`: one field table per object kind drives the strict reader,
  the canonical writer, single-key edits (`wb_set_key`, empty value resets
  to the default) and whole-block reads/inserts/removes.
- `wb_undo.c`: every edit is recorded as the object's block before and
  after; undo and redo read the other block back. The journal file is
  append-only and carries the SHA-256 of the session after each record. A
  journal whose last hash does not match the saved session is moved to
  `.journal.stale` and never applied.
- `wb_root.c`: session root creation (local Git, no remote, `.gitignore`,
  `README.txt`), session directory skeleton, `add-source` (read-only copy
  plus SHA-256), commits for checkpoints, file checks for `wb check`.
- `wb` commands: `root-init`, `new`, `check`, `fmt`, `get`, `set`, `add`,
  `replace`, `rm`, `add-source`, `undo`, `redo`, `history`, `checkpoint`.

### W0: skeleton and frame

- SDL3 renderer + Nuklear, monospace font baked at 13 px (Adwaita Mono, then
  Liberation Mono, DejaVu Sans Mono, Noto Sans Mono, else Nuklear's built-in
  font; `WB_FONT` overrides).
- Global frame of §4.1: two-row 44 px ribbon (transport placeholders, form
  sections, presets F1..F6, session path, `WB_HOME`, CPU and RSS), parts
  column, tabbed workspace, inspector (exact stored text of the selected
  part), collapsible console. Dark theme first, light theme with the same
  roles (`Ctrl+Shift+T`).
- Second monitor: the preset's pinned view, fullscreen on the first display
  that is not the primary one. Wayland ignores client window positions, so
  windows are placed by display: the main window maximized on the primary,
  the pinned one fullscreen on the other (`--single` disables it).
- Views are read-only placeholders that name their milestone; Sources, Form
  and Orchestrate already list session data.

## Decisions taken while implementing (deviations from §3.2)

| Plan text | Implemented | Why |
| --- | --- | --- |
| `gain_db=` | `gain=` linear | the MA3 exhibits use linear gains (1.2, 1.6, 2.2); a dB round trip would not reproduce the f32 bits the W2 render-parity gate needs. The UI will show dB. |
| `velocity_scale=0.55` | `velocity_scale=11/20` (exact ratio, floor) | v2's ghost notes are `max(1, v*11//20)`; a float scale cannot promise byte parity of the derived MIDI (W3 gate). |
| `dedupe=` in `[rule]` | `dedupe=` in `[part]` | the generator dedupes all notes of a part after every rule fed it (Synth 1 + Synth 2 into `pad`). |
| `[stem]  id=x` on one line | one key per line | strict `key=value` lines, no exceptions. |
| trailing `# comments` | full-line comments only | values run to the end of the line (track names contain blanks). |
| `[pattern]` section | `pattern=` lines inside `[groove]` | as in the plan's own example. |
| `energy` expressions | `[curve]` blocks; a lane takes `curve=`, `scale=`, `offset=`, `clamp=` | `offset + scale * curve` reproduces `.05f + .12f * e` operation for operation. |
| `seed=name` or a number | `seed=name` or `seed=0x` + hex | — |
| renderer derives 283 s | `length_seconds=` (0 derives from bars + tail) | v1 is 306 s and v2 283 s; no single tail value derives both exactly. |
| — | `fold_below=` / `fold_above=` in `[rule]` | v2 raises bass notes below 26 by an octave. |
| — | lanes take `op=set|mul|add` | the held ending scales `output.master` by a ramp. |

Sections accepted by W1: `session`, `source`, `form`, `stem`, `part`,
`rule`, `groove`, `curve`, `lane`, `marker`. `import`, `events`, `play` and
roll overlays are parse errors until W3b/W4/W11 define them.
`wb_session_validate` refuses organ and ep73 parts until W9.

The fixture `workbench/test/fixtures/hurt-v2.mws` restates
`make_hurt_orchestration.py --edition 2` and `exhibit_ma3_hurt_orch.c
--edition 2` as a session (ten parts, six stems, ten rules, kick and snare
grooves, the energy curve, fourteen lanes). It is canonical text: the writer
reproduces its bytes. Whether it renders and builds identically is the W2
and W3 gate, not proven here.

## Gates

| Gate | Result |
| --- | --- |
| Desk unit tests (`build/test_workbench`) | 238 checks, 0 failed: SHA-256 vectors incl. 10^6 × 'a', shortest f32/f64 text round trip over 2 × 200 000 bit patterns, fixture byte round trip, CRLF/comment equivalence, 36 strict rejects with line numbers, 25 semantic rejects, key set/reset, block insert/remove, journal replay/undo/redo/truncate/stale, source copy + patch file checks |
| CLI (`workbench/test/wb_cli.sh`) | ok: root-init idempotent, new, add-source read-only copy, add/set/get/undo/redo/history, `check` with and without files, fmt canonical on the fixture, checkpoint commit without trailers, stale journal set aside |
| Compilers | gcc 16.2.1 and clang 22.1.8, `-Wall -Wextra -Wpedantic`, no warnings |
| Sanitizers | `test-workbench` under ASan + UBSan + float-cast-overflow with leak detection: clean |
| Fuzz (`make fuzz-mws`, 90 s) | 484 254 runs, no crash; every accepted input writes text that reads back and writes the same bytes |
| Headless UI smoke | `SDL_VIDEO_DRIVER=dummy build/workbench --smoke workbench/test/fixtures/hurt-v2.mws`: 34 frames (6 presets × their tabs × 2 themes, console both ways, pinned window), also clean under ASan + LSan |
| Window on both monitors | Wayland, DP-1 2560×1080 + eDP-1 1920×1080: main window maximized on DP-1, pinned Roll view fullscreen on eDP-1, checked by screenshot |
| Session root | `/data/tonewheel91-sessions/` created by `wb root-init`, first commit `Initialize the session root`, no remote |
| Core untouched | `src/` unchanged; `build/test` 111 378 checks, 0 failures; `check-core-symbols` clean |

The UI build in this note was compiled against SDL 3.4.18 headers matching the
installed runtime (`libSDL3.so.0.4.18`), passed via `SDL3_CFLAGS`/`SDL3_LIBS`,
because `SDL3-devel` was not installed yet. With `SDL3-devel` installed,
`make workbench` finds SDL3 through `pkg-config sdl3`.
