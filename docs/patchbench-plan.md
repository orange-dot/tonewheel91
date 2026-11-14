# Patchbench — plan for a sound-design desk over the Mamut Analog voice

Date: 2026-11-11. Status: PB0..PB9 implemented, uncommitted; see `docs/patchbench-progress.md`.

Patchbench is a native C23 application in this repository for creating and
shaping patches. It shows the voice as a signal flow, draws every DSP block
from the block's own output, applies edits to the sounding voice without
restarting audio, and feeds notes from a small step sequencer or the computer
keyboard. The work centres on the sources (the two VCOs with sync and
cross-mod, Raster, Mozaik, noise, grains) and on the stages that shape them.

It is a sibling of the Workbench (`docs/workbench-plan.md`), which stays the
desk for arrangements, renders and provenance. Patchbench needs no session
file and no parts. The two share files (`.mapatch`, `.wbpatch`), the UI
toolkit, the theme and the audio-thread rules.

## 0. Why a second desk

The Workbench Patch view (`mamut_panel` in `workbench/ui/wb_views.c`) was
built as one screen among thirteen. For sound design it falls short in ways
that follow from that position:

| Today | Consequence |
| --- | --- |
| The patch is a grid of 50 numeric cells grouped by key prefix. | Nothing on screen shows what a source or the filter is doing. |
| Every edit saves the file, bumps `audio_generation`, and the main loop calls `app_audio_reload`, which stops and restarts the ALSA thread. With a rack loaded the edit is not heard until the next reload. | A parameter cannot be swept while a note sounds. |
| `.mapatch` version 3 has 50 fields. The five Glas fields of `ma_patch` (`formant_mix`, `formant_vowel`, `formant_shift`, `formant_orbita`, `grain_mix`) are not in the catalog. | Glas is reachable only from C exhibits. |
| Glide, LFO, character, unison, detune, chorus and reverb are part state in the session, edited in the Mixer and the inspector. | The controls that decide how a patch sounds are on three screens. |
| Notes come from the two keyboard rows or from a whole arrangement. | There is no hands-free loop to tune a sound against. |
| The scope strip of workbench-plan §5.7 was not built. | No waveform or spectrum of the sounding voice; the Listen view draws finished runs only. |
| Macros shift the voice through the identity resolver (`ma-constants.md` §8) and the deltas in `ma_controls.c`. For example, sync runs at `sync + .30·baklja_sync_bias`, and Ruin is nonzero in every factory patch except Tepih. | Authored values and the values the DSP uses differ, and nothing shows the second set. |

## 1. Decisions and non-goals

| Question | Decision |
| --- | --- |
| Form | A separate binary, `build/patchbench`, sources in `workbench/pb/`, prefix `pb_`. It links the Mamut core and reuses `workbench/ui` (theme, fonts, Nuklear unit), `driver/live_io.c`, `driver/ma_patch_file.c` and `workbench/desk/wb_patch.c`. |
| Engine | Mamut Analog through `ma_voice_pool`, the same pool the Workbench and `render_midi` use. Organ and ep73 voicing pages are a later, separate milestone (PB10). |
| Toolkit | SDL3 + Nuklear + custom-drawn canvases, as in the Workbench. No new dependency. |
| Audio | ALSA, 48 kHz, 128-frame periods × 3, real time through RealtimeKit, as measured in `workbench-progress.md`. |
| Note sources | Step sequencer, computer keyboard, drone/latch. No MIDI hardware is required; a MIDI adapter is optional (PB11). |
| Truth of pictures | Every picture of a DSP block is computed by running the block's code, not by drawing an idealized formula. |
| Screens | One window, opened maximized on the largest display (2560×1080 on this host). The layout must still be complete at 1920×1080. The second-monitor analysis window was removed on 2026-11-11 (operator): the main window already carries the scope and spectrum. |

Non-goals:

- No new DSP and no change to rendered bits. The pinned PCM signatures and
  the core symbol audit stay as they are. The one proposed core addition is a
  white-box entry point for taps (§4.2), which is the operator's decision.
- No arrangement, song mode, automation lanes, audio recording or plug-in
  hosting. The sequencer is a note source: one pattern on each of its four
  channels (operator, 2026-11-12: each channel plays its own patch), no
  pattern chains.
- No automatic level matching, limiter or loudness normalization. Compare
  tools show numbers; the operator sets levels.
- No listening questionnaires and no writes into tracked docs.
- No generic parameter registry in the core. Display metadata lives in the
  hosted layer, keyed by the `.mapatch` field names.

The two DSP non-goals above bind PB0..PB9. The insert rack is a separate
core track with its own gates (`docs/ma-rack-plan.md`, 2026-11-11); its
Patchbench strip lands with RK1.

## 2. Screen

### 2.1 Frame

