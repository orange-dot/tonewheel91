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

`level(v) = (v / 127)^γ` with **`γ = 1.0`**
[derived: hammer velocity is proportional to key velocity through a direct
single action [EP-DAFx17 §3.1], the tine's initial velocity is proportional
to the hammer's, and the pickup's induced EMF is proportional to
`ω × displacement`, hence to the tine's initial velocity and *not* to
`1/ω` — so the electrical amplitude tracks key velocity linearly].
Velocity 1..127 therefore spans 42.1 dB. `γ` is the by-ear knob at EP3.

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

    zone   MIDI       durometer        EP_CORNER_HZ (at v = 127)
    0      28..50     30               1800
    1      51..60     50               2600
    2      61..70     70               3600
    3      71..84     90               5000
    4      85..100    wrapped          7000

The corner frequencies are [FOLK]: their *ladder* is sourced, their
*values* are a geometric working set (roughly x1.4 per step) chosen so that
the bass keeps a strong bell partial and the top of the compass is nearly a
pure fundamental. EP3 owns them.

A struck contact of duration `t_c` cannot excite modes far above `1/t_c`.
Model that as a one-pole roll-off on mode frequency, **normalised at mode 1**
so that the level law of 5.1 and the spectrum law here stay independent
decisions:

    f_c(v, zone) = EP_CORNER_HZ[zone] * (v / 127)^κ
    w(m, k, v)   = EP_BASE_W[m] * (1 + (f_1 / f_c)^2) / (1 + (f_m / f_c)^2)

with **`κ = 1/4`** [derived from a 3.4:1 contact-time ratio across the
dynamic range — `127^(1/4) = 3.357`; the ratio itself is [FOLK]] and

    EP_BASE_W = { 1.00, 0.55, 0.18 }   [FOLK — EP3 owns]

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
    127        3600.0     0.4172      0.0507
     64        3033.2     0.3802      0.0393
     16        2144.8     0.2921      0.0223
      1        1072.4     0.1278      0.0065

That is a 10.3 dB swing in bell content between the softest and hardest
blow, before the pickup adds its own level-dependent harmonics.

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
cost alone. The second half is provisional; **D5 owns the restrike law**
(amplitude replace versus add, phase continue versus reset) and closes it at
EP2 by A/B exhibit. EP1 runs `amplitude replaces, phase continues`.

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

So the bell-to-bark transition is a pickup effect driven by excursion, not a
new mechanical mode — which is exactly the M7 stage with a new operating
point, as `piano-backlog.md` predicted.

**Kernel: the organ's M7 form, unchanged** — the cubic series of
`(1 - exp(-alpha x)) / alpha`:

    y = x - (alpha/2) * (x^2 - S) + (alpha^2/6) * x^3

with **`EP_PICKUP_ALPHA = 1.1`** [decision — see targets below].
Coefficients `pk2 = alpha/2 = 0.55`, `pk3 = alpha^2/6 = 0.2016667`.

Two notes on this kernel:

- It is **monotone for every alpha**. `dy/dx = 1 - alpha x + (alpha^2/2)x^2`
  has discriminant `alpha^2 - 2 alpha^2 = -alpha^2 < 0` [derived]. The
  organ could rely on a small alpha; this line cannot, and does not have to.
- `S` is the mean of `x^2`, subtracted so the stage passes no DC. The organ
  could use the constant 1/2 because a wheel's amplitude is constant; here
  the amplitude decays, so an uncorrected quadratic term would drag a
  decaying DC thump behind every note. For a sum of modes at incommensurate
  ratios the mean is exact and free: **`S = (a1^2 + a2^2 + a3^2) / 2`**,
  from state the voice already carries [derived]. No highpass is needed and
  the drive stage's `drive = 0` bypass stays exact.

Harmonic targets, for a single mode of amplitude `A` [derived by expanding
the kernel at `x = A sin θ`]. The cubic term contributes to the fundamental
as well, so the ratios are not simply `alpha A / 4`:

    H1    = A * (1 + alpha^2 A^2 / 8)
    H2/H1 = (alpha A / 4)      / (1 + alpha^2 A^2 / 8)
    H3/H1 = (alpha^2 A^2 / 24) / (1 + alpha^2 A^2 / 8)

At `alpha = 1.1` and full excursion `A = 1` (velocity 127, mode 1 weight 1):

    velocity   A        H2/H1      H3/H1
    127        1.000    -12.44 dB   -27.17 dB
     64        0.504    -17.50 dB   -38.18 dB
     16        0.126    -29.24 dB   -61.96 dB
      1        0.0079   -53.29 dB  -110.10 dB

