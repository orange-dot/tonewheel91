# Workbench — plan for an expert desk over the tonewheel91 engines

Date: 2026-11-07. Status: W0, W1, W2, W3, W3b desk/CLI parts landed with
their parity gates; W6, W9 and W10 desk/CLI parts landed; the UI views (W4/W5,
W6/W10 screens), live audio (W7/W8/W11/W12) are open. Evidence:
`docs/workbench-w0-w1.md`, `docs/workbench-progress.md`.

The Workbench is a native C23 application in this repository. It covers the
whole loop that renders and experiments currently go through: inspect a source
MIDI, orchestrate it onto engine parts, shape patches, probe short sections,
balance, run the final single-pass render, prepare listening copies, compare
takes and keep provenance. The operator does this alone, with no agent in the
loop.

It is coupled to the engines as tightly as the layering allows. It links the
Mamut Analog pool, the organ instrument and the ep73 piano directly, edits
their concrete structs (`ma_patch`, `ma_voice_pool_config`, organ console
state, ep73 controls), and renders through the same desk code the headless
CLI uses. No plug-in host, no generic parameter registry in the core, no
network protocol.

## 0. Decisions already taken

| Question | Decision |
| --- | --- |
| Platform | Native C23, one process, links the cores directly. |
| Toolkit | SDL3 (window, input, 2D renderer) + Nuklear (stock widgets) + custom-drawn canvases. Fonts through stb_truetype. All vendored or system packages, no C++. |
| Sound | Both: offline renders are the source of truth, plus live audition on ALSA (AG03). |
| Scope | Mamut Analog, organ (tw91) and ep73 in one orchestration. |
| Import | Any existing MIDI arrangement can be imported and split into parts by track, channel, track × channel, program, drum note, pitch split or by hand (§5.2). |
| Live play | The Workbench takes over all live playing: the organ driver (`main.c`), the ep73 driver (`ep73.c`) and Patchlab's live mode. The old drivers stay as references until parity is proven (§6.4). |
| Input | No hardware MIDI for now. Inputs are the computer keyboard, the mouse and on-screen controls; the input layer has one adapter slot for MIDI later. |
| Sessions | A new dedicated folder outside the repository, kept under its own local Git history: `/data/tonewheel91-sessions/` (§3.1). |
| Final render | Always the single-pass desk render (stems + mix written in one pass, no reference pass). Everything else is a preview. |
| Host | i7-4600U, 2 cores / 4 threads, 8 GiB; screens 1920×1080 and 2560×1080. The layout is designed for **wide and short**: horizontal density is cheap, vertical space is scarce. |

Non-goals:

- No change to the freestanding cores (`src/`). The core symbol audit and
  pinned PCM signatures stay as they are.
- No automatic gain riding, limiter or "smart mix". Balance tools only measure
  and show numbers; the operator sets the gains.
- No listening questionnaires. A take gets one free-text verdict field.
- No audio recording, plug-in hosting, VST/LV2/CLAP or generic DAW timeline.
- No writes into tracked docs. `docs/renders.md` and
  `docs/ma3-musical-exhibits.md` stay internal and are edited by hand; the
  Workbench only exports a snippet to the clipboard.

## 1. The loop it replaces

Today's Hurt orchestration took this path. Each row is a Workbench view.

| Step today | How it was done | Workbench | Target turnaround |
| --- | --- | --- | --- |
| Inspect sources | ad-hoc Python (`hurt_midi_inspect.py`, a per-4-bar profile) | **Sources** view: track table, density heatmap, long/late notes, SHA256 | instant on load |
| Take over an existing arrangement | not possible without code: every arrangement needed a generator or exhibit | **Import**: split a MIDI file into parts by track, channel, program, drum note or pitch, with engine and patch suggestions | under a minute |
| Choose the base score | reading two files side by side | Sources: alignment of several files (bar offset, tempo), shared bar ruler | instant |
| Orchestrate | a generator script (`make_hurt_orchestration.py`) with `take(...)` rules | **Orchestrate** matrix: source track × part rule cells; derived MIDI rebuilt on every edit | < 50 ms per edit |
| Add a groove | a Python `groove()` function with section levels | **Groove** step grid per drum lane, pattern per section | < 50 ms per edit |
| Form changes (cut intro) | shift all ticks by 8 bars in code | **Form** ribbon: start bar, end bar, tail, sections | instant |
| Patches | C helper chains (`tepih → pluck → dark`) | **Patch** editor with parent chain and override diff; organ and ep73 panels | live audition |
| Levels | three probes via a hand-made excerpt script, RMS by hand | **Probe**: bar range with pre-roll, per-stem and per-section table, peak driver | 1–3 min in background |
| Final render | build a binary, run under `systemd-inhibit`, read the log | **Render queue**: one click, inhibitor built in, progress/ETA | ~9–11 min on this host |
| Listening copy | `prepare_hurt_listening.py` | automatic after the render | seconds |
| Provenance | hand-written `.md` next to the WAV | **Ledger**: automatic manifest, verdict field, export snippet | automatic |
| A mistake like the stem slot in v2 | found afterwards in the stem peaks | stem routing visible on each mixer strip; check refuses an empty or unnamed stem | prevented |

## 2. Architecture

### 2.1 Layers

```
src/                 freestanding cores (unchanged)
  mamutanalog.h  tonewheel.h  epiano.h
        │  direct calls, concrete structs
workbench/desk/      hosted library, no UI, no ALSA
  session model · file formats · orchestration engine · groove
  automation · desk renderer · probe · analysis · cache · ledger
        │                                 │
workbench/cli/  headless `wb`            workbench/audio/  ALSA thread
  check · import · build-midi ·            live engine · play mode
  probe · render
        │                                 │
                 workbench/ui/  SDL3 + Nuklear + canvases
```

Dependency rules:

- `desk/` uses only `src/` and the existing hosted helpers (`smf.c`,
  `wav.c`, `ma_patch_file.c`, `host_parse.c`, `live_io.c` stays in `audio/`).
  It never includes SDL or ALSA headers.
- `cli/` and `ui/` both call `desk/`. Every render, probe and MIDI build the UI
  can start, the CLI can start with the same session file. That is the parity
  guarantee: `wb render session.mws` and the UI's Render button run the same
  function on the same bytes.
- `audio/` owns live playback. It reuses `desk/` part adapters but has its own
  real-time thread rules (§6).
- The existing exhibits (`exhibit_ma3_*`, `render_midi`) stay. The Workbench
  replaces them for new work only.

### 2.2 Source tree