Two flow rows of six modules, a macro strip, and a bottom row for notes and
analysis. In the sources row a module is 320 px wide at 1920 px and 426 px
at 2560 px. In the shaping row the filter takes two of seven units (548 px
at 1920 px) and the other five take one (274 px).

```
┌ patch ▾  parent: factory:tepih  │ A│B  undo redo  save │ probe C3 v100 ◂▸ │ audio ● RT │ 8/8 voices  38%  0 xr │ ▮▮▮▮▯ -9.2 ┐
├─ SOURCES ────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ VCO1        S│ VCO2        S│ SYNC · CROSS-MOD│ RASTER      S│ MOZAIK      S│ NOISE · MIXER · PRESSURE│      32+300 px
│ [waveform]   │ [waveform]   │ [VCO2 over two  │ [8 families, │ [tile strip  │ [bus weights bar]        │
│ [harmonics]  │ [harmonics]  │  master cycles] │  current row]│  + waveform] │ [pressure curve]         │
│ saw pls tri  │ saw pls tri  │ sync  soft      │ mix pos warp │ mix slope    │ noise  pressure          │
│ sin  pw      │ sin pw level │ xmod            │ mip @ note   │ contr phason │                          │
│              │ int  fine    │ eff. sync .43   │              │ drift        │                          │
├─ SHAPING ────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ BCS          │ FILTER                         │ GLAS         │ AMP          │ OUTPUT      │ VOICE (bench)│      300 px
│ [phase       │ [magnitude response + env      │ [formants on │ [ADSR]       │ [body curve]│ limit unison │
│  portrait]   │  range]        [filter ADSR]   │  harmonics]  │ [velocity]   │ [L/R scope] │ detune char  │
│ amount       │ cutoff res drive env key       │ mix vowel    │ A D S R      │ body width  │ glide LFO    │
│ regime       │ A D S R                        │ shift orbita │              │ xfeed master│ chorus reverb│
│              │                                │ grain        │              │             │              │
├─ MACROS ─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Gravitacija ▬▬▬▭  Bloom ▬▭▭▭  Heat ▭▭▭▭  Ruin ▬▬▭▭  Swarm ▭▭▭▭ │ identity: mass strain headroom focus rupture …  │  90 px
├─ NOTES ────────────────────────────────┬─ SCOPE ──────────────────────┬─ SPECTRUM ───────────────────────┬─ LEVEL ──┤
│ ▶ ■ 96 bpm  16 steps  swing  root C3   │ stage: v1 v2 src prs lad      │ log 20 Hz–20 kHz, -96..0 dB      │ L R peak │  300 px
│ [16 × 25 note grid]                    │        gls vca out | live     │ harmonic grid, alias marks       │ rms knee │
│ [velocity lane] [gate lane]            │ [pitch-locked trace]          │ [peak hold]                      │ voices   │
└ status: file, dirty, last probe 14 ms, DSP 1.02 / 2.67 ms, hints for the control under the pointer ──────────────────┘
```

Height budget at 1080: bar 32, sources 300, shaping 300, macros 90, bottom
300, status 24, borders the rest.

One window only (operator, 2026-11-11). The scope and spectrum live in the
bottom row above; there is no second-monitor view.

### 2.2 Module anatomy

Every module has the same three bands:

- **Header.** Name; a bypass dot that is lit while the block is in its exact
  bypass (a mix or amount at literal zero, as the constants doc defines it);
  `S` solo for sources; `M` mute for shaping stages; a menu with init, copy,
  paste and reset-to-parent for the module's fields.
- **Picture.** About half the module's height. Computed by the probe (§4).
  Where a parameter has a geometric meaning, the picture is also the control:
  drag the pulse edge, the ADSR handles, the cutoff/resonance point, the
  Raster position, the vowel.
- **Controls.** Compact bar sliders with the value printed inside, and
  vertical faders for the waveform mixes.

Solo and mute never touch the authored patch. They produce a derived patch
(§3.3) that goes to the audio thread and to the probe, so the same thing is
heard and drawn.

### 2.3 Controls

One custom control family, drawn on the Nuklear canvas (`pb_widgets.c`).
Nuklear's stock property widget stays for plain integers and text.

| Gesture | Effect |
| --- | --- |
| Drag | Change over the control's scale; the pointer is captured, so the drag may leave the control. |
| Shift + drag, wheel | Fine: the catalog's `fine_step`. |
| Ctrl + drag | Coarse: the catalog's `coarse_step`. |
| Double-click or Enter | Type a value with unit (`1.2k`, `350ms`, `-7c`). |
| Backspace or right-click | Reset to the parent's value (chain) or the init value. |
| `↑ ↓` on the focused control | Fine step; with Shift, coarse step. |
| `←  →`, Tab | Move focus within the module; `[` `]` between modules. |
| `/` | Find a field by name, jump focus to it. |

