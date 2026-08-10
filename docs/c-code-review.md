# tonewheel91 C Code Review and Remediation

Review date: 2026-08-10
Baseline: `main` at `25f4a5a`, including the uncommitted working-tree changes present during the review

## Remediation status

All nine findings were implemented in the current working tree on 2026-08-10.
The detailed sections below are retained as the review-baseline evidence and
rationale; their descriptions of vulnerable code do not describe the remediated
tree.

| ID | Implemented repair |
|---|---|
| C-01 | Extracted a bounded SMF format 0/1 parser with checked cursors, strict VLQs and chunk boundaries, checked growable vectors, owned cleanup, byte-offset diagnostics, malformed fixtures, and a libFuzzer entrypoint. |
| C-02 | Extracted shared hosted PCM setup/write/cleanup, validates the negotiated ALSA geometry, allocates from the final period before the loop, checks all setup calls and size arithmetic, and treats persistent MIDI and zero-frame PCM writes as errors. |
| C-03 | Added explicit per-key damper state, rebuilds live decrements from that state, stores horizontal energy independently from condition depth, and documents condition zero as an output gate that preserves accumulated history rather than an impossible exact mid-note rejoin. |
| C-04 | Replaced shift-based random extraction with four bounded 13-bit slots and three deterministic words per key, assigning independent fields to independent deviations. |
| C-05 | Added hosted source ownership by channel and source note, destination reference counts, maximum-depth aggregation, folded-note ownership, panic reset, and independent out-of-compass accounting. |
| C-06 | Replaced permissive option conversion with typed complete-string parsing and explicit ranges; added checked render/frame/byte arithmetic, a two-hour render ceiling, and RIFF/WAVE representability checks. |
| C-07 | Added one 44.1–192 kHz sanitizer with 48 kHz fallback and applied it to every component and top-level initializer; consolidated the one-pole coefficient helper. |
| C-08 | Added compile-time 8-bit-byte and IEEE binary32 assertions, documented hosted WAV and bare-target runtime assumptions, and added an unresolved-core-symbol allow-list check. |
| C-09 | Removed dead reporting state, corrected the control/status and hard-silence contracts, refreshed current-facing documentation, and added direct control-dispatch tests. |

Current verification after remediation:

| Check | Result |
|---|---|
| GCC `make test` | Core: 9,410 checks; hosted boundaries: 77 checks; MIDI dispatch: 22 checks; all passed |
| Clang `make test-clang` | The same 9,509 checks passed; the core symbol allow-list passed |
| Clang `make sanitize` | ASan, UBSan, and float-cast-overflow builds passed all suites; LeakSanitizer was disabled because the traced environment cannot run it |
| GCC `make analyze` | Full target set built with `-fanalyzer` without diagnostics |
| `make fuzz-smf` | 1,000 generated inputs completed without a parser failure |
| Absolute `BUILD=/tmp/... make test` | Built and ran successfully, including the symbol check |
| Real format-1 SMF smoke | Parsed ten tracks with no selected channel events and produced identical two-run hashes |
| Invalid `-c` and `-R` probes | Rejected immediately with exit status 2; no hang |

The live ALSA paths were rebuilt but were not exercised against physical audio
hardware. Sound quality was intentionally not reevaluated.

## Scope

This review covers the C23 implementation in `src/`, the hosted C programs in
`driver/`, the C test harness, and the Makefile insofar as it defines and
verifies the C build. It evaluates correctness, memory safety, state ownership,
input validation, portability, real-time suitability, test coverage, and
maintainability.

It deliberately does **not** evaluate sound quality, voicing choices, acoustic
accuracy, calibration constants, or whether the organ and electric-piano models
sound convincing. Those points were accepted as outside this review. A DSP
choice appears below only when its implementation violates a C-level invariant
or a documented API contract.

The working tree already contained changes to `src/ep_voice.c`, `src/epiano.h`,
`test/test.c`, and EP documentation. The review phase treated those changes as
the baseline; the subsequent remediation modified the relevant portions in
place. Line links below refer to the reviewed baseline and may move after the
changes are committed.

