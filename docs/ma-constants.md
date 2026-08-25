# Mamut Analog — pinned constants and numeric contracts (MA0)

Date: 2026-08-20. Owner: MA0 of `analog-backlog.md`. This document is the
numeric contract for MA1. A number used by MA1 must occur here, be obtained
from a table defined here, or be an ordinary integer loop/index bound.

The tags are deliberately narrow:

- **[DONOR]** — behavior copied from `mamut-sint-sw` commit
  `d7672912706731b73839d1fc25801669450fd0f1`; the file is named beside the
  contract.
- **[REPO]** — an already-pinned `tonewheel91` kernel or runtime rule.
- **[DERIVED]** — computed by `driver/derive_ma_constants.c`; the equation,
  domain and measured f32 error are recorded here.
- **[DECISION]** — this instrument's bounded working value. It is not
  presented as donor or circuit truth.

The derivation program is hosted C23 and may use `double` and `libm`. It
prints C-ready f32 constants and exits nonzero when a pinned error bound or
golden vector fails. It is manual development tooling:

```sh
make derive-ma-constants
```

No generated file participates in `make`, `make test` or the core build.

## 1. Runtime and number contract

- Supported sample rates: `44100..192000 Hz`; every unsupported or
  non-finite value selects `48000 Hz` [REPO: `tw_sample_rate_hz`].
- Core samples and state are IEEE-754 binary32; no allocation, OS call,
  I/O or `libm` in a core translation unit [REPO].
- Ordinary oscillator source guard: fundamental below
  `0.45 * sample_rate`; a source at or above it targets zero gain through
  the common control smoother [DECISION].
- Mozaik source guard:
  `20 <= f0 <= min(8000, sample_rate / 8) Hz`; leaving it targets zero mix
  through the common smoother, never a clamped pitch [DONOR for `20` and
  `8000`; DECISION for the stricter rate-relative ceiling].
- Common continuous-control smoother:
  `clamp(round(sample_rate * 0.006), 8, 512)` samples, linear from the
  current value to the new target [DONOR: `mamut-engine/src/helpers.rs`].
- State below `1e-20` in an audio path and envelope state below `1e-9`
  after stage completion snaps to exact zero [DONOR for audio tiny;
  REPO/DECISION for the envelope threshold].

## 2. Pitch spine

### 2.1 MIDI table

All MIDI notes `0..127` are accepted. Equal temperament uses A4 = `440 Hz`:

```text
f[n] = 440 * 2^((n - 69) / 12)
```

[DERIVED]. The values stored by MA1 are the 128 f32 values in Appendix A.
Anchors after f32 rounding are note 0 `8.17579937 Hz`, note 69 exactly
`440 Hz`, and note 127 `12543.8535 Hz`.

The table supplies the base note only. Interval, fine tune, bend, character
and modulation are applied before the source guard. No guard changes the
reported pitch.

### 2.2 Bounded pitch ratio

An integer VCO2 interval `i` in `[-24, 24]` is split into an octave and a
remainder and evaluated with the first twelve entries relative to C from the
note table. Continuous pitch is then expressed as octaves `x` and evaluated
over `[-0.25, 0.25]` by [DERIVED]:

```c
static constexpr float MA_EXP2_SMALL[6] = {
    1.000000000e+00f, 6.931471825e-01f, 2.402265072e-01f,
    5.550410971e-02f, 9.618128650e-03f, 1.333355787e-03f,
};
```

Horner order is coefficient 5 down to 0. This is the degree-five Taylor
polynomial for `exp(ln(2) * x)`, evaluated with f32 operations. Across the
full domain its measured worst error is `9.56335e-8` relative, or
`0.000165564` cent. Inputs are clamped before evaluation [DERIVED].

Continuous contributions are bounded before summing:

| Contribution | Octaves | Tag |
| --- | ---: | --- |
| VCO2 fine tune | `[-50, 50] / 1200` | [DECISION] |
| pitch bend | `[-2, 2] / 12` | [DECISION] |
| MA2 card tune, worst combined static | `[-14, 14] / 1200` | [DECISION] |
| MA1 filter-envelope route to VCO1 | `[0, 0.125]` | [DECISION] |
| MA4 GFM pitch | `[-6, 6] / 1200` | [DECISION] |

No one call to the polynomial may exceed `0.25` octave; larger sums are
split into an exact power-of-two octave plus a remainder in that domain.

## 3. Analog oscillators

### 3.1 Phase and ordinary discontinuities

Each VCO owns a Q48 phase accumulator; the top 32 phase bits map uniformly
to `p` in `[0,1)` for f32 waveform evaluation [ALGORITHM REVISION]. At the
8x sharp-edge rate the effective step is
`d = clamp(f / (8*sample_rate), 0, 0.45)`, converted once to Q48. Render the
current phase, then add the integer step and mask to 48 bits on wrap. This
preserves the donor phase semantics without cumulative f32 phase error. A
muted source may keep phase moving; gain owns audibility.

The donor's second-order PolyBLEP established the edge placement, but the
MA1 alias gate rejected its 1x and 2x results. The accepted kernel uses the
C2 quintic smoothstep `S(x)=6x^5-15x^4+10x^3` and an eight-substep reset
slew on either side of the edge [ALGORITHM REVISION]. Let
`e = min(8*d, .49)`:

```text
blep(p,d) = 0                                      d <= FLT_EPSILON
            2*(S((p/e + 1)/2) - 1)                p < e
            2*S(((p-1)/e + 1)/2)                  p > 1-e
            0                                      otherwise

saw(p,d) = clamp(2p - 1 - blep(p,d), -1.25, 1.25)
pulse(p,w,d) = clamp(raw(p,w) + blep(p,d)
                       - blep(wrap(p-w),d), -1.25, 1.25)
raw(p,w) = p < w ? +1 : -1
```

Pulse width clamps to `[0.05, 0.95]`. Triangle initializes to
`1 - 4 * abs(p - 0.5)`, returns its current state clamped to `[-1,1]`, then
integrates the bandlimited 50% pulse:

```text
triangle_state = clamp(triangle_state + pulse(p,.5,d) * 4d,
                       -1.25, 1.25)
```

[DONOR: triangle mechanism from `mamut-dsp/src/bandlimited.rs`; accepted
edge kernel from the MA1-2 evidence loop].

For each VCO, waveform weights are nonnegative and the rendered mix is
divided by `max(saw_level + pulse_level + triangle_level, 1)` [DECISION].
This normalizes simultaneous shapes without boosting a single sub-unity
shape.

### 3.2 Hard sync and fixed modulation routes

VCO1 is the master. On its wrap, VCO2 changes from its current phase to

```text
effective_sync = sync * clamp(1 - 0.75 * sync_softness, 0, 1)
new_phase = old_phase * (1 - effective_sync)
```

and its triangle state resets to the raw triangle at `new_phase` [DONOR:
`mamut-engine/src/engine/macro_state.rs` and `mamut-dsp/src/phase.rs`].
MA1's causal two-sample PolyBLEP residual is scaled by the actual raw
pre/post waveform jump for saw and both pulse edges [DECISION]. A zero sync
amount skips the reset and correction exactly.

For a master wrap at fraction `u` of the current oversampled interval and
an actual mixed-wave jump `j`, the matching C2 step residual is [DERIVED
from the two PolyBLEP branches in 3.1]:

```text
current correction = j * S((1-u)/2)
next correction    = j * (S(1-u/2) - 1)
```

The wrap is known from current phase and step before either corrected sample
is returned, so the implementation stays causal. Partial sync measures `j`
at the phase immediately before and after the partial reset. The triangle
integrator resets at the post-reset phase and advances through the remaining
fraction of the sample.

VCO2 modulates VCO1 instantaneous frequency [DECISION direction, DONOR
depth]:

```text
cross_ratio = clamp(1 + vco2_sample * crossmod * 0.25, 0.25, 4)
```

The filter envelope route uses
`m = filter_envelope * filter_envelope_amount` and the fixed full-depth
deltas [DECISION]:

```text
VCO1 pitch       +0.125 octave * m
VCO1 pulse width +0.120 * m
VCO2 pulse width -0.080 * m
```

Pulse widths clamp after modulation. The effective step after every pitch
route is what PolyBLEP receives.

### 3.3 Noise

Noise is a signed f32 made from the top 24 bits of one SplitMix64 draw per
voice frame, mapped uniformly to `[-1,1)` [REPO: `tw_splitmix64`]. The MA1
one-voice seed is `0x4d414e4f49534531` (`MANOISE1`) [DECISION]. Noise state
advances even when its level is zero so later level gestures do not change
the random timeline.

## 4. Mozaik AUX

The C implementation ports the integer and tile contracts, not a Rust type
or runtime [DONOR: `mamut-dsp/src/quasicrystal.rs`].

### 4.1 Q32 word and control mapping

```text
slope clamp       0x73333333 .. 0xc0000000   (0.45 .. 0.75)
1/2               0x80000000
3/5               0x9999999a
golden 1/tau      0x9e3779b9
5/8               0xa0000000
2/3               0xaaaaaaab
minimum tile      4 samples
contrast gamma    1.0 .. 2.2, default 1.618034
```

Each boundary performs unsigned Q32 addition. Carry means a long tile; no
carry means short. Slope control `u` maps to `0.45 + 0.30u`. A target within
`0.004` of a detent snaps to the nearest exact Q32 value; on an exact tie,
the search order is golden, half, three-fifths, five-eighths, two-thirds, so
golden wins its overlap with five-eighths [DONOR:
`mamut-engine/src/helpers.rs`]. Contrast maps to `1 + 1.2u`; drift maps to
`0.5u^2` phason cycles/s [DONOR].

The factory seed is `0x4d6f7a31`. Its five MA2 card seats, using the donor's
slot-folded SplitMix finalizer and the top 32 bits, are [DERIVED from the
DONOR equation]:

```text
card 0  0xe0922332
card 1  0x679ce881
card 2  0x8d9e469e
card 3  0x1b109f21
card 4  0xc4964d87
```

### 4.2 Tile waveform

Let `sigma = slope_q32 / 2^32`, `M = sample_rate / (2*f0)` and
`mean_factor = (1-sigma) + sigma*gamma`. At a boundary [DONOR]:

```text
long:  tile_len = max(4, M * gamma / mean_factor), sign = +1
short: tile_len = max(4, M         / mean_factor), sign = -1
sample = sign * gain * 0.5 * (1 - cos(2*pi*tile_pos/tile_len))
```

The core evaluates the Hann pulse as
`0.5 * (1 - tw_sin_turns(wrap(unit_pos + 0.25)))`; it does not call
`cos` [REPO/DERIVED identity]. `tile_pos` advances by one. Fractional
leftover at a boundary carries into the next tile. A pending absolute
phason applies only immediately before the next Q32 addition; repeated
drift requests within a tile accumulate modulo `2^32` [DONOR].

### 4.3 Integer golden vectors

`kinds_lsb` packs the first 64 tile kinds with the first tile in bit zero
and long = 1. `frac64` is the Q32 accumulator after those 64 additions.
These values are copied from the pinned donor contract and independently
checked by the floor-difference equation in the hosted C audit:

| slope | initial phason | kinds_lsb | frac64 |
| --- | --- | --- | --- |
| `80000000` | `00000000` | `aaaaaaaaaaaaaaaa` | `00000000` |
| `80000000` | `12345678` | `aaaaaaaaaaaaaaaa` | `12345678` |
| `80000000` | `deadbeef` | `5555555555555555` | `deadbeef` |
| `9999999a` | `00000000` | `ad6b5ad6b5ad6b5a` | `66666680` |
| `9999999a` | `12345678` | `ad6b5ad6b5ad6b5a` | `789abcf8` |
| `9999999a` | `deadbeef` | `b5ad6b5ad6b5ad6b` | `4514256f` |
| `9e3779b9` | `00000000` | `adad6d6b6b6b5b5a` | `8dde6e40` |
| `9e3779b9` | `12345678` | `6d6d6b6b6b5b5ada` | `a012c4b8` |
| `9e3779b9` | `deadbeef` | `b5adadad6d6d6b6b` | `6c8c2d2f` |
| `a0000000` | `00000000` | `dadadadadadadada` | `00000000` |
| `a0000000` | `12345678` | `dadadadadadadada` | `12345678` |
| `a0000000` | `deadbeef` | `6b6b6b6b6b6b6b6b` | `deadbeef` |
| `aaaaaaab` | `00000000` | `6db6db6db6db6db6` | `aaaaaac0` |
| `aaaaaaab` | `12345678` | `6db6db6db6db6db6` | `bcdf0138` |
| `aaaaaaab` | `deadbeef` | `b6db6db6db6db6db` | `895869af` |

A golden word seated at `0x01020304` yields `0x5b5a` for its first 16
kinds. Adding phason delta `0x10203040` at that boundary yields `0x6b5b`
for the next 16 and final fraction `0xd8116a64` [DONOR golden].

## 5. Source mixer and pressure

The two VCO waveform mixes are normalized separately. The pre-pressure bus
is [DECISION]:

```text
weights = 1 + vco2_level + noise_level + mozaik_mix
source = (vco1 + vco2_level*vco2 + noise_level*noise
                 + mozaik_mix*mozaik) / max(weights, 1)
```

Muted/guarded sources contribute zero samples but retain their configured
weight only while their 6 ms guard fade is nonzero. This prevents a source
leaving the numerical domain from turning the other sources up abruptly.

Mixer pressure `p` is [DECISION]:

```text
gain = 1 + 5*p
shaped = tw_sat(gain*source) / tw_sat(gain)
output = source + p*(shaped-source)
```

`p` clamps to `[0,1]`. Literal zero branches directly to `source` and does
not touch pressure state. The blend makes the limit continuous as `p`
approaches zero; the normalizer maps source magnitude one back to one.

## 6. Nonlinear four-pole lowpass

### 6.1 Prewarp polynomial

For normalized cutoff `x = cutoff/sample_rate`, the TPT coefficient is
`G = tan(pi*x)/(1 + tan(pi*x))`. MA1 evaluates `G/x` as the degree-six
Chebyshev series below, with `z = 2*x/0.42 - 1`, then multiplies by `x`
[DERIVED]:

```c
static constexpr float MA_PREWARP_CHEB[7] = {
    2.287037611e+00f, -5.843005180e-01f, 2.182650715e-01f,
   -3.681783006e-02f,  1.203418057e-02f, -2.262040973e-03f,
    6.386489258e-04f,
};
```

