# MA3 musical exhibits — Blues and all-Mamut Hurt

2026-09-08. Prepared and rendered. The operator initially requested stopping
before rendering, then authorized the sequential full renders.
The earlier MA3-2 eight-example listening ballot is accepted; that approval
does not assess these new arrangements or their provisional balance.

## New render paths

`driver/exhibit_ma3_blues.c` and `driver/exhibit_ma3_hurt.c` share the small
hosted `ma3_show` renderer. Every timbre owns a complete digital pool with
deterministic seed/character, sleep/release allocation, fixed unison and one
shared body/DC/safety/chorus/reverb path. Parts are overdubs with independent
limits, not a claim that one single-timbre pool renders a multitimbral score.
Each pool reserves 64 physical slots regardless of its active limit.

Legacy exhibit sources and WAV paths remain intact. The new material headers
are score/patch snapshots from `exhibit_ma_blues_ma2_full.c` and
`exhibit_ma_hurt_organ.c`; they contain no old allocator, organ DSP or hosted
comb/allpass reverb. The new editions can evolve without changing the old
arrangements. They are not PCM comparisons against a continuously running
MA1 overdub desk or five-card MA2 bank.

### Blade Runner Blues — 248 seconds

The original slow F-sharp-minor performance study retains its 125 notes,
234-second form and 14-second tail. It is the existing inspired study,
not a new transcription of a commercial score. Long chord overlaps now use
actual pool release groups. The pads and lead use two-way poly-unison with
5.5/3.5-cent spread; bass stays single-voice per note. Character, Raster,
Mozaik and BCS follow the earlier MA2-full recipes, with evolving pad macros
and inherited per-note expression. The lead retains its delayed vibrato and
entry bend, on a separate MIDI channel per phrase so release tails keep their
own expression. Slot spread replaces the old manually assigned note pans.

| Part | Slot limit | Unison | Chorus mix | Reverb mix | Hosted gain | Stem |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Dark pad | 32 | 2 | .30 | .28 | 1.6 | pads |
| Haze | 16 | 2 | .34 | .32 | 1.0 | pads |
| Root bass | 4 | 1 | 0 | .08 | 2.6 | bass |
| Moving bass | 8 | 1 | .10 | .15 | 2.0 | bass |
| Lead | 8 | 2 | .16 | .28 | 3.8 | lead |

Held-note peaks from the score are 8/2/2/1/1, before unison. Limits total 68
across five independent timbres. Actual DSP peaks, stealing and mix balance
are not measured until rendering. Gains account for the pool's fixed 1/8
normalization; they are musical starting values, not measured acceptance.

### Hurt — 253 seconds, entirely Mamut

The complete vocal line and source-derived backing come from the latest
Hurt-vocal arrangement, including its 125-note main melody and twelve-note
harmony passage at bars 64–67. Former organ parts now use a dark Mamut lead
and a four-way Dubina swell. There is no organ or EP synthesis in this show.
All 200 vocal attacks/releases, overlaps, velocities, timing and the fixed
eight-bar alignment are retained. The vocal stays unison 1 for articulation;
its filter closes from 850 to 240 Hz over the 85-bar form.

Existing local inputs, inspected again without downloading new material:

| Input | SHA256 |
| --- | --- |
| `notes-midi/local/nine-inch-nails-hurt.mid` | `4c98ab235aaac6917562db89f1e54d942ea93f782419705f06389200779ad554` |
| `notes-midi/local/hurt-songparts-vocals.mid` | `1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0` |

See `ma-hurt-vocal.md` for provenance and alignment. The new executable checks
the same inspected MIDI layout and all event pairing before rendering. The
sources stay ignored and are not redistributed with code changes.

| Part | Attacks | Held peak | Slots | Unison | Chorus/reverb mix | Hosted gain | Stem |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| Pluck | 704 | 6 | 16 | 1 | .12/.18 | .9 | tonal |
| Pad | 324 | 2 | 24 | 2 | .28/.27 | .9 | tonal |
| Bass | 128 | 1 | 4 | 1 | 0/.05 | 2.8 | tonal |
| Tenor | 189 | 3 | 8 | 1 | .14/.22 | 1.0 | tonal |
| Texture | 248 | 1 | 16 | 2 | .22/.25 | .7 | tonal |
| Main melody | 125 | 1 | 8 | 2 | .16/.25 | 1.5 | melody |
| Kick | 173 | 1 | 4 | 1 | 0/.04 | 1.1 | rhythm |
| Pulse | 64 | 1 | 4 | 1 | .06/.14 | 1.7 | rhythm |
| Vocal | 200 | 3 | 8 | 1 | .08/.18 | 1.8 | vocal |
| Harmony swell | 12 | 3 | 24 | 4 | .32/.32 | 1.0 | harmony |

Total: 2167 Mamut attacks, 4334 paired score events, 116 available slots across
ten timbres. Pad and texture gain subtle digital content; bass retains its
calmer articulation with BCS, while the full vocal remains focused and mostly
dry. No quality reduction, adaptive polyphony or automatic gain riding.

## Rendering and output validation