Each control can show four marks besides its value:

- **parent** — the inherited value when the patch is a `.wbpatch` chain;
  inherited fields are dim, overridden ones bright;
- **B** — the other A/B slot's value when it differs;
- **effective** — the value the DSP uses at the probe note and velocity
  after macros, velocity and key tracking, read from the probe voice (§4.4);
- **modulation range** — where a route moves the value: filter envelope and
  key tracking on cutoff, BCS on resonance and cross-mod, LFO on pitch.

Scales and units come from a display table (`pb_fields.c`) keyed by catalog
name: linear, logarithmic (Hz, ms), bipolar with a centre detent (fine
cents, formant shift), stepped (interval). A test fails when a catalog field
has no entry or no place on screen.

A drag is one undo step. Undo and redo cover field edits, module paste, init,
snapshot recall and A/B copy.

### 2.4 Modules: sources

**VCO1 and VCO2.**

- Picture: two cycles of the VCO's mixed output at the probe note, taken at
  the 8x source rate, so it shows the smoothstep edge kernel, the
  edge-compensated triangle and the normalization of the waveform mix
  (`ma-constants.md` §3.1). The four components are drawn dim behind it,
  each at its weight.
- Under it: the first 32 harmonics in dB. Energy that falls between
  harmonics at the 1x output is drawn in the warning colour.
- Four vertical faders: saw, pulse, triangle, sine. Pulse width is dragged on
  the picture's pulse edge and also has a field.
- VCO2 adds level, interval and fine. Interval is a stepped control with
  octave buttons and a name (`-12 octave down`, `+7 fifth`). Fine shows the
  beat rate against VCO1 at the probe note in Hz.
- **Compass sheet** (toggle): the same waveform at C1..C6 side by side, with
  the guard fade state of each. It shows how the edge width and the guard
  change the shape across the keyboard.
- The guard state (the 6 ms fade when a source leaves its numerical domain)
  is shown in the header when it is not fully open.

**Sync and cross-mod.** The relation between the two VCOs has its own module
because it is where most oscillator character comes from.

- Picture: VCO2 over two VCO1 periods, VCO1's wraps as vertical marks, the
  reset landing points drawn from the same run. With cross-mod above zero a
  second trace shows VCO1's instantaneous frequency ratio
  (`1 + .25·xmod·vco2`, clamped to `.25..4`).
- Controls: sync, softness, cross-mod.
- Readout: effective sync,
  `clamp(sync + .30·baklja_sync_bias)·(1 − .75·softness)`, and effective
  cross-mod, `clamp(xmod + .24·baklja_ready)`, each with the share that
  comes from the macros named.

**Raster.**

- Picture: the eight table families stacked as rows, the current position as
  a highlighted line between two rows, and the morphed, warped frame drawn
  bold on top. Dragging vertically sets position.
- Readout: the mip selected at the probe note and its crossfade
  (`ma_voice_raster_mip`), as "harmonics kept: 16 → 8, 37 %".
- Controls: mix, position, warp.

**Mozaik.**

- Picture: the next 64 tiles as long and short blocks with their signs, from
  the Q32 accumulator at the current slope and phason, and the resulting
  Hann-pulse waveform under them.
- The slope slider carries the five detents (1/2, 3/5, golden, 5/8, 2/3)
  with their snap zones.
- Contrast shows the long-to-short length ratio; drift shows phason
  cycles per second.
- Controls: mix, slope, contrast, phason, drift.

**Noise, mixer and pressure.**

- Picture 1: one stacked bar of the normalized bus,
  `1 : vco2_level : noise : mozaik` over `max(weights, 1)`, then the Raster
  share, then the grain crossfade. It answers "how loud is each source after
  normalization", which the raw levels do not.
- Picture 2: the pressure transfer curve, with a dot at the current source
  peak from the probe.
- Controls: noise, pressure.

### 2.5 Modules: shaping

**BCS.**

- Picture: the phase portrait of the probe run, Hopf `(re, im)` and Duffing
  `(x, v)`, over the last 200 ms, plus the readout as a small time trace.
- The regime slider is a ruler with the four landmarks (stable, edge,
  subharmonic, recovery).
- The two destinations (cross-mod and resonance) get their modulation-range
  marks from this module.
- Readouts: `reset_count` and `maximum_state` of the sounding voice.

**Filter.**

- Picture: the ladder's magnitude response at the probe note. The ladder is
  nonlinear, so the response is measured, not computed: the probe drives the
  filter stage with a low-level and a nominal-level test signal and draws
  both curves. A band marks the cutoff's travel under the filter envelope at
  the probe velocity; a marker shows the effective cutoff
  (`filter_cutoff_effective_hz`).
- Dragging on the picture sets cutoff (x) and resonance (y).
- The filter ADSR is drawn beside it, with the same handles as the amp
  envelope.
