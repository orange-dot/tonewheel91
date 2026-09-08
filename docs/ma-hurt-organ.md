# Hurt — calmer bass and wet organ

Historical take, rendered and accepted by the user. The same hosted target
now prepares the full-vocal revision in [ma-hurt-vocal.md](ma-hurt-vocal.md).
Commands below record the preceding implementation; the current target
does not reproduce the sparse-answer arrangement.

The next listening revision keeps the dark Mamut palette and bass patch,
halves the bass attacks, and moves the existing high melody to tonewheel91
organ. There is no EP. Earlier listening WAVs remain intact.

## Tasks

- [x] Find and inspect a public vocal-score preview.
- [x] Reduce bass movement while retaining its patch and harmonic roots.
- [x] Route the main melody to a dark, wet organ with mild drive.
- [x] Add occasional short vocal-derived Mamut answers.
- [x] Validate builds, schedule, short render and native checks.
- [x] Render the complete take and aligned stems.
- [ ] Prepare listening files and excerpts: superseded by the user's request
  to change the exhibit and leave the next render to another model.

## Arrangement

Bass attacks fall from 256 to 128: sixteenth positions 2 and 10 in each
groove bar, with 600-tick gates instead of 155/205 ticks. Roots still follow
the source harmony at half-bar boundaries; the final-chorus octave jumps
are removed. The bass patch, gain, BCS automation and gentle kick ducking
are unchanged from the praised Mamut-only take.

All 125 source channel-2 notes retain their timing and pitches at the existing
one-octave-down placement. They now play organ, entering at 91.43 s and
returning at 194.29 s. Registration is `508300000`: fundamental-led, without
upper mutation drawbars or percussion. Wear is zero, preamp drive .11,
rotary chorale with balance .38 and width .78. A 48 kHz one-pole coefficient
.075 softens the direct tone; host gain is .0085. The organ owns a damped reverb return with
direct/wet gains .48/.78. Its separate stem includes both.

### Vocal source and selective use

Inspected the public first-page preview of the Hal Leonard piano/vocal/guitar
edition of Trent Reznor's **Hurt (Quiet)** on
[Virtual Sheet Music](https://www.virtualsheetmusic.com/score/HL-305048.html),
accessed 2026-09-07. The
[public preview image](https://cdn3.virtualsheetmusic.com/images/first_pages/HL-v/HL-305048First_BIG_3.png)
shows the short verse contour E4–G4–E4–D4 in its A-minor setting.
Only this four-note fragment is used, transposed up two semitones to
F♯4–A4–F♯4–E4 for the local MIDI's B-centred arrangement. No full vocal
transcription, downloaded commercial score or lyrics are distributed.

Six Mamut answers start in zero-based bars 11, 19, 43, 51, 61 and 81,
after a quarter-note rest. Alternating answers omit the opening note:
21 added note attacks in total. Their spaced rhythm, stretched final note
and long releases depart from the vocal timing. They are occasional motifs,
not a continuous instrument playing every sung syllable. The first five
avoid the main organ passages; the last plays over the late Mamut backing.
First attacks occur at 32.14, 55.71, 123.57, 147.14, 175.00 and 232.86 s.

The other Mamut patches keep the previous take's low cutoffs and zero
Raster, Mozaik, noise, sync, crossmod and identity macros. Two five-card
MA2 banks retain their shared stereo body and physical character. The
former three-voice melody pool now carries the sparse answers.

## Reproduce

```sh
make CC=/usr/bin/gcc build/exhibit_ma_hurt_organ
./build/exhibit_ma_hurt_organ --check
./build/exhibit_ma_hurt_organ -o build/ma_hurt_organ_2026-09-07
python3 driver/prepare_hurt_listening.py build/ma_hurt_organ_2026-09-07 \
  --stems tonal rhythm organ --previews \
  --reference build/ma_hurt_dark_2026-09-07_mix.wav
```

The hosted target links MA and organ core objects with existing SMF/WAV
helpers; no core API changes. It produces aligned 48 kHz stereo float32
`_tonal.wav`, `_rhythm.wav`, `_organ.wav` and `_mix.wav`. Preparation requires
NumPy and applies constant gain to a -3 dBFS sample peak, without compression.
The comparison tool's historical `_comparison_noir.wav` suffix denotes the
explicit reference supplied above: here, the preceding dark Mamut take.

Short `-s START -d DURATION` renders warm ten seconds of DSP history;
their oscillator history differs from a full render. Delivered listening
excerpts are extracted from the completed full take. The unchanged local
MIDI SHA-256 is
`4c98ab235aaac6917562db89f1e54d942ea93f782419705f06389200779ad554`.

## Validation and delivery

GCC and Clang optimized builds pass; Clang static analysis reports no
diagnostics. The native suite passes all 123054 checks. The event schedule
has no unmatched releases or excess held polyphony. The executable has
no EP symbols.

A Clang ASan/UBSan/float-cast-overflow render across the organ entry passes
with zero clipping/nonfinite samples and no held-note steals. LSan is
disabled for the sandbox's ptrace limitation. The GCC sanitizer link was
unavailable because the installed linker references a missing
`/usr/lib64/libasan.so.8.0.0`; the successful run uses Clang's runtime.

The first organ balance preview measured organ RMS .02898 against tonal
.00980. Reducing its hosted gain from .025 to .0085 brings it near the
tonal stem instead of dominating the backing.

The full 253-second take completed with 1851 MA attacks, 125 organ attacks,
zero held-note steals, 1343 released-voice reuses, and zero nonfinite/clipped
samples. Raw outputs share `build/ma_hurt_organ_2026-09-07`:

| Output | Peak | RMS | FNV64 |
| --- | ---: | ---: | --- |
| Tonal | .06881720 | .01022850 | `94c16f1f7a028d41` |
| Rhythm | .04019452 | .00577821 | `26ac4a3e26fb1bdc` |
| Organ | .04726687 | .00396708 | `336cecce1ce11eb3` |
| Mix | .08471278 | .01240328 | `3b41b2a3f1d7c83a` |

The user accepted this take, then requested full vocal melody and an exhibit-only
handoff. Normalized listening/A-B preparation was not run in this session.