Clenshaw evaluation uses f32 throughout. Over `0 < x <= 0.42`, one million
uniform audit points give maximum `G` absolute error `3.09863e-5`, maximum
relative error `7.53962e-5`, and maximum recovered cutoff relative error
`8.33743e-5` (`0.00834%`). `x=0` returns literal zero [DERIVED].

### 6.2 Two-times boundary filter

The exact seven-tap halfband is [DERIVED/DECISION]:

```c
static constexpr float MA_HALFBAND_7[7] = {
   -4.674123600e-02f, 0.0f, 2.967412472e-01f, 5.0e-01f,
    2.967412472e-01f, 0.0f, -4.674123600e-02f,
};
```

It has form `[a,0,b,.5,b,0,a]` with `a+b=.25`. `a` minimizes the maximum
amplitude error over `0..0.25*pi`; the measured maximum is `0.01424244`.
By exact halfband symmetry the maximum stop magnitude over
`0.75*pi..pi` is the same (`-36.93 dB`). The transition band is deliberately
reported, not called a passband: response magnitude is `0.71159` at
`0.42*pi`. The interpolator uses coefficients multiplied by two; the
decimator uses the listed coefficients. Combined linear-phase latency is
three base-rate frames [DERIVED].

Each sharp-edge VCO 2:1 boundary uses a separate 31-tap exact halfband
[DERIVED/ALGORITHM REVISION]:

```c
static constexpr float MA_OSC_HALFBAND_31[31] = {
    0.0f, 0.0f, 4.103266983e-04f, 0.0f,
   -2.230306389e-03f, 0.0f, 7.100922987e-03f, 0.0f,
   -1.791719720e-02f, 0.0f, 4.010779038e-02f, 0.0f,
   -9.010776132e-02f, 0.0f, 3.126362264e-01f, 5.0e-01f,
    3.126362264e-01f, 0.0f, -9.010776132e-02f, 0.0f,
    4.010779038e-02f, 0.0f, -1.791719720e-02f, 0.0f,
    7.100922987e-03f, 0.0f, -2.230306389e-03f, 0.0f,
    4.103266983e-04f, 0.0f, 0.0f,
};
```

It is the ideal halfband sinc under a 31-point Blackman window, with exact
zero even offsets, exact `.5` center and side normalization to DC gain one.
Over `0..0.25*pi` and `0.75*pi..pi`, its measured worst pass and stop
magnitude error is `0.000170137` (`-75.38 dB`). The hosted C derivation owns
both coefficients and the full-domain audit.

Mixer pressure and the ladder execute at `2 * sample_rate`. There are
exactly two feedback solver iterations per oversampled substep [DECISION].
Changing tap count, oversampling or iteration count is an MA1 algorithm
revision, not tuning.

The MA1-2 alias gate places the sharp-edge VCO kernels at `8 * sample_rate`
and decimates them through two 31-tap halfbands into the mixer's existing 2x
boundary [ALGORITHM REVISION]. PolyBLEP, sync residuals and triangle
integration execute eight times per public frame. Source mixing currently
uses a third 31-tap halfband to reach the source-only public output;
MA1-4 replaces that temporary third boundary with the 2x pressure/ladder
path. One noise draw is held across all eight VCO substeps, preserving the
section 3.3 one-draw-per-voice-frame contract.

The revision was evidence-driven. The recorded 1x referee improved the
eight pinned cases by only `7.38..17.74 dB` against naive edges. Starting
the boundary at 2x improved that to `12.57..20.20 dB`, with only one of
eight cases passing. The narrower 4x/8x edge candidates also failed and are
kept only in the journal. The accepted eight-substep C2 slew yields
`21.86..29.16 dB` across all eight ordinary, hard-sync and cross-mod cases
under both GCC and Clang.

### 6.3 Ladder equations and operating point

The four stages are TPT one-poles with fixed state `s[j]`. For one solver
iteration, using the same pre-step state [DECISION]:

```text
u = tw_sat(input_gain*input - k*y4_guess)
for j = 0..3:
    v[j] = (tw_sat(u) - s[j]) * G
    y[j] = v[j] + s[j]
    u = y[j]
y4_guess = y[3]
```

After the second iteration only, commit `s[j] = y[j] + v[j]` and return
`y[3]`. This is a fixed-point zero-delay feedback solve with no convergence
branch. Input drive and resonance are [DECISION]:

```text
input_gain = 1 + 3 * filter_drive^2
k = 4.65 * resonance
```

The nominal linear self-oscillation threshold `k=4` therefore occurs at
resonance `0.860215`. Saturation bounds the endpoint; resonance bass loss is
not compensated. Cutoff clamps to
`[20, min(20000, 0.42*sample_rate)] Hz`. Any non-finite intermediate resets
the four states and feedback guess to zero, returns zero for that substep,
and increments a diagnostic counter [DECISION]. MA1 evidence may reject
this fixed map, but it may not silently retune it.

## 7. RC envelopes and VCA

Both ADSRs move through `level += c*(target-level)`. A stage time `T` is its
time to reduce the start-to-target error by 60 dB [DECISION]:

```text
x = ln(1000) / (0.001*T_ms*sample_rate)
c = tw_one_pole_coeff(x)
```

`ln(1000) = 6.907755278982137`. The minimum time `1 ms` at `44.1 kHz`
gives `x=0.15664`, inside the repository polynomial's `0.25` domain
[DERIVED/REPO]. On stage entry, completion error is
`max(1e-9, 0.001*abs(target-start_level))`; crossing it snaps exactly to the
target and advances the stage. Sustain is held exactly. NoteOn attacks from
the current level; NoteOff releases from it. Release completion snaps level
to literal zero and marks the envelope idle [DECISION].

The amp VCA multiplies the filtered sample by
`amp_envelope * velocity_gain * pressure_gain`. Velocity and pressure laws
are in sections 9 and 10. No extra VCA saturator exists in MA1 [DECISION].

## 8. Identity resolver

MA1 uses the donor's current identity equations with linear raw macros and
every patch bias fixed to zero [DONOR:
`mamut-identity/src/lib.rs`; DECISION: no patch response curves]. Let
`g,b,h,r,s` be clamped Gravitacija, Bloom, Heat, Ruin and Swarm. Define
`late(v,t) = v <= t ? 0 : clamp((v-t)/(1-t),0,1)`,
`lg=late(g,.68)`, and `es=late(s,.82)`:

```text
horizont_open = clamp(.70b + .25(1-g) - .10r - .05h)
horizont_air  = clamp(.78b + .08(1-h) - .18lg)
horizont_span = clamp(.58b + .32s - .16g)

pec_mass     = clamp(.60h + .25g + .05s - .05b)
pec_heat     = clamp(.80h + .12g)
pec_pressure = clamp(.58g + .24h + .08r)

baklja_ready     = clamp(.56r + .28g + .08es)
baklja_edge      = clamp(.68r + .40lg)
baklja_sync_bias = clamp(.72r + .06h)
grav_pull = g

mass = clamp(.70*pec_mass + .20*pec_heat + .10*grav_pull)
strain = clamp(.40*baklja_edge + .38*pec_pressure + .22*grav_pull)
headroom = clamp(.78 + .18*horizont_air + .08b
                      - .46*grav_pull - .14h - .08*baklja_ready)
body_focus = clamp(.50*pec_mass + .28*grav_pull - .18b)
rupture_threshold = clamp(.78 - .22*baklja_ready - .16*grav_pull)
rupture_response = clamp(.55*baklja_edge + .25*strain + .20r)
spatial_dispersion = clamp(.55*horizont_span + .33s - .10*grav_pull)
```

