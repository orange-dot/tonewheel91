# Mamut Analog circuit-derived oscillators — plan (V0) and V1..V3

Date: 2026-11-12, V2 and V3 2026-11-14. Status: **V0 done** (the two SPICE
studies and this document), **V1 implemented** 2026-11-12 (the pitch law, its
two patch controls, file version 6, the Patchbench controls), **V2 and V3
implemented** 2026-11-14 (the skewed triangle and the saw's bow under the
same amount; the two triangle shapers as new levels, file version 7);
listening open for all three. **V4 implemented** 2026-11-14 (the two cores'
coupling on a shared supply, `vco.supply`, file version 8).

Operator direction: the instrument's character should come from analog
oscillators. The method is the one proposed for VCF-1: simulate a circuit in
SPICE, find the few numbers that explain what it does, and run those numbers
in the core. A full transistor-level model is not run at audio rate.

Scope: Mamut Analog and the Patchbench. The organ and ep73 are frozen. The
milestone prefix is `V`.

## 1. What the studies found

Evidence: `docs/ma-vco-b1-spice.md` (step 1), `docs/ma-vco2-spice.md` (step
2), tooling in `driver/spice/vco/`. The operator listened to both render sets
and accepted them.

- **B1 (triangle core, the lab's 24-transistor design).** In the audio band
  the pulse is an ideal pulse with the droop of its output capacitor. The
  triangle differs from the ideal one only by even harmonics, because its two
  ramps have different slopes: H2 is −32.8 dB at 225 Hz and −49.3 dB at
  1943 Hz. Two straight ramps explain the waveform to −48…−62 dB.
- **VCO-2 (exponential converter and reset-type saw core, 16 transistors,
  written for this study).** The saw is ideal to −42 dB; what is left is a
  30 mV bow on a 5 V ramp. The circuit's character is in its pitch:
  - After a technician's trim (C2 → C6 four octaves, A4 at 440 Hz) it tracks
    within ±2.5 cents from C1 to C6, is 6.8 cents sharp at C0 and goes flat
    at the top: −10.6 cents at C7, −30.5 at C8.
  - One law fits both simulated boards within 0.18 cents over ten octaves:
    `1/f = 1/(f_exp + extra) + dead`, with an extra capacitor current of
    about 0.21–0.28 Hz (the follower's base current) and a dead time of
    about 6.5–6.6 µs per cycle (the reset).
  - Two trimmed boards with ordinary part spread differ by less than
    0.5 cents from C2 to C6. Temperature is what separates them: with the
    +3300 ppm/K scale resistor about 1 cent/K at A4, 1.5 at C2 and 0.7 at C6.
- **Neither simulation has noise**, so neither says anything about jitter or
  drift within a note.
- **Mamut Analog's own waveforms lose top harmonics at high notes** (the saw's
  16th harmonic at C6 is 3 dB low). Recorded, not acted on.

## 2. Decisions

| ID | Decision |
| --- | --- |
| VD1 | **The core runs the law, not the circuit.** Pitch is computed per voice and frame from the VCO-2 law in closed form: a few multiplies and two divisions per oscillator. No solver, no table, no libm. |
| VD2 | **The trim is part of the model.** Each core is trimmed like the board (scale on C2 → C6, pitch on A4), solved to first order in the two small terms. Against the SPICE trim loop that is within 0.4 cents from C0 to C9 (`make check-ma-vco`). |
| VD3 | **Zero is an exact bypass.** `vco_analog = 0` takes the old pitch path and returns the old bits; every pinned signature stays. Every circuit term scales with the amount. |
| VD4 | **The dead time is a patch control** (operator, 2026-11-12): `vco_dead_time` is the share of the reset dead time left untrimmed. Zero, the default, is a board with its high-frequency trim set; one is a board without that trim. |
| VD5 | **The card owns the cores.** `ma_character` gains, per card: a temperature away from the trim temperature (±2 K), VCO2's own offset from it (±0.5 K), and per core the extra current (0.208–0.280 Hz) and the dead time (6.49–6.60 µs). The card's existing slow walk is read as ±0.5 K. The draws come after the older ones, so the older card values keep their bits. |
| VD6 | **The spreads of current and dead time are the two simulated boards; the temperatures are an assumption.** They are stated as such wherever they appear. A voice without a card is the nominal board (0.244 Hz, 6.546 µs) at its trim temperature. |
| VD7 | **VCO2 is its own core.** It follows the law at its own pitch, with its own draws, and is passed to the oscillator kernel as a correction of the interval ratio; the kernel itself is unchanged. |
| VD8 | **Everything else in the voice follows VCO1**, as it does for the card's detune today: Raster, Mozaik, Glas and BCS read the corrected base frequency. The filter's key tracking reads the note and is not affected. |
| VD9 | **The two controls are not smoothed.** An edit moves the pitch by cents and causes no phase step. |
| VD10 | **File version 6** carries `vco.analog` and `vco.dead_time`. A patch without them keeps writing version 4 or 5 byte for byte; versions below 6 refuse the keys. A Patchbench snapshot without the block loads with it at zero. |
| VD11 | **The shape follows the same amount** (operator, 2026-11-14): `vco.analog` also gives both triangles B1's rise and both saws VCO-2's bow. Zero is still the old oscillator bit for bit. |
| VD12 | **B1's rise from SPICE, not from a formula of the circuit.** `0.5 + exp(P(x))`, P a quartic in octaves from A4 fitted to fourteen CVs (27 Hz to 1.94 kHz, within 3 %), run on along its end slopes outside and capped at 0.6. Each VCO takes the rise at its own pitch. No per-card spread: the study has one board. |
| VD13 | **The triangle stays an integrator of a smoothed pulse.** The pulse's second edge moves to the rise; the integrator takes `c + h * pulse` so both ramps span two; the edge compensation scales with h. The kernel's sync paths move the half-turn edge to the rise. |
| VD14 | **The integrator is held on its trajectory.** A new rise moves the state by the difference of the two steady trajectories at its phase; once a frame, away from the edges, a skewed state is pulled 1/64 of the way back to its trajectory, because f32 rounding of unequal steps walks it (0.005 a second at 27.5 Hz without the pull). Equal ramps keep the old integrator. |
| VD15 | **The saw's bow is one constant**, 30.5 mV of 5.06 V at C4 (29.3 to 32.1 from C2 to C8), a mean-free parabola on the saw's term. It is -42 dB; it is there because the circuit has it, not because it is expected to be heard. |
| VD16 | **The shapers are new levels** (operator, 2026-11-14): `vco1.tri_sine`, `vco1.tri_saw`, `vco2.tri_sine`, `vco2.tri_saw`, beside the four older ones, which are unchanged (the Mamut sine stays). They read the triangle state, so the circuit amount reaches them through the rise; at amount zero they shape a symmetric triangle. |
| VD17 | **The sine is the pair's static curve**, trimmed at A4, as an odd and an even polynomial (six terms each, within 5e-5). The saw is the switched pair taken as linear (the stage is -61 dB from a line), `pulse * (x - 1) / 2`, its steps smoothed by the pulse's own edges. Both see the state over the integrator's gain, so their ramps run between +-1 at the trim note. |
| VD18 | **The swing the shapers see is what the saw saw in SPICE**: a cubic in octaves through zero at A4, fitted to the switched pair's step at nine pitches (27 Hz to 1.94 kHz); above that, measured on B1 with a range switch to 18.7 kHz (V2b, operator 2026-11-14), straight in hertz (4.14e-6 per Hz) from the join, held above 20 kHz. It is three quarters of the capacitor's swing growth (the swap comes just after the vertex). It scales with the amount. The range switch's own rise (it repeats the bass's skew) is not taken: VD12 stands. |
| VD19 | **File version 7** carries the four shaper levels. A patch without them keeps writing version 6 or below byte for byte; versions below 7 refuse the keys. A Patchbench snapshot may leave out the pitch block and the shaper block, each whole. |
| VD20 | **The cores couple through their supply** (V4, operator 2026-11-14): a new control `vco.supply`, 0 = a well-decoupled board (1 ohm, 10 uF), 1 = a soft rail (10 ohm, 100 nF), geometric in between; the coupling scales with `vco.analog`, so amount zero is still the old voice. |
| VD21 | **The coupling is the SPICE sawtooth**, not a sine: with `psi = q phi2 - p phi1`, `G(psi) = 2 L (wrap(psi) - 1/2)`, the jump at the resets (psi = 0, within 0.03 of every case), `L` a constant share of the lower core's frequency (C2 to C6). |
| VD22 | **Every interval near a simple ratio couples** (V4b, operator 2026-11-14): unison, the minor and major third, fourth, fifth, major sixth, minor seventh (7:4), octave, tenth (5:2), eleventh (8:3), twelfth, two octaves, above and below VCO1 (mirrored); each at its own measured strength, locking at the simple ratio. Ratios with p + q above 11 were not simulated and do not couple. |
| VD23 | **A phase nudge once a frame**, split evenly: VCO1 by -G / 2p, VCO2 by +G / 2q, in the kernel, where the lanes hold the phases. About 1e-5 turn a frame; the steps, the BLEPs and the triangles do not change. |
| VD24 | **File version 8** carries `vco.supply`; a patch without it keeps writing version 7 or below. Patchbench snapshots may leave it out on its own. |
| VD25 | **B1's ramps bow** (V2c, operator 2026-11-14): a parabola on each ramp by phase, added to what the triangle output and the shapers read, not to the integrator (the pull of VD14 keeps the state straight). Rise and fall heights from SPICE per pitch (a cubic in octaves, within 1.4 %), under `vco.analog`. |
| VD26 | **The sine shaper's input is AC-coupled** (V3b, operator 2026-11-14): a first-order high-pass at 1.40 Hz (the 10 uF coupling capacitor into 10 k and the trimmer) on what the sine's pair reads, its state per oscillator, tracked once a substep while the sine shaper sounds. The saw's pair stays DC-coupled. It changes the committed V3 sine: its third harmonic at low notes is now the board's. |
| VD27 | **B1's buffer bends the triangle** (V2d, operator 2026-11-14): `x + 0.0093 (x^2 - 1)` on the state over the integrator's gain, the vertices fixed, under `vco.analog`, on the triangle output and what both shapers read. It is the -47 dB second harmonic the old notes called "from the vertices": the capacitor's triangle has none beyond its skew. |