```
workbench/
  desk/   wb_session.[ch]  wb_format.[ch]  wb_orch.[ch]  wb_groove.[ch]
          wb_form.[ch]  wb_auto.[ch]  wb_part.[ch]  wb_render.[ch]
          wb_probe.[ch]  wb_analysis.[ch]  wb_cache.[ch]  wb_ledger.[ch]
          wb_hash.[ch]  wb_undo.[ch]
  cli/    wb_main.c
  audio/  wb_live.[ch]  wb_midi_in.[ch]  wb_queue.h (SPSC rings)
  ui/     wb_app.c  wb_layout.[ch]  wb_views_*.c  wb_canvas_*.c
          wb_keys.[ch]  wb_theme.[ch]  wb_font.[ch]
  third_party/  nuklear.h  stb_truetype.h  (+ license files)
  test/   wb_test.c  fixtures/ (synthetic MIDI only, no copyrighted scores)
```

Makefile targets (added in W0): `workbench`, `wb`, `test-workbench`,
`check-workbench-symbols`. `make test` gains `test-workbench` only after W2.

### 2.3 Threads on a 2-core / 4-thread host

| Thread | Priority | Work | Rule |
| --- | --- | --- | --- |
| UI | normal | events, layout, drawing at most 60 Hz, redraw on change only | never blocks on audio or workers |
| Audio | SCHED_FIFO if allowed, else nice −10 | live engine, 128-frame periods at 48 kHz | no malloc, locks, I/O or logging; symbol-audited |
| Input | (UI thread) | computer-keyboard and on-screen notes/controls → SPSC ring to audio, timestamped | key repeat ignored; a MIDI adapter can be added later without changing this path |
| Workers ×1–2 | nice 10 | probes, part-cache renders, MIDI builds, analysis | while live audio runs: 1 worker; otherwise 2 |
| Writer | normal | WAV writes and SHA256 of finished files | — |

A final single-pass render runs alone by default. The UI shows it and blocks
live audio and new probes until it finishes, because timing contamination and
memory pressure on 8 GiB both matter. The block can be overridden for a single
run, and the manifest records that it was.

### 2.4 Memory budget

A Mamut pool with effects is about 0.77 MB (ten pools reserve 7.67 MB).
Workbench memory targets: session + derived score < 20 MB; live engine < 20
MB; part cache on disk, not in RAM (memory-mapped while playing). The UI shows
its RSS in the status bar, because the host often sits at 6 of 8 GiB.

## 3. Data model and files

### 3.1 Session directory

A session is a directory. All sessions live in one dedicated folder outside
the tonewheel91 repository, so they are never pushed with the public code
(they reference and cache copyrighted scores) and are never lost with a build
directory:

- Root: `/data/tonewheel91-sessions/`, on the `/data` disk next to the lab.
  The path is configurable (`WB_HOME`), and the UI shows it in the title bar.
- The root is its own **local Git repository with no remote**. The Workbench
  commits the small text files (`.mws`, patches, manifests, ledgers,
  verdicts) on every checkpoint and every finished run, with a generated
  message (`checkpoint v2`, `run 20261107-1523-v2 final`). Large files
  (`cache/`, run WAVs, `derived/`) are in that repo's `.gitignore` and stay on
  disk only; a run's FNVs and SHA256s in the committed manifest are enough to
  verify them later.
- Source MIDI files are **copied** into the session (`sources/`, read-only,
  SHA256 checked on every load), so a session stays complete if
  `notes-midi/local/` changes. Copies are small, and keeping them is the
  point of this folder.
- A `README.txt` at the root says what the folder is and that nothing in it
  may be published. Nothing under the root is ever deleted by the Workbench;
  "delete run" moves the run to `trash/` inside the session.
- Listening copies can still be copied into `renders/<area>/` in the repo
  (gitignored) as today.

```
/data/tonewheel91-sessions/
  .git/  .gitignore  README.txt
  hurt/
    hurt.mws                     session file (strict text, versioned)
    hurt.mws.journal             undo journal
    sources/ *.mid               read-only copies, SHA256-checked
    patches/ *.mapatch *.twreg *.epset   part patches (session-local)
    takes/   *.mid               recorded live takes (§6.4)
    derived/ hurt-<orchhash>.mid rebuilt orchestration MIDI   (not in Git)
    cache/   <parthash>.f32      per-part preview audio        (not in Git)
    runs/    20261107-1523-v2/   manifest in Git, WAVs not
    trash/                       moved-away runs
    ledger.tsv                   one line per run
```

### 3.2 The session file (`.mws`)

The format is strict `key=value` text in `[section]` blocks, like
`.mapatch`. A version line comes first. Unknown keys, duplicate keys, missing
required keys and out-of-domain values are errors with line numbers. Numbers
are written with round-trip f32 digits. Saves go to a temporary file with
fsync and an atomic rename, the same code path as `ma_patch_save`. There is
no JSON and no general-purpose config library.

Required sections: `session`, `source` (≥1), `form`, `part` (1..16),
`stem` (1..8). Optional: `import`, `rule`, `groove`, `pattern`, `lane`,
`events`, `marker`, `play`.

Tempo: a source may carry a full tempo map and time-signature changes. The
desk converts ticks to frames through the map (as `render_midi` already
does), and the bar grid follows the time signatures. `tempo=source` keeps
the map; `us_per_quarter=` replaces it with one constant tempo; `tempo_scale=`
scales every tempo.

Today's v2 as a session file (abbreviated):

```
mws=1
[session]
name=Hurt orchestra v2
rate=48000
control_frames=480
engine=auto

[source]
id=sp
path=sources/hurt-songparts-vocals.mid    # copied on import
sha256=1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0
offset_bars=0
tempo=source            # or us_per_quarter=714285 to retime

[form]
start_bar=8
end_bar=100
tail_seconds=10.0       # renderer derives 283 s
fade_in_ms=500
fade_out_ms=2000
section=verse1 8 24
section=build 24 40
section=drums1 40 48
section=verse2 48 68
section=drive 68 76
section=climax 76 91
section=hit 91 92
section=ending 92 100

[stem]  id=tonal
[stem]  id=bass
[stem]  id=pads
[stem]  id=vocal
[stem]  id=lead
[stem]  id=rhythm

[part]
id=arp
engine=mamut
patch=patches/arp.mapatch
limit=16
unison=1
detune_cents=4
seed=name               # FNV of the id, as ma3_show does
character=0.20
chorus=0.12
reverb=0.18
gain_db=0.00
stem=tonal
pressure=0.12

[rule]
part=arp
from=sp:"Guitar 2"
ring_max_bars=1.5
dedupe=restrike         # restrike | keep-loudest | off

[rule]
part=snare
from=sp:"Drums"
notes=37
map_to=52
velocity_scale=0.55

[groove]
part=kick
note=35
length_ticks=96
pattern=light   0 8           vel=66
pattern=pulse   0 8           vel=74
pattern=mid     0 6 8         vel=92
pattern=full    0 6 8 11      vel=108 accent0=+6
wobble=7,3,9               # (bar*7 + step*3) % 9 - 4
use=8..16:light 16..24:pulse 24..40:mid 40..48:full 48..56:pulse 56..68:mid 68..91:full
hit=91 vel=124

[lane]
part=vocal
param=filter.cutoff_hz
shape=smoothstep
point=8 900
point=92 340
```

