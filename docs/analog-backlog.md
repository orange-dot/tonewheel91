# tonewheel91 — Mamut Analog implementation backlog

Date: 2026-08-20. Status: active; MA0, MA1 and MA2-1 are closed, and MA2-2
is the next queued task.
This document is decision-complete for `MA0` and the first implementation
slice; task state and validation results live in `docs/ma-dev-journal.md`.

The organ and `ep73` lines are frozen. This backlog replaces the former
plan to reproduce one five-voice synthesizer from 1978. The older machine
now supplies a physical vocabulary — two VCOs, voice cards, a driven
four-pole lowpass, RC envelopes, cross-modulation and unison — rather than
the product identity or an imitation target.

Working name: `mamutanalog`. C identifiers use the `ma_` prefix and the
top-level core type is `ma_synth`.

## Product thesis

`mamutanalog` is a five-card analog organism:

- the analog voice is the pitch and body spine;
- Mamut identity math turns a small number of musical gestures into
  coordinated changes across that spine;
- Mozaik is a third, structurally aperiodic source inside each voice;
- bounded card differences make the polyphony breathe before any effect;
- a later GFM field lets notes inject energy into one shared, hidden
  medium that perturbs the five cards;
- chorus and reverb provide the first playable stereo finish, but neither
  is allowed to manufacture the source identity.

The success question is therefore not "does it clone the 1978 unit?" It
is "does an unmistakably analog five-card instrument remain legible while
Mamut structure, identity and field behavior become part of how it plays?"

The old service documentation and chip data remain useful for reset slew,
control ranges, filter operating regions, envelope time constants and
voice-card tolerances. Reference recordings are listening context, not a
fidelity oracle. No manufacturer or model name enters source identifiers
or user-facing text.

## Donor truth: implemented, planned and speculative

`workspace/systems/mamut-sint-sw` is an executable donor and research
record, not a build or runtime dependency. The backlog uses the following
truth levels deliberately.

### Implemented in the donor

- Five public macros resolve through `Horizont`, `Pec` and `Baklja` into
  hidden `mass`, `strain`, `headroom`, `body_focus`,
  `rupture_threshold`, `rupture_response` and `spatial_dispersion` state;
  DSP primitives consume resolved parameters, never raw macros.
- The conventional VA path already demonstrates bandlimited mixed waves,
  shaped oscillator mechanisms, seeded phase, drift and jitter, FM/PM/AM,
  ring modulation, sync, source cross-mix, spectral and additive sources,
  driven filters, distributed saturation, chorus, reverb and output
  safety. This is a catalogue, not the feature list for this line.
- Mozaik v0.1/v0.2 is implemented as a Q32 cut-and-project word driving
  Hann tiles, with slope detents, contrast, tile-boundary phason changes,
  drift and per-voice pre-filter integration. Host-side controller bindings
  also exist; the separate audible touch-rig pairing remains open there.
- GFM note strikes, stereo probes and a read-only 16x16 inspection path are
  implemented. Its production engine currently renders an audible layer.
  This backlog reuses the field behavior but intentionally changes its
  insertion role.
- BCS Hopf/Duffing scenarios have reached a playable engine layer. Its
  real-rig listening verdict is still pending, so "implemented" does not
  mean accepted as part of this instrument.

### Planned in the donor, without implementation

- Orbita: per-voice partials that drift into, capture and escape from
  small-integer frequency relationships, with a later breakup regime.
- Kosava: one deterministic gust field plus per-voice vortex lock-in,
  including sequential chord ignition under a wind sweep.
- Deep per-note expression and UMP-first MIDI 2.0 transport: only the ADR
  and feasibility spike have landed; the engine voice-expression payload
  has not.
- Further GFM cost/rate work and explicit rupture/recovery performance
  gestures.

### Speculative only

- Lavina (governed avalanche criticality), Brazda (plastic learned
  topology) and Mraz (thermal phase transition and hysteresis) are concept
  notes. Nothing in them is scheduled or implemented.

This line transfers semantics, invariants and evidence methods. It does
not copy the donor's complete feature surface, patch schema, Rust runtime,
GUI, transport architecture, six-voice choice or parallel-layer layout.

## Frozen program decisions

These decisions are inputs to implementation, not milestone questions.

| ID | Decision |
| --- | --- |
| MAD1 | Five fixed voice cards; free-card search is round-robin and exhaustion steals the oldest assigned card. |
| MAD2 | Notes cover the full MIDI range 0..127 with no folding or out-of-compass rejection. |
| MAD3 | Both VCOs continuously mix saw, pulse, triangle and Mamut sine; Mozaik is a separate third/AUX source. |
| MAD4 | One nonlinear four-pole lowpass is the filter identity; no filter-model menu in MA0–MA5. |
| MAD5 | Direct analog controls and five public Mamut macros coexist. All five macros at zero are an exact direct-path bypass and are the init default. |
| MAD6 | GFM first lands as a hidden modulation field, not an audible parallel layer. Its post-MA4 factory amount is subtle and nonzero. |
| MAD7 | The first playable version is stereo and includes fixed card spread followed by Mamut-derived chorus and reverb. |
| MAD8 | The core exposes one concrete `ma_patch` value type and grouped C setters. Hosted tools own a strict versioned one-file-per-patch format; there is no generic registry, plug-in API or core I/O. |
| MAD9 | MIDI control is a generic, stable map owned by this repo; a PC4 or other controller maps to it externally. |
| MAD10 | Tepih is the compiled factory dark pad, Lead is the direct voice, and Dubina is the VCO2-sine-dominant low voice. |
| MAD11 | Mozaik and GFM are committed. BCS, Orbita and Kosava require later evidence and promotion decisions; wild concepts stay research-only. |

