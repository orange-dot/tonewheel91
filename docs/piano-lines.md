# tonewheel91 — piano lines (speculative)

Date: 2026-07-25. An assessment of the two nearest keyboard-instrument lines —
an electric piano (tine/reed family) and an acoustic piano — measured against
what this engine already is. Nothing here is scheduled, and none of it is
implemented. Complexity figures are working estimates in tw91-milestone
currency, not commitments. An implementation backlog derived from this
assessment — still unscheduled — lives in `piano-backlog.md`.

Verdict up front: the engineering order is **electric piano first**. It is the
organ's closest electromechanical relative (~0.7–1x the total tw91 effort,
large direct reuse), and the hammer/damper/decaying-voice machinery it forces
into existence is exactly the foundation the acoustic piano needs. The
acoustic piano is a change of problem class, not just of instrument —
realistically 3–5x the tw91 effort, with the risk concentrated in the
soundboard coupling and sympathetic resonance.

## Where the engine sits

The relevant assets, per `design.md`:

- A freestanding, deterministic, bounded core: no alloc, no libm, fixed state,
  fixed iteration budgets, SoA banks, FNV-64 two-run signatures.
- An electromechanical back half: asymmetric pickup nonlinearity with a
  measured alpha (M7), stateful bias-excursion drive (M5), amp/cabinet
  treatment (M6).
- Full plumbing: MIDI byte parser, ALSA live driver, `render_midi`
  deterministic twin, WAV/PNG writers, evidence-doc and constants-pinning
  workflow.
- A generator paradigm that does **not** carry over: always-running additive
  wheels, gain-gated. Both piano lines replace this with struck, decaying
  voices — velocity scales loudness for the first time (the organ maps it to
  contact stagger only).

## Line 1: electric piano (Rhodes / Wurlitzer family)

Same instrument genus: electromechanical generator -> nonlinear pickup ->
preamp/drive -> tremolo -> speaker. Most of the back half is already built.

What transfers directly:

- **Pickup nonlinearity.** The M7 asymmetric pickup stage is the same physics
  as the tine/pole relationship that produces the bell-to-bark transition:
  asymmetric displacement against the magnet pole, second harmonic rising
  with amplitude. Reuse with recalibrated constants, not new DSP.
- **Drive.** The M5 bias-excursion stage covers the Wurlitzer's internal amp
  and the Rhodes preamp; same structure, new constants.
- **Tremolo.** Rhodes suitcase stereo pan / Wurlitzer AM is trivial next to
  the scanner and the rotary — an LFO and a gain law, no dispersive line, no
  inertia.
- All plumbing (parser, driver, twin renderer, evidence workflow) unchanged.

The genuinely new physics — one item:

- **A struck, decaying resonator.** A tine is close to a tuning fork: 2–3
  dominant modes per note, modeled as damped modal oscillators (a phase
  accumulator plus a decay smoother — kernels the core already has). On top
  of it: a hammer with a felt/neoprene tip (velocity-dependent excitation), a
  damper, and a sustain pedal. No inter-note coupling: every tine is
  independent and damped, so polyphony state stays fixed and small.

Cost and constraints: fits the existing doctrine whole — deterministic,
bounded, no solvers. CPU sits *below* the organ: only active notes ring,
versus 91 wheels always running. The dominant work is calibration, not code:
the dynamic-timbre curve across velocity (where bell becomes bark) is the
instrument's identity, and it is by-ear work of the same kind as the M7 wear
depths. Sources exist in the same genre as the organ's: service manuals and
patents (Rhodes patents; Wurlitzer service documentation), plus DAFx-lineage
physical-modeling literature on the Rhodes (Fontana et al.).

Estimate: one M0–M7-shaped cycle of similar scope, with a large discount at
the M4/M6 slots (no scanner or rotary equivalent exists in this instrument).

## Line 2: acoustic piano

A different problem class. The five properties that make it qualitatively
harder than either electromechanical instrument:

1. **Non-ideal strings.** Stiff-string dispersion (inharmonicity, a per-note
   curve); 2–3 strings per note with micro-detuning producing the two-stage
   decay of coupled strings (Weinreich); longitudinal modes coupling
   nonlinearly to transverse motion at forte (phantom partials).
2. **A seriously nonlinear hammer.** Hysteretic felt compression (Stulov);
   contact duration depends on velocity, and that dependency carries the
   instrument's entire dynamic brightness.
3. **Soundboard and bridge.** String-to-board impedance coupling sets the
   decay rates; the board is a high-modal-density radiator. Nothing in the
   current engine is an analog of this stage.
