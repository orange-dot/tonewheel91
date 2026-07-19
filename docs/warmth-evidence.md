# Warmth evidence — the drive stage vs a circuit-true triode reference

Date: 2026-07-19. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1,
ngspice-46. Commands: `make warmth-ref` (one ngspice run of the
reference into build/spice/), `make warmth` (one run of the shared
analyzer; its full output is quoted verbatim below), `make test` (8911
checks, 0 failures — the core is untouched by this change). All numbers
are from that one run pair.

## Question

Section 14's gate is "no circuit-level tube model unless M5 evidence
demands it", and every 14.1 constant is a working value with by-ear
override. This doc puts numbers under the one claim the stage exists to
make — warmth: even harmonics dominating and blooming with level, soft
compression, dynamic bias. What does a real triode stage measure, and
where does the behavioral stage measurably differ?

## Method

- Reference: `driver/spice/stage1.cir` — a single self-bias
  common-cathode stage on the published Koren 12AX7 parameter set
  (MU 100, EX 1.4, KG1 1060, KP 600, KVB 300, RGI 2k) around textbook
  values (250 V, 100k plate, 1.5k/25u cathode, 22n couplings into
  1 Meg). Sec 14 names the 12AX7 family as control-stage *flavor*; no
  component values were read from the console schematic, so this
  anchors curve-shape laws, not absolute component truth. Operating
  point from the run: plate 170.1 V, cathode 1.199 V (0.80 mA), gain
  58.1 — squarely textbook for the family.
- One analyzer for both subjects (`driver/exhibit_warmth.c`):
  integer-period rectangular DFT windows on the same 48 kHz grid
  (240 Hz = exactly 200 samples/period; the ngspice output is
  `linearize`d onto that grid). Analyzer H2 floor on a pure sine:
  -314.4 dB. Measure-window determinism: two-run FNV64 identical.
- Axis note: the stage's amplitude is in shaper-input units (drive 1.0,
  so amp = the tw_sat input), the reference's is grid volts. The two
  scales are aligned by equal THD; the 1 % THD anchor lands at 0.155 V
  vs 0.215 units (ratio ~1.4). Comparisons below are quoted at matched
  THD. "Warmth window" here means THD <= ~7 % — H2-dominant, pre-grit.

## Run

C side (`exhibit_warmth`, tw_drive at drive 1.0):

    analyzer H2 floor on a pure sine: -314.4 dB

    240 Hz level sweep (amp = shaper-input amplitude, unit gain law):
         f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%       env      bias
        240  0.02    0.9983   -50.9    -90.5    -68.5   -141.7    0.286    +0.0175    -0.0088
        240  0.05    0.9975   -50.9    -74.6    -68.6   -124.8    0.288    +0.0438    -0.0219
        240  0.10    0.9946   -49.5    -62.6    -68.7   -113.0    0.345    +0.0876    -0.0438
        240  0.15    0.9898   -45.9    -55.7    -68.9   -105.3    0.533    +0.1313    -0.0657
        240  0.20    0.9831   -41.9    -50.8    -69.0    -98.7    0.854    +0.1751    -0.0876
        240  0.30    0.9649   -35.4    -44.0    -68.0    -87.1    1.816    +0.2627    -0.1313
        240  0.50    0.9125   -27.0    -36.0    -56.6    -71.7    4.722    +0.4378    -0.2189
        240  0.70    0.8475   -21.9    -31.3    -46.6    -63.1    8.513    +0.6129    -0.3064
        240  1.00    0.7452   -17.0    -27.4    -36.7    -57.9   14.888    +0.8755    -0.4378
        240  1.50    0.5976   -12.5    -24.7    -27.3    -59.2   24.906    +1.3133    -0.6567
        240  2.00    0.4894   -10.2    -24.2    -22.2    -39.9   32.577    +1.7511    -0.8755
        240  3.00    0.3545    -8.3    -24.7    -17.1    -27.7   41.401    +2.6266    -1.3133

    frequency rows at amp 0.30:
         f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%       env      bias
        120  0.30    0.9624   -35.1    -44.0    -63.1    -85.9    1.869    +0.2626    -0.1313
        480  0.30    0.9655   -35.4    -44.0    -71.1    -87.4    1.803    +0.2626    -0.1313

    measure-window determinism (amp 1.0): FNV64 a1bbb93b1448b4a5 (two runs identical)

    harness verdict: PASS

    C onset at amp 0.50, 240 Hz, from zeroed state (bias = env mean):
       t_ms       h1   h2/h1      bias
        0.0   0.4919   -29.2    +0.1043
        4.2   0.4728   -31.1    +0.2376
        8.3   0.4634   -29.9    +0.3101
       12.5   0.4586   -28.8    +0.3531
       16.7   0.4563   -28.1    +0.3802
       25.0   0.4547   -27.5    +0.4098
       41.7   0.4550   -27.1    +0.4305
       79.2   0.4560   -27.0    +0.4373
      245.8   0.4563   -27.0    +0.4377
      steady h2/h1 (periods 41..60): -27.0 dB
      h2 reaches 50% of steady within 4.2 ms, 90% within 25.0 ms