There are no open product decisions before MA1. Numeric derivations still
have to be executed and recorded at MA0, but their algorithms, domains and
selection rules are fixed below.

## Repository cut

Same repo, sibling instrument family — not a fork and not a shared
instrument framework.

- New core translation units use `src/ma_*.c`; public declarations live in
  `src/mamutanalog.h`. That header may include `tonewheel.h` for the
  repository's sine, saturation, absolute-value, one-pole, SplitMix and
  FNV helpers. Neither the organ nor `ep73` includes `mamutanalog.h`.
- Three top-level instrument structs and three live binaries remain
  concrete. Do not add a vtable, generic instrument interface, callback
  graph, patch registry or generic patch framework.
- The organ and `ep73` translation units are not edited. Their existing
  pinned signatures remain in the same `make test` run; any movement is a
  defect.
- The core remains freestanding C23: no OS calls, I/O, allocation or
  `libm`; fixed caller-owned state; f32 render math; hostile input
  sanitization at setters; no `-ffast-math`; `-ffp-contract=off`.
- Hosted code owns ALSA, SMF, WAV, CLI parsing, evidence file output and
  any double-precision analysis. The core owns musical and realtime truth.
- Do not create a dependency on the Mamut Rust workspace. Directly reused
  algorithms receive a language-neutral contract and golden state traces;
  the C implementation then owns this instrument's behavior.

No root layout changes are involved, so `LAB_LAYOUT.md` does not change.
`README.md` and `docs/design.md` gain the third instrument only when MA1
lands code, following the existing repo convention.

## Signal and control architecture

```text
direct analog controls --------------------------+
                                                  |
five macros -> identity -> hidden state -> deltas +--> five voice cards
                                                  |
note/velocity/pressure -> performance state ------+
note strikes -> GFM field (MA4) -> card deltas ---+

per card:
  VCO1 + VCO2 + noise + Mozaik AUX
      -> normalized source mix / sync / cross-mod
      -> mixer pressure
      -> nonlinear four-pole lowpass
      -> VCA

five card outputs
  -> deterministic card pans
  -> shared mid/side body
  -> DC block and bounded safety
  -> chorus
  -> reverb
  -> stereo f32 frame
```

The direct layer constructs a sound. The identity layer is a performance
overlay. A raw macro never enters an oscillator, filter, VCA, effect or
field primitive. `ma_synth` resolves macros and performance inputs into
bounded direct deltas, smooths continuous destinations, and only then
renders the DSP.

All optional stages have an exact inert setting:

- `mozaik.mix = 0`
- `mixer_pressure = 0`
- all macros `= 0`
- `character = 0`
- `gfm.amount = 0`
- chorus disabled with mix `0`
- reverb disabled with mix `0`

Where a stage enters after an established path, its inert setting must
reproduce that pre-stage PCM bit for bit. MA1 stages use baselines captured
inside MA1; character uses the intermediate ideal-card MA2 baseline; GFM
uses the landed MA3 baseline. An off stage is not merely very quiet.

## Core public boundary

The public header exposes instrument-specific groups, not a generic patch
or cross-instrument parameter registry.

Required public shape notation (field bodies are omitted; this is not a
literal compilable header):

```c
typedef struct { float attack_ms, decay_ms, sustain, release_ms; } ma_adsr;
typedef struct { float left, right; } ma_frame;

typedef enum {
    MA_MACRO_GRAVITACIJA,
    MA_MACRO_BLOOM,
    MA_MACRO_HEAT,
    MA_MACRO_RUIN,
    MA_MACRO_SWARM,
    MA_MACRO_COUNT
} ma_macro_id;

typedef struct {
    /* Complete fixed state; fields are added by the owning MA milestone. */
} ma_synth;
```

`ma_synth` is a complete public struct, as the existing instrument types
are, not an opaque or heap-allocated handle. The empty body above is
notation only: the implementation fills it with the fixed state owned by
the current milestone. Its size and all effect buffers are compile-time
fixed.

The event boundary is pinned:

```c
void ma_synth_init(ma_synth *s, float sample_rate_hz);
ma_frame ma_synth_tick(ma_synth *s);
void ma_synth_note_on(ma_synth *s, uint8_t channel,
                      uint8_t note, uint8_t velocity);
void ma_synth_note_off(ma_synth *s, uint8_t channel,
                       uint8_t note, uint8_t release_velocity);
void ma_synth_set_sustain(ma_synth *s, bool down);
void ma_synth_set_pitch_bend(ma_synth *s, float semitones);
void ma_synth_set_channel_pressure(ma_synth *s, float pressure);
void ma_synth_set_poly_pressure(ma_synth *s, uint8_t channel,
                                uint8_t note, float pressure);
void ma_synth_set_mod_wheel(ma_synth *s, float amount);
void ma_synth_panic(ma_synth *s);
```

`ma_synth_note_off` accepts release velocity for forward compatibility but
v1 ignores and counts it. Poly pressure updates every live instance of the
matching channel/note pair. Panic clears sustain and held ownership, puts
all active envelopes into their ordinary release stage, and leaves effect
tails to decay; it does not hard-zero DSP state.

Required operation groups:

- lifecycle/render: `ma_synth_init`, `ma_synth_tick`, `ma_synth_panic`;
- performance: note on/off, sustain, pitch bend, channel pressure,
  per-note pressure and mod wheel;
- oscillator: per-VCO waveform levels, VCO2 level/interval/fine tune,
  pulse widths, sync, cross-mod and noise;
- voice: two ADSR setters, glide, LFO, unison and character;
- filter/body: cutoff, resonance, keytrack, envelope amount, filter drive,
  mixer pressure, master level and stereo width;