4. **Sympathetic resonance.** The sustain pedal opens coupling across all
   ~230 strings — a coupling matrix, not 88 independent voices. Plus una
   corda, half-pedaling, and duplex scales.
5. **Budget.** The organ core is ~100–150 Mflop/s. A modal piano with the
   pedal down and large chords trends toward thousands of ringing modes — an
   order of magnitude up, and on the SBC target it would demand aggressive
   mode-count management. The waveguide route (Smith; Bank & Välimäki) is
   cheaper and real-time proven, but the nonlinear phenomena still have to be
   added by hand on top.

Two non-DSP reasons the line is harder than it looks:

- **Calibration data explodes.** The organ's constants came from gear tables,
  service documentation, and patents — finite, exact numbers. The piano's
  equivalents are per-note measurements: inharmonicity curves, paired decay
  constants (two polarizations), hammer curves per register. The literature
  exists (Fletcher & Rossing; Weinreich 1977; Stulov; Bank & Välimäki;
  Chabassier's full FDTD model), but the numbers are fitted, not transcribed.
- **The comparison bar is brutal.** This project is judged by ear against
  canonical recordings. For the organ, a component model competes well on
  that test. An acoustic piano is inevitably heard against sample libraries
  every ear knows; the one physical model that has won that comparison
  (a commercial modal-modeling product) is the multi-year output of an
  entire company.

Estimate: three to five tw91-scale cycles, with the largest uncertainty in
the soundboard coupling and sympathetic-resonance stages.

## Existing reference: Pianoteq

Modartt's Pianoteq is the commercial proof that the acoustic-piano line in
section "Line 2" above is buildable in real time without a sample library —
worth recording here as a concrete existence proof, installed locally as a
trial (`~/Applications/Pianoteq 9`, v9.2.1) for by-ear reference.

It is physical-modeling synthesis, not sample playback — visible directly
from install size (~40 MB versus the tens of gigabytes a sampled grand
needs). Per the public documentation, the engine covers the same hard items
listed above, run live rather than baked into samples:

- Nonlinear hammer-string contact: a hysteretic (viscoelastic) hammer whose
  stiffness depends on strike velocity, carrying the piano-to-forte timbre
  shift with no velocity-layered samples.
- Dispersive strings: per-string inharmonicity from stiffness, so partials
  are not exact integer multiples of the fundamental.
- Unison coupling: 2-3 strings per note, detuned, producing the Weinreich
  two-stage decay (fast initial drop, extended aftersound).
- A soundboard modeled as a resonator/impulse response that colors the
  spectrum and carries most of the tone's body.
- Sympathetic resonance as an actual coupling matrix across all ~230
  strings when the sustain pedal is down, not a bolted-on reverb effect.
- Duplex scale, una corda, and mechanical pedal/hammer noise as further
  model layers.

Model parameters (string length/tension/mass, hammer hardness, board
characteristics, mic placement) are calibrated against measurements on real
concert instruments; each selectable "instrument" is a different parameter
set for the same physical model, not a different sample set — which is why
hammer hardness, string tension, and mic position are adjustable in real
time, something a sample-based instrument cannot offer without
re-recording.

This is a real-time, shipped instance of the PHS/modal direction flagged as
the "under-served combination" in `research-frontier.md` — proof the
approach is viable, and also the calibration bar: it is the multi-year
output of an entire company, which is the basis for the 3-5x estimate above
rather than a number pulled from nowhere.

## Ordering

Electric piano first — not only because it is smaller. The hammer, damper,
voice-allocation, and decaying-voice machinery built there is precisely the
acoustic piano's foundation; after it, the acoustic line adds "only"
dispersion, string coupling, the board, and sympathetic resonance — still an
enormous job, but one standing on finished groundwork. The reverse order
would load the hardest project with all of that foundational work as well.

## References

Author-venue-year, in the spirit of the evidence docs; titles paraphrased
where a verbatim title carries a protected name.

- Weinreich — JASA 1977 (coupled piano strings; two-stage decay and
  aftersound).
- Stulov — hysteretic piano-hammer felt models.
- Fletcher, Rossing — The Physics of Musical Instruments (piano chapters).
- Smith — digital waveguide synthesis; commuted synthesis of struck strings.
- Bank, Välimäki — modal and waveguide piano modeling (multiple papers).
- Chabassier et al. — full FDTD piano model (not real-time; the accuracy
  ceiling reference).
- Askenfelt, Jansson — piano touch and hammer-string measurements.
- Fontana et al. — real-time physically-informed Rhodes modeling.
- Rhodes and Wurlitzer patents and service documentation — primary sources
  for the electric line, same genre as the organ's constants sources.