The session holds rules, not notes. The derived MIDI is a build artifact
named by the hash of its inputs, so two identical sessions produce
byte-identical MIDI. That is how `make_hurt_orchestration.py` already works,
and the W3 parity gate (§9) holds the Workbench to it.

### 3.3 Part engines

A part has one engine kind. The adapter is a tagged union with a `switch`,
not a vtable:

| Engine | State | Patch file | Note path | Sleep semantics |
| --- | --- | --- | --- | --- |
| `mamut` | `ma_voice_pool` + effects | `.mapatch` v3 (existing reader) | `ma_voice_pool_note_on/off`, poly pressure | pool `active_slots == 0` |
| `organ` | `tw_instrument` (+ rotary, drive) | `.twreg` (new: registration digits, percussion, vibrato mode, drive, rotary mode/speed/balance/width/drive, swell, wear) | the organ MIDI map; key depth on poly pressure | output below −120 dBFS for 1 s after the last release (no voice sleep exists) |
| `ep73` | `ep_piano` (bank, tremolo, cabinet) | `.epset` (new: condition, tremolo rate/depth/mode, cabinet) | `ep_bank_strike/damp/undamp` | bank gated silence after the last damp |

Both new formats follow `.mapatch` rules exactly: strict, versioned,
round-trip, field catalog with min/max/fine/coarse steps in one table. The
editor UI reads that table, so ranges are never typed twice.

### 3.4 Run manifest (`runs/<id>/manifest.mwr`)

Written once at the end of a run and never edited afterwards, except for the
verdict, which goes in a separate `verdict.txt`.

- id, kind (`final` | `probe` | `part-cache`), label, start/end wall time
- session file SHA256, source SHA256s, derived MIDI SHA256, every patch file
  SHA256
- binary SHA256, `git describe --dirty`, compiler and flags, engine (AUTO/AVX2
  or scalar)
- host: CPU model, governor, running `code` / browser processes (contamination
  flags), whether live audio was blocked
- per stem and mix: FNV64, peak, RMS, frames; stem sum error
- per part: notes, held peak, slot peak, steals R/S/H, sleeps, FX clips,
  sanitizations
- listening copy: gain dB, SHA256, peak and RMS dBFS
- per section from `[form]`: stem RMS. This fixes the hardcoded 84 BPM grid of
  `prepare_hurt_listening.py`.

`ledger.tsv` keeps one line per run: id, kind, label, mix FNV, listening
SHA256, wall time, verdict (first line).

## 4. Screen layout

### 4.1 Global frame (2560×1080 primary, 1920×1080 secondary)