## 3. Steps

| Step | Content | Status |
| --- | --- | --- |
| V0 | The two SPICE studies, the tooling in `driver/spice/vco/`, this plan | done |
| V1 | The pitch law for both oscillators, card parameters, patch controls, file version 6, Patchbench controls, the referee test | implemented, listening open |
| V2 | Shape: the triangle's unequal ramps (B1) and the saw's bow (VCO-2). Changes the oscillator kernel and the lanes | implemented, listening open |
| V3 | Shapers that B1 lacks (triangle to sine, triangle to saw), from new netlists | implemented, listening open |
| V2b | The shape above B1's 1.94 kHz: B1 with a range switch, both shapers, to 18.7 kHz | measured and in the core 2026-11-14 |
| V4 | Two cores sharing a supply: locking near unison, the octave and the fifth | implemented 2026-11-14, listening open |

## 4. V1 as built

- **Law**: `ml_vco_law_hz` in `src/ma_vco_kernel.h`, applied at the end of
  front end part A (and in the source-evidence tick), after the pitch bend,
  the LFO and the card's detune: by `ma_voice_vco_frame` in `src/ma_osc.c`
  for one voice, by `ml_front_vco` in `src/ma_lanes.c` for eight (section
  5c). VCO2's correction goes into the interval ratio: through
  `ma_synth.vco2_bend` for one voice, straight into the lane inputs for
  eight.
