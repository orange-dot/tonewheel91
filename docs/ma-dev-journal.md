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
- Active task: **MA3-PERF**, updated 2026-11-07. The interim 8-slot target
  (operator decision 2026-11-06; core default stays 32) is met in hosted
  measurement by bit-exact AVX2 vector lanes (`engine` AUTO). The vector
  engine now covers the oscillator block, decimators, ladder, BCS, front-end
  math and raster. Its worst 8-slot case is 1.737 M cycles per 128-frame
  block (Prizma, BCS on), with 0 deadline misses
  (`docs/ma3-raster-bcs-lanes-20261107.md`). 32 slots remain open; MA3-LIVE
  owns isolation, priority and the no-xrun gate.
- Latest slice: `src/ma_voice.c` split into voice modules, move-only, PCM
  unchanged (2026-11-07 entry below; `b6c0426` … `30cd135`). Files:
  `ma_voice.c` (scalar tick, oscillators, decimators, raster, BCS,
  per-sample controls), `ma_voice_impl.h` (private constants, types,
  `static inline` helpers), `ma_controls.c`, `ma_patches.c`, `ma_lanes.c`,
  `ma_filter.c`, and `ma_raster_table.c` (one raster table, below).
  `ma_sources.c` was measured and rejected.
- No next DSP candidate is chosen yet. Open operator decisions: the
  refactor plan's follow-ups (the stage probe calling the real stages, one
  source for scalar and lane front end part B, and the `MA_SOURCE_EVIDENCE`
  layout switch).
- Other MA3 work: OPT1–3 are measured and await operator listening. OPT4
  was reverted on 2026-11-06 (candidate kept in `bb0ec9a`). MA3-LIVE is
  queued. MA3-1/2 are done; the MA3-2 listening set was accepted on
  2026-09-08.
- Latest green aggregate run: 2026-11-07 on `eb4a5a4` (later commits change
  only comments and docs). GCC, Clang and ASan/UBSan/float-cast-overflow:
  core `111378`, hosted `116`, MIDI map `22`, architecture `227`, character
  `8700`, stereo `2611`, zero failures. The pool engine twin is
  bit-identical over 73,354 frames, the three lane white-box gates hold
  their counts, and both compiler core-symbol audits pass.
- Donor pin: `mamut-sint-sw` commit
  `d7672912706731b73839d1fc25801669450fd0f1`, clean working tree when read.
- Core implementation status: MA0, MA1, MA2-1 through MA2-7, MA2-DIG,
  MA2-BCS and MA2-PERF are closed. MA3-1 and MA3-2 are complete; MA3 remains open.

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


### 2026-09-08 — MA2-7 digital pool implementation; host evidence pending

- Added a separate caller-owned 64-slot pool, default limit 32, group allocation
  and poly-unison 1/2/4/8. Released/sustained groups rank by maximum VCA envelope,
  held groups by age; replacement uses a 5 ms captured-contribution transition.
- Added seeded per-slot character, exact-idle sleep, deterministic wake,
  per-channel sustain/expression and pool-owned stereo/output controls.
  Gain remains fixed through a single shared body/DC/safety output.
- Kept `ma_card_bank`, old struct layouts, patch format and historical
  arrangements intact. Existing PCM assertions and the four new pinned
  unison scripts pass; GCC/Clang aggregate tests, ASan/UBSan/float-cast-overflow
  checks and optimized freestanding symbol audits pass.
- Prepared opt-in `bench_ma_pool` and `exhibit_ma_pool`, with repeated hashes,
  the 16/32/64-slot and 128/256/512-frame cost matrix, and four RMS-aligned
  listening cases. Their execution and acceptance are recorded in the later
  MA2-7 host-evidence entry below.
- Operator steering at implementation time deferred host measurements to the
  next model. No realtime claim and no automatic DSP-quality/polyphony reduction.
- Contract, validation commands and concrete next-model handoff:
  `docs/ma2-7-pool.md`. MA3 and future GFM now target the configured pool limit;
  the closed five-card MA2 remains historical reference evidence.

### 2026-09-08 — MA2-7 host cost matrix and listening renders

- Ran the complete 1,440-row matrix on the x86-64 host at CPU affinity 0.
  The shortened matrix used 8 warmup and 8 measured blocks per pass; every
  one of 720 repeated PCM pairs matched. RSS stayed within 4,476–4,496 KiB,
  with no major faults and 121,768-byte pool state.
- Repeated the Granica/BCS/64-slot/128-frame sustain-release worst case with
  32 warmup and 256 measured blocks. Both passes matched hash
  `b9a60a080718203d`; CPU/audio ratios were 9.192 and 8.742. The 64-slot
  worst case therefore does not meet realtime on this host; no quality or
  polyphony reduction was applied.
- Rendered four 12-second stereo listening cases twice: pad, overlap, steal
  and unison8. Raw hashes and level alignment are recorded in
  `docs/ma2-7-pool.md`; raw and RMS-matched WAVs are in the local build tree.
- Operator accepted all four listening cases. MA2-7 is closed; a concrete
  performance task is needed before MA3 live acceptance only if 64-slot density
  is required on this host.

### 2026-09-08 — MA3-1 dry hosted MIDI integration

- Added `render_midi -I mamutanalog` with factory patch, limit, unison,
  detune, seed and block-size options. The default instrument and organ/EP
  PCM remain unchanged; legacy fixture hashes were checked against `56eb8fd`.
- Added one reusable dry MIDI map for the pool and an event-boundary block
  renderer. Direct pool calls, raw byte parsing and SMF scheduling produce
  the same PCM. The format-1 tempo/running-status twin is pinned at
  `fbe7ee8dede78c92` (1,092 frames, 48 kHz, limit 8, unison 2).