## Executive assessment at the review baseline

The core is unusually disciplined for a small audio engine. It has explicit
caller-owned state, deliberate zero initialization, no allocation or I/O in the
sample path, direct control flow, fixed work bounds, consistent `snake_case`
naming, deterministic PRNG use, and strong identity tests. The organ core's
documented valid-input path is in good condition. No failure was observed in the
9,393-check core suite, under either the normal build or AddressSanitizer and
UndefinedBehaviorSanitizer.

The current C layer is nevertheless not yet robust at all of its boundaries.
The two highest-risk defects are in hosted code: malformed SMF input can drive
out-of-bounds reads or unchecked allocation failures, and ALSA can negotiate a
period larger than the live drivers' fixed buffers after the drivers have
performed their only size check. The current EP condition work also contains
deterministic state bugs that the green test suite does not exercise.

There are no findings that require a new framework or a broad rewrite. The best
repairs are small and local: one bounded SMF cursor, one narrow shared live-I/O
helper, explicit EP damper state, a corrected random-field layout, and a few
checked arithmetic helpers.

## Baseline verification performed

| Check | Result |
|---|---|
| `make test` | `9393 checks, 0 failures` |
| Full GCC 16.1.1 build with `-Wall -Wextra -Wpedantic -fanalyzer` | Built all core, tests, exhibits, renderer, and live drivers; analyzer reported the unchecked `ev` and `tm` allocations in `render_midi` |
| Full Clang 22.1.8 build with additional conversion, shadow, format, and prototype warnings | Built all targets; the core was clean, while a targeted `-Wcast-qual` pass exposed the `-c` parser's `const char **` cast |
| Clang ASan + UBSan + float-cast-overflow test build | `9393 checks, 0 failures`; LeakSanitizer alone was disabled because it cannot run under the execution environment's tracing mechanism |
| Sanitized parse/render of a real format-1 SMF with no selected events | Completed successfully and produced identical two-run hashes |
| `git diff --check` | Clean |
| Focused EP state probe | Reproduced damper-state loss, residual horizontal amplitude at condition zero, the biased 12-bit field, and mixed component sample rates |
| `timeout 1s render_midi -c x ...` | Timed out with exit 124, confirming the invalid channel-list hang |
| `nm -u` over optimized core objects | No OS, allocation, or libm references; several large aggregate initializers compile to an unresolved `memset` reference |

The live ALSA loops were compiled and reviewed but were not exercised against
audio hardware. Exhibits were compiled but not judged by listening.

## Bug register

| ID | Severity | Area | Baseline summary | Status |
|---|---|---|---|---|
| C-01 | High | Offline renderer | The SMF parser trusts chunk and event lengths and does not check several allocations | Resolved in working tree |
| C-02 | High | Live drivers | ALSA may negotiate a period larger than the fixed output arrays | Resolved in working tree |
| C-03 | Medium | EP core | Changing condition destroys live damper state and does not actually remove all condition-only state | Resolved in working tree |
| C-04 | Medium | EP core | A claimed 13-bit random field has only 12 available bits and two deviations reuse other fields | Resolved in working tree |
| C-05 | Medium | MIDI ownership | Merged channels and octave-folded notes can release a key that another source still owns | Resolved in working tree |
| C-06 | Medium | Hosted input/output | Numeric option parsing can hang, invoke undefined conversions, or overflow render/WAV sizes | Resolved in working tree |
| C-07 | Medium | Core initialization | Sample-rate sanitization is inconsistent across components of one instrument | Resolved in working tree |
| C-08 | Low | Portability contract | Binary32 and freestanding-runtime assumptions are real but not asserted or fully documented | Resolved in working tree |
| C-09 | Low | Contracts and dead code | Current-facing comments, counters, and README status have drifted from the implementation | Resolved in working tree |

