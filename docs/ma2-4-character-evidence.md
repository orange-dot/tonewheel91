# MA2-4 — physical-card character

Closed 2026-09-06. Scope: fixed calibration and slow tuning walk on the five
MA2 cards, with an exact character-zero reference. Organ and EP are outside
this slice.

| Task | Status | Closing evidence |
| --- | --- | --- |
| Capture pre-character PCM | done | Five existing benchmark signatures reproduced before edits; source and log under ignored `build/ma2-4-before/`. |
| Seed physical-card calibration | done | Ten independent stable tags per card; 50 independently derived golden draw words in `test/ma_character.c`. |
| Route character and tuning walk | done | Oscillator phase, filter cutoff, six ADSR time destinations and pre-body VCA probes against numeric references. |
| Verify bypass and lifecycle | done | Zero versus five standalone voices; steal, sustain, panic, unison and patch replacement retain calibration and walk. |
| Compiler, sanitizer and symbol gates | done | GCC, Clang, ASan/UBSan/float-cast-overflow, freestanding audit. |
| Cost and dry listening evidence | done | Nine pinned `bench-ma` cases and six repeat-checked `audition-ma2-4` WAVs. |

## Control and lifetime

`ma_card_bank_init[_patch]` assigns each physical slot its calibration and
starts at `.20`. `ma_card_bank_set_character(&bank, amount)` is a session
control: finite values clamp to `[0,1]`, nonfinite values select zero. It is
called on the render thread, like the other bank controls. Positive changes
use the existing 6 ms ramp (8–512 frames); zero bypasses on the next frame.
An independently initialized `ma_synth` remains character-free.

The bank owns `card[].character`; hosts should use the bank setter rather
than rewrite calibration fields. There is no allocation, operating-system
randomness, shared mutable RNG or core libm call. Calibration follows the
physical slot through note reuse, stealing, sustain, panic and unison.
`ma_synth_apply_patch` resets voice DSP but retains the complete character
state, including the clock and any amount ramp. Full bank initialization
restarts all physical clocks and restores `.20`.

Character is not a `.mapatch` field in this slice. Patch files and the
single-card Patchlab retain their existing contract, as for the bank's
unison/session controls. Shared stereo/body remains MA2-5.

## Numeric decisions

The seed, ordinal fold, tags and bounds remain those pinned in
`ma-constants.md` section 11. Each tag is folded independently, so adding a
future destination cannot move existing draws. Sustain tags are reserved;
sustain is a level with no stage duration. Only attack, decay and release
times receive independent fractional biases, in both envelopes. Authored
ADSR controls stay unchanged.

VCO1's static bias and the slow walk move the common pitch reference;
VCO2 receives its additional static bias. Mozaik, Raster and BCS follow
that common reference. Keytracking keeps the played/gliding note position.
Cutoff bias is applied before the existing safety clamp; VCA trim is
applied before the body, DC blocker and safety transfer.

Walk starts at zero. Every 32 public samples it consumes one top-24 signed
SplitMix draw and advances its target by `0.03 * draw` cents, clamped to
`[-3,3]`. The current value linearly approaches that target over the next
32 samples, with an exact endpoint snap. The first draw occurs at sample
32; its first interpolated pitch change occurs at sample 33. The walk runs
while the card is idle and while character is zero. It is sample-clock
defined: at higher sample rates there are more draws per second; this is
not a sample-rate-invariant physical drift model. The step size and
interpolation are MA2-4 implementation decisions, not measured component
behavior.

At zero, character arithmetic is bypassed in each audio destination. Fresh
zero-character renders reproduce the pre-character PCM. Returning to zero
after sounding with character does not rewind oscillator, envelope or
filter history; it removes subsequent deviations immediately.

## Reproduction

Run from the repository root, using compiler paths appropriate to the host:

```sh
make CC=/usr/bin/gcc test
make CC=/usr/bin/clang BUILD=build/clang test
ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang BUILD=build/sanitize TEST_EXTRA= CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test
make CC=/usr/bin/gcc bench-ma
make CC=/usr/bin/gcc audition-ma2-4
```

The listening driver emits `build/ma2-4-{chord,unison}-{000,020,100}.wav`:
8 seconds, 48 kHz float32 mono, fixed gain `sum(cards)/5`. Each uses Tepih,
velocity 100, six seconds held then two seconds of panic release. The chord
is MIDI 48/55/60/64/67; unison is MIDI 48 on all five cards. The only changed
control between corresponding scores is character. Positive edits retain
their normal startup ramp from `.20`; these files have no per-file peak
normalization or added stereo effects. Every file is rerendered and compared
sample for sample before it is written. Musical acceptance remains an
operator listening decision.