- Implemented dry generic CC controls, channel expression/sustain, group
  ownership and diagnostic counters. Macro CCs are 109–113; reserved effects,
  GFM and runtime unison CC114 are ignored and counted. Added pool glide/LFO
  setters; default MA2-7 PCM signatures remain unchanged.
- GCC and Clang aggregate tests, core-symbol audits and
  ASan/UBSan/float-cast-overflow pass. Focused scheduler boundary checks and
  six executable CLI tests pass under all three builds.
- MA3-1 is complete. MA3-2 chorus/reverb and MA3-LIVE remain queued.
  MA3-PERF now explicitly covers default 32-slot throughput, isolated timing
  and a measured live-limit decision before live acceptance. No new host
  timing, long listening render or live-capacity claim was made in this slice.
- Contract, commands and evidence: `docs/ma3-1-hosted.md`.

### 2026-09-08 — MA3-2 shared effects and listening preparation

- Ported the pinned donor `SimpleChorus`/`SimpleReverb` behavior into
  `src/ma_effects.c`, using fixed caller-owned buffers and the existing sine
  polynomial. Effects run once after the digital pool's body/DC/safety output;
  no per-slot FX state or quality change. Full pool size is 766976 bytes.
- Disabled stages freeze; re-enable clears only that stage. Enabled mix zero
  preserves exact dry PCM while advancing history. Panic and voice sleep
  preserve tails; reset clears tails and retains effect controls. Patch edits
  and macros do not change effects.
- Added direct effect setters, CC107/108 and strict Mamut-only CLI mix,
  depth/rate and size/damping options. Default renderer and pool remain dry.
- GCC and Clang full tests and core symbol audits pass; full ASan/UBSan/
  float-cast-overflow tests pass. Final focused builds also pass without
  warnings. The wet direct/MIDI twin is pinned at `2a673b682c7a09a3`, with
  block splits 1/17/128/512/4096; dry twin and legacy signatures are unchanged.
  Analytic taps, independent stage reset, bypass bits, panic/sleep tails,
  hostile controls/input and four sample-rate anchors are checked.
- Added an opt-in two-patch, four-arm listening exhibit with repeated-render
  comparison and fixed RMS alignment over 0–4 seconds. Prepared controls are
  chorus `.30/.35/.25 Hz`, reverb `.25/.65/.55`; operator ballot remains open.
- Retained donor feedback coefficients and documented the maximum-size
  nonlinear tail behavior. No host timing, ALSA/live capacity or operator
  listening acceptance is claimed. MA3-PERF and MA3-LIVE remain queued.
- Contract and listening evidence: `docs/ma3-2-effects.md`.

### 2026-09-08 — new MA3 Blues/Hurt exhibits, stop before rendering

- Operator accepted MA3-2 (“odlicno je ovo”), retaining its effects settings.
  Requested new Blues and Hurt exhibits using the pool/effects path, then
  explicitly selected all-Mamut Hurt. Subsequently requested stopping when
  implementation was ready, before audio rendering.
- Added separate new exhibits and a shared hosted render desk. Legacy sources
  and WAVs remain intact. Blues has five timbre pools, 125 notes, 248 seconds;
  Hurt has ten pools, 2167 notes, 253 seconds, including all 200 vocal notes
  and Mamut replacements for the main organ and harmony passage.
- Each timbre owns its limit, unison, character/seed, body and MA3-2 effects.
  Macros, LFO and expression shape the arrangements. New outputs include
  aligned wet stems plus mix; a two-pass hash gate precedes publication.
- Extended existing listening preparation with new stem names and a Blues
  form selector. Commands, provisional balance tables and source hashes are
  in `docs/ma3-musical-exhibits.md`.
- GCC aggregate tests pass. GCC/Clang/sanitizer score checks and Clang static
  analysis pass without warnings; invalid CLI/missing-source checks pass.
  Verified the two local MIDI hashes. No new Blues/Hurt audio was generated.
- Handoff is ready. Actual DSP peaks, headroom, audio-path sanitizers, stem
  sums, repeat-render identity and musical balance remain unverified until
  rendering is resumed. No host measurement or realtime claim was made.

### 2026-09-08 — MA3 exhibits rendered

- Rendered Blues first, then all-Mamut Hurt, sequentially with GCC and no
  competing job. Both reference/output passes matched every stem and mix hash.
- Blues mix FNV64 is `4c6348322f14d58e`; listening SHA256 is
  `917fe5143f9350e92a80bfa091f07502616d357b2905e37c21b3c4558f6e2694`.
  Hurt mix FNV64 is `2406355f29923b1f`; listening SHA256 is
  `df9d04a6d270289f868e6a2c7ed86f191255fdca691008dbd32fe109884f2cb1`.
- Both stem sums are exact. No FX clipping or sanitization occurred. Blues
  had zero steals; Hurt had only released-group steals, with held and
  sustained steals at zero. All pools slept by the endpoint.
- Listening files, previews and level JSON are in ignored `build/`. Musical
  acceptance remains the next operator gate; deterministic render checks do
  not substitute for listening.

### 2026-09-09 — MA3-PERF hosted throughput baseline

- Added `driver/bench_ma3_perf.c` and `make bench-ma3-perf`. The benchmark is
  pinned externally, records CPU and wall percentiles around the pool render,
  and repeats every case to require identical PCM hashes.
- The isolated GCC run covered all five MA3 patches, BCS on/off, limits
  16/32/64, 128/256/512 frames, idle/one/eight/full occupancy,
  sustain/release, unison 2/4/8, and dry/chorus/reverb/wet arms. It emitted
  1,920 rows with zero hash mismatches, sanitizations or safety interventions.
