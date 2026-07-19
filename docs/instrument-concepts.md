# tonewheel91 — instrument concepts (speculative)

Date: 2026-07-19. A design reference: keyboard-instrument concepts that are not
known to exist as playable instruments, kept so the ideas are not lost. Nothing
here is scheduled, and none of it is implemented. The rule of the exercise: the
keyboard stays as the interface; the novelty lives behind the key. Each concept
is grounded in published science and closes with a prior-art note in the spirit
of the evidence docs — the aim is genuinely new, not overclaimed.

The common thread is a shift in what an instrument *is*: not a generator of
notes, but a bounded, deterministic **dynamical system with state, space, and
time**, played by injecting and shaping energy. The current engine already
carries the seed of this — an always-running generator, deterministic contact
bounce, a stateful drive follower — so these are extrapolations of its own
grain, not a different project.

## Design constraints that keep such instruments playable

Drawn from the engine's existing discipline; any concept below must honor them
before it earns a first render:

- **Deterministic.** Same seed plus same event stream produces bit-identical
  output.
- **Bounded.** Fixed state size, clamped fields, fixed iteration budgets, no
  hidden iterative solvers on the audio path.
- **Audible readout stated up front.** A large internal system is only as
  playable as the part of it that reaches the output; every concept must say how
  it is heard, not assume it will be.
- **A layer, not a religion.** Each is a behavior over a voice, opt-in and
  bounded in the mix.

## The concepts

### 1. Resonant lattice — playing a place

Below the keyboard is a large network of coupled resonators — a virtual
structure of rooms, strings, plates, and channels. A key does not sound a note;
it injects energy into one node, and the output is what happens as that energy
propagates, reflects, and rings through the structure. The instrument has a
geography one learns, the way a fretboard is learned.

- Readout: a fixed set of pickup taps in the structure; energy far from the taps
  is heard only indirectly, so tap placement is part of the design.
- Grounding: coupled-oscillator lattices; energy-stable schemes (finite-
  difference time-domain, Bilbao) and passivity-guaranteed formulations
  (port-Hamiltonian, Falaize & Hélie) keep a driven nonlinear lattice from
  diverging.

### 2. Emergent synchronization — playing tendencies

Each key spins up an oscillator with inertia, friction, and coupling to its
neighbours. The player sets *drives*, not pitches, and the ensemble settles into
its own phase relationships — the coupled-pendulum synchronization Huygens
observed in 1665, taken as an instrument. The music is the process of locking
and unlocking.

- Readout: the summed ensemble; the audible event is the drift into and out of
  phase.
- Grounding: coupled-oscillator synchronization (Huygens; Kuramoto model). The
  engine's rotary/inertia lineage is the nearest existing relative.

### 3. The instrument that ages

A persistent physical state that changes with play: tolerances drift, contacts
wear, couplings loosen. Deterministically seeded, therefore reproducible, but
**irreversible** — two copies of the same instrument diverge over their playing
lives, and the wear pattern is a portable, hashable artifact (a biography as
data) that can be saved, shared, or wiped.

- Readout: practiced material coheres faster and rings fuller than unfamiliar
  material; the difference is audible in the attack, not just the steady state.
- Grounding: Hebbian growth with Oja-style normalization (Oja 1982) plus
  erosion and fatigue; the engine's M7 "wear" milestone is the same idea scoped
  to one milestone rather than to the whole instrument.

### 4. Instrument-space manifold

One physically consistent model whose parameters interpolate between *families*
of instruments — tonewheel, string, reed, bowed plate are points in a single
space. A second control axis moves continuously from one physics to another
mid-note. The instrument is a manifold; the performance is a trajectory through
it.

- Grounding: a single parameterized physical model (waveguide / port-
  Hamiltonian) rather than a bank of presets.

### 5. Self-tuning body

The instrument listens — to the room and to the player — and continuously
re-calibrates its own physical model toward an inferred target: a hummed timbre
morphs its virtual body, or it retunes its resonances to null the room. The body
is a moving, learned target rather than a fixed circuit.

