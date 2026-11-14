# Mamut Analog circuit-derived oscillators — plan (V0) and V1..V3

Date: 2026-11-12, V2 and V3 2026-11-14. Status: **V0 done** (the two SPICE
studies and this document), **V1 implemented** 2026-11-12 (the pitch law, its
two patch controls, file version 6, the Patchbench controls), **V2 and V3
implemented** 2026-11-14 (the skewed triangle and the saw's bow under the
same amount; the two triangle shapers as new levels, file version 7);
listening open for all three. V4 is not started.

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

## 3. Steps

| Step | Content | Status |
| --- | --- | --- |
| V0 | The two SPICE studies, the tooling in `driver/spice/vco/`, this plan | done |
| V1 | The pitch law for both oscillators, card parameters, patch controls, file version 6, Patchbench controls, the referee test | implemented, listening open |
| V2 | Shape: the triangle's unequal ramps (B1) and the saw's bow (VCO-2). Changes the oscillator kernel and the lanes | implemented, listening open |
| V3 | Shapers that B1 lacks (triangle to sine, triangle to saw), from new netlists | implemented, listening open |
| V2b | The shape above B1's 1.94 kHz: B1 with a range switch, both shapers, to 18.7 kHz | measured and in the core 2026-11-14 |
| V4 | Two cores sharing a supply: locking near unison | not started; B1's supply rejection (±0.26 cents for ±5 %) says it needs a deliberately soft supply |

## 4. V1 as built

- **Law**: `vco_law_hz` in `src/ma_voice_impl.h`; applied in
  `ma_voice_front_a` (and the source-evidence tick) after the pitch bend, the
  LFO and the card's detune. VCO2's correction goes through
  `ma_synth.vco2_bend` into the interval ratio in `src/ma_osc.c` and
  `src/ma_lanes.c`.
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

- **Front end**: `analog_shape` in `src/ma_voice.c` (rise and swell per VCO,
  the bow) beside the pitch law; constants in `src/ma_voice_impl.h`. The
  synth carries departures (`vco_skew`, `vco_swell`, `vco_bow`), zero without
  the law, so a zeroed synth or frame is the old oscillator.
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

Gate `make check-ma-vco-shape` (`test/ma_vco_shape.c`, 52 checks):

| Check | Result |
| --- | --- |
| The front end's rise against SPICE, F1..A#6 | within 5.5 % of the asymmetry (3 % at the simulated pitches) |
| The front end's swell against the switched pair in SPICE, F1..G9 | within 0.00042 |
| Above 2 kHz: saw step at 3.8 and 6.2 kHz; sine H3 at 3.8 kHz | within 15 %; within 5 dB (6.2 kHz printed: 8 dB more H3, the core's edge zone) |
| Skewed triangle: H2 against two straight ramps, 27.5 Hz..1.76 kHz | within 0.10 dB; peaks within 1e-4 of +-1, no mean |
| A minute at 27.5 Hz with B1's skew there | peaks within 1e-5 of +-1 |
| 400 notes with new rises and glides | centred within 1e-4 |
| The bow | its parabola within 3.1e-8 |
| Shaper sine against SPICE, 27 Hz..1.2 kHz | THD and H2 within 2.5 dB |
| Shaper saw: quarter points, step at the vertex against SPICE | within 0.006 of -+1/2 (checked to 0.01); step within 3e-4 (1.9 kHz: 10 %) |
| Pool block render against the frame-by-frame tick, every shape on, sync, new notes | 8192 frames, 0 differ |

Side finding, not acted on: the old (symmetric) triangle walks off centre
when the pitch jumps mid-ramp, because its edge compensation changes with
the step: 0.06 after 400 random pitches on one voice (no pool, where every
note re-seeds). The pull of VD14 removes it for skewed triangles; doing the
same for the old one would change the pinned signatures.

## 6. Open

- Operator: listening, in the Patchbench. Suggested: a patch with two
  oscillators in unison and `analog` at 1, held chords low and high, then
  `dead time` from 0 to 1 on a high line.
- The Patchbench module with five controls has been looked at only in the
  smoke's off-screen screenshots (1920 and 2560 wide, 1080 high), not on a
  display.
- No per-voice temperature dynamics beyond the card's existing walk; no
  jitter. Both would be assumptions, not circuit results.
- The cost of the law on the live path is not measured; neither is the
  shape's (the pull and the move run once a frame, the shapers per substep
  only when their level is set).
- V2/V3 listening: `build/vco/shapers/listen/` (docs/ma-vco-shapes-spice.md
  section 5), then the Patchbench. The core's sine has less third harmonic
  than the board's (straight ramps), B1's ramp bow is not modelled.
- Nominal parts only for both shapers; no spread across cards.