- Mozaik: mix, slope, contrast, phason and drift;
- identity: `ma_synth_set_macro(ma_synth *, ma_macro_id, float)`;
- effects: enable plus parameter setters for chorus and reverb;
- MA4: GFM amount and seed/session reset.

Grouped setters may take a small value struct where that makes an update
atomic, notably ADSR and effects. Do not expose one untyped
`set_parameter(id, value)` function. Setters sanitize non-finite and
out-of-domain input; render functions assume initialized, finite state.

## Direct parameter domains

- Waveform, source, sync, cross-mod, pressure, character, macro, width and
  wet values: `[0, 1]`.
- Pulse width: `[0.05, 0.95]`.
- VCO2 interval: integer semitones `[-24, 24]`; fine tune `[-50, 50]`
  cents. Pitch bend is a separate `[-2, 2]` semitone performance delta.
- Filter cutoff: `[20 Hz, min(20 kHz, 0.42 * sample_rate)]`.
- Resonance: `[0, 1]`, with the MA0 mapping pinning the onset of sustained
  self-oscillation below the top endpoint.
- Envelope stages: `[1 ms, 20 s]`; sustain `[0, 1]`.
- LFO: `[0.03, 20 Hz]`; glide: `[0, 10 s]`.
- Mozaik slope control: `[0, 1]` maps to sigma `[0.45, 0.75]` with the
  donor's five Q32 detents; contrast maps to `[1.0, 2.2]`; phason wraps;
  drift maps quadratically to at most `0.5` cycles/s.
- All continuous render destinations use a 6 ms linear smoother, clamped
  to 8..512 samples. Envelope stage changes apply at the next sample
  through their own coefficient state and do not restart a note.

## Fixed initialization patches

`ma_synth_init` selects the factory Tepih; `ma_synth_init_patch` accepts a
concrete `ma_patch` value. Tepih, Lead and Dubina are compiled constants and
have exact hosted mirrors in `patches/mamutanalog/`. The core has no I/O,
allocation or registry; `driver/ma_patch_file.c` owns the strict version-1
text format and atomic save. These values are public behavior and are not
retuned silently:

| Group | Default |
| --- | --- |
| VCO1 | saw `.70`, pulse `.25`, triangle `.15`, sine `.20`, pulse width `.50` |
| VCO2 | saw `.35`, pulse `.20`, triangle `.55`, sine `0`, level `.62`, interval `0`, fine `+7 cents`, pulse width `.50` |
| Oscillator modulation | sync `0`, sync softness `0`, cross-mod `0` |
| Noise | `.02` |
| Mozaik | seed `0x4D6F7A31`, mix `.20`, golden slope, contrast `tau`, phason `0`, drift `.05` |
| Mixer/filter/body | pressure `.15`, cutoff `900 Hz`, resonance `.18`, filter drive `.12`, envelope amount `.30`, keytrack `.45`, body drive `.10` |
| Amp ADSR | attack `600 ms`, decay `1600 ms`, sustain `.82`, release `3000 ms` |
| Filter ADSR | attack `350 ms`, decay `1800 ms`, sustain `.50`, release `2600 ms` |
| Voice | LFO off, glide off, unison off, character `.20` |
| Identity | all five macros `0` |
| Stereo/output | width `.70`, master `.18`, safety knee `.98`; body/crossfeed identity deltas zero |
| Chorus | enabled, mix `.14`, depth `.22`, rate `.18 Hz` |
| Reverb | enabled, mix `.10`, size `.40`, damping `.50` |
| GFM after MA4 | enabled, amount `.12`, seed `0x6A464D40` |

Lead uses VCO1 saw/pulse/triangle/sine `.75/.30/.05/.10`, VCO2
`.55/.25/.10` at level `.55`, moderate sync/cross-mod `.22/.12`, cutoff
`1900 Hz`, short amp/filter envelopes and nonzero Mamut identity. Its complete
literal state is pinned in `docs/ma1-6p-audition.md`.

Dubina uses a dominant VCO2 Mamut sine `.85` one octave below the played
note, with VCO2 level `.90`; its complete literal state and the patch-file /
Patchlab boundary are pinned in `docs/ma1-6r-patchlab.md`.

MA3 contains one named listening ballot for small calibration of this
default. A changed value must be logged against the table, rendered in an
A/B pair and approved as a group; implementation convenience is not a
reason to drift it.

## Voice model

### Note and frequency spine

MA0 pins a 128-entry equal-tempered frequency table for MIDI 0..127. No
runtime transcendental is needed. The table is the pitch spine for both
VCOs and Mozaik; per-card character and performance pitch are bounded
multipliers around it.

A source never lies about pitch to remain numerically convenient:

- the analog oscillators accept any base note whose fundamental is below
  `0.45 * sample_rate`; a source at or above the guard smoothly reaches
  zero gain;
- interval, bend, cross-mod and drift are clamped before phase-step
  construction;
- Mozaik is active only where a full tile can respect its minimum length:
  `20 Hz <= f0 <= min(8000 Hz, sample_rate / 8)`. It fades over the same
  6 ms control window on either boundary instead of clamping or octave
  folding;
- the filter keytrack and envelopes continue normally when an individual
  source is guard-muted.

Thus the public instrument accepts all MIDI notes even though a particular
source can leave the mix at a numerical boundary.

### Analog oscillators

Each VCO owns a phase accumulator and bandlimited triangle state. Saw and
pulse use a second-order PolyBLEP at every discontinuity. Triangle
integrates the bandlimited 50% pulse and resets its integrator consistently
on hard sync. Waveform mixes are divided by `max(sum(levels), 1)` so adding
a waveform does not create an accidental gain multiplier.

VCO2 is the interval/fine-tune oscillator. The core routing fixed for v1:

- VCO1 reset hard-syncs VCO2. On the master wrap, VCO2 applies
  `phase *= 1 - effective_sync`; `effective_sync = sync_amount *
  clamp(1 - .75 * sync_softness, 0, 1)`. Its triangle integrator resets to
  the resulting phase. The discontinuity corrector receives the actual
  pre/post phase jump;
- VCO2 modulates VCO1 instantaneous frequency through the cross-mod amount;
- the filter envelope can additionally modulate VCO1 frequency and both
  pulse widths through fixed depths pinned at MA0;
- no independent PM, AM, ring-mod, spectral source, additive source or
  cross-mix mode enters MA0–MA5.

The discontinuity correction is evaluated with the effective, clamped
phase step. MA1 separately measures ordinary notes, hard sync and the
worst cross-mod script; success in the first case does not excuse aliasing
in the latter two.

### Mozaik AUX

Port the landed donor contract rather than redesigning it:

- Q32 Bresenham/cut-and-project word;
- slope clamp `0.45..0.75` and exact Q32 constants for `1/2`, `3/5`,
  golden, `5/8` and `2/3`;
- long/short tile duration ratio controlled by contrast;
- signed full-tile Hann pulse;
- pending absolute phason latched at a tile boundary;
- deterministic per-card phason obtained by folding the layer seed with
  the card ordinal;
- no RNG in the oscillator itself.

The integer tile-kind and phason traces must match the Rust donor exactly.
PCM is not required to be cross-language bit-identical because the C line
uses `tw_sin_turns`; its waveform invariants, pitch anchor and render
signature are pinned locally.

### Mixer pressure, filter and envelopes

Mixer pressure is a normalized pregain, bounded saturator and compensating
postgain. Zero branches directly to the normalized source sum and leaves
its state untouched. It is distinct from filter drive and the shared
output body.

The filter is a topology-preserving four-one-pole ladder with global
resonance feedback and `tw_sat` inside the loop. Its fixed numerical
contract is:

- cutoff prewarp from a bounded repository-local polynomial over
  `0..0.42 * sample_rate`, coefficients pinned and error-tabled at MA0;
- 2x internal oversampling around mixer-pressure plus ladder nonlinearity;
- two fixed implicit-solver iterations per oversampled substep;
- one fixed seven-tap linear-phase halfband used as a polyphase
  interpolator before the nonlinear section and as the matching decimator
  afterward; its coefficients, latency and passband/stopband error are
  pinned at MA0;
- finite-state sanitization and a deterministic reset to zero if a hostile
  setter or arithmetic fault ever produces non-finite state;
- resonance mapping leaves the physical bass loss uncompensated.

Both ADSRs use RC one-pole movement toward the stage target through
`tw_one_pole_coeff`, with stage completion thresholds and exact epsilon
snap. Note-on retriggers from the current level; it does not force a phase
or envelope reset. Note-off enters release from the current level.

### Output body and safety

After card panning, convert the stereo sum to mid/side. Shared body operates
on mid; side is gain-shaped by width, crossfeed and identity dispersion,
then recombined. This preserves one shared chassis while retaining card
position.

The output stage uses the `tw_drive` form at a new, MA0-pinned operating
point, followed by a DC blocker and a bounded soft safety knee. Samples
strictly below the knee are unchanged. Diagnostics count non-finite
sanitizations, safety hits, maximum reduction and tiny flushes. A safety
limiter is not evidence that an upstream stage is stable; the evidence
tables report both pre- and post-safety peaks.

## Identity and performance mapping

Use the donor's current `Horizont`, `Pec`, `Baklja` and derived-state
equations with all patch bias terms fixed to zero. Raw macros are clamped
and linear in v1; the donor's patch-selectable power/square-root response
curves do not enter a no-patch instrument.

At initialization, resolve a zero-macro identity frame. Every mapping into
the direct layer uses the delta from that frame. This is the mechanism,
not a test-only special case, that makes five zero macros reproduce the
direct path exactly.

Reuse the donor coefficients where the same destination exists:

- Bloom/Horizont air scale filter cutoff;
- Baklja edge plus Ruin increase resonance;
- Pec heat plus strain increase filter drive;
- Baklja sync bias and readiness increase sync and cross-mod;
- Horizont span and spatial dispersion widen the cards;
- mass reduces excessive resonance slightly and adds body;
- body focus narrows side/crossfeed; headroom controls shared-body load.

Normalize multiplicative formulas to unity at the zero frame. Clamp every
resolved destination back to its direct domain before smoothing.

Analog-specific Mozaik deltas are fixed:

- Bloom adds at most `.10` to Mozaik mix;
- Heat adds at most `.20` in normalized contrast-control space;
- Ruin adds at most `.15` turns to pending phason;
- Swarm adds at most `.35` to normalized drift.

Performance overlays happen before identity resolution:

- channel aftertouch adds up to `.45` Gravitacija and `.35` Ruin;
- mod wheel adds up to `.35` Bloom and `.50` Swarm;
- per-note pressure adds up to `.25` octave to that card's cutoff and
  `.10` to its VCA pressure, after smoothing;
- velocity uses MA0-pinned 128-entry approximations of the donor's
  `v^0.78` level and `v^1.08` filter curves. Default amplitude is
  `.25 + .75 * level_curve`; filter-envelope depth gains up to `.25 *
  filter_curve`.

Direct macro CCs set the stored base macro. Aftertouch and mod wheel are
temporary additions and cannot overwrite the base value.

## Five-card polyphony, character and stereo

### Allocator

The bank has five caller-owned cards and a round-robin cursor.

1. Starting at the cursor, assign the first idle card.
2. If no card is idle, steal the assigned card with the oldest monotonically
   increasing age; held, sustained and released phases participate in the
   same rule.
3. Advance the cursor to the slot after the chosen card.
4. A repeated NoteOn may occupy another card. NoteOff releases the oldest
   still-held instance of that channel/note pair.
