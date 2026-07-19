# tonewheel91 — gesture control plane (speculative)

Date: 2026-07-19. A design reference on giving a keyboard instrument a rich
continuous control surface beyond pitch bend, a mod wheel, and a pedal. Nothing
here is scheduled or implemented. It pairs with `instrument-concepts.md`: the
dynamical-system instruments there expose many continuous parameters at once,
which a conventional keyboard cannot reach and a gestural surface can.

## The problem

A keyboard exposes few continuous controls — pitch bend, one mod wheel, perhaps
one expression pedal. A field or dynamical instrument wants several continuous
parameters driven *simultaneously* and *without quantization*: coupling, drive,
heat, criticality, listening position, and so on. The mismatch is the motivation
for a second, gestural control surface, typically for the non-keyboard hand.

## A correction: the theremin is one degree of freedom per hand

A theremin senses the **bulk capacitance** — effectively the distance — of a
hand near an antenna. Two antennas give roughly one continuous degree of freedom
per hand (proximity for pitch, proximity for volume). It does **not** sense
individual fingers or hand pose. So "the position of five fingers and the whole
hand as many controls" is not what a theremin does; it is a richer idea that
needs a different sensor. The theremin is worth keeping as the *metaphor* — a
hand shaping a field without contact — but not as the mechanism.

## Sensor classes for a true multi-degree-of-freedom hand

| Sensor | What it senses | Rough DOF | Notes |
| --- | --- | --- | --- |
| Radar (60 GHz, e.g. Soli) | fine finger micro-motion, hand distance/approach | several | no contact; the closest "theremin, but multi-DOF" |
| Optical hand tracking (e.g. Leap / Ultraleap) | full hand pose, per-finger joints | ~20+ | camera + pose estimation; occlusion and lighting sensitive |
| Surface EMG wristband (e.g. CTRL-labs lineage; Myo) | motor-neuron intent, per-finger, subtle | several+ | worn, not free-space; low fatigue; needs per-user calibration |
| Data glove / instrumented hand (e.g. MiMu; STEIM "The Hands") | flex per finger + inertial orientation | ~10+ | worn; robust; established musical use |

The right choice for "the whole hand as a powerful controller" is radar,
optical, or EMG — not a theremin.

## The real problem is not the sensor — it is the mapping

Even with 15–20 raw degrees of freedom, a direct one-to-one map onto instrument
parameters is unplayable. A theremin is famously hard precisely because
free-space gesture has **no proprioceptive anchor** — nothing to touch, no
detent, no reference. Two mitigations, both compatible with the engine's
discipline:

- **A learned low-to-high mapping (a gestural manifold).** A natural hand motion
  should trace a *musically coherent path* through parameter space, not move
  many independent sliders. Interactive machine-learning tools (Fiebrink's
  Wekinator family) build exactly such maps from a handful of demonstrated
  poses; the deployed map can be a fixed function, preserving determinism.
- **A closed audible/visual feedback loop as the anchor.** A read-only view of
  the instrument's internal state, plus the sound itself, closes the eye/ear
  loop that free-space control otherwise lacks — the substitute for the missing
  tactile reference.

## The bimanual form

The natural split is two roles over one instrument:

- **Right hand — the keyboard** — excites the instrument (notes, or note-events
  injected as strikes into a dynamical medium).
- **Left hand — the gestural surface** — shapes the instrument's *state*: its
  coupling and drive, its regime (how close to a threshold), its material state,
  and — the least obvious, most valuable control — **where in the system the
  output is heard**. Moving the listening position through the medium is itself
  a performance gesture.

This is the potter's-wheel division: one hand forms, the other strikes.

## Keep the engine sensor-agnostic

The sensor should be abstracted behind a high-resolution continuous-control
transport, so the instrument sees normalized degrees of freedom and never a
specific device. MIDI 2.0's Universal MIDI Packet (high-resolution, per-note
expressive controllers) is the natural carrier; any of the sensors above becomes
just a source on that transport. Two consequences:

- **Reproducibility.** A recorded gesture path is an event stream like any
  other; replayed against the same build it renders identically, so gestures can
  be scripted and tested offline before any hardware exists, while live gesture
  input stays in the non-deterministic session class.
- **Real-time hygiene.** A gestural source is high-rate; the same bounded-queue,
  coalescing, drop-visible discipline the engine already applies to controllers
  covers it.

## Prior-art ledger (honesty)

Gestural controllers are not new: the theremin (Léon Theremin, 1920s), optical
hand-tracking instruments, radar micro-gesture experiments, worn glove
controllers (MiMu; STEIM's "The Hands", Waisvisz 1984), and EMG instruments all
exist, and continuous multi-degree-of-freedom gesture is well explored. What
would be new is not the controller but the coupling: a rich hand gesture driving
not only timbre but the **statistical regime, material state, and listening
position** of a bounded, deterministic dynamical instrument — a hand that plays
the distance to an event, the front of a transition, or where in the medium one
listens.

## Ergonomic and honesty caveats

- **Free-space fatigue.** Sustained mid-air gesture tires the arm ("gorilla
  arm"); worn sensors (EMG, glove) and gesture-relative rather than
  absolute-position mapping reduce it.
- **Latency budget.** Sensor and inference latency add to the audio path; the
  mapping must be cheap and bounded, consistent with the render-time rules.
- **Live input is non-deterministic** by nature; only recorded gesture paths are
  reproducible. The determinism claim belongs to the scripted/offline class,
  never to a live take.

## References

- Theremin — 1920s (heterodyne capacitive gesture sensing).
- Lien et al. — SIGGRAPH 2016 (Soli: millimeter-wave radar for fine gesture).
- Ultraleap / Leap Motion — optical hand tracking.
- CTRL-labs / surface-EMG neural interface; Thalmic Labs "Myo".
- MiMu gloves (Heap et al.); Waisvisz — STEIM "The Hands", 1984.
- Fiebrink — Wekinator; interactive machine learning for music performance.
- MIDI 2.0 Universal MIDI Packet; MIDI Polyphonic Expression.
