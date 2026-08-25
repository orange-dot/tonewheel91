# Mamut Analog development journal

Started: 2026-08-20. This is the live execution record for
`docs/analog-backlog.md`. The backlog owns product and acceptance decisions;
this journal owns task order, current status, validation results and short
implementation notes.

Status vocabulary: `queued`, `in progress`, `blocked`, `done`. A public
milestone remains open until every task in its gate is done, even when one of
its internal sub-gates already has working code.

## Current position

- Active milestone: **MA1 — complete one-voice hybrid**.
- Active task: none. **MA1-5 — filter/amp envelopes and VCA** is the next
  queued task.
- Last green aggregate run: GCC and Clang `make test`, 2026-08-25, at
  tonewheel91 commit `7536e14` plus the MA working tree: core `107243`, hosted
  `77`, MIDI map `22`; zero failures. Both core undefined-symbol audits pass.
- Donor pin: `mamut-sint-sw` commit
  `d7672912706731b73839d1fc25801669450fd0f1`, clean working tree when read.
- Core implementation status: MA0 and MA1-1 through MA1-4 are closed; work is
  stopped at the boundary before MA1-5.

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
| MA1-5 | queued | MA1-4 | Filter and amp RC ADSRs plus VCA; retrigger/release/epsilon behavior and hostile sweeps green. |
| MA1-6 | queued | MA1-5 | Linear five-macro identity resolver, zero-frame deltas, performance overlays and 6 ms smoothers; zero macros are bit-identical. |
| MA1-7 | queued | MA1-6 | Centered dual-mono output, body bypass, DC block, safety diagnostics and deterministic signatures. |
| MA1-8 | queued | MA1-7 | `make exhibit-ma1`, evidence document, cost table and operator listening verdict; public MA1 gate closes only here. |

## Decision log

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-08-20 | Keep the public milestone as one MA1 slice but expose eight internal gates in this journal. | The backlog requires a complete voice while also requiring source/filter/identity failures to stop later work. |
| 2026-08-20 | Track this journal in Git instead of using the ignored `docs/ep-journal.md` pattern. | MA0/MA1 status and validation provenance are part of the handoff, not disposable listening notes. |
| 2026-08-20 | Do not edit organ or EP core translation units. | Their frozen signatures are the regression boundary for the third line. |
| 2026-08-20 | Keep donor inspection read-only and all tonewheel91 tooling and implementation in C23. | The target repo is a C instrument; cross-language helper artifacts obscure that boundary and buy nothing. |
| 2026-08-20 | Run sharp-edge VCO kernels at 4x, then decimate into the existing 2x mixer/filter boundary with the pinned halfband. | 1x yielded 7.38–17.74 dB and the 2x trial 12.57–20.20 dB; only one of eight 2x cases passed the mandatory 20 dB gate. |
| 2026-08-20 | Promote the ordinary and sync edge residual from donor quadratic to a C2 quintic PolyBLEP candidate. | Moving the quadratic kernel to 4x still yielded only 13.36–19.88 dB; the error must be reduced at the discontinuity, not hidden by the boundary filter. |
| 2026-08-20 | Accept an 8x Q48 VCO edge path, two 31-tap boundaries into the 2x ladder, and an eight-substep C2 reset slew. | This is the first candidate to pass every pinned ordinary/sync/cross-mod case: 21.86–29.16 dB versus naive edges on both compilers. |
| 2026-08-25 | Render Mozaik once per public audio frame after the sharp-edge VCO decimation, then complete the pinned normalized source sum there. | Hann tiles need no VCO oversampling; keeping their Q32/tile clock at the public sample rate preserves the donor duration contract and leaves the MA1-2 edge referee isolated. |
| 2026-08-25 | At MA1-4, advance Mozaik in both 2x source phases, with its render rate doubled, and feed that native 2x bus directly into pressure and the ladder. Compile the historical MA1-2 alias referee with `MA_SOURCE_EVIDENCE`. | Doubling both Mozaik steps and its clock preserves tile duration and drift in wall time. The dedicated source-only build keeps later filter attenuation from being counted as oscillator anti-alias evidence. |

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
