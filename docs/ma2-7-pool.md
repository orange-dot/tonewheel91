# MA2-7 — digital voice pool

2026-09-08. Implementation, functional verification, host cost measurements and
listening renders are ready. The complete matrix used 8 warmup + 8 measured
blocks per case so it could finish; the selected worst case was rerun at the
planned 32 + 256 block depth. Operator listening accepted pad, overlap, steal
and unison8. MA2-7 is closed; no realtime capacity claim is made for the
64-slot worst case.

## Contract

`src/mamutanalog.h` exposes concrete, caller-owned `ma_voice_pool` state.
`src/ma_pool.c` owns allocation, expression, sleep and the shared stereo output.
`ma_voice_pool_init` accepts an optional config and patch; NULL selects defaults.
Invalid configuration returns false without touching the destination.

| Setting | Contract |
| --- | --- |
| Physical capacity | 64 slots, always reserved in the struct |
| Active limit | 1–64, default 32; includes release DSP |
| Unison | 1/2/4/8 slots per NoteOn; limit must be divisible by it |
| Detune | Finite 0–100 cents, default ±7; symmetric, zero for unison 1 |
| Session seed | `0x4d41504f4f4c3031` (`MAPOOL01`) |
| Character | Default .20, existing calibration ranges and walk algorithm |
| Steal transition | Rounded 5 ms at the sanitized sample rate; 240 frames at 48 kHz |
| Gain | `1/sqrt(unison)` per slot, then fixed `1/8` before shared body |

Configuration is session state, outside `ma_patch` and `.mapatch`. Limit and
unison changes require reinitialization of a stopped instrument. Init/reset
require stopped rendering. Other API calls belong to the render thread; callers
own event timing. There is no allocation, lock, I/O or internal thread in the
core. Block rendering is repeated stereo tick; dispatch events at the same
sample boundaries for identical results. A zero-length render accepts NULL PCM.

NoteOn returns a monotonically increasing allocation ID, starting at one. Zero
means failure. Velocity-zero NoteOn performs NoteOff and returns zero; velocities
above 127 clamp as in the old synth. Invalid channel/note input is ignored.
After the last `uint64_t` ID, NoteOn fails without changing ownership until reset.
NoteOff returns the oldest held matching channel/note group's ID, or zero.
Sustain is per MIDI channel. Panic clears all pedals and releases held/sustained
groups, preserving shared body/DC tails. Reset clears notes, expression,
transitions, tails and counters, restarts the session sequence, and retains
current patch/macro/output/character settings.

Allocation scans free slots round-robin. If necessary, whole groups are stolen
in released → sustained → held order. Released/sustained ranking uses the maximum
amp-envelope level of each group, then allocation age, then group index. Held
ranking uses age. IDs are unique, so an age tie cannot arise through the API;
ascending group iteration provides the final index tie-break. Every successful
NoteOn assigns all unison members. Independently finished release members may
sleep; their group remains owned until its last member sleeps or it is stolen.

Stealing replaces the slot DSP immediately and crossfades its last mono VCA
contribution to the new contribution over 5 ms. The first transition sample is
exactly the previous contribution. No old oscillator/filter continues outside
the limit. Any freed slot not reused immediately fades its captured contribution
to zero. A second steal starts from the currently audible interpolated value.

Slot character derives from session seed and physical index, independently of
allocation order. Wake preserves calibration and walk state, copies current
controls into cleared voice DSP, and seeds oscillator/Raster phases and noise
from seed, index and allocation ID. Both VCOs receive the unison detune. Held and
sustained voices always run full DSP, including a zero envelope. Released voices
sleep only at the existing exact `MA_ENVELOPE_IDLE`; sleeping oscillator, filter
and walk histories remain frozen. Wake resets filter, source and envelope
histories, and immediately resolves current patch, channel bend/pressure,
modwheel and macros. Poly pressure updates existing matching notes only; it is
cleared on a new allocation.