Severity reflects programming risk, not audible impact. “High” denotes a
memory-safety or undefined-behavior path at an external boundary. “Medium” is a
repeatable state or public-contract failure. “Low” is a portability,
maintainability, or reporting defect on the currently declared Linux targets.

## Detailed findings and fixes

### C-01 — Make the SMF reader bounded and allocation-safe

The parser in [`driver/render_midi.c`](../driver/render_midi.c#L304) reads the
whole file and then walks it with raw indices. Valid files work, but malformed or
truncated files are not contained:

- `fseek` and `ftell` are unchecked, so a failed `ftell` is cast to `size_t`.
- The `ev` and `tm` allocations at
  [`driver/render_midi.c:331`](../driver/render_midi.c#L331) are unchecked before
  the parser writes through them. GCC's analyzer independently reported both.
- `end = i + len` can wrap, and a truncated track is silently shortened instead
  of rejected.
- Meta type, tempo bytes, channel data bytes, and the second byte of two-data
  messages are read without first proving that they remain inside the track
  ([`driver/render_midi.c:345`](../driver/render_midi.c#L345)).
- `read_vlq` has no error result, accepts an unterminated VLQ, and does not
  enforce SMF's bounded VLQ representation.
- Meta and SysEx lengths are added to the cursor without checked conversion or
  checked addition.
- `ev_frame` is also unchecked before use.

This is an offline tool rather than the real-time MIDI byte parser, but an input
file should never be able to read outside its buffer or turn allocation failure
into a null dereference.

Fix this with one small hosted parser module, not a general MIDI framework:

1. Introduce a cursor containing `current`, `end`, and an error flag, with
   `take_u8`, `take_be16`, `take_be32`, `take_vlq`, and `take_span` operations.
   Every operation must fail without advancing when insufficient bytes remain.
2. Validate the `MThd` length, format/track-count combination, nonzero PPQ
   division, every `MTrk` boundary, every channel message width, and every meta
   or SysEx payload before reading it.
3. Replace size-derived guessed allocations with a checked growable vector.
   The growth helper should reject `capacity * sizeof(element)` overflow before
   `realloc` and leave the old allocation intact on failure.
4. Reject a partial/truncated file with its byte offset instead of accepting the
   parsed prefix.
5. Use one cleanup exit that closes the stream and frees `d`, `ev`, `tm`,
   `ev_frame`, and `buf` on all paths.
6. Add table-driven malformed fixtures: truncate a valid file at every byte,
   overlong/unterminated VLQs, zero division, oversized chunks, missing data
   bytes, and allocation-size overflow. Run those tests under ASan/UBSan and add
   a small libFuzzer target for the extracted parser if Clang is available.

The existing freestanding byte-stream parser in `src/midi.c` is small and
bounded; it does not need to be replaced by this hosted SMF parser.

### C-02 — Size live buffers after ALSA negotiation and validate startup once

Both live drivers clamp the *requested* period to `MAX_PERIOD` before calling
[`snd_pcm_hw_params_set_period_size_near`](../driver/main.c#L135). ALSA writes
the negotiated value back through `period`, and that value can be larger than
the request. The drivers never check it again, then index fixed
`2 * MAX_PERIOD` arrays through the negotiated period in
[`driver/main.c:215`](../driver/main.c#L215) and
[`driver/ep73.c:160`](../driver/ep73.c#L160). A device whose minimum supported
period exceeds 4,096 frames can therefore produce an out-of-bounds write.

The same duplicated startup code also accepts `atoi`/`atol`/`atof` results
without validation, permits zero periods and period counts, does not reject a
non-finite gain, ignores several ALSA parameter-call results, leaks the PCM
handle if raw-MIDI opening fails, ignores persistent raw-MIDI read errors, and
would spin forever if a successful `snd_pcm_writei` returned zero.

One narrow shared hosted helper can fix both drivers without merging their
instrument logic:

1. Parse `rate`, `period`, `periods`, `gain`, and demo duration with checked
   `strto*` calls, complete-string consumption, `errno`, finite-value checks,
   and explicit ranges.
2. Complete ALSA negotiation, query or retain the final period, then either
   reject it if it exceeds the supported ceiling or allocate the two interleaved
   buffers once from the final value. Allocation during startup is compatible
   with the no-allocation real-time path.
3. Check multiplication before computing `2 * period`, `period * nperiods`, and
   demo frame counts.
4. Check every ALSA setup call, close already-open handles on every error path,
   distinguish `-EAGAIN` from disconnect/fatal raw-MIDI errors, and treat a zero
   PCM write as an error rather than retrying forever.
5. Keep the instrument-specific MIDI maps and render calls in `main.c` and
   `ep73.c`; share only option parsing, PCM setup, float-to-S32 conversion, full
   writes, and cleanup.

### C-03 — Preserve EP runtime state across condition rebuilds and define honest mid-note semantics

`ep_bank_set_condition` always calls `rebuild`
([`src/ep_voice.c:481`](../src/ep_voice.c#L481)). The rebuild regenerates
coefficient tables, but it also assigns every live decrement to the damped
decrement at [`src/ep_voice.c:436`](../src/ep_voice.c#L436) and every horizontal
decrement to its damped value at
[`src/ep_voice.c:460`](../src/ep_voice.c#L460). Consequently, changing CC93
while a key is held or sustained silently puts its modes back on the damped
decay until another key/damper event repairs them.

A focused probe reproduced the transition: before a condition change the test
note's live decrement equalled `dec_free`; immediately afterward it equalled
`dec_damp` even though no release or pedal event occurred.

The same rebuild exposes a second contract problem. A live horizontal amplitude
is stored already scaled by `h_depth` in
[`src/ep_voice.c:553`](../src/ep_voice.c#L553). Returning condition to zero sets
`h_depth` to zero but leaves the nonzero `h_amp` untouched, so `polar_tick`
continues adding it to the pickup. The probe observed identical nonzero
`h_amp` values before and after setting condition to zero. The public promise
that zero restores the ideal instrument “exactly, even mid-note” while phase
and amplitude remain untouched
([`src/epiano.h:190`](../src/epiano.h#L190)) is also internally impossible for
the detuned main modes: time spent at a different step has already changed
their phase history. The current test toggles condition before striking or
ticking, so it does not exercise this claim
([`test/test.c:3584`](../test/test.c#L3584)).

Repair the state model and the contract together:

1. Add explicit `damper_up[EP_KEYS]` state. Zero initialization then naturally
   means “damper on at rest.” `set_decrements`, strike, damp, undamp, silence,
   and panic update that state; `rebuild` derives `dec` and `h_dec` from it
   after recomputing the free/damped tables.
2. Store horizontal excitation independently of the condition-scaled pickup
   depth and multiply by `h_depth` at the output, or explicitly rescale/clear
   and cap the live horizontal amplitude when condition changes. The former is
   cleaner because condition zero becomes a true output gate without erasing
   the physical decay state.
3. Choose and document one mid-note semantic. The proportional, low-ceremony
   contract is: a bank initialized and kept at condition zero is bit-identical
   to the ideal bank; returning to zero neutralizes condition-only output but
   preserves accumulated oscillator history. Remove the claim that it rejoins
   a never-conditioned render. If exact mid-note rejoining is genuinely
   required, the ideal phase must advance independently from a condition-only
   deviation phase; merely rebuilding `step` cannot provide it.
4. Add tests that change condition while a held key rings, while sustain owns a
   released key, after horizontal energy exists, and after both tick layouts
   have run for a nonzero interval.

### C-04 — Correct the EP random-field layout

`field` claims to map a 13-bit field to `[-1, 1)`
([`src/ep_voice.c:191`](../src/ep_voice.c#L191)). Calls using shift 52 can only
read bits 52 through 63 of a 64-bit value: twelve bits. The missing high bit
means the result can never be positive. That malformed field is used for both
pickup offset and mode-3 trim
([`src/ep_voice.c:414`](../src/ep_voice.c#L414)). In the reviewed build, all 73
pickup-offset draws were negative; their reconstructed range was approximately
`[-0.988, -0.002]` instead of spanning both signs.

There are also undocumented exact source correlations: `field(d2, 52)` feeds two
different deviations, and `field(d2, 39)` feeds both a mode trim and horizontal
split. Determinism does not require deviations to share bits.

Fix this by drawing enough words rather than packing past the word boundary.
A restrained implementation would expose `field13(word, slot)` where `slot` is
limited to 0 through 3, generate another `splitmix64` word per key, and assign a
named slot to each independent deviation. Add tests that every full-range bank
has both positive and negative draws, that the observed extrema use a material
part of the requested range, and that unrelated banks are not perfectly
correlated. This changes EP condition signatures, so update pinned hashes once,
in the same focused bug-fix commit.

### C-05 — Preserve note ownership before channels or pitches are merged

The core deliberately stores one `held` bit per physical key. The drivers,
however, discard ownership information before calling it:

- The default live path is channel-agnostic, and `-2` accepts notes from two
  channels but maps both onto the same manual
  ([`driver/main.c:56`](../driver/main.c#L56)). If both channels hold the same
  note, either channel's note-off releases the shared key.
- The offline renderer defaults to all channels and similarly removes the
  channel before dispatch.
- `-f` can fold several source pitches onto one destination key
  ([`driver/render_midi.c:62`](../driver/render_midi.c#L62)). The first folded
  note-off releases or damps that key even if another folded source note is
  still active. Key depth can also be applied to the wrong merged owner.

Keep ownership in the hosted layer. Track active source notes by
`[channel][midi_note]`, maintain a reference count for each destination key,
engage an organ key on the `0 -> 1` transition, and release it on `1 -> 0`.
For EP, every new source note-on may still restrike the tine, but note-off
should engage its damper only when the destination count reaches zero. Define
an aggregation rule for multiple depth owners—maximum depth is the least
surprising—and clear all ownership state on panic. Add interleaving tests for
two channels on one pitch and two pitches folded to one key.

### C-06 — Replace permissive numeric parsing with checked render limits

The renderer's option boundary is not total:

- The `-c` loop casts away `const` and assumes `strtoul` advances. With `-c x`,
  it does not advance and the process loops forever
  ([`driver/render_midi.c:274`](../driver/render_midi.c#L274)). This was
  reproduced with a one-second timeout.
- Channel numbers are masked with `& 15`, so invalid values silently select a
  different channel.
- `-R` maps any non-digit to drawbar 8 rather than rejecting it.
- `atoi`/`atof` cannot distinguish malformed input from zero and accept
  non-finite values. A negative, NaN, infinite, or huge rate/tail can reach
  floating-to-`int64_t` conversions, signed additions, `size_t`
  multiplications, and the final rate conversion
  ([`driver/render_midi.c:381`](../driver/render_midi.c#L381)). Out-of-range
  floating-to-integer conversion is undefined behavior.
- `wav_write_f32` truncates sample and byte counts to 32 bits without checking
  the RIFF limit ([`driver/wav.c:19`](../driver/wav.c#L19)). A long render can
  therefore get a wrapped header even when allocation succeeds.

Use small typed parsing functions based on `strtol`, `strtoul`, and `strtof` or
`strtod`; require an advanced end pointer, complete consumption, no `errno`
range error, `isfinite` for floating values, and a documented range. Parse
channel lists with a separate mutable `end` pointer and reject anything outside
0 through 15. Validate every registration character with an explicit digit
test.

After parsing, compute frame and byte counts with checked addition and
multiplication before allocation or hashing. Set a practical maximum render
duration. Have `wav_write_f32` reject a count not divisible by the channel
count, any byte rate/block alignment that cannot be represented, and any RIFF
payload above its 32-bit limit; RF64 is unnecessary unless files over 4 GiB are
an actual requirement.

### C-07 — Sanitize one sample rate once for the whole instrument

Most initializers replace values below 8 kHz with 48 kHz but accept arbitrarily
large finite values. `tw_rotary_init` alone also rejects values above 192 kHz
([`src/rotary.c:94`](../src/rotary.c#L94)). The top-level initializer passes the
same raw argument independently to every component
([`src/tonewheel.h:410`](../src/tonewheel.h#L410)).

A probe initialized `tw_instrument` at 384 kHz and observed an organ rate of
384 kHz alongside a rotary rate of 48 kHz. One object therefore represented
two different clocks. Very large rates can also overflow time-constant products
to infinity; the shared Taylor helpers then interpret the underflowed zero as a
hostile value and replace it with their maximum coefficient.

Define the supported interval once—currently the documentation points to
44.1–192 kHz, while the code uses 8–192 kHz—and apply one sanitizer before
constructing any subobject. Store or return the chosen rate so hosted callers
can report it. Reuse a single `tw_one_pole_coeff` implementation rather than
the near-identical copies in generator, drive, rotary, EP voice, and EP piano.
Test NaN, infinities, both boundaries, just-outside values, and top-level
agreement between every component.

### C-08 — Make the actual platform contract compile-time visible

The optimized core has no OS, allocation, or libm references, which is a good
result. It is not completely runtime-free: whole-struct zero initialization in
generator, organ, scanner, rotary, EP bank, and EP piano is lowered by both
reviewed compilers to an external `memset`. A bare freestanding target must
supply that symbol even though the Linux build hides the requirement through
libc.

The bit-level helpers also require IEEE-754 binary32: `tw_fabsf`, `ep_field`,
and `ep_sqrtf` reinterpret `float` through `uint32_t` and use binary32 exponent
constants ([`src/tonewheel.h:431`](../src/tonewheel.h#L431)). “Pure C23” alone
does not state that representation requirement. The WAV writer additionally
writes native float bytes and is therefore little-endian/binary32 on the
current x86-64 and Raspberry Pi-class aarch64 configurations, not a
representation-independent writer.

Keep the efficient implementation, but make its boundary explicit:

1. Add compile-time assertions using `<float.h>` for `sizeof(float) == 4`,
   `FLT_RADIX == 2`, `FLT_MANT_DIG == 24`, and the expected exponent range.
2. Document that a bare target must provide the compiler's freestanding memory
   primitives, or provide a tiny target-runtime implementation. Do not replace
   clear aggregate initialization with fragile field-by-field initialization
   merely to hide the generated call.
3. Add an `nm -u` allow-list check for the combined core objects so a future
   compiler change cannot introduce `libm` or another unnoticed runtime call.
4. Either state that hosted WAV output requires little-endian binary32, which
   matches both current targets, or serialize each float explicitly if broader
   portability becomes a real requirement.

### C-09 — Remove stale contracts and dead reporting state

Several current-facing statements already disagree with the code:

- The README reports 9,322 checks while the suite runs 9,393
  ([`README.md:54`](../README.md#L54)).
- The README and `ep73.c` say CC85/91/92/93 are reserved or not wired, while
  all four are dispatched by the driver
  ([`driver/ep73.c:35`](../driver/ep73.c#L35)).
- `stats.reserved` is printed but never incremented, so it is dead state
  ([`driver/ep73.c:26`](../driver/ep73.c#L26)).
- A current EP test comment still says the horizontal component modulates
  rather than adds a pitch, while the reviewed implementation now sums the
  displacement at the pickup
  ([`test/test.c:3659`](../test/test.c#L3659)).
- `ep_bank_silence` promises “everything” reaches exact silence, but it does not
  clear `dc_lp`, and at nonzero condition the bank-level hum/noise floor remains
  active. Either say that it clears only resonator/voice state or reset the
  floor and DC-block state if a literal hard mute is intended
  ([`src/epiano.h:213`](../src/epiano.h#L213)).

Update the MIDI/status text, remove the unused counter, and test the displayed
control map through a small dispatch test. Keep physical derivations and ballot
history in `docs/`; keep C comments focused on local invariants, units,
ownership, and non-obvious ordering. Long research narratives in
`ep_voice.c` and `test.c` already demonstrate their drift risk: a short changed
expression can invalidate a paragraph that reviewers are unlikely to reread.

## Further improvements after the bug fixes

### Add hosted tests without diluting the core suite

The 3,858-line test program has excellent depth for DSP and state behavior, but
the highest-risk hosted boundaries have no regression tests. Split only where
the split earns its keep: retain the current core tests, add a hosted
`test_smf`/`test_wav` binary for parser and writer fixtures, and add a small
driver-state test for MIDI ownership and control dispatch. There is no need for
a test framework dependency; the existing `CHECK` style is sufficient.

Add reproducible targets such as `make test-clang`, `make sanitize`, and
`make analyze`. The current `test` recipe prepends `./` to `$(BUILD)`
([`Makefile:81`](../Makefile#L81)), so an absolute `BUILD=/tmp/...` successfully
builds the test and then tries to execute `.//tmp/.../test`. Invoke
`$(BUILD)/test` directly, and let command-line `BUILD` values support isolated
analyzer builds. Respecting `CPPFLAGS`, `LDFLAGS`, and `LDLIBS` would also make
tooling builds less dependent on overriding `CFLAGS` for link options.

A minimal CI matrix should build and run tests with current GCC and Clang, run
ASan/UBSan on the hosted tests, and compile the aarch64 target when a suitable
toolchain is available. Exact render hashes should remain a separate explicit
check from portable behavioral tolerances.

### Consolidate only helpers that have already drifted

The overall amount of abstraction is appropriate. In particular, keeping
`ep73` as a sibling rather than inventing a generic instrument framework is the
right proportion for two instruments.

Two narrow extractions now earn their keep:

- one internal numeric header for rate sanitization, 0–1 sanitization, and the
  repeated one-pole coefficient;
- one hosted live-I/O unit for validated ALSA setup, buffer ownership, full
  writes, and cleanup.

Avoid generic stage graphs, allocator interfaces, callback-heavy DSP plumbing,
or opaque state solely for architectural appearance. The direct structs and
flat calls are easier to audit and are appropriate for the real-time target.

### State public preconditions explicitly

Core pointer arguments are intentionally unchecked in the sample path, and
audio sample functions generally assume finite input. That is reasonable for a
small real-time core, but the headers should say so. Hosted boundaries should
sanitize external text, files, ALSA results, and MIDI ownership before values
reach those functions. This keeps defensive branches out of per-sample code
without leaving the overall program permissive.

## Baseline implementation order — completed

1. **Contain external memory-safety risk:** extract and bound the SMF parser;
   fix live period ownership and strict startup parsing.
2. **Stabilize the current EP work before pinning more renders:** preserve
   damper state, define condition transition semantics, correct the random-field
   layout, and add the missing live-transition tests.
3. **Fix event ownership:** reference-count merged/folded source notes and
   define depth aggregation.
4. **Unify initialization contracts:** sanitize the sample rate once and add
   binary32/freestanding assertions and symbol checks.
5. **Close the proof gap:** add hosted parser/writer/dispatch tests, sanitizer
   and analyzer targets, then refresh current-facing status text and remove dead
   counters/comments.

Each item can be a focused commit. The first two should be completed before the
hosted tools are treated as safe for arbitrary MIDI files or arbitrary ALSA
devices, and before the current EP condition behavior is considered frozen.

## What should remain unchanged

- Caller-owned, fixed-size core state.
- No allocation, locks, I/O, or OS calls in the sample path.
- Direct module composition and flat control flow.
- Explicit zero/designated initialization.
- Fixed-seed deterministic event and character generation.
- Exact bypass paths where they are part of the regression contract.
- The sibling-core relationship between organ and electric piano unless a
  third real instrument creates demonstrated shared requirements.

These choices give the project most of its current clarity. The recommended
work tightens boundary checks and state contracts without changing that shape.
