# Mamut Analog development journal

Started: 2026-08-20. This is the live execution record for
`docs/analog-backlog.md`. The backlog owns product and acceptance decisions;
this journal owns task order, current status, validation results and short
implementation notes.

Status vocabulary: `queued`, `in progress`, `blocked`, `done`. A public
milestone remains open until every task in its gate is done, even when one of
its internal sub-gates already has working code.

## Current position

- Active milestone: **MA3 — first playable, stereo and effects**.
- Active task: none. **MA2-6 — integrated MA2 evidence and closure** is done;
  evidence is in `docs/ma2-6-evidence.md`.
- Last green aggregate run: GCC, Clang and ASan/UBSan/
  float-cast-overflow, 2026-09-08, on MA2-6: core `111378`, hosted `116`,
  MIDI map `22`, architecture `227`, character `8700`, stereo `2611`;
  zero failures. Both compiler core-symbol audits pass.
- Donor pin: `mamut-sint-sw` commit
  `d7672912706731b73839d1fc25801669450fd0f1`, clean working tree when read.
- Core implementation status: MA0, MA1, MA2-1 through MA2-6, MA2-DIG,
  MA2-BCS and MA2-PERF are closed.

## MA0 task ledger

| ID | Status | Deliverable and closing check |
| --- | --- | --- |
| MA0-1 | done | Record the frozen organ/EP baseline, donor commit and repository boundary. |
| MA0-2 | done | Add one hosted C derivation program for note/control tables and numeric approximation error; generated output is review input, never a build dependency. |
| MA0-3 | done | Pin oscillator, sync, cross-mod and Mozaik contracts; capture donor Q32 word/phason golden vectors at the pinned commit. |
| MA0-4 | done | Pin source gain staging, pressure, 2x ladder, prewarp polynomial, seven-tap halfband, resonance and envelope contracts with full-domain error tables. |
| MA0-5 | done | Pin direct/MIDI ranges, zero-frame identity deltas, character, stereo/body/FX capacities, factory sound and future MA4 GFM constants. |
| MA0-6 | done | Audit every MA1 number for an owner; run the derivation self-check, `make test`, Clang tests and `git diff --check`; publish the MA0 gate verdict. |

## MA1 internal task ledger

MA1 is one public one-voice vertical slice, but it is executed through these
ordered internal gates. A later task does not start over a failing earlier
one.

| ID | Status | Depends on | Deliverable and closing check |
| --- | --- | --- | --- |
| MA1-1 | done | MA0 | `mamutanalog.h`, pinned tables, sanitizers and fixed one-voice state; freestanding symbol audit green. |
| MA1-2 | done | MA1-1 | Two mixed PolyBLEP VCOs, bandlimited triangle, noise, hard sync and bounded VCO2-to-VCO1 cross-mod; ordinary/sync/cross-mod alias tables meet the 20 dB gate. |
| MA1-3 | done | MA1-2 | Per-voice Mozaik AUX with tile-boundary phason latch; integer traces equal MA0 donor vectors and mix zero is bit-identical. |
| MA1-4 | done | MA1-3 | Normalized mixer, exact-bypass pressure and 2x nonlinear four-pole ladder; cutoff/self-oscillation/stability gates green. |
| MA1-5 | done | MA1-4 | Filter and amp RC ADSRs plus VCA; retrigger/release/epsilon behavior and hostile sweeps green. |
| MA1-AUD | done | MA1-5 | Hosted deterministic one-voice listening reel; three checked WAVs expose the landed MA1-5 path without changing the core or closing a later MA1 gate. |
| MA1-6 | done | MA1-5 | Linear five-macro identity resolver, zero-frame deltas, performance overlays and 6 ms smoothers; zero macros are bit-identical. |
| MA1-6-AUD | done | MA1-6 | Ten deterministic reference/macro/performance WAVs expose every MA1-6 identity and expression route without changing the core. |
| MA1-6P | done | MA1-6 | VCO1 sine, compiled Tepih/Lead patch selection and deterministic listening evidence. |
| MA1-6R | done | MA1-6P | Shared enriched Mamut sine, Dubina, concrete patch files and all-C Patchlab; spectral, round-trip, headless and live-null gates green. |
| MA1-7 | done | MA1-6 | Centered dual-mono output, body bypass, DC block, safety diagnostics and deterministic signatures. |
| MA1-8 | done | MA1-7 | Operator accepted the registered long-form render and closed MA1 without additional evidence writing or measurement. |

## MA2 internal task ledger

| ID | Status | Depends on | Deliverable and closing check |
| --- | --- | --- | --- |
| MA2-1 | done | MA1 | Five caller-owned `ma_synth` cards, explicit ownership phases, round-robin idle search, oldest-age steal and oldest-held repeated-note release. |
| MA2-2 | done | MA2-1 | Sustain, pedal-up release and panic ownership transitions. |
| MA2-3 | done | MA2-2 | LFO, glide and panic-bounded unison enter/play/leave behavior. |
| MA2-DIG | done | MA2-3 | Eight-family mipmapped Raster oscillator, spectral morph/warp, two patches, Patchlab controls and deterministic audition. |
| MA2-BCS | done | MA2-DIG | Feedback-only Hopf/Duffing coordinate, exact bypass, Granica patch and deterministic audition. |
| MA2-PERF | done | MA2-BCS | Five-card cost referee, pinned pre-change PCM and removal of redundant source work; evidence in `ma2-perf-evidence.md`. |
| MA2-4 | done | MA2-3 | Fixed-seed per-card character with an exact character-zero baseline; `ma2-4-character-evidence.md`. |
| MA2-5 | done | MA2-4 | Deterministic card pan and shared mid/side body; `ma2-5-stereo-evidence.md`. |
| MA2-6 | done | MA2-5 | Allocator, compass, stereo, character, cost and listening exhibit close the public MA2 gate on the x86-64 Linux target; Pi soak removed from scope and operator listening accepted. |