5. Sustain changes a released held instance to sustained-release; pedal-up
   releases all such instances without reordering their ages.

Entering or leaving unison performs the instrument panic transition first.
In unison, every new NoteOn assigns all five cards to the newest note with
their individual character and pan; NoteOff releases all five. This avoids
undefined migration of already allocated voices.

### Card character

`character` scales fixed-seed, per-card deviations. The canonical draw seed
is `0x4D41434841523031` (`MACHAR01`). At `1.0` the maximum
domains are:

- VCO1 static tune: +/-6 cents;
- VCO2 additional static tune: +/-8 cents;
- slow bounded tuning walk: +/-3 cents, updated every 32 samples;
- filter cutoff bias: +/-0.12 octave;
- each envelope stage time: +/-8 percent;
- VCA trim: +/-3 percent.

Draws come from one canonical seed folded with the card ordinal and
parameter tag, so adding a later draw cannot perturb existing ones. The
slow walk is bounded, deterministic and has a fixed update budget.
`character=0` multiplies every deviation by literal zero and rejoins the
pre-character render bit for bit. The factory default is `.20`.

### Stereo

Card pan positions before the width control are:

```text
card 0   -0.750
card 1   -0.375
card 2    0.000
card 3   +0.375
card 4   +0.750
```

Equal-power coefficients are calculated only when width changes, using the
repository sine kernel. Width zero returns the identical mono sum to left
and right. Identity dispersion and MA4 field pan are bounded offsets around
these positions, never a replacement for them.

## Chorus and reverb

The first playable chain ports the behavior of the donor's
`SimpleChorus` and `SimpleReverb`, not its Rust allocation strategy.

- Chorus: stereo delay buffers up to 80 ms at the maximum sample rate,
  one sine LFO with the donor's three phase taps, interpolated reads,
  bounded feedback, controls `enabled/mix/depth/rate`.
- Reverb: stereo buffers up to 340 ms, two size-dependent tap offsets,
  cross-feedback, damped feedback and controls
  `enabled/mix/size/damping`.
- Buffers are fixed arrays inside caller-owned state. The live driver uses
  static storage; initialization clears them once.
- Disabled skips the stage and leaves its state untouched. The setter that
  changes disabled to enabled clears the stage outside the sample loop so
  stale tails cannot reappear. Mix zero while enabled may advance state but
  must return dry PCM bit-identically.
- No chorus/reverb macro routing in MA0–MA5. The effects remain direct
  controls so identity evaluation stays attributable to the voice.

## Generic MIDI contract

MIDI is single-timbre. The driver's existing selected-channel policy and
owner bookkeeping apply; the core receives normalized instrument events.
All 128 notes are valid. NoteOn velocity zero is NoteOff for MIDI 1.0.

| Input | Assignment |
| --- | --- |
| Pitch bend | bipolar 14-bit, +/-2 semitones |
| Channel pressure | performance overlay to Gravitacija/Ruin |
| Poly key pressure | filter/VCA pressure for every live matching channel/note instance |
| CC1 | mod wheel overlay to Bloom/Swarm |
| CC5 | glide time |
| CC7 | master level |
| CC14 | VCO2 interval, quantized -24..+24 semitones |
| CC15 | noise level |
| CC17..20 | amp decay, amp sustain, filter sustain, filter release |
| CC64 | sustain pedal, value >=64 is down |
| CC65 | glide enable, value >=64 is on |
| CC71 | filter resonance |
| CC72 | amp release |
| CC73 | amp attack |
| CC74 | filter cutoff through the MA0 128-entry log table |
| CC75 | filter decay |
| CC76 | filter-envelope amount |
| CC77 / CC78 | LFO depth / rate |
| CC79 | filter attack |
| CC80..85 | VCO1 saw/pulse/triangle, then VCO2 saw/pulse/triangle |
| CC86 | VCO2 level |
| CC87 | VCO2 fine tune, -50..+50 cents with center 64 |
| CC88 / CC89 | VCO1 / VCO2 pulse width |
| CC90 / CC91 | sync / cross-mod amount |
| CC92 / CC93 | stereo width / card character |
| CC94 / CC95 | Mozaik mix / slope |
| CC102..104 | Mozaik contrast / phason / drift |
| CC105 / CC106 | mixer pressure / filter drive |
| CC107 / CC108 | chorus mix / reverb mix |
| CC109..113 | Gravitacija, Bloom, Heat, Ruin, Swarm |
| CC114 | unison, value >=64 is on |
| CC115 | GFM amount after MA4; ignored and counted before MA4 |
| CC120 / CC123 | panic |

Unassigned CCs and unsupported messages are ignored and counted. The
complete-string/range validation rules of the existing hosted parsers
apply to any equivalent CLI control. No NRPN, MPE zone or UMP transport is
part of v1; the per-note core state is transport-agnostic so those can be
added later without redesigning a voice.

## Determinism, realtime and memory contract

- Same binary, sample rate, seed, initial controls and event stream produce
  bit-identical stereo PCM. Every evidence render runs twice and asserts
  FNV-64 equality.
- All iteration counts, buffers, card counts and field dimensions are
  fixed. Render paths perform no allocation, formatting, logging, I/O,
  blocking or hidden convergence loop.
- Unsupported/non-finite sample rates select 48 kHz; supported rates remain
  44.1..192 kHz.
- Tiny state snaps to exact zero before it can become denormal work.
- `sizeof(ma_synth)` must remain below 1 MiB at the 192 kHz-capable layout.
- Optimized core object inspection must show no unresolved OS, allocation
  or `libm` symbols. Any required freestanding memory primitive is handled
  under the existing repo contract.
