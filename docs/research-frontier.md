# tonewheel91 — research frontier and upgrade candidates

Date: 2026-07-18. A reference catalog of published synthesis frameworks that sit
above the deliberately-chosen model depths of the current engine (see
`design.md`, "Model depth doctrine"). Each entry names the framework, its
primary literature, the stage it would attach to, and the cost it trades against
the engine's determinism and embeddability constraints. Nothing here is
scheduled — this is a map, not a backlog. References are cited
author-venue-year; author lists are lead-author where a full list is not
confirmed here.

## Where the engine sits today

Three physical domains are modeled at intentionally different depths: mechanics
from physics, electronics as calibrated behavior, acoustics as
physics-where-it-pays. The nonlinear stages (preamp drive, rotary amp) use a
behavioral bias-excursion follower rather than a solved circuit; the scanner is
a trapezoidal (bilinear) nodal solve of the real LC ladder; constants are pinned
by hand from sources, with the by-ear values left open ([FOLK] / [decision]).
The frameworks below are the published alternatives to those choices.

## 1. Energy-based circuit modeling — port-Hamiltonian systems (PHS)

A continuous-time, power-balanced state-space formulation whose discrete schemes
preserve a discrete power balance, so the simulation is passive — and therefore
stable — by construction, for linear and nonlinear circuits alike. It is the
rigorous alternative to the behavioral nonlinear stages the engine now
approximates. Recent work extends it to identification from measurements and to
port-Hamiltonian neural networks.

- Falaize, Hélie — Applied Sciences 2016 (passive-guaranteed simulation of
  analog audio circuits).
- Hélie et al. — DAFx 2021 (identification of nonlinear circuits as
  port-Hamiltonian systems).
- Port-Hamiltonian neural networks for nonlinear string dynamics — 2026.

Cost: heavier per-sample solve; passivity guarantees are the payoff. Real-time
embeddable implementations are rare — the open gap.
Attach: `drive.c` (preamp), rotary amp stage.

## 2. Nonlinear wave digital filters (WDF)

Modular circuit discretization; the reference model for the vibrato/chorus line
box already uses it, and the engine's scanner constants come from that work. The
active research problem is circuits with several coupled nonlinearities,
addressed by R-type adaptors — an alternative to PHS for a circuit-true preamp
or amp.

- Werner, Dunkel, Germain — DAFx 2016 (line-box / scanner WDF; source of the
  scanner constants).
- Werner, Bernardini, Sarti — multiple-nonlinearity WDF via R-type adaptors.

Cost: adaptor derivation is intricate once more than one nonlinearity couples.
Attach: `drive.c`, rotary amp; an alternative derivation of the `scanner.c`
ladder.

## 3. Antiderivative antialiasing (ADAA)

Alias reduction for memoryless and stateful nonlinearities using antiderivatives
of the nonlinear map, without — or with only low — oversampling. Extended to
arbitrary-order IIR kernels and to nonlinear WDF structures.

- Parker, Zavalishin, Le Bivic — 2016 (antiderivative antialiasing for
  memoryless nonlinearities).
- Bilbao et al. — 2016 (aliasing reduction via continuous-time convolution).
- Arbitrary-order IIR ADAA — 2021; interpolation filters for ADAA — DAFx 2024.

Cost: modest — first-order ADAA is cheap. Trades exact-bypass simplicity for one
extra state on the shaper.
Attach: `tw_sat` / `drive.c` saturator (currently an odd-rational clamp).

## 4. Differentiable DSP and grey-box calibration

The model topology is fixed and known; parameters — and, in grey-box variants,
the nonlinear maps themselves — are fit by gradient descent against recordings.
The relevant frontier is auto-calibration of a component model to a measured
unit, which is precisely the by-ear-open verdict the evidence docs defer (drive
taper and bias depth, rotor speeds, AM depths, mic geometry).

- Engel, Hantrakul, Gu, Roberts — ICLR 2020 (differentiable DSP).
- Comunità et al. — 2026 (NablAFx: differentiable black-box and grey-box
  audio-effect modeling).
- DDSP guitar amp — 2024 (interpretable amplifier modeling).
- Review of differentiable DSP for music and speech — Frontiers in Signal
  Processing 2023.

Cost: training is offline and non-deterministic, but the deployable artifact can
be a frozen, calibrated set of constants — which preserves the engine's
determinism.
Attach: the constants pipeline (`docs/constants.md`); any stage carrying [FOLK]
/ [decision] / by-ear-open values.

## 5. Neural ordinary differential equations for physical modeling

Nonlinear instrument dynamics learned as continuous-time ODEs and integrated at
render time. Early-stage; no real-time or embedded implementations.

- Neural-ODE physical-modeling synthesis — DAFx 2025.

Cost: high; research-stage. Listed for completeness.

## 6. Large-scale and measured acoustics

Full radiation and room modeling by finite-difference time-domain schemes
(energy-stable, GPU), or measured directivity impulse responses, as alternatives
to the engine's early-reflection plus directivity-AM rotary cabinet.

- Bilbao — FDTD physical modeling of instruments and rooms.

Cost: FDTD is not real-time on modest hardware; measured directivity IRs are a
lighter middle path.
Attach: `rotary.c` cabinet and stereo field.

## 7. Neural and generative synthesis (out of scope, noted)

Diffusion, flow-matching, and codec-token language models generate audio from
learned priors, and real-time neural synthesizers (VAE / adversarial) exist.
This paradigm is stochastic and model-heavy, orthogonal to a deterministic,
freestanding, bit-exact component model, and is recorded here only to mark the
boundary.

## The determinism / embeddability tension

Most frameworks above trade the engine's constraints — determinism,
no-alloc / no-libm, embeddability — for accuracy or adaptivity, which is why
production instruments and this engine use the lighter behavioral forms. The
under-served combination is a real-time, deterministic, embeddable
implementation of an energy-based (PHS / WDF) or differentiably-calibrated
stage: the calibration or derivation runs offline, the deployed C stays fixed
and bit-exact. A validated, open reference implementation along that line is a
contribution independent of any single new algorithm, and fits the engine's
existing determinism discipline without weakening it.

## References

Author-venue-year; titles paraphrased where a verbatim title carries a protected
name.

- Bilbao — numerical sound synthesis; FDTD instrument and room modeling.
- Bilbao, Esqueda, Parker, Välimäki — 2016 (aliasing reduction, continuous-time
  convolution).
- Comunità et al. — 2026 (NablAFx differentiable audio-effect framework).
- Engel, Hantrakul, Gu, Roberts — ICLR 2020 (differentiable DSP).
- Falaize, Hélie — Applied Sciences 2016 (port-Hamiltonian passive simulation).
- Hélie et al. — DAFx 2021 (port-Hamiltonian identification of nonlinear
  circuits).
- Parker, Zavalishin, Le Bivic — 2016 (antiderivative antialiasing).
- Werner, Dunkel, Germain — DAFx 2016 (vibrato/chorus line-box WDF).
- Werner, Bernardini, Sarti — multiple-nonlinearity WDF (R-type adaptors).
- Frontiers in Signal Processing — 2023 (review of differentiable DSP for music
  and speech).
- DAFx 2025 (neural-ODE physical-modeling synthesis).