Reference (`stage1.cir` through the same analyzer):

    spice reference sweep (stage1.cir; source amplitude in volts):
         f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%   vk_mean   vg_mean
        240  0.02   58.0737   -57.8   -109.5   -172.8   -202.1    0.128    +1.1990    +0.0010
        240  0.05   58.0696   -49.9    -93.6   -149.2   -184.8    0.321    +1.1991    +0.0010
        240  0.10   58.0552   -43.8    -81.6   -131.1   -160.8    0.643    +1.1993    +0.0010
        240  0.15   58.0312   -40.3    -74.5   -120.4   -146.8    0.965    +1.1997    +0.0010
        240  0.20   57.9974   -37.8    -69.5   -112.9   -136.8    1.288    +1.2002    +0.0010
        240  0.30   57.9008   -34.2    -62.4   -102.3   -122.9    1.941    +1.2017    +0.0010
        240  0.50   57.5884   -29.7    -53.4    -89.0   -105.6    3.279    +1.2067    +0.0010
        240  0.70   57.1116   -26.6    -47.4    -80.2    -94.6    4.686    +1.2142    +0.0010
        240  1.00   56.0672   -23.2    -40.8    -71.2    -84.2    7.002    +1.2306    +0.0010
        240  1.50   52.8737   -18.5    -33.0    -64.2    -66.6   12.035    +1.2493    -0.0549
        240  2.00   43.5451   -13.0    -28.3    -38.2    -48.4   22.838    +1.0940    -0.6080
        240  3.00   28.6271    -7.5    -29.8    -23.9    -35.0   42.683    +0.9154    -1.6695
        120  0.30   57.6689   -34.3    -62.5   -102.4   -123.1    1.936    +1.2017    +0.0010
        480  0.30   57.9589   -34.2    -62.4   -102.3   -122.9    1.942    +1.2017    +0.0010

    reference onset (tone at t0, bias = cathode-volt walk from quiescent 1.1990 V):
       t_ms       h1   h2/h1      bias
        0.0  28.5568   -29.6    +0.0076
        4.2  28.6225   -29.6    +0.0064
       12.5  28.7078   -29.7    +0.0053
       25.0  28.7681   -29.7    +0.0051
       58.3  28.7980   -29.7    +0.0065
      245.8  28.7942   -29.7    +0.0077
      steady h2/h1 (periods 41..60): -29.7 dB
      h2 reaches 50% of steady within 4.2 ms, 90% within 4.2 ms

(Onset tables abbreviated here; `make warmth` prints the full period
track, and the numbers quoted in the readings are from the full run.)

## Readings

**1. The even/odd recipe is the gap — not the amount of H2.** At
matched THD, H2 itself lands close (the M5 bias-depth tuning did its
job), but the spacing to H3 does not:

    matched THD   reference H2  H3   (H2-H3)     stage H2   H3   (H2-H3)
    1.9 %          -34.2  -62.4  28.2 dB          -35.4  -44.0   8.6 dB
    3.3 %          -29.7  -53.4  23.7 dB         ~-31.2 ~-40.0  ~8.8 dB
    7.0 %          -23.2  -40.8  17.6 dB         ~-23.9 ~-33.2  ~9.3 dB

(stage rows at 3.3/7.0 % interpolated between the 0.30/0.50/0.70
sweep rows.) The triode holds H3 18-28 dB under H2 through the whole
warmth window; the stage holds ~9 dB everywhere, because `tw_sat`'s
own cubic (-8/27) supplies H3 long before a triode's curve would. The
high-order tail says the same (reference H5 -105..-84 dB across
0.3..1.0; stage -87..-58 dB). Warmth *is* H2-above-H3 dominance; this
is the first-order deficit, and it is a curve-shape property, not a
state or solver property.