## Decision log

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-09-08 | Close MA2-6 on the x86-64 Linux host; remove Raspberry Pi from target scope and accept the operator listening verdict. | GCC/Clang/sanitizer gates, deterministic MA2-4/5 listening artifacts and host benchmarks pass; the remaining host budget warning is recorded as follow-up work rather than a release blocker. |
| 2026-09-06 | Close MA2-5 with a raw-card stereo entry point, unnormalized sum and one shared body. | Center-card MA1 and nine older PCM anchors stay exact. Pan, crossfeed, mono transitions and shared-body routing pass; single-timbre shared controls belong to card 0. |
| 2026-09-06 | Close MA2-4 with bank character `.20`, independent A/D/R time biases and a continuous 32-sample walk clock. | Physical calibration survives note and patch events; exact zero PCM, numeric routing probes and deterministic listening evidence pass. Sustain levels remain authored. |
| 2026-09-05 | Insert MA2-PERF before MA2-4; preserve PCM and the existing 8x/2x topology. | The operator accepted early profiling after review exposed a thin host rendering budget. Card character remains the next functional slice. |
| 2026-08-20 | Keep the public milestone as one MA1 slice but expose eight internal gates in this journal. | The backlog requires a complete voice while also requiring source/filter/identity failures to stop later work. |
| 2026-08-20 | Track this journal in Git instead of using the ignored `docs/ep-journal.md` pattern. | MA0/MA1 status and validation provenance are part of the handoff, not disposable listening notes. |
| 2026-08-20 | Do not edit organ or EP core translation units. | Their frozen signatures are the regression boundary for the third line. |
| 2026-08-20 | Keep donor inspection read-only and all tonewheel91 tooling and implementation in C23. | The target repo is a C instrument; cross-language helper artifacts obscure that boundary and buy nothing. |
| 2026-08-20 | Run sharp-edge VCO kernels at 4x, then decimate into the existing 2x mixer/filter boundary with the pinned halfband. | 1x yielded 7.38–17.74 dB and the 2x trial 12.57–20.20 dB; only one of eight 2x cases passed the mandatory 20 dB gate. |
| 2026-08-20 | Promote the ordinary and sync edge residual from donor quadratic to a C2 quintic PolyBLEP candidate. | Moving the quadratic kernel to 4x still yielded only 13.36–19.88 dB; the error must be reduced at the discontinuity, not hidden by the boundary filter. |
| 2026-08-20 | Accept an 8x Q48 VCO edge path, two 31-tap boundaries into the 2x ladder, and an eight-substep C2 reset slew. | This is the first candidate to pass every pinned ordinary/sync/cross-mod case: 21.86–29.16 dB versus naive edges on both compilers. |
| 2026-08-25 | Render Mozaik once per public audio frame after the sharp-edge VCO decimation, then complete the pinned normalized source sum there. | Hann tiles need no VCO oversampling; keeping their Q32/tile clock at the public sample rate preserves the donor duration contract and leaves the MA1-2 edge referee isolated. |
| 2026-08-25 | At MA1-4, advance Mozaik in both 2x source phases, with its render rate doubled, and feed that native 2x bus directly into pressure and the ladder. Compile the historical MA1-2 alias referee with `MA_SOURCE_EVIDENCE`. | Doubling both Mozaik steps and its clock preserves tile duration and drift in wall time. The dedicated source-only build keeps later filter attenuation from being counted as oscillator anti-alias evidence. |
| 2026-08-25 | Anchor zero filter-envelope level at the direct cutoff, then apply the donor's `.78` positive envelope span and `.42` keytrack slope; add the pinned velocity-filter contribution before clamping the effective amount. | The frozen MA0 text pinned the RC and VCO routes but omitted the cutoff equation. This preserves direct-cutoff meaning, uses the donor's bounded musical slope and makes the velocity law executable without introducing a new depth constant. |
| 2026-08-26 | Insert MA1-AUD as a non-gating hosted exhibit between MA1-5 and MA1-6. | The landed source-to-VCA path is already audible; a deterministic WAV handoff obtains the first operator evidence without pulling live MIDI, final output conditioning or later product ownership forward. |
| 2026-08-26 | Snapshot every smoothed destination once per public frame and hold it across the 8x source and 2x filter work. | Identity and performance changes then have one unambiguous sample boundary without creating substep-rate control motion. |
| 2026-08-26 | Add channel ownership and ignored release-velocity accounting to the MA1 one-voice boundary, but leave sustain and panic semantics to MA2. | Matching poly pressure needs the pinned channel/note identity now; allocator, held/sustained ownership and panic transitions still belong to the five-card task. |
| 2026-08-26 | Promote the enriched Mamut sine to the shared VCO control aggregate and pin H2/H3/H5 rather than adding a waveform menu. | VCO2 can dominate Dubina while both oscillators retain one concrete, auditable waveform mixer and the existing anti-alias path. |
| 2026-08-26 | Supersede MAD8 with one concrete `ma_patch` value and a strict hosted `.mapatch` file; keep I/O and discovery out of the core. | Sound design now needs recall, but a generic registry/plugin framework would broaden the product boundary without helping this one instrument. |
| 2026-08-26 | Build Patchlab as one ANSI/termios and synchronous ALSA C loop, with headless modes that never open ALSA. | The current one-card voice needs an audible vertical tool; ncurses, a GUI framework, threads and callbacks are unnecessary ownership. |
| 2026-08-27 | Close MA1 without further evidence writing or measurement and proceed to MA2. | The operator accepted the registered 248-second listening take and made the milestone decision explicitly. |
| 2026-08-27 | Insert the ad hoc MA2-DIG Raster vertical slice without renumbering MA2. | The operator requested one audibly digital direction before returning to fixed-seed card character; exact zero bypass keeps the analog line's prior contract intact. |
| 2026-08-27 | Promote BCS early as an ad hoc feedback-only slice, without renumbering regular MA2 work. | The operator selected BCS from the later candidate list; retaining one continuous coordinate and forbidding direct audio preserves the intended experiment while avoiding the donor scenario player. |

## Entries

### 2026-08-20 — implementation start