- Controls: cutoff, resonance, drive, envelope amount, key tracking, ADSR.

**Glas.**

- Picture: the four formant peaks over the harmonic comb of the probe note
  (vertical lines at `n·f0`). With Orbita above zero the four moons are dots
  on the frequency axis; a captured moon sits on its harmonic line, filled.
- The vowel slider is labelled u, o, a, e, i.
- A small flow mark shows that grains enter before pressure and the formant
  bank sits after the ladder.
- Readouts from the sounding voice: captures, releases, grains stolen.
- Controls: formant mix, vowel, shift, Orbita, grain mix.

**Amp.**

- Picture: the ADSR as the RC curves of `ma-constants.md` §7 on a real time
  axis, with handles for attack, decay, sustain and release. Time handles
  move on a logarithmic scale. The curve is produced by the envelope code.
- Inset: the velocity-to-level and velocity-to-filter curves with the probe
  velocity marked.

**Output.**

- Picture: the body drive transfer curve with the safety knee, and a small
  left/right vector scope for width and crossfeed.
- Readouts: knee hits, sanitizations, pre and post peak.
- Controls: body drive, width, crossfeed, master.

**Voice (bench).** Controls that are session state, not patch fields:
limit, unison, detune, seed, character, glide, LFO depth and rate, chorus and
reverb. They are stored in the bench file (§3.2) and marked on screen as
"not saved in the patch". Limit, unison, detune and seed rebuild the pool
(§5.3); the rest are live.

**Macros.** Five wide sliders. Beside them the resolved identity frame as
small bars in three groups (Horizont, Peć, Baklja) plus mass, strain,
headroom, body focus, rupture and dispersion. Moving a macro updates the
effective marks on every control it reaches.

### 2.6 Notes

**Step sequencer.** Four channels, one pattern each.

- A channel has its own patch (a whole document: file, parent, A and B,
  undo history, insert rack), its own voice setup (the VOICE module,
  including the gain that sets its level in the mix) and its own pattern.
  One channel is on the desk: the modules, the probe pictures, the notes
  grid and the computer keyboard belong to it. The others keep playing
  their patch as authored.
- The channel strip beside the grid lists the four: the name selects the
  channel (`F1`..`F4`), `M` mutes its pattern from the next step on, `S`
  plays it alone (not saved), then its gain, and its voices over a peak
  meter. A star marks an unsaved patch.
- Tempo and swing are one for all channels, so every channel's steps fall
  on the same frames. Step count and root are per channel; patterns of
  different lengths run against each other.
- A grid of up to 32 steps by 25 semitones around the root. A click toggles
  a note; several notes in a column make a chord.
- Below it a velocity lane and a gate-length lane, one bar per step. A step
  can tie into the next, which makes glide audible.
- Transport: play/stop (`Space`) for all channels, tempo, step count, swing,
  root, octave.
- Built-in patterns, chosen for what they reveal:

| Pattern | Use |
| --- | --- |
| Pulse | one note, steady; for sweeping any control |
| Octaves, fifths | interval and sync tuning |
| Arpeggio, bass line | envelopes in a musical context |
| Chord stabs, held chord | polyphony, unison, stereo, effects |
| Velocity ladder | the same note at 20..127; velocity curves and the filter route |
| Pitch ladder | C1..C6; key tracking, band-limiting, guard behaviour |
| Tied line | glide |

**Keyboard.** The Workbench's two rows (`Z`..`M`, `Q`..`P`), octave on
`F7`/`F8`, velocity field, `Esc` panic. A latch toggle turns key presses into
held notes for long edits. An on-screen keyboard strip under the grid takes
the mouse and shows sounding notes.

**Probe note.** The note and velocity the pictures are computed for. It
follows the last played note, or is pinned. `◂ ▸` move it by octaves.

### 2.7 Analysis

- **Scope.** Two sources. *Stage* shows a probe tap (VCO1, VCO2, source bus,
  after pressure, after the ladder, after Glas, after the VCA, output): it is
  rendered offline at the probe note, so it is steady and repeatable. *Live*
  shows the audio thread's output, locked to the pitch of the newest note.
  Time base in cycles or milliseconds.
- **Spectrum.** 8192-point Hann FFT of the same source, log frequency axis,
  −96..0 dB, peak hold, a harmonic grid at `n·f0`. Bins that are not on the
  grid and exceed a threshold are drawn in the warning colour, which makes
  aliasing and inharmonic content visible while tuning sources.
- **Note view** (toggle in the scope): a whole probe note, attack to the end
  of release, as an amplitude envelope with the effective cutoff trajectory
  over it.
- **Level.** Peak and RMS per channel, the safety knee, active voices, DSP
  time per period against the period, xruns.

### 2.8 Patch management