Every unlabelled clamp above is `[0,1]`. The resolved all-zero frame is
stored at initialization [DERIVED]:

| Field | Zero value | Field | Zero value |
| --- | ---: | --- | ---: |
| `horizont_open` | `.25` | `horizont_air` | `.08` |
| `horizont_span` | `0` | `pec_mass/heat/pressure` | `0` |
| `baklja_ready/edge/sync_bias` | `0` | `grav_pull` | `0` |
| `mass` / `strain` | `0` | `headroom` | `.7944` |
| `body_focus` | `0` | `rupture_threshold` | `.78` |
| `rupture_response` | `0` | `spatial_dispersion` | `0` |

Render destinations consume the difference from this frame. Multiplicative
destinations divide by the zero-frame value, so zero macros produce a
literal zero delta or unity ratio through the normal resolver [DECISION;
the exact bypass is not a render special case]:

```text
cutoff_raw = .90 + .62b + .18*horizont_air - .40g
cutoff_ratio = cutoff_raw / .9144
resonance_delta = .14*baklja_edge + .06r - .04*mass
filter_drive_delta = .18*pec_heat + .10*strain
filter_env_delta = .12*(horizont_open - .25)
keytrack_ratio = (.92 - .12*mass) / .92
sync_delta = .30*baklja_sync_bias
crossmod_delta = .24*baklja_ready
width_delta = .18*horizont_span - .20*body_focus
crossfeed_delta = .28*body_focus + .10*grav_pull
                  - .10*spatial_dispersion
body_drive_delta = .25*body_focus
body_load_ratio = clamp(1 + .14*mass + .08*strain
                          + .25*(.7944-headroom), .75, 1.50)
```

[DONOR coefficients where the same destination exists; DECISION for
zero-frame normalization, width narrowing and body load]. Every final value
clamps to its direct domain before smoothing.

Analog-only identity deltas [DECISION] are:

```text
Mozaik mix       + .10 * Bloom
contrast control + .20 * Heat
pending phason   + .15 * Ruin turns
drift control    + .35 * Swarm
```

Performance overlays happen before these equations [DECISION]:

```text
effective Gravitacija += .45 * channel_pressure
effective Ruin        += .35 * channel_pressure
effective Bloom       += .35 * mod_wheel
effective Swarm       += .50 * mod_wheel
```

They clamp to `[0,1]` and never overwrite stored base macros.

## 9. Velocity and per-note pressure

For MIDI velocity `v`, `u=v/127` [DONOR exponents, DERIVED tables]:

```text
level_curve[v]  = v == 0 ? 0 : u^0.78
filter_curve[v] = v == 0 ? 0 : u^1.08
velocity_gain   = .25 + .75*level_curve[v]
filter envelope amount gains .25*filter_curve[v]
```

NoteOn velocity zero is NoteOff and does not use the gain equation.
Appendix B pins the two f32 tables.

Per-note pressure is clamped to `[0,1]`, smoothed for 6 ms per sounding
card, and adds at full pressure [DECISION]:

- `+0.25` octave to that card's cutoff;
- `+0.10` to its VCA multiplier (`pressure_gain = 1 + 0.10p`).

Channel pressure does not enter this per-card path.

## 10. Direct controls and MIDI conversion

Direct domains [DECISION] are:

| Control | Domain |
| --- | --- |
| waveform/source/sync/cross-mod/pressure/character/macros/width/wet | `[0,1]` |
| pulse width | `[.05,.95]` |
| VCO2 interval | integer `[-24,24]` semitones |
| VCO2 fine | `[-50,50]` cents |
| pitch bend | `[-2,2]` semitones |
| cutoff | `[20,min(20000,.42*sample_rate)] Hz` |
| resonance | `[0,1]` |
| ADSR times | `[1,20000] ms` |
| sustain | `[0,1]` |
| LFO rate | `[.03,20] Hz` |
| glide | `[0,10] s` |
| Mozaik slope/contrast/phason/drift controls | `[0,1]` |

Non-finite setter input selects that control's compiled default; finite
input clamps. Integer MIDI conversion is [DECISION]:

```text
unit(cc) = cc / 127
interval(cc) = floor((48*cc + 63)/127) - 24
bipolar50(cc) = cc < 64 ? 50*(cc-64)/64 : 50*(cc-64)/63
pulse_width(cc) = .05 + .90*unit(cc)
phason(cc) = cc / 128             (128 distinct positions; no endpoint alias)
switch(cc) = cc >= 64
```

Pitch bend word `w` uses exact center 8192 and exact endpoints:

```text
w <= 8192: 2*(w-8192)/8192
w >  8192: 2*(w-8192)/8191
```

Cutoff, envelope time and LFO rate use the 128-entry tables in Appendix C:

```text
cutoff[cc] = 20 * 1000^(cc/127) Hz
time[cc]   = 1 * 20000^(cc/127) ms
lfo[cc]    = .03 * (20/.03)^(cc/127) Hz
```

The cutoff setter applies the sample-rate ceiling after lookup. Glide CC5
uses `10*unit(cc)` seconds rather than the envelope-time table. Every other
continuous CC uses `unit(cc)`. The CC ownership is the map in
`analog-backlog.md`; that table is normative and is not duplicated here.

## 11. Card character and pan (MA2 constants pinned early)

Character seed: `0x4d41434841523031` (`MACHAR01`) [DECISION]. For card
ordinal `c` and stable tag `t`:

```text
state = seed ^ ((c+1) * 0x9e3779b97f4a7c15) ^ t
word = tw_splitmix64(&state)
draw = ((word >> 40) - 8388608) / 8388608
```

The signed result is `[-1,1)`. Adding a later tag cannot move an existing
draw. Tags [DECISION] are:

```text
0x4d410101 VCO1_TUNE       0x4d410102 VCO2_TUNE
0x4d410103 TUNE_WALK       0x4d410104 FILTER_CUTOFF
0x4d410110 AMP_ATTACK      0x4d410111 AMP_DECAY
0x4d410112 AMP_SUSTAIN     0x4d410113 AMP_RELEASE
0x4d410120 FILTER_ATTACK   0x4d410121 FILTER_DECAY
0x4d410122 FILTER_SUSTAIN  0x4d410123 FILTER_RELEASE
0x4d410130 VCA_TRIM
```

At character `1`, bounds are VCO1 `+/-6 cents`, VCO2 additional
`+/-8 cents`, tuning walk `+/-3 cents`, cutoff `+/-0.12 octave`, every
envelope stage time `+/-8%`, and VCA trim `+/-3%` [DECISION]. Walk updates
once per 32 samples, uses its own continuing SplitMix state and clamps to
the bound. Character zero multiplies every deviation by literal zero.

Card base positions are `[-.750,-.375,0,.375,.750]` [DECISION]. With
effective position `q=clamp(base*width + identity_offset + gfm_offset,-1,1)`:

```text
left  = sqrt(2) * cos((q+1)*pi/4)
right = sqrt(2) * sin((q+1)*pi/4)
```

The implementation uses `tw_sin_turns`; `sqrt(2)=1.41421356237`. The
normalization makes `q=0` exactly dual mono in the ideal equation. Full
width double-precision reference coefficients are [DERIVED]:

```text
card 0  1.38703990  0.27589938
card 1  1.24722505  0.66665566
card 2  1.00000000  1.00000000
card 3  0.66665566  1.24722505
card 4  0.27589938  1.38703990
```

Width zero bypasses coefficient calculation and sends the identical mono
card sum to left and right [DECISION].

## 12. Shared body, DC block and safety

MA1 is one centered card, so its pre-effect result is dual mono. MA2 sums
cards through the pan coefficients, then converts to
`mid=(L+R)/2`, `side=(L-R)/2`. Width and crossfeed shape side; shared body
processes mid once [DECISION].

The body reuses the repository triode `tw_drive` state and kernel [REPO],
but at the analog bus operating point [DECISION]: when body drive is
positive, call it with `4*mid` and multiply its result by `.25`; literal
zero body drive branches around both scales and leaves state untouched.
This preserves small-signal unity while reaching the table at a useful
single-card level. Factory direct body drive is `.10`; identity body load
from section 8 multiplies its smoothed input.

The stereo DC blockers are one-pole trackers at `10 Hz`:
`c=tw_one_pole_coeff(2*pi*10/sample_rate)`, output `x-lp` [REPO]. Tiny state
snaps at `1e-9`.

The safety knee is literal identity for `abs(x) <= .98`. Above it, with
`t=clamp((abs(x)-.98)/.02,0,1)` [DECISION]:

```text
limited_abs = .98 + .02*(t + t^2 - t^3)
```

Restore the sign. This is monotone, continuous with unit slope at the knee,
zero slope at the unit ceiling and bounded by one. Non-finite input becomes
zero. Diagnostics count sanitizations, knee hits and tiny flushes, and retain
pre/post peak plus maximum reduction. Master level `[0,1]`, factory `.18`,
is applied before safety [DECISION].

## 13. Chorus and reverb capacities (MA3 constants pinned early)

Behavior is ported from `mamut-dsp/src/fx.rs`; storage is fixed [DONOR
behavior, DECISION storage]. At the maximum sample rate:

```text
MA_CHORUS_CAPACITY = round(192000 * .080) = 15360 samples/channel
MA_REVERB_CAPACITY = round(192000 * .340) = 65280 samples/channel
```

Four f32 buffers consume `645120` bytes, leaving more than 400 KiB for the
rest of `ma_synth` under its 1 MiB assertion [DERIVED].

Chorus uses base delay `14 ms`, modulation reach `6 ms * depth`, phase taps
`0`, `.31`, `.63`, cross delay multiplier `.74`, cross channel multipliers
`.94/1.06`, feedback `.08+.12*depth`, wet weights `.66/.22/.12`, dry gain
`1-.72*mix`, wet gain `.78*mix`, and rate floor `.01 Hz` [DONOR]. Its public
rate still clamps to `[.03,20] Hz` [DECISION].

Reverb uses `340 ms` storage, tap fractions `.16+.28*size` and
`.31+.22*size`, feedback `.34+.40*size` capped `.88`, diffusion
`.16+.22*size`, damping blend `1-.88*damping`, input gain `.42`, wet weights
`.48/.18/.26`, dry gain `1-.58*mix`, and wet gain
`mix*(.62+.10*size)` [DONOR]. Damping clamps to `[0,.99]` internally.

Disabled stages return dry PCM and leave state untouched. Enabling clears
state outside the render loop. Enabled mix zero may advance state but
returns the incoming float bits directly [DECISION].

## 14. Future hidden GFM field (MA4 constants pinned early)

MA4 owns implementation. MA0 pins the integration boundary [DONOR behavior,
DECISION insertion]:

- toroidal `16 x 16` field, `256` cells;
- `K=7` links per cell: four local plus three deterministic long links;
- one update per 32 audio frames at effective rate `sample_rate/32`;
- seed `0x6a464d40`;
- five-tap card readout `.50*center + .125*(N+S+E+W)`;
- NoteOff strike: `.30` of NoteOn pressure, `.50` heat, zero rupture bias;
- stereo probe offset `+/-2` columns remains evidence context only — MA4
  mixes no field audio.

Note `n` maps [DONOR: `mamut-field::note_strike_position`]:

```text
x = ((n % 12) * 16) / 12
y = ((n / 12) * 16) / 11
folded = seed ^ (seed >> 27) ^ (n * 0x9e3779b97f4a7c15)
x = wrap(x + ((folded >> 8)  % 3) - 1)
y = wrap(y + ((folded >> 16) % 3) - 1)
```

Automatic program scores [DONOR: `mamut-engine/src/gfm_layer.rs`] are:

```text
horizont = .40*horizont_open + .34*horizont_air
           + .18*horizont_span + .08*Bloom
pec = .30*pec_mass + .32*pec_heat + .20*pec_pressure
      + .12*mass + .06*Heat
baklja = .32*baklja_ready + .34*baklja_edge + .16*baklja_sync_bias
         + .12*rupture_response + .06*Ruin
```

Below best score `.15`, no posture is active. Ties prefer Baklja, then Pec,
then Horizont [DONOR]. At field amount one, card modulation bounds are pitch
`+/-6 cents`, cutoff `+/-0.18 octave`, positive filter drive `.12`, VCA
`+/- .04`, sync softness `+/- .10`, and pan `+/- .08` [DECISION]. Amount
zero skips application exactly.

## 15. Compiled factory dark pad

These are direct initialization values [DECISION]; identity macros start at
zero and therefore add no delta:

| Group | Value |
| --- | --- |
| VCO1 | saw `.70`, pulse `.25`, triangle `.15`, PW `.50` |
| VCO2 | saw `.35`, pulse `.20`, triangle `.55`, level `.62`, interval `0`, fine `+7 cents`, PW `.50` |
| Oscillator modulation | sync `0`, sync softness `0`, cross-mod `0` |
| Noise | `.02` |
| Mozaik | seed `0x4d6f7a31`, mix `.20`, golden slope, contrast `1.618034`, phason `0`, drift `.05` |
| Mixer/filter | pressure `.15`, cutoff `900 Hz`, resonance `.18`, drive `.12`, envelope amount `.30`, keytrack `.45` |
| Amp ADSR | `600 / 1600 / .82 / 3000 ms` |
| Filter ADSR | `350 / 1800 / .50 / 2600 ms` |
| Voice | LFO off, glide off, unison off, character `.20` |
| Identity | all five macros `0` |
| Output | width `.70`, body drive `.10`, master `.18`, safety knee `.98` |
| Chorus | enabled, mix `.14`, depth `.22`, rate `.18 Hz` |
| Reverb | enabled, mix `.10`, size `.40`, damping `.50` |
| GFM after MA4 | enabled, amount `.12`, seed `0x6a464d40` |

## 16. MA0 audit record

The closing audit checks:

- the hosted program's table, prewarp, halfband, pitch-polynomial and 15
  Q32-vector assertions;
- every MA1 literal against sections 1–15;
- aggregate organ/EP signatures and undefined core symbols;
- GCC and Clang C23 builds; and
- whitespace/error checks on the final diff.

Gate result, 2026-08-20: **PASS**. `make derive-ma-constants` passed under
GCC and Clang; `make test` and `make test-clang` each retained core `9410`,
hosted `77` and MIDI-map `22` checks with zero failures; both core-symbol
audits and `git diff --check` passed. The detailed numeric bounds and task
transition are recorded in `ma-dev-journal.md`.

