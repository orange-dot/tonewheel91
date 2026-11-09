# MA3 Spellbound (by the Devil)

A Mamut Analog arrangement of Dimmu Borgir's *Spellbound (by the Devil)* on
the ma3_show desk. `driver/make_spellbound.py` writes the arrangement MIDI
and `driver/exhibit_ma3_spellbound.c` renders it in one pass. It is the
third show built on the 2026-11-08 kit (`docs/ma3-mourning-palace.md`,
`docs/ma3-progenies.md`), and it reuses their guitar and bass voices.

## Source

The source is one of two local transcriptions of the song, and the cleaner
one: a full band with two guitars, bass, three keyboard tracks and a kit,
at 480 PPQ.
- The other transcription is assembled from two unrelated halves, with a
  35 BPM section, so it was not used.
- The conductor track carries two tempo events at tick 0 (428571 µs, then
  400000 µs). The later one is in force, which gives 150 BPM, 140 BPM from
  bar 28 and 110 BPM from bar 90. The derived MIDI writes the map without
  the overridden entry.
- Pitched velocities are kept: 95 everywhere, except 63 on 24 choir notes.
- In bars 90–97 the keyboards play alone (110 BPM).

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | gtr1 | Guitar I | driven saw/pulse, unison 2 | guitar |
| 1 | gtr2 | Guitar II | darker, VCO2 detuned the other way | guitar |
| 2 | bass | Bass | driven bass | bass |
| 3 | choir | Keyboards (prog. 52), 640-tick chords | Hurt pad voice, 180 ms attack | choir |
| 4 | drone | Keyboards (prog. 91), low held notes (MIDI 37–45) | Hurt harmony voice, slow | pads |
| 5 | organ | Keyboards (prog. 19), eighth and sixteenth lines | gate-shaped organ voice | organ |
| 6 | kick | kit: kick (36) | MA3 Hurt kick, one slot | rhythm |
| 7 | pulse | kit: snare (38) | MA3 Hurt pulse | rhythm |

The organ voice is built on the tepih patch:
- VCO1 is sine and triangle with a little saw, and VCO2 is an octave up.
- No raster, mozaik or cross-modulation.
- Amp envelope 5 ms / 150 ms / .92 / 140 ms, so every eighth and sixteenth
  speaks and stops.
- Filter at 1.8 kHz with 0.3 keytrack.

## Kit

The rules are those of `docs/ma3-progenies.md`:
- The kick is on the fifth of the key: G#1 (MIDI 32, 52 Hz) under C# minor.
  It has derived accents, a 148 ms gate and a one-slot choke pool. The
  closing section runs sextuplets at 110 BPM, 91 ms apart.
- The pulse sits on the half-bar root an octave up, with a 401 ms gate.
- A snare closer than 200 ms to the previous kept pulse is not played.
  This drops 3 snares.
- **Blast bars** are bars with at least 7 snares over at least 8 kicks:
  46–49, where kick and snare both run eighth-note triplets at 140 BPM. In
  those bars the pulse plays beats 2 and 4 only.
- Toms, cymbals and hats are not played.

From the derived MIDI: 1452 kicks and 218 pulses, held peak 1 each.

## Form and controls

The energy curve follows the sections:

| from bar | section |
|---|---|
| 0 | keyboard intro |
| 4 | double kick at 150 BPM |
| 12 | backbeat |
| 20 | double kick |
| 28 | the 140 BPM break |
| 30–41 | ride and hat sections |
| 42 | organ entry |
| 46 | blast |
| 50 | sextuplet kick |
| 54–73 | sparse and backbeat bars |
| 74 | china section |
| 90 | keyboard interlude |
| 98 | the return |
| 114–137 | closing sextuplet kick |

The curve drives guitar HEAT (.20 + .25e) and choir BLOOM (.05 + .12e).
The choir and the drone get slow LFOs.

The show length is the last note-off (tick 265010) plus 4 s, 260 s in all.