The second harmonic therefore rises very nearly one dB per dB of level,
about 41 dB of swing across the compass of velocities — the bell-to-bark
identity curve, in the one place the sources say it lives. `alpha` and the
resulting H2 ladder are the [decision] draft; **EP3 owns the final ladder
against reference recordings**.

One consequence worth recording for that pass: at `alpha = 1.1` a single
voice struck at velocity 127 peaks near 2.6 on the negative excursion
against 1.2 on the positive, a 2.4:1 waveform asymmetry. That is the
off-centre pickup doing exactly what the patent describes, and the kernel
stays monotone throughout, but whether the depth is right is an ear
question, not an arithmetic one.

Per-note spread of `alpha` (the pickup-distance spread — [EP-SM 4-8] gives
the setup range as 1/16" to 1/8", and 0.020" in the middle and upper ranges
on instruments built after March 1972) is EP7's `condition` knob, not a base
constant.

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
- Release swaps the mode's decrement to the damped one; nothing else changes.
- Sustain pedal (CC64, `>= 64` = down) defers damper engagement. A release
  with the pedal down keeps the free-decay rate; dampers apply on pedal-up.
- Panic (CC120/123) drops all dampers immediately; the instrument silences
  in the damper tau rather than by a hard mute.
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
- **The exact CC map pins at EP2**, following the organ's convention of
  doing it at the first playable milestone.

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
- **Drive (EP5).** `tw_drive` at an EP operating point. One kernel question
  is already visible: the organ's default kernel is a derived triode curve,
  but this instrument's preamplifier is solid-state, so the M5 odd kernel
  may be the honest choice here. Recorded per the warmth doctrine; **EP5
  decides and records why**. `drive = 0` bypass identity holds regardless.
- **Cabinet / speaker (EP6).** [decision D7] — land small or defer
  last-or-never. Decided by ear after EP5.
- **Condition (EP7).** Per-note tuning and voicing spread, per-note pickup
  alpha spread, hum and noise floor, damper and pedal mechanical noise,
  behind one knob; 0 is the idealized reference, bit-exact to every pre-EP7
  render; shipped default nonzero. Fixed-seed draws advanced per note
  ordinal, the M7 pattern.

  Mechanical-noise inventory for that milestone, from the mechanism sources
  [EP-SM 2-1..2-3, 4-5]: neoprene hammer tip on tine; damper felt return;
  key-bed felt; damper release bar travel. Levels [FOLK], EP7 owns.

  Hum is a special case worth recording now: the pickup coil is "divided
  into two sections, connected in opposite phase for hum cancelling"
  [EP-DAFx17 §3.1]. This instrument's hum floor is therefore structurally
  *lower* than the organ's, and EP7 should not simply transplant the M7
  mains line at the M7 level.

## 12. Open-items register

Every item names the milestone that closes it. An item with no owner is a
defect in this document.

| # | Item | Tag | Owner |
| - | ---- | --- | ----- |
| 1 | `EP_MODE_T60` extra damping factors (0.35, 0.15) | [FOLK] | EP3 |
| 2 | `EP_BASE_W` mode weights (1.00, 0.55, 0.18) | [FOLK] | EP3 |
| 3 | `EP_CORNER_HZ` zone values (1800..7000) | [FOLK] | EP3 |
| 4 | Contact-time ratio 3.4:1 behind `κ = 1/4` | [FOLK] | EP3 |
| 5 | `γ = 1.0` velocity-to-level exponent | [derived] | EP3 confirms by ear |
| 6 | `EP_PICKUP_ALPHA = 1.1` and the H2 ladder | [decision] | EP3 |
| 7 | Damper t60 anchors (0.35 s / 0.0887 s) | [FOLK] | EP3 |
| 8 | Whether an explicit two-rate mode-1 envelope is needed | open | EP3 (EP1's exhibit did not force one) |
| 9 | Mode-shape refinement of `EP_BASE_W` from the striking-line table [EP-SM 4-2] and `f ∝ 1/L²` [AT20] | open | EP3 if the flat weights fail |
| 10 | Bank layout (D4) | closed at EP1: active-gated | — |
| 11 | SBC-class measurement point for D4 | open | operator |
| 12 | Restrike law (D5) | open | EP2 |
| 13 | CC map, CC11 assignment (D6) | open | EP2 |
| 14 | Tremolo rate/depth from the preamplifier schematic | [FOLK] | EP4 |
| 15 | Drive kernel choice, triode curve vs odd kernel | open | EP5 |
| 16 | Cabinet stage (D7) | open | EP6 |
| 17 | Reference-recording set for the by-ear pass | open | operator, before EP3 |