**2. Low-level law.** The reference's H2/H1 rises proportionally to
level from the quietest row (-57.8 dB at 0.02 V, +7.9 dB per +8 dB of
level): static curve curvature at a fixed operating point. The stage's
H2/H1 is level-independent at -50.9 dB below ~0.1 (the follower's
2·f0 ripple leaking additively through the bias term), then bends into
a ~level-squared regime (bias x curvature). Where the magnitudes
happen to cross, the mechanism — and therefore the phase behavior and
the feel of "blooming from silence" — is still different.

**3. Compression law.** Gain reduction relative to small-signal, from
the gain columns:

    amp             0.30   0.50   0.70   1.00   1.50   2.00   3.00
    reference (dB) -0.03  -0.07  -0.15  -0.31  -0.81  -2.50  -6.14
    stage (dB)     -0.30  -0.78  -1.42  -2.54  -4.46  -6.19  -8.99

At matched THD the stage compresses 4-10x more (at 1.9 %: -0.30 vs
-0.03 dB; at 7 %: ~-1.3 vs -0.31 dB). The real single stage is nearly
gain-transparent through the warmth window and then collapses; the
stage compresses early and smoothly. Musically that early softness may
even be liked — but it is a voicing choice, not the triode's law, and
the evidence should stop it masquerading as circuit behavior.

**4. Where warmth lives, bias is static; where bias moves, it's the
grid.** Through 1.0 V the reference cathode walks at most +0.032 V
(2.6 % of the 1.199 V bias; at 0.30 V: +0.0027 V). The onset confirms
it: first-period H2/H1 (-29.6 dB) equals steady (-29.7 dB) — the
reference's warmth-window H2 is static-curve, with no bloom-in. What
does move, above ~1.5 V, is the grid side: vg_mean -0.055 -> -0.608 ->
-1.670 V while the cathode falls back below quiescent (1.094, 0.915 V)
and gain collapses — coupling-cap charge trapping (blocking), the
fast-charge/slow-leak asymmetry, the recipe inverting. The stage's
bias does the opposite in both regimes: proportional at every level
(-0.44 x amp), substituting for the missing static asymmetry at low
level, and never trapping charge at high level (its 2.0/3.0 rows
compress smoothly; nothing collapses). So 14.1's follower is currently
the H2 *generator*; in the reference the equivalent slow state is a
*dynamics modulator*, and mostly above the warmth window. Corollary:
if the kernel ever gains true static asymmetry, bias depth 0.5 must be
re-derived downward, or H2 doubles.

**5. Onset.** Stage: H2/H1 wobbles -29.2 -> -31.1 -> -27.0 dB over the
first ~25 ms (t90 = 25 ms, the follower attack). Reference: flat
within the first period. At 240 Hz this is subtle — it is the
mechanism's signature more than a defect on its own.

**6. Frequency flatness.** Both sides are flat across 120/240/480 Hz
(H2 within 0.2 dB). The quasi-static picture holds on both sides of
the warmth window; nothing here asks for a per-sample
reactive-nonlinear solve.

## Verdict

- What more-warmth demands, if the ear asks: **reshape the kernel's
  recipe, not the architecture.** A genuinely asymmetric saturator
  (true even term and/or a weakened cubic) targeted at: H3 sitting
  18-28 dB under H2 across the window, H2/H1 proportional to level at
  low level, and the compression column re-tuned (or knowingly kept as
  voicing, tagged [decision]). Constants graduate from [FOLK]/[decision]
  to [derived] against this reference; bias depth re-derives downward
  and its role narrows to dynamics. The runtime shape does not change:
  memoryless kernel + follower + one-pole, no libm, no allocation, no
  per-sample iteration, exact-bypass discipline intact.
- What warmth does **not** demand: a circuit-level runtime model. The
  sec 14 gate stays closed for this goal. The phenomena that genuinely
  need circuit state at audio rate sit above the warmth window — the
  measured blocking regime (grid walking to -1.7 V, cathode falling,
  gain collapsing, recipe inverting) — and that, plus power-stage
  effects the preamp reference does not model (sag, crossover), is
  what the named wave-digital (or equivalent) upgrade stays reserved
  for.
- Open, by ear (sec 16): whether the stage's early compression is a
  feature to keep; the onset wobble; the -51 dB ripple floor.

## Round 2 — the derivation landed (same day)

Commands: `make warmth-ref` (now also runs `driver/spice/curve.cir`),
`./build/exhibit_warmth fit build/spice/curve.txt`, `make` + `make
test` (8911 checks, 0 failures), `make warmth`, `./build/exhibit_warmth
render`. One run each; every number below is from that run set.