```
┌─ transport & form ribbon (2 rows, 44 px) ───────────────────────────────────────────────────────────┐
│ ▶ ■ ⟲  bar 76.1.000  02:32.4  81.0 BPM   [verse1|build|drums1|verse2|drive|climax|hit|ending]  CPU 41% ░ │
├─ parts (fixed 300 px) ─┬─ workspace (tabs, flexible) ─────────────────────────────┬─ inspector 380 px ─┤
│ ■ arp     mamut  T  S M│                                                          │ selected object:   │
│ ■ keys    mamut  T  S M│   Sources | Import | Orchestrate | Roll | Groove |       │ part / rule / note │
│ ■ bass    mamut  B  S M│   Patch | Mixer | Probe | Render | Listen | Ledger | Play │ / patch field /    │
│ ■ vocal   mamut  V  S M│                                                          │ run                │
│ ■ pad     mamut  P  S M│                                                          │ exact values,      │
│ ■ kick    mamut  R  S M│                                                          │ ranges, diffs      │
│ ...                    │                                                          │                    │
├─ console / queue (collapsible, 1 line collapsed, 8 lines open) ─────────────────────────────────────────┤
│ 15:23 probe climax 76–92 done  mix −23.0 dBFS peak, peak driver: rhythm (toms)  [open]                  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- Fixed layout presets instead of free docking: `F1` Orchestrate,
  `F2` Patch, `F3` Mix/Probe, `F4` Listen, `F5` Ledger, `F6` Play. Each preset fixes
  which tabs are open and which tabs go to the second monitor. Docking is
  expensive to build and gets in the way on a short screen.
- Second monitor (1920×1080): a pinned view, by default the Roll or Patch
  editor, at full height.
- Vertical rules: no panel header above 20 px, 18 px table rows, 13 px
  monospace text (one font family with tabular figures), no rounded cards, no
  padding beyond 4 px. Separation comes from 1 px lines and alignment, not
  whitespace.
- Color carries state only: clip/failure red, released-steal amber,
  held-steal red, cached part green tick, stale cache grey, live part blue.
  Parts get a fixed small color swatch for identification in the roll.
  Dark theme first, with a light theme that keeps the same contrast tokens.
- Every number is an exact value. The display shows enough digits for an f32
  round trip on focus and a rounded value at rest. Units are always shown:
  dB, Hz, ms, cents, bars.

### 4.2 Parts column (always visible)

One row per part: swatch, id, engine, stem letter, live/cache state, solo,
mute, and a vertical meter in the row background (post-gain RMS and peak
while playing). Keys: `J/K` move, `Space` audition from cursor, `S`/`M` solo
and mute (preview only, never written to a final render), `Enter` opens the
part inspector, `Ctrl+D` duplicates a part (patch copy + rules), `Del` with
confirmation. Drag to reorder. Order sets the stem summation order in final
renders, and the inspector says so.

## 5. Views in detail

### 5.1 Sources

Purpose: understand the input before orchestrating. This replaces
`hurt_midi_inspect.py` and the per-4-bar profiler.

- Header per file: path, SHA256, format, PPQ, tempo map, time signatures,
  length in bars and seconds, known/unknown badge (known = listed in the
  session with the matching hash).
- Track table: index, name, program, channel, notes, pitch range (names and
  numbers), velocity min/median/max, duration median/max, held peak, first and
  last bar, long-note list (> 2 bars), CC and pressure counts.
- Density heatmap: tracks × bars, cell = note count; hovering a cell shows
  the notes. The section ribbon is drawn above it, so the structure of the
  score is visible at a glance (that is how the Songparts layout was read
  today).
- Pitch-class histogram per track and per section, used to check harmony
  derivations.
- Alignment of a second file: offset in bars and retime option; both files
  drawn on one bar ruler, anchors (entries) marked; the 8-bar vocal shift from
  `ma-hurt-vocal.md` is one numeric field.
- Actions: `A` adds a rule from the selected track to the selected part;
  `P` previews the selected track live on the selected part.

### 5.2 Import

Purpose: take an existing MIDI arrangement as it is and turn it into parts
in one step, without writing rules by hand. Import is a mode of the Sources
view with its own preview table.

**Entry points.** "New session from MIDI" (`Ctrl+Shift+O`), "Add source" in
an open session, dropping a `.mid` file on the window, and the headless
`wb import file.mid --split track+channel --session /data/tonewheel91-sessions/x`.

**What is read.** SMF format 0 and 1, PPQ division, the full tempo map, time
signatures, key signatures, track names, instrument names, markers, text and
lyrics, program changes (including mid-track changes) with bank select, and
every channel event: notes, velocity, CC, channel and poly pressure, pitch
bend. SMPTE division and format 2 are refused with a clear message. Running
status, zero-velocity note-offs, unmatched note-offs and stuck notes are
reported as counts and fixed in a stated way (FIFO pairing per pitch, stuck
notes closed at the track end).

**Split strategies.** The preview shows the resulting part list before
anything is committed; switching strategy re-runs the preview instantly.

| Strategy | Result | Typical use |
| --- | --- | --- |
| track | one part per MTrk | format-1 files with one instrument per track |
| channel | one part per channel, tracks merged | format-0 files, or files that put one instrument on several tracks |
| track × channel (default) | one part per (track, channel) pair that has notes | the safe default; nothing is merged that the file kept apart |
| program | one part per program segment of a channel | a channel that changes instrument mid-song |
| drum notes | one part per note group of a drum channel; default groups: kick 35/36, snare 38/40, side stick 37, toms 41/43/45/47/48/50, hats 42/44/46, crash 49/57, ride 51/53/59, other | turning a GM kit into separate Mamut voices, as the v2 kick/snare/toms split did |
| pitch split | two parts at a split note, or top voice vs. the rest (skyline: the highest sounding note at each moment) | piano tracks with a melody over accompaniment |
| manual | a two-column table: source streams on the left, part buckets on the right; drag or type the bucket number | everything else |

Strategies combine per source stream: for example track × channel for
everything, drum notes for channel 10, and a pitch split for the piano track.
Empty streams (no notes) are listed but create no part.

**Engine and patch suggestion.** Each proposed part gets an engine and a
patch template from the program number (GM families) and the track name.
The mapping is a table the operator edits once
(`/data/tonewheel91-sessions/import-map.mws`):

| Source | Engine | Template |
| --- | --- | --- |
| pianos 0–7 | ep73 (electric pianos 4–5) or Mamut keys | `keys` |
| organs 16–23 | organ | registration `888000000`, rotary chorale |
| guitars 24–31 | Mamut | `pluck` (clean), `drive` (overdriven/distortion 29–30) |
| basses 32–39 | Mamut | `bass` |
| strings, ensembles, pads 40–55, 88–95 | Mamut | `pad` |
| leads 80–87 | Mamut | `lead` |
| effects 96–103, 120–127 | Mamut | `wash` (noise) or none |
| drum channel / drum-note groups | Mamut | `kick`, `snare`, `toms`, `stick`, `hats` templates |
| no program, unknown name | Mamut | `tepih` |

Suggestions are pre-filled and marked as suggestions; nothing is applied
until the import is committed.

**Per part, in the preview table:** notes, pitch range, velocity range,
held peak, the suggested limit (held peak × unison, rounded up to a valid
pool limit ≤ 64 that unison divides), engine compass check (organ 36..96,
with a fold option), first/last bar, a mini roll, and an audition button that
plays that stream live on the suggested engine from the playhead.

**Controllers.** Each controller stream gets a choice: keep, convert or drop.

| Source event | Kept as | Notes |
| --- | --- | --- |
| CC7 volume | part gain lane (dB) | or folded into one constant gain per part |
| CC11 expression | expression lane; organ swell natively | — |
| CC10 pan | part pan (hosted, equal-power, in the desk mix) | the cores have no per-part pan; pan is a desk feature |
| CC64 sustain | `ma_voice_pool_set_sustain`; ep73 damper; organ: dropped | — |
| CC1 mod wheel | mod wheel | — |
| pitch bend | pitch bend (range from RPN 0 if present, else ±2) | — |
| channel / poly pressure | pressure | organ key depth uses poly pressure as `fullauto.py` does |
| organ console CCs 70–90 | organ console events | only for parts on the organ engine |
| other CCs | dropped, counted | — |

Controller streams are stored as **event lanes**: exact timestamped events
dispatched at their sample, like notes. Curve lanes (§3.2) are evaluated
every 480 frames and are for drawn automation. The two kinds never mix in one
lane.

**Velocity.** Keep, scale, or remap through a curve per part.

**Form from the file.** Markers and text events become suggested sections;
the end bar is the last release; the start bar is the first note, with an
option to keep leading silence.

**Linkage and re-import.** An import writes one `[import]` block per source
(strategy, groups, split points) and one rule per part
(`from=src:track=3,channel=2,program=26,notes=35-36`). The parts stay linked
to the source. If a source file is replaced (new SHA256), re-import shows a
per-part diff (notes added/removed/moved, controller counts) and offers to
relink, keeping patches, gains and hand overlays.

**Round trip.** Exporting the derived MIDI (one track per part, part names,
one channel per part) and importing it again with the track strategy gives
the same parts and the same notes. Today's `hurt-orch-20261107-v2.mid`
imports into ten parts this way.

### 5.3 Orchestrate (rule matrix)

Rows are source tracks, columns are parts. A cell holds a rule or is empty.

- Cell text, compact: `−12 ≤1.5b v×.55 n37→52`. The inspector edits the full
  rule: note filter (list or range), transpose, octave fold below/above,
  ring cap (bars), minimum length, velocity scale/offset/curve, bar ranges
  (in/out), channel filter, dedupe policy, humanize (deterministic formula,
  shown).
- Under the matrix, one line per part: attacks, held peak, held peak × unison
  against the limit (red when `ma3_check` would refuse), number of dedupe
  merges, range, first/last bar. This is `make_hurt_orchestration.py`'s
  printout, live.
- Conflict list: same-pitch overlaps, zero-length notes, notes outside an
  engine's compass (organ 36..96 unless fold is on), notes after `end_bar`.
  Each entry jumps to the Roll.
- Every edit rebuilds the derived score in a worker. A 600-note part rebuilds
  in well under 50 ms. The derived MIDI is written only on render or export.

### 5.4 Roll

The piano roll of the derived score: all parts, or only the selected parts.

- Wide and short: time runs horizontally across the 2560 px; the pitch axis
  folds to the used range per part (lanes stacked, each only as tall as its
  range). A toggle switches to one shared pitch axis.
- Note colors by part; a note's source rule appears on hover. Velocity is a
  thin bar under each lane.
- Overlays: held-voice count per part (step line) against limit/unison;
  released-steal markers from the last probe or render; automation lanes as
  thin curves below the part's notes.
- Editing: the roll is a view of rules. Direct note edits go into an
  **overlay** layer per part (add, delete, move, length, velocity). The
  overlay is stored in the session and listed in the inspector, so rules stay
  the primary source and hand edits stay visible and reversible.
- Navigation: `H/L` move by beat, `Shift+H/L` by bar, `[`/`]` previous or next
  section, `Z` zoom to selection, `0` whole form. The playhead and loop range
  are shared with transport.

### 5.5 Groove

Step grid for derived kit parts (kick, snare, or any part).

- Patterns: named rows of 16 (or 12/24) steps with per-step velocity and an
  optional accent. Several lanes (kick, snare, hats) can be edited together
  on one grid.
- Section assignment: a strip under the form ribbon with one pattern per
  section, or per bar range. A one-off hit (bar 91) is a marker.
- Ghost notes from the source: a rule (as in v2's side-stick → snare) shown
  dimmed in the grid.
- Deterministic humanize: the formula and its constants are visible and
  editable. No random state.
- `Space` loops the current section live with the kit parts plus whatever is
  solo.

### 5.6 Form

The ribbon at the top plus a full view:

- start bar, end bar, tail seconds, fade-in/out, total length readout (in s
  and frames)
- sections with names and colors, drawn everywhere
- energy curves: named breakpoint curves over bars that lanes can reference
  (`energy` in v1/v2). Lanes say `param=macro.bloom expr=.05+.12*energy`.
  Expressions are restricted: one curve, scale, offset, clamp.
- markers: probe ranges, listening excerpts, notes for later.

### 5.7 Patch (per engine)

**Mamut panel.** All 50 `.mapatch` fields in signal-flow order on one wide
screen, in columns:

```
VCO1        VCO2         SOURCE      RASTER    BCS     MOZAIK      FILTER        AMP ENV    FILT ENV   MACROS       OUTPUT
saw  .120   saw  .030    sync  .000  mix .070  amt .06 mix .055    press .045   A  260 ms   A 180 ms   grav .000    body .055
pulse .000  pulse .000   soft  .000  pos .240  reg .14 slope .54   cut  980 Hz  D 1250 ms   D 1500 ms  bloom .070   width .340
tri  .480   tri  .320    xmod  .000  warp .050         contr .57   res  .140    S  .640     S  .340    heat .000    xfeed .120
sine .340   sine .560    noise .012                    phason .00  drive .065   R 2900 ms   R 2500 ms  ruin .000    master .150
pw   .500   pw   .500                                  drift .065  env  .310                           swarm .000
            level .520                                             key  .000
            int  0  fine 0.0