`set_patch` preserves active envelopes/ownership/character and shared output
history, with the existing control ramps. `set_output` and shared macro-derived
output controls live in the pool's control template. This template ticks controls
only, never voice DSP, regardless of slot 0 or other activity. Channel expression
is per voice; it does not select one channel to drive the shared chassis.

Pan bases span −.75 to +.75 across the configured limit (a one-slot pool is
centered). Width/identity dispersion and crossfeed follow the old bank transfer.
Width zero is immediate exact dual mono even after stereo DC history. Summation
is in slot order through one body/DC/safety output, at fixed gain independent of
note count and limit. Output diagnostics retain pre/post-safety peaks, knee
interventions, sanitizations and maximum reduction. Pool diagnostics count active
and peak DSP slots, stolen groups per category, natural sleeps and allocations
that wake a DSP slot (including replacements).

The five-card bank, synth layout, patch format and existing arrangements remain
reference behavior. Pool PCM intentionally differs from always-running cards.
The shared voice helper adds detune only to the new entry point. Fixed-size
voice copying at wake uses volatile byte reads to prevent compiler-generated
`memcpy`; the existing symbol audit still allows only `memset`.

## Functional verification

`test/ma_pool.c` covers configuration rejection, every limit and unison mode,
full allocation/overflow, complete-group ranking, released and sustained ties,
repeated notes and MIDI channels, sustain/panic, ID exhaustion, partial release
completion, randomized ownership invariants, sleep/wake identity and reset,
controls during silence, phase seeds, patch continuity, exact mono, pan symmetry,
fixed contribution gain, full MIDI range, hostile controls and 44.1/48/96/192 kHz
finite bounded output. Tick versus uneven block splits is checked through note
replacement and panic boundaries. Each unison PCM scenario runs twice.

Pinned new functional hashes (48 kHz, eight-slot Granica event script):

| Unison | FNV-64 |
| --- | --- |
| 1 | `330b2b6d9f8f30a5` |
| 2 | `71d5cec193f3ee93` |
| 4 | `ac05ed8c196f0d3b` |
| 8 | `8bc31ab3650c18f1` |

Validation commands (no host timing or exhibit rendering):

```sh
make CC=/usr/bin/gcc prepare-ma2-7 test
make BUILD=build/ma2-7-clang CC=/usr/bin/clang prepare-ma2-7 test
ASAN_OPTIONS=detect_leaks=0 make BUILD=build/ma2-7-sanitize \
  CC=/usr/bin/clang TEST_EXTRA= \
  CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' prepare-ma2-7 test
```

GCC and Clang optimized builds include the unchanged freestanding symbol audit.
The aggregate suites retain their old PCM assertions. Build/test logs are local
`build/ma2-7-{gcc,clang,sanitize}.log`; generated logs and audio are not committed.

## Host measurements and listening

Build the prepared tools with `make CC=/usr/bin/gcc prepare-ma2-7`. The benchmark
and exhibit require explicit run/render flags.

First ensure no tests, renders or competing benchmarks are running. Record CPU
model, governor, compiler flags, kernel and load context. Select a permitted CPU
from `taskset -pc $$`; use its concrete index below. Do not infer affinity from
an old run. The benchmark records the actual affinity and compiler itself.

```sh
MA_POOL_WARMUP=8 MA_POOL_BLOCKS=8 taskset -c CPU build/bench_ma_pool --run > build/ma2-7-cost.csv
build/exhibit_ma_pool --render build/ma2-7 > build/ma2-7-listening.log
```

For a deep tail check, constrain the matrix and restore the planned sample
count, as used for the worst case here:

```sh
MA_POOL_PATCH=Granica MA_POOL_BCS=1 MA_POOL_LIMIT=64 MA_POOL_FRAMES=128 \
MA_POOL_SCENARIO=4 MA_POOL_WARMUP=32 MA_POOL_BLOCKS=256 \
taskset -c CPU build/bench_ma_pool --run > build/ma2-7-worst-256.csv
```