## Appendix A — 128-note frequency table

The following is the reviewed f32 output of `derive_ma_constants.c`:

```c
static constexpr float MA_NOTE_HZ[128] = {
    8.175799370e+00f, 8.661956787e+00f, 9.177023888e+00f, 9.722718239e+00f, 1.030086136e+01f, 1.091338253e+01f, 1.156232548e+01f, 1.224985695e+01f,
    1.297827148e+01f, 1.375000000e+01f, 1.456761742e+01f, 1.543385315e+01f, 1.635159874e+01f, 1.732391357e+01f, 1.835404778e+01f, 1.944543648e+01f,
    2.060172272e+01f, 2.182676506e+01f, 2.312465096e+01f, 2.449971390e+01f, 2.595654297e+01f, 2.750000000e+01f, 2.913523483e+01f, 3.086770630e+01f,
    3.270319748e+01f, 3.464782715e+01f, 3.670809555e+01f, 3.889087296e+01f, 4.120344543e+01f, 4.365353012e+01f, 4.624930191e+01f, 4.899942780e+01f,
    5.191308594e+01f, 5.500000000e+01f, 5.827046967e+01f, 6.173541260e+01f, 6.540639496e+01f, 6.929565430e+01f, 7.341619110e+01f, 7.778174591e+01f,
    8.240689087e+01f, 8.730706024e+01f, 9.249860382e+01f, 9.799885559e+01f, 1.038261719e+02f, 1.100000000e+02f, 1.165409393e+02f, 1.234708252e+02f,
    1.308127899e+02f, 1.385913086e+02f, 1.468323822e+02f, 1.555634918e+02f, 1.648137817e+02f, 1.746141205e+02f, 1.849972076e+02f, 1.959977112e+02f,
    2.076523438e+02f, 2.200000000e+02f, 2.330818787e+02f, 2.469416504e+02f, 2.616255798e+02f, 2.771826172e+02f, 2.936647644e+02f, 3.111269836e+02f,
    3.296275635e+02f, 3.492282410e+02f, 3.699944153e+02f, 3.919954224e+02f, 4.153046875e+02f, 4.400000000e+02f, 4.661637573e+02f, 4.938833008e+02f,
    5.232511597e+02f, 5.543652344e+02f, 5.873295288e+02f, 6.222539673e+02f, 6.592551270e+02f, 6.984564819e+02f, 7.399888306e+02f, 7.839908447e+02f,
    8.306093750e+02f, 8.800000000e+02f, 9.323275146e+02f, 9.877666016e+02f, 1.046502319e+03f, 1.108730469e+03f, 1.174659058e+03f, 1.244507935e+03f,
    1.318510254e+03f, 1.396912964e+03f, 1.479977661e+03f, 1.567981689e+03f, 1.661218750e+03f, 1.760000000e+03f, 1.864655029e+03f, 1.975533203e+03f,
    2.093004639e+03f, 2.217460938e+03f, 2.349318115e+03f, 2.489015869e+03f, 2.637020508e+03f, 2.793825928e+03f, 2.959955322e+03f, 3.135963379e+03f,
    3.322437500e+03f, 3.520000000e+03f, 3.729310059e+03f, 3.951066406e+03f, 4.186009277e+03f, 4.434921875e+03f, 4.698636230e+03f, 4.978031738e+03f,
    5.274041016e+03f, 5.587651855e+03f, 5.919910645e+03f, 6.271926758e+03f, 6.644875000e+03f, 7.040000000e+03f, 7.458620117e+03f, 7.902132812e+03f,
    8.372018555e+03f, 8.869843750e+03f, 9.397272461e+03f, 9.956063477e+03f, 1.054808203e+04f, 1.117530371e+04f, 1.183982129e+04f, 1.254385352e+04f,
};
```

## Appendix B — velocity curves