- Read the active backlog, build graph, public core conventions, aggregate
  tests and core symbol guard.
- Read the donor Mozaik word/oscillator, bandlimited waveform, sync,
  identity, FX and GFM note-placement contracts at the pinned commit.
- Ran the pre-MA1 baseline: `make test` passed (`9410 + 77 + 22` checks,
  zero failures).
- Next: land the hosted MA0 derivation/check program and write its reviewed
  constants into `docs/ma-constants.md`.

### 2026-08-20 — MA0 gate closed

- Added `driver/derive_ma_constants.c` and the manual
  `make derive-ma-constants` target. It derives six 128-entry f32 tables,
  the cutoff prewarp, the exact seven-tap halfband and bounded pitch
  polynomial, and checks 15 Q32 Mozaik vectors.
- Added `docs/ma-constants.md`: every number needed by MA1 now has a donor,
  repository, derivation or explicit design owner; no TODO/TBD/open marker
  remains.
- Numeric audit on GCC and Clang: effective cutoff error `0.00834%`,
  halfband declared-band error `1.42424%`, pitch error `0.0001656` cent;
  verdict `PASS` on both compilers.
- Regression: `make test` and `make test-clang` each passed core `9410`,
  hosted `77`, MIDI map `22`, with zero failures and a clean undefined-core-
  symbol audit. `git diff --check` passed.
- Next: MA1-1 public C state, pinned runtime tables and primitive tests.

### 2026-08-20 — MA1-1 gate closed

- Added the public caller-owned `ma_synth` foundation, compiled factory
  state, all MA1 note/velocity tables, total hostile table lookups, ADSR
  sanitizers and the five-macro exact-zero identity state.
- Kept `ma_voice.o` out of both frozen product link graphs; only the
  aggregate core test and unresolved-symbol audit host it at this gate.
- GCC and Clang each pass `9692` core checks with zero failures and no
  unexpected hosted dependency in the combined freestanding core.
- Next: MA1-2 mixed VCO render state, sync/cross-mod and alias evidence.

### 2026-08-20 — MA1-2 gate closed

- Added two continuously mixed saw/pulse/triangle VCOs, bandlimited
  triangle integration, fixed interval/fine pitch, deterministic one-draw-
  per-frame noise, partial-to-hard sync and bounded VCO2-to-VCO1 cross-mod.
- The mandatory alias stop condition rejected the 1x, 2x and narrow 4x/8x
  candidates. The accepted Q48/8x C2 edge path and 31-tap boundaries pass
  all eight hosted FFT cases at `21.86..29.16 dB` improvement over naive
  edges. `make exhibit-ma1-osc` is the repeatable C referee.
- Public setter, note ownership, guard fade, sync-zero bit identity,
  deterministic noise timeline, bounds and hostile-input tests are in the
  aggregate suite. GCC and Clang each pass `82678 + 77 + 22` checks, zero
  failures, with clean freestanding symbol audits. The hosted derivation
  also passes on both compilers.
- Next: MA1-3 Q32 Mozaik word, tiles and boundary-latched controls.

### 2026-08-25 — MA1-3 gate closed

- Added the per-voice Q32 word, signed full-tile Hann source, fractional tile
  carry, slope detents, contrast, absolute boundary-latched phason, quadratic
  drift and the `20 Hz .. min(8 kHz, rate/8)` source guard. Note-on reseats
  the one-voice oscillator deterministically; the oscillator uses no RNG.
- The aggregate suite checks all 15 MA0 donor word/fraction vectors and the
  `0x5b5a -> 0x6b5b` boundary-phason golden. Early and late requests inside
  one tile render identically through their shared latch boundary.
- `mozaik.mix=0` branches around source-sum arithmetic and reproduces the
  recorded MA1-2 FNV-64 signature `9c74c61be71d53b9` while Mozaik state keeps
  advancing. The MA1-2 alias exhibit explicitly selects this baseline.
- GCC and Clang each pass core `95216`, hosted `77`, MIDI map `22`, with zero
  failures and clean freestanding symbol audits. The MA0 derivation and all
  eight MA1-2 alias cases also pass on both compilers.
- Next: MA1-4 normalized mixer pressure and the 2x nonlinear four-pole ladder.

### 2026-08-25 — MA1-4 gate closed

- Replaced the temporary source-only 2x-to-1x boundary with the normalized
  VCO/noise/Mozaik mix, literal-zero pressure bypass and a nonlinear
  four-stage TPT ladder running at two substeps per public frame. Each
  substep uses exactly two fixed feedback iterations over the same pre-step
  state, followed by the pinned seven-tap output halfband.
- Added the bounded cutoff/resonance/drive/pressure setter, f32 Chebyshev
  prewarp, the pinned drive and feedback laws, explicit four-stage state and
  deterministic non-finite reset counter. The source-evidence compile keeps
  the MA1-2 referee upstream of this new filter; its eight cases remain at
  `21.86..29.16 dB` improvement on GCC and Clang.
- Permanent tests recover the cutoff within the two-percent gate at all four
  supported rates, prove pressure zero bit-exact at the source boundary,
  measure bounded self-oscillation from C2 through C7 at
  `-4.15..+15.64` cents, and show strictly increasing RMS from resonance
  `.86` through `1.0`. Hostile setters, an injected NaN and maximum
  cutoff/resonance/drive/pressure sweeps remain finite with deterministic
  recovery and no unexpected resets.
- GCC and Clang each pass core `107243`, hosted `77`, MIDI map `22`, with zero
  failures and clean freestanding symbol audits. ASan/UBSan passes the same
  aggregate counts; the MA0 numeric audit reports `0.00834%` maximum
  effective-cutoff error; `git diff --check` passes.
- Next: MA1-5 filter/amp RC ADSRs and VCA. No MA1-5 code has started.

### 2026-08-25 — MA1-5 gate closed

- Added explicit idle/attack/decay/sustain/release state for the filter and
  amp envelopes. Both use the repository one-pole coefficient, the pinned
  60 dB time interpretation and a stage-entry relative epsilon; targets snap
  to literal one, sustain or zero before the next stage begins.
