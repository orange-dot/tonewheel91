# tonewheel91 — control plane hardware options

Date: 2026-07-19. A decision reference for a gestural control surface — the
"left-hand control plane" — that gives a keyboard instrument many continuous
parameters at once, which the dynamical/field instruments in
`instrument-concepts.md` need and a keyboard alone cannot supply. This catalogs
both **worn** and **contactless** sensing, their DIY-buildability, the design
problems common to all of them, and how a controller attaches to the engine.
Nothing here is scheduled — it is a menu to choose from later. Pairs with
`gesture-control.md` (the concept) and `instrument-concepts.md` (the
instruments).

## Requirements shaping the menu

- **Many simultaneous continuous axes**, non-quantized: coupling, drive, heat,
  regime/criticality, listening position, and so on.
- **Operator preference: nothing worn that must be donned and doffed.** This
  favors the contactless family; the worn family is catalogued for completeness.
- **Sensor-agnostic, real-time integration** consistent with the engine's
  bounded, deterministic-friendly discipline.
- **Builder profile:** electronics-capable (soldering, EE background), time
  available, cost not the binding constraint — DIY builds are on the table.

## The two families at a glance

| | Worn (family A) | Contactless (family B) |
| --- | --- | --- |
| On the hand/arm | yes — remove when unused | no — sensor fixed to the instrument |
| Proprioceptive anchor | present (you feel your hand) | absent — must be supplied by feedback/frame |
| Best DOF richness | glove: ~10–15; EMG: few + intent | optical: ~20+; radar/ToF/cap: ~3–8 |
| DIY fit | high (glove) | high (radar/ToF/cap), buy-only (optical) |
| Don/doff friction | yes (the disqualifier here) | none — hand enters/leaves a zone |

## A. Worn options (something on the hand/arm)

Catalogued for completeness; each must be removed when the plane is not in use,
which is the operator's stated objection.

1. **Data glove / instrumented hand** — flex sensor per finger + an IMU, plus
   optional fingertip force sensors. **Full DIY.** ~10–15 DOF. Gives a real
   proprioceptive anchor and is robust and cheap. Worn; needs donning; tethered
   or wireless.
   - Parts: MCU with good ADC + native USB-MIDI (Teensy 4.x, RP2040); flex
     sensors (Spectra Symbol) or DIY velostat; IMU with onboard fusion
     (BNO085/BNO055 — returns a clean orientation, saving the fusion work);
     force-sensing resistors (FSR-402).
2. **Surface-EMG wristband (sEMG)** — reads motor-neuron intent, potentially
   per-finger. Worn on the forearm. **DIY-hard:** a single channel is easy
   (MyoWare-class module → grip/effort), but per-finger decoding needs
   multi-channel electrodes plus signal processing/ML and is a research problem
   in its own right. Low fatigue (no hand held in a volume), but worn and
   per-user calibrated.
   - Parts: MyoWare 2.0 (single/few channel); OpenBCI Ganglion/Cyton
     (multi-channel); instrumentation-amp front end (INA-class) for a custom
     board.
3. **Finger/hand IMU rings** — minimal worn mass, per-finger orientation only;
   still worn, and prone to drift without a fixed reference.

## B. Contactless options (nothing on the hand) — the preferred family

Sensor fixed to the instrument; the bare hand enters a sensing zone.

1. **Optical hand tracking** (Ultraleap / Leap Motion Controller 2, or a camera
   with a pose model such as MediaPipe Hands). **Buy, don't build.** The richest
   bare-hand data — full skeletal pose, per-finger joints, ~20+ DOF. It is the
   *only* contactless technology that delivers true per-finger tracking with
   nothing worn. Costs: occlusion (fingers hidden behind the hand), lighting
   sensitivity (Leap supplies its own IR), ~10–30 ms latency, a bounded
   interaction volume, and a black-box SDK — so the EE effort moves to the
   mount, the frame, and the mapping rather than the sensor.
