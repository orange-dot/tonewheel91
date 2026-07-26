# tonewheel91 — piano implementation backlog

Date: 2026-07-26. Status: proposed backlog — plan only. No code exists for
either line and nothing is scheduled; this document turns the
`piano-lines.md` assessment into an ordered, gated plan so that starting
is a decision, not a design session. Ordering follows that doc's verdict:
electric piano first; the acoustic piano is a later program standing on
the electric line's machinery.

Currency: milestones are tw91-milestone-shaped. Effort totals restate
`piano-lines.md` (~0.7–1x tw91 for the electric line, 3–5x for the
acoustic program) — working estimates, not commitments. Open choices are
tagged [decision Dn] and collected in the register at the end; each names
the milestone that closes it.

## Part 1 — electric piano (working name: ep73)

### Scope

One instrument: the tine line. The reed sibling is a named follow-on
variant, not a co-developed target — its generator differs and its pickup
is electrostatic, which does not share the M7 electromagnetic form.
Building both at once would force a generator abstraction before either
exists; building one keeps every interface concrete. [decision D8]

Working name `ep73`: 73 keys, E1–E7, MIDI 28..100 — the mid-size compass
of the reference instruments. Out-of-compass notes are ignored and
counted, as in the organ. Brand names stay in references and sources,
never in identifiers or user-facing text (house discipline). Compass and
name pin at EP0. [decision D1]

### Repo cut

Same repo, sibling instrument family — not a fork, not a framework.

- New core translation units under `src/` with an `ep_` prefix; one new
  header `src/epiano.h` that includes `tonewheel.h` for the shared
  kernels (`tw_sin_turns`, `tw_sat`, `tw_splitmix64`, `tw_fnv1a64`), the
  MIDI parser (`tw_midi_*`), and `tw_drive`. The organ never includes
  `epiano.h`.
- No shared "instrument framework": two top-level structs, two live
  binaries, no vtable, no dispatch layer. A `kernels.h` split out of
  `tonewheel.h` is named here and explicitly not scheduled — including
  `tonewheel.h` costs nothing until proven otherwise.
- The organ stays bit-stable by construction: no organ TU is edited, and
  the pinned organ signature suite in `test/test.c` is the enforced
  proof. Any EP change that moves an organ signature is a defect.
- Same core doctrine, unchanged: freestanding C23, no OS calls, no
  allocation, no libm, fixed state in caller-provided structs, f32,
  SoA banks, epsilon-snap smoothers, never `-ffast-math`,
  `-ffp-contract=off`.

### What transfers, what is new, what is not carried

Transfers as-is (constants may change, structure does not):

- MIDI byte parser; ALSA live-loop shape (read bytes -> parse -> state ->
  render period -> blocking write); xrun recovery and panic pattern.
- `tw_drive` — the EP preamp is the M5 bias-excursion stage with a new
  operating point (EP5).
- WAV/PNG writers, exhibit and viz harnesses, render_midi twin,
  constants-pinning and evidence-doc workflow, FNV-64 two-run signature
  discipline, epsilon-snap smoother discipline, the bypass-is-bit-exact
  ("scanner-OFF") discipline.

Recalibrated, same form:

- Pickup nonlinearity: the M7 asymmetric cubic (`pk2`/`pk3` shape) —
  but applied per voice, before summation, so it is IMD-free per note
  (the t2/t3 discipline transplanted). New alpha, new asymmetry targets.
- Cabinet treatment: the M6 early-reflection machinery is the candidate
  form if EP6 lands at all.

Not carried (the paradigm change `piano-lines.md` names):

- Always-running wheels, gear table, foldback, taper/robbing, contact
  stagger and bounce, percussion, scanner, rotary. Velocity -> contact
  stagger is organ-only; in ep73 velocity scales loudness and timbre —
  the first time in this codebase.

### Signal chain

    73 struck-voice bank (silent until struck; velocity-excited)
      -> per-voice pickup nonlinearity (asymmetric, pre-sum)
      -> passive sum [+ static voicing filter, only if the ear demands]
      -> preamp drive (tw_drive, EP operating point)
      -> tremolo / stereo pan (LFO gain law; stereo begins here)
      -> speaker/cabinet treatment (small; possibly deferred)

Mono until the tremolo stage. Each stage is a struct plus a render
function; interfaces freeze early so internals can deepen without
touching neighbours (house rule).

### The voice model (the one new physics)

