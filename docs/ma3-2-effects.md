# MA3-2 — shared chorus and reverb

2026-09-08. Implementation, functional verification and operator listening
acceptance are complete (operator: “odlicno je ovo”). MA3 live acceptance and host
throughput remain separate MA3-LIVE/MA3-PERF tasks.

## Signal and storage contract

`ma_voice_pool_tick/render` now runs one shared `ma_effects` after the existing
body/DC/safety output: chorus, then reverb. Slot count, unison gain, character,
oscillator/filter quality and patch files are unchanged. The five-card bank
and individual `ma_synth` have no effects state and retain their PCM behavior.
Pool width/crossfeed still control the dry stereo image; enabled wet effects
can create stereo from a mono dry signal. Disable both or use both mixes zero
for the previous exact dual-mono width-zero contract.

The port follows `mamut-sint-sw` commit
`d7672912706731b73839d1fc25801669450fd0f1`,
`crates/mamut-dsp/src/fx.rs`, SHA256
`deec76ceb4269370718cbc9bd6162c206487b66d2d0a2ff7fa71f961a1517bfc`.
Tap, feedback, diffusion and gain constants remain those pinned in
`ma-constants.md` section 13. Chorus sine uses the existing freestanding
polynomial, so donor behavior is preserved without a Rust PCM identity claim.

All four buffers are caller-owned, capacity 15360/channel for chorus and
65280/channel for reverb; active lengths round 80/340 ms at the initialized
rate. Supported rates are 44100–192000 Hz; invalid rate uses the existing
48000 Hz fallback. Sizes on the supported x86-64 host:

| State | Bytes |
| --- | ---: |
| Four sample buffers | 645120 |
| `ma_effects` including controls/diagnostics | 645208 |
| Entire `ma_voice_pool`, all 64 slots and effects | 766976 |
| Individual `ma_synth` (unchanged) | 1808 |

The complete pool remains below 1 MiB. Hosted MIDI rendering keeps its large
renderer state in static storage. Effects do no allocation, I/O, locking or
thread creation. Re-enabling clears the stage in its setter, between frames;
this bounded buffer-clear cost still needs the future live budget measurement.

## Controls and lifecycle

Public API: `ma_effects_init/reset`, `ma_effects_set_chorus`,
`ma_effects_set_reverb`, `ma_effects_tick`. The pool owns `pool.effects`; callers
apply its setters between frames on the render thread. Initialization/reset
of the instrument belongs to the stopped-instrument boundary.

| Control | Bounds | Initial value |
| --- | --- | --- |
| Chorus enabled / mix | bool / 0–1 | false / 0 |
| Chorus depth / rate | 0–1 / .03–20 Hz | .35 / .25 Hz |
| Reverb enabled / mix | bool / 0–1 | false / 0 |
| Reverb size / damping | 0–1 / 0–.99 | .65 / .55 |

Nonfinite setters choose the lower bound; finite controls clamp to bounds.
Disabled stages return incoming bits and freeze their histories. A false→true
setter clears only that stage's history/phase. Setting controls while enabled
preserves history, including when mix becomes zero. Enabled mix zero advances
history but returns incoming bits exactly. Thus mix zero→positive rejoins the
running tail, whereas disabled→enabled starts clean. There is no crossfade on
explicit parameter edits; these are direct donor controls.

Panic releases voices and pedals, preserving effect tails. Effects keep
running when every slot sleeps. Pool reset preserves effect controls but
clears all effect history/phase and diagnostics. A patch/macro edit does not
alter effects. `ma_effects_reset` clears just effects with the same retention
rule. There is no effect macro routing.

Wet outputs and feedback writes sanitize nonfinite samples, flush below
`1e-20` and clamp to ±1. Diagnostics count those writes and outputs together:
`clips`, `sanitizations`, `pre_peak`, `post_peak`; CLI reports them separately
from existing body safety diagnostics. Exact bypass deliberately preserves
input bits even for hostile standalone input; the integrated dry pool already
provides finite bounded input.

The donor reverb is a bounded nonlinear feedback network, not an RT60 model.
At maximum size, feedback times `(1 + diffusion)` exceeds one at DC; its
sanitizer bounds it, but no finite time-to-silence is promised for every
control combination. The prepared listening size .65 stays below that loop
gain. The pinned donor coefficients are retained; any decay-law change needs
its own documented comparison.

## MIDI and hosted rendering

CC107/108 enable chorus/reverb and set mix to value/127. Value zero keeps the
stage enabled and history running. Only selected MIDI channels can change
these global controls. Other effect parameters remain API/CLI controls.
CLI mix options enable the stage even with value zero; parameter-only options
do not enable it. Events later in the MIDI file override the initial CLI mix.