```c
static constexpr float MA_VELOCITY_LEVEL[128] = {
    0.000000000e+00f, 2.285772935e-02f, 3.924971446e-02f, 5.385024473e-02f, 6.739689410e-02f, 8.021021634e-02f, 9.246791899e-02f, 1.042820513e-01f,
    1.157292873e-01f, 1.268651187e-01f, 1.377314478e-01f, 1.483608782e-01f, 1.587795168e-01f, 1.690086424e-01f, 1.790659279e-01f, 1.889662594e-01f,
    1.987223327e-01f, 2.083450854e-01f, 2.178440243e-01f, 2.272275090e-01f, 2.365029156e-01f, 2.456768006e-01f, 2.547550499e-01f, 2.637429237e-01f,
    2.726452053e-01f, 2.814662457e-01f, 2.902099490e-01f, 2.988799810e-01f, 3.074796498e-01f, 3.160119653e-01f, 3.244798183e-01f, 3.328857720e-01f,
    3.412322700e-01f, 3.495215476e-01f, 3.577557802e-01f, 3.659368753e-01f, 3.740667105e-01f, 3.821469843e-01f, 3.901793659e-01f, 3.981653750e-01f,
    4.061064422e-01f, 4.140039682e-01f, 4.218592048e-01f, 4.296734333e-01f, 4.374477565e-01f, 4.451833069e-01f, 4.528810978e-01f, 4.605422020e-01f,
    4.681675136e-01f, 4.757579267e-01f, 4.833143651e-01f, 4.908376038e-01f, 4.983284771e-01f, 5.057877302e-01f, 5.132160783e-01f, 5.206142068e-01f,
    5.279827714e-01f, 5.353224874e-01f, 5.426338911e-01f, 5.499176383e-01f, 5.571743250e-01f, 5.644043684e-01f, 5.716084242e-01f, 5.787869692e-01f,
    5.859404206e-01f, 5.930693746e-01f, 6.001742482e-01f, 6.072554588e-01f, 6.143134832e-01f, 6.213486791e-01f, 6.283615232e-01f, 6.353523135e-01f,
    6.423214674e-01f, 6.492694020e-01f, 6.561964154e-01f, 6.631028056e-01f, 6.699890494e-01f, 6.768553257e-01f, 6.837020516e-01f, 6.905294657e-01f,
    6.973379254e-01f, 7.041276693e-01f, 7.108989954e-01f, 7.176522017e-01f, 7.243874669e-01f, 7.311051488e-01f, 7.378054857e-01f, 7.444887161e-01f,
    7.511550188e-01f, 7.578046918e-01f, 7.644379735e-01f, 7.710550427e-01f, 7.776561379e-01f, 7.842414379e-01f, 7.908112407e-01f, 7.973656058e-01f,
    8.039048910e-01f, 8.104291558e-01f, 8.169386387e-01f, 8.234335184e-01f, 8.299140334e-01f, 8.363802433e-01f, 8.428324461e-01f, 8.492707014e-01f,
    8.556951880e-01f, 8.621061444e-01f, 8.685036898e-01f, 8.748879433e-01f, 8.812591434e-01f, 8.876172900e-01f, 8.939626813e-01f, 9.002953768e-01f,
    9.066155553e-01f, 9.129232764e-01f, 9.192187786e-01f, 9.255021214e-01f, 9.317734241e-01f, 9.380329251e-01f, 9.442805648e-01f, 9.505166411e-01f,
    9.567412138e-01f, 9.629543424e-01f, 9.691561460e-01f, 9.753468633e-01f, 9.815264344e-01f, 9.876950979e-01f, 9.938529134e-01f, 1.000000000e+00f,
};

static constexpr float MA_VELOCITY_FILTER[128] = {
    0.000000000e+00f, 5.344314035e-03f, 1.129807346e-02f, 1.750583947e-02f, 2.388453484e-02f, 3.039342165e-02f, 3.700797632e-02f, 4.371171817e-02f,
    5.049276724e-02f, 5.734213814e-02f, 6.425278634e-02f, 7.121903449e-02f, 7.823619992e-02f, 8.530034870e-02f, 9.240814298e-02f, 9.955671430e-02f,
    1.067435294e-01f, 1.139663979e-01f, 1.212233528e-01f, 1.285126507e-01f, 1.358327121e-01f, 1.431821287e-01f, 1.505596042e-01f, 1.579639763e-01f,
    1.653941423e-01f, 1.728491336e-01f, 1.803280115e-01f, 1.878299564e-01f, 1.953541487e-01f, 2.028998882e-01f, 2.104664743e-01f, 2.180532664e-01f,
    2.256596684e-01f, 2.332851142e-01f, 2.409290671e-01f, 2.485910356e-01f, 2.562705278e-01f, 2.639671266e-01f, 2.716803849e-01f, 2.794098854e-01f,
    2.871552706e-01f, 2.949161530e-01f, 3.026921749e-01f, 3.104830682e-01f, 3.182884455e-01f, 3.261080384e-01f, 3.339415491e-01f, 3.417886794e-01f,
    3.496491909e-01f, 3.575228155e-01f, 3.654092848e-01f, 3.733084202e-01f, 3.812199235e-01f, 3.891436160e-01f, 3.970792890e-01f, 4.050267339e-01f,
    4.129857421e-01f, 4.209561050e-01f, 4.289377034e-01f, 4.369302988e-01f, 4.449337423e-01f, 4.529478550e-01f, 4.609724879e-01f, 4.690074921e-01f,
    4.770526886e-01f, 4.851079583e-01f, 4.931731522e-01f, 5.012481213e-01f, 5.093327761e-01f, 5.174269080e-01f, 5.255303979e-01f, 5.336432457e-01f,
    5.417651534e-01f, 5.498961210e-01f, 5.580360293e-01f, 5.661846995e-01f, 5.743421316e-01f, 5.825080872e-01f, 5.906825662e-01f, 5.988654494e-01f,
    6.070565581e-01f, 6.152558923e-01f, 6.234633923e-01f, 6.316788197e-01f, 6.399022341e-01f, 6.481334567e-01f, 6.563723683e-01f, 6.646190286e-01f,
    6.728732586e-01f, 6.811349988e-01f, 6.894041300e-01f, 6.976806521e-01f, 7.059644461e-01f, 7.142554522e-01f, 7.225536108e-01f, 7.308588028e-01f,
    7.391709685e-01f, 7.474901080e-01f, 7.558161020e-01f, 7.641488910e-01f, 7.724884152e-01f, 7.808346152e-01f, 7.891874313e-01f, 7.975468040e-01f,
    8.059126735e-01f, 8.142849803e-01f, 8.226636648e-01f, 8.310486674e-01f, 8.394399285e-01f, 8.478374481e-01f, 8.562411070e-01f, 8.646509051e-01f,
    8.730667233e-01f, 8.814886212e-01f, 8.899164200e-01f, 8.983501792e-01f, 9.067897797e-01f, 9.152352214e-01f, 9.236863852e-01f, 9.321433306e-01f,
    9.406059384e-01f, 9.490742087e-01f, 9.575480819e-01f, 9.660274982e-01f, 9.745124578e-01f, 9.830029011e-01f, 9.914987683e-01f, 1.000000000e+00f,
};
```

## Appendix C — MIDI logarithmic tables