- NoteOn retriggers both envelopes from their current levels. Matching
  NoteOff enters ordinary release without muting the oscillator/filter
  state. The VCA is the direct filtered sample multiplied by amp-envelope
  level and the pinned velocity gain; pressure remains literal unity until
  MA1-6 owns its state and smoothing.
- The velocity-aware filter envelope drives the effective cutoff, VCO1
  pitch and both pinned pulse-width deltas. Direct keytrack uses the bounded
  donor slope. The effective amount and envelope level are calculated once
  per public frame and held across the oversampled source/filter work.
- Permanent tests close `10/20/30 ms` stages at `480/960/1440` frames at
  48 kHz, prove exact sustain/zero snaps, release and retrigger continuity,
  velocity-only VCA scaling, the 480 Hz full-depth VCO1 anchor, cutoff and
  keytrack anchors, hostile extremes at all supported rates and byte-exact
  repeated event scripts. The MA1-5 zero-Mozaik render signature is
  `48685bb104788f11`.
- GCC and Clang each pass core `111261`, hosted `77`, MIDI map `22`, with
  zero failures and clean freestanding symbol audits. ASan/UBSan passes the
  same aggregate counts; the MA0 derivation and all eight isolated MA1-2
  alias cases remain green on both compilers; `git diff --check` passes.
- Next: MA1-6 identity resolver, performance overlays and 6 ms smoothers.
  No MA1-6 code has started.

### 2026-08-26 — MA1-AUD interstitial exhibit closed

- Added `make audition-ma1-5`, a hosted 14-second listening script that
  renders the compiled factory voice, its exact Mozaik-off A/B and one
  explicitly stronger Mozaik/drive take. It uses only the public MA1-5 API
  and the existing WAV writer; the core and later MA1 ownership are unchanged.
- Each take is rendered twice and must be byte-identical, finite, nonzero,
  dual-mono and below full scale after the fixed `.5` monitoring gain. GCC and
  Clang agree on all three emitted PCM signatures: factory
  `ff6f374aa5f6d149`, analog-only `af9bcbea779b3359`, and Mozaik-focus
  `018d9ab2064a3fe1`.
- Raw peak/RMS/DC are `.245351/.053287/+.0070261`,
  `.235781/.054212/+.0013814`, and `.326470/.085680/+.0230711` respectively.
  The residual DC is reported rather than hidden because MA1-7 owns the core
  DC blocker.
- GCC and Clang each pass core `111261`, hosted `77`, MIDI map `22`, with zero
  failures and clean freestanding symbol audits. The audition path also passes
  ASan/UBSan/float-cast-overflow; `git diff --check` passes.
- Operator verdict: accepted as an audible MA1-5 handoff, described as an
  "odličan jeziv zvuk". The finer factory/Mozaik A/B questions remain open;
  no provisional control value was retuned from this first reaction.
- Next implementation gate remains MA1-6; no MA1-6 code has started.

### 2026-08-26 — MA1-6 identity, overlays and smoothers closed

- Added the complete caller-owned `ma_identity` zero/effective frames and the
  pinned linear Gravitacija, Bloom, Heat, Ruin and Swarm resolver. Every
  additive destination subtracts the stored zero frame and every
  multiplicative destination resolves to literal unity at zero macros.
- Routed identity into cutoff, resonance, filter drive/envelope/keytrack,
  sync, cross-mod, Mozaik mix/contrast/phason/drift and the MA1-7-owned
  body/width/crossfeed targets. Channel pressure temporarily overlays
  Gravitacija/Ruin and mod wheel overlays Bloom/Swarm without changing the
  stored base macros.
- Pinned the channel-aware one-voice event boundary. Pitch bend is a smoothed
  `+/-2` semitone contribution shared by both VCOs and Mozaik. Matching
  channel/note poly pressure has its own smoother and contributes a quarter
  octave of cutoff plus a `1.10` VCA multiplier at full pressure. Release
  velocity is accepted, ignored and counted. Sustain, allocator ownership and
  panic remain untouched for MA2.
- Added explicit named linear smoother state for every continuous MA1 render
  destination. At 48 kHz a target closes in exactly `288` frames and at high
  rates the common limit is `512`; the final frame snaps to the exact target.
  Envelope times retain their own RC coefficient rule and Mozaik phason keeps
  its tile-boundary latch. Literal Mozaik-off and mixer-pressure-off paths
  remain immediate exact bypasses.
- Permanent tests cover the full resolved frame and destination values,
  overlay/base ownership, hostile performance inputs, smoother length and
  exact target snap, channel/note matching, release-velocity accounting,
  pitch-bend tuning, poly-pressure cutoff/VCA routes and byte-exact zero-macro
  PCM. The MA1-5 zero-Mozaik anchor remains `48685bb104788f11`.
- GCC and Clang each pass core `111286`, hosted `77`, MIDI map `22`, with zero
  failures and clean freestanding symbol audits. ASan/UBSan/
  float-cast-overflow passes the same aggregate counts. The MA0 derivation and
  all eight alias cases pass on both compilers at `21.86..30.00 dB`; the
  factory and analog-only audition signatures remain `ff6f374aa5f6d149` and
  `af9bcbea779b3359`. `git diff --check` passes.
- Work stops here. MA1-7 output body, DC blocker and safety code has not
  started.

### 2026-08-26 — MA1-6 listening companion closed

- Added `make audition-ma1-6` and a fixed 9-second A/B script for one
  reference, all five individual macros, channel aftertouch, mod wheel,
  pitch bend and matching poly pressure. The common timeline makes the
  unchanged, active and returned-to-zero regions directly comparable.
- Each take renders twice, writes one stereo float WAV and must be finite,
  dual-mono, below the `.5` monitoring headroom limit, byte-identical on
  repeat and distinct from the reference for more than one second. All nine
  controlled takes pass; exact paths, timestamps and signatures are recorded
  in `docs/ma1-6-audition.md`.
