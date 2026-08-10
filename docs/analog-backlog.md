# tonewheel91 — analog synthesizer backlog

Date: 2026-08-03. Status: proposed backlog — plan only. No code exists and
nothing is scheduled; this document turns a line decision into an ordered,
gated plan so that starting is a decision, not a design session.

Currency: milestones are tw91-milestone-shaped. Effort figures are working
estimates, not commitments. Open choices are tagged [decision MDn] and
collected in the register at the end; each names the milestone that closes
it. Milestone IDs use the `MA` prefix and decisions the `MD` prefix, because
`piano-backlog.md` already spends `A0`–`A7` on the acoustic program and
`D1`–`D10` on its own register.

## Scope

One instrument: the five-voice polyphonic analog synthesizer of 1978 — two
oscillators per voice, a four-pole lowpass, two envelope generators, the
cross-modulation section, an LFO, and unison. Working name `mamutanalog`,
the house-named third line alongside the organ and `ep73`.

It is a component-modeled target in the same sense the other two lines are:
its voice chips have public datasheets carrying the actual transconductance
and exponential-converter laws, and the manufacturer's service documentation
carries the voice-card schematic. That is the same class of source base as
the organ's gear tables and the tine line's patents — not a genre pastiche.

## Repo cut

Same repo, sibling instrument family — not a fork, not a framework.

- New core translation units under `src/` with an `ma_` prefix; one new
  header `src/mamutanalog.h` that includes `tonewheel.h` for the shared
  kernels (`tw_sin_turns`, `tw_sat`, `tw_fabsf`, `tw_splitmix64`,
  `tw_fnv1a64`), the MIDI parser (`tw_midi_*`), and `tw_drive`. Neither the
  organ nor `ep73` includes `mamutanalog.h`.
- No shared "instrument framework": three top-level structs, three live
  binaries, no vtable, no dispatch layer. The `kernels.h` split out of
  `tonewheel.h` named in `piano-backlog.md` stays named and unscheduled;
  a third includer does not yet make it pay.
- The organ and `ep73` stay bit-stable by construction: no existing TU is
  edited, and their pinned signature suites in `test/test.c` are the
  enforced proof. Any `ma_` change that moves one of those signatures is a
  defect.
- Same core doctrine, unchanged: freestanding C23, no OS calls, no
  allocation, no libm, fixed state in caller-provided structs, f32, SoA
  banks, epsilon-snap smoothers, never `-ffast-math`, `-ffp-contract=off`.

## What transfers, what is new, what is not carried

Transfers as-is:

- MIDI byte parser; ALSA live-loop shape (read bytes -> parse -> state ->
  render period -> blocking write); xrun recovery and panic pattern.
- WAV writer, exhibit harness, `render_midi` twin, constants-pinning and
  evidence-doc workflow, FNV-64 two-run signature discipline, epsilon-snap
  smoother discipline, the bypass-is-bit-exact ("scanner-OFF") discipline.
- `tw_sat` as the saturating element inside the filter loop; `tw_drive`'s
  structure for the output stage at a new operating point, the move EP5
  already made once.
- The one-pole exponential coefficient kernel (`one_minus_exp` in
  `ep_voice.c`, `one_pole_coeff` in `drive.c`). An analog envelope generator
  **is** an RC one-pole, so this is not an approximation of the target — it
  is the target's own mechanism.

Genuinely new — three items, and they carry the risk:

- **Bandlimiting.** Both existing lines synthesize from sines through
  `tw_sin_turns`; neither has ever produced a hard edge. Sawtooth and
  variable-width pulse are the first, and the aliasing they bring is a
  problem this repo has not solved. [decision MD2]
- **A nonlinear resonant filter with feedback.** A naive cascade of four
  one-poles with a feedback tap carries cutoff and resonance error from the
  loop delay. Topology-preserving transform removes it, but with saturation
  inside the loop the step becomes implicit and wants a fixed iteration
  budget — which is the existing doctrine, not an exception to it.
  [decision MD3]
- **Voice allocation.** The first in this repo: the organ has 91 fixed
  wheels, `ep73` has 73 fixed resonators, and neither allocates. Here the
  allocation and stealing rule is audible and idiomatic, so it is model
  behavior pinned from the service documentation, not infrastructure.
  [decision MD4]

Not carried: always-running wheels, gear table, foldback, taper and robbing,
contact stagger and bounce, percussion, the scanner, the rotary, the struck
modal bank, dampers and the sustain pedal.