- **Card**: `src/ma_pool.c` and `src/ma_bank.c`.
- **Controls**: `ma_patch.vco_analog`, `ma_patch.vco_dead_time`,
  `ma_synth_set_vco_analog`.
- **Files**: `driver/ma_patch_file.c` (version 6), `workbench/pb/pb_state.c`
  (older snapshots).
- **Patchbench**: the two controls sit in the module now titled
  "SYNC · XMOD · PITCH"; its picture gives up their height.

Gate `make check-ma-vco` (`test/ma_vco_analog.c`, 68 checks):

| Check | Result |
| --- | --- |
| Law against SPICE tracking, C0..C9 | board A within 0.38 cents, board B within 0.37 |
| Law against SPICE at 10 and 40 C | within 0.45 cents |
| The pitch a voice plays, VCO1, four notes, three settings | equals the law within 0.05 cents |
| The pitch VCO2 plays, an octave below and a twelfth above | equals the law at its own pitch within 0.05 cents |
| Amount zero | 48000 frames, 0 differ |
| Pool block render against the frame-by-frame tick, law on, 10 voices | 4096 frames, 0 differ |
| Card parameters | every assigned slot inside the stated spreads |

All three builds pass `make test` in full with 0 warnings (GCC with LTO,
`make test-clang`, `make sanitize`; logs in `build/vco-v1-gates/`). Every
pinned signature is unchanged: engine parity `cfa5a2329dacba61`, bounce
parity `f2b0813223e7cf07`. `test/hosted.c` covers file version 6 (157 checks)
and `workbench/pb/test/pb_test.c` the older snapshots (795 checks).