### Derivation

`curve.cir` holds the cathode at the reported quiescent 1.198993 V (an
ideally-bypassed Ck — what the audio band sees) and sweeps the grid
source -6..+6 V DC through the same 1k source impedance the AC path
has: the stage's static fast-manifold transfer, grid-conduction knee
included, blocking dynamics deliberately absent. `exhibit_warmth fit`
normalizes it (vp0 = 170.13 V, G0 = 60.56 from the sweep itself — the
60.56/58.07 DC-vs-AC gain ratio is the 1 Meg load divider, a physics
cross-check; axis S = 0.72 V/unit from round 1's 1 % THD anchor) and
fits a monotone C1 cubic Hermite on uniform knots over [-8, 8],
h = 0.25, Fritsch-Carlson limited, flat C1 rails, y(0) forced exactly
0. Worst residual against the sweep: 0.0009. The table lands in
`src/drive.c` as `tw_drive_curve`: exact 0 and unit slope at 0, rails
-1.831 (cutoff floor) / +3.722 (conduction ceiling), and the ~+15 %
slope rise before the positive knee that is the triode's 3/2-power
law. Two independent derivations agree where they overlap: the sweep's
local quadratic gives a2 = 0.088 and cubic -0.016 where round 1's AC
harmonic laws predicted 0.094 and -0.0175.

### What changed in the code

- `src/drive.c` — the preamp kernel is `tw_drive_curve` (the table);
  bias depth forked per kernel: **0.037** for the triode curve
  [derived: the reference's cathode walk per unit envelope at the
  1.0-1.5 V anchor — below that the curve, not the walk, owns the even
  harmonics], **0.5** for the M5 odd kernel where the walk is what
  fakes asymmetry. `tw_sat` itself is untouched.
- `tw_drive_set_kernel(d, odd)` — the fork. `src/rotary.c` selects the
  odd kernel for its 40 W ceiling: a power stage wants a hard bound,
  not preamp warmth; the arithmetic there is order-identical to M6, so
  every rotary signature survives bit-for-bit. Power-stage truth
  (push-pull, sag, crossover) stays with the reserved WDF rung.
- Exact-bypass, snap discipline, pregain/makeup law, follower taus,
  10 Hz highpass: unchanged.

### Test changes (both sanctioned by this evidence)

- The loud-tone DC-image check asserted `hp_lp < -0.02` on a single
  end-sample. The tracker carries ~0.06 of fundamental ripple, so a
  single sample aliases it — the old pass was sitting inside the
  ripple, and with the new kernel the same read landed at -0.0201 by
  stop-phase luck while the true mean was +0.040. The check now
  averages over 11 whole cycles and asserts the mean POSITIVE: the
  asymmetric curve rectifies upward (the reference's plate-current
  mean rises; its vk_mean climbs), and the coupling cap breathes on
  exactly that image.
- `PRE_M7_DRIVE_FNV` re-pinned 0x730ac52bff1f129d ->
  0xa3c0070288f0a1cd: the driven script legitimately renders anew with
  the derived kernel. The other three pre-M7 pins are drive-free or
  rotary-only and hold unchanged.
- `exhibit_wear`'s M6-transition identity anchor re-pinned
  0x00eb4c9bf80cb408 -> 0xf961d056e8b12e32 for the same reason: that
  passage runs the preamp at drive 0.4. Its wear-0-vs-default split
  and every other wear gate hold unchanged.

### Round-2 battery (the derived stage through the same analyzer)

    240 Hz level sweep (amp = shaper-input amplitude, unit gain law):
         f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%       env    dc_img
        240  0.02    0.9976   -61.1    -89.1    -91.1   -104.4    0.088    +0.0175    -0.0006
        240  0.05    0.9976   -53.3    -83.2    -91.1    -97.4    0.217    +0.0438    -0.0015
        240  0.10    0.9976   -47.2    -81.8    -91.3    -91.0    0.435    +0.0876    -0.0028
        240  0.15    0.9974   -43.7    -89.5    -91.0    -87.5    0.655    +0.1313    -0.0039
        240  0.20    0.9970   -41.1    -82.8    -90.6    -85.0    0.877    +0.1751    -0.0047
        240  0.30    0.9957   -37.6    -68.1    -91.3    -87.3    1.322    +0.2627    -0.0058
        240  0.50    0.9928   -33.1    -60.3    -90.2   -102.2    2.220    +0.4378    -0.0052
        240  0.70    0.9888   -30.1    -54.3    -90.1   -104.6    3.138    +0.6129    -0.0009
        240  1.00    0.9810   -26.8    -47.8    -82.6    -92.5    4.567    +0.8755    +0.0123
        240  1.50    0.9627   -23.0    -40.3    -73.7    -79.9    7.127    +1.3133    +0.0534
        240  2.00    0.9372   -20.2    -34.8    -78.5    -68.6    9.964    +1.7511    +0.1188
        240  3.00    0.8369   -17.6    -23.5    -36.1    -48.4   14.834    +2.6266    +0.2667

    frequency rows at amp 0.30: 120/240/480 Hz H2 all -37.6, H3 all
    -68.1 (flat). determinism FNV64 fcef8457ddd50a85, two runs
    identical; harness verdict PASS.

    C onset at amp 0.50: h2/h1 -33.2 dB in the FIRST period, -33.1
    steady; 50 % and 90 % of steady both within 4.2 ms — the triode's
    static-curve onset (round 1's follower wobble is gone; the residual
    bias walk is the derived 0.037 seasoning).

The dc_img column tells the two-mechanism story directly: tiny and
negative at low level (the -0.037 env offset), crossing positive once
the curve's own rectification takes over (+0.267 at amp 3) — the
breathing the coupling cap now does is the reference's.

### Matched-THD residuals, stage vs reference

    THD anchor     H2 stage/ref     H3 stage/ref     gain-drop stage/ref
    ~1.3 %         -37.6 / -37.8    -68.1 / -69.5    -0.02 / -0.03 dB
    ~4.6 %         -26.8 / -26.6    -47.8 / -47.4    -0.15 / -0.15 dB
    ~10 %          -20.2 / ~-20.5   -34.8 / ~-35.9   -0.55 / ~-0.5  dB

(reference rows at 0.20 / 0.70 / ~1.30 V; ~ = interpolated between
sweep rows.) The stage now sits within ~0.4 dB on H2, ~1.4 dB on H3,
and a few hundredths of a dB on compression through the warmth window.
Round 1's deficits — the ~9 dB H2:H3 spacing, the flat low-level H2
floor, the 4-10x over-compression, the follower-lagged onset — are
gone; the H2-H3 spacing now runs 28-30 dB at warmth levels.

### The ears' A/B

`./build/exhibit_warmth render` — the M5 exhibit passage (low C-E-G,
percussion, swell closing to 0.3 at 3 s and reopening at 5 s), wear 0,
1/8 headroom, f32:

    build/warmth_ab_dry.wav      peak 0.805  FNV64 15cd7c4253da2f6e
    build/warmth_ab_old_035.wav  peak 0.601  FNV64 df4538333e643da1
    build/warmth_ab_new_035.wav  peak 0.876  FNV64 b455ac715cc3432e
    build/warmth_ab_old_070.wav  peak 0.335  FNV64 b1703918fbaa6d40
    build/warmth_ab_new_070.wav  peak 0.733  FNV64 7ccf3146c8150ddb

"old" is the M5 odd kernel + 0.5 depth — exact, via the kernel fork,
not a replica. Listening note: the derived stage compresses an order
of magnitude less, so equal-knob renders come out louder (the peak
column); level-match the monitor before judging warmth, and remember
the open sec 16 level-trim verdict belongs to the by-ear pass. What to
listen for at 0.35: the same passage rounder and wider, with the
percussive attack keeping its even glow instantly instead of fading
in; at 0.7: the old render's grit against the new render's push — the
new knob does audibly less damage at the same position, which is the
reference's own law, and whether the top of the knob should be
re-tapered is an open [decision], not a regression.

## Files

- `driver/spice/stage1.cir` — the AC reference battery;
  `driver/spice/curve.cir` — the static transfer sweep. Both under
  `make warmth-ref` (ngspice, dev-side only; never a build dependency).
- `driver/exhibit_warmth.c` — the shared analyzer, the kernel fit
  (`fit`), and the A/B renders (`render`); `make warmth`.
- `src/drive.c` — `tw_drive_curve` (the fitted table) and the kernel
  fork; `src/rotary.c` — the ceiling keeps the odd kernel.
- `build/spice/*.txt`, `build/warmth_ab_*.wav` — exports and renders
  (gitignored; regenerate with `make warmth-ref` / `make warmth` /
  `exhibit_warmth render`).
