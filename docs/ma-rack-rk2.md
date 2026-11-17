# RK2 — chorus and reverb as blocks, delay, saturator, compressor

2026-11-16. Plan and decisions: `docs/ma-rack-plan.md` (RKD9, RKD27..RKD35).
Committed on top of `d910be0`. The functional gates
pass on GCC, Clang and the sanitizer build, and the dry and wet paths stay
inside the refactor gate of `bench_ma3_perf`; listening waits for the
operator.

## What landed

Core (`src/ma_rack.c`, `src/ma_effects.c`, declarations in
`src/mamutanalog.h`):

- **The pool's chain is `body -> DC block -> rack -> master -> safety`**
  (RKD9). `ma_voice_pool` holds no effects any more and is 319 kB (it was
  964 kB). Dry renders are bit-identical to `d910be0`.
- **Five new kinds** in the closed set: `chorus`, `reverb`, `delay`,
  `saturator`, `compressor`. A chain holds at most one chorus, one reverb
  and two delays (RKD8); `ma_rack_spec_valid` enforces it.
- **Chorus and reverb** run the MA3-2 kernels of `ma_effects.c`, now
  written once for the rack's blocks and for the standalone `ma_effects`
  pair. The slot's mix is the kernel's own mix law (RKD27). The lines are
  never cleared (RKD7): a restart puts the write index at zero and counts
  the frames written since, and an older entry reads as zero. The bound on
  the lines is ±64 for the rack's blocks, which sit before master; it stays
  ±1 for `ma_effects`.
- **Delay**: two lines of 2^19 frames per rack. Free time 1 ms to 2.7 s, or
  one of 13 note values of the host tempo; feedback to .95 through a high
  cut and a low cut; ping-pong; a time change is followed with a 50 ms time
  constant (RKD30). `ma_rack_set_tempo` takes the host's tempo.
- **Saturator**: tilt, input gain, the `tw_drive` stage (its triode curve,
  its odd kernel, or a blend), the input gain taken back out, tilt, output
  gain (RKD31).
- **Compressor**: peak or RMS detector on both channels, threshold, ratio,
  soft knee, attack, release, makeup (RKD32).
- **Input gate** (RKD28): a chorus, reverb or delay coming out of a bypass
  takes its input into the line over 10 ms.
- **Session helpers** (RKD29): `ma_rack_spec_set_chorus`,
  `ma_rack_spec_set_reverb`, `ma_rack_spec_set_effects`.
- `ma_rack` carries its own lines and is 9.06 MB. `ma_rack_init` clears the
  small state only (RKD34).

Hosted:

- `.mapatch` **version 9** and **`wbpatch=3`** for a rack with an RK2 kind
  (RKD33). Sync and detector are written as words.
- `ma_midi_map` owns a rack. CC107 and CC108 set the mix of its chorus or
  reverb block and add the block when the chain has none. `ma_midi_render`
  settles the rack before the first frame and takes an optional tempo
  track.
- `render_midi`: `--chorus-mix` and `--reverb-mix` set or add the same two
  blocks after the `--rack` file's own; the file's tempo events reach the
  rack (`smf_make_tempo_frames`).
- `ma3_show` (the MA3 exhibits), the Workbench desk and its live path: one
  rack per pool. The Workbench plays the patch's rack with the part's sends
  (`wb_part_rack`); before, it carried the patch's rack and did not play it.
- `bench_ma3_perf`: the effect arms are rack blocks; `MA3_PERF_RACK=rk2`.
- Patchbench: `docs/patchbench-progress.md`, "RK2 kinds in the rack row".

## Signatures

Dry: every pinned Mamut signature is unchanged. The printed dry hashes of
the baseline run at `d910be0` and of this tree agree: pool unison 1/2/4/8,
the MA3-1 SMF twin `77b6939786ac9661`, the rack test's Tepih pair
`2ea2d2643a744455` / `f0951b343b271ab1`, the Patchbench engine parity
`cfa5a2329dacba61`, bounce parity `f2b0813223e7cf07` and
`c1f56d26b168556f`, the rack bounces `d5a43458345a95ab` /
`ba28bc8d65fafff9`.

The kernels: the eight pinned `ma_effects` stress hashes (four rates, benign
and hostile input) are unchanged, so the rewrite for two hosts, the restart
count and the gate at 1 did not move a bit.

Wet, re-pinned once (RKD9, operator-confirmed 2026-11-11):

| Pin | Before | After |
|---|---|---|
| `test/ma_effects.c` MIDI/direct wet twin | `eca2f01ea356eda1` | `4654fccc1c01a1db` |