## Signal chain

    2 oscillators + noise per voice (bandlimited; sync and cross-mod)
      -> mixer (per-source level; overdrives into the filter)
      -> four-pole lowpass (resonant, saturating, per voice)
      -> VCA (envelope-controlled, per voice)
      -> voice sum
      -> output stage (tw_drive form, analog operating point)

Mono throughout: the instrument's output is mono, and unlike the organ and
`ep73` there is no cabinet or rotor stage to open a stereo field. A stereo
spread across the five voices is a [decision MD6] item and defaults to
absent.

Two envelope generators per voice, one to the filter and one to the VCA;
the LFO and the cross-modulation section are the only global modulators.
Each stage is a struct plus a render function; interfaces freeze early so a
stage can deepen without touching neighbours (house rule).

## The voice model

Five voices, each a fixed struct in an SoA bank of five. Per voice: two
oscillator phase accumulators, a filter state of four poles plus the
feedback node, two envelope states, a VCA gain.

- **Oscillators.** Sawtooth, variable-width pulse, and triangle; the second
  oscillator additionally reaches low frequencies for use as a modulator,
  and can be hard-synced to the first. Waveform shapes and their departures
  from ideal (the reset slew on the sawtooth, the pulse's width-dependent
  asymmetry) pin at MA0 from the oscillator datasheet.
- **Pitch.** A pinned 128-entry note table supplies the base frequency, as
  `ep_key_freq_hz` does for the tine line. Glide, detune, bend and drift
  multiply it by a ratio drawn from a small `exp2` polynomial over a bounded
  exponent — no runtime libm, and the bound is what makes the polynomial
  cheap and exact enough.
- **Filter.** Four-pole lowpass with resonance feedback and saturation. The
  cutoff law is the chip's transconductance-versus-control-current relation
  [MA0], not a chosen curve. The loss of passband gain as resonance rises —
  the thinning of the bass that the instrument is known for — is a modeled
  consequence of the feedback topology and is **not** compensated. That is
  the same posture the organ takes toward robbing.
- **Envelopes.** Four-stage, from the RC network's own time-constant range
  [MA0], through the existing exponential-coefficient kernel. The stage
  shapes are the capacitor's, so the characteristic non-linear attack is
  structural rather than a curve applied afterward.
- **Cross-modulation.** The second oscillator and the filter envelope
  routed to the first oscillator's frequency and pulse width, and to the
  filter cutoff. This runs at audio rate, so it aliases by construction and
  the MD2 method must survive it — the exhibit at MA4 says by how much.

## Polyphony and allocation

Fixed five-voice state, no dynamic allocation; the bank always ticks. The
allocator is a rule over which of the five is assigned:

- Assignment order and the stealing rule pin at MA0 from the service
  documentation [decision MD4]. Round-robin assignment and oldest-note
  stealing are the working assumption; both are audible in fast passages and
  neither is a free choice.
- Unison collapses all five onto one note, which is the instrument's other
  well-known voice mode and costs nothing structurally.
- Worst case is five voices sounding with the filter oversampled, and the
  budget is quoted at that bound.

Whether the bank ticks always or gates on envelope-idle is the MD7
measurement, decided the way EP1 decided the same question for the tine
bank: by numbers on the host and the SBC class, not by argument.

## Controls, velocity, MIDI

- The reference instrument has no velocity sensing. Velocity is parsed and
  ignored, counted for the driver to report — the organ's treatment of the
  same signal, and for the same reason: the machine does not read it. A
  velocity assignment is a [decision MD5] item, not a default.
- Control surface (working set): `cutoff`, `resonance`, `env_amount`, the
  two envelopes' four stages each, `mix`, `pulse_width`, `detune`, `lfo`,
  `poly_mod`, `glide`, `unison`, and `calibration` (the wear analog).
- Compass and note map pin at MA0 [decision MD1]; the reference instrument's
  61 keys and the organ's MIDI 36..96 are the working assumption, which
  would let one controller cover both lines. Out-of-compass notes are
  ignored and counted, as in both existing lines.
- The exact CC map pins at MA2, following the house convention of doing it
  at the first playable milestone. Panic (CC120/123) releases all voices.

## Determinism

