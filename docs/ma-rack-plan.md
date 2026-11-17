# Mamut Analog insert rack — plan (RK0)

Date: 2026-11-11. Status: **RK0 done** (this document and the backlog
amendments it names); RK2 re-pin and the RK3/RK3b scope confirmed by the
operator the same day. **RK1 implemented** the same day, gates 1–7 pass,
listening (gate 8) open: `docs/ma-rack-rk1.md`. **RK1b** (8-band parametric
EQ, EQ editor, AVX2 wavefront) the same day: `docs/ma-rack-rk1b.md`. **RK2
implemented** 2026-11-16 (chorus and reverb as blocks, delay, saturator,
compressor, `.mapatch` version 9; decisions RKD27..RKD35 in section 3):
`docs/ma-rack-rk2.md`; listening open. Operator request: build reusable blocks,
starting with an EQ, that can be inserted anywhere in the chain from the
Patchbench, so that a sound is built from structure and not only from
parameter edits.

The milestone prefix is `RK` because `R0..R4` already name the refactor steps
in `docs/refactor-20261109.md`.

Scope: Mamut Analog and the Patchbench only. The organ and ep73 are frozen;
nothing here touches their translation units, and nothing they decided binds
this line.

## 1. Decisions

| ID | Decision |
| --- | --- |
| RKD1 | **A closed set of block kinds, not a plug-in API.** One `enum` of kinds compiled into the core. No registry, dynamic loading, string lookup or core I/O. Adding a kind is a core change with its own evidence, like any other DSP stage. |
| RKD2 | **One fixed-length serial chain on the bus**: 8 slots, each empty or holding one block. Parallel routing, sends and per-voice inserts are not part of RK1..RK3 (per-voice is RK4). |
| RKD3 | **Exact bypass for every block.** A disabled block, or one at `mix = 0`, returns its input bits and runs no DSP (the ep73 cabinet rule: `a + 0*(wet-a)` is not exact); when it comes back, its filters start from zero history. A block whose parameters are neutral (an EQ with every gain at 0 dB and the high-pass off) takes the same exact path. An empty rack reproduces every current Mamut signature bit for bit. |
| RKD4 | **The rack's state is caller-owned and separate from the pool.** `ma_voice_pool` keeps its 1 MiB budget, and a pool rebuilt by the Patchbench (`patchbench-plan.md` §5.3) does not cut the rack's tails. The pool reads it through a pointer; `NULL` is the empty rack. |
| RKD5 | **Description and state are two types.** `ma_rack_spec` is a small fixed-size value (kind, stable slot id, enabled, mix and the kind's parameters per slot) that travels by latest-wins mailbox. `ma_rack` is the DSP state. `ma_rack_apply(rack, &spec)` runs between frames on the render thread. |
| RKD6 | **State follows the slot id.** On apply, a slot with the same id and kind keeps its state. A new or changed slot starts from zero. A removed block fades its mix to zero and an inserted block fades in from bypass, over 10 ms [decision], so add, remove and reorder do not click. |
| RKD7 | **Buffered blocks never clear on the render thread.** A delay line counts frames written since activation and reads zero beyond that count. Activation is O(1) whatever the buffer size. |
| RKD8 | **Large buffers come from per-kind instance caps**, not from every slot: at most 1 chorus, 1 reverb and 2 delays (RK2). Small blocks (EQ, utility, saturator, compressor) keep their state inside the slot. |
| RKD9 | **Position.** RK1: `body -> DC -> rack -> master -> safety -> chorus -> reverb`. The rack sees the instrument's level before the master fader, so moving master does not move how hard a saturator or compressor works. RK2 moves chorus and reverb into the rack as two blocks: `body -> DC -> rack -> master -> safety`. That is the one point where wet signatures are re-pinned (confirmed by the operator 2026-11-11). Dry renders stay bit-identical through both steps. In place since 2026-11-16. |
| RKD10 | **Per-frame tick inside the pool tick**, as `ma_effects` runs today. Block processing waits until `bench_ma3_perf` shows it is needed. |
| RKD11 | **Parameter changes are smoothed per frame** with the core's 6 ms smoother convention. Filter coefficients are recomputed only while a smoother is moving. Coefficient math is freestanding: `tan` from the repo sine kernel (as in the rotary's TPT crossover) and a deterministic `exp2` approximation for dB to linear. No `libm`. |
| RKD12 | **Storage: `.mapatch` version 5** carries the rack as optional `rack.<n>.*` keys. The kind is written as a name (`rack.1.kind=eq`), never as a number. A separate hosted parser owns these keys, so the 64-bit field mask and `wb_patch` are not involved. Versions 1..4, or a v5 file without rack keys, give the empty rack. Every writer must round-trip the rack: a tool that does not edit it keeps it opaque and does not drop it. An empty rack keeps writing version 4, byte for byte. A `.wbpatch` child inherits its parent's rack; `wbpatch=2` with `rack=1` replaces it with the child's own `rack.<n>.*` keys (RK1 decision, so a saved edit of a factory patch keeps its rack). |

