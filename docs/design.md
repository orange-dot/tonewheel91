# tonewheel91 — design notes

Date: 2026-07-16. Status: accepted at bootstrap; constants pinning (M0) is
the first implementation step.

## Goal

A component-modeled electromechanical tonewheel organ as a standalone
instrument: playable live over MIDI from day one, embeddable in a hardware
instrument later. Judged by ear against canonical recordings of the
instrument family (kept outside the repo).

## Architecture: two layers

**Core** (`src/`) — freestanding C23. No OS calls, no allocation, no I/O, no
libm: per-sample math uses repo-local kernels (polynomial sine over a phase
accumulator, tanh-shaped saturator, a small exp for init-time smoothing
coefficients). All state lives in caller-provided structs, fixed capacity for
sample rates up to 192 kHz, f32 throughout, structure-of-arrays layout so the
91-wide banks auto-vectorize. Gain smoothers snap to target below an epsilon
so decaying banks never enter denormal territory. The MIDI byte parser
(running status, note on/off, program change, CC) is part of the core: on
Linux it is fed by ALSA rawmidi, on an instrument build by a UART — same
bytes, same parser. Velocity is parsed but never scales loudness (contact
closure is binary); it maps to contact-stagger timing only.

**Drivers** (`driver/`) — the Linux live driver and the offline renderer.
Live driver: one thread, synchronous:

    read pending MIDI bytes (nonblocking) -> parser -> organ state
    render one period -> snd_pcm_writei (blocking; the loop's clock)

No second thread, no ring buffer; ALSA's period/buffer machinery is all the
buffering there is. Event timing quantizes to one period (2.7 ms at 128
frames / 48 kHz). The offline renderer replays a small text event script and
writes WAV; it is the determinism/test twin of the live path, not a product.

## Dependency policy

- Core: no third-party code. What one would import there is either the
  project's own subject matter (oscillators, filters, delay lines, shapers)
  or smaller to write than to vendor (WAV writer, FNV-64, xorshift RNG,
  later a radix-2 FFT for offline metrics).
- Drivers: platform libraries are legitimate when a real capability demands
  them. Today that is exactly one: `libasound`.
- libc, libm-in-drivers, and POSIX getopt are standard, not dependencies.
- Litmus test: would we be replacing code that is the point of the project,
  or plumbing? Plumbing may be taken ready-made; the point we write.

## Signal chain

    91-wheel generator (always running, constant cost)
      -> key contacts / busbar folding (taper, drawbar gains, robbing, click)
      -> percussion join
      -> vibrato/chorus scanner (dispersive tapped line)
      -> swell (expression)
      -> preamp drive (stateful bias stage)
      -> rotary speaker (crossover, two rotors with inertia, Doppler, AM,
         cabinet reflections, stereo pickups)

Mono until the rotary stage. Swell sits before drive on purpose: closing the
pedal also cleans the drive up, as players expect. Each stage is a struct
plus a render function; interfaces freeze early so a stage's internals can
move behavioral -> stateful -> circuit-level without touching neighbours.
While the organ runs, all 91 wheels advance and render every frame; behavior
differences are gain-gated, never per-wheel branch-gated.

## Model depth doctrine

Three physical domains, deliberately modeled at different depths:

- **Mechanics — physics from the start.** Gear-train frequencies and
  shared-wheel phase coherence; contacts switching a live signal through
  deterministic bounce; the scanner sweeping real taps; rotors with
  per-direction inertia. One planned refinement: velocity -> contact stagger
  (the nine contacts engage over ~0-15 ms on a slow press).
- **Electronics — calibrated behavior, one named deep-modeling candidate.**
  Taper/robbing/drawbar-step tables; static per-wheel level and harmonic
  profiles. The tube stages (preamp, rotary amp) get a stateful
  bias-excursion model — an envelope follower shifting the shaper's
  operating point, plus coupling-cap highpass — the cheap middle step that
  carries most of the audible difference from a bare waveshaper. A full
  wave-digital triode stage is the named upgrade if the ear demands it. A
  solved busbar network is explicitly last-or-never; the robbing lookup
  stands until proven insufficient.
- **Acoustics — physics where it pays, data where it pays more.** Doppler as
  fractional delay from horn geometry; AM from directivity lobes (one
  acoustic branch per revolution); the cabinet as a short set of early
  reflections. Upgrades here mean better measured data in the same
  structure, not heavier simulation.

Leakage is structured, not uniform: a wheel's bleed is dominated by its
neighbours in the generator bin layout, which is not the musical order.

## The deviation ledger

In principle the generator is an additive synthesizer; executed ideally it
would sound sterile. The instrument's identity lives in the machine's
deviations from that ideal, so each is tracked as a first-class model
feature with a source and an owning milestone:

1. **Non-sine wheel EMF** — tooth profile and magnetic-circuit
   nonlinearity add harmonic content and asymmetry, per wheel and
   IMD-free (constants sec. 12) — M7; the shared-preamp IMD stays a
   separate stage (M5).
2. **Motion AM ("shimmer")** — shaft eccentricity and bearing play
   modulate each wheel at its own rotation rate (constants sec. 12) — M7.
3. **Leakage/crosstalk** — magnetic and capacitive bleed follows the
   physical bin/shaft layout, not the musical scale (constants sec. 13) —
   M7; the `leak` frame slot exists since M1.
4. **Key click** — nine asynchronous contacts switching live signals
   (constants sec. 7) — **landed at M2**.
5. **Foldback** — the finite wheel set borrows top/bottom harmonics,
   bending timbre along the compass (constants sec. 4) — **landed at
   M1**.
6. **Scanner vibrato** — a resonant LC ladder scanned by linear
   crossfade: lowpass edge, moving ripple, comb notches; not an LFO on a
   delay line (constants sec. 9) — **landed at M4**.

`wear = 0` keeps the idealized reference for tests; the shipped default is
nonzero because tolerance effects exist on a factory-new unit.

## Control surface

Organ: `registration` (nine digits, one atomic vector, leftmost = the
sub-octave drawbar), `percussion` (off | second/third x fast/slow x
soft/normal), `vibrato` (off | V1..V3 | C1..C3), `drive`, `wear`. Rotary:
`mode` (bypass | chorale | tremolo | brake), `balance`, `width`, `drive`.

MIDI: notes 36..=96 (61-key compass; outside notes ignored and counted),
velocity -> contact stagger only, CC11 swell, nine CCs for the drawbars
(nine-fader surfaces map 1:1), one control for rotary speed as a live
performance switch. The exact CC map is pinned at M2.

## Determinism

The same input (script or captured MIDI bytes) renders bit-identical output
on the same binary: fixed-seed RNG advanced per event ordinal (contact
bounce), fixed iteration budgets, clamped and sanitized control inputs.
Never `-ffast-math`; `-ffp-contract=off` for cross-build stability. Tests
assert two-run FNV-64 signature equality.

## Targets and budget

Two targets, in order:

1. **Now — the development host.** x86-64 Linux with a USB audio interface;
   the reference rig is a Yamaha AG03 (USB Audio Class 2: S32_LE native,
   2 ch, 44.1-192 kHz, async endpoint, 125 us packets). The driver opens
   `hw:` directly and converts f32 -> S32_LE itself — no plug layer.
   Defaults: 48 kHz, 128-frame period, 3-period buffer (~8 ms plus ~1-2 ms
   USB). MIDI arrives over ALSA rawmidi from whatever controller is plugged
   in; `snd-virmidi` is the keyboard-less test path. (The AG03's own USB
   MIDI port is the mixer's DSP-control port, not a DIN bridge — notes come
   from a real controller.) On a PipeWire desktop, if the `hw:` open returns
   EBUSY, release the card's node (wireplumber) first.

2. **Later — the instrument build.** A Linux SBC, reference Raspberry Pi 3B
   (4x Cortex-A53 @ 1.2 GHz, aarch64 OS). The SBC constraints are audio I/O
   (an I2S DAC hat or a USB interface; the onboard jack is not
   instrument-grade) and scheduling hygiene (SCHED_FIFO; a stock kernel
   usually holds at 3 x 128-frame buffering).

Estimated core load is ~100-150 Mflop/s f32 at 48 kHz: negligible on the
host, and comfortably under one A53 core (~0.5-1 Gflop/s scalar, before NEON
auto-vectorization of the 91-wide banks) on the SBC. Default rate: 48 kHz.

## Milestones

- **M0 constants**: `docs/constants.md` — every number pinned with a source
  or explicitly marked open with an owning milestone. Gear table (twelve
  driver/driven pairs -> wheel 1..91 frequencies, deviation-from-ET table),
  foldback rule (taps clamp into wheel range [13, 91] by octave steps),
  drawbar footage offsets (-12, +7, 0, +12, +19, +24, +28, +31, +36),
  ~3 dB/step curve, taper and robbing targets, click bounce (<= 3 toggles
  in 0-2 ms) and smoothing range, percussion decays/levels and quirks
  (single-trigger, re-arm on all-keys-up, ninth-drawbar theft, pre-drawbar
  tap), scanner (~6.9 Hz scan, ~1 ms total line delay, section/tap counts,
  V/C spans and mixes), rotary (crossover ~800 Hz, per-rotor chorale and
  tremolo speeds, horn ~1 s vs drum several-seconds inertia, Doppler
  0.3-0.9 ms swing, AM depths), leakage/hum levels.
- **M1 generator**: 91 wheels + foldback + registration, offline render.
  Founding exhibit: phase-coherence A/B against two independently detuned
  sines (shared-wheel reinforcement vs chorusing), audible and tabled —
  the inventor ran the same comparison in the lab (constants sec. 3).
- **M2 first playable organ**: MIDI parser + ALSA live driver + contacts,
  click, robbing; drawbar CCs and swell mapped; xrun recovery and panic.
- **M3 percussion**: truth-table tested trigger logic.
- **M4 scanner**: the real 18-section ladder from sourced component values
  (lowpass edge, moving ripple, comb notches for free) + triangular
  crossfade scanner; exhibit: dispersion A/B against a plain
  modulated-delay chorus.
- **M5 drive**: stateful bias stage from day one, not bare tanh.
- **M6 rotary**: rotors + inertia + Doppler + AM first; cabinet, stereo
  field, and amp drive interaction second.
- **M7 wear**: structured leakage, hum, motion AM, wheel/pickup color,
  per-wheel character behind one knob; `wear = 0` is the idealized
  reference, bit-identical to pre-M7 renders; shipped default nonzero.
- **M8 (optional) metrics**: small FFT, registration recovery, beat-rate
  checks against the gear table.

## References

- Smith, Serafin, Abel, Berners — DAFx 2002 (Doppler simulation of the
  rotary speaker).
- Pekonen, Pihlajamaki, Valimaki — DAFx 2011 (computationally efficient
  tonewheel-organ synthesis).
- Werner, Dunkel, Germain — DAFx 2016 (wave-digital model of the
  vibrato/chorus line box and scanner; component values and tap tables).
- Werner, Abel — Applied Sciences 2016 (modal-processor study: wheel
  waveforms, pickup nonlinearity, pseudo-harmonic cent errors).
- Muenster, Pfeiffle — ISMA 2019 (measurements and FEM of the
  mechano-electrical tone generator on a 1938 unit).
- US Patents 1,956,350 (founding, 1934); 2,159,505 (chorus generators,
  1939); 2,342,338 (percussion concept, 1944); 2,382,413 (scanner
  vibrato, 1945) — primary sources, cited by number.
- Service-documentation-derived public constant tables (gear ratios, taper
  and busbar values); concrete sources pinned in `docs/constants.md` at M0.

## Open items

- Exact MIDI CC map (M2).
- I2S hat vs USB interface for the instrument build (decided at hardware
  time; irrelevant to the code).