```sh
make check-ma3-2
build/render_midi -I mamutanalog --patch Tepih --voice-limit 16 \
  --chorus-mix .30 --chorus-depth .35 --chorus-rate .25 \
  --reverb-mix .25 --reverb-size .65 --reverb-damping .55 \
  -t 4 -o build/mamut-wet.wav input.mid
make audition-ma3-2
```

New options reject nonfinite/out-of-range input and other instrument targets
before touching the output file. Default rendering remains dry. No automatic
panic is inserted at EOF and `-t` remains the caller's tail duration.

## Functional evidence

`test/ma_effects.c` covers exact frozen bypass (including float bits), enabled
mix-zero history, stage-local clear/rejoin, independent analytic impulse tap
and gain anchors, silent voices with live FX tails, panic/reset/patch retention,
hostile controls and input, and repeat-verified bounded output at
44.1/48/96/192 kHz. Wet direct-pool/MIDI block sizes 1/17/128/512/4096 give the
same pinned FNV64 `2a673b682c7a09a3`. Eight effect stress hashes are pinned in
the test. Full MIDI compass testing also enables both effects.

The actual executable test compares an SMF containing CC107/108 against the
same notes with equivalent CLI mix values and varying blocks; exact mix-zero
dry identity, hostile options and legacy organ/EP hashes remain checked.
The dry MA3-1 SMF twin remains `fbe7ee8dede78c92` and MA2-7 pool anchors remain
unchanged. Full verification commands are `make test`, `make test-clang`,
`make sanitize`; no timing or live capacity result is inferred from them.

## Listening ballot

`audition-ma3-2` writes two sets (Tepih and Granica) of four 8-second arms:
dry, chorus-only, reverb-only and combined wet. Each arm repeats the same
four-note chord, sustain/release, overlapping transposed chord and panic at
4 seconds, then renders another 4 seconds. Limit 16, unison 2, 48 kHz,
unchanged session seed. Prepared wet controls are the .30/.25 mix values
above; these are provisional until the operator retains or adjusts them.

Each raw render is repeated and compared bit-for-bit before writing WAVs.
`build/ma3-2_{tepih,granica}_{dry,chorus,reverb,wet}_raw.wav` preserves native
gain. `_listen.wav` uses one fixed gain per file, matching stereo RMS over
the active 0–4 second interval within each patch set. Target is −22 dBFS,
lowered for the entire set if needed to keep every full-file peak ≤−3 dBFS.
The tail does not determine the matching gain. There is no dynamic gain.

Operator review: compare `_listen.wav` within each patch set for retained
attack/source identity, useful stereo finish and unobtrusive tails; check the
raw files for native gain behavior. Verdict: **accepted, 2026-09-08**; retain
the prepared settings. This is the new MA3-2 verdict, separate from MA2-7.

Rendered successfully with GCC on 2026-09-08 after all tests finished, with
no concurrent test/render job. All eight raw pairs are bit-identical. WAV
readback confirms 384000 stereo float32 frames at 48 kHz per file, finite
samples and −22 dBFS active RMS within .00001 dB. Matched peaks range from
−9.870 to −6.745 dBFS; no peak constraint lowered the RMS target. Dry/chorus
files end in exact silence; reverb/wet final 100 ms RMS is below −76 dBFS.
Files have a fixed 8-second endpoint, with no editorial fade.

| Patch | Dry | Chorus | Reverb | Combined |
| --- | --- | --- | --- | --- |
| Tepih | [listen](../build/ma3-2_tepih_dry_listen.wav) | [listen](../build/ma3-2_tepih_chorus_listen.wav) | [listen](../build/ma3-2_tepih_reverb_listen.wav) | [listen](../build/ma3-2_tepih_wet_listen.wav) |
| Granica | [listen](../build/ma3-2_granica_dry_listen.wav) | [listen](../build/ma3-2_granica_chorus_listen.wav) | [listen](../build/ma3-2_granica_reverb_listen.wav) | [listen](../build/ma3-2_granica_wet_listen.wav) |

Native raw signatures and exact matching gains are logged in `renders.md`;
local complete logs are `build/ma3-2-listening.log`, `build/ma3-2-gcc.log`,
`build/ma3-2-clang.log` and `build/ma3-2-sanitize.log`. Final focused rebuilds
use corresponding `*-final.log` files. All compiler/sanitizer runs pass with
no warnings, as do both freestanding symbol audits. Generated WAVs/logs stay
outside version control.