- GCC, Clang and ASan/UBSan/float-cast-overflow builds agree on all ten PCM
  signatures. This task added hosted evidence only; `src/ma_voice.c` and
  `src/mamutanalog.h` did not change, and MA1-7 remains unstarted.

### 2026-08-26 — MA1-6P VCO1 sine and compiled patches closed

- Added one VCO1-only sine weight using the shared freestanding
  `tw_sin_turns` kernel. The existing VCO control aggregate remains the
  saw/pulse/triangle contract shared with VCO2; sine has its own clamped
  public setter and 6 ms smoother, so VCO2 did not acquire a hidden shape.
- Changed the intentional factory sound to Tepih by adding sine `.20` to the
  existing dark pad. Added the fixed Lead initialization patch with moderate
  sync/cross-mod, short envelopes and a nonzero Mamut identity. Patch choice
  exists only at initialization; there is no loader, registry, schema or
  persistence layer.
- The eight established alias cases remain green at `21.86..30.00 dB`
  reduction. Pure sine evidence at 44.1, 48, 96 and 192 kHz measures
  `-96.36`, `-96.56`, `-101.21` and `-95.89 dBc` outside the fundamental
  mask. Core tests now total `111299` checks with zero failures, including the
  bit-exact pre-sine PCM anchor when the VCO1 sine level is zero.
- Added `make audition-ma1-6p`. Tepih and Lead signatures are
  `b76c9c420a960925` and `fb26440d488b857d`; the matched sine-off/on pair is
  `c9df75040b17c751` / `6c1409f71d818069` and differs in `472767` frames.
  The previous MA1-5/MA1-6 factory signatures are historical anchors and are
  superseded by the documented sine-bearing factory renders.
- MA1-7 remains unstarted.

### 2026-08-26 — MA1-6R Mamut sine, patch bank and Patchlab closed

- Replaced the generic pure-sine contribution with the pinned Mamut source:
  a softly driven fundamental plus a `.07` phase-offset second harmonic and
  measured `.95591217` peak normalization. The same grouped control now
  belongs to both VCOs, including VCO2 preview, hard-sync residual and
  cross-mod routes.
- Extended the alias referee to both VCOs at 44.1, 48, 96 and 192 kHz. Every
  case pins H2/H3/H5 at `-24.00/-19.08/-36.67 dBc`; all out-of-contract
  energy is `-94.26..-101.08 dBc`. The eight pre-existing saw, pulse, sync
  and cross-mod cases remain green at `21.86..30.00 dB` reduction.
- Added one concrete, sanitized `ma_patch`, the Tepih/Lead/Dubina compiled
  bank and exact shipped file mirrors. The strict 45-field v1 parser rejects
  unknown, duplicate, missing, malformed, non-finite and out-of-domain input;
  its writer round-trips f32 exactly and commits through `fsync` plus rename.
- Added the all-C Patchlab: list/dump/render headless modes, deterministic WAV
  script, directory bank, ANSI/termios field editor, atomic save/reload,
  QWERTY notes, synchronous ALSA PCM, optional raw MIDI, expression and
  CC16..20 macro editing. Patch selection performs a full voice/DSP reset at
  a period boundary; individual edits use grouped setters and smoothers.
- The common reel signatures are Tepih `86bd2977cfdfda45`, Lead
  `c62c23f3766f6955`, Dubina `57b228c4e2a8de39`, sine-off
  `eb7d0c1253507751` and sine-on `ed3516126ed8d7e9`. Patchlab renders are
  `28787fba70a2e465`, `7fbb7fa6dc495b39` and `8629294ca611fe05`.
- GCC and Clang produce identical evidence and pass core `111301`, hosted
  `102` and MIDI map `22` checks. ASan/UBSan/float-cast-overflow passes the
  same suites plus both exhibits and Patchlab list/dump/render. GCC
  `-fanalyzer`, both core-symbol audits and `git diff --check` are clean.
  A pseudo-terminal live smoke through ALSA `null` loaded Dubina, ran the
  screen/audio clock, quit cleanly and reported zero xruns; physical hardware
  is intentionally left as an operator check.
- MA1-7 remains unstarted; no final body, stereo, DC-block or safety claim is
  made by this interstitial slice.

### 2026-08-26 — Blade Runner Blues hosted audition pass

- Added `driver/exhibit_ma_blues.c` as a concrete, all-C listening companion
  for the Mamut Analog line: a slow F-sharp-minor synth-blues study with
  fixed Tepih/Dubina beds, long Lead phrases, expressive pitch/pressure
  gestures and bounded stereo reverb.
- Removed the bell layer after listening review. The new Lead overlay has no
  pulse, noise, sync, crossmod or meaningful drive; sine and triangle carry
  the continuous tone, with 220 ms attack and 7 s release. Factory patch
  files remain unchanged except for repairing the stale invalid Lead mirror
  so the hosted round-trip gate can pass.
- The exact complete Blues MIDI was not freely available during the search;
  the output is therefore labelled an interpretation, not a transcription.
  The executable writes a 180-second float WAV and performs a two-run FNV
  check. Final evidence: 79 notes, 13-voice peak, 0 steals, peak `.825503`,
  FNV `c45ddcf0f8b0a571`, WAV SHA-256
  `9b762468972c0fe925acc27c75cc668d34b9f7f76c2634d395877fbc8dbb91fb`.
- MA1-7/MA3 remain unstarted; this is a hosted audition, not the product
  allocator or a live-device claim.
## 2026-08-26 — Blade Runner Blues dark-register lead pass

- Lead arrangement was reduced to nine long notes in the low register (MIDI 54–64), leaving long spans for Tepih and Dubina alone.
- Lead timbre now leans toward the bed voices: triangle/sine dominant, dark 880 Hz filter, slow 520 ms attack and 9 s release.
- A restrained amount of oscillator sync (`.055`), crossmod (`.035`) and filter drive (`.085`) adds movement without restoring the former bright or dirty character.
- Pitch entry and vibrato were softened and delayed to keep the lead continuous and recessed.

### 2026-08-26 — MA1-7 output body, DC block and safety closed