Per key, 2–3 damped modal partials: the tine fundamental, the fork
overtone (clamped-bar theory places it near 6.27x; pinned at EP0), and
one attack/tone-bar partial if the ear demands a third. [decision D2]
Each mode is a phase accumulator rendered through `tw_sin_turns` with an
exponentially decaying amplitude — a one-pole toward zero with the
existing epsilon snap, so a decayed voice reaches exact silence and
never enters denormal territory.

- **Strike.** A note-on writes the modal amplitude set from the EP0
  velocity tables — level and spectrum both. No continuous hammer-contact
  simulation at this depth: contact-duration physics folds into the
  per-velocity spectral weights, which is the model-depth doctrine's
  "electronics — calibrated behavior" slot applied to a mechanical
  contact. The felt/hysteresis ladder stays on the acoustic side (A3).
- **Damper.** Release swaps the decay coefficient to the damper rate.
  The sustain pedal (CC64) defers damper engagement; a release with the
  pedal down keeps the free-decay rate and dampers apply on pedal-up.
- **Restrike.** A strike on a ringing voice adds energy to a live phase;
  the law (amplitude replace vs add, phase continue vs reset) is audible
  and undecided — closed at EP2 by A/B exhibit. [decision D5]
- **Polyphony.** Fixed per-key state in SoA banks, no voice allocator,
  no stealing: 73 resonators exist physically and their state is theirs.
  Worst case is the full compass ringing (pedal down, glissando) and the
  budget is quoted at that bound.

One layout decision stays open into EP1, closed by measurement, not
argument [decision D4]:

- *Always-advance*: all ~219 mode oscillators tick every sample,
  amplitudes zero when silent — constant cost, branch-free,
  auto-vectorizes like the wheel banks. Rough worst case ~150–200
  Mflop/s, the organ core's ballpark.
- *Active-gated*: voices below the amplitude epsilon skip entirely —
  typical cost far below the organ (the `piano-lines.md` claim), same
  worst case. Physically honest for this instrument: a tine at rest has
  no phase worth preserving, unlike a wheel, so the organ's
  "never branch-gated" rule does not bind here — it exists to protect
  wheel phase continuity, which has no EP equivalent.

EP1 carries a perf exhibit of both layouts on the host and the SBC
class; the pinned choice records the numbers.

### Velocity, controls, MIDI

- Velocity -> loudness law and velocity -> spectrum tables (the
  bell-to-bark identity curve, per register) both pin at EP0 as
  [FOLK]/[decision] working values; EP3's by-ear pass owns them. This is
  the instrument's identity and the dominant work, per `piano-lines.md`.
- Control surface (working set): `condition` (the wear analog),
  `drive`, `tremolo` (off | rate/depth or stereo pan variant),
  `cabinet` (bypass default). Sustain pedal is performance state, not a
  panel control.
- MIDI: notes 28..100; velocity as above; CC64 sustain; CC11 unassigned
  for now (no swell — the instrument has no expression pedal; a volume
  assignment is a [decision D6] item, not a default); panic (CC120/123)
  drops all dampers immediately and silences in the damper tau. Poly key
  pressure has no EP meaning yet — parsed, ignored, counted. The exact
  CC map pins at EP2, following the organ's convention of doing it at
  the first playable milestone.

### Determinism

Unchanged discipline: same input renders bit-identical output on the
same binary; two-run FNV-64 signatures in tests. The EP core uses no RNG
until EP7 — a strike is fully determined by its event, there is no
bounce equivalent — and EP7's per-note character draws use fixed seeds
advanced per note ordinal, the M7 pattern. Identity defaults: `condition
= 0`, tremolo off, `drive = 0`, cabinet bypass are each bit-exact
bypasses (the scanner-OFF discipline), so every earlier milestone's
renders stay pinned as later stages land.

### Drivers, tests, docs

- Live driver: a second small binary (working name `ep73`) sharing the
  ALSA loop shape and flags with `tw91`; no merged multi-instrument
  binary. render_midi grows an instrument switch defaulting to the organ
  so existing logged renders and CLI behavior stay stable.
- `test/test.c` grows an EP section; the organ's pinned signatures stand
  untouched in the same run — the bit-stability guarantee is asserted,
  not promised.
- New docs: `docs/ep-constants.md` (EP0, same pinning discipline and
  source tags as `constants.md`), `docs/ep1-evidence.md` onward per
  milestone, renders logged in `docs/renders.md` with the instrument
  named.

### C discipline (how the house style applies)

- `ep_` prefix, snake_case, struct-per-stage with `init`/`set_*`/`tick`;
  all state in caller-provided structs; zero-initializable where the
  parser pattern fits.