## PCM evidence

All five pre-character anchors reproduced before editing and remain the
benchmark's exact zero-character gates. Four new positive-character
anchors are also pinned in `driver/bench_ma.c`:

| Score | Character | FNV-1a64 PCM |
| --- | --- | --- |
| Tepih idle | 0 | `c4c2a0b9a1f22325` |
| Tepih five notes | 0 | `8b94b7d526875ec1` |
| Granica, BCS off | 0 | `fc137a48a372ec55` |
| Granica, BCS on | 0 | `3e5cf262b15baaf1` |
| Lead gestures | 0 | `83b5c4826511163d` |
| Tepih five notes | .20 | `8fc634b744781f75` |
| Tepih five notes | 1 | `7806d75a328f6745` |
| Granica, BCS on | .20 | `026a47c65f63b845` |
| Granica, BCS on | 1 | `9d82f80e3cbfd119` |

Hashes cover raw little-endian float32 PCM with FP contraction disabled.
The two passes of every benchmark case agree on GCC and Clang. They are
PCM reproducibility evidence on this host/toolchain, not a promise of bit
identity across all architectures or floating-point compiler modes.

| WAV stem | FNV-1a64 mono PCM | Peak | RMS |
| --- | --- | --- | --- |
| `ma2-4-chord-000` | `bf8aed151f45404e` | .022338 | .004802 |
| `ma2-4-chord-020` | `81e65840545bee94` | .024813 | .004890 |
| `ma2-4-chord-100` | `0c52a2f01e02b4c0` | .023130 | .004840 |
| `ma2-4-unison-000` | `5b6192337cb48e85` | .038775 | .010647 |
| `ma2-4-unison-020` | `e3cbc5c44dace800` | .036153 | .006472 |
| `ma2-4-unison-100` | `dff943ef2caf7f9e` | .026111 | .005015 |

These are deliberately quiet raw renders, preserving the existing card
gain staging. In zero-character unison, all five cards coincide exactly;
detuning changes their summed level and interference. The files preserve
that level difference, so they are not a loudness-matched preference test.

## Validation and cost

Final aggregate counts on GCC 16.1.1, Clang 22.1.8 and Clang with
ASan/UBSan/float-cast-overflow: core `111378`, hosted `116`, MIDI map `22`,
architecture `227`, character `8700`; zero failures. Both optimized
freestanding symbol audits and all 16 oscillator alias/harmonic cases pass.
Both compilers reproduce all six listening hashes. `git diff --check`
passes. Local logs are `build/ma2-4-{gcc,clang,sanitize}-final.log` and
`build/ma2-4-bench-final.log`; Clang benchmark signatures are in
`build/ma2-4-bench-clang-final.log`.

Fixed state grew from 1704 to 1792 bytes per voice and from 8624 to 9064
bytes per bank on x86-64: **88 bytes per physical card**, including the
amount smoother and walk. There is no heap work in the audio core.

The final GCC benchmark ran on the i7-4600U with `taskset -c 2`, after the
other validation jobs finished. CPU mean and p99 below are microseconds
per 128 frames at 48 kHz, with two fresh passes, 256 warmup blocks and 1024
measured blocks per pass:

| Case | Mean, pass 1 / 2 | CPU p99, pass 1 / 2 |
| --- | --- | --- |
| Tepih five, character 0 | 1127.63 / 1131.85 | 1264.95 / 1321.57 |
| Tepih five, character .20 | 1139.43 / 1136.26 | 1300.69 / 1273.59 |
| Tepih five, character 1 | 1141.05 / 1140.89 | 1275.30 / 1311.66 |
| Granica BCS, character 0 | 1542.88 / 1507.83 | 2304.19 / 1680.51 |
| Granica BCS, character .20 | 1520.48 / 1524.81 | 1704.96 / 1718.61 |
| Granica BCS, character 1 | 1519.31 / 1530.05 | 1712.98 / 1863.35 |

The observed character cost is small beside the existing rendering cost;
run variation, especially the first zero-character Granica pass, prevents a
precise overhead percentage. Granica still exceeds the `1333.33 us`
half-deadline target. This does not close the Raspberry Pi cost gate or
public MA2. Next: MA2-5 card pan and shared mid/side body, followed by the
MA2-6 integration/cost/listening gate.