```c
static constexpr float MA_MIDI_CUTOFF_HZ[128] = {
    2.000000000e+01f, 2.111796379e+01f, 2.229841995e+01f, 2.354486084e+01f, 2.486097717e+01f, 2.625065994e+01f, 2.771802521e+01f, 2.926741219e+01f,
    3.090340805e+01f, 3.263085175e+01f, 3.445485687e+01f, 3.638082123e+01f, 3.841444397e+01f, 4.056174469e+01f, 4.282907104e+01f, 4.522314072e+01f,
    4.775102997e+01f, 5.042022705e+01f, 5.323862839e+01f, 5.621456909e+01f, 5.935686111e+01f, 6.267480469e+01f, 6.617821503e+01f, 6.987745667e+01f,
    7.378347778e+01f, 7.790784454e+01f, 8.226274872e+01f, 8.686109161e+01f, 9.171646881e+01f, 9.684325409e+01f, 1.022566147e+02f, 1.079725723e+02f,
    1.140080490e+02f, 1.203808899e+02f, 1.271099625e+02f, 1.342151794e+02f, 1.417175751e+02f, 1.496393280e+02f, 1.580038910e+02f, 1.668360291e+02f,
    1.761618652e+02f, 1.860089874e+02f, 1.964065552e+02f, 2.073853302e+02f, 2.189777985e+02f, 2.312182617e+02f, 2.441429443e+02f, 2.577901001e+02f,
    2.722001038e+02f, 2.874155884e+02f, 3.034815979e+02f, 3.204456787e+02f, 3.383580017e+02f, 3.572716064e+02f, 3.772424316e+02f, 3.983296204e+02f,
    4.205955200e+02f, 4.441060486e+02f, 4.689307861e+02f, 4.951431580e+02f, 5.228207397e+02f, 5.520455322e+02f, 5.829038696e+02f, 6.154871216e+02f,
    6.498917236e+02f, 6.862195435e+02f, 7.245779419e+02f, 7.650805664e+02f, 8.078471680e+02f, 8.530043945e+02f, 9.006857910e+02f, 9.510324707e+02f,
    1.004193481e+03f, 1.060326050e+03f, 1.119596436e+03f, 1.182179810e+03f, 1.248261475e+03f, 1.318037109e+03f, 1.391713013e+03f, 1.469507202e+03f,
    1.551650024e+03f, 1.638384521e+03f, 1.729967163e+03f, 1.826669189e+03f, 1.928776733e+03f, 2.036591919e+03f, 2.150433594e+03f, 2.270639160e+03f,
    2.397563721e+03f, 2.531583252e+03f, 2.673093994e+03f, 2.822515137e+03f, 2.980288818e+03f, 3.146881592e+03f, 3.322786377e+03f, 3.508524170e+03f,
    3.704644531e+03f, 3.911727295e+03f, 4.130385742e+03f, 4.361267090e+03f, 4.605053711e+03f, 4.862468262e+03f, 5.134271484e+03f, 5.421267578e+03f,
    5.724307129e+03f, 6.044285156e+03f, 6.382149902e+03f, 6.738900391e+03f, 7.115592773e+03f, 7.513341797e+03f, 7.933324219e+03f, 8.376782227e+03f,
    8.845029297e+03f, 9.339451172e+03f, 9.861508789e+03f, 1.041275000e+04f, 1.099480371e+04f, 1.160939355e+04f, 1.225833691e+04f, 1.294355664e+04f,
    1.366707812e+04f, 1.443104297e+04f, 1.523771191e+04f, 1.608947266e+04f, 1.698884570e+04f, 1.793849023e+04f, 1.894122070e+04f, 2.000000000e+04f,
};

static constexpr float MA_MIDI_TIME_MS[128] = {
    1.000000000e+00f, 1.081101298e+00f, 1.168779969e+00f, 1.263569474e+00f, 1.366046548e+00f, 1.476834655e+00f, 1.596607924e+00f, 1.726094842e+00f,
    1.866083264e+00f, 2.017425060e+00f, 2.181040764e+00f, 2.357925892e+00f, 2.549156666e+00f, 2.755896568e+00f, 2.979403257e+00f, 3.221036673e+00f,
    3.482266903e+00f, 3.764683008e+00f, 4.070003510e+00f, 4.400085926e+00f, 4.756938934e+00f, 5.142732620e+00f, 5.559814453e+00f, 6.010722637e+00f,
    6.498199940e+00f, 7.025212288e+00f, 7.594965935e+00f, 8.210927010e+00f, 8.876843452e+00f, 9.596767426e+00f, 1.037507725e+01f, 1.121650887e+01f,
    1.212618256e+01f, 1.310963154e+01f, 1.417283916e+01f, 1.532227421e+01f, 1.656492996e+01f, 1.790836716e+01f, 1.936075783e+01f, 2.093094063e+01f,
    2.262846565e+01f, 2.446366310e+01f, 2.644769859e+01f, 2.859263992e+01f, 3.091153908e+01f, 3.341850281e+01f, 3.612878799e+01f, 3.905887604e+01f,
    4.222660065e+01f, 4.565123367e+01f, 4.935360718e+01f, 5.335624695e+01f, 5.768350601e+01f, 6.236171341e+01f, 6.741932678e+01f, 7.288711548e+01f,
    7.879835510e+01f, 8.518900299e+01f, 9.209793854e+01f, 9.956719971e+01f, 1.076422272e+02f, 1.163721466e+02f, 1.258100815e+02f, 1.360134277e+02f,
    1.470442963e+02f, 1.589697723e+02f, 1.718624268e+02f, 1.858006897e+02f, 2.008693542e+02f, 2.171601257e+02f, 2.347720795e+02f, 2.538123932e+02f,
    2.743969116e+02f, 2.966508484e+02f, 3.207095947e+02f, 3.467195740e+02f, 3.748389587e+02f, 4.052388611e+02f, 4.381042480e+02f, 4.736350708e+02f,
    5.120474854e+02f, 5.535751953e+02f, 5.984708252e+02f, 6.470075684e+02f, 6.994807129e+02f, 7.562094727e+02f, 8.175390625e+02f, 8.838424683e+02f,
    9.555232544e+02f, 1.033017456e+03f, 1.116796387e+03f, 1.207369995e+03f, 1.305289307e+03f, 1.411149902e+03f, 1.525595947e+03f, 1.649323730e+03f,
    1.783085938e+03f, 1.927696533e+03f, 2.084035156e+03f, 2.253052979e+03f, 2.435778564e+03f, 2.633323242e+03f, 2.846889160e+03f, 3.077775391e+03f,
    3.327386963e+03f, 3.597242188e+03f, 3.888983154e+03f, 4.204384766e+03f, 4.545365723e+03f, 4.914000488e+03f, 5.312532227e+03f, 5.743385254e+03f,
    6.209181152e+03f, 6.712753418e+03f, 7.257166504e+03f, 7.845731934e+03f, 8.482030273e+03f, 9.169934570e+03f, 9.913627930e+03f, 1.071763477e+04f,
    1.158684863e+04f, 1.252655762e+04f, 1.354247656e+04f, 1.464078906e+04f, 1.582817578e+04f, 1.711186133e+04f, 1.849965430e+04f, 2.000000000e+04f,
};

static constexpr float MA_MIDI_LFO_HZ[128] = {
    2.999999933e-02f, 3.157597408e-02f, 3.323473781e-02f, 3.498063982e-02f, 3.681825846e-02f, 3.875241429e-02f, 4.078817368e-02f, 4.293087870e-02f,
    4.518614337e-02f, 4.755988345e-02f, 5.005832016e-02f, 5.268800631e-02f, 5.545583740e-02f, 5.836907029e-02f, 6.143534184e-02f, 6.466269493e-02f,
    6.805958599e-02f, 7.163491845e-02f, 7.539808005e-02f, 7.935892791e-02f, 8.352784812e-02f, 8.791577071e-02f, 9.253420681e-02f, 9.739525616e-02f,
    1.025116667e-01f, 1.078968570e-01f, 1.135649458e-01f, 1.195307970e-01f, 1.258100420e-01f, 1.324191540e-01f, 1.393754631e-01f, 1.466971934e-01f,
    1.544035673e-01f, 1.625147611e-01f, 1.710520685e-01f, 1.800378561e-01f, 1.894956827e-01f, 1.994503587e-01f, 2.099279761e-01f, 2.209560126e-01f,
    2.325633764e-01f, 2.447805107e-01f, 2.576394379e-01f, 2.711738646e-01f, 2.854193151e-01f, 3.004130721e-01f, 3.161945343e-01f, 3.328050077e-01f,
    3.502880633e-01f, 3.686895669e-01f, 3.880577385e-01f, 4.084433615e-01f, 4.298999012e-01f, 4.524836242e-01f, 4.762536883e-01f, 5.012724996e-01f,
    5.276055336e-01f, 5.553219914e-01f, 5.844944119e-01f, 6.151993275e-01f, 6.475172639e-01f, 6.815329790e-01f, 7.173355818e-01f, 7.550190091e-01f,
    7.946820259e-01f, 8.364285827e-01f, 8.803682923e-01f, 9.266161919e-01f, 9.752936363e-01f, 1.026528239e+00f, 1.080454230e+00f, 1.137213230e+00f,
    1.196953773e+00f, 1.259832740e+00f, 1.326014876e+00f, 1.395673752e+00f, 1.468991876e+00f, 1.546161652e+00f, 1.627385378e+00f, 1.712875962e+00f,
    1.802857518e+00f, 1.897566080e+00f, 1.997249961e+00f, 2.102170467e+00f, 2.212602615e+00f, 2.328835964e+00f, 2.451175451e+00f, 2.579941750e+00f,
    2.715472698e+00f, 2.858123064e+00f, 3.008267403e+00f, 3.166299105e+00f, 3.332632542e+00f, 3.507704020e+00f, 3.691972256e+00f, 3.885920763e+00f,
    4.090057850e+00f, 4.304918766e+00f, 4.531066418e+00f, 4.769094467e+00f, 5.019627094e+00f, 5.283320427e+00f, 5.560866356e+00f, 5.852992058e+00f,
    6.160464287e+00f, 6.484088898e+00f, 6.824714184e+00f, 7.183233261e+00f, 7.560585976e+00f, 7.957762241e+00f, 8.375802994e+00f, 8.815804482e+00f,
    9.278921127e+00f, 9.766365051e+00f, 1.027941704e+01f, 1.081941986e+01f, 1.138779068e+01f, 1.198601913e+01f, 1.261567497e+01f, 1.327840710e+01f,
    1.397595501e+01f, 1.471014595e+01f, 1.548290634e+01f, 1.629626083e+01f, 1.715234375e+01f, 1.805340004e+01f, 1.900178909e+01f, 2.000000000e+01f,
};
```