The shared renderer uses 128-frame output blocks, dispatches notes at exact
sample positions, and updates expressive controls every 480 frames. Each
part uses a stable session seed derived from its name. Notes are released
from the score; pool effects keep their tails as voices sleep. A fixed
half-second opening fade and two-second ending fade apply to each stem.
Only full-render endpoints fade; `--seconds` captures a deterministic prefix
without retiming notes or adding an early closing fade.

One reference pass writes no audio. A second pass writes temporary stem/mix
WAVs. Per-output PCM hashes must agree before publishing final paths. Samples
must stay finite and within ±1, held/sustained steals are rejected, and all
voices must sleep by the full endpoint. Released-group steals are reported.
Every file logs peak/RMS/hash; each part logs active/peak slots, stealing,
sleep counts and effect clipping. The sum of faded stems is the mix, with
no nonlinear processing after summation.

Effects use the accepted MA3-2 shape settings: chorus depth .35/rate .25 Hz,
reverb size .65/damping .55; per-part mixes are above. Since no audio has yet
run, the output checks, headroom, actual release stealing and musical balance
are **unverified**. Schedule-only sanitizers do not establish audio-path safety.
No host throughput/live capacity measurement is performed in this exhibit task.

## Handoff commands

Build and inspect, without audio:

```sh
make CC=/usr/bin/gcc check-ma3-shows
```

After the operator resumes rendering, first use short prefixes to check the
audio path and balance. `--seconds` takes a whole number of seconds and does
not change the full score. For late passages, the full render is the source
for exact excerpts; there is no fast-forward approximation of DSP history.

```sh
build/exhibit_ma3_blues --render build/ma3_blues_preview --seconds 32
build/exhibit_ma3_hurt --render build/ma3_hurt_preview --seconds 24

# Full renders, sequentially, without competing tests or other render jobs:
make CC=/usr/bin/gcc audition-ma3-blues
make CC=/usr/bin/gcc audition-ma3-hurt

python3 driver/prepare_hurt_listening.py build/ma3_blade_runner_blues \
  --stems pads bass lead --form blues --previews
python3 driver/prepare_hurt_listening.py build/ma3_hurt \
  --stems tonal rhythm melody vocal harmony --previews
```

The preparation tool checks every stem and its exact float sum, writes a
constant-gain −3 dBFS-peak listening mix and excerpts, and records JSON levels,
stereo correlation/mono loss, tail levels and SHA256 values. It already needs
NumPy; no new dependency is introduced. Previous Hurt preparation defaults
remain unchanged. Optional `--reference` produces RMS-matched old/new pairs
when duration matches; it must name the intended previous take explicitly.

Generated outputs:

- `build/ma3_blade_runner_blues_{pads,bass,lead,mix,listening}.wav`
- `build/ma3_hurt_{tonal,rhythm,melody,vocal,harmony,mix,listening}.wav`
- Per-show `_levels.json` and named `_preview_*.wav` files.

The retained old WAVs are not audio evidence for these new implementations.

## Rendered delivery — 2026-09-08

Both full renders ran sequentially with GCC. Each made a reference pass and an
output pass; every stem and mix hash matched across passes. Stem preparation
reported exact sum error `0.0` for both shows.

Blues: pads `1c16e5dfcc48b3a3`, bass `6de5de99a8e62e63`, lead
`ad2555b18adc556c`, mix `4c6348322f14d58e`. The listening file is
`build/ma3_blade_runner_blues_listening.wav`, SHA256
`917fe5143f9350e92a80bfa091f07502616d357b2905e37c21b3c4558f6e2694`, RMS
−18.033188 dBFS and peak −3.000001 dBFS.

Hurt: tonal `63b8f0a7b959fb57`, rhythm `20af68883dca23cd`, melody
`9a80cb567592c4de`, vocal `82dd44adafbbf984`, harmony `271a774795a285bd`,
mix `2406355f29923b1f`. The listening file is `build/ma3_hurt_listening.wav`,
SHA256 `df9d04a6d270289f868e6a2c7ed86f191255fdca691008dbd32fe109884f2cb1`,
RMS −19.047447 dBFS and peak −3.000000 dBFS.

No FX clipping or sanitization occurred. Blues had no steals. Hurt had only
released-group steals: pluck 456, tenor 181, melody 117 and harmony 6; held
and sustained steals were zero. All pools slept at the full endpoint. Musical
quality still requires operator listening.

## Completed non-audio verification

- GCC aggregate `make test` passes with all prior PCM signatures intact.
- New `check-ma3-shows` runs pass under GCC, Clang and Clang
  ASan/UBSan/float-cast-overflow. These initialize pools and validate complete
  scores, but deliberately do not tick audio.
- Clang static analysis passes for both executables and `ma3_show.c`, without
  diagnostics. Both compiler builds are warning-free.
- Missing Hurt input, conflicting check/render modes, invalid duration and
  duration without explicit render all fail; the local input SHA256 values
  match those recorded above.
- The preparation script accepts both new stem sets and the Blues form
  selector; both full stem preparations passed after rendering.
- `git diff --check` passes. Local logs use `build/ma3-shows-*.log`; analyzer
  reports are `build/ma3-{show,hurt,blues}-analysis.plist`.

The shared hosted desk statically reserves ten pool states (7669760 bytes);
Blues initializes five, Hurt ten. This is storage accounting, not an RSS or
realtime benchmark result. Audio render and stem checks are complete;
listening judgment remains.
