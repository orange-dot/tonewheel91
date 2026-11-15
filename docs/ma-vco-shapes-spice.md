# V2 and V3 in SPICE: B1's two ramps, and the two shapers B1 lacks

Date: 2026-11-14. Tooling in `driver/spice/vco/` (`b1_low.py`,
`shapers.cir`, `shapers.py`, `shapers_render.py`); every number here comes
from `build/vco/b1low/low.json` and `build/vco/shapers/*.json`. What the core
takes from it is in `docs/ma-vco-plan.md` (V2, V3).

## 1. B1's triangle below 225 Hz

Step 1 measured B1 from 225 Hz to 1.94 kHz. The same DUT, unchanged, at six
lower CVs (`b1_low.py`, capacitor node and square only):

| CV (V) | f (Hz) | rise fraction | up (V/s) | down (V/s) | swing (V) | two straight ramps, residual |
|---:|---:|---:|---:|---:|---:|---:|
| -0.04 | 27.39 | 0.54461 | 128.66 | 153.92 | 2.5592 | -39.0 dB |
| -0.02 | 33.59 | 0.54036 | 159.09 | 187.05 | 2.5594 | -39.9 dB |
| 0 | 40.07 | 0.53709 | 190.95 | 221.55 | 2.5596 | -40.6 dB |
| 0.03 | 50.16 | 0.53333 | 240.77 | 275.17 | 2.5599 | -41.4 dB |
| 0.08 | 67.68 | 0.52885 | 327.65 | 367.78 | 2.5605 | -42.6 dB |
| 0.15 | 93.05 | 0.52454 | 454.35 | 501.26 | 2.5612 | -43.9 dB |
| 0.25 | 130.21 | 0.52038 | 641.14 | 695.63 | 2.5623 | -45.3 dB |
| 0.35 | 167.90 | 0.51746 | 831.58 | 891.95 | 2.5634 | -46.5 dB |
| 0.5 | 224.94 | 0.51434 | 1121.64 | 1187.96 | 2.5648 | -48.0 dB |
| 1 | 416.51 | 0.50876 | 2103.48 | 2178.47 | 2.5693 | -51.5 dB |
| 2 | 800.56 | 0.50443 | 4090.17 | 4163.38 | 2.5772 | -55.8 dB |
| 3 | 1183.54 | 0.50260 | 6085.94 | 6149.62 | 2.5845 | -58.6 dB |
| 4 | 1564.70 | 0.50159 | 8083.50 | 8134.99 | 2.5913 | -60.7 dB |
| 5 | 1943.40 | 0.50094 | 10078.45 | 10116.52 | 2.5979 | -62.4 dB |

The upper eight rows repeat step 1 to the fourth digit. The asymmetry grows
toward the bass about as f^-0.54, not as 1/f, so it does not run away: the
rise is 0.545 at 27 Hz. Its second harmonic is `|cos(pi r)| / 2` of the
fundamental, -33 dB at 225 Hz and -23 dB at 27 Hz. The ramps also bow a
little more toward the bass (two straight ramps leave -39 dB at 27 Hz); the
core does not take that.

