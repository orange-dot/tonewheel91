# AO-28 preamplifier model — BOM and verification seam

This documents `driver/spice/ao28.cir` + `driver/spice/ao28.lib`: a full
Hammond **AO-28 preamplifier** model re-derived from the real B3/C3 service
manual schematic (`docs/externalDocs/Hammond-Organ-B3-C3-Service-Manual.pdf`,
sheet p.72), replacing the generic single-triode that `stage1.cir` used to be.

It exists because the old `stage1.cir` comment claimed *"no component values
were read from the preamp schematic"* — which was false. The schematic was in
`docs/externalDocs/` the whole time. This model reads it.

## The honest seam: read vs. supplied vs. reconstructed

**Read straight off the sheet (circuit-true).** Every resistor and capacitor
value, every rail topology, the two 6AU6 feedback networks, the shared +1.7 V
cathode-bias rail, the 6X4/T7 filter string, and the tube *types*. These are
transcribed verbatim (values table below).

**Supplied, not on the sheet (and calibrated to it).** Tube SPICE *model
parameters* are not printed on any schematic. `ao28.lib` uses published Koren
parameter sets, then fits the free constants so each stage lands on the DC
plate/screen/cathode voltages the sheet prints. The 6AU6 pentode has no single
canonical card, so it uses a conservation-form sharp-cutoff pentode (one space-
current law split plate/screen by a constant screen fraction + soft plate knee).
The first cut fitted its constants to V1's DC box alone: **Vgk −1.7 V, Vg2
60 V → Ip 311 µA, Ig2 99 µA** vs. the sheet's 130 V plate / 60 V screen. The
warmth Round-3 datasheet check (`docs/warmth-evidence.md`) showed that a
one-point DC fit leaves the *curvature* free — and it came out ~3.5× too
shallow (log-slope d(ln Ip)/dVgk ≈ 0.72/V at the box vs ≈ 2.5–2.8/V read off
the GE 6AU6-A transfer families), plus an unphysically slow plate knee (32 %
of space current still missing at Vpk = 0). The current `p6au6` is re-fitted
to the **GE 6AU6-A datasheet shape** (transfer families at Ec2 50/75 V, the
10 µA cutoff points, the typical-operation points; `docs/externalDocs/
6AU6A-GE-datasheet.pdf`, cross-checked against the RCA sheet) with the exact
Hammond DC box held as an equality constraint (a +0.41 V grid offset absorbs
the GE-average-vs-this-box tube spread). It reproduces the box **exactly**
(t_6au6.cir: 132.5 V / 61.9 V / 313.8 µA / 99.1 µA) *and* the sheet's
curvature around it. That two-level fit — box for the operating point,
datasheet family for the shape — is the anchor to the real circuit.

**Reconstructed at scan-legibility limit (flagged, not trusted for audio).**
The vibrato/expression mixing matrix around V4 (C15 51 pF, C16 .033, C18/C20
220 pF, R28–R34, the scanner return) could not be traced wire-for-wire from the
scan. Its DC is close but offset (V4 plates ~173 V model vs ~140 V sheet), and
the fundamental does not pass through it cleanly. **Warmth is therefore measured
at the V1 6AU6 input stage** (fully DC-verified, real values), not at the T3
output.

Two Round-3 caveats sharpen that seam (`docs/warmth-evidence.md`, Round 3):
**(a)** measuring *at* v(P1) does not isolate V1 — C16 (.033 µF) couples the
plate straight into V4A's grid, and V4A grid conduction clip-loads every
positive plate swing, so the harmonic mix seen at v(P1) under drive is
dominated by the reconstructed V4 region, not by the 6AU6 transfer (lift C16
and the mix changes completely). The stage's own transfer is taken from
`ao28_curve.cir` / the C16-lifted sweep instead. **(b)** the full model
motorboats at ~8–10 Hz for drive ≤ 0.05 V (both tube models; stronger with
the corrected one) — a real AO-28 does not, so at least one loop through the
reconstructed region (V4 cathode tie, rail bypassing, or the feedback return)
is wrong. Low-drive rows from full-circuit sweeps are junk; the isolated-stage
decks are unaffected. Also not on the sheet: the scanner section inductance (only the damping
R's and shunt C's are printed — the documented Hammond ~0.7 H/section is used),
and all transformer turns ratios (T1/T2/T3/T4/T5/T6 — plausible ratios, flagged
inline).

## DC validation (psu=0 ideal rails)

| Node | Model | Sheet | |
|---|---|---|---|
| V1 plate / screen | 132.1 / 61.4 V | 130 / 60 | ✓ |
| V2 plate / screen | 132.1 / 61.3 V | 130 / 60 | ✓ |
| +1.7 V bias rail (V2 cathode) | 1.68 V | 1.7 | ✓ |
| V3B output cathode | 14.4 V | 12 | ✓ |
| output plate (into T3) | 290 V | 285 | ✓ |
| V4A / V4B plate | 173 / 166 V | ~140 | offset (matrix) |
| V3A scanner-follower cathode | 21 V | ~100 | offset (grid-DC path) |