- Setters sanitize hostile input at the boundary (NaN/out-of-range ->
  clamp), tick paths assume sane state — the `tw_organ_set_*` contract.
- `[[nodiscard]]` on pure lookups; designated initializers where they
  make aggregates legible; flat control flow in tick paths.
- No speculative abstraction: no instrument interface, no callback
  plumbing, no helper extraction until duplication with the organ is
  proven annoying in code that exists. Deletion pressure applies: the
  voicing filter and the cabinet stage enter only when the ear demands
  them, and default to absent/bypass.

### EP milestones

- **EP0 constants** — `docs/ep-constants.md`: compass and note map
  (D1); per-note fundamentals (equal temperament — tines are tuned by
  design, there is no gear table; stretch, if any, is a [FOLK] entry);
  mode ratios and a Nyquist clamp rule (a mode above the guard band
  renders at gain zero — foldback-spirit, but silence, since a piano
  borrows nothing) (D2); per-register decay taus; velocity -> level law
  and velocity -> spectrum tables (D3 draft); pickup alpha/asymmetry
  recalibration targets; damper rates and pedal semantics; tremolo
  rates/depths; drive reference points; a mechanical-noise inventory
  (hammer thump, damper return, key bed) with working levels. Sources:
  the patent and service-documentation genre plus the DAFx-lineage
  Rhodes literature and clamped-bar acoustics; every number sourced or
  tagged with an owner. Gate: the register audit passes.
- **EP1 struck-voice bank** — the new physics, offline only: SoA bank,
  strike/decay, per-voice pickup stage, summation; determinism
  signatures land. Founding exhibit (M1-spirit): one note's velocity
  ladder pp -> ff, tabled second-harmonic rise and decay envelope,
  audible A/B; plus per-register decay conformance against the EP0 tau
  table; plus the D4 layout measurement on both targets. Gate:
  signatures stable, exhibit tables match pinned constants, D4 closed.
- **EP2 first playable** — dampers, sustain pedal, restrike law (D5
  closed by A/B exhibit), velocity -> loudness wired, live binary on
  the rig, panic and xrun recovery, render_midi twin with the
  instrument switch; CC map pinned. Gate: live play; twin renders
  logged and signed.
- **EP3 hammer voicing** — the identity milestone and the dominant
  cost: per-register strike-spectrum calibration against reference
  recordings by ear (the M7-wear kind of work, scheduled early because
  it is the instrument); hammer-noise transient; bark-threshold
  placement across the compass; D3 finalized. Gate: A/B exhibit vs
  reference set; constants re-pinned with the by-ear values.
- **EP4 tremolo/pan** — the discounted M4 slot (no scanner exists
  here): one LFO and a gain law — mono AM variant and stereo
  opposing-pan variant behind one control; stereo begins; off is a
  bit-exact mono bypass. Small by design.
- **EP5 drive** — `tw_drive` at the EP operating point; kernel choice
  (derived triode curve vs the M5 odd kernel) recorded per the warmth
  doctrine; `drive = 0` bypass identity holds.
- **EP6 cabinet/speaker** — the discounted M6 slot (no rotary): either
  a short rolloff-plus-early-reflections treatment reusing the M6
  cabinet form, or explicit deferral in the busbar "last-or-never"
  spirit. Decided by ear after EP5, not before. [decision D7]
- **EP7 condition** — the wear pass: per-note tuning/voicing spread,
  pickup-distance spread (per-note alpha), hum and noise floor, damper
  and pedal mechanical noises, behind one `condition` knob; 0 is the
  idealized reference, bit-exact to every pre-EP7 render; shipped
  default nonzero (the factory-new tolerance doctrine); fixed-seed
  draws.
- **EP8 (optional) metrics** — decay-rate recovery and velocity-curve
  recovery checks against the pinned tables; shares the M8 FFT if and
  when that lands.

Estimate: one M0–M7-shaped cycle at ~0.7–1x tw91, the EP4/EP6 slots
heavily discounted, the cost concentrated in EP0/EP3 calibration.

### EP risks

- Calibration data is fitted, not transcribed: the organ's constants
  came from tables and patents; the EP's velocity-timbre tables come
  from recordings and by-ear work. Mitigation: EP0 pins working values
  with owners; EP3 is scheduled early and sized as the dominant cost.
- Restrike behavior is audible, idiomatic (fast repeated notes), and
  has no obvious single answer — hence a dedicated A/B gate (D5).