- Added the centered one-card output path. Positive body drive reuses the
  shared `tw_drive` state at the pinned `4x` input / `.25x` output operating
  point; the smoothed identity body-load ratio multiplies its input. Literal
  zero drive is an exact branch that ignores load and leaves body state
  untouched.
- Added independent 10 Hz stereo DC trackers with counted `1e-9` state
  flushes, then master level and the pinned `.98..1.0` polynomial safety
  curve. Non-finite channel samples become zero. Persistent diagnostics own
  sanitizations, knee hits, tiny flushes, pre/post peaks and maximum positive
  reduction.
- Added explicit pre/post-body taps so previous MA1 stage signatures remain
  testable after final output conditioning. The one-card production result is
  still bit-identical dual mono.
- Added `make audition-ma1-7`. Body-bypass, direct-body and identity-load
  signatures are `9d74d51119c9a551`, `124cc7e5fd4e7181` and
  `d32e24b085a54d15`; both fresh renders and diagnostics match byte for byte.
- Permanent tests cover the safety polynomial and slopes, output ordering,
  body/load/state bypass, diagnostics, sanitization and 44.1/48/96/192 kHz
  hostile sweeps. GCC, Clang and ASan/UBSan/float-cast-overflow pass core
  `111320`, hosted `102` and MIDI map `22`; both core-symbol audits,
  GCC `-fanalyzer` and `git diff --check` are clean. MA1-8 remains queued and
  still owns the public evidence and human listening gate.

### 2026-08-27 — MA1 operator closure and MA2-1 fixed-card allocator

- Recorded the operator decision to close MA1 after accepting the registered
  248-second listening take. No additional MA1 evidence document or cost
  measurement is queued.
- Added `ma_card_bank`: five fixed caller-owned `ma_synth` cards, a round-robin
  cursor, monotonically increasing assignment ages and explicit idle, held and
  released ownership phases. No allocation or audio-loop heap use was added.
- Idle search begins at the cursor; exhaustion steals the oldest assigned card.
  Repeated NoteOn events may occupy separate cards, and NoteOff releases the
  oldest still-held matching channel/note instance. Released cards retain their
  age until their envelopes finish and participate in the same steal order.
- `ma_card_bank_tick` advances all five cards and returns their frames in fixed
  slot order. Sustain, panic, unison, character, pan and shared summing remain
  outside MA2-1.
- GCC, Clang and ASan/UBSan/float-cast-overflow pass core `111336`, hosted
  `109`, MIDI map `22` and architecture `227`; both core-symbol audits pass.
  The MA2 bank translation unit is analyzer-clean. The full analyzer build
  retains a warning in unchanged `driver/ma_architecture_render.c`.

### 2026-08-27 — MA2-2 sustain, release and panic closed

- Added the sustained ownership phase and bank-level pedal state. NoteOff
  under a down pedal removes the oldest matching held instance from key
  ownership without starting its envelopes' release.
- Pedal-up starts the ordinary release on every sustained card while retaining
  assignment ages and cursor order. Held, sustained and already-released cards
  remain peers in the oldest-age steal rule.
- Added the bank panic transition: sustain clears, held and sustained cards
  enter ordinary release, already-released cards continue, and oscillator,
  filter and output state are not reset. Effect tails therefore remain able to
  decay when the later shared stages land.
- Tests cover repeated notes under sustain, unmatched NoteOff accounting,
  pedal idempotence, sustained-card stealing, mixed-phase panic, preserved
  ages/cursor/DSP phase and deterministic decay back to idle.
- GCC, Clang and ASan/UBSan/float-cast-overflow pass core `111342`, hosted
  `109`, MIDI map `22` and architecture `227`; both core-symbol audits and the
  focused GCC analyzer pass, and `git diff --check` is clean.

### 2026-08-27 — Blade Runner Blues Panic hosted audition

- Added a separate `driver/exhibit_ma_blues_panic.c`; the accepted MA1 Blues
  source and WAV were not changed. Three five-card hosted overdub banks retain
  Tepih, Dubina and dark Lead colours while exercising the landed MA2-2
  allocator, repeated-note, sustain, steal and panic paths.
- The three 72-second arcs end in ordinary-release panic transitions, leaving
  envelope and reverb tails rather than hard-zeroing DSP state. The hosted
  role mix is not the future MA2 card-pan/shared-body topology.
- Two complete passes are PCM-identical at FNV64 `43a8af4dd33b877e`: 88
  notes, 12-card peak, 39 steals, three panic events, peak `.126336`, RMS
  `.016443`, finite with headroom. WAV SHA-256 is
  `8b97c6c459df249f17c45bc87b0b5a2008a720d10ce2a46c2e0a024b9dc6523c`.

### 2026-08-27 — MA2-3 glide, LFO and unison closed

- Added per-card linear glide in MIDI-note space. The first assigned pitch
  starts directly, later notes glide from the current pitch, mid-glide
  retargeting does not jump, and the last sample lands exactly on target.
- Added the bounded sine voice LFO with smoothed depth/rate and a one-semitone
  maximum pitch span. Exact depth zero rejoins the prior PCM path and leaves
  phase state untouched.
- Added the bank unison mode machine. Enter and leave perform the existing
  ordinary-release panic transition; unison NoteOn assigns all five slots in
  fixed order to the newest note, and NoteOff/sustain operate on all five.
  Leaving restores the ordinary allocator without migrating ownership.
- Tests cover the exact glide/LFO-off PCM baseline, first-note and retarget
  rules, hostile domains, LFO phase bypass, unison enter/play/replace/release/
  leave and the post-unison allocator state.
- GCC and Clang pass core `111361`, hosted `109`, MIDI map `22` and architecture
  `227`; ASan/UBSan/float-cast-overflow pass with the repository's required
  leak check disable, and both core-symbol audits pass.

### 2026-08-27 — MA2-DIG Raster oscillator and spectral morph closed