(Values with the datasheet-anchored `p6au6`; the DC-box-only first cut sat
within 1.5 V of these on the ✓ rows.)

The five stages that carry and shape the audio (both 6AU6 inputs, the bias
rail, the 12BH7 output) match the sheet. The two offsets are both in the
reconstructed vibrato region and do not touch the warmth measurement.

## Component values (read from sheet p.72)

Input/term: R1 3.9M, R2 1M, C1 470pF (per matching transformer T1/T2).
V1 6AU6: R3 270k grid leak, R6 47k grid series, C3 100pF g-k, R8 470k plate
load, R7 2.2M screen dropper, C4 .33 screen bypass. Feedback: C5 .01, R9 820k,
R10 3.9M, C8 30pF, R4 1.8M, C2 39pF, R5 10M.
V2 6AU6: R12 270k, R16 470k, C8g 100pF, R19 470k plate, R20 2.2M screen, C10
.33. Self-bias R17 1.2k / C9 30µF (this makes the +1.7 V rail). Feedback C11
.0047, R13 4.7M, C7 24pF, R14 10M, R15 2.7M.
V3A 12BH7 follower: C12 .0047, R25 100 (plate to +290), R21 1M, R22 820, R23/
R24 22k. Output C13 1.0µF → scanner node D.
Scanner: R65 27k, R67 56k, R69 39k, R71 33k, R73 18k, R75 12k, R66 68k, R68
150k, R70 150k, R72 180k, R74 180k, R76 180k, R44 22k; C42–C53 .004, C54 .001.
L1–L18 = 0.7 H (assumed, see above).
V4 12AX7: C16 .033 (audio), C15 51pF (HF shunt), R28 270k, R27 1.8M, R29 330k,
C18/C20 220pF, R30/R32 1.8M, R31/R33 68k, C19 .0022, C21 .001, R34 15M, R35
3.3M, R36 330k. Radio-phono C17 680pF, C22 24–90pF trimmer, C23 .033.
Output: C24 82pF, R37 270k, R39 330k, R38 390k, R40 300k tone, R41 100k, V3B
12BH7 into T3, R42 1.2k, C26 30µF.
Percussion: V5 6C4 (R47 47k, R48 470k, C28 .047, R49 2.2k, C29 100pF, C30
25µF), V7 12AU7 (R58 4.7M, R57 1.5M, R60 30k, R59 120k, R55 82k, R56 22k, R51
4.7k, R50 22), R61 33k, R62 100k, C32 390pF, C34 .001, C33 .0033, C35 150pF,
R63 390k.
PSU: V8 6X4, T7 340 Vrms/side CT, C56 40µF, R91 900, C57 40µF, R97 4500, C60
50µF, R98 7500, R99 180, R100/R101 2.7k, R94/R95/R96 4.7k, C58 30µF, C56b 10µF.

## Running

```
make warmth-ref            # runs stage1/curve (legacy) — unchanged
ngspice -b driver/spice/ao28.cir -o build/spice/ao28.log        # full circuit
ngspice -b driver/spice/ao28_curve.cir                          # V1 static transfer
```

`.param psu` 0=ideal rails / 1=real 6X4 ripple; `.param vib` 0=scanner parked;
`.param drv` swell drive amplitude (V, grid-referred). Sweeps write
`build/spice/ao28_lvl_240_*.txt` and `ao28_freq_*.txt` with columns
`time v(P1) v(out) v(kbias)` — read `v(P1)` (the verified V1 6AU6 plate) for
the warmth signature, with caveat (a) above. The tran save window starts at
1.55 s: the slowest state is the screen operating-point walk through R7/R20
2.2M against C4/C10 0.33 µ (τ ≈ 0.73 s), and the original 0.4 s start sat
mid-walk (Round 3 measured the contamination; the pre-fix exports are kept as
`build/spice/*.txt.oldwin`). Only `v(P1) v(out) v(kbias)` are saved — full-node
storage over the window is ~132 MB per tran and trips ngspice's output-memory
check on a small host.

`driver/spice/ao28_curve.cir` is the V1 static (fast-manifold) transfer: the
same `p6au6` open-loop, cathode and screen held at the full-circuit op values,
grid swept −6..+6 V in 2 mV steps through R6 — the 6AU6 analog of `curve.cir`.
Round 3 also ran a C16-lifted variant of the full deck (C16 → 1e-18 F in the
control block, nothing else touched) to separate V1's own transfer from V4A
grid-conduction loading at v(P1).

Tube-model ground truth in `docs/externalDocs/`: `6AU6A-GE-datasheet.pdf`
(ET-T916A; transfer families, cutoffs, typicals — the shape anchor) and
`6AU6-RCA-datasheet.pdf` (cross-check).
