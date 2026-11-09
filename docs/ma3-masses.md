# MA3 Masses for the New Messiah

A Mamut Analog arrangement of Dimmu Borgir's *Masses for the New Messiah*
on the ma3_show desk. `driver/make_masses.py` writes the arrangement MIDI
and `driver/exhibit_ma3_masses.c` renders it in one pass. It is the fourth
show on the 2026-11-08 kit (`docs/ma3-mourning-palace.md`,
`docs/ma3-progenies.md`, `docs/ma3-spellbound.md`). It reuses the Mourning
Palace guitar, bass, pad and strings voices.

## Source

The source is a full-band transcription: two guitars, bass, two keyboard
tracks and a kit, at 480 PPQ.
- The tempo map alternates between 165 and 150 BPM: 165, then 150 from
  bar 38, 165 from bar 54, 150 from bar 150 and 165 from bar 182.
- Two of its seven entries repeat the tempo already in force. The derived
  map leaves them out.
- Pitched velocities are kept: 95 everywhere, except 79 on 184 choir notes.
- The keyboards play alone in bars 0–7.
- The song is in A minor.

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | gtr1 | Guitar I | driven saw/pulse, unison 2 | guitar |
| 1 | gtr2 | Guitar II | darker, VCO2 detuned the other way | guitar |
| 2 | bass | Bass | driven bass | bass |
| 3 | choir | Keyboard (prog. 52), held chords | Hurt pad voice, 2.5 s release | choir |
| 4 | strings | Keyboard (prog. 48), held peak 6 | tenor, 70 ms attack | strings |
| 5 | kick | kit: kick (36) | MA3 Hurt kick, one slot | rhythm |
| 6 | pulse | kit: snare (40) | MA3 Hurt pulse | rhythm |

The two guitars share 1391 attacks and stay on separate pools. The two
keyboard tracks share 236 attacks.

The dedupe pass removes 13 choir notes. These are same-pitch re-strikes
inside a held note: the earlier note ends at the re-strike, and exact
duplicates merge.

## Kit

The rules are those of `docs/ma3-spellbound.md`, with two differences.

**Kick pitch.** The other shows put the kick on the fifth of the key, kept
inside MIDI 32..37 (52–69 Hz). The fifth of A minor is E, and no E falls in
that register. So the kick sits on the tonic, A1 (MIDI 33, 55 Hz).

**Blast bars.** A blast bar has at least 8 snares over at least 8 kicks.
That gives bars 86–101 and 134–149, where kick and snare alternate at
165 BPM, one hit every 182 ms. In those bars the pulse plays beats 2 and 4.
Bar 201 is a 7-snare fill, so it stays a fill under the 200 ms rule.

Across the song, 8 snares are dropped under the 200 ms rule. Toms, cymbals
and hats are not played.

From the derived MIDI: 1792 kicks and 349 pulses, held peak 1 each.

## Form and controls

| from bar | section |
|---|---|
| 0 | keyboard intro |
| 10, 20 | backbeat verses |
| 30 | double kick |
| 38–53 | the 150 BPM half |
| 56 | verse |
| 66 | double kick |
| 74 | tom break |
| 78 | double kick |
| 86 | blast |
| 102 | ride section |
| 118 | double kick |
| 134 | blast |
| 150–181 | 150 BPM backbeat and sparse bars |
| 182 | ride section |
| 198–213 | closing double kick |

Energy drives guitar HEAT (.20 + .25e) and choir BLOOM (.05 + .12e).

The show length is the last note-off (bar 214, 318.3 s) plus a tail to the
next whole second and 4 s more: 323 s.
