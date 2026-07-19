# M7 evidence — wear, the structured deviations

Date: 2026-07-19. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, one clean-build run of each binary;
all numbers below are from that run.

## Changes

The design.md deviation ledger's three remaining generator items — non-sine
wheel EMF, motion AM, structured leakage — plus the level profile, pickup
nonlinearity, and mains hum, all behind the one `wear` knob; M7-1..M7-8 in
one change. `wear = 0` is the idealized reference and reproduces every
pre-M7 render bit-for-bit (asserted, below); the shipped default is 0.2
because tolerance effects exist on a factory-new unit (design.md).

- `tonewheel.h` — `tw_generator` grows the deviation banks (level spread,
  tooth depths, rotation accumulators + AM depths, pickup coefficients,
  hum oscillator, bleed weights) and `tw_generator_set_wear`; the organ
  grows `tw_organ_set_wear` and the `TW_WEAR_DEFAULT` constant; splitmix64
  moves into the shared-helpers block (organ.c's copy now calls it —
  bit-identical algorithm, verified by the unchanged signatures).
- `src/generator.c` — every constant from sections 11.1/12.1/13.1:
  - **Level profile** (M7-1, sec 11.1): per-wheel spread +-0.12 at wear 1
    plus conditioning-zone trims 0/-0.02/+0.02, from one fixed-seed
    splitmix64 draw per wheel (21-bit fields; seed `0x7765617274773931`).
    `set_wear` re-derives the draws, so the knob is stateless and
    `wear = 0` restores exact flat.
  - **Toothing** (M7-2, sec 12.1): exact 2nd/3rd partials on the wheel's
    own phase accumulator (per wheel, IMD-free, band-bounded at
    3 x wheel 91 = 17.8 kHz), depths 0.015/0.03 x 4/teeth — stronger for
    the cornered low registers [ISMA19].
  - **Motion AM** (M7-3, sec 12.1): one sinusoidal AM per wheel at its
    own rotation rate `f_wheel/teeth` (16.3..30.9 Hz), depth = draw x
    0.05 max, random start angle; the accumulator always advances, so a
    wear change never breaks the rejoin discipline.
  - **Pickup nonlinearity** (M7-4, sec 12.1): `(1 - exp(-alpha x))/alpha`
    at alpha = wear x 0.3 [AS16], as its cubic series (dropped quartic
    ~-59 dB, bandwidth bounded, no aliasing at any supported rate); the
    series' static term is subtracted at the source (the matching
    transformer passes no DC). Asymmetric per wheel — harmonic
    distortion only; IMD still enters first at the M5 preamp.
  - **Structured leakage** (M7-5, sec 13.1): the bleed bus weights from
    the bin/shaft matrix contraction — shaft partner 3e-3, bin mate
    8e-4 per neighbour at wear 1 — yielding the three structural
    classes (full-bin 4.6e-3; the r < 5 shaft pairs 3.8e-3; blank-partner
    wheels 37..41 at 1.6e-3). The bus taps the conditioned wire signal
    (the per-pickup filters shape what leaks [ISMA19]).
  - **Mains hum** (M7-6, sec 1): 60 Hz on the bleed bus at 1e-3 x wear.
- `src/organ.c` (M7-5/M7-7) — `frame.leak` joins the keyed+percussion sum
  ahead of the scanner line and swell (crosstalk couples at the wheel
  wiring); `tw_organ_set_wear` forwards to the generator;
  `tw_organ_init` ships `TW_WEAR_DEFAULT = 0.2`.
- `test/test.c` (M7-7) — the four scripted renders now carry **pinned
  pre-M7 FNV-64 baselines** (captured on the M6 tree the day M7 landed)
  and must reproduce them at `wear = 0`; mid-note `wear -> 0` must
  rejoin the never-worn render bit-exactly; the shipped default must
  differ. Organ tests whose subject is contact machinery, compass
  silence, panic, stagger release, the scanner bass split, or
  drive-stage silence pin `wear = 0` with a one-line rationale — the
  shipped default now carries the sec 13 idle floor by design.
- `driver/exhibit_*.c` — the seven historical exhibits pin `wear = 0`
  (their recorded signatures predate the wear stage); `render_midi` and
  the live driver deliberately do **not** pin it — they ship the
  default, which is the point of the docs/renders.md cross-milestone
  A/B log.
- `driver/exhibit_wear.c` + `Makefile` (M7-8) — the M7 exhibit, below.

## Test result

    8911 checks, 0 failures

(M6 baseline was 7764.) The wear = 0 identity contract is now asserted
three ways:

- the four scripted renders reproduce their pre-M7 signatures
  bit-for-bit at wear 0 — `f0b4c7c3f7705480` (organ script),
  `b01485a1702721a3` (vibrato), `730ac52bff1f129d` (drive),
  `f1d10bfe4b6cab4d` (rotary) — pinned as constants in test.c;