- The reed variant tempts scope creep into a generator abstraction;
  D8 exists to keep it a post-EP7 decision.
- Reference-recording curation (kept outside the repo, house rule) must
  exist before EP3 or the identity milestone stalls.

## Part 2 — acoustic piano (program outline; not scheduled)

Gated on the electric line: EP delivers the struck-voice bank, damper
and pedal machinery, the velocity paradigm, and the evidence workflow
the acoustic program assumes. Milestones below are coarser — each is
closer to a tw91-cycle phase than a tw91 milestone — and the 3–5x
estimate with risk concentrated in A4/A5 stands from `piano-lines.md`.

- **A0 data and literature** — the "calibration data explodes"
  milestone: per-note inharmonicity curves, paired two-polarization
  decay constants, hysteretic hammer parameters per register, touch and
  contact-time measurements. Output is an `ap-constants.md` skeleton
  where every section is either filled from literature or named as a
  fitting task with a method. No DSP.
- **A1 synthesis-route gate** — modal (per-note inharmonic partial
  tables; nearest to house kernels; worst case trends toward thousands
  of ringing modes) versus waveguide (cheaper, real-time-proven;
  dispersion and nonlinearity added by hand). Closed by two costed
  prototypes measured on the host and the SBC class, plus a target
  decision: the SBC is likely out at full depth, so the target ladder
  (host first; embedded class revisited later) is decided here, not
  assumed. [decisions D9, D10]
- **A2 stiff-string voice** — dispersion (inharmonic partials per
  note), 2–3 detuned unison strings per note, the two-stage decay.
  Founding exhibit: coupled-unison A/B against a single string —
  aftersound present vs absent, tabled and audible.
- **A3 hysteretic hammer** — velocity-dependent contact with felt
  hysteresis; contact duration carrying dynamic brightness. Replaces
  the EP's calibrated strike tables with the real mechanism; the EP
  tables remain the fallback voicing layer.
- **A4 bridge and soundboard** — risk item one. String-to-board
  impedance coupling sets decay rates; the board itself follows the
  acoustics doctrine — "physics where it pays, data where it pays
  more": a measured resonator/IR treatment in a fixed structure, not
  heavier simulation. Budget-gated.
- **A5 sympathetic resonance and pedals** — risk item two. Pedal-down
  coupling across the full string set as a pruned/gated coupling
  matrix under an explicit mode-count budget; half-pedal and una corda
  semantics. Budget-gated with a defined degradation order (which
  couplings drop first, and audibly why).
- **A6 second-order identity** — duplex scale, hammer/pedal mechanical
  noise layers, release samples-equivalent behaviors done as model
  layers, not samples.
- **A7 condition pass** — the wear analog: unison detune spread,
  voicing drift, per-note hammer wear; same one-knob, zero-is-idealized
  discipline.

The comparison bar stays brutal and is recorded as such: this line is
heard against sample libraries every ear knows, and the one physical
model that wins that comparison is a company's multi-year output —
`piano-lines.md` keeps the local trial install as the reference bar.
The program only proceeds phase by phase, each with its own by-ear gate;
an honorable stop after A4 still leaves a playable, novel instrument.

## Decision register

| ID  | Decision                                                    | Closes at |
| --- | ----------------------------------------------------------- | --------- |
| D1  | Compass and working name (73, E1–E7, MIDI 28..100)          | EP0       |
| D2  | Mode count and ratios per voice (2 vs 3; overtone ratio)    | EP0       |
| D3  | Velocity -> level law and -> spectrum tables                | EP0 draft, EP3 final |
| D4  | Bank layout: always-advance vs active-gated (by measurement)| EP1       |
| D5  | Restrike law: amplitude replace/add, phase continue/reset   | EP2       |
| D6  | CC11/volume assignment; poly-pressure meaning (if any)      | EP2; else unscheduled |
| D7  | Cabinet stage: land small or defer last-or-never            | EP6       |
| D8  | Reed variant go/no-go (electrostatic pickup form)           | post-EP7  |
| D9  | Acoustic synthesis route: modal vs waveguide (costed)       | A1        |
| D10 | Acoustic target ladder (host-only vs embedded class)        | A1        |

## References

Inherits the `piano-lines.md` reference set verbatim (Weinreich; Stulov;
Fletcher & Rossing; Smith; Bank & Välimäki; Chabassier; Askenfelt &
Jansson; Fontana et al.; the patent and service-documentation genre for
the electric line). New sources acquired at EP0/A0 are pinned in the
respective constants docs, not here.
