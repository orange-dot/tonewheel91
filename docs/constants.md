# tonewheel91 — pinned machine constants (M0)

Date: 2026-07-16. Every number below carries a source tag or an explicit
**open** marker naming the milestone that must resolve it. No constant is
"known" without a source.

## Sources

Operator-local copies live in `docs/externalDocs/` (untracked, for copyright
reasons). Citation tags:

- **[P]** — US Patent 1,956,350 (filed 1934-01-19, granted 1934-04-24), the
  instrument family's founding patent. Page tags are printed spec pages.
- **[P39]** — US Patent 2,159,505 (filed 1937, granted 1939): paired
  detuned chorus generators for the upper registers on the pre-scanner
  models; complete gear/detune table (Figs 10/10a) and the inventor's
  borrowed-vs-natural-harmonic listening experiments.
- **[P44]** — US Patent 2,342,338 (Hanert, filed 1942, granted 1944):
  capacitor-discharge percussion generator — the concept ancestor of the
  console's percussion feature.
- **[P45]** — US Patent 2,382,413 (Hanert, filed 1943, granted 1945): the
  scanner vibrato — phase-shift line plus rotary capacitive pickup;
  depth, rate, and design rationale as primary source.
- **[SM]** — the manufacturer's service manual for the classic console models
  (with tone cabinet), 73-page edition; tags are printed paragraph/figure
  numbers (e.g. 5-26, Fig 5-2).
- **[ED]** — T. Wiltshire (Electric Druid), technical article on the
  tonewheel organ, 2008; includes the gear-ratio and foldback tables and the
  2022-2026 reader corrections.
- **[MW]** — C. Meyer, console manual wiring and tapering scheme (chart,
  tonewheel.de), published on the community organ wiki's "Manual Tapering"
  page; operator-local copy `keyboard-tapering-meyer.png`. Its author
  compiled it from an older factory tapering chart plus a wire chart —
  **secondary**: no factory chart was seen here directly. Sole source for
  the per-(key, bus) values in section 6. The wiki hotlinks it from the
  author's site, where it is now gone (301 to HTML); the local copy is
  byte-identical (md5 818f948b...) to the Internet Archive's 2022-01-21
  snapshot, which is the only retrievable original found.
- **[MF]** — M. Fulk's scan of a factory tapering diagram, "Standard
  Manual Keycircuit Tapering", same wiki page; operator-local copy
  `taper-scan-fulk.gif`. Isometric sketch: names the level ladder, the
  registers, and the double-back range — no per-key values.
- **[WIKI]** — the community organ wiki's "Manual Tapering" page text
  (last edited 2012; Dairiki et al., open publication licence).
  Uncredited prose; used for mechanism claims only, never for numbers.
- **[LB]** — the rotary-cabinet maker's product book (2007 edition,
  operator-local copy): configuration and behavior semantics, no numeric
  dynamics.
- **[RS]** — the rotary-cabinet maker's own service manual for the classic
  single-channel tube family (installation, service procedures, parts list,
  schematics); operator-local copy `rotary-cabinet-service-manual.pdf`
  (md5 b4dc543f...). **Primary** — a factory document, unlike [HX] and
  [LB]. Pins the drive mechanics and one transition time. Carries **no rpm
  figure, no motor pole count, and no pulley ratio**: it is an
  installation/service manual, not an engineering spec.
- **[HX]** — C. A. Henricksen, engineering article on the rotary cabinet,
  *Recording Engineer/Producer*, April 1981; operator-local copy
  `henricksen-1981-rotary-cabinet.html` (md5 2ece763a...; still live at
  theatreorgans.com, where [DAFx02] ref [3] points). A hands-on teardown
  with measured response curves by a loudspeaker engineer at Community
  Light & Sound — a **third party**, not the cabinet's maker, so this is
  **secondary**, but it is the technical description both [DAFx02] and
  [DAFx11] cite, and it is the closest thing to an engineering account we
  have. Pins construction and the crossover; **carries no rotor speeds**.
- **[KX]** — a rotary-cabinet dealer/restorer's teardown-overview video
  (2012, ~13 min; operator-local copy `rotary-cabinet-teardown-2012.mp4`,
  md5 10fbfa4a..., plus a de-duplicated `.transcript.txt`; source id
  `GkgQ6jU-4G4`). A hands-on strip-down of one classic
  single-channel cabinet with the rear panel off, plus a chorale/tremolo
  playing demo. **Corroboration of construction only** — a popular-level
  source, admitted as a *second witness* to [RS] and [HX], never as a
  primary pin, and it **carries no numbers** (no rpm, no crossover
  frequency, no transition time). It loosely narrates the drum's effect as
  "Doppler"; that is wrong and **does not** override [HX]'s AM-only drum.
- **[DAFx02]** — Smith, Serafin, Abel, Berners, DAFx 2002 (rotary-speaker
  Doppler simulation) — the designated reference for M6 rotary dynamics.
- **[DAFx11]** — Pekonen, Pihlajamaki, Valimaki, DAFx 2011 (efficient
  tonewheel-organ synthesis; construction-inaccuracy AM, rotary modulator
  rates).
- **[DAFx16]** — Werner, Dunkel, Germain, DAFx 2016 (wave-digital model of
  the vibrato/chorus line box and scanner, with measured component values
  and tap tables from a late-model console).
- **[AS16]** — Werner, Abel, Applied Sciences 6(7):185, 2016
  (modal-processor study: wheel waveforms, pickup nonlinearity,
  pseudo-harmonic cent errors, crosstalk audibility).
- **[ISMA19]** — Muenster, Pfeiffle, ISMA 2019 (high-speed-camera and
  oscilloscope measurements plus FEM of a 1938-vintage generator: wheel
  wobble AM, magnetization nonlinearity, pickup/filter spot values).
- **[derived]** — computed here from pinned constants; the derivation is
  shown. **[decision]** — a design choice, not a machine fact.
  **[FOLK]** — a widely-repeated community/encyclopaedia/forum figure with
  no primary source behind it, pinned as a *working default* to be
  overridden by ear or by a clean measurement. Tagged, never laundered
  into a fake derivation.

## 1. Machine clock

- Synchronous run motor: 1200 rpm on 60 Hz mains (2-pole field, 6-pole
  armature); the 50 Hz variant runs 1500 rpm with a 4-pole armature and
  different drive gearing [SM 5-12].
- Main drive shaft: **20 rev/s** [ED; verified: it makes wheel 46 exactly
  440 Hz]. This is the model's master clock; mains region does not change
  pitch.
- The motor drives through spring couplings that absorb its per-half-cycle
  pulsations [SM 5-13] — the physical basis of the "fixed but not rigid"
  phase relation between wheels. Modeled as nothing in v1 (wheels stay
  phase-locked); noted.
- Mains hum fundamental: default **60 Hz** (the reference-era machines and
  canonical recordings are 60 Hz units); one documented constant, switchable
  to 50 [decision].
- Hum level (M7): **1e-3 at wear = 1** (-60 dB vs a unit wheel) [FOLK],
  scaled by the `wear` knob, injected on the generator bleed bus so it
  passes scanner and swell with the leakage [decision]; exactly absent
  at wear = 0. The 50 Hz switch stays the `HUM_HZ` constant in
  `generator.c` pending a real control.

## 2. Gear table (12 driver/driven tooth pairs)

