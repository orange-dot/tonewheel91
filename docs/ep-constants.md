# ep73 — pinned machine constants (EP0)

Date: 2026-07-26. The electric-piano line of `docs/piano-backlog.md`, part 1.
Same rule as `constants.md`: every number below carries a source tag or an
explicit **open** marker naming the milestone that must resolve it. No
constant is "known" without a source.

Two things separate this document from the organ's. First, the sources are
thinner on numbers: the organ's constants came out of gear tables and
tapering charts, while this instrument's identity lives in a
velocity-to-timbre relationship nobody wrote down. Where that bites, the
value is tagged and EP3 owns it. Second, the machine is simple enough that
several constants are genuinely derivable from beam theory plus two anchor
numbers in the founding patent, and those derivations are shown rather than
asserted.

## Sources

Operator-local copies live in `docs/externalDocs/` (untracked, for copyright
reasons). Citation tags:

- **[EP-P61]** — US Patent 2,972,922 (Rhodes, granted 1961-02-28), the
  founding patent of the tine line. Primary source for the asymmetric
  tuning fork (a slim tine against a heavy resonated inertia bar), the
  intended envelope, the dwell figures at both ends of the compass, the
  deliberately off-centre pickup edge, and the hammer-felt ladder.
- **[EP-P68a]** — US Patent 3,384,699 (Rhodes, filed 1964-12-16, granted
  1968-05-21): mounting the tone generator and positioning it relative to
  the transducer.
- **[EP-P68b]** — US Patent 3,418,417 (Rhodes, filed 1965-06-24, granted
  1968-12-24): the multi-component tuning fork — tine, cross-member and
  inertia bar as separate elements of different materials.
- **[EP-P72]** — US Patent 3,644,656 (Fender and Rhodes, granted
  1972-02-22): tone generator with vibratory bars.
- **[EP-P77]** — US Patent 4,040,321 (Lover, filed 1975-07-18, granted
  1977-08-09): the electromagnetic pickup for a tine-type instrument.
- **[EP-P83]** — US Patent 4,373,418 (Rhodes and Woodyard, granted
  1983-02-15): tuning-fork mounting assembly.
- **[EP-SM]** — the manufacturer's service manual for the tine piano,
  1979 edition (P/N 34.0119.000); operator-local copy of the eleven
  chapters. Tags are the manual's own chapter and page numbers (e.g.
  4-7). Primary for the mechanical dimensional standards, the hammer-tip
  hardness ladder, the damper arrangement, and the factory tuning policy.
- **[EP-DAFx17]** — Pfeifle, DAFx 2017 (real-time physical model of the
  reed and tine electric pianos): high-speed-camera measurements of tine
  motion in two polarisations, the cantilever-beam formulation, the
  hysteretic hammer model, and a magnetic-field model of the pickup.
- **[AT20]** — Russell, *Acoustics Today* 16(2):48-55, 2020 (tuning-fork
  acoustics, open access). Primary for the clamped-free bar mode ratio and
  for the measured dependence of fork spectrum on strike strength.
- **[derived]** — computed here from pinned constants; the derivation is
  shown. **[decision]** — a design choice, not a machine fact. **[FOLK]** —
  a working default with no primary source behind it, pinned to be
  overridden by ear or by a clean measurement. Tagged, never laundered
  into a fake derivation.

Not obtained: the JASA 2020 study of this instrument's inharmonic overtones
is paywalled and is therefore cited nowhere below. The stretch-tuning chart
in [EP-SM] 5-6 exists only as a scanned figure; its per-register cent values
are unreadable in the operator-local copy and are not pinned (they are not
needed — see section 2).

## 1. Compass and note map — D1 closed

Seventy-three keys, E1 to E7, MIDI 28..100 inclusive [decision — the
mid-size compass of the reference instruments; `piano-backlog.md` scope].
Notes outside the compass are ignored and counted, as in the organ.

- `EP_KEYS = 73`, `EP_NOTE_MIN = 28`, `EP_NOTE_MAX = 100`.
- Key index `k = midi_note - 28`, range 0..72.

## 2. Tuning

**Equal temperament, A4 = 440 Hz** [EP-SM 5-4]: the manual states plainly
that the instrument is *not* stretch tuned at the factory and is tuned to
equal temperament instead. This closes the "stretch, if any" question the
backlog left open — there is no gear table here and no tempering story;
the tines are tuned by design, one at a time, by sliding a coil spring
along the tine [EP-P61; EP-SM 5-1].

- `EP_A4_HZ = 440.0`
- `f(k) = EP_F_E1 * 2^(k/12)`, with **`EP_F_E1 = 41.203444614108747 Hz`**
  [derived: `440 * 2^(-41/12)`].
- Implementation without libm: `f(k) = EP_F_E1 * EP_SEMI[k % 12] * 2^(k/12)`
  where the octave factor is an exact power of two and `EP_SEMI[j] = 2^(j/12)`
  is pinned as twelve constants [derived]:

      j   2^(j/12)
      0   1.000000000000000
      1   1.059463094359295
      2   1.122462048309373
      3   1.189207115002721
      4   1.259921049894873
      5   1.334839854170034
      6   1.414213562373095
      7   1.498307076876682
      8   1.587401051968199
      9   1.681792830507429
     10   1.781797436280679
     11   1.887748625363387

  Endpoints: `f(0) = 41.2034 Hz` (E1), `f(72) = 2637.02 Hz` (E7).

Two tuning facts are recorded but not modelled at EP0. The tuning spring
sits near the free end deliberately, "in order that variations in spring
locations will alter the fundamental frequencies and not the harmonics"
[EP-P61] — so in a real instrument the overtone *ratio* drifts per note
as a side effect of tuning. And a steel fork's frequency falls by 0.01 %
per degree Celsius [AT20]. Both are per-note character, and both belong to
the `condition` knob at EP7, not here.

## 3. The voice: modes and ratios — D2 closed

The tone generator is an asymmetric tuning fork: a slim cylindrical tine of
piano wire (0.075 inch diameter [EP-P61]) fixed at one end, and a heavy
cast-iron inertia bar tuned to the *same* fundamental as the tine
[EP-P61; EP-SM 1-1]. The bar is deliberately low-Q — cast iron, a flat
resonance curve — so that the tine's pitch can be moved "as much as half an
octave without completely departing from resonance" [EP-P61]. The bar is
therefore a broad supporting resonance at the fundamental, not a separate
partial: it lengthens the dwell and does not add a mode.

The partials that do exist are the tine's own flexural modes. The tine is a
fixed-free (clamped-free) cantilever, which is the boundary condition
[AT20] settles by measurement in favour of over the free-free alternative.

**`EP_MODES = 3`** [decision], with ratios [derived from the clamped-free
eigenvalues `β₁ = 1.8751041`, `β₂ = 4.6940911`, `β₃ = 7.8547574`; mode
frequency scales as `β²`]:

    mode   ratio      corroboration
    1      1.0        —
    2      6.2669     [AT20] quotes 6.26 for theory and measures 6.03 on a
                      432 Hz fork (clang tone at 2605 Hz)
    3      17.5475    —

Why three and not two: the third mode is a real mode of a real cantilever,
it costs one oscillator per voice in a fixed-size bank, and the Nyquist rule
below silences it over the top of the compass anyway. Pinning it now avoids
a structural change if EP3's by-ear pass wants it. If EP3 finds it
inaudible, its weight table goes to zero and the mode can then be deleted
outright — deletion pressure applies to it, not to the interface around it.
Three modes across 73 keys is the 219-oscillator bank the backlog costs.