- At full 32-slot wet occupancy the largest CPU means were 9.884 ms for 128
  frames, 19.753 ms for 256 and 40.265 ms for 512. These are 3.707x, 3.704x
  and 3.775x of their 48 kHz block budgets on the recorded host. This is a
  hosted baseline only; MA3-PERF remains open for stage profiling and measured
  optimization before a live limit is claimed.
- Evidence and schema are documented in `docs/ma3-perf.md`; generated CSVs
  remain ignored under `build/`.

### 2026-09-09 — MA3-OPT1, stop for operator listening

- Extended the existing per-sample source preparation with waveform and
  mixer divisors, one VCO2 guard-target update and a prepared VCO1 Q48 step
  for exactly zero cross-mod. Nonzero cross-mod retains substep pitch updates.
  Arithmetic division, oscillator state, FIRs and oversampling are unchanged.
- Preserved the pre-change source, release objects and executables in
  `build/ma3-opt1-yHOVSa/`. Eight new baseline PCM/state signatures cover
  dynamic cross-mod, sync, guards and BCS at all four supported rate anchors.
  `make check-ma3-opt1` is included in the aggregate tests.
- GCC/Clang full tests, freestanding symbol audits and Clang sanitizers pass;
  the oscillator alias/harmonic referee passes in all three builds. The
  existing pool, MIDI and effects signatures are unchanged.
- Rendered the existing eight-second Tepih/Granica exhibit before and after,
  sequentially. Every raw and level-matched WAV pair is byte-identical.
  Listening links, provenance and timing evidence are in `docs/ma3-opt1.md`.
- Operator listening is pending. OPT2, FIR and further optimization work are
  intentionally left for the next decision.
- Isolated release runs in before/after/after/before order retain identical
  PCM across all 80 rows. Tepih without BCS uses 8.00% less CPU on average;
  other patch results are small and uneven, including aggregate regressions.
  The full table and pair variability are retained in `docs/ma3-opt1.md`.
  OPT1 does not establish realtime support for 32 voices.

### 2026-09-10 — MA3-OPT2, implementation and functional handoff

- Added per-sample waveform activity masks from smoothed controls. Sparse
  previews skip zero-level waveform calculations; full mixes retain the
  previous preview helper. Phase, triangle integration, guards, sync and
  VCO2 cross-mod state continue even when an oscillator is inaudible.
- Added `make check-ma3-opt2` to the aggregate tests. Sixteen waveform
  combinations, signed zero, control ramps, sync/cross-mod, interval and
  note changes match OPT1 PCM and oscillator-state signatures at
  44.1/48/96/192 kHz, twice per scenario.
- GCC/Clang complete tests and freestanding symbol audits pass. Clang
  ASan/UBSan/float-cast-overflow and all three oscillator numerical referees
  pass. Existing OPT1, pool, MIDI and effects signatures are unchanged.
- Preserved baseline, superseded first variant and revised sources/builds in
  `build/ma3-opt2-1OlEaY/`. Preliminary timing of the first variant showed a
  Lead regression; the subsequent full/sparse helper split is unmeasured.
  The earlier CSVs must not be presented as results for the revised code.
- At the operator's request, stopped before further performance analysis
  and full renders; another model will handle those. Final benchmark and
  listening executables are built but unrun. `docs/ma3-opt2.md` records
  provenance, validation and the remaining measurement/listening work.
  OPT2 has no accepted speedup or realtime claim yet; FIR work is separate.

### 2026-09-10 — MA3-OPT2, hosted measurements complete

- Measured revised OPT2 against the preserved OPT1 baseline under taskset CPU
  0, GCC 16.1.1, 48 kHz and wet chorus/reverb. Matched scenarios cover
  0/1/8 active notes at limits 16/32/64, full 32-slot occupancy, and
  sustain/release at limits 16/32.
- CPU mean reductions were 1.76–25.02% in the 0/1/8-note matrix, 7.74% in
  the repeated full 32-slot matrix, and 4.43–8.34% in sustain/release. Every
  matched row retained its PCM hash; p50/p99 and wall fields remain in the
  CSV artifacts under `build/ma3-opt2-measure-20260910/`.
- One-block buffer probes measured 7.28% at 256 frames and 14.16% at 512;
  these are directional and do not establish realtime throughput. Full
  32-slot wet cost remains above the 48 kHz block budget.
- Full musical renders and operator listening remain intentionally deferred
  until requested. FIR and later optimization slices remain separate.

### 2026-09-10 — MA3-OPT3, shared BLEP preview; stop before measurement

- Saw and pulse previews share their initial BLEP correction for the same
  oscillator phase and step. Pulse retains its width-shifted correction;
  sparse masks avoid the common calculation when neither waveform is active.
  Triangle integration, sync resets, arithmetic order and DSP quality remain
  unchanged.
- Preserved the final OPT2 baseline and new OPT3 source/builds in
  `build/ma3-opt3-1d3Rwv/`. Reused the pinned OPT1/OPT2 PCM/state fixtures
  without regenerating expectations.
- GCC/Clang full tests and freestanding symbol audits pass; Clang
  ASan/UBSan/float-cast-overflow and all three oscillator numerical referees
  pass. Pool, MIDI and effects signatures remain unchanged.
- Stopped before timing/profiling and listening renders as requested.
  The paired benchmark/exhibit executables are ready for another model.
  `docs/ma3-opt3.md` records provenance and the measurement handoff;
  no OPT3 speedup is claimed before that experiment.

### 2026-09-10 — MA3-OPT3, hosted measurement complete

- Measured OPT3 against the preserved OPT2 baseline at 48 kHz, 128 frames,
  limit 32, scenario 3, wet effects and CPU 0. Each of Tepih, Lead, Raster,
  Prizma and Granica ran with BCS off/on in both baseline-first and
  optimization-first order, eight observations per patch.