The benchmark is a serial matrix: limits 16/32/64, frames 128/256/512 at 48 kHz,
Tepih/Lead/Raster/Prizma/Granica, BCS off and Granica's BCS amount/regime on each
patch. Scenarios are 0/1/8/full slots, full sustain→release, and full unison 2/4/8.
Every case initializes afresh and runs twice with matching PCM required.
The completed full matrix used `MA_POOL_WARMUP=8 MA_POOL_BLOCKS=8`; event dispatch, hashes
and output reporting are outside block timing. CPU/wall mean, p50, p99 and maximum,
CPU/audio-time ratio, active range, state size, process RSS high-water before/after,
page-fault deltas, safety diagnostics and PCM hashes are emitted. RSS is process
high-water, not isolated pool allocation; p99 has only about three tail samples
per pass. Repeat/extend the block count if a live configuration decision needs
stronger tail evidence. Timings include clock-call overhead, visible in idle.
The release scenario combines sustained and decaying intervals and reports the
observed active range; it is not a constant-polyphony cost row.

Compare wall tails against 2666.67/5333.33/10666.67 µs at 128/256/512 frames.
CPU/audio ratio above one indicates insufficient sustained DSP throughput;
a larger buffer does not fix that. Record what actually fits this host. A
32-voice realtime result is not required to close MA2-7. If target polyphony
misses, open a measured, concrete performance task before MA3 live acceptance.
Do not lower oversampling, solver quality or polyphony automatically.

The full CSV contains 1,440 rows (five patches, BCS off/on, limits 16/32/64,
three block sizes, eight scenarios and two passes). All 720 pass pairs have
matching hashes. Host was `192.168.1.16`, x86-64, kernel
`7.1.8-200.fc44.x86_64`, GCC 16.1.1, CPU affinity `0`, and pool state size
121,768 bytes; RSS was 4,476–4,496 KiB with no major faults. The worst short-pass
ratio was 14.83 CPU/audio at Granica BCS, limit 64, 128 frames,
sustain-release. A focused 32+256 run of that case produced matching
`b9a60a080718203d` hashes, CPU/audio ratios 9.192 and 8.742, CPU p99 values
40,219 and 33,410 µs, and wall p99 values 44,634 and 34,458 µs. The 128-frame
realtime budget is 2,666.67 µs, so the 64-slot worst case is not realtime on
this host. The 8-block full matrix and focused CSVs are local build artifacts.

The separate exhibit produces four 12-second stereo cases: long Tepih pad,
release-overlapped chords, an eight-slot Lead stealing sequence, and 8-way
poly-unison with replacement. It rerenders each and requires exact PCM equality.
It writes both raw and listening WAVs, preserving the raw fixed-gain behavior.
Listening copies use one constant gain per file to match whole-file stereo RMS:
target −22 dBFS, lowered uniformly if necessary to keep every peak at or below
−3 dBFS. Logs record original RMS/peak, gain and matched RMS. This alignment
permits timbre/continuity comparison; it is not a loudness standard or a verdict.

The four renders completed twice with exact PCM equality. Raw results were:

| Case | Hash | RMS | Peak | Matching gain |
| --- | --- | ---: | ---: | ---: |
| pad | `80ae605c7e344d03` | −40.345 dBFS | −24.112 dBFS | +18.345 dB |
| overlap | `79e2fa9a1671ff12` | −39.974 dBFS | −24.112 dBFS | +17.974 dB |
| steal | `956c55eef1bd4f01` | −36.761 dBFS | −22.101 dBFS | +14.761 dB |
| unison8 | `145ec46f65b3eccf` | −40.297 dBFS | −24.920 dBFS | +18.297 dB |

Listening copies target −22 dBFS whole-file stereo RMS and remain below −3 dBFS
peak. Operator listening accepted pad texture, chord overlap, steal continuity,
unison center and stereo width at the documented alignment. The cost report and
listening evidence close MA2-7. Any attempt to make the measured 64-slot
worst-case realtime belongs in a separate performance task before MA3 live
acceptance.
MA3 effects and future GFM must use `pool.config.limit` and awake/group ownership,
not `MA_CARD_COUNT`. Historical MA2 remains closed as the five-card reference.