- Default live target: 48 kHz, 128-frame periods, three-period ALSA buffer.
- Final Raspberry Pi 3B gate: p99 rendering of a worst-case 128-frame
  period is below 50 percent of the 2.667 ms deadline with five held
  voices, factory effects and GFM enabled; no xrun in a 30-minute soak.
  Host numbers are recorded separately and never presented as SBC proof.

## Evidence protocol

Each milestone names its experiment boundary before implementation:

1. command;
2. pre-slice signature baseline;
3. input script or MIDI fixture and its checksum;
4. expected WAV, CSV/table and console-log artifacts;
5. executable assertions;
6. listening question that metrics do not answer;
7. remaining unknowns after the run.

Artifacts render under `build/` during development. The durable result is
the evidence Markdown plus the logged render entry in `docs/renders.md`;
large WAVs remain ignored unless the existing repo policy explicitly says
otherwise. A green unit test proves its named invariant, not musical
acceptance. Listening gates record the compared files and operator verdict.

Canonical targets introduced as their milestone lands:

```sh
make test
make exhibit-ma1
make exhibit-ma2
make exhibit-ma3
make exhibit-ma4
```

`make test` remains the one aggregate regression command and always runs
the pinned organ and EP suites. An exhibit target fails on broken numeric
or signature assertions; it does not silently emit an unjudged WAV and
claim success.

## MA milestones

### MA0 — hybrid contract and constants

Deliver `docs/ma-constants.md` before core code. It pins:

- the 128-entry note-frequency table and source guard policy;
- oscillator phase, PolyBLEP, triangle integration, sync reset and
  cross-mod depth laws;
- source normalization, mixer pressure and gain staging;
- Mozaik Q32 constants, detent snap, tile duration and pitch-anchor rules,
  copied from the landed donor contract with provenance;
- filter cutoff prewarp polynomial, seven-tap halfband coefficients,
  solver/oversampling budget, resonance map and saturation operating point;
- RC envelope coefficient/threshold rules and the factory timings;
- all direct control ranges, 128-entry MIDI log/curve tables and exact CC
  conversions;
- identity equations, zero-frame values, performance depths and direct
  delta clamps;
- card-character seeds, tag-stable draws, bounds and default;
- pan coefficients, body/output operating point, effect capacities and
  factory dark-pad table;
- GFM dimensions, rate and modulation bounds for MA4, clearly marked
  future even though pinned now.

Every constant is tagged `[DONOR]`, `[SOURCE]`, `[DERIVED]` or `[DECISION]`.
Hosted derivation programs may use double and `libm`; they print C-ready
tables and maximum error. Generated output is reviewed and then pasted as
pinned constants — code generation is not a build dependency.

Gate:

- register audit finds no unowned number used by MA1;
- polynomial/table errors are stated over their full domains;
- Mozaik integer golden vectors are captured from the donor at a named
  commit;
- current organ/EP `make test` baseline is recorded before MA1 changes.

### MA1 — complete one-voice hybrid

One L-size, offline vertical slice containing:

- VCO1/VCO2 mixed bandlimited oscillators, noise and hard sync;
- bounded VCO2-to-VCO1 cross-mod and filter-envelope oscillator routes;
- Mozaik as the third pre-filter source;
- normalized mixer and exact-bypass pressure;
- 2x nonlinear four-pole filter;
- filter and amp RC ADSRs plus VCA;
- five-macro identity resolver, performance overlays and 6 ms smoothers;
- centered dual-mono output, output body bypass, DC blocker, safety
  diagnostics and deterministic signatures.

No allocator, live MIDI, LFO/glide/unison, character, GFM, chorus or
reverb lands here. Do not reserve unused behavior merely to avoid future
header edits; this repo does not promise a stable pre-release struct ABI.

Experiment boundary: can one complete analog voice retain a coherent dark
pad while Mozaik and macro gestures enter inside the voice rather than as
post-effects?

`make exhibit-ma1` produces named renders/tables:

- naive vs PolyBLEP saw/pulse at MIDI 24, 60, 96, 120;
- free vs hard-sync and low/high cross-mod alias cases;
- analog-only, golden Mozaik, rational-detent and drifting-phason takes;
- low/mid/high cutoff sweeps, resonance-to-self-oscillation and driven
  filter A/B;
- each macro swept alone, all macros zero, and one combined gesture;
- pre/post-safety peak and limiter-hit table;
- per-frame host cost for each stage.

Acceptance:

- all state remains finite under full-domain hostile control sweeps;
- bandlimited ordinary, sync and cross-mod cases reduce in-band alias
  energy by at least 20 dB relative to their naive counterparts at the
  named high-note anchors, or the slice stops for an explicit algorithm
  revision;
- small-signal cutoff matches the MA0 double-precision reference within
  two percent across the pinned sweep;
- self-oscillation is bounded and monotonic with resonance, and stays
  within 30 cents of its MA0 tuning target from C2 through C7 at 48 kHz;
  other rates and notes are tabled rather than hidden;
- Mozaik tile/phason integer traces equal the donor vectors;
- `mozaik.mix=0`, zero pressure and zero macros each satisfy their exact
  bypass signatures;
- two fresh renders of every acceptance take are byte-identical;
- an operator listening note answers the experiment question before MA2.

Closure note, 2026-08-27: the operator accepted the registered 248-second
listening take and explicitly closed MA1 without further evidence writing or
measurement. The list above remains the original milestone contract rather
than an outstanding work queue.

### MA2 — five-card body

Add the fixed five-card bank, allocator, sustain, LFO, glide, unison,
character, deterministic pan positions and shared mid/side body. Still
offline; no ALSA binary or SMF instrument switch yet.

Experiment boundary: do five identifiable cards behave like one instrument
rather than five cloned software voices, and is stealing musically legible
without clicks or hidden state ambiguity?