- Aggregate CPU mean moved from 8500.0 us to 8319.7 us (2.12%). Tepih,
  Granica, Prizma and Lead improved 8.35%, 3.22%, 2.04% and 1.43%; Raster
  regressed 6.40%. All paired PCM hashes remained identical. This is a mixed,
  host-sensitive result, not a universal speedup claim.
- Full 32-slot wet cost remains above the 48 kHz budget. Listening renders
  remain deferred until the operator requests them.

### 2026-10-02 — MA3-PERF resumption plan

- Added `docs/ma3-perf-plan.md`: P0–P6, agent/model/effort recommendations,
  explicit file ownership, serialized measurement windows, and candidate gates.
- Verified current voice source matches the recorded OPT3 hash. Historical
  artifact directories exist; their reuse still requires manifest verification.
- Current GCC is 16.2.1 rather than the historical 16.1.1. The plan requires
  matched rebuilds and a fresh baseline. Gprof is available; perf/Valgrind are
  absent from PATH. These are planning observations, not profiling results.
- Recorded benchmark limitations: small p99 samples, permissive filters,
  release timing dependent on run length, and render-only timing boundaries.
- Performance execution and DSP changes have not started. Proposed 32-slot
  readiness/headroom criteria do not close MA3-PERF or MA3-LIVE.
- Planning validation: GCC 16.2.1
  `make CC=/usr/bin/gcc BUILD=build/ma3-perf-plan-check-20261002 test`
  passed, including the release core-symbol audit and seven CLI tests.
  This is a fresh functional check, not a performance baseline. Local links
  in the four affected documents and `git diff --check` also passed.

### 2026-10-02 — MA3-PERF first execution batch

- The operator authorized starting P0–P2. Run
  `build/ma3-perf-20261002-01/` freezes current sources, build identities,
  host context and historical artifact audit; DSP/organ/EP source is unchanged.
- Strengthened the hosted harness: strict filters/nonzero case counts,
  chronological raw samples, endpoint occupancy and release phase, nearest-rank
  quantiles, deadline/safety counters, output-error handling and empty-bracket
  measurements. Added `check-ma3-perf` to the native test target.
- Full GCC suite passed. The final harness passed seven focused CLI checks
  under GCC, Clang and sanitizers; its PCM/effect fingerprints match the
  original harness in all 160 short comparison rows. Independent review
  recomputed the measured summaries and checked matching source/binary hashes.
- Two release epochs produced 80 summaries / 20,480 measured blocks. All
  10,240 full-32/wet blocks missed CPU and wall deadlines; pass CPU means
  range 7.610–14.699 ms against 2.667 ms. One-slot cases had no CPU misses
  but 241 wall misses. A 5.511 ms empty wall-clock bracket and process variation
  prevent precise small-speedup or live-readiness claims.
- Eight Gprof cases and release assembly support an initial candidate:
  split FIR circular traversal while preserving every tap and accumulation
  order. Gprof clone-symbol misnaming requires address-based interpretation;
  inlined FIR cost remains inseparable from the broader `synth_tick` bucket.
- Evidence, reproduction commands and remaining limits:
  `docs/ma3-perf-20261002.md`. P3 Raster regression and P4 DSP optimization
  remain pending, as do long release/tail/control coverage and listening.

### 2026-10-03 — MA3-OPT4 implemented; stopped before measurement

- Completed the first candidate started on 2026-10-02: two descending spans
  replace per-tap wrap checks in the 31-tap FIR. The original coefficient,
  multiplication and accumulation order is preserved. No seven-tap filter,
  state-layout, oscillator, organ or EP change is included.
- New `test/ma_fir.c` checks 664 histories at all 31 positions: 20,584 exact-bit
  comparisons with the original loop and input immutability checks. Independent
  source review passed. `check-ma3-opt4` is part of native `make test`.
- GCC/Clang native suites, release symbol audits, pinned PCM/state fixtures,
  seven renderer CLI checks and all 16 numerical oscillator cases passed.
  The initial sanitizer run was interrupted; its separate completion run
  passed the full functional suite and numerical referee with exit 0.
- Every validation command excludes `check-ma3-perf`, including short timed
  benchmark CLI checks. Baseline/candidate benchmark executables were built
  only and preserved in `build/ma3-opt4-fir-20261002-01/`.
- The operator explicitly requested stopping before performance measurement.
  No benchmark, profiler or overhead run was executed for OPT4. Speedup and
  keep/reject decisions remain pending. See `docs/ma3-opt4.md`.

### 2026-10-03 — MA3-OPT4 matched performance screen

- The operator subsequently authorized the OPT4 A/B measurements. A fresh
  `build/ma3-opt4-measure-20261003-01/` run compares frozen OPT3 baseline and
  OPT4 binaries with the same GCC 16.2.1 benchmark harness and flags.
- Two epochs with reversed ABBA/BAAB process order covered ten full-pool wet
  patch/BCS cases and two sparse cases: 96 processes, 192 pass rows, 49,152
  measured blocks. Per-process raw samples, summaries, PCM/effect signatures,
  active slots and diagnostics were checked before aggregation.
- Full-pool CPU changes varied in sign and size; four of ten cases reversed
  direction between epochs. Lead/BCS-off improved in both epochs; Lead/BCS-on
  regressed in both. Both variants missed CPU and wall deadlines in every
  20,480 full-pool block per variant. Frequency snapshots varied markedly.
- OPT4 does not pass the plan's retention screen. No general speedup or
  realtime claim is supported. The source candidate and all evidence remain
  preserved; a later keep/reject decision needs more stable conditions and
  balanced confirmation. See `docs/ma3-opt4-measure-20261003.md`.