`ln(r - 1/2)` is a quartic in octaves from A4, within 3 % of the asymmetry at
every row. The swing grows by 20.4 uV/Hz (each reversal overshoots by the
comparator's delay), within 1.4 mV of a straight line.

## 1b. The bow in each ramp (V2c)

Two straight ramps leave -39 dB at 27 Hz and -62 dB at 1.9 kHz. A parabola
per ramp (the same traces, `build/vco/b1low/bow.json`) leaves -85 dB or
less: each ramp slows toward the vertex it runs to, the rise above its chord,
the fall below it, the fall about twice as much. Height at mid-ramp over half
the swing:

| f (Hz) | rise | fall | a parabola per ramp leaves (rise / fall) |
|---:|---:|---:|---:|
| 27.39 | +0.01493 | -0.02148 | -87 / -85 dB |
| 33.59 | +0.01301 | -0.01977 | -89 / -87 dB |
| 40.07 | +0.01162 | -0.01842 | -90 / -88 dB |
| 50.16 | +0.01013 | -0.01682 | -92 / -90 dB |
| 67.67 | +0.00850 | -0.01485 | -95 / -93 dB |
| 93.05 | +0.00710 | -0.01288 | -98 / -95 dB |
| 130.21 | +0.00587 | -0.01094 | -101 / -98 dB |
| 167.90 | +0.00507 | -0.00954 | -103 / -101 dB |
| 224.94 | +0.00424 | -0.00804 | -105 / -104 dB |
| 416.51 | +0.00280 | -0.00533 | -110 / -110 dB |
| 800.56 | +0.00170 | -0.00322 | -114 / -118 dB |
| 1183.54 | +0.00123 | -0.00232 | -116 / -123 dB |
| 1564.70 | +0.00097 | -0.00182 | -118 / -127 dB |
| 1943.40 | +0.00080 | -0.00151 | -119 / -128 dB |

Like the skew, it grows toward the bass about as f^-0.55. The core adds the
two parabolas by phase to what the triangle output and the shapers read
(`ln |bow|` a cubic in octaves from A4, within 1.4 %); the integrator stays
on its straight trajectory.

## 2. The shapers (`shapers.cir`)

Two study circuits hung on B1's buffered triangle (TBUF), with ideal rails,
bias and trims, the lab's transistor models. Not board designs.

- **Triangle to sine:** a long-tailed pair with emitter degeneration and a
  1 mA tail, driven through a coupling capacitor and a 10 k / trimmer divider.
  The degeneration was chosen on the static curves, each trimmed for its
  lowest distortion on B1's A4 triangle: 0 ohm gives -35.9 dB, 22 ohm
  -38.1 dB, **47 ohm -39.8 dB**, 100 ohm -38.8 dB.
- **Triangle to saw:** a pair with 2.2 k degeneration, linear in TBUF
  against a reference, whose two currents a switching quad swaps on B1's
  own square. While the triangle rises the output follows it, while it falls
  its mirror image, so both halves rise. The reference sits at the top
  vertex, where the swap then takes no step. The static stage is linear to
  2.7 mV in 3.2 V (-61 dB), so the core takes it as linear.

**Trim at A4** (`shapers.py trim`, B1 and both shapers in transient, as a
technician would): the drive trimmer for the sine's lowest odd distortion
(static, k = 0.1215, 1.38 k), its offset for the lowest second harmonic in
place (+0.30 mV), and the saw's reference until the step at the swap is
under 0.1 uV (4.06891 V, 7 passes). At A4 the sine is then -39.5 dB THD,
with H2 at -40.4 dB. The offset trim barely moves H2 (-39.4 to -40.4 dB): it
comes from B1's own skew, not from the pair.

## 3. Across the range (`shapers.py bench`, `analyze`)

The trimmed board at nine CVs. "Static" is the pair's DC curve applied to
that case's TBUF, which is what the core computes.

| f (Hz) | rise | sine THD | static | H2 | static | H3 | static | saw step |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 27.40 | 0.54459 | -25.3 | -25.8 | -25.8 | -25.9 | -36.6 | -44.2 | +0.00373 |
| 40.09 | 0.53708 | -27.0 | -27.4 | -27.4 | -27.5 | -39.4 | -46.4 | +0.00349 |
| 67.72 | 0.52884 | -29.3 | -29.3 | -29.6 | -29.4 | -43.2 | -48.6 | +0.00308 |
| 130.29 | 0.52038 | -32.3 | -32.5 | -32.6 | -32.7 | -47.4 | -53.7 | +0.00238 |
| 225.08 | 0.51434 | -35.1 | -35.0 | -35.5 | -35.2 | -49.8 | -55.3 | +0.00154 |
| 440.09 | 0.50833 | -39.5 | -39.0 | -40.4 | -39.5 | -53.2 | -58.6 | -0.00000 |
| 801.03 | 0.50443 | -43.4 | -41.9 | -45.4 | -42.9 | -57.2 | -58.0 | -0.00228 |
| 1184.23 | 0.50260 | -45.4 | -43.5 | -48.2 | -45.1 | -62.7 | -55.2 | -0.00452 |
| 1944.50 | 0.50094 | -46.4 | -45.8 | -49.9 | -50.7 | -64.1 | -50.9 | -0.00872 |

dB relative to the fundamental. The saw's step at the top vertex is relative
to half its span.

- **The sine's character is B1's skew.** Its distortion is almost all second
  harmonic, and that follows the triangle's rise: -25 dB at 27 Hz, -40 dB at
  the trim note, -46 dB at 1.9 kHz. The static curve gets THD and H2 within
  2 dB up to 1.2 kHz. H3 does not follow it: at the bottom the input coupling
  (1.4 Hz corner) tilts the ramps, at the top the pair's own speed matters.
- **The saw's step is the swing.** Trimmed at A4, the vertex moves with the
  triangle's swing and the swap takes a small step: +0.4 % of half the span
  at 27 Hz, -0.9 % at 1.9 kHz. It is about three quarters of what the
  capacitor's swing predicts at the top, because the quad swaps just after
  the vertex; the core therefore takes the swing the pair saw, fitted to
  these steps (a cubic in octaves, within 2.3e-4).

## 3b. Above 2 kHz: B1 with a range switch (V2b)

B1 is linear from 0.5 to 5 V by design (225 Hz to 1.94 kHz). To see what
the shape does higher up, the same board takes a smaller timing capacitor,
C3 / 8 (1.8 to 14.5 kHz over 0.5 to 5 V) and C3 / 16 at 3.3 V (18.7 kHz), as
a range switch on a real board would; the shapers and their A4 trim are
unchanged (`shapers.py bench_high`, `analyze_high`, `high.json`).

| f (Hz) | case | rise | swing (V) | saw step | sine THD | H2 | H3 | static H3 |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1769 | r8-cv0.5 | 0.51463 | 2.5973 | -0.01038 | -34.3 | -34.6 | -49.4 | -48.4 |
| 3844 | r8-cv1.2 | 0.50775 | 2.6278 | -0.01852 | -38.1 | -38.7 | -48.6 | -44.7 |
| 6175 | r8-cv2 | 0.50470 | 2.6611 | -0.02779 | -39.0 | -40.9 | -44.1 | -41.4 |
| 9017 | r8-cv3 | 0.50286 | 2.7016 | -0.03921 | -37.9 | -42.1 | -40.3 | -38.4 |
| 11779 | r8-cv4 | 0.50184 | 2.7413 | -0.05045 | -36.3 | -42.6 | -37.7 | -36.2 |
| 14461 | r8-cv5 | 0.50119 | 2.7804 | -0.06159 | -34.8 | -42.7 | -35.7 | -34.5 |
| 18742 | r16-cv3.3 | 0.50279 | 2.8391 | -0.07704 | -32.7 | -42.2 | -33.4 | -32.4 |

- **The rise follows the current, not the pitch.** With C3 / 8 the rise at
  0.5 V is 0.5146, what the plain board has at 0.5 V (225 Hz). A range switch
  repeats the bass's skew three octaves up; a core whose current spans the
  range (the VCO-2 law the core plays) runs on toward equal ramps. The core
  keeps the latter (VD12); this column is not taken.
- **The swing grows with the pitch, nearly straight in hertz**: the
  comparator's delay is a fixed time, so the overshoot at each vertex is a
  growing share of the period. The saw's step at the vertex follows it:
  -1.0 % at 1.8 kHz, -2.8 % at 6.2 kHz, -6.2 % at 14.5 kHz, -7.7 % at
  18.7 kHz. As the swell the pair sees, 4.14e-6 per Hz, within 8e-4. At the
  join the range switch sits 0.0024 above the plain board at the same pitch
  (its turn-around is slower at its lower current); the core takes the slope
  and joins it to the plain board's curve at 1.94 kHz.
- **The sine gets third harmonic back** above 6 kHz (-44 dB at 6.2 kHz,
  -33 dB at 18.7 kHz): the growing swing overdrives the trimmed pair. The
  static curve on the board's own triangle predicts it within 2 dB of THD.
  Above about 7 kHz that harmonic is past 20 kHz, above 10 kHz the second
  is too, and only the fundamental's level is left in the audio band (the
  sine's grows 0.4 dB to 18.7 kHz, the saw's 0.2 dB).

