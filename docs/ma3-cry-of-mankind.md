# MA3 The Cry of Mankind

A Mamut Analog arrangement of My Dying Bride's *The Cry of Mankind* on the
ma3_show desk. `driver/make_cry_of_mankind.py` writes the arrangement MIDI
and `driver/exhibit_ma3_cry_of_mankind.c` renders it in one pass. The kit is
the 2026-11-08 kit from the Dimmu Borgir shows (`docs/ma3-spellbound.md`),
with one change for this song, described under Kit.

## Source

- Unnamed tracks: a rhythm guitar, the high guitar line that carries the
  motif through all 198 bars, bass, piano and a kit.
- 960 PPQ, 4/4, one tempo of 100 BPM, 7:55.
- Pitched notes are a flat 95. The derived MIDI keeps 960 PPQ, and at 48 kHz
  one tick is exactly 30 frames.
- The song is in C# minor.

Two other local transcriptions were not used. One is a live take (78 BPM,
ritardando). The other is three semitones higher and its kit plays only the
kick.

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | gtr | rhythm guitar (chords up to a bar long) | driven guitar (Mourning Palace) | guitar |
| 1 | lead | the high line (MIDI 56–68, 640-tick notes) | orchestration lead (dark lead), unison 2 | lead |
| 2 | bass | bass (MIDI 25–44) | driven bass | bass |
| 3 | piano | piano (bars 36–43, 68–83, 124–147) | struck keys voice (industrial) | piano |
| 4 | kick | kit: kick (35) | MA3 Hurt kick, one slot | rhythm |
| 5 | pulse | kit: snare (40) | MA3 Hurt pulse | rhythm |
| 6 | toms | kit: toms (41, 43, 45, 47) | MA3 Hurt pulse voice, own pool | rhythm |

## Kit

The kick:

- sits on G#1 (MIDI 32, 52 Hz), the fifth of C# minor;
- uses derived accents, a 148 ms gate and a one-slot choke pool;
- has no runs: the slow tempo spaces the hits at least 200 ms apart.
  The run threshold is time-based here (167 ms, the 160 ticks at 120 BPM of
  the other shows).

The snare plays the pulse on the half-bar root an octave up, with the
401 ms gate. No bar qualifies as a blast.

The toms are the change. In bars 20–35 and 160–167 the groove is on the
toms: eleven hits a bar against one snare. Dropping them, as the other shows
do, would leave those bars to two kicks and a snare. So they play on the
pulse voice, in a channel and pool of their own:

- floor toms (41, 43) on the half-bar root;
- rack toms (45, 47) on its fifth;
- velocity 66 (the snare's is 84).

Snare and toms are thinned as one stream by the 200 ms rule, which drops
37 hits. Cymbals and hats are not played.

From the derived MIDI: 840 kicks, 233 snare pulses and 236 tom pulses, each
at held peak 1. The source has 877 kick notes, because some ticks carry two
kicks.

## Form and controls

| from bar | section |
|---|---|
| 0 | the line alone |
| 4 | ride and kick |
| 20 | tom groove |
| 36 | full band with piano |
| 68, 124 | piano returns |
| 160 | tom groove |
| 168 | drums stop |
| 169 | bass and line |
| 183–197 | line alone |

Energy drives guitar HEAT (.20 + .25e) and the lead's mod wheel
(.06 + .12e).

The show length is the last note-off (bar 198, 475.2 s) plus 4 s, so 480 s.

## Edition 2: humanized, wetter

Edition 2 is built with `make_cry_of_mankind.py --edition 2` and played with
`exhibit_ma3_cry_of_mankind --edition 2`. Edition 1 output is unchanged.

The humanization is deterministic, from a fixed-seed hash per note, so there
is no global RNG.

**Timing**

- Each note moves by a per-part offset plus a triangular jitter, and keeps
  its length.
- Offset and jitter sd per part:

  | part | offset | jitter sd |
  |---|---|---|
  | kick | 0 ms | 3 ms |
  | bass | +4 ms | 5 ms |
  | snare pulse | +3 ms | 5 ms |
  | guitar | +2 ms | 6 ms |
  | toms | +2 ms | 6 ms |
  | piano | −1 ms | 6 ms |
  | lead | 0 ms | 7 ms |

- Chords strum upward in pitch order: 7 ms per string on the guitar, a 3 ms
  roll on the piano.

**Velocity**

- Pitched parts get a metric accent: +6 on the one, +2 on the other beats,
  −2 on offbeat eighths and −4 elsewhere.
- On top of that, a four-bar swell of ±4 and a triangular scatter of 5–7
  depending on the part.
- The kit keeps its derived accents and gets a scatter of 5–6 on top.

**Overlaps**

- The kick gate is clipped to the next moved hit, so the one-slot choke
  still holds.
- Same-pitch overlaps resolve as in edition 1.

**Mix**

- Chorus goes up on every part.
- Reverb mix goes up on every part: lead .42, piano .40, guitar .30.
- Guitar, lead, piano and the pulse voices get a larger room: size .82,
  damping .50.

## Edition 3: cutting lead

`exhibit_ma3_cry_of_mankind --edition 3` plays the edition-2 score and mix.
The lead line moves to a cutting voice.

The voice:

- VCO1 is saw plus pulse (pulse width .38).
- VCO2 is saw, detuned +8 cents.
- Sync and cross-modulation are on.
- Resonance and pre-filter drive are up.
- Every note gets a fast filter bite (6 ms attack, 320 ms decay to .30,
  amount .40).

The voice opens with the form. The energy curve e drives these controls
together, every 480 frames:

| control | value |
|---|---|
| cutoff | 1000 + 2000e Hz |
| resonance | .24 + .16e |
| drive | .22 + .28e |
| sync | .08 + .16e |
| body | .18 + .14e |
| HEAT | .10 + .15e |
| vibrato | .05 + .20e |

The line is darkest where it plays alone, at the start and at the end. It is
brightest and roughest in the band sections with the piano.

## Edition 4: cutting guitar line

`exhibit_ma3_cry_of_mankind --edition 4` plays the edition-2 score and mix.
The guitar line (channel 0) moves to the edition-3 cutting voice, with the
same energy automation. This line is the sustained melodic line: single held
notes from bar 36. The lead motif stays on its edition-2 voice.

## Edition 5: octave-doubled guitar line

Generator `--edition 3` adds the octave above every guitar note that has no
octave partner at the same tick. It does this before humanization, so each
added note gets its own timing and velocity. This is the texture the source
itself has in bars 92–100.

`exhibit_ma3_cry_of_mankind --edition 5` plays that score with the edition-4
guitar voice. The guitar pool has 16 slots, to make room for the held peak
of 5 at unison 2.

## Edition 6: kick on its own stem

`exhibit_ma3_cry_of_mankind --edition 6` plays edition 5 with the balance of
its listening mix folded into the part gains, and puts the kick on a sixth
stem. Bass and kick can then be set apart from the rest in the mix.
