# Handoff: re-fit the warmth drive kernel to the circuit-true AO-28

**To:** the next agent picking up tonewheel91 warmth work.

> **PREMISE REVISED — read §0 first.** This handoff was originally written on a
> 4-point partial sweep whose premise ("real 6AU6 has materially more 3rd
> harmonic → shipping kernel too clean → re-fit warranted") **did not survive
> the full 12-point sweep.** The corrected framing is in §0. The build steps
> (§2–3) are unchanged; what changed is the expected *outcome* — a re-fit is now
> plausibly a **null result**, and confirming the current kernel is fine is a
> valid, likely deliverable.

This is a **re-fit *evaluation*, not a bug fix.** Nothing is broken. Do not
"correct" the existing table as if it were wrong, and do not manufacture a new
table to justify the work — your job is to determine *whether* a re-fit is
warranted, then stop at the listening gate (§6) either way.

---

## 0. Corrected premise (supersedes the original)

The full level sweep at the V1 6AU6 plate (240 Hz), frequency-checked:

| drive | H2/H1 | H3/H1 | H2/H3 |
|---|---|---|---|
| 0.02 | 1.8 % | 0.3 % | 16 dB |
| 0.05 | 4.8 % | 0.3 % | 25 dB |
| 0.10 | 10.3 % | 5.6 % | **5.3 dB** ← dip |
| 0.15 | 18.0 % | 7.4 % | 7.8 dB |
| 0.20 | 22.5 % | 7.3 % | 9.8 dB |
| 0.30 | 27.6 % | 6.2 % | 13 dB  ← trust-anchor |
| 0.50 | 32.9 % | 3.9 % | 18 dB |
| 0.70 | 36.3 % | 2.0 % | 25 dB |
| 1.00 | 40.2 % | 0.8 % | 34 dB |
| 1.50 | 45.4 % | 4.9 % | 19 dB (suspect) |
| 2.00 | 49.7 % | 8.8 % | 15 dB (suspect) |
| 3.00 | 44.7 % | 2.3 % | 26 dB (H2 compressing) |

Frequency independence @0.3 V: H2 = 26.7 / 27.6 / 27.9 %, H3 = 6.0 / 6.2 / 6.0 %
at 120 / 240 / 480 Hz — essentially flat.

**What this means, concretely:**

1. **H2 dominates throughout** (13–34 dB over most of the range) — comparable to
   the 12AX7 stand-in's ~18–28 dB. The kernel is **not** globally "too clean."
   The re-fit case is weak; **a null result is the likely, valid outcome.**
2. The only real divergence is a **localized mid-drive H3 bump at ~0.10–0.20 V**
   (H2/H3 dips to 5–8 dB). Proving *that* is real and audible is the entire
   burden — not a broad even/odd shift.
3. **Trust-anchor = the 0.3 V point** (H2 27.6 %, H3 6.2 %, H2/H3 13 dB): settled,
   frequency-independent. Fit and compare against mid-drive points; distrust the
   extremes.
4. **Frequency-independence confirms static-kernel modeling is correct** — so
   `tw_drive_curve` being a static transfer table is *right*, independent of any
   re-fit. Use ≈27 % H2 / ≈6 % H3 at mid-drive as a **correctness check on your
   `ao28_curve.cir`**: if your static fit doesn't reproduce it, your deck or the
   `p6au6` model is wrong — fix that before fitting anything.
5. **The 1.5–2.0 V H3 wobble and the low-drive points are contaminated** by the
   slow screen-RC settle (C4/C10 .33 µF × 2.2 M ≈ 0.73 s vs the 0.4 s save
   window; see §4). If you already fitted to those raw extremes, **discard that
   fit** and re-run with save-start past ~0.8 s first. Fit only settled points.

Because the premise weakened, the bar to justify a re-fit is now **higher**, and
§5 (pentode-curvature validation) and §6 (listening gate) are more binding, not
less. Report "no re-fit warranted" plainly if that is what the evidence gives.

---

## 1. What already exists (read these first)

- `driver/spice/ao28.cir` — full AO-28 preamp, DC-validated against the B3/C3
  service-manual sheet (p.72). Params: `psu` (0 ideal rails / 1 real 6X4),
  `vib`, `drv`, `scan` (0 collapses the scanner LC line for fast runs).
- `driver/spice/ao28.lib` — tube models. **The 6AU6 pentode params are fitted,
  not a published card** — this is the single biggest source of uncertainty in
  the H3 number you are about to fit to. Read §5 before trusting it.
- `docs/ao28-netlist.md` — the BOM and the read-vs-supplied-vs-reconstructed
  seam. The V4 vibrato/expression matrix is reconstructed and NOT trustworthy
  for audio; warmth is measured at the **V1 6AU6 plate**, upstream of it.
- `src/drive.c` — the target. `tw_drive_curve()` interpolates the 65-knot
  monotone-Hermite tables `CURVE_Y` / `CURVE_MH` (lines ~55–84). Comment at
  ~42–54 documents provenance (currently: fitted to `driver/spice/curve.cir`,
  the 12AX7 stand-in, S = 0.72 V/unit axis, G0 = 60.56 output norm).
- `driver/exhibit_warmth.c` — the fitter. `./build/exhibit_warmth fit <file>`
  reads a **static DC sweep** with columns `vin, v(p), v(g), v(k)` and prints
  ready-to-paste `CURVE_Y` / `CURVE_MH` (uniform knots over [−8,8], h=0.25,
  monotone limiter, curve(0)≡0). This is the same tool that made the current
  table — reuse it, do not hand-roll a fit.
- `driver/spice/curve.cir` — the *pattern* to copy: it holds the 12AX7 cathode
  at its quiescent value (ideally-bypassed) and DC-sweeps the grid, writing
  `v(p) v(g) v(k)`. Your AO-28 deck must be the 6AU6 analog of this.

---

## 2. The core task — build the AO-28 static-transfer deck

Create `driver/spice/ao28_curve.cir`: the static (fast-manifold) transfer of
the **V1 6AU6 input stage**, structured exactly like `curve.cir` so the fitter
can eat it unchanged.

Requirements:
- `.include driver/spice/ao28.lib`, use the `p6au6` subckt.
- Real surrounding network from the sheet: R8 470k plate load to +280, R7 2.2M
  screen dropper + C4 .33 bypass (at DC the screen just sits at its divider
  point — hold it there), grid via R6 47k from the sweep source.
- **Hold the cathode at the +1.7 V bias-rail value** (the DC-validated
  `v(kbias)` ≈ 1.68 V), the same "ideally-bypassed cathode" trick curve.cir
  uses — this isolates the static grid→plate transfer from cathode dynamics.
- Decision you must make and document: **open-loop vs closed-loop.** curve.cir
  is open-loop (bare triode, no feedback). The AO-28 6AU6 has global feedback
  (R4 1.8M/C2 39pF ∥ R5 10M). The kernel models a *local saturator*, so the
  apples-to-apples choice is **open-loop** (feedback removed) — the feedback is
  a linear loop the C models elsewhere. Build open-loop first; note it.
- `.control`: `dc VIN -6 6 0.002`, then
  `wrdata build/spice/ao28_curve.txt v(P1) v(g1) v(kbias)`
  (columns must be plate, grid, cathode — match curve.cir's order).

Validate: the sweep must be monotone rising in v(P1), span cutoff→grid-
conduction, and its 0-crossing region must be smooth. If ngspice throws
non-convergence on the DC sweep, add `.options gmin=1e-10` and/or a source
ramp; do NOT add artificial series R that changes the transfer.

---

## 3. Fit and diff

```
make exhibit_warmth                       # builds ./build/exhibit_warmth
ngspice -b driver/spice/ao28_curve.cir    # writes build/spice/ao28_curve.txt
./build/exhibit_warmth fit build/spice/ao28_curve.txt   > /tmp/ao28_fit.txt
./build/exhibit_warmth fit build/spice/curve.txt        > /tmp/12ax7_fit.txt
```

- Confirm the fit residual is comparable to the current 0.0009 worst-case. If
  it's much worse, the AO-28 curve has a kink the monotone Hermite can't hold —
  investigate the deck before trusting the table.
- Diff the two `CURVE_Y`/`CURVE_MH` blocks. Quantify the *asymmetry* difference
  (the whole point): compute H2/H3 vs level for each fitted curve with the
  existing transient harness (§4) and confirm the AO-28 really does carry more
  odd content. If the gap is inside the noise of the pentode-model uncertainty
  (§5), STOP and report — there is no re-fit to make.

---

## 4. Cross-check against the transient sweep

The transient warmth sweep is already wired: `make ao28-ref` runs
`ao28.cir` and writes `build/spice/ao28_lvl_240_*.txt` /
`ao28_freq_*.txt` (columns `time v(P1) v(out) v(kbias)`; read **v(P1)**).
Analyze H2/H1, H3/H1 vs level at 240 Hz. **The full expected curve + the
frequency-independence check are in §0** — reproduce it; if your numbers
diverge from the §0 trust-anchor (0.3 V → H2 27.6 % / H3 6.2 %), the deck
regressed.

**Gotcha:** the screen-bypass RC is slow (C4/C10 .33µF × 2.2M ≈ 0.73 s), longer
than the current 0.4 s save window, so `v(P1)` peak-to-peak drifts and is NOT a
clean amplitude readout at low drive. Push the transient save-start past ~0.8 s
(e.g. `tran ... 0.8 2u` with stop 1.3 s) before quoting a clean H2/H3-vs-level
curve, or confirm the windowed-FFT ratios are stable regardless.

---

## 5. The honest caveat you must not skip

The 6AU6 pentode model (`p6au6` in ao28.lib) is a **hand-fitted conservation-
form pentode**, calibrated only to a single DC operating point (Vgk −1.7,
Vg2 60 → Ip 311µA/Ig2 99µA). Its curvature away from that point — which is
exactly what sets H3 — is *not* independently validated. Before re-fitting the
shipping kernel to it:
- Sanity-check `p6au6` against 6AU6 datasheet plate curves (Ip vs Vp at a few
  Vg2/Vgk), at least by eye. Adjust EX / the plate-knee (VA) if the curvature
  is off. Document any change.
- If you cannot raise confidence in the pentode curvature, treat the re-fit as
  *provisional* and say so in `docs/warmth-evidence.md`.

---

## 6. Gate — do NOT ship silently

`docs/constants.md` §14.1: *"By-ear verdicts against reference recordings
override any [decision]."* tonewheel91 is milestone-gated. So:
- Land the new deck (`ao28_curve.cir`), the diff, and the transient cross-check
  as **evidence** in `docs/warmth-evidence.md` (new round; keep the old 12AX7
  round intact for comparison — do not delete it).
- Propose the re-fitted `CURVE_Y`/`CURVE_MH` as a **candidate**, behind the
  listening gate. Do not overwrite the shipping table in `src/drive.c` until a
  by-ear verdict against reference recordings signs off.
- Update the `src/drive.c` provenance comment only when the swap is actually
  made — not before.

## 7. Acceptance checklist

- [ ] `driver/spice/ao28_curve.cir` exists, is the 6AU6 analog of curve.cir,
      DC-sweeps clean and monotone, open-loop, cathode held at +1.7.
- [ ] `exhibit_warmth fit` produces a table with residual ~≤0.001.
- [ ] H2/H3-vs-level quantified for both references; the odd-harmonic gap is
      shown to be real (or shown to be model noise → task ends).
- [ ] `p6au6` curvature sanity-checked against datasheet; any tweak documented.
- [ ] `docs/warmth-evidence.md` gets a new round; old round preserved.
- [ ] Re-fitted table proposed as candidate only; `src/drive.c` untouched until
      the listening gate passes.
