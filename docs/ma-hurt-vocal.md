# Hurt — full Mamut vocal, wet organ

Implementation handoff, 2026-09-07. The user accepted the previous organ
render, then requested the entire vocal line on a slightly dirtier Mamut,
retention of the organ melody, and one diffuse organ harmony passage.
**Do not treat the earlier WAVs as audio evidence for this revision.**
The user explicitly assigned rendering to another model; no new audio was
generated after these edits.

## Tasks

- [x] Find a complete vocal MIDI and inspect its actual note content.
- [x] Align all vocal events to the existing backing without truncating phrases.
- [x] Replace sparse answers with the complete Mamut vocal part.
- [x] Preserve the existing main organ and calmer bass.
- [x] Add a separate diffuse organ harmony passage.
- [x] Finish build, schedule and static checks without rendering audio.
- [ ] Next model: audition balance, render, measure and prepare listening files.

## Source and alignment

The earlier Hal Leonard preview covered only part of the vocal score.
The complete source used here is the public Songparts arrangement
[Hurt, version e7yTgIiCMAZgkLqO](https://songparts.com/songs/nine-inch-nails/hurt/e7yTgIiCMAZgkLqO/play),
accessed 2026-09-07, through its offered
[MIDI download](https://songparts.com/songs/nine-inch-nails/hurt/e7yTgIiCMAZgkLqO/download).
This is a MIDI transcription, not a claim of note-for-note verification
against the commercial score or original vocal recording.

Local file: `notes-midi/local/hurt-songparts-vocals.mid`, SHA-256
`1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0`.
It is format 1, 11 tracks, 480 PPQ, tempo 740740 microseconds/quarter.
Track 1 (zero-based), named `Lead Vocals`, uses MIDI channel 0. It has
200 note-ons and 200 releases, notes 45–69, velocities 73–91, and a maximum
of three overlapping held notes. Those overlaps and note lengths are retained.
GM controller setup is not applied to the Mamut patch.

The source guitar, bass and piano enter at bars 8, 24 and 40; the existing
backing enters at bars 0, 16 and 32. Subtracting exactly eight 4/4 bars
(15360 ticks) aligns these anchors. All vocal pitches, velocities, note
durations, rests and relative timing are copied. No phrase is omitted,
repeated, snapped to chords or transposed. Playback uses the backing's
84 BPM instead of the transcription's approximately 81 BPM. The first
vocal attack is at 0.714285 s and the final release at 240.714045 s, inside
the existing 253-second render window.

The rejected Warren Rice / BitMidi candidate had only three sustained notes
in its track named `Lead 6 (voice)`; it is not used. The MIDIfind alternative
with the original six-track backing likewise lacks a full vocal part.
Downloads stay under ignored `notes-midi/local/`; do not commit these files.

## Sound changes

The former sparse-answer pool becomes three Mamut vocal voices. Saw,
triangle and sine oscillators feed an 850 Hz filter with .16 filter drive,
.18 BCS amount and .14 body drive. Attack is 28 ms, sustain .72, release
420 ms; this preserves short vocal notes better than the former slow pad.
Raster, Mozaik, noise, sync, crossmod and identity macros remain zero.
Master is .14 and hosted gain .18 per side. The vocal owns a damped reverb
stem with direct/wet gains .72/.60. These levels are starting values for
the next model's audition, not a measured balance verdict.

The vocal filter now closes continuously over the 85-bar form. Its eased
cutoff goes from 850 Hz at the opening to 240 Hz at the end; resonance moves
from .16 to .21, drive from .16 to .21, and mixer pressure from .14 to .18.
This automation is applied at control updates only and does not alter any
vocal note, gate, velocity or event timing.

The main organ retains its 125 source events, octave, registration,
drive, rotary, low-pass, hosted gain and reverb. The approved bass patch
and its 128-note calmer pattern are unchanged. Existing Mamut backing and
rhythm retain their settings.

One separate organ plays three source-derived chord tones in each of
zero-based bars 64–67: 12 attacks, approximately 3:03–3:14, immediately
before the late main-organ return. Registration `608400000`, .11 preamp
drive, chorale rotary, width .95, balance .32 and a darker low-pass provide
the fuller harmonic layer. Its gain swells with approximately .80 s attack;
host gain is .0045, direct/wet gains .20/1.0. It owns a separate instrument
and reverb, so its nonlinear drive cannot alter the established main organ.

The six outputs are `_tonal.wav`, `_rhythm.wav`, `_organ.wav`, `_vocal.wav`,
`_harmony.wav` and `_mix.wav`, at 48 kHz stereo float32. Each stem includes
its own linear reverb return and shared final fade. The preparation script
accepts the two new stem names.

## Handoff commands

GCC and Clang builds, Clang static analysis, and the Clang
ASan/UBSan/float-cast-overflow **schedule-only** run pass. Schedule validation
finds no unmatched releases or excess held polyphony: 2030 MA attacks and
137 organ attacks (125 lead plus 12 harmony). Missing vocal input and the
wrong MIDI layout both fail before creating audio. The preparation CLI
accepts all five source stems. No audio-path or listening validation has
been performed for this revision; the earlier native core suite passed
123054 checks and the core code has not changed during these exhibit edits.

Build and schedule inspection only:

```sh
make CC=/usr/bin/gcc build/exhibit_ma_hurt_organ
./build/exhibit_ma_hurt_organ --check
```

For the model assigned to rendering:

```sh
./build/exhibit_ma_hurt_organ \
  -o build/ma_hurt_vocal_2026-09-07
python3 driver/prepare_hurt_listening.py build/ma_hurt_vocal_2026-09-07 \
  --stems tonal rhythm organ vocal harmony --previews \
  --reference build/ma_hurt_organ_2026-09-07_mix.wav
```

`-v PATH` overrides the vocal MIDI location; it must retain the inspected
layout. The default input is the original local backing MIDI, SHA-256
`4c98ab235aaac6917562db89f1e54d942ea93f782419705f06389200779ad554`.
Default output prefix changes to `build/ma_hurt_vocal`, preserving earlier
render files. The preparation tool's historical `_comparison_noir.wav`
suffix refers to whichever explicit reference is supplied above.

Before the full render, check vocal articulation and vocal/organ balance
where they overlap around 1:35 and 3:15, and the new harmony around 3:03.
Then verify stem summing, finite samples, headroom, tail fade and the organ
stem against the accepted take. Musical balance remains a listening judgment;
the full render and excerpts are now ready for audition.

## Rendered delivery

The previous full render completed 2026-09-07 as `build/ma_hurt_vocal_2026-09-07`.
Six aligned
253-second, 48 kHz stereo float32 WAVs were rendered. The stem sum reproduces
the mix with maximum sample error **0.0**. The render reports 2030 Mamut
attacks, 137 organ attacks, zero held-note steals, zero nonfinite samples and
zero clipped samples.

| Output | Peak | RMS | FNV64 |
| --- | ---: | ---: | --- |
| Tonal | .06881720 | .01016368 | `ca8165025ed8dd12` |
| Rhythm | .04019452 | .00577821 | `26ac4a3e26fb1bdc` |
| Organ | .04726687 | .00396708 | `336cecce1ce11eb3` |
| Vocal | .02699876 | .00333691 | `44b822ba2282f4eb` |
| Harmony | .03920958 | .00198914 | `a186cd4d942554e2` |
| Mix | .09619452 | .01293158 | `4a7284e8e871c752` |

The listening file is constant-gain normalized to -3 dBFS peak, with RMS
-20.42997 dBFS, L/R correlation .35255 and mono RMS loss 1.71758 dB.
Listening SHA-256:
`bb3bef48143b97dcae246b7d182a900a45cb242df3cb237f17bab611f47be298`.
The preparation report is `build/ma_hurt_vocal_2026-09-07_levels.json`;
it also contains RMS-matched comparison files against the accepted organ take
and excerpts for intro, answer, groove, breakdown, main melody, climax and
outro.

After that render, the exhibit gained a new continuous vocal-filter
automation: 850 Hz down to 240 Hz across the form, with resonance, drive and
mixer pressure rising gently. The WAVs above intentionally remain the prior
render; regenerate them after auditioning this color change.