### 2026-11-06 — MA3-PERF: interim 8-slot target and hardware counters

- Operator decision: the MA3-PERF working target is **8 slots for now**, to
  stabilize first. The core default (`ma_voice_pool_default_config.limit =
  32`), the exhibits and every pinned signature are unchanged. The 32-slot
  objective in `docs/ma3-perf-plan.md` is recorded as the original target, and
  32 slots stay measurable as a scaling point.
- `driver/bench_ma3_perf.c` accepts limit 8. The full matrix is now 3,840
  cases / 7,680 rows; any single limit is 960 cases / 1,920 rows.
- Optional user-space hardware counters, `MA3_PERF_PMU=auto|required|off`
  (default `auto`), via `perf_event_open`. Cycles and instructions of the
  measuring thread are read as one group immediately around
  `ma_voice_pool_render`, inside the clock brackets
  (`clock_order=wall_cpu_pmu_render_pmu_cpu_wall`). Two counter reads are
  therefore inside the CPU/wall times when counters are on.
- New summary columns: `cycles_mean/p50/p99/max`,
  `instructions_mean/min/max`, `pmu_multiplexed_blocks`. New raw columns:
  `cycles`, `instructions`, `pmu_multiplexed`. Multiplexing is detected per
  block from enabled/running time and counted, never scaled. A `# pmu=` header
  states availability. With counters off or unavailable the fields are empty,
  and `required` exits 2. `--overhead` reports the empty counter bracket too.
- Cycles do not depend on clock frequency, so they are the screening metric for
  A/B candidates on this host. The time-based deadline checks are unchanged.
- Verification: GCC, Clang and Clang ASan/UBSan/float-cast-overflow builds pass
  all 10 CLI checks. Three checks are new: 8-slot occupancy, empty fields with
  counters off, and counter summaries recomputed from raw samples with
  pass-to-pass instruction agreement within 0.01%. Every parsed CSV row now
  must match its header's field count, and that check caught one missing raw
  field during development. A full GCC `make test` in a scratch build directory
  passed with exit 0 (111,378 core checks, 0 failures).
- Harness equivalence: the measured October harness (SHA256 `aeba7d67…`) and
  this one, linked to the same current objects, give identical non-timing
  output. That covers 160 rows (five patches, BCS off/on, eight scenarios, wet,
  limit 32, 128 frames, two passes) and 30 fields each, including PCM hash,
  effect fingerprints, occupancy and diagnostics: zero differences.
- Smoke observation, not evidence: Tepih / BCS off / 8 slots / wet / 128
  frames ran at about 5.05 M cycles and 9.37 M user instructions per block. The
  empty bracket is 61 instructions and about 120 cycles.
- Identities: `driver/bench_ma3_perf.c` SHA256 `18bb8001…`,
  `test/ma3_perf_cli.py` `69895450…`. The voice source in the tree is still
  the unretained OPT4 candidate (`src/ma_voice.c` `aea2d365…`), so a new
  baseline must state which voice source it measures.
- Next: a fresh matched baseline at 8 slots with counters on (all ten
  patch/BCS cases, wet, 128 frames), then candidate screening on cycles.

### 2026-11-06 — MA3-PERF: 8-slot baseline with counters

- At the operator's request, the unretained OPT4 candidate was reverted, so
  `src/ma_voice.c` is OPT3 again (`3f99f5d7…`, identical to HEAD). The OPT4
  diff is saved in the run directory, and its full source stays in the
  October runs.
- Functional gate on OPT3: 10/10 CLI checks on the release binary, and a full
  GCC `make test` (111,378 core checks, 0 failures).
- Run `build/ma3-perf-20261106-01/`: ten patch/BCS cases, limit 8, all slots
  held, wet, 128 frames, counters required. One process per case, 256 + 2,048
  blocks × 2 passes, three epochs, CPU 2, sleep inhibited, no suspend in the
  run window. Results and reading are in `docs/ma3-perf-20261106.md`.
- Medians are 4.39–7.00 M cycles per block, stable to 0.55% between epochs,
  and all within the 80% target at the observed ~3.28 GHz. CPU p99 exceeds
  that target in nine of ten cases and the deadline in four. The cycle tail is
  uncorrelated with block index across passes, so it comes from the
  environment, not the DSP workload.
- Next: the SIMD-across-voices spike, screened on median cycles. Tail control
  (isolation, SMT sibling, priority) belongs with MA3-LIVE.

### 2026-11-06 — MA3-PERF SIMD spike: ladder and 31-tap FIR as 8 AVX2 lanes

- New `driver/spike_ma_simd.c` (`make spike-ma-simd`, not part of `test`). It
  includes `src/ma_voice.c` white-box. Only the AVX2 functions are
  target-attributed, with no FMA, so the scalar reference keeps production
  flags. No shipping code changed.
- Bit-exact: 2 M calls × 8 lanes per kernel, with edge values in every input,
  match the scalar voice on GCC, Clang and Clang ASan/UBSan. For the ladder that
  covers output, states, `y4_guess` and reset counts, with 172,130 resets
  exercised.
- Cycles per voice-call (three epochs, CPU 2, within 0.3%): ladder 420 → 70.6
  (5.95x), FIR push+decimate 104 → 11.6 (9.0x).
- Estimate against the 8-slot baseline: these stages are 21–34% of per-voice
  cost, so the whole chain would gain about 1.22–1.41x. The worst case needs
  about 1.5x at base clock. Integration needs a stage-major pipeline, SoA
  ladder and ring state, ring rotation on wake, and masking of sleeping lanes.
  See `docs/ma3-simd-spike-20261106.md`.