## 4. The core against this (`make check-ma-vco-shape`)

`test/ma_vco_shape.c` renders the core's 8x source with B1's rise and the
pair's swell at each pitch:

| f (Hz) | sine THD, core (SPICE) | H2 | H3 | saw step, core (SPICE) |
|---:|---:|---:|---:|---:|
| 27.4 | -25.8 (-25.3) | -25.9 (-25.8) | -52.4 (-36.6) | +0.00373 (+0.00373) |
| 130.3 | -32.5 (-32.3) | -32.7 (-32.6) | -60.4 (-47.4) | +0.00237 (+0.00238) |
| 440.1 | -39.8 (-39.5) | -40.4 (-40.4) | -58.5 (-53.2) | -0.00004 (-0.00000) |
| 1184.2 | -47.2 (-45.4) | -49.9 (-48.2) | -82.7 (-62.7) | -0.00477 (-0.00452) |
| 1944.5 | -50.9 (-46.4) | -55.5 (-49.9) | -56.9 (-64.1) | -0.00945 (-0.00872) |

Both shapers follow SPICE where the circuit's character is (the sine's H2,
the saw's step). The sine's third harmonic took two more pieces of the
circuit:

| f (Hz) | straight ramps | ramps bowed (V2c) | and the input coupling (V3b) | SPICE |
|---:|---:|---:|---:|---:|
| 27.4 | -52.4 | -45.5 | -37.0 | -36.6 |
| 40.1 | -59.6 | -47.8 | -39.7 | -39.4 |
| 67.7 | -74.8 | -50.2 | -43.3 | -43.2 |
| 130.3 | -60.4 | -52.4 | -47.4 | -47.4 |
| 225.1 | -58.1 | -53.9 | -50.5 | -49.8 |
| 440.1 | -58.5 | -56.2 | -54.1 | -53.2 |
| 801.0 | -63.0 | -60.6 | -58.6 | -57.2 |
| 1184.2 | -82.7 | -67.2 | -63.7 | -62.7 |