- Added one explicitly digital Raster source per fixed card: eight immutable
  phase-aligned families, seven harmonic mip levels, linear sample/family
  interpolation, continuous adjacent-mip crossfade and bounded phase warp.
- Literal mix zero executes the prior source-mix branch byte for byte and
  leaves Raster phase/mip state untouched. Positive mix enters the existing
  normalized 2x source bus before pressure and the ladder; the three controls
  use the common 6 ms smoother.
- Added the pure Raster and hybrid Prizma compiled patches and exact file
  mirrors. Patchlab exposes all three controls. The strict 48-field file is
  version 2; version-1 45-field input remains readable with Raster bypassed.
- Added the 56-second two-bank `make audition-ma2-dig` exhibit. Both passes
  equal FNV `f70e00dd1cf6d64a`; peak is `.522408`, RMS `.097373`, all samples
  are finite and no frame clips. The WAV SHA-256 is
  `a1687f4a59a8a84bb6bef7599fa2fb5da65c1a43de41e100d9659a131e7a6142`.
- Permanent tests cover exact bypass, spectral-position distinction,
  determinism, mip selection, hostile controls, factory roles and full-rate/
  note-domain finiteness. MA2-4 remains the next queued task.
- GCC, Clang and ASan/UBSan/float-cast-overflow pass core `111369`, hosted
  `112`, MIDI map `22` and architecture `227`; both core-symbol audits, the
  focused GCC analyzer and `git diff --check` pass.

### 2026-08-27 — MA2-BCS nonlinear feedback coordinate closed

- Ported the donor's four-state Hopf/Duffing model at the pinned commit into
  each fixed card, with four RK4 substeps per public frame. One continuous
  coordinate interpolates stable, edge, subharmonic and recovery coefficient
  landmarks; no scenario enum, timeline or hosted donor layer entered core.
- The bounded BCS readout changes existing cross-mod and ladder-feedback
  controls, and the previous ladder state weakly excites Duffing. A permanent
  source-null test proves that nonzero BCS state contributes no direct PCM.
- Literal amount zero skips integration and destination arithmetic, preserves
  nonlinear state and reproduces the pre-slice render byte for byte. Unsafe
  state resets to the pinned seed; hostile full-note sweeps at 44.1, 48, 96
  and 192 kHz remain finite and bounded.
- Added the pulse-free dark Granica compiled/file patch and Patchlab controls.
  Strict `.mapatch` version 3 has 50 fields; v1 supplies Raster/BCS zeros and
  v2 supplies BCS zeros.
- Added `make audition-ma2-bcs`. Its 56-second Granica-over-Prizma render is
  identical in both passes at FNV `b0ce05ae487a1495`: peak `.276843`, RMS
  `.058201`, BCS maximum state `1.165633`, zero safety resets, non-finite
  frames or clips. WAV SHA-256 is
  `459a6be8d3c1e6af002c4573cd02a59bcd8754b398f83f06263bb2c4de0214ba`.
- GCC, Clang and ASan/UBSan/float-cast-overflow pass core `111378`, hosted
  `116`, MIDI map `22` and architecture `227`; both compiler core-symbol
  audits, the focused GCC analyzer and `git diff --check` pass. MA2-4 remains
  the next queued task; operator listening of Granica remains an explicit
  musical verdict rather than an automated claim.

### 2026-09-05 — MA2-PERF five-card cost slice closed

- Added `make bench-ma`: five fixed bank scenarios, separate thread CPU and
  elapsed timing, two fresh passes, finite PCM checks and signatures captured
  before core changes. Timed rendering excludes event dispatch and hashing.
- A separate gprof build identified the source path as a significant cost.
  Reused the VCO2 preview already computed for cross-mod and prepared invariant
  source controls once per public frame rather than eight times. Kept the
  8x source, 2x ladder, arithmetic order, continuous idle cards and public
  state layout; no factory patch or DSP algorithm changed.
- The affinity-pinned i7-4600U/GCC comparison observed 17–20% lower mean CPU
  time in active scenarios. Five-note Tepih moved from 1404.64 to 1117.66 us
  per 128-frame block; Granica with BCS from 1855.27 to 1491.16 us. Both
  passes and p99 observations are recorded in `docs/ma2-perf-evidence.md`.
  The half-deadline budget and Raspberry Pi acceptance remain open.
- GCC/Clang aggregate tests and optimized core-symbol audits pass. Clang
  ASan/UBSan/float-cast-overflow passes the aggregate suite and the benchmark.
  Counts remain core `111378`, hosted `116`, MIDI map `22`, architecture
  `227`. All five benchmark signatures remain identical on both compilers
  and under sanitizers; all 16 oscillator alias/harmonic cases pass on GCC
  and Clang. `git diff --check` passes.
- Next functional task: MA2-4 fixed-seed card character. No MA2-4 code has
  started; this performance slice does not close public MA2 or MA5.

### 2026-09-06 — MA2-4: physical-card character

- Closed all six implementation tasks in `docs/ma2-4-character-evidence.md`.
  Ten independent fixed-seed draws per card control common/additional VCO
  tuning, cutoff, six envelope times and VCA trim. Sustain levels stay fixed.
- Added bank character control with factory `.20`, immediate zero bypass
  and the existing positive-control ramp. The bounded walk takes one draw
  every 32 samples and interpolates over the following 32. Physical state
  continues while idle or bypassed and survives steals, sustain, panic,
  unison and patch replacement. Standalone MA1 voices retain zero character.
- All five pre-character benchmark signatures remain exact; four positive
  character signatures are pinned. Six 8-second dry chord/unison WAVs at
  `0/.20/1` repeat exactly and agree across GCC and Clang. Listening
  preference remains the operator's decision.
- GCC, Clang and ASan/UBSan/float-cast-overflow aggregate tests pass:
  core `111378`, hosted `116`, MIDI map `22`, architecture `227`, character
  `8700`. Both freestanding audits and all 16 source alias/harmonic cases
  pass. Final fixed state is 1792 bytes/card and 9064 bytes/bank on x86-64.