- **Library browser** (`Ctrl+O`): factory bank, the bench library, and the
  patches of Workbench sessions under the session root. A selected patch can
  be auditioned before it is opened.
- **Chains.** `.wbpatch` parent chains through `wb_patch`: derive a child,
  reset a field or a module to the parent, flatten to a `.mapatch`.
- **A/B.** Two slots, `Tab` swaps while notes sound. Differing controls carry
  the B mark. The bar shows the RMS difference of the two slots at the probe
  note in dB, as a number.
- **Snapshots** (PB9): eight slots per patch, `Ctrl+1..8` store, `1..8`
  recall, and a morph slider between two slots (floats interpolate, the
  interval switches at the midpoint).
- **Save.** Explicit (`Ctrl+S`), atomic through `ma_patch_save`. An autosave
  copy protects unsaved work. Edits no longer write the file on every change.
- **Bounce** (PB9): the current pattern rendered offline to a WAV with a
  small manifest (patch SHA256, bench file, FNV-64 of the PCM).

## 3. Data

### 3.1 `.mapatch` version 4

Version 4 adds the five Glas fields, appended after `output.master` so
existing catalog indices do not move:

```
glas.formant_mix   0..1     glas.formant_vowel  0..1    glas.formant_shift  -1..1
glas.formant_orbita 0..1    glas.grain_mix      0..1
```

The reader accepts versions 1 to 3 and supplies the Glas bypass (mixes 0,
vowel .25, shift 0, Orbita 0), following the pattern of the Raster and BCS
extensions. The writer emits version 4. The shipped files in
`patches/mamutanalog/` stay valid as they are. `wb_patch` (64 slots) and the
`static_assert` on the required-field mask (55 < 64) have room.

This is a hosted change in `driver/ma_patch_file.c`; the core is untouched.
Workbench, `wb`, Patchlab and `render_midi` pick it up through the same
reader.

### 3.2 Bench file (`.pbench`)

Strict `key=value` text in sections, the same rules as `.mapatch` and
`.mws`: version line first, unknown or duplicate keys are errors with line
numbers, round-trip f32 digits, atomic save.

```
patchbench=2
selected=1

[probe]
note=48  velocity=100  pinned=0

[sequence]
bpm=9600  swing=0

[channel]
index=1  patch=library/dark-pad.wbpatch  mute=0  steps=16  root=48
[voice]
limit=8  unison=1  detune_cents=0  seed=...  character=.2
glide_s=0  lfo_depth=0  lfo_rate_hz=5
chorus_mix=0 chorus_depth=.5 chorus_rate_hz=.6  reverb_mix=0 reverb_size=.5 reverb_damping=.5
gain=16
[step]
index=1  notes=48,55  velocity=100  gate=50  tie=0  octave=0
...

[channel]
index=2  ...

[snapshot]
slot=1
filter.cutoff_hz=1250
...
```

(One key per line in the file; the block above is condensed and its values
are placeholders.) `[voice]` and `[step]` belong to the `[channel]` above
them; a channel the file leaves out keeps its defaults. Version 1 files (one
`patch=`, one `[voice]`, `[sequence]` with `steps` and `root`) still load,
into channel 1; saving writes version 2.

The default limit is 8, one AVX2 vector, which is the measured comfortable
load on this host.

### 3.3 Three patch values

| Value | Owner | Meaning |
| --- | --- | --- |
| Authored | the document, undo history, the file | what `Ctrl+S` writes |
| Audible | `pb_derive(authored, solo, mute, morph)` | what the audio thread and the probe receive |
| Effective | read back from the probe voice | what the DSP uses after identity, velocity and key tracking; display only |

Solo of a source zeroes the bus weights of the others. VCO2's waveform mix
is kept when VCO1 is soloed, because cross-mod reads VCO2's waveform and sync
reads its phase; VCO1 is removed by zeroing its four waveform levels, which
leaves sync and cross-mod as they were.

### 3.4 Where files live

- Factory patches: `patches/mamutanalog/` in the repository, read-only
  parents (`factory:tepih`).
- Bench library: `/data/tonewheel91-sessions/patchbench/` (`WB_HOME`), inside
  the session root's local Git. Each explicit save is a commit there, so a
  patch has a history without any work in Patchbench.
- Session patches: Patchbench can open and save a file under a session's
  `patches/` directly. The Workbench Patch view gets an "open in Patchbench"
  button that starts the binary on the part's patch.

## 4. The probe

### 4.1 What it is

A worker thread owns private `ma_synth` voices. After an edit (debounced
30 ms) it re-renders the tap set for the audible patch at the probe note and
velocity, and hands the result to the UI through a double buffer. The audio
thread is not involved. The lanes and the scalar voice produce identical
bits, so a scalar probe shows what the pool plays for one voice without
unison, detune or character.

One probe pass:

| Product | How |
| --- | --- |
| Source waveforms (VCO1, VCO2, Raster, Mozaik, grains) | a settled run of a solo patch, tapped at the source rate |
| Source bus, after pressure | the same run with all sources |
| After ladder, after Glas, after VCA, output | further taps of that run |
| Harmonics and alias marks | FFT of the taps |
| Filter response | filter stage driven by a test signal at two levels |
| Envelopes, note view | a whole note: note-on, hold, note-off, to idle |
| BCS portrait | the BCS state over the run |
| Effective values | the probe voice's state after settling (§4.4) |
| Compass sheet | six short runs at C1..C6, on demand |

Target: a full pass in under 50 ms on this host, so pictures follow a drag.
The pass is cancelled and restarted when a newer edit arrives. While the
audio thread's DSP time is above 80 % of the period the probe waits.

### 4.2 Taps

The scalar tick in `src/ma_voice.c` is a short composition of stages that
are already exported through `ma_voice_impl.h` for the white-box gates:

```
voice_front_end → ma_voice_render_source_block → ma_voice_decimate_source
  → ma_glas_prepare → ma_voice_filter → ma_glas_formant → voice_back_end
  → ma_voice_render_output
```

Two ways to read the values between the stages:

1. **A white-box entry in the core** (recommended):
   `ma_voice_tick_taps(ma_synth *, ma_voice_taps *)` in `ma_voice.c`,
   declared in `ma_internal.h` beside `ma_voice_tick_raw` and
   `ma_voice_tick_detuned`, which exist for the same kind of use. It calls
   the same static helpers as the tick and stores the eight source samples,
   the two 2x samples, the frame's `ma_render_controls`, and the values
   after the ladder, Glas, VCA and output. Nothing on the render path
   changes.
2. **A hosted mirror**: the composition repeated in `pb_probe.c` from the
   exported stage functions. No core edit, but `voice_front_end` is static,
   and the mirror can drift when the tick changes.

Either way one test is the guarantee: for every factory patch, a Glas patch,
several notes and velocities, the final tap equals `ma_synth_tick` bit for
bit over a whole note. With route 1 the usual gates apply as well: pinned
signatures unchanged, core symbol audit, and the `bench_ma3_perf` gate
(median ≤ +1 %, no case above +2 %).

### 4.3 Pictures that need no taps

Read directly from immutable data or pure functions: the Raster tables
(`ma_raster_table.h`) and mip choice, the Mozaik tile kinds from the Q32
accumulator, the identity frame, the velocity tables, the formant table.

### 4.4 Effective values

The front end resolves the per-frame `ma_render_controls` from the authored
controls and the identity frame (`ma_controls.c`): sync, cross-mod, Mozaik
mix, contrast and drift, cutoff, resonance, filter drive, envelope amount,
key tracking, width, crossfeed and body drive all move with the macros. The
taps carry that struct, and the effective marks read it. The rest comes from
the probe voice after it settles: `identity.*`,
`filter_cutoff_effective_hz`, the envelope levels and stages,
`glas.centre_hz[]` and `glas.moon[]`, `bcs.*`. Values of the sounding pool that the probe cannot know (active
slots, steals, the newest voice's BCS resets and Glas counters, output
diagnostics) come from the audio thread's snapshot (§5.4).

## 5. Audio engine

### 5.1 Threads

| Thread | Priority | Work |
| --- | --- | --- |
| UI | normal | events, layout, drawing on change and at most 60 Hz while audio or the sequencer runs |
| Audio | real time through RealtimeKit, else as granted | pool, sequencer, scope tap; no allocation, locks, I/O or logging; symbol-audited |
| Probe | nice 10 | §4 |

### 5.2 UI to audio

The Workbench ring carries one command per event. For patch edits that would
mean a field protocol on the audio thread. Patchbench uses **latest-wins
mailboxes** instead: the UI publishes a whole value, the audio thread adopts
the newest one at the start of a period.

The engine plays four channels. A channel is one `ma_voice_pool` with its
patch, voice setup and insert rack; the output is the sum of the channels
in order, each at its gain. Patch, voice setup, rack and pool have one
mailbox per channel; the sequence is one value for all.

| Mailbox | Value | Applied with |
| --- | --- | --- |
| Patch (per channel) | one `ma_patch` (audible) | `ma_voice_pool_set_patch`, which keeps ownership, envelopes, character and output histories |
| Voice setup (per channel) | glide, LFO, character, chorus, reverb, gain | the pool and effects setters |
| Rack (per channel) | one `ma_rack_spec` | `ma_rack_apply`, with its fades |
| Sequence | the four patterns and mutes | swapped at the next step boundary, all channels at once |
| Pool (per channel) | a pointer to a pool built by the UI thread | pointer swap at a period boundary; the old pool goes back through a return slot and is freed by the UI |