- Open: integrate these two stages and measure, or first run an oscillator
  SIMD feasibility spike (q48 phases, u64↔float, sync/BLEP branches).

### 2026-11-06 — MA3-PERF oscillator SIMD spike

- New `driver/spike_ma_osc_simd.c` (`make spike-ma-osc-simd`, not part of
  `test`). It runs the whole per-sample oscillator block (VCO2 guard target,
  8 × `render_source_substep`, both `next_guard_gain` calls) as 8 AVX2 lanes.
  Every path is masked. Exact helpers replace the u32→float, u48→float and
  float→u64 conversions that AVX2 lacks. No shipping code changed.
- Bit-exact: 200 k samples × 8 lanes × 8 substeps, with randomized sanitized
  controls over all five patches. Outputs and the full state of both
  oscillators match after every sample on GCC, Clang and Clang ASan/UBSan.
  Coverage: 1.2 M cross-mod, 43.5 k sync-on wraps, 14.2 k guard moves, 971 k
  sparse previews and 1.3 M sine lane-samples. Five of six deliberate
  mutations were caught; the sixth is an equivalent mutant (0 × finite).
- Cycles per voice-sample, as 3-epoch medians: Tepih 2,246 → 394 (5.70x),
  Lead 2,691 → 501 (5.38x), Raster 1,566 → 412 (3.81x), Prizma 2,956 → 612
  (4.83x), Granica 2,957 → 615 (4.81x).
- Estimated with the ladder/FIR spike: the whole chain gains 1.86–2.72x, and
  the worst 8-voice block falls to about 3.35 M cycles, under 80% of the
  base-clock deadline (4.48 M). That is an estimate from isolated kernels;
  integration cost is not included. See `docs/ma3-simd-osc-spike-20261106.md`.
- Next decision: integrate the stage-major 8-lane voice pipeline (oscillator
  block, FIR cascade, ladder) behind an AVX2 requirement, with the scalar voice
  kept as reference. The pinned `make test` signatures are the gate.

### 2026-11-06 — MA3-PERF vector lanes integrated

- First, the voice tick was staged (`synth_tick` split into front end,
  oscillator block, decimators, filter and back end). Per-voice float
  operations are unchanged. GCC and Clang `make test`, the freestanding
  audit and ASan/UBSan passed before any vector code was added.
- Then the vector lanes went in: `ma_voice_lanes` and `ma_lanes_*` in
  `src/ma_voice.c`, plus the new `ma_voice_pool_config.engine` field
  (AUTO = AVX2 when CPUID/XGETBV allow it, or SCALAR).
  `ma_voice_pool_render` gathers awake slots into banks of eight and
  scatters them back in the exact scalar representation, at the end of the
  call or when a slot sleeps. The core stays freestanding; only `memset`
  is unresolved.
- Gates, with lanes active: GCC/Clang `make test` (111,378 checks, every
  pinned signature, 0 warnings), the core symbol audit and ASan/UBSan.
  The new `engine_twin` compares AUTO and SCALAR bit-for-bit over 73,354
  frames, including slot state after every render and 59 sleeps. Both
  deliberate faults were caught.
- A/B on one binary, 8 slots, 3 epochs, counters: 1.79–3.17x faster than
  the scalar engine, and the worst case runs 2.95 M cycles per block
  (Prizma, BCS on). Across 122,880 vector blocks there were 0 deadline
  misses and 0 blocks over the 80% target; the largest block took 1.59 ms.
  See `docs/ma3-vector-lanes-20261106.md`.
- The interim 8-slot target is met in hosted measurement. MA3-LIVE still
  owns isolation, priority and the no-xrun gate. Next DSP candidates are
  BCS (0.9–1.1 M cycles of the remaining BCS-on cost), mozaik/raster and the
  front end.

### 2026-11-06 — MA3-PERF PMU probe: cache, branches, denormals, divider

- New `driver/probe_ma3_pmu.c` (Makefile target `$(BUILD)/probe_ma3_pmu`,
  not part of `test`) counts user-space Haswell raw events per render block
  for the ten 8-slot wet cases on both engines, one event group at a time.
  `--calibrate` checks every event against a known trigger. Because it was
  unstable, `MEM_LOAD_UOPS_RETIRED.L3_MISS` was replaced by
  `LONGEST_LAT_CACHE.MISS`.
- The first runs were contaminated: browser media playback ran on the same
  physical core and moved cycles by up to 66%. The quiet runs, with all
  browsers closed, agree to 0.47% in cycles and match the integrated A/B.
- Floating-point assists were zero in every case, so there are no denormals
  and FTZ/DAZ is not needed. Memory and branch mispredictions are each at
  most a few percent of vector time. The divider is roughly 8–15% of vector
  time, and bit-exactness keeps it from being converted to multiplies. BCS
  adds about 1.1 M cycles per BCS-on block. See `docs/ma3-pmu-20261106.md`.
- Next: per-stage cycle attribution, then BCS in the lanes, then exact
  any-lane division skips.

### 2026-11-06 — MA3-PERF stage attribution of the vector engine

- New `driver/probe_ma3_stages.c` (target `$(BUILD)/probe_ma3_stages`, not
  part of `test`) re-runs the vector render path white-box with TSC reads
  between stages. Its frames and slot state are bit-identical to a twin
  pool rendered through the real API.
- With BCS off, the scalar front end takes 35–47% of a block, the
  oscillator block 25–33%, filter + FIR7 9–11%, the decimators 5–7%, output
  and effects 5–6%, and the lane fill 4–5%; everything else is about 1%.
  With BCS on, the front end rises to 58–64%, because BCS adds about 1.3 M
  ticks per block.