2. **Radar, 60 GHz** (Infineon BGT60TR13C, the Soli lineage; or Acconeer A121
   pulsed coherent radar). **Module plus integration.** ~4–6 rich *dynamic*
   features: range, radial velocity, micro-motion energy, and angle with
   multiple receive antennas. Robust to light, small, low power, truly
   contactless — the closest thing to a multi-DOF theremin successor, and a real
   EE/DSP project. It does not give clean per-finger coordinates, and the
   SDK/DSP ramp is the steepest here.
3. **Multizone time-of-flight** (ST VL53L5CX / VL53L7CX 8×8; tile several for a
   wider or denser field). **DIY** — an I²C part on your own board. ~4–8 DOF:
   hand x/y/z, tilt, and openness read from a low-resolution depth image with no
   camera and little compute. Cheap, robust to ambient light, and the fastest
   path to a first working signal; limited by spatial resolution and
   field-of-view/range.
4. **Capacitive electrode array** (a heterodyne LC front end in the theremin
   tradition, or charge-transfer sensing; MPR121 for turnkey digital cap-sense).
   **Full DIY analog.** ~3–6 DOF from per-electrode proximity → x/y/z, tilt,
   spread. The most theremin-authentic and the cheapest, and pure EE craft.
   Costs: drift, inter-electrode coupling, mains-hum (50 Hz here) pickup,
   environmental sensitivity, and hand-shape ambiguity.
5. **IR-proximity or ultrasonic arrays** (SHARP IR, HC-SR04-class) — crude,
   cheap, low-resolution; noted only as a budget/experiment tier.

## The reframe: for a field instrument, aggregate axes beat per-finger

For the dynamical/field instruments this control plane targets — playing drive,
heat, regime/criticality, and listening position — roughly five to eight rich
*aggregate* axes are more playable than twenty finger coordinates that cannot be
controlled independently anyway. So the DIY-contactless path (radar / ToF /
capacitive) is not a downgrade from optical per-finger tracking; for these
instruments it is arguably the better fit. Per-finger sensing (optical) earns
its cost only if the instrument genuinely needs individuated fingers.

## Design problems shared by every option

Independent of the sensor, and the real work:

1. **Mapping is the risk, not the electronics.** Raw DOF mapped one-to-one onto
   parameters is unplayable. The map from a few natural gestures to many
   parameters should be a *learned low-to-high manifold* (interactive machine
   learning — the Wekinator family), deployed as a fixed function so determinism
   holds. De-risk this with a bought sensor before soldering anything.
2. **The anchor problem** (worse for contactless; the glove was hiding it).
   Free-space bare-hand gesture has no proprioceptive reference — the reason
   theremins are hard. Substitutes: mandatory audible **and** visual state
   feedback (a read-only field view); a physical, non-worn **frame / arch /
   rest-bar** by the keyboard for spatial reference; and a clearly bounded
   interaction volume.