A drag at 60 Hz therefore costs the audio thread at most one `set_patch`
per period, and the core's 6 ms smoothers turn the steps into ramps. Notes
(with their channel), panic and transport stay events on an SPSC ring, as in
`wb_live_rt.c`.

With the voice-bank worker up (live path, step 7) and two or more channels
sounding, the channels are spread over the two threads, each to the one with
fewer voices so far; with one channel sounding its voice banks are, as
before. Channels share no state, so every split gives the same bits.

### 5.3 Pool changes without restarting audio

Limit, unison, detune and seed need `ma_voice_pool_init`, which must not run
on a playing pool. The UI thread builds and initializes a new pool with the
current patch and setup, then publishes its pointer. The audio thread swaps
at a period boundary; sounding notes end there. ALSA keeps running.

### 5.4 Audio to UI

- **Scope ring.** The output frames of each period go into an SPSC ring of
  16384 stereo frames. The newest note's number and the frame it started at
  ride along, for the pitch lock.
- **Snapshot** under a sequence counter, as `wb_live_publish` does: peaks,
  RMS, DSP time, xruns, and per channel the voices, the peak and the
  sequencer step. Pool and output diagnostics and the newest voice's
  envelope stage, BCS and Glas counters come from the channel on the desk.
- **Safety.** The Workbench rule: a non-finite sample or more than 10 ms
  above 0 dBFS mutes the output and panics the pool, shown in the bar.

### 5.5 Sequencer timing

The sequencer runs on the audio thread and counts frames. A step lasts
`rate·60 / (bpm·4)` frames, kept as an exact rational so no drift
accumulates; swing shifts odd steps by a fraction of a step. Note-offs come
from the gate length, ties suppress the note-off before the next note-on.
Each channel has its own run over its own pattern; they start together and
share tempo and swing, so their step boundaries are the same frames. A muted
channel keeps time and starts no notes.
The same code (`pb_seq`) runs offline for the bounce, which is the parity
check: the live engine in non-real time and the bounce produce the same PCM.
A bounce pass is as long as the longest pattern that sounds.

## 6. Source tree and build

```
workbench/pb/
  pb_app.c          main loop, frame, keys, files
  pb_modules.c      the twelve module panels and the macro strip
  pb_views.c        sequencer, scope, spectrum, level, library browser
  pb_widgets.[ch]   bar slider, fader, envelope and curve handles, focus, text entry
  pb_fields.[ch]    display table: module, unit, scale, init value per catalog field
  pb_derive.[ch]    authored → audible, solo and mute, morph, probe patches
  pb_probe.[ch]     worker, tap runs, responses
  pb_fft.[ch]       radix-2 FFT, window, dB
  pb_seq.[ch]       pattern model, frame math, strict text
  pb_state.[ch]     .pbench read/write, autosave
  pb_undo.[ch]      edit history
  pb_engine.[ch]    pool build, mailboxes (UI side), ALSA thread
  pb_rt.c           the audio thread's half
  test/pb_test.c  test/pb_rt_test.c  test/check_rt_symbols.sh  test/fixtures/
```

`pb_widgets` and the keyboard-row mapping (now `key_offset` in `wb_app.c`)
are written so the Workbench can link them later.

Make targets, in a `workbench/pb/patchbench.mk` included like
`workbench.mk`: `patchbench`, `test-patchbench`, `smoke-patchbench`,
`check-patchbench-symbols`. `test-patchbench` joins `make test` at PB2.

## 7. Verification

| Check | Where |
| --- | --- |
| `.mapatch` v4: v1..v3 load with Glas bypassed; v4 round trip; factory files decode to the compiled constants as today | `test/hosted.c` |
| Every catalog field has display metadata and a module | `pb_test` |
| `.pbench` strict parsing, round trip, fuzz entry | `pb_test`, fuzz target |
| Probe: final tap equals `ma_synth_tick` bit for bit | `pb_test` |
| Derive: solo and mute leave the authored patch untouched; solo of everything equals the authored patch | `pb_test` |
| Sequencer: step frames exact for a table of tempi and swing values; ties; gate lengths | `pb_test` |
| Engine: a scripted run (notes, patch mailbox traffic, a pool swap) in non-real time equals a reference render through `ma_voice_pool`; the same for four channels with their own patches, voice setups and patterns, summed, and with the channels spread over two parts | `pb_rt_test` |
| Bounce equals the live engine in non-real time | `pb_rt_test` |
| Audio thread references only engine functions and pure libm | `check-patchbench-symbols` |
| FFT against known sines; harmonic and alias classification on synthetic input | `pb_test` |
| Headless smoke: every module, both themes, 1920×1080 and 2560×1080, with scripted drags; the patch file must not change without input | `smoke-patchbench` |
| gcc, clang, ASan+UBSan+LSan | the existing build matrix |
| Core untouched: `make test` signatures, symbol audit; with tap route 1 also the perf gate | existing gates |

