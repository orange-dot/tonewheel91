# MA2-5 — card pan and shared body

Closed 2026-09-06. The implementation follows `ma-constants.md` sections
8, 11 and 12, with the decisions below filling the remaining stereo details.

| Task | Status | Closing check |
| --- | --- | --- |
| Raw post-VCA card output | done | Legacy one-card and five-card PCM anchors remain exact. |
| Deterministic pan and side controls | done | Equal-power reference, monotonic slots, zero-width mono, smooth edits. |
| Shared body/DC/safety | done | One body pass on mid, side retained, bypass state untouched, bounded output. |
| Stereo regression suite | done | Numeric routing, identity, lifecycle and hostile-input checks on GCC/Clang/sanitizers. |
| Listening and cost evidence | done | Eight repeat-checked WAVs, correlation, twelve pinned benchmark cases and whole-bank timing. |

## Interface and routing decisions

`ma_card_bank_tick_stereo` is the new summed instrument output. It renders
each card once through oscillator/filter/envelope/VCA, then pans the raw
samples, sums in slot order and runs one shared output stage. The existing
`ma_card_bank_tick` remains the historical per-card MA1 output used by older
exhibits and pinned PCM references. Choose one render entry point for a
bank's lifetime; initialize a fresh bank to change output topology.

The bank is single-timbre. Card 0 owns the shared output settings and their
resolved identity/performance overlays; hosts apply common macros and
performance controls to all cards. `ma_card_bank_set_output` updates the
existing output controls on every card. Shared settings start from the
sanitized patch. Per-card pitch/ADSR/character remain independent.

The raw bus is a sum, with no division by active voice count or five-card
normalization. Master gain follows the shared body and DC blockers. A lone
center card can therefore be compared directly with the MA1 output path.

Pan uses `q=clamp(base*effective_width*(1+.10*dispersion),-1,1)` and the
pinned sqrt(2)-normalized sine law. The `.10` dispersion depth is an MA2-5
decision; offsets are bounded to ±.075 and smoothly follow resolved spatial
dispersion. The offset shrinks with width for continuous reopening from
mono. Existing identity width/crossfeed/body-load equations remain
active. No GFM pan offset is added before MA4. Coefficients are cached and
recalculated only as width or dispersion changes.

Width shapes side through pan positions; it is not multiplied into side a
second time. Crossfeed applies `side *= 1-effective_crossfeed`, preserving
mid. Literal direct width zero overrides spatial offsets and returns the
raw mono sum. On transition to mono, the two DC trackers are merged by
their mean so prior stereo history cannot leave a residual side signal.
Positive width/crossfeed/dispersion edits use existing control smoothing.

The shared body receives `4*body_load*mid`, with output scaled by `.25`.
Effective body drive zero bypasses both scales and preserves body state.
Stereo is reconstructed, DC-blocked per channel, master-scaled and passed
through the existing safety curve. Panic releases cards and preserves the
shared chassis/DC state so tails complete normally.

## Reproduction and evidence

Run from the repository root:

```sh
make CC=/usr/bin/gcc test exhibit-ma1-osc
make CC=/usr/bin/clang BUILD=build/clang test exhibit-ma1-osc
ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang BUILD=build/sanitize TEST_EXTRA= CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test
make CC=/usr/bin/gcc audition-ma2-5
make CC=/usr/bin/gcc bench-ma
```

The native suite now includes `test/ma_stereo.c`: 2611 checks. Numeric
references cover 129 width settings against double-precision sin/cos,
constant pan power, exact centered-card MA1 equivalence at four sample
rates, raw-output state isolation, one shared body call, mid-preserving
crossfeed, mono transitions after stereo DC history, bounded identity pan,
control sanitization, panic/unison state retention and a full 128-note
steal sweep. The sweep checks finiteness before and after shared body and
safety; no sanitization is needed for normal rendered signals.

GCC, Clang and ASan/UBSan/float-cast-overflow aggregate counts are core
`111378`, hosted `116`, MIDI map `22`, architecture `227`, character `8700`,
stereo `2611`; zero failures. Both optimized freestanding symbol audits
and all 16 source alias/harmonic cases pass. Logs are under
`build/ma2-5-{gcc,clang,sanitize}.log`.