The measured 6.03 against the theoretical 6.2669 is the expected direction:
a mass near the free end (here the tuning spring) pulls the ratio down. The
theoretical value is pinned as the base; the deviation is per-note character
and belongs to EP7.

### 3.1 Nyquist rule

A mode whose frequency reaches the guard band renders at **gain zero** —
silence, not foldback. A piano borrows nothing from the top of its compass,
so there is no foldback analogue here; the organ's wheel-borrowing rule has
no equivalent.

- `EP_NYQUIST_GUARD = 0.45` of the sample rate [decision — a phase
  accumulator aliases above 0.5; 0.45 leaves margin and sits above the
  audible band at every supported rate].
- A mode with `f_m >= 0.45 * fs` gets step 0 and strike weight 0, once, at
  init.

At 48 kHz this silences mode 3 from MIDI 87 upward (`f₃ >= 21600 Hz` at
`f₁ >= 1231.0 Hz`) and never silences mode 2 (which would need
`f₁ >= 3446.7 Hz`, above the compass). At 44.1 kHz mode 3 silences from
MIDI 86 upward.

## 4. Free decay

Two anchors from the founding patent, at opposite ends of the compass
[EP-P61]:

- low-pitched notes: dwell **about 17 seconds**;
- high-pitched notes: **3 to 5 seconds** at maximum dwell.

The patent is explicit that the low end is *deliberately* detuned off the
inertia bar's resonance peak to shorten the dwell to that figure, and that
the top is set on the peak for maximum dwell — so both numbers are design
targets, not incidental measurements.

Reading "dwell" as time to inaudibility, pinned at **-60 dB** [decision]:

- `t60(k) = EP_T60_E1 * EP_T60_RATIO^k` with
  **`EP_T60_E1 = 17.0 s`** [EP-P61] and
  **`EP_T60_RATIO = 0.980099`** per semitone
  [derived: `t60 ∝ f^-p` with `p = 0.348` fixed by the two anchors —
  `17 * 64^-0.348 = 3.999 s` at E7, inside the patent's 3-5 s band; the
  per-semitone ratio is `2^(-0.348/12)`].
- The amplitude time constant is `tau = t60 / ln(1000) = t60 / 6.907755`.

Resulting octave anchors [derived]:

    note   MIDI   f (Hz)     t60 (s)   tau (s)
    E1     28       41.20      17.00     2.461
    E2     40       82.41      13.36     1.934
    E3     52      164.81      10.49     1.519
    E4     64      329.63       8.24     1.194
    E5     76      659.26       6.48     0.938
    E6     88     1318.51       5.09     0.737
    E7    100     2637.02       4.00     0.579

Higher modes decay faster, by the same `f^-p` law evaluated at the mode's
own frequency and then by an extra per-mode damping factor
[FOLK — the extra factor has no source; EP3 owns it]:

    mode   f^-p part          extra    EP_MODE_T60[m]
    1      1.0                1.00     1.00000
    2      6.2669^-0.348      0.35     0.18480
           = 0.527975
    3      17.5475^-0.348     0.15     0.05535
           = 0.368978

So `t60(k, m) = t60(k) * EP_MODE_T60[m]`. At E4 that is 8.24 s, 1.52 s and
0.46 s for the three modes.

The two-stage envelope the patent asks for — "an initial percussive effect
followed by a relatively rapid decay and then by a limited dwell"
[EP-P61] — is not pinned as a separate envelope stage. It falls out of
three modes decaying at three rates together with the level-dependent
pickup nonlinearity of section 6, and EP1's exhibit is where that claim gets
checked. If the ear says otherwise, an explicit two-rate envelope on mode 1
is the named upgrade and EP3 owns it.

### 4.1 Coefficient form

Store the per-sample **decrement**, not the multiplier:
`EP_DEC[m][k] = 1 - exp(-1 / (tau * fs))`, and tick `a -= a * dec`. This is
the organ's one-pole toward a zero target, with the same epsilon snap
(`a < 1e-9 -> 0`) so a decayed voice reaches exact silence and never enters
denormal territory. Storing the decrement rather than the multiplier keeps
full f32 relative precision: at the longest tau the multiplier is
0.99999153, where the interesting part is 71 ulps wide, while the decrement
8.465e-6 carries a full mantissa.

`1 - exp(-x)` is computed at init by the same alternating Taylor form the
generator already uses (`x*(1 - x*(0.5 - x*(1/6 - x/24)))`, valid to
x <= 0.25). Here x is at most 6.5e-4, so the truncation is far below f32
resolution.

## 5. Strike: velocity to level and to spectrum — D3 draft

This is the instrument's identity and the dominant work of the line. EP0
pins a shape with a derivation and an owner; **EP3 owns the final values**.

### 5.1 Level

`level(v) = (v / 127)^γ` with **`γ = 1.042`** [decision — closed by ear at
EP3, D3a].

The derivation gives 1.0 exactly: hammer velocity is proportional to key
velocity through a direct single action [EP-DAFx17 §3.1], the tine's
initial velocity is proportional to the hammer's, and the pickup's induced
EMF is proportional to `ω × displacement`, hence to the tine's initial
velocity and *not* to `1/ω`. The ear asked for slightly more than that —
45 dB across the velocity range rather than 42 — and 1.042 is what it
settled on. The gap between the two is the honest size of the by-ear
correction, and it is small, which is a good sign for the derivation.

Carried as a **pinned 128-entry table**, because the core has no libm and
velocity is an integer 1..127 anyway. Re-pinning `γ` means regenerating the
table, which is the point: the exponent is a documented generator
parameter, not live code.

The manual notes that the achievable dynamic range is itself an adjustment:
a smaller tine-to-pickup gap gives more volume and "more pronounced dynamic
response", defined there as percentage of volume increase per increase in
weight of touch [EP-SM 4-8]. That is a per-instrument setup variable, so it
belongs to `condition` at EP7, not to the base law.

### 5.2 Spectrum

The hammer tip is neoprene, graded in hardness and height from bass to
treble. The service manual's ladder, by hammer number on the 88-note
instrument [EP-SM 4-3]:

    hammers   durometer        tip height
     1-30     30               1/4"
    31-40     50               5/16"
    41-50     70               3/8"
    51-64     90               7/16"
    65-88     wrapped, extra   7/16"

The founding patent gives the same ladder as intent rather than as a table:
hammers for the low-pitched generators "are relatively large and are thickly
felted, in order to damp out harmonics", reduced progressively in size and
felt thickness toward the treble, where the hammer is "relatively sharp and
only thinly felted" [EP-P61].

Hammer number 1 is the lowest key of the 88-note instrument (A0, MIDI 21),
so the ladder is pitch-referenced as [derived — the grading follows tine
length, which follows pitch; the assumption is stated, not hidden]:

    zone   MIDI       durometer    t_c (ms)   EP_CORNER_HZ (v = 127)
    0      28..50     30           9.00        55.556
    1      51..60     50           6.00        83.333
    2      61..70     70           4.20       119.048
    3      71..84     90           2.85       175.439
    4      85..100    wrapped      1.95       256.410

**Closed by ear at EP3 (D3b).** The ladder was lengthened by half again
over the values the compass sweep left it at. Recorded plainly because the
bass figure is now at the edge of what a hammer contact plausibly is: 9 ms
is long against the 3-5 ms a piano bass hammer spends on a string. What
argues for it is the founding patent's stated intent — those hammers are
"relatively large and thickly felted, in order to damp out harmonics"
[EP-P61] — and the ear. What argues against it is that no source gives a
number. It stays [FOLK] and it is the first thing to revisit if a clean
reference recording ever arrives.