[ED; the patent's gearing chart, Figs 28/28a, is the primary origin [P].]

| note class | driving A | driven B | ratio A/B  |
|-----------|-----------|----------|------------|
| C         | 85        | 104      | 0.81730769 |
| C#        | 71        | 82       | 0.86585366 |
| D         | 67        | 73       | 0.91780822 |
| D#        | 105       | 108      | 0.97222222 |
| E         | 103       | 100      | 1.03000000 |
| F         | 84        | 77       | 1.09090909 |
| F#        | 74        | 64       | 1.15625000 |
| G         | 98        | 80       | 1.22500000 |
| G#        | 96        | 74       | 1.29729730 |
| A         | 88        | 64       | 1.37500000 |
| A#        | 67        | 46       | 1.45652174 |
| B         | 108       | 70       | 1.54285714 |

Store the integer pairs; compute ratios in code.

## 3. Wheels: teeth, frequencies, deviation from ET

- 91 wheels, numbered 1..91. Teeth per octave group: 12 wheels each of
  2, 4, 8, 16, 32, 64, 128 teeth, plus **7 wheels of 192 teeth**; five
  16-tooth wheels carry blank (inactive) shaft partners to preserve rotor
  balance [SM 5-5, Fig 5-2].
- Frequency: `f(n) = 20 rev/s x teeth(n) x ratio(class(n))`.
  - Wheels 1..84: `teeth = 2^(floor((n-1)/12)+1)`, `class = (n-1) mod 12`
    (0 = C).
  - Wheels 85..91: `teeth = 192`, `class = ((n-1) mod 12) + 5` — the top
    seven use the F..B ratios because 256-tooth wheels could not be cut
    [ED; SM 5-5].
- Verification anchors [derived]:
  - wheel 13 (lowest manual C) = 65.38462 Hz
  - wheel 46 (A) = **440.00000 Hz** exactly
  - wheel 73 (highest manual C fundamental) = 2092.3077 Hz
  - wheel 85 = 4189.0909 Hz, wheel 91 (top) = 5924.5714 Hz
    [ED reader-corrected values, 2022/2025 comments]
- Deviation from equal temperament: A-class wheels exact; worst case in
  wheels 1..84 is the G# class, 0.69 cents flat; in the top seven, wheel 86
  is the worst at about +1.93 cents [ED]. Unit test: table within f32
  tolerance.
- Drawbar-interval identity (the "rational intonation" signature): octave
  drawbars are exact powers of 2; the sub-3rd/3rd/6th sit at 1.49882353 /
  2.99764706 / 5.99529412 x fundamental; the 5th at 5.04094118 x
  (~+14 cents vs the true 5:1) [ED]. These emerge from the gear table; the
  table here is test data, not separate constants. The ideal-ET share of
  those errors is exactly **+13.686 cents** on the 5th-harmonic drawbar and
  **-1.955 cents** on the 12th/19th [AS16 Table 1]; the remaining fraction
  of a cent is gear rounding [derived]. The inventor's own listening
  experiments [P39 p.2]: a borrowed (tempered) harmonic is
  indistinguishable from an exact one when used alone; both together beat
  slowly; sharing one generator across notes yields "a certain effect of
  purity and simplicity" in complex music — the primary-source statement
  of the phase-coherence identity the M1 founding exhibit demonstrates.
- Wheels 1..12 (2 teeth) are pedal wheels cut to a complex, squarish
  profile with 3rd and 5th harmonic content [ED]; citable waveform in
  section 12. Out of scope for the 61-key manual model (they are below
  the manual foldback floor); noted for a future pedal division.

## 4. Compass and foldback

- 61-key manual, keys k = 1..61, MIDI notes 36..96 [decision, matching the
  machine's compass]; fundamental of key k is wheel `k + 12` (13..73).
- Drawbar semitone offsets relative to the fundamental, left to right
  (16', 5-1/3', 8', 4', 2-2/3', 2', 1-3/5', 1-1/3', 1'):
  **-12, +7, 0, +12, +19, +24, +28, +31, +36** [ED].
- Foldback rule: `w = 13 + (k-1) + offset`, then fold by octaves into
  **[13, 91]** (add 12 while below 13; subtract 12 while above 91)
  [ED; SM Fig 4 per the article's 2026 correction note].
- Verification table (wheel ranges the rule must reproduce over k = 1..61)
  [ED]: 16' spans 13-61 (keys 1-12 fold up to 13-24); 5-1/3' spans 20-80
  (no fold); 8' 13-73; 4' 25-85; 2-2/3' 32-91 then top key folds to 80;
  2' 37-91 then 80-85; 1-3/5' 41-91 then 80-89; 1-1/3' 44-91 then 80-91
  then 80; 1' 49-91 then 80-91 then 80-85 (the last two revisit wheels
  80-85 twice — expected, not a bug).

## 5. Drawbar step curve

- Each bus feeds a tap on the matching transformer; taps sit at
  **6, 8, 11, 16, 22, 32, 45, 64 turns** for digits 1..8 — the nearest
  whole numbers to a geometric series with ratio sqrt(2) [P p.12].
- Digit-to-gain: `g(d) = turns[d] / 64`, `g(0) = 0` (open circuit):
  0.09375, 0.125, 0.171875, 0.25, 0.34375, 0.5, 0.703125, 1.0.
  Each step is ~+3 dB (doubled power) [P p.12: "substantially doubling the
  intensity"]. Drawbar digits mark "a progressive increase in intensity,
  0 (fully pushed in) to 8" [SM 4-3].
- One matching transformer per manual, high-impedance secondary to the
  preamp [SM 5-33..5-35, Fig 4-1].

## 6. Contact/busbar network, taper, robbing

- Nine contact springs per key; resistance wires connect generator
  terminals to the contacts; **all key contacts are live at all times**;
  a depressed key impresses its nine frequencies on the nine busbars
  [SM 5-32]. This is why closing a contact clicks: it switches a running
  signal.
- Network values (patent worked example): generator winding ~4 ohm,
  series resistance wire ~15 ohm, reflected busbar/transformer load
  ~1 ohm [P p.13, "Elimination of robbing"].
- Consequence, same wheel tapped k times on one bus [derived from P p.13]:
  the k wires sit in parallel from one source, so the summed contribution
  is `a(k) = i_k / i_1 = (Rg + Rw + Rb) x k / ((Rg + Rb) x k + Rw)`; with
  4/15/1 ohm that is `a(k) = 4k / (k + 3)`: a(1)=1.00, a(2)=1.60,
  a(3)=2.00, a(4)=2.29 — additions merge rather than sum. Distinct wheels on a bus sum ~linearly
  (independent sources through their own high resistances into a low-Z
  node). The audible "compression" as keys stack comes from foldback and
  shared harmonics making same-wheel collisions common. Model: since M3
  the closed contacts of each (bus, wheel) fold through the general
  parallel-network ratio law over their per-key taper wires (section
  6.1); `a(k)` is that law's equal-wire special case, kept here as the
  derivation.

### 6.1 Taper

Per-(key, bus) resistance-wire classes shaping registration balance across
the compass. The wires exist [SM 5-32, Fig 4-3 "resistance wires"] but this
manual edition does not tabulate values. **Pinned at M2 from [MW]**;
provenance is the weak point — one secondary chart, corroborated in shape
and ladder but not in values by [MF]. A factory wiring chart landing in
`docs/externalDocs/` would upgrade this from pinned to primary.

Six wire classes [MW legend; same ladder, unnumbered, in [MF]]:

| wire     | level   | where |
|----------|---------|-------|
| 10 ohm   | +7 dB   | treble end of 16'/5-1/3'/8', bass end of 2-2/3'/2' |
| 15 ohm   | +3.5 dB | the patent's nominal series wire [P p.13] |
| 24 ohm   | 0 dB    | reference — [MF]'s "normal loudness level" |
| 34 ohm   | -3.5 dB | |
| 50 ohm   | -7 dB   | |
| 100 ohm  | -10 dB  | 16' keys 1-10 only, inside the foldback octave |

Full table, run-length over keys k = 1..61 (section 4 numbering), each
entry `keys:ohm` [MW, read off the chart's cell colours]:

| bus    | runs |
|--------|------|
| 16'    | 1-10:100, 11-16:50, 17-24:34, 25-36:24, 37-48:15, 49-61:10 |
| 5-1/3' | 1-14:34, 15-38:24, 39-50:15, 51-61:10 |
| 8'     | 1-15:50, 16-23:34, 24-37:24, 38-49:15, 50-61:10 |
| 4'     | 1-13:34, 14-39:24, 40-61:34 |
| 2-2/3' | 1-12:10, 13-20:15, 21-40:24, 41-52:34, 53-61:50 |
| 2'     | 1-11:10, 12-20:15, 21-41:24, 42-55:34, 56-61:50 |
| 1-3/5' | 1-18:15, 19-42:24, 43-51:34, 52-61:50 |
| 1-1/3' | 1-43:24, 44-48:34, 49-61:50 |
| 1'     | 1-43:24, 44-61:50 |

All 549 cells (9 x 61) resolve to exactly one class — no cell is ambiguous.

- Shape [derived from MW]: the three low buses (16', 5-1/3', 8') tilt **up**
  toward the treble (-10..-3.5 dB in the bass, all three reaching +7 dB at
  the top); the five upper-harmonic buses (2-2/3' .. 1') tilt **down**
  (0..+7 dB in the bass, all five reaching -7 dB at the top); 4' is the odd
  one out — a shallow dip (-3.5 dB at both ends, 0 dB across the middle 26
  keys). Net: bass notes get their harmonics pushed up, treble notes get
  their fundamental pushed up and their harmonics pulled down.
- Purpose [WIKI]: pre-emphasis on the harmonics plus a preamp rolloff, to
  bury key click — so taper is part of the same click strategy as section 7,
  not an independent voicing knob.
- [MW]'s author further reads the classes as compensating per-wheel generator
  impedance, "therefore the jump in tapering wire resistance at notes with
  filter circuit". **Checked against section 11's conditioning zones and not
  confirmed** [derived]: of the 28 run breaks in the table, only 2 land on a
  zone boundary (16' at wheel 49, 2-2/3' at wheel 44) — about what chance
  gives for 3 boundary wheels out of 79. Recorded as the chart author's
  interpretation, not adopted as a mechanism.
- The -10 dB class is used in exactly **10 cells**, all of them 16' keys
  1-10: an extra pad inside the doubled-back bottom octave. It stops two keys
  short of the fold boundary — 16' keys 11-12 fold back too, yet sit at -7 dB
  alongside the unfolded keys 13-16 — and it does not track the wheels either
  (wheels 13-16 are 100 ohm via keys 1-4 but 50 ohm via keys 13-16). No
  explanation for the key-10 break is in hand; recorded as read. [MF] marks
  the same "double-back range" as specially treated.
- Untapered variants exist: the earliest console model shipped with no taper
  at all ("all harmonics at reference loudness" [MF]); spinets use a constant
  wire (16 ohm on one series) [WIKI]. Taper is a console-vs-spinet voicing
  difference, not an incidental one [ED reader comment, 2018].
- **Not a per-wheel function** [derived] — the tempting collapse, refuted:
  over the 79 wheels the manual reaches, every one carries 2-3 different
  classes (wheel 13 is 100 ohm via 16'/key 1, 50 ohm via 16'/key 13, 50 ohm
  via 8'/key 1). The table stays per-(key, bus); it does not reduce.
- **The two columns disagree** [derived]: the network above (Rg 4, Rb 1)
  gives `29/(5+Rw)` = +5.7, +3.2, 0, -2.6, -5.6, -11.2 dB for the six classes
  in table order, against the chart's +7, +3.5, 0, -3.5, -7, -10. Ordering
  agrees, magnitudes do not, and the gap widens at both extremes. Which
  column is the achieved level is **not known here**: the ladder's roundness
  (3.5 dB steps, the same values written on [MF]) suggests nominal design
  labels rather than measurements, while the network figure rests on
  Rg = 4 ohm from one patent worked example and the true per-wheel Rg is not
  in hand.
- **Level authority is the dB column** [decision, not a machine fact]: it is
  the one thing [MW] and [MF] independently agree on, and the alternative
  needs a per-wheel Rg we do not have. Rw stays the authority for the robbing
  network. Revisit if a factory chart or a measured Rg lands.
- Consequence for robbing, **landed at M3** [derived + decision]: the core
  folds the closed contacts i of each (bus, wheel) through the ratio form
  of the parallel network — `Rpar = 1/sum(1/Rw_i)`, `merge_ratio =
  [1/(R0 + Rpar)] / sum_i 1/(R0 + Rw_i)` with `R0 = Rg + Rb = 5 ohm`, and
  `contribution = merge_ratio x sum_i g_i` over the dB-column gains. The
  ratio form is what reconciles the two authorities [decision]: a single
  closed contact has merge_ratio == 1 and passes its dB-column gain
  exactly, so level authority stays the dB column while robbing authority
  stays Rw. Equal wires collapse it to a(k) — the old law is the special
  case, not a rival (no real collision site has equal wires, so a(k) was
  right only for a taperless machine). Verified in `test.c` against a
  double-precision oracle; measured across all 53 sites by the M3 exhibit.
- Sizing the correction [derived; measured by `exhibit_taper`]: there are
  **53 (bus, wheel) sites** where two or three keys of one bus reach the
  same wheel — 16' (12), 1' (12), 1-1/3' (12), 1-3/5' (10), 2' (6),
  2-2/3' (1); seven are three-key. All sit in the foldback region, and the
  foldback region is where the table puts its **high-resistance** classes
  (24-100 ohm), which rob weakly: real merge lands at **0.82-0.94** of the
  naive sum against the flat placeholder's 0.67 (k=3) / 0.80 (k=2). The
  M2 model **over-robbed every one of the 53, by +0.97..+1.78 dB (mean
  +1.19)** — a systematic error, loudest exactly in the top octaves.
  Robbing is strongest for low Rw; the placeholder's flat Rw = 15 (the
  +3.5 dB class, not even the 24-ohm reference) robbed hardest where the
  machine robs least.
- **What that does NOT mean** [derived; A/B measured against the retired M2
  core, which is in git at 125678a]: the +0.97..+1.78 dB is a **ratio** at
  collision sites, not an audible lift. The same change also applies taper,
  which pads the upper-harmonic buses at the treble end by up to -7 dB — and
  the collision sites are exactly there. The two effects largely cancel. On
  a full 888888888 registration the measured M3-vs-M2 change is **-0.24 dB
  on a top chord** (i.e. unchanged) and **+1.61 dB on a bottom chord** —
  the audible lift lands in the **bass**, where the upper-harmonic buses
  carry the +7 dB classes. Isolated, the 1' triple on wheel 80 lands 2.22 dB
  **quieter** than M2 (its robbing is weaker, but two of its three keys are
  -7 dB wires). Taper redistributes; it does not uniformly lift.
- Corroboration for section 4 [derived]: the chart's cell numbers are wheel
  numbers, and where spot-checked they reproduce the foldback table exactly
  (16' keys 1-12 -> wheels 13-24 then 13..61; 8' 13-73; 4' 25-85; 5-1/3'
  20-80). Independent of [ED], which is where that table came from.

## 7. Key click

- Mechanism pinned: nine contacts do not close simultaneously; bounce and
  contact dirt cause rapid re-switching of a live signal; the machine
  even carried an RC by-pass specifically to tame "key click" [P p.12;
  ED]. Click is high-frequency transient content, not an added noise
  sample. The taper (section 6.1) is the third leg of the same strategy —
  pre-emphasis into a preamp rolloff [WIKI] — so click, taper, and preamp
  voicing cannot be tuned independently of each other.
- Numbers **open — tuned at M2**: starting points are <= 3 toggles inside
  0-2 ms per contact, per-wheel gain smoothing time constant 0.2-1.5 ms
  (the smoothing constant is what shapes click brightness).
- Velocity-to-contact-stagger (slow press engages the nine buses over
  ~0-15 ms) [decision — playability model; the machine does this via key
  travel, and MIDI 1.0 has no continuous key-position message]. Loudness
  stays velocity-independent: contact closure is binary [SM 5-32 by
  construction]. Section 7.1 takes the same stack the other way round.

### 7.1 Key depth

The nine springs of a key are a stack, not a switch: they meet their
busbars at nine points along the travel, so how far a key is held decides
how many of its frequencies reach the manual [SM 5-32 by construction —
nine springs at nine heights is what the drawing shows; the machine's own
players call the resulting sound a half-press]. Section 7 already models
that stack in *time* (a slow press engages the buses over ~0-15 ms).
Depth models the same stack in *position*, which is the physically prior
quantity: velocity stagger is what depth looks like when the only thing
known about the press is how fast it was.

- **Make-point spacing: even, nine points over the travel** [decision].
  The stagger model already spreads the buses linearly over the press
  (`t = span x b / 8`), which is a constant-speed descent through evenly
  spaced contacts; depth uses the same spacing so the two agree. The real
  spring heights are not tabulated in [SM]; a factory keybed drawing
  would upgrade this.
- **Closure order: bus 0 -> 8**, the same order the stagger walks
  [decision, following section 7]. Order within the stack is a mechanical
  fact this edition does not give either.
- **Make/break band: +-4 of 128** [decision]. A wiping spring contact
  breaks lower than it makes, so a band is the mechanism and not only a
  numerical guard — but the width here is chosen for the control problem,
  not measured: nine steps over a 0..127 travel put the make points ~12.8
  apart, and a finger parked on one would otherwise chatter its contact
  at whatever rate the surface reports. The band is ~1/3 of a step,
  leaving every make point reachable from both directions with margin
  (full scale 127 makes all nine with 8 counts to spare).
- **Carrier: poly key pressure (0xA0)** [decision]. MIDI 1.0 has no
  key-position message; poly pressure is the only per-note continuous
  channel it has, and this is what it is being used for here. A MIDI 2.0
  per-note controller is the honest carrier if the transport ever moves
  (see `docs/gesture-control.md`).
- Consequences that fall out of the existing model, not added to it:
  - The section 6/6.1 merge law already folds an arbitrary set of closed
    contacts per wheel, foldback collisions included, so depth changes
    **only which cells of the contact matrix are closed**. No new signal
    path exists for it — asserted by `exhibit_depth`, which measures each
    made bus passing exactly its full-press contribution.
  - **The ninth contact is stolen while percussion is on** (section 8):
    the 1' bus is the trigger-sensing line, so the top step of the travel
    goes silently out of service for the sustained tone. Depth 8 and
    depth 9 are then bit-for-bit the same sound — while *also* being the
    step that fires percussion. With percussion on, the top of the travel
    is a pure trigger control and nothing else.
  - **A half-press does not fire percussion**, for the same reason and
    with no extra rule: section 8's trigger is that contact, and the
    travel never reached it. The envelope stays armed for whoever does
    bottom out.
  - Depth 0 is a held key with every contact open — **not** a note-off.
    Note-off stays authoritative, and a key that is not held ignores
    depth entirely.
- **Open, by ear**: the spacing, the band width, and the feel of the
  ninth contact now that it both steals the 1' bus and fires percussion.
  Nothing in this subsection is measured.

## 8. Percussion

- Sourced behavior [SM 5-47..5-52]:
  - Single generator-wide envelope; sounds only on a detached (staccato)
    key-down; **no retrigger while any manual key is held; re-arms only
    after all keys are released** — legato playing keeps it silent
    [SM 5-52]. The re-arm has an RC rate (grids recover -15 V -> -25 V);
    that rate is now derived below.
  - Trigger sensing line is the **8th-harmonic (1') drawbar wire**, at
    about **-25 V** (terminal K) [SM 5-51]. Pressing any key connects the
    busbar to a generator terminal and "virtually grounds" K through the
    generator filters; that grounds the plate of V6, stops its conduction,
    and thereby **isolates the control-tube grid circuit** — which is what
    lets C31 start drifting. So the key does not gate the audio; it
    releases the envelope by cutting the clamp that held the grid at
    -25 V. Hence the quirk: while percussion is enabled the 1' drawbar is
    out of service. Our model mutes the 1' bus contribution when
    percussion is on.
  - **The trigger is that contact, landed post-M7** [derived from the
    above]. Until the key-depth pass the model fired the envelope on the
    note event, which is only the same thing when every press bottoms
    out. It does not: the nine contacts close over the velocity stagger
    (~0-15 ms) and the 1' contact closes **last**, and under section 7.1
    a press may stop short of it entirely. So the state machine now
    follows that one contact — closing any key's ninth contact grounds K
    and releases the envelope, and while any of them is closed the grid
    stays clamped, which *is* the single-trigger rule rather than a
    separate rule about keys. Three consequences, all measured in
    `docs/depth-evidence.md`: the trigger arrives at the end of a slow
    press instead of at its start; a half-press never fires it and
    leaves the envelope armed; and riding a held key back onto its ninth
    contact retriggers percussion with no note event at all.
  - **Re-arm is now modeled, and had to be** [derived]: with the trigger
    on the sensing line, that line's own contact **bounce** (section 7,
    <= 3 toggles inside 2 ms) would retrigger the envelope several times
    per press if recovery were instant. The 34 ms RC below is what makes
    one press one hit — it sits an order above the bounce window and two
    orders below a playable staccato gap, so it separates them cleanly.
    Reading recovery as one tau is [decision].
  - Source signal: the **2nd or 3rd harmonic bus, borrowed pre-drawbar**
    from the upper-manual "B" drawbar group, amplified, with part returned
    to the same drawbar through a third winding on the input transformer
    T5 via "equivalent key circuit resistor R50" [SM 5-48/5-49] — so a
    drawbar at 0 still feeds percussion, and the sustained tone is
    slightly affected while percussion runs.
  - **R50 = 22 ohm** [SM schematic] — note the unit: ohms, not kilohms.
    That is the point of the manual's word *equivalent*: 22 ohm sits
    inside the taper wire range (10-100 ohm, section 6.1), so the return
    leg is deliberately built to look like one more key-circuit wire on
    that busbar. Consequence [derived]: the NORMAL sustained-tone
    attenuation is **not a free by-ear constant** — it should fall out of
    the same parallel-network law section 6.1 already pins for robbing,
    with R50 as an extra 22-ohm wire on the borrowed bus. **Open at M3**:
    do that derivation and check it by ear, rather than dialing a number.
  - Decay [SM 5-50/5-51 + schematic; **corrected — an earlier reading of
    this section had the resistors backwards**]: pressing a key isolates
    the control-tube grid circuit, and **C31 = 0.33 uF** discharges
    "through R57 and R58" [SM 5-51 verbatim], drifting the grid from about
    -25 V to about -15 V, at which point the percussion signal is blocked.
    R58 = 4.7 Mohm sits permanently across that path; R57 = 1.5 Mohm is
    switched in parallel by the two-pole DECAY tablet. So [derived]:
    - **SLOW = R58 alone = 4.70 Mohm -> tau = 1.551 s**
    - **FAST = R57 || R58 = 1.137 Mohm -> tau = 0.375 s**
    - **ratio 4.133 : 1** — this replaces the ~3.7 : 1 pinned here before,
      which came from R55/R56 (82k/22k). Those are the **re-arm** path
      [SM 5-52], not the decay path. Which contact is FAST is [derived]:
      paralleling R57 lowers R, so that must be the fast one.
    - Corroboration that the correction is the right way round: the
      by-ear targets this section carried before the correction were
      ~1.0 s slow / ~0.25 s fast, i.e. **4.0 : 1** — which matches the
      derived 4.13 and not the pinned 3.73. The ear was right and the
      pinned number was wrong.
  - Absolute audible decay is **a range by design, not one constant** —
    and **this model does not close it. Do not trust the seconds below.**
    [SM 4-4] gives a service procedure for it: with the expression pedal
    open, both volume tablets NORMAL and percussion ON, hold any key in
    the upper half of the manual for at least 5 s and set the PERCUSSION
    CUTOFF control "exactly to the point where the signal becomes
    inaudible" — readjusted whenever V7 is replaced. So the decay is
    trimmed per unit against an audible threshold, which is why [SM]
    states no absolute time anywhere.
  - What can be said [derived]: the fade is the grid's *partial* drift
    -25 V -> -15 V toward a target Vf set by **R60, a 30 kohm pot across
    +33 V and ground with its wiper feeding R58**, so Vf is reachable only
    in **[0, +33 V]** and `t = tau * ln((-25-Vf)/(-15-Vf))` gives
    **0.29-0.79 s (slow) / 0.07-0.19 s (fast)** across the pot's travel,
    longest at the grounded end.
  - **Three things do not reconcile, and the gap is the tube**:
    (a) the by-ear target this section carried, ~1.0 s slow, sits *above*
    the reachable 0.79 s maximum; (b) [SM 4-4]'s procedure implies a
    setting that only *just* reaches cut-off, but on this model every Vf
    in [0, +33] reaches -15 V and cuts off, so the procedure would have no
    threshold to find; (c) a grid drifting *less* negative driving *less*
    signal is backwards for an ordinary triode, so V7's gate behavior is
    not the simple thing assumed here. Conclusion: the RC values and the
    **4.133 : 1 ratio are solid** (they are resistor ratios and do not
    depend on any of this), but **tau -> audible seconds is not derived**.
    **Open at M3**: pick a slow decay by ear, derive fast = slow / 4.133,
    mark it [decision]. Closing this properly needs V7's transfer
    behavior or a measurement on a real unit — not more arithmetic.
  - Method note, recorded because it nearly shipped: the first write-up of
    this bullet picked Vf = 0..-5 V "because it gives ~1.0 s". That is
    fitting the input to a wanted answer, and -5 V is not even reachable
    on the divider. The ratio survived only because it never depended on
    Vf.
  - Re-arm rate [SM 5-52 + schematic; **was open, now derived**]: the
    grids drop back to -25 V at "the time required to charge C31 ...
    through R55 and R56" [SM 5-52] — 82k + 22k = 104 kohm, so
    **tau ~= 34 ms**. Two orders below the decay: re-arm is essentially
    immediate on release, and detachment — not recovery time — is what
    gates the next hit. Note this **kills the [P44] ~8 notes/s (~125 ms)
    figure as an anchor**: that is the ancestor circuit, and it is 4x out.
    **Landed post-M7**, with the contact-driven trigger above: what was
    "essentially immediate" is now the actual constant, because the
    sensing line's own bounce lives at 2 ms and needs separating from a
    real detachment. Measured threshold: two notes 0/10/20 ms apart give
    one hit, 40/100 ms apart give two (`exhibit_percussion`).
  - SOFT/NORMAL: level pad on the percussion channel plus the NORMAL-mode
    sustained-tone effect above; a two-position tablet [SM 5-48]. The pad
    parts are now named — **R46 = 12 kohm** switched at PERCUSSION VOLUME,
    with **R59 = 120 kohm** and **R51 = 4.7 kohm** in the same leg
    [SM schematic] — but the divider topology is **still not resolved**
    from the prose alone: reading a schematic, not more arithmetic on the
    values in hand, is what closes this. **Landed at M3 as two separate,
    honestly-scoped pieces** rather than waiting on the divider:
    - **NORMAL sustained-tone attenuation [derived]**: R50 (22 ohm,
      already pinned above) folded as one more, signal-free, parallel leg
      into the *same* section 6/6.1 merge network that already carries
      taper and robbing — `PERC_NORMAL_ATTEN = R_SRC / (R_SRC + R50) =
      5 / 27 ~= 0.8148` (-1.77 dB), applied to the wheel the percussion
      channel is currently tapping. This is a simplification against the
      full per-taper-class recomputation (a single fixed ratio rather
      than folding R50 in per the exact merge law for whatever class
      happens to be playing there) — close enough to be worth landing,
      not claimed to be the last word; docs/m3-evidence.md carries the
      caveat.
    - **SOFT pad on percussion's own peak [decision]**: `PERC_SOFT_PAD =
      0.5` (-6 dB), a round-number placeholder — **not** derived from
      R46/R59/R51, which stay open exactly as above. Halves the trigger
      peak until whoever has the schematic in hand pins the real ratio.
  - Four tablets total, so model four controls, not one enum:
    ON/OFF, SOFT/NORMAL, FAST/SLOW, THIRD/SECOND harmonic [SM 5-48].
    Live-driver CC map (M3, `driver/main.c`): CC80 on/off, CC81
    2nd/3rd harmonic, CC82 fast/slow decay, CC83 soft/normal volume;
    value >= 64 is each toggle's second-named position.
  - Percussion trigger peak amplitude: `PERC_PEAK_GAIN = 1.0` **[decision]**
    — matches the 0 dB taper reference class (section 6.1); the manual
    gives no absolute figure since the borrowed-bus transformer gain is
    not modeled to that depth. Tune by ear once played against reference
    recordings.
- Concept lineage [P44]: the original percussion generator is a
  normally-charged capacitor discharged through a key into a resonant
  mesh, distorted to a "square boxlike" pulse by a sharp-cutoff pentode.
  Already present there: silent release (recharging through 500 kohm
  runs at ~1/2500 of the discharge rate, so key-up makes no transient),
  repetition limited by recharge (~8 notes/s max), and the observation
  that rolled chords still read as chords because contacts never close
  simultaneously — the same physics our stagger model plays on. The
  console's bus-borrow circuit [SM] supersedes it; the
  discharge-then-recharge DNA is visible in both.
- Percussion joins the chain **before the scanner** (it rides the manual
  signal into the preamp's vibrato channel) [SM 5-30/5-49 chain reading];
  kept behind one documented constant in code.

## 9. Vibrato/chorus scanner

Fully specced by [DAFx16] (component values and tap tables measured from a
late-model console's line box) on top of the [SM] behavior description. The
line is a real filter network, not an ideal delay — its lowpass edge,
moving ripple, and dispersion are the effect's signature and come free from
modeling the actual circuit.

- Line: **18 LC lowpass sections** — L1..L18 = **500 mH** each; C1..C17 =
  **0.004 uF**, C18 = **0.001 uF**; termination Rt = **15 kohm**
  [DAFx16 Table 1; SM Fig 5-8 agrees on 18 sections and ~0.004 uF].
- Input feed: series resistor **Rc = 22 kohm**, shorted by the switch in V
  modes, in circuit for C modes [DAFx16] — the same component as [SM]'s
  R44; the two sources describe one part.
- Stage voltage dividers (stages 1..6 only): R1+ 27k / R1- 68k; R2+ 56k;
  R3+ 39k; R2-, R3- 150k; R4+ 33k; R5+ 18k; R6+ 12k; R4-..R6- 180k
  [DAFx16 Table 1]. Resulting per-tap gains: **-2.9, -2.8, -2.0, -1.5,
  -0.83, -0.56 dB, then 0 dB for taps 7..19** [DAFx16 Table 3] — a
  built-in AM ramp across each scan cycle.
- Tap tables (line nodes v1..v19 -> scanner terminals t1..t9)
  [DAFx16 Table 2]:
  - **V1/C1**: v1 v2 v3 v4 v5 v6 v7 v8 v9
  - **V2/C2**: v1 v2 v3 v5 v7 v9 v11 v12 v13
  - **V3/C3**: v1 v2 v4 v7 v10 v13 v16 v18 v19
  These supersede the earlier "V2 ~ half, V1 ~ third" reading of [SM 5-22]:
  spacing is non-uniform, denser near both ends.
- Line character [DAFx16 measurement/SPICE]: passband edge ~**7075 Hz**;
  passband ripples ~6 dB and deeper whose positions move over the scan
  cycle (audibly phaser-like on broadband input); idealized total delay
  sqrt(sum L x sum C) ~ **0.85 ms**; the impulse smears progressively
  along the line (dispersion) [SM 5-17 agrees qualitatively].
- Scanner: 16 fixed plate stacks around the rotor, wired t1..t9
  there-and-back, so one revolution = one full triangular sweep. The rotor
  crossfades adjacent terminals as a capacitive divider
  (eta = C_a/(C_a + C_b)), gain triangular in angle — effectively **linear
  interpolation**, whose comb notches between taps are part of the sound
  (an allpass-interpolated delay is the wrong model) [DAFx16]. Rate: on
  the run motor at **412 rpm = 6.867 Hz** [SM 5-26]; literature rounds to
  ~6 Hz.
- Depth and rate, primary source [P45 p.1]: "a cyclical shift in
  frequency of approximately 1.5%, at a rate of about 6 per second". The
  patent's worked example lands at **+/-1.4% ~ +/-1/4 semitone** for the
  full line at 6 rev/s, with medium = 3/4 and small = 1/4 of the large
  extent (the production tap tables [DAFx16] supersede those proportions
  for our machine). Rate window: the claims pin **5-8 cps**; guidance in
  the text: nearer 7 for fast staccato, nearer 5 for slow legato.
  [SM 5-20]'s ~1.5% and 412 rpm = 6.867 Hz sit inside all of it; [AS16]
  restates typical depth as ~25 cents. Tension [derived]: 0.85 ms x 13.7
  legs/s = 1.16%, while exactly 1.5% needs 1.07 ms of line. Resolution:
  the model builds the actual ladder from the component values above, so
  depth *emerges* from the circuit; acceptance band for V3 is
  **1.1-1.6%**. M4 measurement (48 kHz, 1 kHz tone, zero-crossing
  instantaneous frequency): cyclical depth **+/-1.48%** (32 ms smoothing,
  the [P45]-comparable figure — in band); peak deviation **+/-1.91%**
  (8 ms smoothing) because the low-impedance V-mode drive sets up a
  standing wave in the mismatched line whose moving ripple superposes on
  the sweep — the same ripple [DAFx16] measures, audible as the
  phaser-like motion, so the excess over 1.6% is signature, not error.
- Mechanism intent [P45]: nine pickup points are a deliberate economy —
  picking the signal "from nine equally spaced sections" sounds
  "substantially identical" to scanning every section; the capacitive
  crossfade makes tap transitions "gradual rather than abrupt"; and
  phase shift per section grows ~linearly with frequency below cutoff,
  which is exactly what makes the *percentage* shift uniform across the
  scale (constant line delay).
- Bass bypass [P45]: components below ~**80 Hz** are routed around the
  line straight to the amplifier — vibrato is deliberately "limited to
  frequencies above the bass tone range". The manual compass starts at
  65.4 Hz, so this trims only the lowest keys' fundamentals. **Included
  at M4**, realized as an exact wheel split [derived]: the manual sum's
  spectral content below 80 Hz is precisely the fundamentals of wheels
  13..16 (section 3: wheel 16 = 77.8 Hz, wheel 17 = 82.4 Hz), so the
  keyed sum over wheels 1..16 bypasses the line with no crossover filter
  at all — zero leakage, zero phase error, and a sub-80 Hz-only
  registration renders bit-identically with vibrato on or off.
  Percussion's lowest tap is wheel 25, so all of it is line-side.
- The patent's worked line (32 T-sections, 13.7 kHz cutoff, 0.186 H,
  ~8 kohm characteristic, 180 deg/section at cutoff) is an earlier,
  longer design; the production line we model is the measured
  18-section one [DAFx16]. For a dual-manual future, [P45] suggests
  *different* scan rates per channel (e.g. 5.8 / 6.2 cps).
- Chorus modes C1..C3: dry component mixed with the swept line via the
  22 kohm resistor above [SM 5-23]. Settled at M4 [decision]: a
  behavioral equal-amplitude mix, **out = (dry + swept) / 2**, on the
  [P39] precedent (the pre-scanner chorus ran its detuned pair at equal
  power with the solo wheel). The circuit-level feed — driving the line
  through Rc against its ~11.2 kohm image impedance, with the dry path
  meeting at the matching transformer — is the named upgrade if the ear
  objects; the component value stays pinned above.
- Chorus voicing reference [P39]: the pre-scanner chorus was a second
  generator with gear-realized detuned pairs for wheels 56..91 only —
  detune "two to five parts in a thousand" (the table works out to
  ~0.25-0.53%, i.e. ~4-9 cents, slightly asymmetric pairs), at **equal
  power** with the solo wheel, and deliberately **treble-only** ("not as
  musically desirable" lower down). M4 voicing target for the C modes:
  perceived detune magnitude in that band, treble-biased — consistent
  with the bass bypass above. [P39 p.5] also names the room-pattern
  motive: three near-frequencies defeat position-dependent room nulls.
- The machine's vibrato switch has no OFF (off = per-manual tablets)
  [SM 5-29]; our `off` mode models the tablet.
- Implementation note [decision, chosen at M4]: trapezoidal (bilinear)
  nodal model of the real ladder — eliminating the inductor updates
  leaves one constant-coefficient tridiagonal solve per sample, factors
  computed at init (no libm). **No prewarp**: plain bilinear keeps low-
  and mid-band group delay exact, so the emergent V3 depth lands in the
  acceptance band above; the cost is the passband edge warping ~6% low
  at 48 kHz (measured last -6 dB point 6575 Hz), converging toward the
  analog ~7075 Hz as the rate rises (~7.1 kHz at 192 kHz). The line runs
  on the mono manual sum, so 18 sections cost noise (~300 flops per
  frame). Percussion joins before the scanner (section 8).
- MIDI [decision, M4]: **CC84** selects the mode, value/19 ->
  off, V1, V2, V3, C1, C2, C3 (extends the M2 map).

## 10. Swell (expression)

- Capacitive divider: rotor meshes with "LOUD" and "SOFT" stator sets;
  SOFT path passes through a compensating network (loudness-style tilt at
  low volume) [SM 5-45]. Curve and tilt: **open — tune at M2** (control
  lands with the live driver; CC11).

## 11. Per-wheel output conditioning (level & character basis)

- Three conditioning zones on the generator [SM 5-9..5-11]:
  - wheels <= 43: resistance-wire shunt wound on the magnet coil (no
    filter);
  - wheels 44..48: reactor (more turns), no capacitor;
  - wheels 49..91: tuned LC resonant at the fundamental — caps 0.255 uF
    (49..54) and 0.105 uF (55..91).
- Copper rings on certain low-frequency coils suppress coil harmonics
  [SM 5-7]; larger coils on low wheels, smaller on high [SM 5-6].
- Factory intent is equal loudness across the compass, trimmed per wheel
  by magnet position [P p.13; SM 3-25/4-6 alignment procedures]. Model:
  `level_profile` nominal flat (post-conditioning), per-unit spread and
  any zone coloration land with `wear` — **open, M7**.
- Measured spot values for one wheel pair (#9 / #57 on a 1938 unit):
  pickup inductances **15.52 / 14.35 mH**; following filter **100 nF**
  across **73 mH / 153 mH** transformer windings [ISMA19 Fig 1] — anchors
  for any future circuit-level pickup/filter model.

### 11.1 M7 level profile — pinned

The `wear` knob scales every value here linearly; `wear = 0` restores
the exact flat reference. By-ear verdicts against reference recordings
override all of it (section 16).

- Per-unit spread: uniform per wheel in **+-0.12 (~+-1 dB) at wear = 1**
  [FOLK — alignment-tolerance folklore; the factory trimmed each wheel
  to equal loudness by magnet position, sec 11, so the shipped deviation
  is the residual of that procedure].
- Zone coloration: the three conditioning zones (sec 11) trimmed
  **0 / -0.02 / +0.02** (<= 43 / 44..48 / 49..91) at wear = 1 [FOLK —
  placeholder direction and size; a real value needs measurements or
  ears].
- Character draws: **one splitmix64 draw per wheel** at the fixed seed
  `0x7765617274773931` [decision — sec 12's determinism rule], split
  into 21-bit fields: bits 0..20 level spread, 21..41 motion-AM depth,
  42..62 motion-AM phase (sec 12). Re-derived on every wear change, so
  the knob is stateless.

## 12. Wheel waveform, pickup nonlinearity, and motion AM

What keeps 91 near-sines from sounding like a bare additive synthesizer at
the generator itself (post-generator deviations have their own sections).
Measured ground truth: high-speed-camera plus oscilloscope study of a
1938-vintage generator [ISMA19].

- Waveform: wheels 13..91 induce an approximately sinusoidal EMF with
  visible amplitude fluctuation [ISMA19]. Wheels 1..12 (complex profile)
  are square-ish; citable literature model: the first three square-wave
  terms, `(4/pi)(sin f + (1/3) sin 3f + (1/5) sin 5f)` [AS16 eq 10].
  Dormant below the manual foldback floor (section 3) until a pedal
  division exists.
- Slight waveform "toothing" from nonlinear magnetization/demagnetization
  of the rotor/stator iron, stronger for the more cornered tooth
  geometries of the low registers [ISMA19 FEM + measurement]. The maker
  conceded the same from the factory side: generators supply "unintended
  higher harmonics ... due to slight unavoidable imperfections" in
  construction, filtered per wheel [P39 p.4].
- Pickup nonlinearity: memoryless and asymmetric (compresses one polarity,
  expands the other); literature form `y = (1 - exp(-alpha*x)) / alpha`
  with alpha ~ 0.3 [AS16, after a measured guitar-pickup model]. Two
  architecture facts follow:
  - it acts **per wheel** — each wheel meets only its own pickup, so this
    stage yields harmonic distortion but **no intermodulation**; IMD
    enters first at the shared preamp (section 14 / M5 drive) [AS16
    demonstrates the distinction explicitly];
  - any pickup nonlinearity expands bandwidth — mind aliasing on the top
    wheels [AS16].
- Motion AM ("shimmer"): wheels wobble on their shafts (imperfect
  fixation, bearing play, material defects), producing pronounced AM of
  the induced voltage at the wheel's own **rotation rate,
  f_rev = f_wheel / teeth**, and its low multiples [ISMA19 camera
  tracking]. Literature models it as a small sinusoidal AM [DAFx11].
  Model: per-wheel AM depth and phase, deterministic (fixed-seed
  splitmix64, one draw per wheel at init) [decision].
- Depths (AM percent, toothing strength, alpha) are
  character-of-one-unit, not machine constants: working values **pinned
  at M7 in section 12.1** behind the `wear` knob; final judgement stays
  by ear. `wear = 0` stays the idealized, bit-identical-to-pre-M7
  reference; the shipped default is nonzero, because tolerance effects
  exist on a factory-new unit [decision].

### 12.1 M7 wheel-deviation set — pinned

All depths scale linearly with `wear`; `wear = 0` zeroes every one of
them exactly. All values below are working defaults awaiting the by-ear
pass (section 16) unless tagged otherwise.

- **Toothing** (the [ISMA19] iron nonlinearity): exact 2nd and 3rd
  partials added on the wheel's own phase accumulator — per wheel and
  IMD-free by construction, band-bounded by 3 x wheel 91 = 17.8 kHz <
  any supported Nyquist [derived]. Depths follow **4/teeth** (stronger
  for the cornered low-register geometries — the [ISMA19] observation
  gives the shape [derived]; the anchors are folklore): at the 4-tooth
  lowest manual octave, 2nd = **0.015**, 3rd = **0.03** at wear = 1
  [FOLK]. Wheels 1..12 take the same law as a placeholder; their true
  squarish profile (sec 3 / [AS16 eq 10]) stays out of scope below the
  manual foldback floor.
- **Motion AM**: one sinusoidal AM per wheel ([DAFx11]'s literature
  form of the [ISMA19] wobble) at the wheel's own rotation rate
  `f_wheel / teeth` (= 20 rev/s x class ratio: 16.3..30.9 Hz), applied
  to the whole induced EMF. Per-wheel depth = draw x **0.05 max at
  wear = 1** [FOLK]; per-wheel random start angle (draw bits 42..62,
  taken at init — phase is state). The rotation accumulator always
  advances, so a wear change never breaks the rejoin discipline.
- **Pickup nonlinearity**: `y = (1 - exp(-alpha x))/alpha` with
  **alpha = 0.3 at wear = 1** (the measured literature value [AS16];
  scaling it by `wear` so the idealized pickup is exactly linear is
  [decision]). Implemented as the cubic series
  `x - (alpha/2) x^2 + (alpha^2/6) x^3` [derived]: the dropped quartic
  term is alpha^3/24 ~ 1.1e-3 (~-59 dB) at full wear, and truncation
  bounds the bandwidth expansion at 3 x wheel 91 = 17.8 kHz — alias-free
  at every supported rate (the [AS16] aliasing caution). The x^2 term's
  static component (-alpha/4 per unit wheel) is subtracted at the
  source [decision]: the matching transformer (sec 5) passes no DC, and
  carrying it forward would put a swell-scaled offset in every render.
  Asymmetry check: the curve compresses the positive polarity and
  expands the negative, per the measured pickup shape [AS16].
- **Shipped default: `wear = 0.2`** [FOLK] — a factory-new unit's
  tolerance effects (design.md deviation ledger); `wear = 0` stays the
  idealized test reference that reproduces every pre-M7 signature
  bit-exactly. The by-ear pass against reference recordings owns the
  final value (section 16). The knob rebuilds gain banks only — no
  smoothing — so it is a setup control, not a performance control
  [decision]; no MIDI CC is assigned at M7.

## 13. Leakage structure (the wear/character matrix)

- Generator geometry [SM 5-3..5-5, 3-26, Figs 3-5/5-2]: 48 two-wheel
  shafts; 24 driving gears (2 per note class); each compartment (bin) =
  one driving gear + 2 shafts = **4 wheels of the same note class, same
  speed**, magnetically shielded from other bins by steel plates.
- Shaft pairs: teeth 2&32, 4&64, 8&128, 16&192 — i.e. partner index
  n <-> n+48 for n = 1..36; the 192-tooth partners pair as
  **(42..48) <-> (85..91)** (ratio identity: a 192-tooth wheel runs 12 x
  its 16-tooth shaftmate — offset +43); wheels **37..41 have blank
  partners** [SM 5-5; consistent with SM 3-26's "differ by 48, few
  exceptions... 37-41 single"].
- Compartment sets [derived from the above; back-view column example
  {6, 30, 54, 78} in SM Fig 3-5]: for note class r (0=C..11=B), bins are
  `{r+1, r+25, r+49, r+73}` and `{r+13, r+37, r+61, (192-wheel r+80 for
  r >= 5, else blank)}`.
- Model: leakage gain matrix dominated by same-shaft partner (strongest),
  then same-bin wheels; all other wheels ~0. Levels: **open — pinned at
  M7** (the `wear` knob scales the family; an idle-organ noise-floor
  render is the evidence).
- Measured corroboration [ISMA19]: crosstalk between neighbouring
  wheel/pickup pairs is real on a vintage unit; the pointed pickup tip
  exists specifically to minimize it, and the per-pickup filters shape
  what leaks. The pair measured in [ISMA19 Fig 1] is wheels 9 and 57 —
  a shaft pair, n and n+48, exactly this matrix. [DAFx11] notes leaked
  frequencies follow the physical wheel ordering, not the musical one —
  the bin/shaft matrix above *is* that ordering.
- Audibility calibration for M7 [AS16 demo, frequency-neighbour
  simplification]: crosstalk clearly audible in spectrograms at
  **-24..-6 dB**; -inf..-24 dB subtle. Physical-adjacency levels start
  below that band and tune upward by ear.

### 13.1 M7 leakage — pinned

- Model [decision]: the `leak` frame slot is a **static bleed bus** —
  the idle-organ noise floor. Each wheel's weight on it is the sum of
  its couplings to its physical neighbours' wires (the matrix above
  contracted per wheel: every wire couples onward into the common
  harness, all key contacts being live, sec 6). The bus taps the
  conditioned wire signal (post-pickup, post-level: the per-pickup
  filters shape what leaks [ISMA19]) and joins the keyed+percussion sum
  ahead of the scanner line and swell [decision]; it rides the line
  whole, its sub-80 Hz share being far below the bass-split's
  audibility [decision].
- Coupling strengths at wear = 1 [FOLK]: same-shaft **3e-3 (-50 dB)**,
  same-bin **8e-4 (-62 dB)** per neighbour. The contraction yields
  three structural classes — full-bin wheels (shaft + 2 mates,
  4.6e-3), the r < 5 shaft pairs 13..17/61..65 (shaft + 1 mate,
  3.8e-3), and the blank-partner wheels 37..41 (2 mates, 1.6e-3) — so
  bleed follows the bin layout, not the musical order (adjacent wheel
  numbers 36/37 sit in different classes). Aggregate idle floor
  ~**-30 dB rms at wear = 1** (~-44 dB at the shipped default), below
  the -24 dB clearly-audible band above, tuned upward by ear only.

## 14. Preamp and drive reference points

- Signal path: matching transformer -> vibrato/no-vibrato preamp channels
  -> swell -> output stages [SM 5-39..5-46]. Tube lineup (flavor, not a
  component model): 6AU6 channel inputs, 12AU7/12AX7 family control
  stages, 12BH7 output [SM Fig, preamp schematic].
- Our drive stage is behavioral-stateful by design (bias-excursion
  follower + coupling-cap highpass; see design.md); its curves are
  **tuned at M5** against ear and the odd/even harmonic proxy. No
  circuit-level tube model unless M5 evidence demands it. The demand
  question now has numbers: docs/warmth-evidence.md scores the stage
  against a circuit-true triode reference — what warmth asks for is
  the kernel's even/odd recipe, not a runtime circuit model.
- Division of nonlinear labor [AS16]: per-wheel pickup distortion is
  IMD-free (section 12); the shared preamp intermodulates everything it
  sums. Keeping the two stages distinct is load-bearing for the sound —
  do not fold one into the other.
- The console's own tone cabinet adds a 3-spring reverb and a 200 Hz
  crossover power amp [SM 5-53..5-55] — noted; out of scope v1.

### 14.1 M5 drive stage — pinned

Every number in this block is behavioral-stage tuning, not a measured
machine constant. The circuit-true side now DOES read the schematic: the
full Hammond AO-28 preamplifier is modelled in driver/spice/ao28.cir with
real component values off [SM] sheet p.72 (6AU6 pentode inputs + feedback,
12AX7 driver, 12BH7 output), DC-validated against the sheet's printed node
voltages (docs/ao28-netlist.md). This behavioral block is fitted to that
reference's warmth signature, not to the generic stand-in in stage1.cir.
By-ear verdicts against reference recordings override any [decision] below;
section 16 tracks that.

- **Drive kernel** [derived, warmth pass 2026-07-19]: the preamp's
  saturator is `tw_drive_curve` — the normalized static transfer of
  the circuit-true triode reference (driver/spice/curve.cir, Koren
  12AX7 family, fixed-bias cathode), fitted as a monotone C1 cubic
  Hermite table on uniform knots over [-8, 8], h = 0.25 (worst
  residual 0.0009; axis 0.72 V/unit from the 1 % THD anchor). Exact 0
  and unit slope at 0; flat C1 rails at -1.831 (cutoff) / +3.722
  (grid conduction). Asymmetric on purpose: H2 rides 18-30 dB above
  H3 through the warmth window, H2/H1 grows ~proportionally to level,
  and compression follows the reference's near-transparent law
  (docs/warmth-evidence.md round 2 pins the numbers). No libm.
- **The M5 odd kernel, retained as `tw_sat`** [derived, chosen at M5]
  — the rotary's 40 W ceiling (`tw_drive_set_kernel(_, true)`) and the
  exhibits' bare-shaper twin: the odd rational
  `r(x) = x (27 + x^2) / (27 + 9 x^2)`, input clamped to `|x| <= 3`.
  Derivation: in the family `r(x) = x (A + x^2) / (A + B x^2)` (odd; unit
  slope at 0 automatically), require the +-1 bound to be reached
  *tangentially* at the clamp point m = 3 — `r(3) = 1`, `r'(3) = 0` —
  chosen because true tanh is within 0.5% of its bound there
  (tanh 3 = 0.995). That forces A = 27, B = 9, and makes the clamp
  C1-continuous. `r'(x) = 9 (x^2 - 9)^2 / (27 + 9 x^2)^2 >= 0`, so the
  curve is monotone everywhere; max deviation from true tanh on the
  clamp range is ~0.024 near |x| = 1.5 (cubic term -8/27 vs tanh's -1/3:
  a slightly softer knee). "Tanh-shaped" is the claim; transcendental
  exactness is not.
- **Input reference level** [decision, M5]: `X_ref = 8` — the nominal
  full-organ manual sum (the M1..M4 exhibits render at 1/8 headroom
  scale for exactly this reason). Shaper input is `pregain * x / X_ref`;
  output is scaled back by `X_ref / pregain`, so the small-signal
  through-gain is exactly 1: the knob adds saturation, never volume.
- **Control law** [decision, M5]: one knob, `drive` in [0, 1];
  `pregain = 1 + 7 * drive^2` (audio-taper reach of 0..+18 dB into the
  shaper — the x^2 precedent of the swell law, section 10). `drive = 0`
  is an **exact bypass**: tick returns its input bit-identically and
  leaves all state untouched (the scanner-OFF discipline), so pre-M5
  renders stay stable.
- **Bias-excursion follower**: full-wave `|.|` envelope follower on the
  shaper input (post-pregain); one-pole attack **5 ms**, release
  **50 ms** [decision, M5] — RC-order figures for a small-tube stage's
  grid/cathode network, working values only. Depth forked at the
  warmth pass: the triode kernel shifts by **-0.037 * env** [derived —
  the reference's cathode walk per unit envelope at the 1.0-1.5 V
  anchor; below that the curve's own asymmetry, not the walk, owns the
  even harmonics], while the odd kernel keeps **-0.5 * env** [decision,
  M5] — there the walk is what fakes asymmetry, and the rotary ceiling
  still runs it. With the derived kernel the even-harmonic bloom is
  instantaneous and level-proportional (the curve), and the follower's
  job narrows to the slow operating-point breathing (design.md
  model-depth doctrine, now with the mechanisms in the reference's
  proportions).
- **Coupling-cap highpass** [decision, M5]: one pole at **10 Hz**,
  after the shaper. The classic 0.01-0.047 uF into ~1 Mohm interstage
  coupling lands at 3-16 Hz; 10 Hz sits 2.7 octaves under the manual
  floor (wheel 13 = 65.4 Hz), so the pole's job is the dynamic
  DC-blocking response to bias excursion (the "breathing"), not tone
  shaping.
- **Top-level chain** [decision, M5]: `tw_instrument` = organ -> drive
  (-> rotary at M6); mono tick until the rotary's stereo field lands.
  Swell stays inside the organ, before drive (design.md: closing the
  pedal also cleans the drive up).
- MIDI [decision, M5]: **CC85** -> drive, value/127 (extends the
  M2/M3/M4 map).

## 15. Rotary speaker

The console sources document a *stationary* tone cabinet; the rotary
cabinet is a separate maker's device. Configuration is pinned from that
maker's product book [LB]; construction and the crossover are pinned from
an independent engineering teardown [HX]; construction is corroborated
from the physical unit by a dealer teardown video [KX]. **Rotor speeds
remain unpinned by any *source*** — they are adopted at the end of this
section as folklore [FOLK] working defaults, for the operator to judge by
ear; see that register.

Pinned signal path and construction [HX]:

- **Crossover: 800 Hz, 12 dB/octave, passive, 16 ohm** both sides — stated
  outright, twice, and it is the same figure [DAFx11] used for its own
  Butterworth cutoff. This replaces the "expected ~800 Hz" guess that this
  section carried before.
- **The rotor motors are mains-synchronous**: their "speed is determined by
  the 60 Hz frequency input" [HX], which is why period speed-modification
  shops had to resort to a variable-frequency supply. Consequence
  [derived]: rotor speed is **not a free constant** — it follows from
  mains frequency, motor poles, and pulley ratio, exactly as the tone
  generator's 20 rev/s does in section 1.
- **The maker compensated for mains frequency with the pulley, so 50 Hz
  units run at the same rotor speed as 60 Hz ones** [RS parts list:
  separate part numbers for "Pulley, 3 Step, 60 Hz" and "Pulley, 3 Step,
  50 Hz", plus a 50 Hz two-speed motor assembly and a 50 Hz lower drive
  belt]. **Corrected**: this section briefly claimed "the 50 Hz variant
  runs slower here too" as a [derived] consequence of synchronism. It does
  not — that is the whole reason a distinct 50 Hz pulley exists, and it is
  the same trick section 1 records for the generator's drive gearing.
  Rotor speed is therefore **one target, not a mains-dependent pair**.
- **The bass rotor is an AM device only**, and only over roughly the upper
  two octaves of the bass channel (~200-800 Hz); below ~200 Hz a scoop
  that size does essentially nothing, the wavelength being far larger than
  the drum [HX, and he argues it from the wavelength]. Consequence
  [derived]: **no Doppler on the drum** — the M6 model needs FM on the
  horn only, plus AM on both, with the drum's AM band-limited. This makes
  the drum path materially cheaper than the horn path.
- Some FM may exist near the 800 Hz crossover, but [HX] judges the drum
  "sounds like AM" — recorded as his hedge, not as a pin.
- Horn: a symmetrical **dual** horn of which only one side radiates, the
  other being a dummy for mass and form balance; fed by a stationary
  3/4-inch-throat compression driver through a vertical tube acting as the
  thrust bearing. Measured response ±5 dB from 400 Hz to 10 kHz [HX].
  The dummy side matters to us only as the reason the horn is balanced;
  it radiates nothing.
- The 40 W tube amplifier (6550 output tubes) [HX] corroborates [LB p. 6]
  independently; [RS]'s specification sheet gives the same 40 W and 16 ohm
  on both drivers, and states the 800 Hz split a second time — so the
  crossover is now carried by a factory document as well as by [HX].

Pinned drive mechanics [RS]:

- **Each rotor has a two-speed motor *assembly*: a large motor and a small
  one, not one motor with two windings.** Tremolo actuates the large
  motors; Chorale actuates the small motors, which **brake** the rotors
  down to chorale speed [RS, control-switching description]. A hands-on
  teardown corroborates both halves independently [KX]: two separate
  motors per rotor, and the speed switch shown as a relay that cuts mains
  to one motor and feeds the other ("turning the electricity off to one
  motor and on to the other"). Model
  implication [derived]: chorale is not "the motor driving slower" — it is
  a second, weaker synchronous motor dragging a spinning rotor to its own
  speed. That is why the fall is slow and asymmetric with the rise.
- **The two motors couple by rim drive, not by gearing or clutch** [RS
  teardown steps + parts list]: a **rim drive wheel assembly** sits on the
  large motor's shaft, and the small motor is carried on a bracket with a
  **shaft adjustment screw** that sets how its shaft meets that wheel. So
  the speed change is a *friction* engagement between two synchronous
  motors, mediated by a rubber tyre, with a mechanical adjustment on it.
  Model implication [derived]: the rise and fall are **slip-limited**, not
  torque-limited — which is the honest reason the transition takes seconds
  and why [RS] treats belt tension and this shaft adjustment as the things
  that set the timing. A first-order lag is a fair model of that; a
  "motor spins up" model is not.
- **Bass rotor tremolo -> chorale transition: about 5 to 8 seconds** [RS,
  bass drive belt adjustment]. This is the first **sourced** number for
  rotary inertia anywhere in this document, and it is a *service
  acceptance* figure: the manual has the technician switch Tremolo to
  Chorale, time the bass rotor, and **re-tension the belt if it falls
  outside 5-8 s**. So it is a real, checked behavior, not lore. Caveats:
  it is a *deceleration* (tremolo -> chorale), not a spin-up
  from rest, and belt tension is exactly what it is testing — so read it
  as "the drum's speed changes take single-digit seconds", not as a
  precise tau.
- **No equivalent timing spec exists for the treble rotor** [RS]: its belt
  runs over a spring-mounted idler that sets its own tension, so there is
  nothing to time. Absence of a treble figure is a fact about the
  document, not evidence the horn is fast.
- **The treble rotor's speed is an installation choice.** The belt sits in
  one of three grooves on a 3-step drive pulley: **centre groove = "normal"
  Tremolo speed**, lower groove faster, upper groove slower [RS, treble
  belt replacement]. Multi-cabinet installations were *encouraged* to pick
  different grooves per cabinet "for contrasting tremolo effects" [RS].
  Consequence [derived]: there is a factory-nominal horn speed (the centre
  groove) but no single universal one — which is part of why no source
  quotes one number.

Pinned configuration [LB]:

- Two rotors, one passive crossover on a single-channel input: treble =
  compression driver (100 W RMS rating, molded dual-branch horn) firing
  through the rotating horn on top; bass = stationary 15-inch woofer
  firing into a rotating molded-foam drum below [LB pp. 5-8 diagrams].
  A teardown video corroborates the layout from the physical unit [KX]:
  15-inch bass woofer firing down into the lower rotor, a compression
  driver feeding the upper horn through a passive crossover behind the
  lower motor, a tube power amp, and a cloth horn-drive belt "like a fan
  belt" (independent witness to the belt coupling [RS] pins).
- Three rotor states: Fast (tremolo), Slow (chorale), Off (brake)
  [LB p. 8]. On brake, rotors park **facing forward** ("maximizes the
  sound towards the audience") [LB pp. 15, 18] — model the brake as a
  target-angle stop at the front, not a free coast.
- Slow/fast speeds and rise/fall times are independent per-unit
  adjustables on production units [LB pp. 11, 13] — confirming the model
  shape: per-rotor x per-direction time constants are four free pinned
  constants, not one shared inertia.
- Reference voicing: the classic single-channel tube family — a 40 W RMS
  tube amplifier drives both rotors [LB p. 6]. The rotary drive stage
  models "tube amp pushed toward a 40 W ceiling" ahead of the rotors.

**Rotor speeds: still open. The factory service manual does not carry
them.** This was searched for deliberately, on the reasoning that
synchronous motors + poles + pulley ratio would derive every speed. [RS]
was found and read: it has **no rpm figure, no pole count, and no pulley
ratio** — it is an installation and service manual, so it tells a
technician which groove to use and how to time a belt, never what the
resulting speed is. The search half-worked: it pinned the *mechanics* above
and one *transition time*, and it killed a wrong claim about 50 Hz, but
the speeds themselves remain unsourced. Also empty: [HX] (construction, no
rpm), [LB] (no rpm; current production makes speeds and rise/fall times
per-unit adjustables), [DAFx02] (geometry, no speed).

**The type plate and groove diameters were searched for too, with the same
empty result.** [RS]'s parts list names the parts but gives **numbers only,
never dimensions or ratings** — "Pulley, 3 Step, 60 Hz" (050500) and its 50 Hz twin (050559)
carry no diameters; "Motor, Large, 117V 50/60Hz" and "Motor, Small"
(050450) carry no pole count. The community wiki's motor page is about
aftermarket speed controllers, not motor engineering. A specialist parts
vendor's motor catalogue lists refurbished stacks and rebuild kits with
**no rpm or pole figure anywhere**. So: no type plate, no groove diameter,
no pole count in any source reachable so far.

One thing the vendor catalogue did settle, because it matches [RS]: the
stack couples by an **O-ring on the fast motor's shaft**, which is the
rim-drive wheel above — an independent corroboration of the friction
coupling, from someone who rebuilds them.

A pole count picked to reproduce the ~400/~40 rpm lore must not be called
derived. That is the same move section 8 had to retract — choosing an input
because it yields the wanted output. Pole count is either read off a plate or
it is unknown. Adopting the lore *openly as [FOLK]* below is the alternative
to laundering it through a fake derivation: the number is used, but its
provenance is labeled for exactly what it is.

**Rotor speeds and dynamics — pinned as folklore [FOLK], for the operator
to judge by ear.** These are the widely-repeated community/encyclopaedia/
forum figures; **no primary source in hand carries them** (the search above
came back empty), and they are exactly the kind of unsourced intermediary
this document treats with caution. They are pinned anyway — as **working
M6 defaults, tagged [FOLK]**, not promoted to sourced constants — so M6 has
numbers to build against and a labeled thing to correct. Every one is
expected to move once judged by ear or measured from a clean reference
recording:

- Tremolo (fast): **horn ~400 rpm = 6.7 Hz**, **drum ~340 rpm = 5.7 Hz**
  [FOLK]. The horn/drum split is a designed pulley ratio, not a beat.
- Chorale (slow): **both ~40-50 rpm = 0.67-0.83 Hz** [FOLK].
- Horn spin-up (chorale -> tremolo): **~1 s** [FOLK]. The drum's *fall*
  (tremolo -> chorale) is **not** [FOLK] — [RS] pins **5-8 s** above, a
  service-acceptance figure, several times longer than the horn's rise.
- Doppler delay swing: **~0.3-0.9 ms** [FOLK, from the expected horn
  radius] — horn only; the drum is AM, no Doppler [HX].
- AM depth: **greater on horn than drum** [FOLK], consistent with [HX]'s
  band-limited AM-only drum.

**One rough measured cross-check [KX, this document's own analysis], not a
pin.** Frame-by-frame tracking of the horn in the teardown video's intro
(one cabinet, one compressed 30 fps clip) confirms the *shape* — a real
chorale/tremolo speed contrast with two plateaus and audible/visible
switching — and puts horn rotation in the **low single-digit rev/s** range.
It does **not** confirm the numbers: a near-symmetric dual-lobe horn leaves
an irreducible 1x/2x ambiguity and 30 fps aliases the fast plateau, so the
recovered fast/slow ratio (~3x) is softer than the [FOLK] ~8-10x. Recorded
only so the folklore has one measured touch-point; the by-ear call
overrides both.

**[DAFx11] is not corroboration of the speeds** [corrected]: an earlier
reading of this section said it was. What that paper actually states is
that *its own demonstration* used a modulator at "2 Hz for slow, 6 Hz for
fast" — implementation settings, not a measurement of a cabinet. They
also disagree with the expectations above: chorale at ~40-50 rpm is
0.67-0.83 Hz, so the paper's slow rate is about **3x** ours. Its fast
rate (6 Hz vs ~400 rpm = 6.7 Hz) is the only one that lines up. Treat
[DAFx11]'s rates as one team's dial settings.

**The +0.1 Hz motor mistuning is withdrawn** [corrected]: this section
carried "the two motors are near-but-not-equal (after a maker-lore
article)". [DAFx11] introduces that 0.1 Hz offset and attributes it to
[HX] — but the claim **is not in [HX]**; searched, and his only statement
about motor speed is that it is fixed by the 60 Hz line. Two synchronous
motors on one line are exactly locked in ratio, so the attribution points
the wrong way. The horn and drum do run at *different* speeds (~400 vs
~340 rpm expected), but that is a designed pulley ratio, not a slow beat
between near-equal motors. Nothing here is pinned; the mistuning does not
carry into M6 unless a real source turns up.

### 15.1 The M6 working set (pinned at implementation)

Every number `src/rotary.c` consumes, in one place. Sourced pins carry
their tags from the section above; everything else is a labeled working
default the by-ear pass is expected to move.

| constant | value | tag |
|----------|-------|-----|
| crossover | 800 Hz, 12 dB/oct, both sides | [HX], corroborated [RS] |
| crossover realization | TPT state-variable filter, Butterworth k = sqrt 2, g = tan(pi fc/fs) via the repo sine kernel — the -3 dB points land exactly on 800 Hz | [derived] |
| horn branch polarity | inverted (`lp - hp` recombination): a 2nd-order crossover's outputs are antiphase at fc, the same-polarity sum nulls there; the inverted sum is flat within the +3 dB fc bump | [derived] |
| drum AM band | AM applies above a 200 Hz floor (one-pole split inside the drum branch); below it the drum barely modulates | [HX] band; one-pole order [decision] |
| tremolo speeds | horn 400/60 Hz, drum 340/60 Hz | [FOLK] |
| chorale speeds | horn 48/60 Hz, drum 40/60 Hz | [FOLK] band 40-50; the split inside it [decision], keeping the tremolo pulley ordering |
| transition = 3 tau | a pinned transition time is read as ~3 tau of the first-order slip lag | [decision] |
| horn rise | 1 s -> tau 1/3 s | [FOLK] |
| horn fall | 2 s -> tau 2/3 s | [decision] |
| drum rise | 2.5 s -> tau 2.5/3 s | [decision] |
| drum fall | 6.5 s -> tau 6.5/3 s | [RS] 5-8 s service window, midpoint |
| brake park | at the front stop; phase pull engages below 0.5 Hz, pull tau 0.3 s, snaps exact | park-forward [LB]; dynamics [decision] |
| Doppler delay | per mic: base - amp x cos(horn - mic); base 1.0 ms, amp 0.3 ms (0.6 ms peak-to-peak swing, inside the [FOLK] 0.3-0.9 ms) | [decision] |
| AM depths | horn 0.40, drum 0.15 | ordering horn > drum [FOLK]; values [decision] |
| virtual mics | +-1/8 turn about the front; parked forward the field is exactly symmetric | [decision] |
| stereo controls | balance = linear horn/drum tilt (gains 2b and 2(1-b), 0.5 neutral); width = mid/side scale | [decision] |
| amp ceiling | the M5 drive stage reused ahead of the rotors, its own knob; models "a 40 W tube amp pushed toward its ceiling" [LB p. 6] | reuse [decision] |
| rotor lag realization | rate = target + dev, dev decaying multiplicatively per direction — lands exactly on target, no f32 stall | [derived] |

## 16. Open-items register

| item | owner |
|------|-------|
| taper resistance classes per (key, bus) | **table pinned in docs (section 6.1) from [MW]**, single secondary source; **wired into the core at M3** (per-(key, bus) classes, dB column as level authority, precomputed gains — no libm); by-ear verification **still open**; a factory chart would still upgrade the provenance |
| robbing | **a(k) replaced at M3** by the parallel-network ratio law over per-key wires (section 6.1); a(k) survives as its equal-wire special case. All 53 collision sites now rob per their 24-100 ohm classes; the M3 exhibit measures the retired flat model's over-robbing at +0.97..+1.78 dB (mean +1.19). `test.c` asserts the 16'/wheel-13 collision at ~0.7183 absolute — merge_ratio 0.9416 x (0.3162 + 0.4467); the a(2)-**equivalent** is ~1.88 against the old 1.6, but taper puts the actual contribution below 1. By-ear verification of the new robbing **still open** |
| bounce count/window, smoothing constant | M2 shipped defaults (<= 3 toggles / 2 ms, tau 0.25 ms, stagger 0-15 ms, release 3 ms); final by ear |
| key depth: make-point spacing, closure order, make/break band | **Landed post-M7** (section 7.1): nine make points evenly over a 0..127 travel, closing bus 0 -> 8 as the stagger does, with a +-4 band so a parked finger cannot chatter a contact; carried on poly key pressure (0xA0). Every one of those three numbers is a **[decision]** — [SM] tabulates neither the spring heights nor their order, and the band is sized for the control problem, not measured. The feature is inert without a depth message: every percussion-off signature, including the M7 wear identity anchor and the whole-song `renders.md` hashes, reproduces bit-for-bit (docs/depth-evidence.md). **Still open**: all three numbers by ear, and the feel of the ninth contact now that it both steals the 1' bus and fires percussion |
| percussion trigger source, re-arm timing | **Landed post-M7** with key depth (section 8): the trigger follows the **1' contact**, not the note event, which is what the sensing-line reading said all along — the old note-driven form was only equivalent when every press bottomed out, and section 7.1 makes that false. The 34 ms R55/R56 recovery is now modeled too, because the sensing line's own 2 ms bounce would otherwise retrigger the envelope several times per press. Measured: trigger lands at the end of a slow press, not its start; bounce gives one hit per press; the detachment threshold sits between 20 and 40 ms; a half-press fires nothing. **This moved the percussion-on baselines** — `exhibit_percussion` and `exhibit_drive` re-pinned in docs/depth-evidence.md; nothing percussion-off moved. **Still open, by ear**: whether one tau is the right reading of recovery, and the absolute decay seconds that were already open above |
| swell curve | M2 shipped x^2 taper; compensation tilt (loudness-style, section 10) **still open — deliberately not cut into M5**, which modeled the drive stage only; lands with a by-ear voicing pass |
| percussion decay, re-arm, SOFT pad, NORMAL attenuation | **mostly closed by reading [SM]'s prose (section 8), not by ear**: decay tau 1.551 s / 0.375 s from C31 = 0.33 uF through R58 / R57\|\|R58, ratio **4.133 : 1 (the old 3.7 : 1 was the wrong resistors)**; re-arm tau ~34 ms from R55+R56. Still open at M3: **absolute decay seconds are NOT derived** — tau -> audible time needs V7's transfer behavior or a measurement ([SM 4-4] trims it per unit against an audible threshold); pick slow by ear, derive fast = slow / 4.133, mark [decision]. The NORMAL attenuation and SOFT pad are now **derivable** (R50 = 22 ohm as a section 6.1 wire; R46/R59/R51 divider) rather than by-ear. Concept lineage [P44] — but its ~8 notes/s figure is **not** an anchor, it is 4x out |
| scanner | **components + taps [DAFx16]; depth/rate [P45]; C-mode voicing target [P39]**; **M4 landed**: trapezoidal nodal ladder (no prewarp; edge 6575 Hz at 48 kHz, ~7.1 kHz at 192 kHz), C mix = (dry+wet)/2 [decision], bass bypass as the exact wheel-1..16 split [derived], CC84; measured V3 cyclical depth 1.48% (in band), peak 1.91% with the moving ripple. **Still open**: output level trim and every by-ear verdict (C-mode voicing vs [P39] treble-detune target, ripple strength) |
| drive curves (bias depth, tilt) | **M5 landed** (section 14.1): saturator kernel [derived, tangent-bound rational], X_ref = 8, pregain 1..8 on drive^2, bias follower 5/50 ms at depth 0.5, coupling cap 10 Hz, CC85 — stage structure and unit behavior test-pinned. **Still open, by ear**: attack/release, bias depth, drive taper, any level trim; a wave-digital triode stage is the named upgrade if the ear demands it. **Warmth measured, then landed** (docs/warmth-evidence.md): round 1 found the deficit in the kernel's H2:H3 recipe (~9 dB spacing vs the triode's 18-28 dB), not in missing circuit state; round 2 derived the kernel from the reference's static transfer (`tw_drive_curve`, matched within ~0.4 dB H2 / ~1.4 dB H3 / ~0.03 dB compression at matched THD) and re-derived bias depth to 0.037; the rotary ceiling keeps the M5 odd kernel via `tw_drive_set_kernel`. **Still open, by ear**: level trim (the derived stage compresses ~10x less — equal-knob renders come out louder; A/B wavs at build/warmth_ab_*.wav), drive-taper feel at the top of the knob, and the WDF rung stays reserved for blocking/sag/power-stage truth |
| rotary numeric dynamics | **Crossover pinned: 800 Hz, 12 dB/oct, 16 ohm passive** [HX], corroborated by the factory spec sheet [RS] (was a guess). Construction [HX]: motors **mains-synchronous**; drum is **AM-only, ~200-800 Hz — no Doppler on it**. Drive mechanics pinned [RS]: **two motors per rotor** (large = tremolo; small = chorale, which *brakes*), **bass tremolo->chorale 5-8 s** (a service acceptance figure — first sourced inertia number), treble speed is an **installation choice** on a 3-step pulley (centre groove = normal). 50 Hz units run the **same** speed — a distinct 50 Hz pulley compensates; the earlier "50 Hz runs slower" was wrong. Construction now **corroborated by a teardown video [KX]** (two-motor assembly, relay speed-switch, belt drive, 15" woofer + tube amp + compression-driver horn) — second witness, no numbers. **Speeds pinned as [FOLK] working defaults** (tremolo ~400/~340 rpm, chorale ~40-50 rpm, horn spin-up ~1 s) — used for M6, tagged as folklore, **by-ear call overrides**; a [KX] frame-track cross-check confirms the chorale/tremolo *shape* but not the numbers (1x/2x + 30 fps aliasing). **Truly still open for a primary source:** motor pole count + 3-step pulley groove diameters (those derive every speed) and horn radius. [DAFx11]'s 2/6 Hz are its own dial settings, **not corroboration**; its +0.1 Hz mistuning is **withdrawn**, unsupported by [HX]. **M6 landed** on the 15.1 working set: crossover measured -3 dB at 800 Hz both sides, horn FM 50.9 Hz p-p on a 2 kHz tone at tremolo (geometry says 50.3), drum pitch residual 0.33 Hz (AM-only holds), AM floors 0.60 horn / 0.87 drum-mid / 0.97 drum-100 Hz, brake parks exact. **Still open, by ear:** every [FOLK] speed, all four transition taus, both AM depths, the Doppler swing, mic geometry, balance/width laws — the whole 15.1 [decision] column awaits reference recordings |
| leakage/hum levels, level-profile spread | **M7 landed** (secs 11.1/13.1): level spread +-0.12 + zone trims 0/-0.02/+0.02; bleed 3e-3 shaft / 8e-4 bin per the compartment classes (bleed follows the bin layout — asserted); hum 1e-3 at 60 Hz — all [FOLK] working values pinned to start below the [AS16] -24 dB clearly-audible band and tune upward. Idle-floor evidence render in docs/m7-evidence.md (-30.2 dB at wear 1, -44.5 dB at the shipped default). By-ear verification **still open** |
| wheel motion-AM depths, pickup alpha, low-register toothing, default `wear` | **M7 landed** (sec 12.1): motion AM max 0.05 x per-wheel draw at each wheel's own rotation rate; alpha = wear x 0.3 ([AS16] — the one measured magnitude) as a DC-free cubic; tooth anchors 0.015/0.03 x 4/teeth; shipped default `wear = 0.2`. `wear = 0` reproduces every pre-M7 render bit-for-bit (pinned signatures in test.c; the M7 exhibit re-derives the m6-evidence transition hash). Every magnitude except alpha is [FOLK]/[decision]; the by-ear verdicts **still open** — the default 0.2 most of all |