The coupling capacitor (10 uF into 10 k and the 1.38 k trimmer, a 1.40 Hz
corner) tilts each ramp a little toward the middle; at low notes the trimmed
pair turns that into third harmonic. With both, the core's sine is within
1.5 dB of the board in transient to 1.2 kHz.

**The second harmonic above 1 kHz is the buffer's (V2d).** The capacitor's
own triangle has exactly the second harmonic its skew gives, to 1.9 kHz
(-56.5 dB against -56.6). TBUF, two emitter followers later, has more: the
followers' Vbe follows their current, a static bend `x + k2 (x^2 - 1)` with
k2 = 0.0093 at every pitch (225 Hz to 14 kHz), which adds -46.8 dB of second
harmonic. Below 1 kHz the skew hides it; above 2 kHz it is all there is. The
module's triangle output comes from TBUF too, so the core bends both its
triangle output and what the shapers read:

| f (Hz) | triangle H2, core (straight) | core (bent) | SPICE TBUF | sine H2, core | SPICE |
|---:|---:|---:|---:|---:|---:|
| 225.1 | -32.9 | -32.7 | -32.7 | -35.3 | -35.5 |
| 440.1 | -37.7 | -37.1 | -37.0 | -39.4 | -40.4 |
| 801.0 | -43.2 | -41.4 | -41.3 | -43.1 | -45.4 |
| 1184.2 | -47.8 | -43.9 | -43.9 | -45.0 | -48.2 |
| 1944.5 | -56.7 | -46.0 | -45.9 | -46.4 | -49.9 |

The sine's second harmonic is now above the board's from 0.8 kHz, by 2.3 to
3.5 dB instead of 5 dB below it at 1.9 kHz. Two things are left, both
outside the buffer: the pair's static curve on the board's own TBUF already
gives 2.5 to 3 dB more than the board in transient at 0.8 to 1.2 kHz (a
dynamic effect in the pair), and at 1.9 kHz the core's own triangle, rounded
over its edge zone, adds about 4 dB. The sine's THD is within 1.5 dB of the
board at every pitch. The fit also offers a cubic term; on the board's own
triangle it puts the sine's third harmonic 2 to 3 dB above what TBUF gives,
so it is not the buffer's and is left out.

Above 2 kHz, against the range-switched board: the saw's step -0.0179
(SPICE -0.0185) at 3.8 kHz and -0.0310 (-0.0278) at 6.2 kHz; the sine's H3
-44.2 dB (-48.6) and -36.2 dB (-44.1). The core's triangle loses top
harmonics at high notes (its edge zone), so the pair, which cancels a true
triangle's H3, leaves more of it; at 6.2 kHz that harmonic is at 18.5 kHz.

## 5. Listening (`build/vco/shapers/listen/`, 48 kHz, 24 bit, mono)

Each `tone-*-AB.wav` is SPICE, 0.4 s of silence, then the core, at the same
RMS, at 40, 130, 440 and 1184 Hz: `tri_sine`, `tri_saw` and `tri` (B1's own
triangle against the core's skewed one); and from the range-switched board
at 3844 and 6175 Hz, `tri_sine` and `tri_saw`. Each `ma-*.wav` is the core alone,
the old shape then the circuit one: the triangle at C1 and C2, the saw at C2
(the bow; at -42 dB it is not expected to be audible), the Mamut sine against
the shaper sine at C3, the saw against the saw from the triangle at C2.

## 6. Method and limits

- ngspice-47, Gear, reltol 1e-5, 0.5 us maximum step, linearized to
  1.92 MHz. Steady CVs; tones resynthesized from their harmonics up to
  20 kHz.
- The shapers are study circuits with ideal rails and an ideal difference
  stage reading the saw. No board has been built or measured. The models have
  no noise, so nothing here is about jitter.
- Nominal parts only: no spread in the pairs, no temperature. The trims are
  done at 25 C.