```

- Every cell is editable with: type a number, `↑/↓` fine step, `Shift+↑/↓`
  coarse step (steps from the field catalog), `Ctrl+drag` horizontal scrub,
  `Backspace` reset to parent, `=` copy from A/B other slot.
- **Parent chain**: a patch can name a parent (factory `tepih`, or another
  session patch) and store only overrides. Overridden fields are bright, and
  inherited fields are dim with the parent's name on hover. "Flatten" writes a
  full `.mapatch`. This replaces the C helper chains (`dark_patch(pluck_patch(0))`).
- **A/B slots**: two versions of the same patch, `Tab` toggles live while
  playing; a diff column lists only fields that differ.
- **Part config** next to the patch: limit, unison, detune, seed, character,
  chorus mix/depth/rate, reverb mix/size/damping, gain dB, stem, poly
  pressure.
- **Scope strip** under the grid (live): the part's output waveform (last 50
  ms), spectrum (log, 1/6 octave), envelope follower, and the pool's active
  slots per frame.
- **Audition**: computer keyboard (two octaves, `Z`..`/` and `Q`..`P`
  rows, octave on `−/+`), the on-screen keyboard, or "play the part's notes from the
  playhead". Velocity from a fixed value or the source.

**Organ panel.** Nine drawbars as vertical sliders with digit entry
(`888000000` typed directly), percussion (on, harmonic, decay, volume),
vibrato/chorus selector (off, V1..V3, C1..C3), drive, rotary mode / speed /
balance / width / drive, swell, wear. Automation directives from `fullauto.py`
(`cc`, `ramp`, `speed`, `perc`, `depth`) become lanes and markers in the
session, edited on the Roll.

**ep73 panel.** Condition, tremolo rate/depth/mode, cabinet, and the per-key
offsets if the core exposes them later (not assumed).

### 5.8 Mixer

One strip per part, horizontally (16 strips fit in 2560 px at 150 px each):
engine, stem selector (a dropdown that lists only defined stems, so a part
cannot silently land in another part's stem), gain dB fader with typed entry,
chorus and reverb sends, limit/unison readout, live meters (RMS, peak, slots),
and the latest probe numbers in small type under the fader. Stem buses on the
right with their meters, then the mix.

The summation order of stems is shown and editable, because it is part of
the bit-exact result.

### 5.9 Probe

A probe renders a bar range offline in a worker and measures it.

- Range from the selection, a section or a marker. **Pre-roll** (default 4
  bars) is rendered and discarded, so envelopes, reverbs and the automation
  state at the range start are correct. That fixes two flaws of today's
  excerpt hack: it dropped notes that started before the range, and the
  automation saw bar 0 instead of the real bar.
- Options: parts subset (solo), engine (AUTO/scalar), write WAVs or measure
  only.
- Result table (per part, per stem, mix): RMS dBFS, peak dBFS, crest dB,
  share of the mix peak, steals, slot peak. A **peak driver** line names which
  stem sets the mix peak, which is what decides the listening normalization.
  Today, the toms setting the normalization was found by hand.
- Comparison with the previous probe of the same range: Δ dB per stem,
  colored.
- "Apply suggested gain" never runs by itself. A button shows the gain
  change that would put a stem at a typed target relative to another
  (for example vocal = arp + 1.5 dB) and applies it only on confirmation.
- Probes write a manifest like final runs (kind `probe`), so they show up in
  the Ledger.

### 5.10 Render

The queue of final renders.

- One click (or `Ctrl+R`) queues a final single-pass render of the current
  session state. The session is snapshotted (copied into the run directory)
  at queue time, so edits during a render do not change it.
- Pre-flight: full check (as `ma3_check` plus the new engine checks), disk
  space for stems + mix (6 stems × 283 s ≈ 650 MB), contamination scan
  (browsers, `code`), a lid-close note. Nothing starts with a failed check.
- Sleep inhibition through the logind D-Bus inhibitor for the run (the same
  effect as `systemd-inhibit --what=sleep:idle`), released at the end or on
  cancel.
- Progress: rendered seconds, wall time, ETA from measured speed, per-part
  slot peaks so far. Cancel removes incomplete files, as `ma3_render` does.
- At the end: listening copy at −3 dBFS peak (constant gain), section table,
  manifest, ledger line, optional copy of the listening file to
  `renders/<area>/` under a name template (`<show>_listening_<date>.wav`)
  that refuses to overwrite.

### 5.11 Listen

A player for rendered runs, not for live playback.

- Waveform of the mix with the section ribbon, loudness lane (400 ms RMS),
  spectrogram lane, and markers for steals and FX clips from the manifest.
- Stem solo/mute from the rendered stems (sums the chosen stems in memory).
  It never re-renders.
- A/B between two runs: RMS-matched constant gain (the method of
  `prepare_hurt_listening.py --reference`), instant switch with `Tab` at the
  same position, a loop range, and an offset field when the forms differ (v1
  started 8 bars earlier than v2).
- Verdict: one text field per run, saved to `verdict.txt` and the ledger line.

### 5.12 Ledger

All runs of the session, newest first: label, kind, date, length, mix FNV,
listening SHA256, wall time, contamination flags, verdict. Actions:

- **Diff** two runs: session diff (rules, patches field by field, gains,
  form), output diff (per stem FNV equal/different, Δ RMS per section).
- **Reproduce**: re-run a past final render from its snapshotted session and
  compare FNVs (determinism check).
- **Export snippet**: a Markdown block with commands, hashes and FNVs in the
  style of `renders.md`, copied to the clipboard. It is never written to a
  tracked file.

## 6. Live audio

### 6.1 Engine

- ALSA through `live_io.c`, 48 kHz, 128-frame periods, 3 periods, AG03 by
  name with a device picker.
- The audio thread owns live instances of the parts being auditioned. The UI
  sends commands through lock-free SPSC rings: note on/off, parameter set
  (part, field index, value), patch swap, transport (play, stop, locate,
  loop), panic. The audio thread applies them between frames, which matches
  the core rule that setters are called on the render thread between frames.
- Meters and slot counts flow back through a triple buffer read by the UI.
- Output safety: per-sample finiteness check on the live mix. On a
  non-finite sample or more than 0 dBFS for more than 10 ms, the output is
  muted, all parts are panicked and a red banner appears. The live path never
  changes render settings.

### 6.2 Hybrid playback (frozen + live)

The current pool engine meets an 8-slot live budget on this host; 32 slots
are still open in MA3-PERF. A full arrangement (46 aggregate active slots in
v2) cannot play live. So playback mixes two sources:

- **Frozen parts**: per-part audio from the part cache (`cache/<parthash>.f32`,
  stereo f32, rendered offline from bar 0 so state is exact). The audio thread
  plays it from memory-mapped files, which costs nothing in DSP.
- **Live parts**: the selected or edited parts, run in real time from the
  playhead, with a pre-roll silently rendered at locate time (fast,
  non-real-time, before the first audible period) so envelopes and reverbs
  are settled.

The cache key is the hash of everything that affects that part's audio:
engine kind, patch bytes, part config, the part's derived events, its lanes
and the curves they use, form start/end, rate, engine selection, and the
binary SHA256. Edit a patch, and only that part's key changes. Its row turns
grey, a worker re-renders it in the background, and it turns green when done.
Until then the part plays live if the budget allows, or it stays muted with a
"stale" mark.

The **budget meter** in the ribbon shows live DSP time per period against
2.667 ms with an 80% line (the MA3-PERF numbers). Adding a part to the live
set predicts its cost from its slot count and refuses beyond the budget,
with "freeze something first". A one-key action `F` freezes the selected part.

Cache previews are approximations of the final render: stems are summed in
another order, so the bits differ. Final renders always run as one pass from
the session snapshot. The UI marks cached playback as "preview" in the
transport.

### 6.3 Inputs

No hardware MIDI for now. All playing comes from the computer keyboard, the
mouse and on-screen controls.

- **Computer keyboard as manuals.** Two keyboard rows are two octaves of one
  manual (`Z`..`/` lower, `Q`..`P` upper, the number row for the black keys
  of the upper row, as trackers do). In organ play mode the two rows can be two
  manuals instead (lower = second manual, upper = first). Octave shift per row
  is `F7/F8` and `F9/F10`. Velocity is fixed per layer and set in the
  ribbon, because a computer keyboard has none.
- **Key rollover.** The input layer reads raw SDL key down/up events with
  scancodes, ignores auto-repeat, and shows the host keyboard's ghosting
  limit (tested once at first run with a chord test), so missing chords are
  explained instead of looking like engine faults.
- **On-screen controls.** Drawbars, switches and knobs in the Play view
  respond to mouse drag, wheel and typed values; every control also has a
  key binding.
- **Latency.** The input event is timestamped at the SDL event and applied
  at the start of the next audio period; at 128 frames this adds at most
  2.7 ms plus SDL's event latency. The ribbon shows the measured key-to-audio
  path once per session (loopback test with the AG03 when the cable is
  connected, otherwise the computed figure).
- **Later.** A MIDI adapter (raw MIDI or the ALSA sequencer, as the existing
  drivers use) plugs into the same timestamped event ring. The engine maps
  (`midi_map.c`, `ep_midi_map.c`, `ma_midi_map.c`) are already in the desk,
  so this is an input adapter only, and the tablet surface works once it
  exists. Not planned in W0–W12.

### 6.4 Play mode: the Workbench takes over live playing

The Workbench replaces three live programs. They stay in the repository
as references until the parity gates pass, then their usage text points to
the Workbench.

| Today | Workbench | What carries over |
| --- | --- | --- |
| `main.c` organ live driver | Play view, organ instrument | the organ console (registration, percussion, vibrato/chorus, drive, rotary mode/speed/balance/width/drive, swell, wear), two-manual protocol semantics of `-2` (notes of both manuals merge, console controls once), period/periods/gain/device options |
| `ep73.c` ep73 live driver | Play view, ep73 instrument | condition, tremolo rate/depth/mode, cabinet, sustain on a key |
| Patchlab live mode (`ma_patchlab.c`) | Patch view audition + Play view, Mamut instrument | bank browse, all patch fields, save through `ma_patch_save` |

**Play view layout** (2560 px wide, ~900 px usable height):

```
┌ instrument rack ──────────────────────────────────────────────────────────────────────────────────────┐
│ [1] organ  upper=row Q  lower=row Z   [2] ep73  off   [3] mamut "pad"  layer on row Q  vel 96            │
├ organ console ────────────────────────────────────┬ ep73 ───────────────┬ mamut quick ─────────────────────┤
│ ║║║║║║║║║  8 8 8 0 0 0 0 0 0   perc ON 2nd fast │ cond .62  trem .40   │ cutoff 980  res .14  bloom .07   │
│ ║║║║║║║║║                       vib C3   drive .35│ rate 5.2  cab .50    │ attack 260  release 2900  gain   │
│ rotary: chorale ◀▶ tremolo [Space]  bal .50 wid .80│                      │ (8 pinned fields, any of the 50) │
├ meters ──────────┬ budget ─────────────────────┬ take ────────────────────────────────────────────────────┤
│ L ▮▮▮▮▮▯ R ▮▮▮▮▯ │ 1.21 / 2.667 ms  45%  xr 0  │ ● REC  take 3  02:14  [keep] [discard]  → source "takes" │
└──────────────────┴─────────────────────────────┴─────────────────────────────────────────────────────────┘
```

- **Instrument rack.** Up to four live instruments at once, each with a key
  zone (row, octave range) and a layer/split rule; the budget meter refuses
  combinations that do not fit, and the organ (no voice limit, fixed cost) is
  measured once and shown as a fixed share.
- **Console keys.** Every console control has a key in the F6 layout, for
  example `Space` rotary speed switch (momentary or toggle), `Ctrl+1..9`
  drawbar select then `↑/↓`, `P` percussion on, `V` vibrato step, `D/Shift+D`
  drive. Bindings are a table in the session defaults file and can be edited.
- **Session link.** Play mode can run over a session: the frozen parts of the
  current arrangement play from the cache, and the operator plays one
  instrument live on top (a band-in-a-box way of rehearsing a part before
  writing it down).
- **Take recording (MIDI, not audio).** A take records the timestamped input
  events: notes, velocities and every console control change. A kept take is
  saved as a format-1 SMF in `takes/` (notes + console CCs on the engine's
  existing CC map, so `render_midi` can render it too) and registered as a
  session source. From there it goes through Import like any other file.
  This replaces `pc4ms-take-to-mid.py` for takes played in the Workbench.
- **Performance mode.** While Play is in front, workers stop, probes and
  renders are refused, the UI redraws at most 30 Hz, and contamination checks
  (browser, `code`) show as a warning in the ribbon.
- **Safety.** `Esc` is panic on every instrument; losing window focus
  releases all held keys (no stuck notes when the window loses focus);
  a non-finite or over-0 dBFS output mutes as in §6.1.

**Parity before retiring the old drivers.** The live map code is shared, so
parity is tested offline: a recorded take (events with frame stamps) is fed
both to the old driver's map path (through `render_midi` with the same
engine options) and to the Workbench live engine run in non-real-time mode.
The PCM FNVs must match for the organ, ep73 and Mamut. Only then do
`main.c`, `ep73.c` and Patchlab's live mode get a "superseded by" line in
their usage text. They are not deleted.

## 7. Analysis

All in `desk/wb_analysis.c`, used by probes, renders and the Listen view.

- RMS and peak per stem, per part, per section, per 400 ms window
- crest factor, L/R correlation and mono loss (as the current prep script)
- peak attribution: for the mix's top N peaks, the stem contributions at that
  sample
- polyphony timeline per part: active slots, held voices, steals (from
  diagnostics sampled every control period)
- tail check: level in the last 100 ms, all-asleep at endpoint
- spectrogram (STFT 2048/512, Hann) computed lazily for visible ranges only
- optional K-weighted loudness (LUFS-I/S) as a readout. It is never used to
  change gains automatically.

## 8. Expert interaction model

- **Keyboard first.** Every action has a key. `:` opens a command line with
  completion (`:probe climax`, `:gain vocal +1.5`, `:freeze arp keys`,
  `:render v3 "no wash, stronger kit"`, `:diff 12 14`). `Ctrl+P` gives fuzzy
  search over parts, fields, sections and runs.
- **Selection model**: parts, rules, notes, fields and runs are all
  selectable; the inspector always shows the current selection; multi-select
  edits apply relative (`+1.5 dB`) or absolute (`=0.30`) to all.
- **Undo/redo** as a journal of session edits (field, old, new). It survives
  restarts (`hurt.mws.journal`) and is cleared on explicit save-as-checkpoint.
- **Checkpoints**: named session snapshots (`v1`, `v2`), diffable like runs.
  The v1/v2 split from today becomes two checkpoints of one session instead of
  an `--edition` switch in C.
- **No modal dialogs** except destructive confirmations. Errors go to the
  console line and the relevant row turns red.
- **Mouse** works everywhere but is never required. The wheel zooms the time
  axis, `Shift+wheel` scrolls, and `Ctrl+wheel` scales fonts per view.
- Startup reopens the last session, layout preset, playhead and selection.

Default key map (excerpt): `Space` play/stop, `Enter` play from selection,
`L` loop selection, `,`/`.` nudge playhead by beat, `[`/`]` sections,
`Ctrl+R` render, `Ctrl+Shift+P` probe selection, `F` freeze, `U` unfreeze,
`S`/`M` solo/mute, `Tab` A/B, `Esc` panic and stop, `F1..F6` layouts, `?`
key help overlay.

## 9. Verification and tests

| Gate | What it proves |
| --- | --- |
| Format round trips | `.mws`, `.twreg`, `.epset` write → read is byte-identical; strict rejects (unknown, duplicate, missing, out of range, non-finite) with line numbers |
| Parser fuzzing | the three readers under `FUZZ_CC` (clang libFuzzer), plus ASan/UBSan |
| Import | synthetic fixtures (format 0 multi-channel, format 1 multi-track, mid-track program change, tempo map + time-signature changes, GM kit, stuck notes, running status) split under every strategy; counts and note lists pinned; refusals for SMPTE and format 2 |
| Import round trip | export of a derived MIDI and re-import with the track strategy give identical parts; `hurt-orch-20261107-v2.mid` imports into ten parts whose rebuilt MIDI is byte-identical |
| Tempo maps | tick→frame conversion equals `render_midi`'s on tempo-map fixtures |
| Live-play parity | takes fed through the old live map path and the Workbench live engine (non-real-time) give equal FNVs for organ, ep73 and Mamut |
| Orchestration parity | the Hurt v1 and v2 sessions build MIDI byte-identical to `hurt-orch-20261107.mid` (`3a16982b…`) and `-v2.mid` (`d4f7691c…`) |
| Render parity | the Hurt v2 session (with v2's stem routing replicated, kick in `pads`) renders mix FNV `96d601778ccf8305`, and v1 renders `bac34975b9f912ec`; the MA3 Hurt and Blues exhibits re-expressed as sessions match their logged stem FNVs |
| CLI/UI parity | `wb render` and the UI path give identical manifests apart from timestamps |
| Determinism | two final renders of one snapshot: identical FNVs; AUTO vs scalar engine identical (as the core already guarantees) |
| Real-time audit | the audio-thread objects pass a symbol audit (no malloc/free/printf/locks/file I/O), like `check-core-symbols` |
| Headless UI smoke | `SDL_VIDEO_DRIVER=dummy` opens a session, runs every view's draw once, saves, and exits clean under ASan |
| Core untouched | `make test` stays green, with all pinned PCM signatures unchanged |

Test fixtures are synthetic MIDI written by the test code. Copyrighted scores
appear only in operator-run parity checks that skip when the files are
absent.

## 10. Milestones

Each milestone ends with its gate green and a short evidence note in
`docs/`. Sizes are rough working days for one person.

| ID | Content | Gate | Size |
| --- | --- | --- | --- |
| W0 | Skeleton: `workbench/` tree, Makefile targets, SDL3 + Nuklear + font, empty frame with layout presets; session root `/data/tonewheel91-sessions/` with its local Git, `.gitignore` and README | window opens on both monitors, headless smoke, session root created and committed | 2 |
| W1 | `desk/` session model + `.mws` format + journal/undo; `wb check` | round trips, strict rejects, fuzz | 4 |
| W2 | Desk renderer: Mamut parts only, stems, lanes and curves, form, single-pass writer, manifest, ledger; `wb render` | render parity on Hurt v1/v2 and MA3 Hurt/Blues | 5 |
| W3 | Orchestration engine (rules, dedupe, overlay) + groove + derived MIDI; tempo maps; `wb build-midi` | MIDI byte parity v1/v2, tempo-map parity | 4 |
| W3b | Import: full SMF reading, all split strategies, controller event lanes, engine/patch suggestion table, linkage and re-import diff; `wb import` | import fixtures, round trip on v2 | 4 |
| W4 | UI: Sources, Import preview, Orchestrate, Roll (read + overlay edits), Form, parts column, inspector | Hurt v2 reproduced from scratch in the UI, and once more by importing the Songparts file | 8 |
| W5 | Patch editor (Mamut) with parent chain and A/B, Mixer, part config | today's patch helpers exported as `.mapatch` parents, flatten equals the C values bit for bit | 4 |
| W6 | Probe + analysis + peak driver + section tables; Render queue with inhibitor; listening copy | probe of the climax matches a final render's section numbers within float tolerance | 4 |
| W7 | Live audio: ALSA thread, SPSC commands, live audition of one part, computer keyboard + on-screen input, panic and safety | audio symbol audit, 30 min without xruns at 8 live slots on this host | 5 |
| W8 | Part cache + hybrid frozen/live playback + budget meter | v2 full-form playback with 1 live part, no xruns | 4 |
| W9 | Organ and ep73 engines: adapters, `.twreg`/`.epset`, panels, organ automation directives as lanes | `render_midi` organ/ep73 renders reproduced as sessions with identical FNV | 6 |
| W10 | Listen view, A/B, verdicts, Ledger diff/reproduce/export | reproduce button on v1/v2 gives identical FNVs | 4 |
| W11 | Play mode: instrument rack, organ console, ep73 and Mamut panels, key bindings, two-manual rows, performance mode | 30 min organ + one Mamut layer without xruns; no stuck notes on focus loss | 5 |
| W12 | Take recording to SMF, takes as sources; live-play parity against the old drivers; "superseded by" notes in their usage text | live-play parity gate | 3 |

Total about 62 days. W2 and W3 make the CLI useful on their own: after them,
a new orchestration is a session file and one command, with no C or Python
edits. The UI then builds on a desk that is already tested.

## 11. Risks

- **Live budget.** Full-arrangement live playback depends on MA3-PERF reaching
  the 32-slot target. Hybrid playback (§6.2) keeps the Workbench useful
  without it; the plan does not depend on that work.
- **8 GiB host.** A browser plus the Workbench plus a render can push the
  host into swap. Mitigations: cache on disk, mmap playback, final renders run
  alone, RSS in the status bar.
- **Organ and ep73 have no voice sleep.** "All asleep at the end" becomes a
  silence threshold for them. That is a new acceptance rule and is written
  into the engine table rather than left implicit.
- **Nuklear limits.** Table and text-entry ergonomics may need local patches.
  The custom canvases (roll, grid, waveforms, meters) bypass Nuklear from the
  start, so the risk stays in forms and lists.
- **Stem summation order** is part of bit-exactness. The UI makes it
  explicit, and parity tests pin it.
- **Computer keyboard as the only instrument input.** No velocity, limited
  rollover, and no continuous controllers in the hand. Live play is usable
  for auditioning, rehearsal and recording rough takes; expressive takes
  wait for the MIDI adapter. The plan keeps that adapter a drop-in (§6.3).
- **Session folder outside the repo.** It is not covered by the repo's
  remote. The local Git history protects against bad edits, not against disk
  loss; a backup target for `/data/tonewheel91-sessions/` is the operator's
  decision.
- **Scope creep toward a DAW.** No audio tracks, no recording, no plug-ins;
  new needs go through this document first.

## 12. Operator decisions (2026-11-07)

1. No hardware MIDI for now. Inputs are the computer keyboard, the mouse and
   on-screen controls; MIDI stays a later adapter (§6.3).
2. The Workbench takes over all live playing: organ, ep73 and Patchlab live
   mode (§6.4). The old drivers stay until parity passes.
3. Sessions live in a new dedicated folder that is kept:
   `/data/tonewheel91-sessions/`, outside the repository, with its own local
   Git history (§3.1).
4. Existing MIDI arrangements can be imported and split by track, channel,
   program, drum notes, pitch or by hand (§5.2).