`make exhibit-ma2` produces:

- a scripted sixth-note steal with slot/age/phase trace and WAV;
- repeated-note and sustain/steal traces;
- unison enter/play/leave sequence;
- one-, three- and five-card stereo renders with correlation/pan tables;
- `character=0`, factory `.20` and maximum character A/B;
- full MIDI 0..127 sweep reporting each active/muted source and guard
  transition;
- worst-case five-card cost with filter oversampling.

Acceptance:

- traces match the frozen allocator and unison rules exactly;
- no assignment, steal, pedal or mode transition produces a non-finite or
  discontinuity beyond the documented envelope/reset behavior;
- character zero matches an intermediate MA2 ideal-card baseline rendered
  and hardcoded before the deviation pass is added; it is not compared to
  MA1, whose one-voice topology is different;
- fixed seed and event stream repeat exactly;
- width zero is exact dual mono; card pan ordering is monotonic;
- source guard transitions are smoothed and no source emits a false folded
  or clamped pitch.

### MA3 — first playable, stereo and effects

Land:

- the `mamutanalog` ALSA live binary using the existing one-loop shape and
  xrun/panic discipline;
- `render_midi -I mamutanalog`, leaving the current default and existing
  organ/EP options unchanged;
- the complete generic MIDI map and counters;
- fixed-buffer Mamut-derived chorus and reverb;
- the compiled dark-pad defaults and one controlled listening ballot.

Experiment boundary: does the same event stream produce the same playable
instrument live and offline, and do the effects finish rather than conceal
the hybrid voice?

`make exhibit-ma3` produces:

- direct-event and SMF-twin renders with identical PCM signatures;
- dry, chorus-only, reverb-only and factory wet A/B;
- effect disabled and mix-zero bypass/rejoin tests;
- velocity, modwheel, aftertouch, poly-pressure and macro-control scripts;
- panic, sustain, repeated-note and full-compass SMF cases;
- memory-size report and full-chain host cost;
- a dated dark-pad ballot whose only allowed outcome is retain the table or
  replace named values with recorded A/B evidence.

Gate: live play on the reference rig, twin signatures logged in
`docs/renders.md`, no xrun during the listening session, effects judged as
support rather than source identity, and all pre-existing test signatures
unchanged.

### MA4 — hidden GFM card field

Port the donor's bounded field substrate as an engine-owned control field:

- 16x16 toroidal lattice;
- K=7 topology: four local and three deterministic long links;
- fixed phase, velocity, detune, energy, strain, heat, coherence, fracture
  and health state per cell;
- deterministic pitch-class/octave note-strike placement. For note `n`,
  `x_base = (n % 12) * 16 / 12` and
  `y_base = (n / 12) * 16 / 11`; compute
  `folded = seed ^ (seed >> 27) ^
  (n * 0x9E3779B97F4A7C15)`, take
  `dx = ((folded >> 8) % 3) - 1` and
  `dy = ((folded >> 16) % 3) - 1`, then wrap both coordinates;
- weaker NoteOff release strike;
- channel pressure/identity posture excitation;
- fixed update every 32 audio samples. Initialize field coefficients from
  the effective control rate `sample_rate / 32` so seconds-based behavior
  survives the rate change, then use 6 ms interpolation into cards;
- no audio probe mixed into PCM and no GUI/INSPECT requirement.

Only automatic identity posture exists in v1; there is no public
`horizont/pec/baklja` program selector. Copy the donor's current automatic
posture score at the MA0-pinned donor commit. A sounding card reads the
cell at its note-strike coordinate through a fixed five-tap cross:
`.50 * center + .125 * (north + south + east + west)`. At
`gfm.amount=1`, the resulting normalized field readout can contribute:

- pitch bias up to +/-6 cents;
- filter cutoff up to +/-0.18 octave;
- positive filter-drive pressure up to `.12`;
- VCA pressure up to +/-`.04`;
- sync-softness offset up to +/-`.10`;
- pan offset up to +/-`.08`.

Health/fracture scales those modulation readings but never steals, mutes or
quarantines a voice. `amount=0` skips all application and reproduces MA3
PCM bit for bit. After acceptance the factory amount becomes `.12`.

Experiment boundary: can a silent shared field make the cards remember and
react to a played gesture without becoming a hidden random LFO or a sixth
sound source?

`make exhibit-ma4` produces:

- field-off baseline and factory-amount A/B;
- repeated note-strike coordinate/state traces;
- held chord with low/mid/high aftertouch;
- per-card pitch/cutoff/drive/VCA/pan modulation tables;
- health histogram, rupture/recovery counts and clamp counters;
- field-only CPU cost and complete-chain cost;
- two-run signatures for every scenario.

Acceptance:

- amount zero equals the recorded MA3 signatures;
- same seed/events/rate yield identical field and PCM traces;
- state and modulation remain inside every stated bound under a maximum
  strike/pressure stress script;
- GFM contributes no sample of direct audio and cannot alter gate/allocator
  state;
- low/mid/high gestures produce distinct card-control traces and audible
  PCM signatures;
- the operator can identify coherent gesture memory in the A/B without a
  level-only clue.

### MA5 — consolidation and promotion gate

No new synthesis concept lands. Consolidate:

- full-chain per-stage cost at factory and maximum controls;
- `sizeof(ma_synth)` and per-subsystem state inventory;
- 44.1, 48, 96 and 192 kHz safety/guard sweeps;
- full MIDI compass, dense five-note performance and pathological
  controller-rate stress;
- optimized core undefined-symbol audit;
- 30-minute reference-host and Raspberry Pi 3B live soaks;
- docs/code/control/default truth audit;
- final factory render set and listening record.