Operator checks, not automated: 30 minutes with the sequencer running and a
control swept, 0 xruns; each module looked at on both real displays.

## 8. Milestones

| | Content | Gate | Days |
| --- | --- | --- | ---: |
| PB0 | Spikes: tap route 1 against route 2 with the perf gate; `set_patch` cost per period with 8 and 32 held voices; one bar slider, one fader and one canvas on both real displays | a recorded choice for §4.2; `set_patch` fits the period with margin; controls readable at 1920 | 2 |
| PB1 | `.mapatch` v4, display table | hosted tests; `wb` and the Workbench read v4 | 1.5 |
| PB2 | Engine and shell: window, bar, pool, mailboxes, keyboard, latch, level, safety. Stock widgets for all fields in the module layout | engine parity test; symbol audit; pool swap without an ALSA restart; a sweep on a held note is free of steps beyond the core's smoothing | 3 |
| PB3 | Control kit, module layout, focus and keyboard, undo, A/B, chains, library, explicit save, autosave | every field editable and round-trips; undo test; chain flatten equals `wb_patch`; smoke | 4 |
| PB4 | Sequencer, built-in patterns, on-screen keyboard, `.pbench` | timing tests; live against offline parity | 3 |
| PB5 | Probe and the oscillator pictures: VCO1, VCO2, sync and cross-mod, bus weights, pressure, compass sheet, effective marks | probe identity test; pass under 50 ms; tap hashes pinned for the factory bank | 5 |
| PB6 | Raster, Mozaik, BCS and Glas pictures | pictures checked against the golden vectors of `ma-constants.md` §4.3 and the mip rule of §10A | 4 |
| PB7 | Filter response, envelopes with handles, output, macros with the identity frame | measured response agrees with the cutoff marker within a stated tolerance at low level | 3 |
| PB8 | Scope (stage and live), spectrum with alias marks, note view | FFT tests; pitch lock holds on the pitch ladder pattern | 3 |
| PB9 | Snapshots and morph, module copy/paste/init, bounce with manifest, "open in Patchbench" from the Workbench | bounce parity; morph endpoints equal the two snapshots bit for bit | 3 |
| PB10 | Organ and ep73 voicing pages (drawbars with the resulting harmonic picture, percussion, scanner, drive, rotary, wear; ep73 condition, tremolo, cabinet, drive) | the pages write `.twreg` / `.epset` that the desk loads unchanged | 3 |
| PB11 | Optional: MIDI input adapter; the module panels linked into the Workbench Patch view | Patchlab's live map reproduced (`ma_midi_map`) | 2–3 |

PB0 to PB9 is about 31 working days. The desk is usable for real patch work
after PB4 (all controls live, looped notes) and reaches its purpose at PB5.

## 9. Risks

| Risk | Handling |
| --- | --- |
| Custom controls in an immediate-mode toolkit: drag capture, focus and text entry need state Nuklear does not keep. | PB0 prototype; one active-control record in `pb_widgets`, nothing per widget. |
| The Workbench views added after W0 were never seen on a real display. | PB0 and every picture milestone end with a look on both displays. |
| `set_patch` touches every slot; its cost at 32 slots is unmeasured. | PB0 measurement; if it is too high, apply only on change and spread slots over periods. |
| The probe drifts from the tick. | The bit-identity test in `make test`. |
| The ladder's response depends on level. | Two measured curves, labelled with their levels. |
| A 2-core host: probe and UI compete with the audio thread. | One probe thread at nice 10, backing off above 80 % DSP time; redraw on change. |
| The sequencer grows into an arranger. | Four channels with one pattern each, 32 steps, no pattern chains, no lanes for parameters. Arrangement work belongs to the Workbench. |
| A running Patchbench disturbs timed benchmark runs, as browsers and editors do. | Noted in the status bar while audio is on; close it before `bench_ma3_perf`. |

## 10. Operator decisions

1. **Name and form.** "Patchbench", a separate binary beside the Workbench.
   The alternative is a mode inside the Workbench; it would inherit the
   session requirement and the frame's fixed columns.
2. **Tap route** (§4.2): the white-box entry in `ma_voice.c`, or the hosted
   mirror.
3. **`.mapatch` version 4** with the Glas fields now (PB1), since nothing
   else lets a file carry a Glas patch.
4. **Library location**: `/data/tonewheel91-sessions/patchbench/` under the
   session root's local Git.
5. **Organ and ep73** (PB10): in this plan, or left to the Workbench panels.
6. **Workbench Patch view**: keep the grid as it is until PB11, or retire it
   in favour of "open in Patchbench" at PB9.
7. **Patchlab** (`driver/ma_patchlab.c`): its terminal editor stays as the
   reference until PB11 reproduces its MIDI map, then gets the "superseded
   by" line.