3. **Come-and-go / clutch** (the operator's explicit requirement). Because
   nothing is worn and the hand enters and leaves, the system needs an explicit
   **arm → hold → release**: values latch (or ease back to a rest state) when
   the hand exits, and re-engage cleanly on return. The clutch is a footswitch
   (the foot is free) or a threshold plane at the zone boundary. This reuses the
   engine's existing *armed momentary gate* pattern rather than inventing new
   state.
4. **Latency budget.** Target sensor-to-sound under roughly 10–15 ms.
   IMU/flex/ToF/capacitive over I²C plus native USB hold this comfortably; radar
   and optical add frame/SDK latency — budget it end to end against the already
   low-latency audio path.
5. **Transport and resolution.** 7-bit MIDI CC is too coarse for smooth field
   control. Use high resolution: MIDI 2.0 Universal MIDI Packet (the Linux
   kernel carries UMP; native UMP on a hobby MCU is still emerging via TinyUSB),
   or — since the engine is ours — a custom high-rate USB link to a laptop shim
   that speaks the engine's control path directly. Do not let the 7-bit legacy
   constrain the prototype; 14-bit CC / NRPN is a fallback.
6. **Calibration and noise.** Flex, capacitive, and EMG all drift and are noisy
   → per-session calibration plus smoothing. The **One-Euro filter** (Casiez,
   Roussel & Vogel, 2012) is the standard for interactive gesture: low latency
   and low jitter together. For capacitive/EMG, the usual analog hygiene —
   shielding, a solid ground reference, 50 Hz mains rejection, and the
   body-as-antenna effect.

## Suggested build phases

- **Phase 0 — de-risk the mapping without soldering.** Drive the engine from a
  bought optical sensor (Leap 2) or a webcam + pose model, and answer "is this
  playable?" before building hardware. The mapping is the risk; prove it first.
- **Phase 1 — first DIY contactless rig.** A "field frame" beside the keyboard,
  instrumented with capacitive electrodes or one-to-two multizone ToF sensors,
  plus a footswitch clutch. MCU → One-Euro filter → high-rate USB → laptop shim
  → engine. Bare hand in and out, nothing worn.
- **Phase 2 — the frontier sensor.** Add 60 GHz radar (or a ToF/radar fusion) as
  the contactless micro-gesture and distance axis.
- **Phase 3 — stretch, only if per-finger is truly needed.** Optical for genuine
  per-finger pose, or multi-channel EMG as a worn research branch.

## Integration with the engine

- The sensor is only a control source; abstract it behind the control link so
  the engine stays sensor-agnostic.
- The pattern already exists in the runtime lineage: a virtual MIDI source that
  changes nothing in the engine. The hardware controller is its physical
  analogue — a laptop shim reads the sensor and speaks the engine's control path
  (a controller-profile binding / an engine command), never a bespoke runtime
  hook.
- The clutch maps onto the engine's **armed momentary gate**.
- A recorded gesture path is an event stream and stays reproducible (the
  offline/scenario class); live gesture input remains the non-deterministic
  session class. The determinism claim belongs only to the recorded class.

## Prior-art / honesty ledger

| Approach | Musical precedent | — |
| --- | --- | --- |
| Capacitive free-space | the theremin (Léon Theremin, 1920s) | one hand, ~1 DOF; the array extends it |
| Optical hand tracking | Leap-Motion instruments | per-finger, bare-hand, but bought |
| Radar micro-gesture | Soli experiments | robust dynamic features, not per-finger |
| Worn glove | MiMu gloves; STEIM "The Hands" (Waisvisz, 1984) | rich and anchored, but worn |
| EMG | Myo/CTRL-labs instruments | intent sensing, worn, per-finger is research |

What would be new is not the controller but the coupling: a bare-hand,
contactless gesture driving the **regime, material state, and listening
position** of a bounded, deterministic field instrument, mediated by an explicit
clutch — see `gesture-control.md` and `instrument-concepts.md`. Continuous
multi-DOF gesture control itself is well explored and is not claimed as novel.

## References

- Lien et al. — SIGGRAPH 2016 (Soli: millimeter-wave radar for fine gesture).
- Ultraleap / Leap Motion; MediaPipe Hands (optical pose).
- ST VL53L5CX / VL53L7CX multizone time-of-flight datasheets.
- Infineon BGT60TR13C; Acconeer A121 (60 GHz / pulsed coherent radar).
- NXP/Freescale MPR121 (capacitive touch/proximity controller).
- MyoWare 2.0; OpenBCI (surface EMG front ends).
- Casiez, Roussel, Vogel — CHI 2012 (the One-Euro filter).
- Fiebrink — Wekinator (interactive machine learning for performance mapping).
- MIDI 2.0 Universal MIDI Packet; MIDI Polyphonic Expression.
- Theremin — 1920s (heterodyne capacitive sensing); Waisvisz — STEIM "The
  Hands", 1984; MiMu gloves (Heap et al.).