- Grounding: differentiable DSP (Engel et al., ICLR 2020) and grey-box
  calibration; see `research-frontier.md`. The deployable artifact can be a
  frozen calibrated model, which keeps the engine deterministic.

### 6. Edge of chaos — playing the distance to an event

Sustained energy is driven into a nonlinear system held near a bifurcation or a
cascade threshold. The primary control is not loudness but **how close the
system sits to its tipping point**: quiet and ticking at one setting, one
release from a cascade at another. The player commands a regime; the system
finds the events.

- Readout: cascade events are voiced directly (a bounded grain pool) so
  audibility does not depend on where in the system they occur.
- Grounding: self-organized criticality (Bak, Tang & Wiesenfeld 1987; Olami,
  Feder & Christensen 1992) for cascade statistics; Hopf and Duffing dynamics
  for continuous bifurcation control. The distinguishing move is a *governed*
  distance-to-criticality as the played axis, which open-loop density controls
  on granular engines do not expose.

### 7. Continuous-contact keybed — the press as a multi-stage event

The engine already models a key as nine asynchronous contacts closing in
sequence (velocity maps to contact stagger). Taken further, the entire press
trajectory becomes expressive: different depths engage different harmonic taps
at different times, so *how* a key is pressed is a small instrument per key —
not on/off, not simple aftertouch, but a shaped multi-stage physical event under
the finger.

- Grounding: MIDI Polyphonic Expression and continuous-keyboard research; born
  directly from the engine's own contact/bounce model, so part of the substrate
  already exists.

## Prior-art ledger (honesty)

| Concept | Exists elsewhere | Believed new |
| --- | --- | --- |
| Resonant lattice | modal/waveguide/FDTD physical modeling; scanned synthesis | a large driven coupled-resonator field as the whole instrument, played by geographic energy injection, bounded and deterministic |
| Emergent synchronization | Kuramoto/Huygens studies; drone and feedback instruments | coupled inertial oscillators whose *phase-locking* is the played material, not a fixed pitch set |
| The aging instrument | scanned synthesis; media-wear emulations; played-in acoustic folklore | bounded deterministic plasticity inside the sound-producing medium, with wear as a portable hashable artifact |
| Instrument-space manifold | morphing synthesis; multi-model instruments | one physically consistent model with continuous cross-family trajectories, not preset crossfades |
| Self-tuning body | adaptive-mapping tools; auto-EQ | the sound-producing body itself as a live differentiably-calibrated target |
| Edge of chaos | SOC theory and sonification; chaotic oscillators | a governed distance-to-criticality as the primary played axis, bounded and deterministic |
| Continuous-contact keybed | MPE controllers; continuous keyboards | the multi-contact make-sequence of each key as a shaped per-key expressive event |

## The determinism / embeddability thread

The heavier of these (lattice, edge-of-chaos, self-tuning) trade the engine's
constraints — determinism, no-alloc, embeddability — for accuracy or
adaptivity. The under-served combination is a real-time, deterministic,
embeddable realization: any learning or derivation runs offline, the deployed
code stays fixed and bit-exact. A validated, open reference implementation along
that line is a contribution independent of any single new algorithm.

## References

- Bak, Tang, Wiesenfeld — 1987 (self-organized criticality).
- Olami, Feder, Christensen — 1992 (non-conservative cellular earthquake model).
- Oja — 1982 (a simplified neuron model as a principal-component analyzer).
- Huygens — 1665 (synchronization of coupled pendulum clocks); Kuramoto — 1975
  (coupled-oscillator synchronization).
- Bilbao — numerical sound synthesis; finite-difference physical modeling.
- Falaize, Hélie — Applied Sciences 2016 (port-Hamiltonian passive simulation).
- Engel, Hantrakul, Gu, Roberts — ICLR 2020 (differentiable DSP).
- Verplank, Mathews, Shaw — 2000 (scanned synthesis).
- MIDI Polyphonic Expression; MIDI 2.0 Universal MIDI Packet.