- Final affinity-pinned host means at `.20` are 1139.43/1136.26 us for
  Tepih five and 1520.48/1524.81 us for Granica BCS per 128 frames. Full
  timing conditions and p99 values are in the evidence document. The
  half-deadline/Pi cost gate remains open. Next functional task is MA2-5.

### 2026-09-06 — MA2-5: card pan and shared body

- Closed the five-task ledger in `docs/ma2-5-stereo-evidence.md`.
  `ma_card_bank_tick_stereo` renders raw post-VCA cards, applies deterministic
  slot pans, sums without voice-count normalization and processes one shared
  mid body before stereo DC block, master and safety. Historical per-card
  rendering remains available for existing exhibits and PCM references.
- Added the bank output setter, cached equal-power coefficients, smoothed
  identity pan dispersion and mid-preserving crossfeed. Direct width zero
  overrides identity widening and merges DC history for immediate exact
  dual mono. Panic and unison preserve shared chassis state.
- All 2611 new stereo checks and the prior regression battery pass on GCC,
  Clang and ASan/UBSan/float-cast-overflow. Both optimized core-symbol audits
  and all 16 source alias/harmonic cases pass. A lone center card at zero
  character matches complete MA1 PCM at all four supported test rates.
- Twelve benchmark anchors and eight repeat-rendered WAV hashes agree
  across GCC and Clang. The listening set covers one/three/five cards,
  mono/stereo and shared-body bypass/drive, without gain normalization or
  effects. Listening preference remains an operator decision.
- Fixed state is 1808 bytes/card and 9328 bytes/bank on x86-64. Final pinned
  host stereo means per 128 frames are 1122.05/1139.53 us for Tepih and
  1504.45/1489.68 us for Granica. Tail timings and Granica mean still miss
  the half-deadline goal; Raspberry Pi acceptance is open. MA2-6 is next.

### 2026-09-06 — Hurt industrial arrangement with ep73

- Added a separate hosted arrangement around the local Hurt MIDI: Mamut
  kick, noise backbeat, Raster metal, syncopated bass, harmonic pads and
  EP voicings/answers. Source pluck, high melody and late guitar notes
  retain their timing. The arrangement follows half-bar source harmony
  and explicit phrase boundaries, with a breakdown and a final withdrawal.
- Pad, texture and metal use real MA2 shared stereo banks and fixed physical
  character; phrase energy controls width and Raster/BCS. The existing
  Noir driver remains the comparison reference. Public core APIs are unchanged.
- Aligned tonal/rhythm/EP stems own their linear reverb returns. A hosted
  Python tool validates their sum, prepares constant-gain listening and
  RMS-matched A/B files, and records measured levels and hashes.
- Task ledger, commands, validation and listening evidence:
  `docs/ma-hurt-industrial.md`. This offline arrangement does not close
  MA2-6 or the live performance budget.

### 2026-09-07 — Hurt listening correction: dark Mamut only

- User preferred the original Noir sound and rejected EP and metallic tones.
  Reworked the hosted take as `exhibit_ma_hurt_dark`, removing EP linkage,
  events and stem, and deleting the metal bank/sequencer.
- All patches use softened analog waveforms, low cutoff, gentle envelopes
  and no Raster/Mozaik/noise/sync/crossmod. Identity macros are zeroed too,
  so Bloom cannot silently add a Mozaik source back into the mix.
- Channel 9 follows low harmonic pitches, the melody returns to a slow pad
  one octave lower, and a soft tonal pulse replaces the noisy backbeat.
  Both source stems receive stronger, damped reverb.
- Current commands, task ledger and evidence: `docs/ma-hurt-dark.md`.

### 2026-09-07 — Hurt revision: calmer bass and wet organ

- Kept the praised dark bass patch and harmonic roots, halved its attacks
  from 256 to 128, lengthened gates and removed final-chorus octave jumps.
- Moved the 125 existing high-melody notes to a warm `508300000` organ
  registration with .11 drive, slow rotary, low-pass and its own damped
  reverb stem. The rest retains the dark Mamut palette, without EP.
- Inspected the public Hal Leonard vocal preview and placed six short,
  rhythmically changed verse-motif answers on Mamut: 21 added notes, not
  a continuous vocal doubling. Source, timing ledger, reproduction and
  delivery evidence: `docs/ma-hurt-organ.md`.

### 2026-09-07 — Hurt full vocal: exhibit-only handoff

- User accepted the organ take and requested the full vocal melody on a
  slightly dirtier Mamut. Found and inspected a separate public Songparts
  MIDI: all 200 vocal notes fit the backing after an eight-bar offset,
  with original pitches, velocities, rests and gates retained.
- Replaced the sparse answers, kept the approved bass and main organ,
  and added a separate slow, diffuse organ harmony in bars 64–67.
  Vocal and harmony have their own reverb stems and preparation options.
- Builds, static analysis and schedule checks pass; no new audio render
  was run, as explicitly requested. The next model's source provenance,
  commands, balance checkpoints and task ledger are in `docs/ma-hurt-vocal.md`.

### 2026-09-07 — Hurt for two manuals and pedals

- Added the separate organ-only `exhibit_organ_hurt`: one right-hand melody
  combines 200 vocal attacks with 48 instrumental replies in rests; compact
  left-hand voicings retain common tones; monophonic pedals follow roots.
- Pedals address low tonewheel frequencies directly through the existing
  generator API, avoiding the manual's low-note octave foldback. Manuals
  use fixed warm registrations, slow rotary and a shared expression curve.
- Added three-part MIDI export and aligned right/left/pedal stems with damped
  reverb. Full-score hand-span/polyphony checks, MIDI roundtrip and a short
  audio check accompany the new exhibit. No Mamut/EP linkage or core changes.
- Commands, performance assumptions and validation: `docs/organ-hurt.md`.
- Completed the full 253-second take and listening preparation. Right, left
  and pedal stems sum exactly; no clipping/nonfinite samples. Listening
  peak is -3 dBFS and RMS -19.67342 dBFS. The complete three-part MIDI is
  exported alongside the audio under `build/organ_hurt_full`.
