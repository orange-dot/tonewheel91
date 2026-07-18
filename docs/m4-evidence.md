# M4 evidence — the vibrato/chorus scanner

Date: 2026-07-17. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, one clean-build run of each binary;
all numbers below are from that run.

## Changes

The section 9 scanner, whole: the real 18-section line box, the 16-plate
triangular sweep, V/C mode mixes, the bass bypass, organ wiring, and the
CC84 control — M4-1..M4-8 in one change.

- `src/scanner.c` — the section 9 constants ([DAFx16] components, tap
  tables, divider-derived node gains, 412 rpm) and the line-box kernel:
  a trapezoidal (bilinear) nodal model of the LC ladder. Eliminating the
  inductor updates leaves one constant-coefficient tridiagonal solve per
  sample (Thomas factors at init, no libm, ~300 flops/frame). No prewarp
  [decision, sec 9]: plain bilinear keeps low/mid-band group delay exact
  so the V3 depth *emerges* inside the pinned band; the passband edge
  warps ~6% low at 48 kHz and converges at higher rates. Node gains come
  from the divider resistor ratios `R- / (R+ + R-)` rather than
  transcribed dB values — same table, no new numbers.
- Scanner sweep: 16 plate stacks, terminals t1..t9 wired there-and-back,
  linear crossfade of the two plates under the pickup (the capacitive
  divider [DAFx16] says behaves as linear interpolation), 412/60 Hz
  rotor. One revolution = one full there-and-back sweep.
- V/C modes: V = swept only; C = (dry + swept)/2 [decision, sec 9, per
  the [P39] equal-power precedent]. V and C share tap tables per depth.
- Bass bypass, realized as an exact wheel split [derived, sec 9]: the
  manual sum's content below 80 Hz is precisely the fundamentals of
  wheels 13..16, so `tw_generator_tick` captures the running keyed sum
  after wheel 16 (`tw_frame.keyed_low`) — same additions in the same
  order, so `keyed` is bit-identical to pre-M4 — and the organ routes
  that prefix around the line. No crossover filter, zero leakage.
- `src/organ.c` — scanner between the keyed+perc sum and swell;
  `vibrato = off` takes the original expression untouched.
- `driver/main.c` — CC84, value/19 -> off, V1..V3, C1..C3.
- `test/test.c` — impulse timing/dispersion, passband edge at 48 and
  192 kHz, DC gain, sweep rate, there-and-back symmetry, per-mode mix
  ratios, tap-span ordering (V1 vs V3 arrival), the wheel-split prefix,
  sub-80 Hz bit-identity, OFF-rejoin bit-identity, hostile-mode clamps,
  and two-run FNV determinism of a vibrato script.

## Test result

    7671 checks, 0 failures

(M3 baseline was 7238.) All pre-M4 signatures verified unchanged against
the recorded evidence: exhibit_phase `b71cbb09b1ecd064` / `012442c11623cab8`
/ `96b17679450dec1b` (m1), exhibit_contacts `3f25ffe656644fd6` (m3),
exhibit_taper `0565b81fd82c84a7` (m3), exhibit_percussion
`69ae12dcd88cd2ea` (m3) — vibrato off is bit-identical to the pre-M4
organ, by construction and by measurement.

## Exhibit result

    line box (18-section ladder, v19, 48 kHz):
      impulse peak: 0.85 ms (idealized total delay ~0.85 ms)
      passband edge (last -6 dB): 6575 Hz (analog ~7075 Hz;
        bilinear warp puts ~6.7 kHz at 48 kHz)
      V3 cyclical depth at 1 kHz: +/-1.48% (sec 9 acceptance 1.1-1.6%;
        [P45] ~1.5%)
      V3 peak deviation (with moving ripple): +/-1.91%
      scan rate: 6.866 Hz (412 rpm = 6.867 Hz)
    scripted determinism (C3 chord): FNV64 079088b2a0394053
      (two runs identical)

The depth is reported as two figures on purpose. Smoothed to the sweep
scale (32 ms), the cyclical shift is +/-1.48% — the number [P45] calls
"approximately 1.5%" and [AS16] restates as ~25 cents, dead in the
pinned 1.1-1.6% band. On an 8 ms window the peak deviation is +/-1.91%:
the low-impedance V-mode drive (the switch shorts Rc) sets up a standing
wave in the mismatched line, and its moving ripple superposes on the
sweep. That ripple is the same one [DAFx16] measures ("~6 dB and deeper,
positions move over the scan cycle") and is audible as the phaser-like
motion — signature, not error. Test-probed mode ordering agrees with the
tap spans: V1 +/-0.67%, V2 +/-1.08%, V3 +/-1.91% peak.

## Dispersion A/B

    build/m4_dry.wav           dry chord (16'+5-1/3'+8'+4', C-E-G)
    build/m4_scanner_v3.wav    the same chord through V3
    build/m4_scanner_c3.wav    the same chord through C3
    build/m4_chorus_naive.wav  the wrong model: ideal delay line,
                               linear-interp read, same 6.867 Hz
                               triangular sweep, same 0.85 ms span,
                               same 1:1 mix

The naive render has no lowpass edge, no moving ripple, no per-tap AM
ramp and no dispersion — A/B against `m4_scanner_c3.wav` is the audible
argument for modeling the actual circuit (sec 9). By-ear verdicts
(C-mode voicing against the [P39] treble-detune target, ripple strength,
output level trim) stay open in the section 16 register.
