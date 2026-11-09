# MA3 Orion

A Mamut Analog arrangement of Metallica's *Orion* on the ma3_show desk.
`driver/make_orion.py` writes the arrangement MIDI and
`driver/exhibit_ma3_orion.c` renders it in one pass. The show uses the
2026-11-08 kit (`docs/ma3-mourning-palace.md`) and the humanization and wet
mix of The Cry of Mankind edition 2 (`docs/ma3-cry-of-mankind.md`).

## Source

The source is a detailed transcription with named tracks:

- three solo/melody guitars;
- the bass, doubled on two tracks with identical notes (a lead-bass program
  and a picked-bass program);
- the machine-gun guitar and its low doubling;
- opening chords, two chord tracks and a rhythm guitar;
- a kit split into kit, snare, hi-hat and cymbal tracks.

The source runs at 120 PPQ. The song is in E minor. Source dynamics are
real: the opening fades in from velocity 24 to 68, the snare rolls build,
and the snare has ghost notes.

The tempo map:

| from (source) | tempo |
|---|---|
| tick 0 (silent first bars) | 300 BPM |
| then | 133 BPM |
| tick 60960 (middle section) | 83 BPM |
| tick 88440 | 133 BPM |

The derived MIDI changes the source in three ways:

- It is rescaled to 480 PPQ (×4, exact).
- It starts at the first note. The four silent source bars (4.2 s) are
  dropped, so derived bar *b* is source bar *b* + 4.
- Pitched velocities are scaled by 95/127, so the source dynamics are kept.

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | lead1 | Solo 1 | orchestration lead, unison 2 | lead |
| 1 | lead2 | Solo 2 | same, own pool | lead |
| 2 | lead3 | Solo 3 | same, own pool | lead |
| 3 | bassl | Bass (left), lead-bass program | driven bass, brighter and harder | bass |
| 4 | bassr | Bass (right), picked | driven bass | bass |
| 5 | gtr1 | Opening chords + Rhythm | driven guitar | guitar |
| 6 | gtr2 | Machine-gun guitar + its low doubling | darker driven guitar, short decay | guitar |
| 7 | chords | Chords 1 + Chords 2 | tenor strings voice | chords |
| 8 | kick | kit: kicks (35, 36) | MA3 Hurt kick, one slot | rhythm |
| 9 | pulse | snare (38) | MA3 Hurt pulse | rhythm |

The three melody lines play in harmony through the slow middle section.
Each has its own pool, so the harmony stays three voices.

## Kit

- The kick sits on the fifth of E minor. That is B1 (MIDI 35), the Hurt
  kick's own note.
- The kick has derived accents, a 148 ms gate and a one-slot choke pool. Its
  run threshold is time-based (167 ms). The closing double kick plays
  sixteenths at 133 BPM, 113 ms apart.
- The snare is played as the pulse, on the half-bar root an octave up. It
  has a 401 ms gate and a 200 ms minimum spacing, which drops 42 snares from
  the rolls.
- The source snare velocity scales the derived pulse velocity, so the
  crescendo rolls and ghost notes carry through.
- No bar qualifies as a blast.
- Toms, hi-hat and cymbals are not played.

## Humanization and mix

These follow The Cry of Mankind edition 2.

Timing:
- Each note moves by a per-part offset plus a triangular jitter. The jitter
  is converted to ticks at the local tempo.
- Standard deviations:

  | part | timing sd |
  |---|---|
  | kick | 3 ms |
  | machine-gun guitar (kept tight) | 4 ms |
  | bass, pulse | 5 ms |
  | rhythm guitar, chords | 6 ms |
  | leads | 7 ms |

- Chords strum upward at 7 ms per string.

Velocity:
- Metric accent, a four-bar swell of ±4, and scatter.

Mix:
- Lead chorus .24 and reverb .42.
- Rhythm guitar .20 / .30.
- Chords .22 / .38.
- A larger room (size .82, damping .50) on the leads, guitars, chords and
  pulse.

## Form and controls

Bars are derived bars.

| from bar | section |
|---|---|
| 0 | fade-in |
| 14 | drums |
| 32 | rhythm guitar |
| 56 | machine-gun section |
| 96 | first melody |
| 118 | break |
| 123 | slow middle section, 83 BPM |
| 130 | harmonized lines |
| 171 | chords |
| 181 | 133 BPM again |
| 209 | closing double kick |
| 223 | fade |

Energy drives guitar HEAT (.20 + .25e) and the leads' mod wheel
(.06 + .12e).

The show length is the last note-off (derived tick 432121) plus a tail to
the next whole second and 4 s more. That makes 473 s.