## 2. Block kinds

Waves, in landing order:

| Wave | Kind | Contents |
| --- | --- | --- |
| RK1b | `eq` | up to 8 parametric bands (bell, low/high shelf, low/high cut, notch) and output gain, TPT SVF, linked L/R; frequencies clamp to .45 of the sample rate (RKD21..RKD24). RK1 shipped a fixed four-section form first; it was replaced before any commit. |
| RK1 | `utility` | gain (±24 dB), width (0..2, mid/side), pan (-1..1). |
| RK2 | `chorus`, `reverb` | the existing `ma_effects` code moved into blocks, bit-identical at equal position. Parameters: depth and rate; size and damping. |
| RK2 | `delay` | stereo or ping-pong, a low cut and a high cut inside the feedback loop, time in seconds or synced to the host tempo (13 note values). Capacity 2^19 frames per channel (2.7 s at 192 kHz) [decision]. |
| RK2 | `saturator` | the `tw_drive` kernel (its triode curve, its odd kernel, or a blend of the two; read-only reuse as the output body already does), input gain, tilt before and after, output gain, mix. |
| RK2 | `compressor` | RMS or peak detector, threshold, ratio, soft knee, attack, release, makeup gain, mix (parallel compression). |
| later | `filter` | multimode SVF (LP/BP/HP/notch) on the bus. |
| later | `phaser`, `flanger`, `tremolo` (with auto-pan), `crusher` | crusher: rate and bit reduction. |
| decision | `ringmod` | out of scope through MA5 as written; needs an operator decision. |
| decision | `rotary` | MA may call `tw_rotary` through `tonewheel.h` without editing it; the input is mono. |

## 3. Milestones

**RK0 — plan and decisions.** This document; `analog-backlog.md` MAD8,
MAD4, the out-of-scope list and the status register amended; journal entry.

**RK1 — rack host, `eq`, `utility`, Patchbench strip.**

- Core: `ma_rack_spec`, `ma_rack`, `ma_rack_apply`, the RKD9 hook inside the
  output stage, `eq` and `utility`.
- Hosted: the `.mapatch` v5 reader and writer, and `render_midi --rack` from
  a patch file, which gives the deterministic offline twin.
- Patchbench: a rack strip (add, remove, reorder, bypass dot, `M` mute
  through the derived patch value of §3.3), a Rack mailbox, undo, and block
  pictures computed by the block's own code. The probe worker runs a
  private instance on an impulse for linear blocks, and at two levels for
  nonlinear blocks. A post-rack spectrum comes from the audio thread's
  snapshot.
- Measure the level the rack receives for each factory patch. This is the
  reference for the nonlinear blocks of RK2; no number is assumed now.