That twin moves its chorus and reverb mixes with CC107/108 in mid-render, so
it also hears the 6 ms mix ramp and the start from an empty line. Other wet
values are compared against a referee, not pinned: the Patchbench channel
parity (chorus and reverb on two channels) is `fe97148a4a7292c1` on one and
on two threads and equals four pools with their own racks, built from the
core's calls and ticked frame by frame.

How far a settled wet render moved: `progression.mid` on Tepih with
`--chorus-mix .30 --reverb-mix .25`, rendered by `render_midi` of `d910be0`
and of this tree. The two files differ by 2.4e-9 RMS, 142 dB below the
signal, and by 3.0e-8 at most. Chorus and reverb are linear below their
bound, and master is a gain, so moving them from after the safety to before
master changes only the rounding while the safety knee is not touched.
Logged FNVs of earlier wet renders (`renders/`, the exhibit documents) no
longer reproduce bit for bit from this tree.

## Gates

All on this host (x86-64-v3; GCC 16.2.1 with LTO, Clang 22.1.8,
`make sanitize`).

| Gate | Result |
|---|---|
| Empty and bypassed chain | `test/ma_rack.c`: Tepih, Lead and Granica, dry and wet, with no rack, an attached rack, an empty spec and a spec of neutral, disabled and mix-0 blocks (a delay at mix 0 among them): bit-identical. Rack level: chorus at mix 0, reverb disabled, two delays bypassed, a saturator at drive 0, a compressor at ratio 1 and one far under its threshold: every frame's bits, −0 included, through `ma_rack_tick` and `ma_rack_process`, and no bypassed block ran its DSP. |
| Chorus and reverb against the kernel | The two blocks in place against `ma_effects` on the same input: 2,280,600 frames at 44.1, 48, 96 and 192 kHz and three settings, 0 mismatches, equal meters. Past ±1 the two differ by design (ceiling ±64 against ±1). |
| Delay | An impulse returns after exactly time × rate frames, whole; nothing before or between echoes; later echoes fall by more than the feedback. Ping-pong: left .5, then right, then left, no stray frame. A dotted eighth at 120 is 18,000 frames at 48 kHz, and 24,000 at 90 once the distance has followed. A whole note at 20 clamps to 524,286 frames. After mix 0 and back, silence in gives exact silence out (RKD7). Two delays hold two lines, and a kept delay keeps its line through a structural change. |
| Saturator | Small-signal gain within 0.05 dB at full drive for both curves. Harmonics below. Tilt +8 dB: −4.00 dB at 20 Hz, 0.00 at the pivot, +4.00 at 20 kHz, each within 0.1 dB; the opposite tilt after the idle stage leaves less than 0.001 dB at six frequencies. A silent channel stays exactly silent. Bounded on +40 dBFS noise. |
| Compressor | Steady 1 kHz sines from −50 to 0 dBFS against the static curve, knees 0 and 12 dB: worst deviation **0.023 dB**. Under the threshold the frames pass untouched. Attack 10 ms reaches 63 % in 14.6 ms and release 150 ms falls to 37 % in 170 ms (both include the 10 ms RMS window); the gain change returns to exactly 0 dB. Ratio 20 with the peak detector holds a −6 dBFS sine at the threshold. Both channels get one gain. Finite on hostile input. |
| Structure without clicks | Insert, reorder, replace and remove with the RK2 kinds on a 220 Hz sine, five changes: the largest frame step is 0.00721 against a steady 0.00720 (bound: steady + 10 %). |
| Sweeps | A random valid spec of any kinds every 64 frames with tempo changes, 400k frames of full-scale noise at 44.1 and 192 kHz: finite, 0 sanitizations. |
| Block against frame | `ma_rack_process` against `ma_rack_tick` with random chains of every kind: 143,166 frames, 0 mismatches, 321 EQ wavefront runs. Through the pool: the block render, the per-frame tick and a second run of a five-kind chain agree (`af74be71b04126b1`). |
| Session helpers, caps, files | The helpers (a send at zero, order, the patch's own block, clamps, closing gaps, a full rack) and the instance caps, in `test/ma_rack.c`. `.mapatch` version 9 round trip with every kind and 8 malformed cases; a version 8 file refuses an RK2 kind; two choruses are not written (`test/hosted.c`). |
| MIDI and CLI twins | `test/ma_effects.c`: direct events equal the renderer at block sizes 1, 17, 128, 512 and 4096. `test/ma3_cli.py`: CC107/108 at frame zero equal `--chorus-mix` / `--reverb-mix`; mixes of zero equal dry. |
| Core symbol audit | `check-core-symbols` passes; both audio-thread audits pass. |
| Full suites | See "Suites" below. |

### Saturator harmonics

220 Hz sine, input gain +20 dB (the initial value), 48 kHz. Level, drive,
curve: change of the fundamental, second and third harmonic in dB relative
to the fundamental.

| Level | Drive | Triode: H1, H2, H3 | Odd: H1, H2, H3 |
|---|---:|---|---|
| −28 dBFS (a note's peaks) | .25 | −0.02, −50.1, −81.8 | −0.03, −49.9, −68.4 |
| | .50 | −0.02, −44.5, −85.8 | −0.08, −46.7, −57.2 |
| | 1 | −0.05, −35.1, −64.3 | −0.53, −30.7, −39.5 |
| −20 dBFS (a chord's peaks) | .25 | −0.03, −42.1, −92.9 | −0.12, −43.4, −52.6 |
| | .50 | −0.04, −36.4, −66.2 | −0.40, −33.1, −41.8 |
| | 1 | −0.17, −26.8, −47.8 | −2.56, −17.0, −27.4 |
| −12 dBFS | .25 | −0.06, −34.0, −62.3 | −0.66, −28.7, −37.5 |
| | .50 | −0.13, −28.2, −50.5 | −2.03, −18.9, −28.9 |
| | 1 | −1.00, −18.6, −27.7 | −7.74, −9.0, −24.4 |

The triode curve is led by its second harmonic with the third 20 to 50 dB
further down. The odd kernel rides its bias follower, so it has a second
harmonic too; what sets it apart is 15 to 25 dB more third and a harder
bound.

## Performance

Run `build/rk2-perf-20261116-01/` (`run-ab.sh`, `ab_table.py`,
`ab-summary.md`, `binary-sha256.txt`, `source-rev.txt`, `run.log`). A =
`bench_ma3_perf` from `d910be0` (chorus and reverb in the pool, after the
safety); B = this tree (both are rack blocks before master); K = B with
`MA3_PERF_RACK=rk2`: a saturator, a compressor and a delay at their initial
settings, dry arm. Tepih, Lead, Raster, Prizma and Granica with BCS off and
on, 8 slots, 128-frame blocks, scenario 3, 2048 measured blocks, user-space
PMU cycles, `taskset -c 2`, three epochs in rotating order, both engines:
300 runs. Each number is the median over the blocks of all three epochs.

| | auto (lanes): median / worst | scalar: median / worst |
|---|---|---|
| B dry (no rack attached) against A dry | 0.993× / 1.001× | 1.000× / 1.002× |
| B wet (chorus .30, reverb .25) against A wet | 1.001× / 1.005× | 1.003× / 1.007× |
| Chorus + reverb, M cycles a block: in the pool (A), as blocks (B) | 0.045, 0.054 | 0.049, 0.065 |
| K: saturator + compressor + delay, M cycles a block | 0.067 / 0.073 | 0.082 / 0.094 |

The dry PCM of A and B is identical in all 20 cases. Dry and wet are both
inside the refactor gate (median ≤ +1 %, no case above +2 %).

Chorus and reverb cost about 0.01 M cycles a block more as rack blocks than
they did in the pool: under 1 % of an 8-slot block (1.3 to 1.7 M cycles on
the lanes, 3.7 to 5.5 M on the scalar engine). The three new blocks together
add 0.07 to 0.09 M cycles a block, about 4 to 5 % of a lanes block. Both are
recorded, not gated.

The host was not quiet: a browser was open, and the load average was 1 to 2.
Four of the 100 cells have one disturbed epoch, the first in each case, all
on the lanes (Raster BCS off A dry 2.006 against 1.288 and 1.292 M cycles;
Tepih and Lead BCS off B dry, +12 % and +8 %; Tepih BCS on A wet, +10 %);
the other 96 spread by less than 5 % between epochs. Taking
each cell's best epoch instead of the pooled median gives the same picture:
dry 0.994× / 1.001× and 1.000× / 1.002×, wet 1.001× / 1.007× and 1.004× /
1.005×, chorus + reverb 0.047 and 0.058 (lanes), 0.050 and 0.066 (scalar),
with B above A in 19 of 20 cases.

## Suites

On the final tree (the last source change is the 16 kB rack text buffer in
`driver/ma_patch_file.c` and `workbench/desk/wb_patch.c`): `make all test`
with GCC, `make test-clang` and `make sanitize` exit 0 with 0 compiler
warnings and no sanitizer report. The three builds print the same check
counts and the same hashes.

| Suite | Checks, 0 failures |
|---|---:|
| Core (`build/test`) | 111,378 |
| Hosted | 184 (143 at RK1) |
| MIDI map | 22 |
| Architecture / character / stereo | 227 / 8,700 / 2,611 |
| Rack (`test/ma_rack.c`) | 545 (412 at RK1b) |
| Circuit oscillator: analog / shape / couple | 68 / 68 / 31 |
| Pool parallel | 32 (16 cases × 4 executors) |
| Workbench desk / live | 408 / 50 |
| Patchbench / RT | 879 / 82 |
| CLI twins (`test/ma3_cli.py`, `test/ma3_perf_cli.py`) | 8 + 11 tests |

`ma_glas`, the MA3-1 MIDI twins and the MA3-2 effects test pass. Sizes
printed by the effects test: chorus + reverb 645,224 bytes, rack 9,058,120,
pool 319,064, voice 2,848.

`build/patchbench` and `build/workbench` are outside `make all`; both were
built from the final tree and `make smoke-patchbench` and
`make smoke-workbench` pass. The Patchbench smoke leaves
`patches/mamutanalog/prizma.mapatch` unchanged and draws at most 48,421
vertices a frame before the RK2 pass and 48,791 with seven blocks in the
rack (gate 50,000).

## Listening

`build/rk2-listen/` (inputs and their SHA256s in `inputs-sha256.txt`). All
files are `progression.mid` (four four-note chords, 1.8 s each) through
`render_midi -I mamutanalog --voice-limit 32 -g 16 -t 4`, so each differs
from its dry file only by the rack.

| File | Rack | Peak | FNV-64 |
|---|---|---:|---|
| `rk2-tepih-dry.wav` | none | 0.276 | `33ffd5cc75224d42` |
| `rk2-tepih-wet-before.wav` | `d910be0`: chorus .30 and reverb .25 in the pool, after the safety | 0.191 | `56661ab473b9518d` |
| `rk2-tepih-wet.wav` | this tree: the same two as rack blocks | 0.191 | `2541ef271240401b` |
| `rk2-tepih-delay.wav` | delay: dotted eighth at the file's 120 (375 ms), ping-pong, feedback .45, cuts 150 Hz and 4.5 kHz, mix .35 | 0.164 | `65e88f99074b77a8` |
| `rk2-tepih-compressor.wav` | compressor: RMS, threshold −40 dB, ratio 4, knee 6 dB, 10 / 150 ms, makeup +6 dB | 0.305 | `30efdddb3097dc92` |
| `rk2-lead-dry.wav` | none | 0.260 | `9f07a115f3f89a20` |
| `rk2-lead-saturator-triode.wav` | saturator: drive .8, triode, input +20 dB | 0.252 | `2cb6945eb2bb6fa4` |
| `rk2-lead-saturator-odd.wav` | the same with the odd kernel | 0.254 | `bceed0477dda58aa` |
| `rk2-lead-saturator-tilt.wav` | the triode setting with tilt +6 dB before and −6 dB after | 0.253 | `8269f917dba822e0` |
| `rk2-lead-saturator-hot.wav` | drive 1, triode, input +32 dB | 0.203 | `a374a200c4a03dc2` |
| `rk2-lead-chain.wav` | saturator (drive .5), compressor (−38 dB, 3:1, +4 dB), delay (as above, mix .25), chorus .30, reverb .25 | 0.192 | `fc3bbc8bd9de9c25` |

`render.sh` in that directory renders the set and writes `renders.txt`;
`BEFORE` names a `render_midi` built from `d910be0` for the second file.
Every render reported "two runs identical". The two wet files are the pair
measured under "Signatures": 142 dB apart.

## Open

- The operator's listening verdict, and a look at the rack row on a display.
- Every value marked [decision] in RKD30..RKD32 is unjudged by ear: the
  delay's 50 ms follow, the saturator's +20 dB input gain and 650 Hz pivot,
  the compressor's 10 ms RMS window and −36 dB threshold, the ±64 bound.
- A chorus or reverb that comes on in mid-sound starts from an empty line,
  where MA3-2 kept its history running at mix zero.
- `driver/probe_ma3_stages.c` still mirrors the old chain (it has not built
  since the shared kernels and is outside `make all`).
- The Patchbench live check and the soak were not run; no audio was opened.
- Bench snapshots still store the patch only (as in RK1).