The corner is **not** a free number: `fc = 1 / (2 t_c)` where `t_c` is the
contact time. The zone boundaries and their ordering are sourced from the
durometer ladder; the times themselves are [FOLK] and EP3's to settle. A
softer, thicker tip stays in contact longer and cannot excite anything far
above its own reciprocal contact time.

EP0 originally pinned these corners at 1800-7000 Hz as a geometric working
set, which was never checked against a contact time and implies one of
about 0.14 ms — an order of magnitude shorter than any hammer. **EP3
corrected them.** The bass zone was then lengthened again, from 4 to 6 ms,
on the founding patent's own statement of intent: the hammers for the
low-pitched generators are "relatively large and thickly felted, **in order
to damp out harmonics**" [EP-P61]. That is a design goal, not a side
effect, and the model has to carry it.

Together with the length cap above, the two corrections take the clang
partial at E1 from 2.7 dB under the fundamental to 11.8 dB under it, and
put it **below** the pickup's own second harmonic rather than above it.
That matters more than the number: a 41 Hz fundamental is inaudible on most
playback, so whichever partial sits on top of the harmonic series is the
pitch the ear assigns. With the clang on top, a bass note reads as a short
hollow pop somewhere near C4. The spread of the clang across the whole
compass is now 4.8 dB, against 15.2 dB before.

Two factors set how strongly a blow excites a mode. Where the hammer lands
decides the mechanical coupling; how long it stays decides how much of the
blow the mode can hear at all.

**Where it lands (derived).** The tine is a clamped-free beam struck at
`xi = x0 / L` from its fixed end, and the pickup reads the tip, so mode m's
output goes as `phi_m(xi) * phi_m(1)`. The striking line is tabulated —
2-1/4 inch from the tone generator's leading edge at the extreme bass,
1/8 inch at the extreme treble [EP-SM 4-2] — and the tine is uniform
0.075 inch piano wire [EP-P61], so `L` follows from `f1 = K / L^2` and
`xi` follows from both. Interpolating the striking line geometrically
across the compass gives **`xi` sweeping 0.316 in the bass to 0.141 in the
treble**: the strike sits proportionally *closer to the fixed end* as the
tines shorten.

**A tine cannot be longer than the harp is deep.** The bare beam law asks
for 181 mm at E1, and the longest replacement blank the factory ships is
4-3/8 inch, 111.125 mm [EP-SM 5-1] — so the law was demanding a tine
1.6 times longer than the instrument contains. Below about MIDI 45 the
length stops growing and the pitch comes from the counterweight instead,
which is exactly what the founding patent describes: the springs for the
lower-pitched generators are deliberately heavier [EP-P61]. Pinning
**`L = min(L_beam, 111.125 mm)`** puts the bass strike proportionally
further out along its tine, where it couples far less to the high modes.

    xi:  0.514 (E1) -> 0.260 (MIDI 45) -> 0.141 (E7)

Striking near the clamp couples strongly to the high modes, because mode 1
has almost no motion there. Normalised at mode 1, the shape factors are

    mode 2:  1.981 (E1) -> 4.193 (MIDI 45) -> 5.201 (E7)
    mode 3:  0.167 (E1) -> 7.123 (MIDI 45) -> 11.918 (E7)

Mode 3 nearly vanishes at the bottom because the capped `xi` of 0.514 lands
almost exactly on that mode's node at 0.5035 — a real consequence of the
corrected length, not a fitted one.

They replace the flat [FOLK] triple EP0 pinned. The core has no libm and
the length law has a corner in it, which defeats a polynomial, so both
curves are **pinned as 73-entry tables**; the test checks every entry
against the exact transcendental shape. Register item 9 is closed by this.

**How long it stays (derived shape, fitted time).** A contact of duration
`t_c` cannot excite anything far above `1/(2 t_c)`, and the excitation
falls as `1/f^2` beyond — the envelope of a finite-duration force pulse.
Model that, **normalised at mode 1** so that the level law of 5.1 and the
spectrum law here stay independent decisions:

    f_c(v, zone) = EP_CORNER_HZ[zone] * (v / 127)^κ
    w(m, k, v)   = EP_BASE_W[m] * ep_mode_shape(k, m)
                 * (1 + (f_1 / f_c)^2) / (1 + (f_m / f_c)^2)

The pulse's own spectrum has nulls, which the envelope form deliberately
drops: nulls assume one exact contact duration, and real contacts vary
enough to wash them out. Keeping them would comb the compass and break the
monotonicity of brightness in velocity, which is the whole bell-to-bark
claim.