With the nominal board, C8 plays −29.2 cents untrimmed, −14.6 at half amount
and +4.1 with the dead time trimmed out.

## 5. V2 and V3 as built

Evidence: `docs/ma-vco-shapes-spice.md`.

- **Front end**: `ml_vco_frame_of` in `src/ma_vco_kernel.h` (rise and swell
  per VCO, the bow) beside the pitch law; constants in
  `src/ma_voice_impl.h`. The frame carries departures (`vco_skew`,
  `vco_swell`, `vco_bow` in the synth for one voice), zero without the law,
  so a zeroed synth or frame is the old oscillator.
- **Kernel** (`src/ma_osc_kernel.h`): `ml_tri` (rise, h, c, swing, the
  shapers' view), the skewed integrator input and gain, the two-ramp seed,
  the move and pull in `ml_seed_triangle`, `ml_circuit_mix` (bow and
  shapers), the rise in the sync paths. The state keeps the rise it last ran
  on (`ma_oscillator.rise`, the lane bank's `rise`).
- **Controls**: `ma_vco_controls.tri_sine_level`, `tri_saw_level`, smoothed
  like the other levels.
- **Files**: `driver/ma_patch_file.c` (version 7), `workbench/pb/pb_state.c`
  (older snapshots).
- **Patchbench**: each VCO module has six level knobs ("tri sine", "tri
  saw"); the component traces cover them.

Gate `make check-ma-vco-shape` (`test/ma_vco_shape.c`, 68 checks):

| Check | Result |
| --- | --- |
| The front end's rise against SPICE, F1..A#6 | within 5.5 % of the asymmetry (3 % at the simulated pitches) |
| The front end's swell against the switched pair in SPICE, F1..G9 | within 0.00042 |
| Above 2 kHz: saw step at 3.8 and 6.2 kHz; sine H3 at 3.8 kHz | within 15 %; within 5 dB (6.2 kHz printed: 8 dB more H3, the core's edge zone) |
| Skewed triangle: H2 against two straight ramps, 27.5 Hz..1.76 kHz | within 0.10 dB; peaks within 1e-4 of +-1, no mean |
| A minute at 27.5 Hz with B1's skew there | peaks within 1e-5 of +-1 |
| 400 notes with new rises and glides | centred within 1e-4 |
| The bow | its parabola within 3.1e-8 |
| Shaper sine against SPICE | THD within 1.5 dB 27 Hz..1.9 kHz; H2 within 1.5 dB to 440 Hz, 4 dB above (V2d); H3 within 1.5 dB to 1.2 kHz (V2c, V3b) |
| Triangle output H2 against SPICE TBUF, 225 Hz..1.9 kHz | within 0.2 dB (V2d) |
| Shaper saw: quarter points, step at the vertex against SPICE | within 0.006 of -+1/2 (checked to 0.01); step within 3e-4 (1.9 kHz: 10 %) |
| Pool block render against the frame-by-frame tick, every shape on, sync, new notes | 8192 frames, 0 differ |

Side finding, not acted on: the old (symmetric) triangle walks off centre
when the pitch jumps mid-ramp, because its edge compensation changes with
the step: 0.06 after 400 random pitches on one voice (no pool, where every
note re-seeds). The pull of VD14 removes it for skewed triangles; doing the
same for the old one would change the pinned signatures.

## 5b. V4 as built

Evidence: `docs/ma-vco4-spice.md`.

- **Front end**: in `ml_vco_frame_of` (`src/ma_vco_kernel.h`): the row for
  VCO2's interval (`VCO_COUPLE` in `src/ma_voice_impl.h`, indexed by the
  interval), the supply between the two boards, the amount, the lower
  core's frequency. The frame carries `vco_couple_hz` (0 = none), p, q and
  the jump.
- **Kernel**: `ml_couple` in `src/ma_osc_kernel.h`, first in
  `ml_source_block`: psi from the two Q48 phases, the sawtooth, the two
  nudges.
- **Files**: `driver/ma_patch_file.c` (version 8), `workbench/pb/pb_state.c`.
- **Patchbench**: `supply` under `dead time` in "SYNC · XMOD · PITCH".

Gate `make check-ma-vco-couple` (`test/ma_vco_couple.c`, 31 checks):

| Check | Result |
| --- | --- |
| A voice's sawtooth against SPICE, unison, octave, fifth (A4), fourth, major third, twelfth, two octaves (A3) | lock within 3 %, jump within a SPICE bin or two of the resets |
| Soft rail, unison, 1 cent apart | locked (mean beat 0.0000 Hz), held at the resets |
| Soft rail, unison, 3 cents apart | beats at 0.66 Hz against 0.76 free |
| The front end's lock range against the table, 21 intervals, 3 supplies, 3 notes | within 1 % (the law's own move at C0), zero where the table has nothing |
| The supply without the law | no coupling |
| Pool block render against the frame-by-frame tick, coupling on | 8192 frames, 0 differ |

## 5c. The frame in the lanes

Evidence: `docs/ma-vco-lanes-20261115.md` (2026-11-15).

The once-a-frame part of V1 to V4 (both pitch laws, the shapes at each
core's pitch, the coupling) is one kernel, `src/ma_vco_kernel.h`, run at
width one by the one-voice tick and at width eight by the pool's lanes
(`ml_front_vco`). Until then every pool voice ran it in scalar code, which
added 0.53 to 0.62 M cycles to an 8-voice block; it now adds 0.20 to 0.29 M.
The PCM is the same bit for bit, and `check-ma-front-lanes` compares the
lane frame with the one-voice frame field by field.

## 5d. Kolo, the factory patch

2026-11-15. Until now no factory patch set `vco.analog`, so the bank, the
exhibits and the default bench never ran the circuit-derived oscillators.
Kolo (`ma_patch_kolo`, `patches/mamutanalog/kolo.mapatch`, file version 8)
is the bank's seventh patch and has nothing else: no Raster, no BCS, no
Glas, every macro at zero.

| Group | Value |
| --- | --- |
| VCO1 | saw `.55`, pulse `.15` at width `.46`, triangle-to-sine `.30` |
| VCO2 | saw `.30`, triangle `.25`, triangle-to-saw `.45`, level `.70`, interval `0`, fine `+1 cent` |
| Circuit | `vco.analog` `1`, `vco.dead_time` `.35`, `vco.supply` `.85` |
| Oscillator modulation | sync `0`, cross-mod `0` |
| Noise, Mozaik | `.01`; mix `.06`, golden slope, contrast `tau`, drift `.03` |
| Mixer/filter/body | pressure `.18`, cutoff `1400 Hz`, resonance `.22`, filter drive `.18`, envelope amount `.45`, keytrack `.50`, body drive `.12` |
| Amp ADSR | attack `40 ms`, decay `900 ms`, sustain `.75`, release `900 ms` |
| Filter ADSR | attack `60 ms`, decay `700 ms`, sustain `0`, release `800 ms` |
| Stereo/output | width `.60`, crossfeed `.05`, master `.58` |

Why these values:

- **The filter envelope ends at zero.** It bends VCO1 up by an eighth of an
  octave per unit of envelope (`ml_prepare_source`). With a sustain of .35
  and an amount of .45 that held the cores 34 cents apart at A3 for as long
  as the note sounded, far outside any lock range; the other factory
  patches, with sustains of .18 to .50 and up to 7 cents of fine tuning,
  never lock either. Here the bend lasts as long as the envelope: a note
  beats through its attack and decay and then comes to rest on the two
  cores' own difference.
- **One cent of fine tuning on a soft supply.** At `vco.supply` `.85` the
  unison lock range is about 1.3 cents. With the card's spread on top of
  the one cent, about half the cards are inside it.
- **Both families of shape sound**: the saw core's saw on both VCOs, the
  triangle core through its sine shaper on VCO1 and through its saw shaper
  and directly on VCO2.
- **The master** puts a six-note chord at the level of Prizma and Granica
  (RMS -34.4 dBFS against -34.2 and -34.5 in the same test).

Measured on the pool's first 32 cards, velocity 100, one voice at a time
(the phase of VCO2 against VCO1; a one-off probe, not kept):

| Note | Turns in the first second | Cards that lock | From (mean) | The others creep at |
| --- | ---: | ---: | ---: | --- |
| C2 | 0.53 | 0 | | 0.006 to 0.096 Hz |
| A3 | 1.84 | 16 | 3.4 s | 0.001 to 0.291 Hz |
| C5 | 4.13 | 22 | 1.6 s | 0.234 to 0.616 Hz |
| C6 | 8.52 | 22 | 1.5 s | 0.301 to 1.206 Hz |

A card counts as locked when the phase difference stops moving and stays
within a hundredth of a turn from the eighth to the sixteenth second. A
creeping pair does not beat evenly: the sawtooth of VD21 lets the
difference crawl away from the lock point, faster and faster, and then
slip.

Listening set, dry, every file 12 dB above the patch's own level:
`build/vco/kolo/listen/` (not in the repository).

- `kolo-chords.wav`: four six-note chords, nine seconds each.
- `kolo-chords-law-off.wav`: the same with `vco.analog` at zero.
- `kolo-chords-good-supply.wav`: the same with `vco.supply` at zero.
- `kolo-held-a2-a3-a4.wav`, `kolo-held-a2-a3-a4-law-off.wav`: three single
  notes, twelve seconds each.
- `kolo-line.wav`, `kolo-line-top-trimmed.wav`: a line up four octaves,
  four held notes from C7, then C4 against C7; the second with
  `vco.dead_time` at zero.

The values are a first setting, chosen from these measurements and not yet
by ear.

## 6. Open

- Operator: listening, in the Patchbench. Suggested: a patch with two
  oscillators in unison and `analog` at 1, held chords low and high, then
  `dead time` from 0 to 1 on a high line.
- The Patchbench module with five controls has been looked at only in the
  smoke's off-screen screenshots (1920 and 2560 wide, 1080 high), not on a
  display.
- No per-voice temperature dynamics beyond the card's existing walk; no
  jitter. Both would be assumptions, not circuit results.
- Cost, hosted (section 5c): the law at full amount adds 0.20 to 0.29 M
  cycles to a 128-frame block of 8 lane voices. The shapers' own levels and
  live play with the law on are not measured.
- V2/V3 listening: `build/vco/shapers/listen/` (docs/ma-vco-shapes-spice.md
  section 5), then the Patchbench. The core's sine has less third harmonic
  than the board's (straight ramps), B1's ramp bow is not modelled.
- Nominal parts only for both shapers; no spread across cards.
- V4 listening: `build/vco/vco4/listen/` (SPICE pair against the core's,
  and the core's pair with and without coupling), then `supply` in the
  Patchbench. Only the rails couple in the study; a board that takes its
  pitch trim from the same rail would couple more. How the pull is shared
  between the cores is assumed even.