- every historical exhibit still prints its recorded signature:
  exhibit_phase `b71cbb09b1ecd064` / `012442c11623cab8` /
  `96b17679450dec1b` (m1), exhibit_contacts `3f25ffe656644fd6` (m3),
  exhibit_taper `0565b81fd82c84a7` (m3), exhibit_percussion
  `69ae12dcd88cd2ea` (m3), exhibit_scanner `079088b2a0394053` (m4),
  exhibit_drive `c5c2f6ce4161ca74` and `bf83b19c31cd7b74` (m5),
  exhibit_rotary `00eb4c9bf80cb408` (m6) — all verdicts PASS;
- the M7 exhibit re-renders the M6 transition passage at wear 0 and
  asserts its FNV equals the m6-evidence baseline (below).

## Exhibit result

    identity: M6 transition at wear 0: FNV64 00eb4c9bf80cb408
      == the m6-evidence baseline
    default (0.20) transition: FNV64 657e8b8d6996ed40 (two runs identical)
    wheel 13 (4 teeth): H2/H1 0.0748 (alpha/4 + the 0.015 tooth term in
      quadrature), H3/H1 0.0259 (the 0.03 tooth term net of the pickup
      cubic)
    pickup alpha, wheel 73 (toothing ~1e-3 there): H2/H1 0.0740
      (alpha/4 says ~0.074)
    motion AM, wheel 46: depth 0.0342 at 27.5 rev/s (draw x 0.05 max)
    idle floor at wear 1: rms 0.0309 (-30.2 dB; sec 13.1 aims ~-30)
      bleed lines: wheel 20 (full bin) 4.91e-03 vs wheel 39 (blank
      partner) 1.70e-03 -- the bin layout, audible
      mains hum: 60 Hz line 1.01e-03 (pinned 1e-3 at wear 1)
    idle floor at the shipped default: rms 0.00596 (-44.5 dB)

The wheel-13 row is the two per-wheel nonlinearities superposed, as the
architecture predicts: the pickup's alpha/4 H2 rides in quadrature with
the 0.015 tooth term (measured 0.0748 vs sqrt(0.075^2 + 0.015^2) =
0.0765), and the tooth's 0.03 H3 loses the pickup cubic's 0.00375 on
the same axis (measured 0.0259 vs 0.0263). Wheel 73 isolates alpha
(its toothing is 4/64 scaled, ~1e-3). The bleed-line pair is the
sec 13 claim made audible: wheels 20 and 39 are four semitones apart
but sit in different generator compartments — 20 keeps a shaft partner
(full class, 4.9e-3 measured with its level trim) while 39 runs blank
(1.7e-3) — bleed follows the bin layout, not the musical order.

## A/B renders

    build/m7_wear_off.wav      one chord (16'+5-1/3'+8'+4', bare organ),
                               wear 0 — the idealized additive reference
    build/m7_wear_default.wav  the same chord at the shipped 0.2
    build/m7_wear_full.wav     the same chord at wear 1: level spread,
                               tooth harmonics, shimmer beating, pickup
                               asymmetry, and the floor ringing in the
                               1.5 s release tail
    build/m7_idle_floor.wav    the sec 13 evidence render: an idle organ
                               at wear 1, nothing keyed, +24 dB makeup —
                               91 wheels bleeding through the bin/shaft
                               matrix under a 60 Hz line

## The [FOLK] caveat and the by-ear open items

**Every depth in this milestone except alpha is a working default.**
The mechanisms are sourced ([ISMA19] wobble and toothing, [AS16] pickup
curve and crosstalk audibility, [DAFx11] AM form and physical-order
leakage, [SM] bin/shaft geometry); the magnitudes mostly are not:

- level spread +-0.12 and the zone trims 0/-0.02/+0.02 [FOLK];
- tooth anchors 0.015/0.03 at four teeth (the 4/teeth shape follows
  [ISMA19]'s low-register observation; the anchors do not) [FOLK];
- motion-AM max 0.05 [FOLK]; per-wheel spread is a draw, not a
  measurement;
- alpha = 0.3 is measured [AS16], but scaling it by wear is [decision];
- bleed strengths 3e-3/8e-4 [FOLK], pinned to start below the [AS16]
  -24 dB clearly-audible band and tune upward by ear;
- hum 1e-3 [FOLK]; the 50 Hz switch stays a documented constant;
- **the shipped default 0.2 most of all** [FOLK] — a factory-new unit's
  tolerance effects, judged against nothing yet.

The by-ear pass against reference recordings owns every one of these;
`make exhibit` re-derives every number above from the pinned working
set. No MIDI CC is assigned to wear at M7 (a setup control, not a
performance control — it rebuilds gain banks without smoothing);
`render_midi` renders at the shipped default, so the next
docs/renders.md entry on the same input A/Bs M6-vs-M7 directly.