with **`κ = 1/4`** [derived from a 3.4:1 contact-time ratio across the
dynamic range — `127^(1/4) = 3.357`; the ratio itself is [FOLK]] and

    EP_BASE_W = { 1.00, 1.00, 1.00 }   [decision — EP3's by-ear trim]

`EP_BASE_W` is no longer a base weight, because the shape above is derived.
It is the one place the by-ear pass turns when the ear says there is still
too much bell, and what it is finally pinned at is EP3's gate.

The quarter power is not a rounding of a physical exponent — it is chosen
so the weight can be written as a ratio of squares,

    f_c^2 = EP_CORNER_HZ^2 * sqrt(v / 127)
    w     = EP_BASE_W[m] * (f_c^2 + f_1^2) / (f_c^2 + f_m^2)

which costs one square root per strike and no division by the corner. A
freestanding core with no libm pays for every transcendental it asks for,
and a strike is the only place this one is asked.

`w(1, ·, ·)` is exactly `EP_BASE_W[0]` by construction, so velocity moves
loudness through 5.1 and timbre through 5.2, and neither leaks into the
other.

Worked values at E4 (MIDI 64, zone 2, `f_c0 = 3600 Hz`) [derived]:

    velocity   f_c (Hz)   w(mode 2)   w(mode 3)
    127         227.3     0.1716      0.0431
     64         191.5     0.1561      0.0391
     16         135.4     0.1370      0.0342
      1          67.7     0.1225      0.0305

That is a 2.9 dB swing in bell content between the softest and hardest
blow, before the pickup adds its own level-dependent harmonics — much
narrower than the 10.3 dB the uncorrected corners gave, because a corner
an octave below the partial can only move it so far. The pickup's 41 dB of
second-harmonic swing (section 6) now carries nearly all of the
bell-to-bark travel, which is where the sources say it lives.

[AT20] is the qualitative check the model has to pass, on a real fork: a
soft blow gives a single narrow peak at the fundamental; a slightly harder
blow at the tip brings in the clang mode; a hard blow — tip excursion of a
couple of millimetres — brings in nine integer harmonics on top of the clang
tone. Modes 1 and 2 rising with velocity is section 5.2; the integer
harmonics are section 6.

### 5.3 Phase at the strike

A strike **resets the phase of any mode whose amplitude is exactly zero**,
and leaves the phase of a still-ringing mode alone [decision]. The first
half is physically plain — a tine at rest has no phase worth preserving —
and it has a useful consequence: the always-advance and active-gated bank
layouts of section 9 then produce bit-identical output, so D4 is decided on
cost alone. What a blow does to a mode that is *not* at rest is section
5.4's business.

### 5.4 Restrike — D5 closed: add, phase continues

What a blow does to a tine that is already ringing. **The blow adds to what
is there, and the tine rides through it.**

Four combinations were implemented and rendered as an A/B at EP2, and the
other three are now deleted rather than left as options — which is what
this section committed to. The measurement that decided it was a soft blow
onto a loud ring: under `replace` the note went **14.6 dB quieter**, and a
hammer puts energy into a tine and has no way to take any out. The ear
agreed and picked `add`; `continue` came with it.

`add` needs a bound, or a fast repeated note grows without limit. The bound
is not a new constant: **a blow may not push a mode past the amplitude its
own hardest single blow would give it** [decision] — the hammer has a
maximum and the tine has an escapement and a pickup gap in front of it.

The phase rule of section 5.3 stands above this: a mode at exactly zero
always resets. That is what keeps the two bank layouts bit-identical and
decision D4 standing, and it survives D5 untouched.

### 5.5 The contact transient — the hammer noise

The tip meeting the tine is a mechanical event of its own, not part of any
mode. It is short, broadband and loudest relative to the tone at soft
dynamics, which is much of what makes a struck instrument read as struck
rather than as a quiet sine.

    burst(t) = noise(t) * EP_HAMMER_LEVEL[zone] * sqrt(level) * decay(t)

- **`EP_HAMMER_LEVEL = { 0.160, 0.150, 0.130, 0.110, 0.100 }`** [FOLK], the
  burst's rms against the strike level, per hammer-hardness zone.
- **`EP_HAMMER_MS = { 12, 10, 8, 6, 5 }`** [FOLK], its t60. Softer, heavier
  bass tips thud longer.
- **`EP_HAMMER_HZ = { 1200, 1500, 1900, 2400, 3000 }`** [FOLK], its own
  bandwidth — a one-pole lowpass on white noise.
- Level rises as **the square root** of the strike level while the tone
  rises linearly (section 5.1), so the knock gains about 6 dB on the tone
  for every 12 dB the tone loses. At E4 the burst sits 16 dB under the
  first 10 ms of the tone at velocity 8 and 22 dB under it at velocity 32.

Three things about this are worth stating because each was got wrong first.

**Its bandwidth is not the mode roll-off's corner.** Contact *duration*
decides which modes can ring (section 5.2); contact *hardness* decides how
bright the knock itself is. They are different questions and reusing the
5.2 corner put a 113 Hz filter — time constant 1.4 ms — in front of a burst
that is over in about 1 ms, which threw away some 40 dB and left the
constant meaning nothing like what it said.

**It is normalised.** Uniform noise on [-1, 1) carries rms 1/sqrt(3), and a
one-pole at coefficient c passes variance c/(2 - c). Both are divided back
out at the strike, so `EP_HAMMER_LEVEL` is an rms ratio a person can
calibrate rather than a number that happens to work.

**It enters after the pickup, not before.** The magnet's field geometry
acts on tine displacement; folding a noise burst into that stage would also
feed the quadratic term a nonzero mean square and slide a DC step under
every attack (section 6). The difference is second order and the DC is not.

### 5.6 Determinism, and a revision to the backlog

`piano-backlog.md` states that the EP core uses no RNG until EP7, on the
grounds that "a strike is fully determined by its event". The transient
needs a noise source, and a fixed seed derived from the note and the strike
ordinal **is** fully determined by its event — so the reason stands while
the prediction does not. The seed is
`HAMMER_SEED ^ note*C1 ^ ordinal*C2`, drawn with the repo's `tw_splitmix64`,
the same pattern as M7's per-wheel character draws.

Both the seed and the filter state are reset at every blow. That is not
tidiness: without it the always-advance and active-gated layouts would
enter a strike with different noise state — one of them advances a silent
voice and the other does not — and section 9's bit-identity, which decision
D4 rests on, would quietly fail. A test strikes a note, lets it fall to
exact silence, strikes it again, and compares the two layouts sample for
sample.

The successive-blow ordinal matters for a second reason: without it a trill
would replay one waveform, which buzzes.

## 6. Pickup nonlinearity

One pickup per tine — a coil wound on a permanent magnet whose steel tip
faces the free end of the tine [EP-P61; EP-P77; EP-DAFx17 §3.1]. As with
the organ's wheels, each voice meets only its own pickup, so this stage
produces harmonic distortion and **no intermodulation**; IMD enters first at
the shared preamp (EP5). The stage therefore runs **per voice, before
summation**.

Two sourced facts fix its shape:

- The magnet's chisel edge is deliberately set **off-centre** relative to
  the tine axis at rest, and the manual's timbre adjustment tells the
  technician to leave the tine "slightly above dead centre of the pickup"
  [EP-P61; EP-SM 4-7]. The patent is unusually direct about why: the
  off-centre relationship "permits accurate adjustment of the
  fundamental-overtone relationships sensed by the pickup ... highly
  important to the musical characteristics of the instrument".
- The tine's own motion is approximately sinusoidal; the complex waveform
  appears only behind the pickup [EP-DAFx17 §4.2, Fig 3]. The timbre change
  from displacing the rest position is shown directly [EP-DAFx17 Fig 7a/7b].

**Kernel: the field the tine sweeps through, taken from the geometry.**

    Psi(u) = (1 + u^2)^(-3/2)
    y      = Psi(u0) - Psi(u0 + g * x)

`u` is the tine tip's lateral offset from the pole axis **in units of the
pickup's own gap**, `u0` the rest offset (6.1), `g` the swing per unit
strike (6.1), and `x` the tine displacement the bank produces.

The shape is derived, not chosen. [EP-DAFx17 §5.4] reduces the pickup to
Faraday's law over a field it builds from magnetic point charges, giving
`Bz = B0 * dz / |r|^3` [eq 6], and the tine tip sweeps that field laterally,
`x' = x_hat * sin(2 pi f t)` [eq 8]. For a point charge at fixed gap `d` and
lateral offset `s`, the linked flux goes as `d / (s^2 + d^2)^(3/2)`, which
in gap units is the expression above. One parameter, and it is a geometry.

**Three consequences, and they are the reason this replaced the saturator.**

*It is a bell, not a saturator.* The flux is maximal when the tine is in
front of the pole and falls away on both sides. It is even in `u`, so at
`u0 = 0` it passes **no fundamental at all** — which is what makes the
patent's off-centre claim a mechanism rather than a remark. The rest offset
is the fundamental-to-overtone control the patent says it is.

*It has no rail.* It falls as `u^-3` and never flattens. This is what the
previous kernel got wrong and it was audible: `tw_sat` is the organ's power
stage saturator, and tonewheel.h says so in as many words — the odd kernel
is kept for "the rotary's 40 W ceiling, where a power stage wants a hard
bound". A power amplifier has a rail. **A magnetic field does not.** The
borrow carried the rail across, and at the pinned drive the bass sat on it:
at E1 and velocity 100 the operating point reached `u = 3.18` against a
curve clamped flat at `|u| = 3`, and at velocity 127 the whole bottom
twenty notes were clipped outright.

What that cost is measurable, and it is the envelope. Section 4 pins E1 at
`t60 = 17 s`, which is 3.53 dB/s. Rendered through the railed saturator the
fundamental fell **0.77 dB/s** between 0.3 s and 1 s — a fifth of its own
pinned rate — and the harmonic ladder did not move either: `h3` sat within
1.4 dB of itself for the first 1.5 s. A note that holds level and holds
timbre for a second is a struck bar with a resonator under it, not a tine,
and it is the direct contradiction of the founding patent's "an initial
percussive effect followed by a relatively rapid decay and then by a
limited dwell" [EP-P61]. The compression was graded across the compass,
worst at the bottom: 22 % of the pinned rate at E1 against 84 % at E4.

*The two-stage envelope now falls out, as section 4 said it should.* With
the field in place the same note decays at **4.32 dB/s** over its first
0.3 s, then settles to 2.79 and 3.28 and 3.48 against the pinned 3.53 —
percussive, then rapid, then dwell. Section 4 bet that the patent's shape
would emerge from three modes plus a level-dependent pickup rather than
needing an explicit envelope stage. Against the saturator that bet failed
in the bass; against the field it holds, and open item 8 closes with it.

    E1, velocity 100, dry, decay of the f1 band in dB/s (pinned 3.53)

    window        0.05-0.3   0.3-1    1-3    3-6
    saturator         2.20    0.77   1.96   3.22
    field             4.32    2.79   3.28   3.48

Measured on a rendered note at velocity 127, the ladder the field builds:

    MIDI    g      h2     h3     h4     h5    clang
      28  0.500  -13.8  -18.8  -28.1  -24.5  -20.6
      64  0.297  -19.1  -28.9  -62.3  -60.9  -19.2
      88  0.210  -22.9  -35.2  -67.9  -74.6  -19.1

Even-dominant and continuous, thinning toward the treble, and the clang no
longer stands over a hole. The second harmonic is the octave the off-centre
pickup makes, which is the instrument's growl; the old kernel's ladder ran
odd-dominant because a symmetric saturator clipped on both halves.

### 6.1 The two setup adjustments

The manual keeps **two** adjustments here and they are independent. The
model now carries both, in the same units the technician works in, and
neither is derivable — the manual says to set them by ear.

**TIMBRE — the rest offset `u0`.** "Manipulating the Timbre Adjustment
Screw until the end of the Tine rests on a plane slightly above dead center
of the Pickup ... Let your ear guide you" [EP-SM 4-7].

- **`EP_PICKUP_OFFSET = 0.35`** [decision, **closed by ear 2026-07-28**].
  Chosen off the four-way ballot in
  `renders/ep73-ballots-field-20260728/`; the losing settings were 0.25,
  0.30 and 0.40. Nothing but this constant distinguishes them, so there
  is no dead code to remove the way decision D5 left four restrike laws
  behind — the ballot renders stay as the evidence for the pin.

What the field does supply is a map, and one value inside it to avoid.
`Psi''` vanishes at exactly `u = 1/2`, because the maximum of `|Psi'|` is
the field's inflection point, and second-harmonic generation vanishes with
it. That offset was pinned here briefly for exactly the wrong reason — it
maximises output, which is the *volume* adjustment's job, not timbre's —
and the EP1 exhibit caught it immediately: H3 came out above H2 and the
voice went hollow. Below the null the ladder is even-dominant and densifies
as the offset shrinks; above it, it is even-dominant again but the
fundamental is falling away. At a swing of 0.3 gaps:

    u0     h1 (dB)   h2-h1   h3-h1
    0.20    -16.99    -9.7   -26.3
    0.25    -15.43   -12.7   -26.9
    0.30    -14.31   -15.7   -27.6
    0.35    -13.51   -18.9   -28.5       <- pinned
    0.40    -12.95   -22.7   -29.5
    0.50    -12.42   -37.9   -32.0       <- the null: odd-dominant, hollow
    0.65    -12.65   -26.7   -37.3

0.35 is "slightly above dead centre", clear of the null, and leaves the
second harmonic about 14 dB over the third at E1 and 10 dB over it at E4.

**VOLUME — the swing per unit strike `g`.** "Slide Pickup Arms in or out to
establish a gap between Pickup and Tine of between 1/16" (1.588 mm) and
1/8" (3.175 mm) ... the smaller the gap, the greater the volume of sound.
More important — the more pronounced the DYNAMIC RESPONSE" [EP-SM 4-8].

    g(key) = EP_PICKUP_DRIVE_REF * (f_ref / f(key))^EP_PICKUP_SLOPE

- **`EP_PICKUP_DRIVE_REF = 2^(-11/8) = 0.38555271`** at `f_ref = 329.63 Hz`
  (E4) [decision, by ear]. Pinned so the hardest blow on the lowest tine
  sweeps exactly half a gap: mode 1 at velocity 127 has amplitude exactly
  1.0 by the section 7 reference, E1 sits three octaves under E4 and the
  octave is an exact power of two, so `g(E1) = 1/2` closes as
  `2^-1 * 2^(-3/8)` at the pinned slope. The anchor is held fixed when the
  slope moves, which is what made the slope ballot decide one thing. The gap is a number a technician sets and the swing it
  is measured against has no figure in any source here, so their ratio is a
  decision; what is not a decision is what it costs, and this pin leaves E1
  at 79 % of its pinned decay rate against the saturator's 22 %.
- **`EP_PICKUP_SLOPE = 1/8`** [decision, **closed by ear 2026-07-29**]. Momentum alone gives 1, since
  swing goes as `1/f` for a given blow. The gap is not constant across the
  compass — 1/16" to 1/8" generally, and 0.020" (0.508 mm) "can be
  accommodated in the middle and upper ranges" on instruments built since
  March 1972 [EP-SM 4-8] — and a wider bass gap divides the bass swing
  down, so the exponent sits under 1. **That direction is sourced; the
  magnitude is not.** The manual names two zones without saying how far
  apart they sit, and the implied exponent runs from 0.12 to 0.45 across
  plausible spans, so it was settled on the ballot of 2026-07-28
  (`renders/ep73-ballots-slope-20260728/`, four settings, bass anchor held
  fixed and every take rms-matched). 1/8 is at the shallow end of the
  bracket, and is a ratio of eighths, so `r^(1/8)` is three nested square
  roots and the core still needs no libm — the same move section 5.2 made
  for `kappa`. It keeps the growl nearly even across the compass: the
  second harmonic runs -13.8 dB at E1 to -19.4 dB at E7 at full strike,
  against the 21 dB spread the steepest ballot setting gave.

**No cap.** The old kernel carried `PICKUP_DRIVE_MAX = 3.5`, described here
as "a stated limit, not a derived one", to keep a saturator plausible. It
had a side effect nobody had looked at: it pinned the bottom nine notes,
E1 to C2, to one identical drive value, so the model had no per-note
gradation at all down there. The field needs no such limit — the law rises
toward the bass and the compass ends, so `g(E1)` is its maximum by
construction and the strike level is bounded with it. The constant is
deleted rather than re-tuned.

The bark is where the tine crosses dead centre, and that is now a geometric
statement rather than a threshold: `g(key) > u0`. At the pinned slope it
holds from E1 up to **F5 (MIDI 77), fifty of the seventy-three notes** — so
at 1/8 the growl is not a bass phenomenon but a compass-wide one that
merely thins toward the top. At the 1/4 setting this section carried until
2026-07-29 it stopped at E3, twenty-five notes. The bark's reach is a
consequence of the slope pin and moves with it; what does not move, and is
what the test asserts, is that the lowest tine crosses at full strike and
the highest never does.

### 6.2 The coupling capacitor

A tine sitting off-centre in the field leaves a level-dependent DC behind
it: the field is even about dead centre and the rest offset is not. That DC
has no closed expression to subtract, so it is filtered off where the
instrument filters it off — a one-pole highpass at **10 Hz**, once, on the
summed bus rather than per voice, because the coupling capacitor sits at
the preamplifier's input and there is one of it.

It costs E1 about a quarter of a dB. It also means a silenced instrument
reaches exact zero only after the capacitor discharges, which the same
epsilon snap every envelope here uses then completes.

### 6.3 What the kernel costs to compute

`(1 + u^2)^(-3/2)` is an inverse square root cubed, and the core has no
libm. The seed is exponent halving and negation (`0x5f400000 - (i >> 1)`),
worst relative error 8.9 % over the range `u` takes, and three Newton steps
bring that to 7.3e-8 — f32-exact. `y = 1 + u^2 >= 1` always, so there is no
zero, denormal or negative case to guard. The test checks the whole range
against the transcendental with libm, so the kernel is validated rather
than trusted, exactly as the mode-shape tables are.

## 16. The second polarisation

A tine is a round wire. It vibrates in two transverse planes, not one, and
the two are split slightly in frequency by whatever asymmetry the wire and
its mounting carry. Beating between them is what makes a real note breathe
instead of decaying smoothly, and it is the single thing that most marked
the model as synthetic on the by-ear pass.

Two sources appear to disagree about it, and the disagreement is the design.

[EP-DAFx17 sec 4.2] **measured** it: high-speed camera tracking of one tine
in two horizontal dimensions shows non-planar motion, with the vertical
plane — the hammer's direction — larger than the horizontal, and the
horizontal "excited either through coupling effects on the tine or due to
imperfections in the hammer tip".

[EP-P61] says the instrument is **built to prevent it**. The inertia bar is
"so constructed and mounted as to be incapable of supporting any
substantial vibration of the associated tine in a plane other than the
common plane in which the hammer moves", and the patent gives the reason:
out-of-plane motion produces "an undesirable beating noise in the
loudspeaker".

So the design suppresses it and real instruments keep some anyway. That
makes it **a deviation, not a feature** — which is why it lives behind
`condition` with everything else in section 15, and why `condition = 0`
has no second plane at all.

    COND_POLAR_SPLIT = 0.004    the twin's frequency offset, +/-   [FOLK]
    COND_POLAR_DEPTH = 0.60     modulation index at a full strike  [FOLK]

**It multiplies, it does not add.** The pickup's chisel edge stands
perpendicular to the plane the hammer drives [EP-P61], so horizontal motion
induces very little by itself; what it does is carry the tine across the
field and change how well the vertical motion couples. So the horizontal
twin is applied as a gain on the voice, and what comes out is a slow
breath rather than a second pitch. The pickup's mean-square term is scaled
by the same factor squared, so section 6's DC removal stays exact.

The split is a **ratio**, because the asymmetry is in the wire's section
and its mounting rather than an absolute frequency. The beat rate therefore
rises with pitch, which is what a real instrument does: the bass swells
slowly and the treble shimmers.

Measured, at a velocity-110 strike:

    MIDI   condition   modulation peak-to-peak   beat period
      40      0.0            0.00 dB              —
      40      0.5            3.48 dB              11.7 s
      40      1.0            7.27 dB               5.8 s
      64      0.5            0.78 dB               1.66 s
      88      0.5            2.76 dB               0.54 s

The spread between notes at one condition — 0.78 dB against 3.48 dB — is
the per-note draw and is the point: some notes breathe and some barely do.

One thing was got wrong first and is worth recording. The excitation and
the pickup's sensitivity to it were separate depths, each a small number,
and their product came out around two percent — inaudible. What the ear
cares about is the modulation index that actually reaches the bus, so that
is the single quantity now pinned. Two plausible small factors multiplied
together is a good way to build a stage that measures correct and does
nothing.

## 15. Condition — EP7

The wear pass, and the answer to the one thing the by-ear listening kept
returning to: **the instrument was too uniform**. Seventy-three identical
idealized tines, every one tuned exactly, voiced exactly, at exactly the
same distance from its pickup. No real instrument is like that and no
listener mistakes one that is.

One knob, `condition`, 0..1, scaling four per-note deviations and two
floors. Every one is exactly neutral at 0, so `condition = 0` reproduces
the pre-EP7 instrument bit for bit even mid-note — the scanner-OFF
discipline, asserted by test and confirmed on a four-and-a-half minute
performance whose signature is unchanged. Draws come from one fixed seed,
two per key, split into 13-bit fields: the M7 pattern.

    COND_DETUNE_CENTS = 6.0     tuning drift, +/- at condition 1  [FOLK]
    COND_RATIO_DEV    = 0.04    clang ratio per note, +/-         [FOLK]
    COND_VOICE_DEV    = 0.25    per-note voicing trim, +/-        [FOLK]
    COND_ALPHA_DEV    = 0.30    pickup distance, +/-              [FOLK]
    COND_HUM_LEVEL    = 1.3e-4  mains hum, -78 dB                 [FOLK]
    COND_FLOOR_LEVEL  = 2.5e-4  broadband floor, -72 dB           [FOLK]

Three of these are more than taste.

**The clang ratio drifts per note, by construction.** The founding patent
says the tuning spring sits near the free end deliberately, "in order that
variations in spring locations will alter the fundamental frequencies and
not the harmonics" [EP-P61]. Tuning a tine therefore moves `f1` and leaves
the overtone where it was — the ratio is *different on every note of a real
instrument*, and section 3 pinned a single value only because a base had to
exist. This is where it gets its spread.

**The pickup distance is a documented setup tolerance, not a defect.** The
manual gives the gap as 1/16 to 1/8 inch, and 0.020 inch in the middle and
upper ranges on later instruments [EP-SM 4-8] — a two-to-one range a
technician sets by ear, note by note. 30 % sits well inside it.

**The hum floor is structurally below the organ's.** The pickup coil is
wound in two sections in opposite phase for hum cancelling
[EP-DAFx17 sec 3.1], so transplanting M7's -60 dB mains line would have
been wrong. -78 dB is the working figure.

Measured across the knob:

    cond   worst detune   pickup spread    idle floor
    0.0      0.00 cents   1.000 .. 1.000   silent, exactly
    0.2      1.19 cents   0.941 .. 1.056   -89 dB
    0.5      2.98 cents   0.853 .. 1.140   -81 dB   <- shipped
    0.6      3.58 cents   0.824 .. 1.167   -80 dB
    1.0      5.97 cents   0.706 .. 1.279   -75 dB

**The shipped default is 0.5**, not 0, because tolerance effects exist on a
factory-new instrument. That is the organ's own doctrine, and the split is
the same: `ep_bank_init` builds the idealized bank, `ep_piano_init` applies
the shipped condition, exactly as `tw_generator_init` and `tw_organ_init`
divide it. An idle instrument therefore carries a floor near -81 dB, and
the test that used to assert exact silence now asserts exactly that.

The value was first pinned at 0.2, by analogy with the organ's shipped
wear. **The ear moved it to 0.5** — which is worth recording rather than
quietly adopting, because it says something: this instrument wants more
character than the organ does. That is consistent with what it is. The
organ sums ninety-one always-running wheels, and its deviations pile up
across the whole bank before they reach an ear. Here a note is one tine
and three partials, alone and exposed, so the same amount of uniformity is
far more audible. At 0.5 the tuning spread is about 3 cents worst case,
which is what makes a chord shimmer instead of standing still.

The floors are **bank-level, not per voice**. A floor is there whether
anything is playing or not, and bank level also means both tick layouts
advance it identically — which is what keeps decision D4's bit-identity
intact. A per-voice floor would have broken it.

Not included, and named rather than forgotten: the damper and key-bed
mechanical noises of the section 11 inventory. They are events rather than
per-note character, they need their own trigger path, and nothing measured
so far demands them. If the ear asks, they are an addition, not a gap.

## 14. Cabinet — EP6, decision D7

D7 asked for one of two things: a short rolloff-plus-early-reflections
treatment "reusing the M6 cabinet form", or explicit deferral. **Neither,
as posed** — the M6 cabinet form does not exist. The organ deferred its
early reflections too (`m6-evidence.md`: "deliberately not modeled at M6"),
so the backlog was pointing at something that was never built.

D7 is therefore answered by splitting it:

- **Landed: the loudspeaker's bandwidth.** A box and a cone impose a
  passband on anything played through them, and the model had none at all.
  This is not new DSP and not speculative.
- **Declined: the early-reflection set.** It has no source here, the organ
  declined the same thing on the same grounds, and deletion pressure
  applies. If the ear later asks for it, it is a named addition rather than
  a gap.

Also recorded: the cabinet is a **variant, not the instrument**. The
suitcase model plays through its own speaker; the stage model is fed to an
external amplifier and has no cabinet at all. That is why bypass is the
default rather than a compromise setting.

    EP_CAB_LOW_HZ  =   80    one pole   — the box
    EP_CAB_HIGH_HZ = 4000    two poles  — the cone

Both [FOLK], from ordinary loudspeaker behaviour: a twelve-inch cone in a
box of this size runs out below roughly 80 Hz and breaks up above roughly
4 kHz. **They were pinned before the result was measured, and not adjusted
afterwards.**

The knob is a dry/wet blend, so 0 is bypass and 1 is the modelled speaker.
At 0 the stage returns its input and touches no state, the way the organ's
rotary bypass does — blending with a zero coefficient is not sufficient,
because `a + 0*(wet - a)` turns -0.0 into +0.0, which is a real hole in a
bit-exactness claim even though nobody would hear it.

One consequence worth stating plainly: **the bass fundamental does not
survive the cabinet.** E1 is 41 Hz and the box gives up an octave above
that. A real instrument of this kind does not reproduce its own lowest
fundamental either, which is part of why its bass register sounds the way
it does — the note is carried by its harmonics.

## 13. Drive — EP5

The preamp stage, reusing the organ's `tw_drive` whole: bias-excursion
follower, coupling-cap highpass, makeup to unity small-signal gain. No new
DSP, which is what `piano-backlog.md` predicted.

**Kernel: the odd one, not the derived triode curve** [decision, recorded
per the warmth doctrine]. Two reasons, and the second is the stronger.
This instrument's preamplifier is solid-state — the manual's schematics are
transistor and op-amp designs [EP-SM 11-1] — and symmetric clipping is what
such a stage gives, where the triode curve's asymmetric rails belong to a
tube. And the voice already carries a pronounced asymmetry of its own from
the off-centre pickup (section 6); a second asymmetric stage on top of it
would double an effect the sources place in exactly one part of the
instrument.

**Operating point: `EP_DRIVE_SCALE = 2`** [decision]. `tw_drive` refers
internally to the organ's signal level, and this instrument runs lower —
its bus peaks around 1.4 to 4.5 on real material against the organ's
reference of 8 — so the EP scales into the stage and back out again. Two
rather than any nearer number because it is **a power of two**: the scale
in and out is then exact in f32, and `drive = 0` stays a bit-identical
bypass of the whole instrument, asserted by test.

### 13.1 What the stage is for

The bare voice bank is peaky. Measured over the reference loop library, its
crest factor is about **5.0**; the model without drive measures **7.8** on
comparable material. That gap is the amplifier and the recording chain the
references went through and the model did not, and it is the most concrete
thing the reference set has told this line.

Measured on a four-note pedalled figure, the knob closes it directly:

    drive   crest
    0.000    8.82
    0.125    5.47
    0.250    4.48
    0.500    3.70
    1.000    2.69

So **a drive of roughly 0.15 puts this instrument at the reference set's
own crest factor** — a setting arrived at by measurement rather than by
taste. It is not a shipped default: `drive = 0` remains the identity, and
what the instrument should sit at is EP3's by-ear business along with
everything else in the voice.

## 12. Tremolo — EP4

One LFO and a gain law, and the first stereo in this instrument. Two
variants behind one control, as `piano-backlog.md` asks: the mono amplitude
wobble, and the opposing stereo pan of the cabinet-equipped model.

    lfo   = sin(2 pi phase)
    AM :  l = r = x * (1 - depth/2 * (1 - lfo))
    PAN:  l = x * (1 - depth/2 * (1 - lfo))
          r = x * (1 - depth/2 * (1 + lfo))

- Rate **1.0 to 10.0 Hz, default 5.5** [FOLK — the oscillator's component
  values are in the preamplifier schematic [EP-SM 11-1], which is a scanned
  figure; EP4 either reads it or keeps these].
- Depth 0..1. **Depth 0 is a bit-exact bypass**: both gains are exactly 1
  and the stereo tick reproduces the mono one sample for sample, so every
  pre-EP4 render stays pinned (the scanner-OFF discipline).
- The LFO runs whatever the depth is. It is an oscillator in the amplifier,
  not something the panel switches on, and at depth 0 it changes nothing.
- The pan variant conserves the sum: `l + r = x` at every phase, so
  collapsing to mono removes the effect rather than leaving a wobble.
- CC91 covers off, both variants and the whole depth range in one control,
  the way the organ's CC84 covers off and six vibrato positions: **0 is
  off, 1..63 is the mono wobble with rising depth, 64..127 is the stereo
  pan with rising depth** (section 10.1).

## 7. Summation and reference level

Passive sum of the 73 per-voice pickup outputs, in ascending key order
[decision — matches the organ's passive busbar sum; the driver's gain flag
handles absolute level]. Reference: one voice at velocity 127 has mode-1
amplitude exactly 1.0, which is what makes the `alpha * A / 4` ladder above
a calibration anchor rather than an arbitrary scale.

No static voicing filter at EP0. The backlog allows one "only if the ear
demands"; nothing has demanded it yet, and the default is absent.

## 8. Damper and pedal — EP2 owns the code

Every tine has its own felt damper bearing on it from below, released by
that key's hammer through a bridle strap, and released across the whole
compass by the sustain pedal through the damper release bar [EP-SM 2-1,
2-2, 4-5]. Damper felts and arms are graded in three sections: long wide
felts and full-width arms in the bass, medium in the middle, short narrow in
the treble [EP-SM 2-2]. Damping should be immediate on key release
[EP-SM 4-6].

Pinned working values [FOLK — the manual gives the mechanism and the
grading, no times; EP3 owns the numbers]:

- `EP_DAMP_T60_E1 = 0.35 s`, per-semitone ratio
  **`EP_DAMP_T60_RATIO = 0.981119`**
  [derived: same `f^-q` shape as section 4 with `q = 0.33`, giving 0.0887 s
  at E7 — a bass damper has a long heavy tine to stop, a treble damper has
  almost nothing].
- The damped t60 carries **no per-mode factor**, unlike the free one: the
  felt sets the rate and does not care which mode it is stopping.
- One rule covers the whole damper surface: **the damper is off when the
  key is held or the pedal is down, and on otherwise.** Release, pedal
  down, pedal up, catching the pedal late under a note already decaying
  under its damper, and letting the pedal go while keys are still held all
  fall out of it. A strike lifts the damper by itself, because the hammer
  does — the bridle strap pulls the damper arm down as the hammer swings
  [EP-SM 4-5].
- Sustain pedal: CC64, `>= 64` = down (section 10.1).
- Panic (CC120/123) drops all dampers and lets go of the pedal; the
  instrument silences in the damper tau rather than by a hard mute. The
  bank's hard mute stays available for tests, not for performance.
- Half-pedalling is **not** modelled [decision — deferred; there is no
  continuous-position source for this action and no EP milestone owns it].

Recorded and deliberately not modelled: with the pedal down the manual
describes the undamped tines vibrating sympathetically with the struck ones,
"as is the case with an acoustic piano" [EP-SM 2-2]. `piano-backlog.md`
excludes inter-note coupling from this line by design — it is the acoustic
program's A5. Named here so the omission is a decision with a source, not an
oversight.

## 9. Bank layout — D4, closed by measurement at EP1

Two candidate layouts, identical in output by section 5.3, different in
cost:

- **always-advance** — all 219 mode oscillators tick every sample,
  amplitudes zero when silent. Constant cost, branch-free, mode-major SoA
  banks of 73 floats that auto-vectorize like the wheel banks.
- **active-gated** — voices at exactly zero amplitude are skipped entirely,
  walking an ascending list of live keys, key-major. Typical cost far below
  the organ; identical worst case.

The organ's "never branch-gated" rule does not bind here. That rule exists
to protect wheel phase continuity, and section 5.3 removes the EP's
equivalent concern.

**Closed at EP1: active-gated** (`docs/ep1-evidence.md` section C). Measured
on the development host, both layouts bit-identical over a four-second
script: at six to twelve voices the gated layout costs 0.7-1.5 % of one
core against a flat 10 %, and at the full-compass worst case the two sit
within run-to-run variance of each other. `ep_bank_tick` survives as the
constant-cost reference the bit-identity test asserts against.

The second measurement point the backlog asks for — the SBC class of
`design.md` — is not attached to the development host, so D4 closes on the
host measurement plus a stated projection. The SBC number stays open
(register item 11).

## 10. Controls, MIDI

Working set, per `piano-backlog.md`: `condition` (the wear analogue),
`drive`, `tremolo`, `cabinet`. Sustain pedal is performance state, not a
panel control.

- Notes 28..100; out-of-compass ignored and counted.
- Velocity scales loudness and timbre (sections 5.1, 5.2) — the first time
  in this codebase.
- CC64 sustain (section 8).
- Poly key pressure has no EP meaning yet: parsed, ignored, counted. The
  organ's use of it for key depth has no analogue here — a key that is down
  has already thrown its hammer.
- CC11 unassigned; a volume assignment is [decision D6], not a default.
  The instrument has no expression pedal.

### 10.1 The CC map — pinned at EP2, D6 closed

Numbers are chosen so one controller can drive both instruments without
remapping: the organ's map occupies CC11, CC70-78 and CC80-90, so the
piano takes CC64 (where the sustain pedal already lives by MIDI
convention), reuses CC85 for drive, and continues upward from CC91.

    CC64    sustain pedal        >= 64 is down          EP2, wired
    CC85    drive                value / 127            EP5, wired
    CC91    tremolo              value / 127, 0 = off   EP4
    CC92    cabinet              value / 127, 0 = bypass EP6, wired
    CC93    condition            value / 127            EP7, wired
    CC120   all sound off        drops all dampers      EP2, wired
    CC123   all notes off        drops all dampers      EP2, wired

Everything from CC85 upward is reserved, not implemented: the milestone
named beside it wires it, and until then the message is parsed and
ignored like any other. Each reserved control's zero is its bit-exact
bypass, so wiring one later cannot move an earlier render.

Deliberately unassigned:

- **CC11** — the organ's swell. This instrument has no expression pedal
  and no swell stage, so an assignment here would be an output-gain trim
  with nothing physical behind it [decision D6]. If the operator wants a
  volume pedal it lands here, and that is his call, not a default.
- **Poly key pressure (0xA0)** — parsed, ignored, counted. The organ uses
  it for key depth, which has no analogue here: a key that is down has
  already thrown its hammer, and the tine no longer knows where the key
  is.
- Note-on with velocity 0 is a note-off, per the MIDI convention the
  organ's parser already follows.

Identity defaults, each a bit-exact bypass (the scanner-OFF discipline):
`condition = 0`, tremolo off, `drive = 0`, cabinet bypass. The pickup
nonlinearity of section 6 is **not** one of them: it is always on, because
it is the instrument rather than a deviation from it. `condition` scales a
spread *around* the pinned alpha, and `condition = 0` puts every voice at
exactly the pinned value.

## 11. Later milestones — slots, not values

- **Tremolo / pan (EP4).** One LFO and a gain law: a mono AM variant and a
  stereo opposing-pan variant behind one control; off is a bit-exact mono
  bypass. Working range 1.0-10 Hz, default 5.5 Hz, depth 0..1 [FOLK — the
  oscillator's component values are in the preamplifier schematic
  [EP-SM 11-1], which is a scanned figure; EP4 either reads it or keeps
  these].
- **Drive.** Landed at EP5 — see section 13.
- **Cabinet / speaker.** Landed at EP6 — see section 14.
- **Condition.** Landed at EP7 — see section 15.

## 12. Open-items register

Every item names the milestone that closes it. An item with no owner is a
defect in this document.

| # | Item | Tag | Owner |
| - | ---- | --- | ----- |
| 1 | `EP_MODE_T60` extra damping factors (0.35, 0.15) | [FOLK] | EP3 |
| 2 | `EP_BASE_W` by-ear trim (1, 1, 1); the shape is now derived | [decision] | EP3 |
| 3 | Contact times behind the corners (4.0..1.1 ms) | [FOLK] | EP3 |
| 4 | Contact-time ratio 3.4:1 behind `κ = 1/4`; the 9.0..1.95 ms ladder | [FOLK] | closed by ear at EP3; revisit on a clean reference |
| 5 | `γ = 1.042` velocity-to-level exponent | **closed at EP3** | — |
| 6 | Pickup kernel: the field of sec 6, `(1+u^2)^(-3/2)` | **closed: derived from [EP-DAFx17] eq 6+8** | — |
| 6a | Contact-transient level, decay and bandwidth (sec 5.5) | [FOLK] | EP3 |
| 6b | `EP_PICKUP_SLOPE = 1/8`; direction sourced [EP-SM 4-8], magnitude not | **closed by ear 2026-07-29** | — |
| 6c | `EP_PICKUP_OFFSET = 0.35` — the manual's TIMBRE adjustment | **closed by ear 2026-07-28** | — |
| 7 | Damper t60 anchors (0.35 s / 0.0887 s) | [FOLK] | EP3 |
| 8 | Whether an explicit two-rate mode-1 envelope is needed | **closed: no** | the field produces it — sec 6 |
| 9 | Mode-shape derivation of the strike weights | **closed at EP3** | — |
| 10 | Bank layout (D4) | closed at EP1: active-gated | — |
| 11 | SBC-class measurement point for D4 | open | operator |
| 12 | Restrike law (D5) | **closed at EP3: add, phase continues** | — |
| 13 | CC map, CC11 assignment (D6) | open | EP2 |
| 14 | Tremolo rate/depth from the preamplifier schematic | [FOLK] | EP4 (range pinned, values still folk) |
| 15 | Drive kernel choice, triode curve vs odd kernel | **closed at EP5: odd** | — |
| 16 | Cabinet stage (D7) | **closed at EP6: bandwidth in, reflections declined** | — |
| 17 | Reference-recording set for the by-ear pass | open | operator, before EP3 |
| 18 | `EP_PICKUP_DRIVE_REF = 2^(-11/8)` — the manual's VOLUME adjustment | [decision] | operator |
| 19 | `PICKUP_DRIVE_MAX` | **deleted at the field kernel** | — |