- Chrome ran on the core's SMT sibling during both runs. The absolute ticks
  are inflated, but the shares agree to within 1–3 points between runs.
  See `docs/ma3-stages-20261106.md`.
- Next: BCS in lanes, then the front-end controls and envelopes in lanes,
  writing lane inputs directly.

### 2026-11-06 — MA3-PERF vector BCS

- The front end is split into part A, then BCS, then part B. The scalar
  path is still A → `render_bcs` → B. The lanes run BCS eight wide
  (`ml_bcs`), with RK4 operation order kept; resets use the scalar
  `reset_bcs`. The new `test/ma_bcs_lanes.c` (`check-ma-bcs-lanes`, in
  `test`) compares `ml_bcs` with `render_bcs` bit-for-bit over 89,752
  lane-steps, 304 of them resets. It catches constant and summation-order
  mutations. The exact |v| = 32 ceiling boundary was not reached by the
  randomized test and was checked by inspection.
- GCC/Clang `make test`, ASan/UBSan and the core symbol audit all pass,
  with every pinned signature intact.
- A/B with the AUTO engine, 3 epochs (`build/ma3-bcs-ab-20261106-01`),
  where A is `10418a8` and B adds vector BCS: BCS-on blocks run
  1.31–1.37x faster (worst case Prizma, 2.954 → 2.254 M cycles), BCS-off
  medians differ by 0.1–2.5%, and PCM is identical in every case.
- Continuation audit: the candidate has 1 CPU and 134 wall deadline misses
  across 122,880 blocks. This run proves cycle improvement, not live
  readiness or a quiet host throughout. See `docs/ma3-bcs-lanes-20261106.md`
  for the per-case table and source/binary identities. Next: controls and
  envelopes in lanes with direct lane-input writes, then raster/mozaik.

### 2026-11-07 — MA3-PERF front-end sub-stages

- `driver/probe_ma3_stages.c` now mirrors the current lane path (front A,
  vector BCS, front B) and splits the front end into ten sub-stages, run
  stage-major across voices. Frames and slot state are bit-identical to a
  twin pool, the two quiet runs agree to within about 0.7 points, and timer
  overhead is 7–11%.
- In the held state, controls (4.7–6.9%) and envelopes (1.7–4.5%) are
  cheap. The source frame (8–12%) is the largest front-end item.
  Raster is 9–12% where it is used, cutoff and prewarp 5–8%, mozaik 4–6%,
  the lane fill 3–4%, and vector BCS 13.6–16.2% when on.
  See `docs/ma3-frontend-stages-20261107.md`.
- Next: put the source frame, cutoff and prewarp in lanes, writing the
  lane inputs directly (about 18–24% of a block). Then raster. Controls and
  envelopes wait until a moving-control workload shows a cost.

### 2026-11-07 — MA3-PERF front-end math in lanes

- New `ml_front_math` (AVX2) computes the envelope amount, effective
  cutoff, Chebyshev prewarp and full source frame for eight voices, and
  writes them straight into the lane inputs. `voice_front_b_lane` keeps
  the scalar parts and writes the plain lane inputs directly, so the lane
  fill is gone. The scalar path is unchanged.
- New `test/ma_front_lanes.c` (`check-ma-front-lanes`, in `test`): 400,000
  lane-frames bit-identical to the scalar functions, and both deliberate
  mutations caught. GCC/Clang `make test`, the core audit and ASan/UBSan
  pass with 0 warnings and every pinned signature.
- A/B against `4106152` on a quiet host: 1.08–1.12x faster with identical
  PCM, worst case 2.073 M cycles (Prizma, BCS on), and 0 CPU and wall
  deadline misses in 122,880 blocks. See
  `docs/ma3-front-lanes-20261107.md`.
- Next candidate: raster (9–12% where used).

### 2026-11-07 — MA3-PERF raster in lanes, BCS inputs in registers

- A first `ml_raster` was bit-exact but made the raster patches 5–7% slower.
  The PMU probe has a new `store` group (`LD_BLOCKS.STORE_FORWARD`, 4K alias,
  store-buffer-full stalls; the store-forward event is calibrated at 1.000
  per trigger). It found about 2,800 blocked store forwards per block: lane
  arrays were filled one scalar at a time and reloaded as vectors. The vector
  BCS already had about 3,500 per BCS-on block from the same pattern.
- Fix:
  - `ML_PICK_PS`/`ML_PICK_U64` build per-lane inputs in registers.
  - `ml_raster` runs fully eight lanes wide. The mip choice is a count, and
    each table row is one gather of the int16 (index, index + 1) pair, with
    an index-255 fixup that keeps every read inside the table.
  - `ml_bcs` takes its landmark lerp through `vpermps`. The scalar path is
    unchanged.
- New `test/ma_raster_lanes.c` (`check-ma-raster-lanes`, in `test`): 224,620
  lane-samples bit-identical. 18 mutations were caught. Four survivors are
  documented as equivalent. GCC/Clang `make test`, the core audit and
  ASan/UBSan pass with every pinned signature.
- A/B against `d1e76c0` on a quiet host:
  - 1.15–1.23x with BCS on, 1.07–1.08x for raster with BCS off, and 1.00–1.02x
    for the rest
  - identical PCM, 0 deadline misses
  - worst case 1.737 M cycles (Prizma, BCS on)
  - See `docs/ma3-raster-bcs-lanes-20261107.md`.
- Refactor plan for splitting `ma_voice.c`: `docs/ma-voice-refactor-plan.md`.

### 2026-11-07 — `ma_voice.c` split into voice modules

