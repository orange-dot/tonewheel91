# Hurt — two manuals and bass pedals

New organ-only interpretation in `driver/exhibit_organ_hurt.c`. It links
tonewheel91 organ/generator/drive/rotary code and hosted SMF/WAV helpers.
There are no Mamut or EP voices, drums or layered synth parts. The earlier
Mamut exhibits remain separate. The atmosphere stays dark and wet.

## Performance

- Right hand, upper manual: all 200 vocal attacks, one octave above the
  source MIDI, plus 48 instrumental-motif notes that fit wholly inside
  vocal rests. Source overlap tails are shortened at the next vocal attack
  to produce one playable melodic line. No vocal attack is dropped.
- Left hand, lower manual: 133 attacks in compact three-note voicings,
  with common tones held through chord changes. Pitch classes follow the
  backing guitar and bass; closest inversions reduce hand movement. Open
  two-pitch-class chords double an existing pitch in the octave. The held
  span never exceeds 12 semitones.
- Pedals: 76 monophonic root attacks, concert pitches MIDI 31–41. Repeated
  roots are tied across bars. This leaves the right foot available for
  the shared manual expression curve while the left foot carries bass.

The right hand ranges over MIDI 57–81, the left over 45–66. Overlap between
their pitch ranges is intentional: they use separate manuals. The checks
bound simultaneous notes and hand span; they do not certify every fingering
or guarantee comfort on a particular player's hand size or pedalboard.

Both manuals use warm registrations (`408300000` upper, `307200000` lower),
percussion off, wear zero, .08 preamp drive and slow rotary. A smooth shared
expression curve shapes verses, choruses and the breakdown. Registration
stays fixed during the performance. Each manual is a separate hosted
instrument instance; this is an arrangement exhibit, not a new model of
a complete shared-generator two-manual console.

The existing manual mapping folds low notes above wheel 12. Pedal tones
therefore use the existing tonewheel generator directly, including its low
wheels, with a fundamental and .22 octave component, 8 ms gain smoothing
and .07 drive. The MIDI pedal pitch denotes the sounding fundamental;
it is not sent through manual foldback. No new core API is required.
This hosted pedal division does not model a vintage pedal contact network.

Right/left/pedal host gains are .018/.012/.012. Damped stereo reverb has
four comb lines and two all-pass diffusion stages per side. Pedals receive
less reverb to keep the low register clear. The full take lasts 253 s and
fades during the final three seconds. Short `-d` checks render from time
zero and end at the requested duration; they do not add a new short fade.

## Inputs and export

The existing local backing and vocal files are used unchanged:

- `notes-midi/local/nine-inch-nails-hurt.mid`, SHA-256
  `4c98ab235aaac6917562db89f1e54d942ea93f782419705f06389200779ad554`.
- `notes-midi/local/hurt-songparts-vocals.mid`, SHA-256
  `1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0`.

The vocal source and its eight-bar alignment to the backing are documented
in [ma-hurt-vocal.md](ma-hurt-vocal.md). This arrangement changes vocal
register and overlap articulation for the right hand; it does not import
the Mamut vocal filter automation.

`_score.mid` is format 1, 480 PPQ, 84 BPM, with a conductor track and
separate `right`, `left`, `pedals` tracks on MIDI channels 1, 2 and 3.
It exports notes and tempo, not drawbar registrations, rotary settings or
the hosted expression curve; set those on the receiving organ separately.
Generated score and audio files remain under ignored `build/`.

## Commands

```sh
make CC=/usr/bin/gcc build/exhibit_organ_hurt
./build/exhibit_organ_hurt --check
./build/exhibit_organ_hurt --score-only -o build/organ_hurt_study
```

Full render and listening preparation:

```sh
./build/exhibit_organ_hurt -o build/organ_hurt_full
python3 -B driver/prepare_hurt_listening.py build/organ_hurt_full \
  --stems right left pedals --previews
```

Outputs: `_right.wav`, `_left.wav`, `_pedals.wav`, `_mix.wav`, plus the MIDI
score. Each stem owns its reverb return and final fade, so summing the three
reproduces the mix. Listening preparation applies one constant gain to a
-3 dBFS sample peak. `make audition-organ-hurt` builds and renders using
the default `build/organ_hurt` prefix. `-i` and `-v` override input paths;
the inspected source layouts are required.

## Validation

GCC/Clang builds and Clang static analysis pass. The native suite passes
123054 checks. Full-score validation enforces one right-hand note, at most
three left-hand notes within an octave, one pedal note, matching releases
and keyboard ranges. Exporting and reparsing the MIDI reproduces all 914
events, their velocities and the 714285 microsecond tempo exactly.

The final eight-second GCC preview has raw mix peak .03313222 and RMS
.00847804, with no clipping or nonfinite samples. The three stems reproduce
the mix with maximum error 0.0. Listening preview peak is -3 dBFS, RMS
-14.83910 dBFS. Files use prefix `build/organ_hurt_preview`; the full-length
three-part MIDI is `build/organ_hurt_study_score.mid`. An introductory
preview establishes levels, not a musical verdict for the whole form.

The eight-second GCC and Clang WAVs match byte-for-byte for all four outputs.
A two-second ASan/UBSan/float-cast-overflow render exercises both manuals and
pedals without errors (LSan disabled for the sandbox).

## Full delivery

Completed 2026-09-07 with prefix `build/organ_hurt_full`: 253 seconds,
12144000 stereo frames, no clipping or nonfinite samples. The three full
stems sum to the mix with maximum sample error 0.0. The exported MIDI is
byte-identical to the checked score-only export.

| Output | Peak | RMS | FNV64 |
| --- | ---: | ---: | --- |
| Right | .04165491 | .00613381 | `b68f903c5dc9da02` |
| Left | .02212049 | .00423855 | `50bbaca6c213cb22` |
| Pedals | .01315618 | .00591635 | `332f592e51b6dd48` |
| Mix | .06919639 | .01014875 | `c1bf71dd0df7a345` |

Listening gain is +20.19833 dB, peak -3 dBFS, RMS -19.67342 dBFS.
L/R correlation is .76955, mono RMS loss .58214 dB, and the final 100 ms
peak is below 8e-17. Listening SHA-256:
`e8d8db24304112b02526b9093fcb1c40ebfcc18c9d32306b8b6cb545043285a1`.

`_listening.wav` and seven full-render excerpts are ready for audition;
`_levels.json` contains per-section measurements and raw file hashes.
`_score.mid` contains the full three-part performance. Signal verification
does not replace the user's musical judgment.