GCC and Clang agree on all twelve benchmark signatures and all eight WAV
PCM hashes. Each compiler's renders repeat exactly from fresh state. These
are little-endian float32 PCM anchors with FP contraction disabled, not a
claim of bit identity under arbitrary architectures or compiler FP modes.
`git diff --check` passes.

All nine historical benchmark signatures from MA2-PERF/MA2-4 remain exact.
Three new shared-stereo signatures are pinned in `driver/bench_ma.c`:

| Score | Character | FNV-1a64 stereo PCM |
| --- | --- | --- |
| Tepih, five notes | .20 | `1a254c3750cc209f` |
| Granica, five notes, BCS on | .20 | `6b4ddec5f3916af7` |
| Lead gesture/sustain/unison/panic | .20 | `028a8d6205d46ad5` |

## Listening files

`audition-ma2-5` writes eight 6-second, 48 kHz float32 stereo WAVs under
`build/`. Every case starts from a fresh Tepih bank at character `.20`,
velocity 100, with four seconds held then two seconds of panic release.
One card uses slot 0 / MIDI 48; three use slots 0/2/4 / MIDI 48/60/67;
five use slots 0..4 / MIDI 48/55/60/64/67. Master stays at the patch value;
there is no normalization or added chorus/reverb. Two independent passes
must match sample for sample before each WAV is written.

Filenames have the form `ma2-5-N-cards-width-WWW-body-DDD.wav`.
Both peaks are measured around safety, after master; correlation is Pearson
L/R over the complete file. These renders have no safety knee hits.

| N | Width | Body | PCM hash | Pre/post peak | RMS | L/R correlation |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 | .10 | `fb9e1b3c41f2a485` | .039781 | .010233 | 1.000000 |
| 1 | .70 | .10 | `6874175ee8b60b2f` | .052836 | .010237 | .997298 |
| 3 | 0 | .10 | `257ffc2f4aa554e9` | .081828 | .017565 | 1.000000 |
| 3 | .70 | .10 | `1b1bab0307ae6110` | .081209 | .017560 | .784970 |
| 5 | 0 | .10 | `6b2ce222157c3c21` | .121800 | .023215 | 1.000000 |
| 5 | .70 | .10 | `256b1aba6bfbc8dc` | .119128 | .023182 | .841193 |
| 5 | 1 | 0 | `8fa906539636828c` | .120591 | .023113 | .689916 |
| 5 | 1 | .80 | `aa626d6b3d58ce62` | .113681 | .023342 | .695964 |

These demonstrate reproducible stereo routing and the shared body. They do
not record an operator listening verdict or close the broader MA2 gate.

## Cost and remaining gate

Fixed state on x86-64 is 1808 bytes per card and 9328 bytes per bank. Relative
to MA2-4 this adds one 16-byte dispersion smoother per card plus 184 bytes
of shared output/pan state, or 264 bytes per bank. Audio rendering allocates
nothing and the optimized core still requires only the allowed `memset`
runtime primitive.

The final GCC 16.1.1 run used the i7-4600U with `taskset -c 2`, after the
other validation jobs completed. At 48 kHz: 128-frame blocks, 256 warmup
blocks and 1024 measured blocks, two fresh passes. CPU times below are
microseconds per block. Logging, event dispatch and PCM hashing are outside
the timed region; the stereo cases include all five raw cards, pan, shared
body, DC block and safety.

| Shared stereo case | Mean, pass 1 / 2 | CPU p99, pass 1 / 2 |
| --- | --- | --- |
| Tepih five, character .20 | 1122.05 / 1139.53 | 1438.95 / 1494.14 |
| Granica five, character .20 | 1504.45 / 1489.68 | 1901.46 / 1748.41 |
| Lead gesture, character .20 | 1156.16 / 1155.28 | 1397.96 / 1424.82 |

Logs: `build/ma2-5-bench-gcc.log` and `build/ma2-5-clang-render.log`.
Earlier per-card scenarios remain in the benchmark for PCM continuity;
their timing is not the cost of the new stereo topology. Host variation
does not justify a precise speedup claim. Granica's mean and all three
cases' observed p99 exceed the `1333.33 us` half-deadline target. Raspberry
Pi acceptance remains open. MA2-6 owns the combined allocator, compass,
stereo, cost and listening gate; MA2-5 alone does not close public MA2.