Executed `docs/ma-voice-refactor-plan.md` from `1308ef0`. Move-only: no
arithmetic, operation order, constant or flag changed. After every step,
GCC/Clang `make test` (111,378 core checks), ASan/UBSan, both core-symbol
audits (only `memset` unresolved), all 20 bench PCM hashes (10 cases × both
engines), the `exhibit-ma1-osc` output and the white-box counts matched the
baseline. Evidence: `build/ma-voice-refactor-*`.

| Commit | Module | Contents |
|---|---|---|
| `b6c0426` | `ma_voice_impl.h` | Private header: constants, internal types, `static inline` primitives. Machine code identical to the baseline. |
| `80e65d8` | `ma_patches.c` | The six presets. Adds the evidence object set `$(BUILD)/evidence/%.o`, which replaces `ma_voice_source.o`. |
| `1d38dfc` | `ma_lanes.c` | The AVX2 lane section and its evidence stubs. |
| `1881c1e` | `ma_controls.c` | Cold control code, note events, `ma_synth_init*`, the setter API. |
| `eb4a5a4` | `ma_filter.c` | Mixer pressure, ladder, filter history, output decimator, `voice_filter`, `render_output`. |
| `30cd135` | — | White-box comment updates. |

Notes:

- Lanes: first attempt made `render_envelope`, `render_mozaik` and
  `voice_back_end` extern. AUTO regressed up to 3.1 % (instructions +1.5 %:
  lost `.isra` clones and cross-module calls). Moving them, plus
  `interval_ratio`, to the header as `static inline` gave AUTO −0.3 to
  −1.4 % and scalar +0.4 to +1.4 % against step 2. Plain `static` header
  copies (to keep the baseline's inlining limits) were worse on scalar
  (+0.8 to +2.3 %). Inlining cannot match the baseline exactly: each helper
  now has one caller per module, so GCC's called-once heuristic applies.
- Only `voice_front_a`, `mozaik_slope_q32`, `voice_filter` and
  `render_output` became extern (`ma_voice_` prefix).
- `ma_voice_tick_controls` stays in `ma_voice.c` next to
  `next_render_controls`, because the pool calls it every frame.
- `ma_lanes.c` includes `ma_raster_table.h` (the vector raster reads the
  table), so the 28 KiB `static constexpr` table has a copy in two objects.
  A single extern table would change the generated header and needs an
  operator decision.
- Rejected: `ma_sources.c` with raster and BCS (`render_raster`/`render_bcs`
  extern). Scalar was +2.0 to +3.4 % in all ten cases, also Tepih with BCS
  off, where neither function runs; instructions rose only 0.3–0.7 %. The
  cost is `synth_tick` code generation (2144 → 1664 instructions without the
  inlined sources), not the calls. The header-inline remedy would empty the
  module, so the code stays. The oscillators were not tried: the evidence
  tick calls their helpers per substep. Patch:
  `build/ma-voice-refactor-step5-sources/attempt.patch`.
- Left in `ma_voice.c` for performance: oscillators, the 8x→2x decimators,
  raster, BCS, envelopes, the staged scalar tick and the per-sample control
  path. 1,061 lines; `ma_voice_impl.h` 554, `ma_controls.c` 757,
  `ma_lanes.c` 1,161, `ma_filter.c` 99, `ma_patches.c` 127.
- Object sizes: `.text` 67,533 B before, 69,900 B over the five objects
  after (shared `static inline` helpers compile into each module);
  `.rodata` +29.5 KiB, mostly the second raster table.
- Final quiet-host A/B, baseline `1308ef0` vs `30cd135`, 10 cases × 3
  epochs, CPU 2: AUTO −1.25 to +0.46 %, scalar −2.32 to −0.08 %; PCM
  identical. Deadline misses (CPU/wall, all blocks): AUTO 0/0 on both
  sides; scalar 669/698 before and 314/352 after, at 8 voices where the
  scalar engine already runs near the 128-frame deadline.
  Runs: `build/ma-voice-refactor-abfinal-{auto,scalar}-20261107-01/`.

### 2026-11-07 — one raster table

- Operator decision: one extern raster table instead of a copy in
  `ma_voice.o` and `ma_lanes.o`. The data moves unchanged from
  `src/ma_raster_table.h` into `src/ma_raster_table.c` as
  `const int16_t MA_RASTER_TABLE[8][7][256]`. The header keeps the
  dimensions and gains the `extern` declaration. The new object is in
  `MA_OBJS` and the evidence object set.
- Identity: the header SHA256 pinned in `docs/ma2-perf-evidence.md`
  (`d2a27107…`) is now historical. The compiled table is byte-identical to
  the old static table: 28,672 bytes, SHA256 `928257a6…`, 32-byte aligned
  in both. Evidence: `build/raster-table-extern-20261107/`.
- The definition is `alignas(64)`. Both former static copies happened to sit
  on 64-byte boundaries. Without the specifier the shared table landed at
  32 mod 64, so every 512-byte row started mid-line under the vector gather.
- `ma_voice.o` and `ma_lanes.o` disassemble identically to `b6bb3a2` per
  function. `.rodata` across the voice objects drops by 28 KiB, to within
  0.8 KiB of the pre-split size.
- GCC/Clang `make test`, ASan/UBSan, both core-symbol audits, all 20 PCM
  hashes, the `exhibit-ma1-osc` output and the white-box counts are
  unchanged.
- Quiet-host A/B against `30cd135`, 10 cases × 3 epochs, CPU 2: AUTO −1.32
  to +0.85 %, scalar −0.43 to +0.19 %; PCM identical. AUTO was noisy in this
  window (one A epoch 19 % off). Two earlier AUTO runs of the unaligned
  table each had one case near +1.5–1.8 % from a single noisy epoch, in
  different cases. Runs: `build/ma-voice-refactor-abrt*-20261107-*`.
