# MA3 Progenies of the Great Apocalypse

A Mamut Analog arrangement of Dimmu Borgir's *Progenies of the Great
Apocalypse* on the ma3_show desk: `driver/make_progenies.py` writes the
arrangement MIDI, `driver/exhibit_ma3_progenies.c` renders it in one pass.
It follows the Mourning Palace arrangement of the same day
(`docs/ma3-mourning-palace.md`) and reuses its kit rules and its guitar and
bass voices.

Source: a full-band transcription, 480 PPQ, one tempo (140 BPM), tracks
named after the players. The two guitar tracks are note-for-note identical,
and so are the first two keyboard tracks; the generator checks both, along
with the SHA256, the tempo, the names and the per-track note counts.
Every note has velocity 127. Pitched parts are played at 95, the level the
shared voices were balanced at. The source is local and is not tracked.
In bars 113–128 the keyboards play alone.

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | gtr1 | guitar (Galder) | driven saw/pulse, unison 2, 7 cents | guitar |
| 1 | gtr2 | guitar (Silenoz), identical notes | darker, VCO2 detuned the other way, 9 cents | guitar |
| 2 | bass | bass (Vortex), MIDI 23–34 as written | driven bass | bass |
| 3 | strings | keyboard (prog. 48); its doubled track is not taken | tenor, 90 ms attack | strings |
| 4 | brass | keyboard (prog. 61) | saw-forward tenor, filter swell | brass |
| 5 | choir | keyboard (prog. 52), 26 held notes | Hurt harmony voice, slow | choir |
| 6 | kick | kit: kick (35) | MA3 Hurt kick, one slot | rhythm |
| 7 | pulse | kit: snare (40) | MA3 Hurt pulse | rhythm |

The identical guitar tracks give a double-tracked pair. The two pools have
separate seeds, opposite VCO2 fine tune and different detune spreads.

## Kit

These rules are the same as in Mourning Palace:
- The kick is tuned to the fifth of the key: A#1 (MIDI 34, 58 Hz) under D# minor.
- The kick gate is 148 ms, or the time to the next hit if that is shorter.
- The kick pool has one slot, so each hit chokes the previous one.
- The kick velocities are derived: Hurt accents on spaced hits, and lifted
  one, beats and eighths in runs.
- The kick runs here are sextuplets at 140 BPM, 80 ticks (71 ms) apart.
- The pulse is placed at the half-bar root an octave up, with the 401 ms
  gate or the time to the next pulse.

These rules are specific to this song:
- **Blast bars** are bars with at least 7 snares over at least 16 kicks:
  129–130 and 137–142, where the snare runs in eighth-note triplets against
  the sextuplet kick. In these bars the pulse plays beats 2 and 4 only.
- **Fills** in bars 39, 71 and 91 are snare rolls 60–160 ticks apart. A
  snare closer than 200 ms to the previous kept pulse is not played: 33
  snares in total. The pulse's 80 ms attack cannot articulate faster
  repeats.

From the derived MIDI: 1486 kicks and 206 pulses, held peak 1 each.

## Form and controls

The energy curve follows the transcription's sections:

| from bar | section |
|---|---|
| 0 | opening crash pattern |
| 16, 24, 32 | verse beats |
| 40 | ride section |
| 48 | opening pattern |
| 64 | sextuplet kick |
| 72, 80 | sparse bars |
| 96, 104 | kick runs |
| 113 | orchestral interlude |
| 129 | blast |
| 145 | opening pattern to the end |

Energy drives guitar HEAT (.20 + .25e), choir BLOOM (.05 + .12e) and the
brass mod wheel (.04 + .10e).

The show length is the last note-off (bar 160.62) plus 4 s, 280 s in all.
