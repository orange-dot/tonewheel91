# MA3 Mourning Palace

A Mamut Analog arrangement of Dimmu Borgir's *Mourning Palace* on the
ma3_show desk: `driver/make_mourning_palace.py` writes the arrangement MIDI,
`driver/exhibit_ma3_mourning_palace.c` renders it in one pass.

Source: a full-band transcription (two guitars, bass, four keyboard tracks,
kit) at 480 PPQ with a four-entry tempo map: 120 BPM, 195 BPM from tick
178080 (bar 92.75), 120 BPM from 193440 (bar 100.75), 180 BPM from 224160
(bar 116.75). The generator checks the source SHA256, the tempo map, the
track names and per-track note counts. Every pitched note in the source has
velocity 95, and the kit has no dynamics beyond a few accents. The source is
local and is not tracked.

## Parts

| ch | part | source | voice | stem |
|---|---|---|---|---|
| 0 | gtr1 | Guitar I | driven saw/pulse, unison 2, 7 cents | guitar |
| 1 | gtr2 | Guitar II | same, darker filter, VCO2 detuned the other way, 9 cents | guitar |
| 2 | bass | Bass | driven bass, 220 ms release | bass |
| 3 | synth | Keyboards (prog. 82), doubles the low strings | dark lead | keys |
| 4 | strings | Keyboards (prog. 48), low ensemble | tenor, 70 ms attack | keys |
| 5 | pad | Keyboards (prog. 48), held chords | pad, 2.5 s release | pads |
| 6 | pizz | Keyboards (prog. 45), sixteenth figure | short struck voice | pizz |
| 7 | kick | Drums: kick | MA3 Hurt kick (`drum_patch(KICK)`), one slot | rhythm |
| 8 | pulse | Drums: snare | MA3 Hurt pulse (`drum_patch(PULSE)`) | rhythm |

The two guitars share 1664 of their attacks. They stay on separate pools,
so the shared notes sound as a double-tracked pair: separate seeds, opposite
VCO2 fine tune, and different detune spreads. Guitar release is 160 ms so
sixteenth-note chugs stay separate.

## Kit

The kit is the MA3 Hurt pair: a tonal kick and a pitched pulse. The source's
kick and snare keep their rhythm. Toms, cymbals and hats are not played.

Kick:
- Pitch is C#2 (MIDI 37, 69 Hz), the fifth of the song's F# minor. The Hurt kick
  sat on B1 (MIDI 35).
- The gate is the Hurt kick's 148 ms (96 ticks at its tempo), or the time to
  the next hit if that is shorter.
- The pool has **one slot**. Each hit steals the previous voice in its
  release through the pool's 5 ms steal crossfade, so the kick chokes like a
  drum machine. Every note-on starts a voice with fresh random oscillator
  phase, so on a multi-slot pool the double-kick runs (sixteenths at
  120 BPM, sextuplets at 120 BPM, sixteenths at 180 BPM, 83–125 ms apart)
  would stack several decaying same-pitch tails with unrelated phases.
- Velocity: the source has none. Spaced hits use the Hurt accents, 113 on
  the one and 94 elsewhere. A hit is in a run when a neighbouring kick is
  within 160 ticks. Run hits get 116 on the one, 106 on beats, 92 on
  eighths and 74 in between. A deterministic ±3 scatter on the sixteenth
  grid is added to every hit. The velocity curve is shallow (gain
  0.25 + 0.75·level), so 106 against 74 is about 2.5 dB.

Pulse:
- Each source snare becomes a pulse at the half-bar root an octave up. This
  is the Hurt rule: the lowest bass attack in the half bar, else the lowest
  guitar attack, else the previous root, folded into MIDI 31..42.
- Velocity is 84 on beats, 80 on eighths, 72 on sixteenths, and 96 for the
  six source accents.
- The gate is the Hurt pulse's 401 ms, or the time to the next pulse.
- Bars 165–180 are a blast beat: the snare alternates with the kick on
  sixteenths at 180 BPM, 7–8 hits a bar. There the pulse plays the
  backbeat (beats 2 and 4, velocity 92) and the kick keeps its eighths.
  The pulse's 80 ms attack cannot articulate 167 ms repeats.

Kit numbers from the derived MIDI: 1586 kicks (held peak 1), 328 pulses
(held peak 1).

## Form and controls

`controls()` runs every 480 frames on the exact tempo-map position. The
energy curve follows the transcription's sections: keyboard intro (bars
0–4), verse beat (5), double kick (24–35), second verse (36), double kick
(49–60), half time (63), china bars (75), fast passage (93), sextuplet kick
(101), break (117), 180 BPM double kick (125), sparse bars (149) and the
closing blast (165). Energy drives guitar HEAT (.20 + .25e), pad BLOOM
(.05 + .12e) and the lead's mod wheel (.06 + .12e).

The show runs for the last note-off (bar 181.75) plus 4 s. The desk's
presentation fade covers the last 2 s.

## Desk capacity

The arrangement has 18,486 note events. `MA3_EVENTS` in `driver/ma3_show.h`
was raised from 16,000 to 24,000. Only the size of the event array changes;
a short render of an existing exhibit is byte-identical before and after.