Unchanged discipline: the same input renders bit-identical output on the
same binary; two-run FNV-64 signatures in tests. The core uses no RNG until
MA6, whose per-card draws use fixed seeds advanced per voice ordinal — the
M7 and EP7 pattern. Identity defaults are each bit-exact bypasses, so every
earlier milestone's renders stay pinned as later stages land: `calibration =
0`, `poly_mod = 0`, `lfo = 0`, output stage at 0, resonance at 0.

The autotune routine at MA6 is deterministic state, not a random process:
it runs on a fixed schedule and nulls the drift the same draws produced.

## Drivers, tests, docs

- Live driver: a third small binary, `mamutanalog`, sharing the ALSA loop
  shape and flags with `tw91` and `ep73`; no merged multi-instrument binary.
- `render_midi` grows a third instrument in its existing `-I` switch; the
  `apply_msg` / `apply_msg_ep` pair gains `apply_msg_ma`. The SMF reader is
  shared and nothing else is.
- `test/test.c` grows an `ma_` section; the organ's and `ep73`'s pinned
  signatures stand untouched in the same run.
- New docs: `docs/ma-constants.md` (MA0, same pinning discipline and source
  tags as `constants.md` and `ep-constants.md`), `docs/ma1-evidence.md`
  onward per milestone, renders logged in `docs/renders.md` with the
  instrument named. Source copies land in untracked `docs/externalDocs/`.
- `README.md` and `docs/design.md` gain the third line when MA1 lands code,
  not while this document is unscheduled.

## C discipline

- `ma_` prefix, snake_case, struct-per-stage with `init`/`set_*`/`tick`; all
  state in caller-provided structs.
- Setters sanitize hostile input at the boundary (NaN and out-of-range
  clamp); tick paths assume sane state.
- `[[nodiscard]]` on pure lookups; designated initializers where they make
  aggregates legible; flat control flow in tick paths.
- No speculative abstraction: no instrument interface, no callback plumbing,
  no helper extraction until duplication with the two existing lines is
  proven annoying in code that exists.

## MA milestones

- **MA0 constants** — `docs/ma-constants.md`: the reference revision
  [decision MD8], compass and note map (MD1), the oscillator's exponential
  converter law and its temperature error, waveform shapes and their
  departures from ideal, pulse-width range and its control law, the
  filter's transconductance cutoff law, the resonance-versus-passband-gain
  relation and the saturation point, envelope time-constant ranges per
  stage, mixer gain staging and where it overdrives the filter, the
  allocation and stealing rule (MD4), the cross-modulation routing and
  depths, LFO range, the per-card tolerance inventory with working levels.
  Sources: the voice-chip datasheets, the manufacturer's service
  documentation, and the virtual-analog literature for discretization only
  — a discretization method is not a machine constant and is tagged as
  such. Gate: the register audit passes.

- **MA1 oscillators** — the bandlimiting milestone, offline only: two
  oscillators, the waveform set, hard sync, determinism signatures.
  Founding exhibit (M1-spirit): naive versus bandlimited across the
  compass, aliasing tabled as in-band spurious energy and audible A/B, at
  the fundamental and under sync; the cost of each candidate measured on
  the host and the SBC class. Gate: MD2 closed by that table, signatures
  stable.

- **MA2 first playable** — voice bank, allocator and stealing (MD4 wired),
  both envelopes, the VCA, unison, glide; live binary on the rig; panic and
  xrun recovery; `render_midi` twin with the instrument switch; CC map
  pinned. Unfiltered but musical. Gate: live play on the rig; twin renders
  logged and signed.

- **MA3 filter** — the four-pole lowpass: topology-preserving structure,
  the chip's cutoff law, saturation inside the loop under a fixed iteration
  budget, the passband-gain loss left as a consequence. MD3 closed with the
  oversampling cost measured, not assumed. Exhibit: cutoff sweep against
  the pinned law, self-oscillation purity and tuning, resonance-versus-bass
  A/B. The instrument becomes recognizable here.

- **MA4 cross-modulation, LFO, wheels** — the routing matrix that is this
  instrument's signature, at audio rate; the LFO and the modulation wheel's
  depth. Exhibit: aliasing under cross-modulation, which is the hardest
  case MD2 has to survive.

- **MA5 output stage** — mixer overdrive into the filter, and the output
  stage in the `tw_drive` form at an analog operating point; kernel choice
  recorded per the warmth doctrine. Zero is an exact bypass.

- **MA6 calibration** — the wear pass: per-oscillator tuning drift and its
  slow random walk, exponential-converter temperature error, per-card
  filter cutoff offset, envelope timing spread, VCA offset, and the
  autotune routine that periodically nulls the drift. One knob; 0 is the
  idealized reference, bit-exact to every pre-MA6 render; shipped default
  nonzero, since tolerances exist on a factory-new unit. Fixed-seed draws
  per voice ordinal.

- **MA7 (optional) metrics** — FFT-based aliasing and THD measures, filter
  response recovery against the pinned law, drift statistics. Shares the M8
  FFT if and when that lands.

Estimate: `ep73`'s core is ~1210 LOC. This line is larger — the allocator,
the cross-modulation matrix and oversampling are all new — at roughly
**~1700 LOC core**, plus ~250 driver, ~600 exhibit, ~1200 test. About
1.3–1.5x the tine line, or seven milestones at the existing cadence.

## Risks

- Aliasing is a new problem class for this repo, and it appears twice: in
  the oscillators (MA1) and again under audio-rate cross-modulation (MA4),
  where the MA1 choice may not hold. Mitigation: MA1's exhibit measures
  candidates rather than picking one, and MA4 carries its own gate.
- The filter's cost is the budget risk. Five voices with an oversampled
  saturating four-pole is the worst case, and the SBC target in `design.md`
  is the bound it must clear. MD3 is closed by measurement at MA3, with
  degradation order (oversampling ratio, then iteration count) named there.
- The reference instrument has two materially different revisions with
  different voice chips, and they do not sound the same. Building "both"
  would force a chip abstraction before either exists; MD8 picks one at
  MA0 and the other becomes a named follow-on variant.
- This line is heard against a saturated field of software emulations of
  the same instrument, several of them mature. The repo's answer is the
  same as the organ's: the identity is in the modeled deviations and the
  sourced constants, and the comparison is made by ear against reference
  recordings, kept outside the repo.
- Reference recordings must exist before MA3's by-ear pass. Unlike the tine
  line, isolated material is easy here — single-oscillator tones, filter
  sweeps and self-oscillation are common — which lowers this risk relative
  to `ep73`.

## Decision register

| ID  | Decision                                                      | Closes at |
| --- | ------------------------------------------------------------- | --------- |
| MD1 | Compass and note map (61 keys, MIDI 36..96 assumed)           | MA0       |
| MD2 | Bandlimiting method, by measured aliasing and cost            | MA1       |
| MD3 | Oversampling ratio and iteration budget in the filter loop    | MA3       |
| MD4 | Voice assignment and stealing rule                            | MA0       |
| MD5 | Velocity assignment, if any (reference instrument has none)   | MA2; else unscheduled |
| MD6 | Stereo spread across voices: land small or stay absent        | post-MA5  |
| MD7 | Voice bank: always-tick vs envelope-gated (by measurement)    | MA2       |
| MD8 | Reference revision (early vs late voice chips)                | MA0       |
| MD9 | Part designations in prose — the `AO-28` precedent            | MA0       |

## References

Acquired at MA0 into untracked `docs/externalDocs/` and pinned in
`ma-constants.md`, not here. The set to acquire:

- The oscillator and filter chip datasheets for both revisions, and the
  manufacturer's service documentation carrying the voice-card schematic.
- Stilson, Smith — ICMC 1996 (analysis of the ladder filter and its digital
  implementation).
- Huovilainen — DAFx 2004 (nonlinear digital implementation of the ladder).
- Välimäki, Huovilainen — Computer Music Journal 2006 (oscillator and
  filter algorithms for virtual analog synthesis).
- D'Angelo, Välimäki — ICASSP 2013 (improved virtual analog ladder model).
- Zavalishin — the virtual-analog filter design text (topology-preserving
  transform, zero-delay feedback).
- Välimäki — IEEE Signal Processing Letters 2005 (differentiated polynomial
  waveform, reduced-aliasing sawtooth).
- Brandt — ICMC 2001 (minimum-phase bandlimited step; hard sync without
  aliasing).
- Parker, Zavalishin, Le Bivic — DAFx 2016 (antiderivative antialiasing for
  nonlinear waveshaping).
- Hutchins — the Electronotes newsletters, for the period's circuit
  practice.
- Moog — JAES 1965 (voltage-controlled modules) and US Patent 3,475,623
  (granted 1969-10-28), the transistor ladder filter. Prior art to the
  target's filter rather than its source, and cited as such.