The Raspberry Pi measurement, not a host projection, closes the target
budget. If no Pi-class target is available, MA5 remains explicitly open;
host success cannot close it.

MA5 then records three independent promotion decisions. None is silently
the next implementation task:

1. BCS experiment: continuous stable -> edge -> subharmonic -> recovery
   coordinate inside filter/cross-mod feedback, never the donor scenario
   player or a parallel oscillator layer.
2. Orbita experiment: VCO2/AUX resonance capture, hysteretic escape and
   breakup, offline before engine integration.
3. Kosava experiment: one global gust CV plus per-card lock-in and chord
   ignition, offline before engine integration.

Each candidate needs its own backlog and v0.1 evidence. A candidate is kept
only if it is audibly distinct at matched peak, deterministic, bounded,
exactly bypassable, within an assigned cost budget and judged to deepen the
analog organism rather than sound appended. Orbita and Kosava are not both
scheduled in advance; the operator chooses after their offline evidence.

## Explicitly out of scope through MA5

- a literal clone or revision selector for the 1978 instrument;
- patch schema, preset browser or persistence;
- Mamut spectral/additive sources or filter-model menu;
- PM, AM, ring modulation and generic source cross-mix modes;
- direct GFM audio, GFM program selector or INSPECT UI;
- BCS, Orbita or Kosava production integration;
- Lavina, Brazda or Mraz implementation;
- MIDI 2.0 UMP transport, MPE zones or sample-accurate hosted scheduling;
- a generic instrument framework or cross-repo runtime dependency;
- editing the organ or EP implementation to share speculative helpers.

Per-note state is reserved so future MIDI 2.0/MPE transport has a landing
zone; transport does not define the DSP model.

## Risks and fixed mitigations

- **MA1 breadth.** It is intentionally a complete one-voice proof rather
  than a source-only demo. Keep one public milestone/evidence document but
  execute source, filter and identity as named internal sub-gates; do not
  start the next sub-gate with a failing previous one.
- **Aliasing under sync/cross-mod.** Ordinary PolyBLEP success is
  insufficient. The MA1 hard-sync and cross-mod table is mandatory and has
  a 20 dB improvement floor against naive forms.
- **Filter CPU/stability.** Oversampling and solver counts are fixed before
  code. If the target budget fails, stop and record the measured failure;
  do not silently lower quality. A changed ratio/iteration count requires
  a new backlog decision and new A/B.
- **Full MIDI compass.** Sources mute at explicit numerical guards; they do
  not clamp to a wrong pitch. Every sample rate gets a boundary test.
- **FX state size.** Fixed maximum-rate buffers can dominate the struct.
  The <1 MiB assertion and static live allocation make this visible.
- **Feature soup.** Only Mozaik and GFM are committed Mamut worlds. Other
  donor features remain named exclusions or gated experiments.
- **Fake analog.** Character is tag-seeded and bounded at physical sites;
  no generic random drift or one final warmth knob substitutes for the
  distributed model.
- **Evidence inflation.** Metrics, smoke success, real-target timing and
  listening verdicts are reported separately. None is renamed as another.

## Status register

| Item | Backlog status | Earliest action |
| --- | --- | --- |
| Analog/Mamut identity contract | committed | MA0 |
| Mozaik AUX | implemented and accepted in closed MA1 | MA1 |
| Five-card analog body | committed | MA2 |
| Stereo + Mamut FX | committed | MA3 |
| Hidden GFM field | committed; donor field implementation exists but insertion differs | MA4 |
| Per-note expression state | committed internal landing zone | MA3; no new transport |
| BCS | candidate; donor playable, listening pending | MA5 promotion decision |
| Orbita | candidate; donor plan only | post-MA5 offline backlog if selected |
| Kosava | candidate; donor plan only | post-MA5 offline backlog if selected |
| Mraz | research priority among wild concepts | unscheduled |
| Lavina | research | unscheduled |
| Brazda | research; recall/persistence conflict unresolved | unscheduled |

## References

Local donor truth, read from the canonical sibling checkout when a slice
starts and pinned to its exact commit in that slice's evidence document:

- `mamut-sint-sw/docs/dsp/control-identity-math.md`
- `mamut-sint-sw/docs/dsp/masnoca.md`
- `mamut-sint-sw/docs/dsp/mozaik-v0.1-quasicrystal-osc-evidence.md`
- `mamut-sint-sw/docs/dsp/mozaik-v0.2-voice-source-evidence.md`
- `mamut-sint-sw/docs/dsp/gfm-performance-control-contract.md`
- `mamut-sint-sw/docs/dsp/gfm-v2.1-note-strike-excitation-evidence.md`
- `mamut-sint-sw/docs/dsp/gfm-v2.2-stereo-probe-evidence.md`
- `mamut-sint-sw/docs/dsp/gfm-v2.3-inspect-field-view-evidence.md`
- `mamut-sint-sw/docs/dsp/bcs-v1.2-playable-midi-layer-evidence.md`
- `mamut-sint-sw/docs/EPM1_BACKLOG_SET4_MIDI2_UMP_EXPRESSIVENESS.md`
- `mamut-sint-sw/docs/EPM1_BACKLOG_SET5_ORBITA_MOZAIK_KOSAVA.md`
- `mamut-sint-sw/docs/dsp/gfm-wild-concepts.md`
- `mamut-sint-sw/docs/dsp/implementation-language-strategy.md`

Filter and oscillator discretization references remain the standard
primary literature named by the former backlog: Stilson/Smith, Huovilainen,
Valimaki/Huovilainen, D'Angelo/Valimaki, Zavalishin, Brandt and the
antiderivative-antialiasing literature. MA0 records exact editions and
sections beside the constants they support. Circuit and service documents
support physical ranges; they do not reinstate clone fidelity as the goal.