**RK1b — parametric EQ and its editor** (operator, 2026-11-11: "eq mora
malo napredniji ... da mogu da definisem opsege, broj i sirinu"). Replaces
RK1's fixed four-section EQ before anything was committed.

| ID | Decision |
| --- | --- |
| RKD21 | **Up to 8 bands per EQ block, in series, plus an output gain.** The operator first asked for 42, then accepted 8 on the AVX2 argument (RKD25). For more bands, add a second EQ block to the rack. The layout is `bands` (0..8), `output_db` (±24) and five fields per band: `type`, `freq_hz` (10..22000), `gain_db` (±24), `q` (.1..40) and `on`. That makes 42 parameters per slot. |
| RKD22 | **Six shapes**: bell, low shelf, high shelf, low cut, high cut, notch, all TPT state-variable forms (Simper). Q is the width of the bell and the notch, the slope of the shelves and the resonance of the cuts (.7071 is Butterworth, 12 dB/oct; steeper cuts are two bands). |
| RKD23 | **Discrete changes crossfade per band.** Switching a band on or off, changing its shape, or changing the band count fades that band against its own input over the 6 ms ramp. A shape change fades out, swaps the coefficients at zero and fades back in. Continuous values ramp as before. A gain shape at 0 dB, now and as a target, is skipped exactly. |
| RKD24 | **Canonical specs.** A band past the count holds its initial values, so a spec has one form and the file round trip is exact. The file writes only the bands in use: `rack.<n>.bands`, `rack.<n>.output_db`, `rack.<n>.band.<b>.type` (by name), `freq_hz`, `gain_db`, `q`, `on`. |
| RKD25 | **AVX2 as a measured follow-up.** The bands are a serial chain, so the lanes cannot hold eight bands of one sample. They can run as a wavefront, where lane k handles band k on sample t − k. That path needs the rack to run per block and a scalar fallback while anything ramps. It must be bit-identical to the scalar path and pass a `bench_ma3_perf` A/B. |
| RKD26 | **The editor's curves come from the block's own coefficients.** A private rack holds the block alone, is settled and ticked once, and the transfer of each band's computed SVF coefficients is evaluated at each pixel. A measured FFT of an impulse (8192 points, 5.9 Hz bins at 48 kHz) smeared narrow low bands. The DSP loop itself stays refereed against the analytic response in `test/ma_rack.c`. |

The editor replaces the two module rows while it is open (rack row: "open
EQ editor" or double-click; Esc closes). It has:

- a log-frequency and dB grid with ±6/12/24/36 dB ranges;
- the summed curve, the selected band's own curve and the live output
  spectrum;
- numbered nodes: drag sets frequency and gain, Shift+drag or the wheel sets
  width, right-click switches a band on or off, double-click on empty space
  adds a bell, Del removes the selected band;
- a panel with the band count, add and remove, enabled, mix, output, the
  selected band's shape, values and width in octaves, and a list of every
  band.

**RK2 — `chorus`, `reverb`, `delay`, `saturator`, `compressor`.** Chorus and
reverb leave the pool. CC107/108, the `render_midi --chorus-mix/--reverb-mix`
switches and the `[voice]` keys of `.pbench` map onto those two blocks. Wet
signatures are re-pinned once (RKD9, operator-confirmed 2026-11-11).

Implemented 2026-11-16; evidence and measurements in `docs/ma-rack-rk2.md`.
The decisions below were taken while implementing. Each number marked
[decision] is a working value that has not been judged by ear.

| ID | Decision |
| --- | --- |
| RKD27 | **Chorus and reverb mix by their own law.** The slot's mix, with its 6 ms ramp and the structural fade, is passed to the kernel as its mix; there is no crossfade around the kernel. At a settled mix the two blocks give the bits of `ma_effects` on the same input (`test/ma_rack.c`, four rates). A new chorus or reverb block starts at mix .30 and .25, a delay at .30, every other kind at 1. |
| RKD28 | **A buffered block takes its input through a gate.** A chorus, reverb or delay that starts from an empty line (RKD3, RKD7) would begin its first delayed copy with a step, one delay time after it came on. The input written into the line ramps from 0 to 1 over the 10 ms fade time; the dry path is not scaled. A gate of 1 is the kernel as it was, bit for bit. A settled rack starts with the gate at 1. |
| RKD29 | **A session's chorus and reverb are blocks under two reserved ids.** `ma_rack_spec_set_chorus` and `ma_rack_spec_set_reverb` set the chain's chorus or reverb block when it has one (the patch's own block takes the session's values). Without one, and with a mix above zero, they add one after the last block, the chorus before a reverb added the same way. A send at zero leaves the chain alone. A full chain refuses the block: `render_midi` fails, a CC107/108 is counted as ignored, the Workbench refuses the part, the Patchbench says so. `render_midi` settles the rack before the first frame, so a CC at frame zero equals the switch. |
| RKD30 | **Delay.** The wet signal is the echoes alone; the slot's mix crossfades it against the input. Stereo: each channel feeds itself. Ping-pong: the mono sum goes into the left line and each line feeds the other, so every echo is `feedback` below the one before. The feedback passes a one-pole high cut and low cut; the first echo does not. `feedback` is at most .95. A time change is followed like a tape machine changing speed: the distance still to go decays with a 50 ms time constant [decision]. Sync takes 13 note values from 1/32 to 1/1, triplets and dotted included, against the host tempo (`ma_rack_set_tempo`, 20..400, 120 without a host). A time longer than the line clamps to the line. |
| RKD31 | **Saturator.** Per channel: tilt, input gain, the `tw_drive` stage, the input gain taken back out, tilt, output gain. The stage has unit small-signal gain, so the block has too. `shape` blends the stage's triode curve (0) with its odd kernel (1); both run only while the blend needs both. The input gain starts at +20 dB [decision], which puts the rack's measured chord peaks (about −20 dBFS) at the triode curve's knee. A tilt of t dB is first-order about 650 Hz [decision]: −t/2 far below, +t/2 far above, 0 dB at the pivot, and the opposite tilt after the stage is its exact inverse. Drive 0 with flat tilts and 0 dB out returns the input bits. |
| RKD32 | **Compressor.** Feed-forward with one gain for both channels. The detector reads the larger channel's magnitude (peak) or the mean square of both over 10 ms [decision] (RMS). The static curve has a soft knee (Giannoulis, Massberg and Reiss, JAES 2012); attack and release act on the gain change in dB. The threshold starts at −36 dB [decision], under the rack's measured chord level of about −31 dBFS RMS. Ratio 1 with 0 dB makeup returns the input bits, and so does any frame whose gain change and makeup are both 0 dB. No look-ahead and no latency. |
| RKD33 | **Storage: `.mapatch` version 9** and **`wbpatch=3`** for a rack that holds a kind beyond `eq` and `utility`. Version 9 carries every patch field. A rack of EQs and utilities keeps writing the version it wrote before. Values that are words are written as words: `sync=1/8d`, `detector=rms`, as an EQ band's type already was. This takes the version number RKD20 set aside for the modulation bus, which becomes version 10. |
| RKD34 | **The lines live in `ma_rack`, and `ma_rack_init` does not write them** (RKD7, RKD8). One rack is 9.06 MB with its chorus, reverb and two delay lines. A chain that has no delay never writes the delay lines, so that memory is never touched in a process that does not lock it. The pool no longer holds the 645 kB of chorus and reverb lines. The Patchbench locks its engine, four racks included, before audio starts. |
| RKD35 | **The pool does not own the tails.** `ma_voice_pool_reset` leaves the rack alone, as it did for the EQ; a host that wants silence at once calls `ma_rack_reset` too (the Patchbench's overload stop does). `ma_voice_pool_panic` never cut effect tails and still does not. |

The Workbench desk and its live path now give each Mamut pool a rack: the
patch's own rack with the part's chorus and reverb sends (`wb_part_rack`).
Before RK2 they carried a patch's rack through flattening and did not play
it.

**RK3 — modulation bus for rack parameters.** Operator decisions
2026-11-11: macros may drive effects through explicit routes (the MA3-2
rule is amended in `analog-backlog.md`), RK3b follows RK3 directly, and
the source set below is final for RK3.

| ID | Decision |
| --- | --- |
| RKD13 | **One modulation bus** (`ma_modbus`), caller-owned beside the rack. Sources are evaluated once per frame at frame start; every target reads the same values. |
| RKD14 | **Sources.** `lfo1`, `lfo2`: sine, triangle, saw, square, sample-and-hold, smooth random (seeded from the pool seed); rate free in Hz (.01..40) or as a beat division synced to the host tempo. `follow`: envelope follower on the **rack input**, attack and release, one frame late by construction (RKD13), so no loop is possible. `noteenv`: an ADSR on the bus, triggered by any note-on (`retrigger`) or only by the first note after all keys are released (`legato`); with negative depth it ducks, which gives pumping. `knob1..4`: CC116..119, a fixed map under MAD9. `gravitacija`, `bloom`, `heat`, `ruin`, `swarm`: the **effective** macro values after the mod wheel and pressure have been folded in, not the internal identity terms. `wheel`, `pressure`: the raw performance values. Velocity is not a bus source. |
| RKD15 | **Routes.** 16 slots, each `{source, target, depth}`. Depth is bipolar in the target's normalized range; routes into one target are summed, then the target maps its normalized value to its unit (log for frequency, dB for gain) and clamps it to its range. A target is a rack slot id and a parameter name, so it survives a reorder. Deleting a block deletes its routes; undo restores them. |
| RKD16 | **After the smoother.** The authored value goes through the 6 ms smoother and the modulation is added after it, so a 20 Hz LFO is not attenuated by the smoother. Square and sample-and-hold carry their own short slew (2 ms [decision]) against clicks. |
| RKD17 | **Sources modulate sources, in one direction.** LFO rate and depth, and the `noteenv` times, are targets for the control-like sources only (`knob*`, macros, `wheel`, `pressure`, `follow`, `noteenv`). An LFO cannot modulate an LFO, so there is no evaluation order to define and no cycle. |
| RKD18 | **Exact bypass.** No routes, or every depth at literal zero, reproduces RK2 bit for bit. The modulated sum is skipped, not added as zero (`-0.0f + 0.0f` is `+0.0f`). LFOs run from rack initialization whether routed or not, so adding a route mid-render is deterministic. |
| RKD19 | **Tempo.** The host supplies tempo: the Patchbench from its sequencer, `render_midi` from the SMF tempo map. RK3 syncs rate to beat divisions and resets synced phases at transport start; bar-locked phase is later work. |
| RKD20 | **Storage: `.mapatch` version 10** (written as version 6 here on 2026-11-11; versions 6 to 8 went to the circuit-derived oscillators and 9 to the RK2 kinds, RKD33), keys `mod.lfo1.*`, `mod.noteenv.*`, `mod.follow.*`, `mod.route.<n>.*`. The strict parser makes a version bump cheaper than reserving keys in v5. |

Patchbench, RK3: a sources panel with pictures computed by the sources' own
code (the LFO waveform over two cycles, the follower and `noteenv` traces
from the audio snapshot). Dragging a source onto a slider creates a route; a
modulated slider shows its range as a band and its live value.

**RK3b — the same bus into global voice parameters.** Directly after RK3.

- Targets: continuous voice controls that already have a smoother. These are
  VCO waveform levels and pulse widths, VCO2 level and fine tune, sync,
  sync softness, cross-mod, noise, Raster mix/position/warp, BCS
  amount/regime, Mozaik mix/slope/contrast/drift, mixer pressure, filter
  cutoff/resonance/drive/envelope amount/key tracking, the Glas mixes,
  vowel and shift, body drive and width. Integer controls (VCO2 interval)
  are not targets.
- Every awake voice receives the same per-frame offset: one scalar,
  broadcast across the lanes through the width layer of `ma_lane_ops.h`.
  It is added after the identity deltas and before the domain clamp, so it
  moves the value the DSP actually uses (`patchbench-plan.md` §4.4).
- The bus is evaluated once per frame by the pool before the voices tick.
  `follow` is therefore the previous frame's rack input (RKD13), as in RK3.
- The 16 route slots are shared between rack and voice targets.
- Gates: no routes reproduces every pinned signature, the scalar and lane
  paths stay bit-identical with routes active, the white-box lane gates
  hold, and `bench_ma3_perf` stays within the refactor gate with zero
  routes. The cost with eight voice routes is recorded.
- The probe's pictures show the authored value with the modulation range as
  a band. The live value comes from the audio snapshot, because the probe
  voice does not run the bus.

Per-note sources (an LFO or envelope per voice) are not part of RK3b. They
need per-voice state on the lanes and belong with RK4.

**RK4 — one per-voice insert slot** after the ladder, for kinds that have a
lane implementation in the single-source width layer (`ma_lane_ops.h`).
Gated by `bench_ma3_perf` at the 8-slot target.

## 4. RK1 gates

1. Empty rack, and a rack of bypassed blocks: every pinned Mamut signature,
   the pool engine twin and the MIDI twins unchanged. `make test` green on
   GCC, Clang and the sanitizer build.
2. EQ response, measured by running the block: the -3 dB point of the
   high-pass, shelf plateaus, and bell centre and gain within 0.1 dB of the
   analytic SVF response at 44.1, 48, 96 and 192 kHz.
3. Structural changes on a sustained tone (insert, remove, reorder, kind
   change): the largest frame-to-frame step stays within the tone's own
   largest step plus a stated margin. No NaN, no safety hits caused by the
   change.
4. Parameter sweeps at controller rate and at extreme values: finite output.
   Every safety hit is counted.
5. Two-run FNV-64 determinism of a rack render through `render_midi`.
6. Core symbol audit: no `libm`, no allocation.
7. `bench_ma3_perf`: empty rack within the refactor gate (median ≤ +1 %, no
   case above +2 %); the cost of eight active EQ blocks recorded.
8. Listening pairs (operator): one factory patch dry and with a stated EQ and
   utility setting. Files only, no questionnaire.

## 5. Risks

| Risk | Handling |
| --- | --- |
| A block that makes up for a weak patch. | The rack is a finish. Patch identity still comes from the voice; evidence renders state their rack. |
| A boost after the rack is clipped by the safety knee. | Safety diagnostics are shown in the Patchbench rack strip; the operator sets levels. |
| Delay memory in the live driver. | Static storage; the per-kind caps fix the total at build time. |
| Writers that drop rack keys. | RKD12 round-trip rule; a hosted test saves through every writer. |
| A move from block to block reads as a click. | RKD6 fades and RK1 gate 3. |
