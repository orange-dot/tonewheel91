# MA1-6 identity and performance audition

Date: 2026-08-26. Status: deterministic listening companion complete.

This hosted exhibit exposes the completed MA1-6 identity and performance
routes without adding output conditioning or any MA1-7 core behavior. It
renders the factory one-voice path at 48 kHz with a fixed `.5` monitoring
gain.

## Run

```sh
make audition-ma1-6
```

The command writes ten 9-second stereo float WAV files under `build/`. Every
take uses MIDI channel 3, note 48 and velocity 96:

- `0.25 s`: NoteOn;
- `2.00 s`: selected control moves from zero to its audition endpoint;
- `3.50 s`: pitch-bend alone moves from `-2` to `+2` semitones;
- `5.00 s`: selected control returns to zero;
- `6.00 s`: NoteOff with ignored release velocity 64;
- `6.00..9.00 s`: factory release tail.

The reference receives the same note events and no control movement. Each
macro take drives one stored base macro from `0` to `1`; the other four remain
zero. Aftertouch means channel pressure `0 -> 1 -> 0`, which temporarily adds
`.45` Gravitacija and `.35` Ruin. Mod wheel temporarily adds `.35` Bloom and
`.50` Swarm. Full matching poly pressure adds a quarter octave of cutoff and
a `1.10` VCA multiplier.

## WAV files

| Take | File | Stereo FNV-64 |
| --- | --- | --- |
| reference | `build/ma1-6_reference.wav` | `f0ac7e9a891645c9` |
| Gravitacija | `build/ma1-6_macro_gravitacija.wav` | `050ed1936e6fdcb1` |
| Bloom | `build/ma1-6_macro_bloom.wav` | `e7553e487deaa799` |
| Heat | `build/ma1-6_macro_heat.wav` | `58a15aa584ea2629` |
| Ruin | `build/ma1-6_macro_ruin.wav` | `83e92e3baa55627d` |
| Swarm | `build/ma1-6_macro_swarm.wav` | `5bbed2f1d5b0297d` |
| channel aftertouch | `build/ma1-6_aftertouch.wav` | `8382112666e6338d` |
| mod wheel | `build/ma1-6_mod_wheel.wav` | `446f79b40e7db2d5` |
| pitch bend | `build/ma1-6_pitch_bend.wav` | `3d0b1c3f26a33805` |
| poly pressure | `build/ma1-6_poly_pressure.wav` | `f75377e4fabb3e6d` |

Each take renders twice and must repeat byte for byte. The renderer also
requires finite dual-mono output, nonzero energy, monitoring headroom and at
least 48,000 frames different from the reference for every controlled take.
GCC and Clang agree on every signature. The sanitizer build with ASan, UBSan
and float-cast-overflow produces the same PCM and reports no failure.

## Listening prompts

Compare each take with the reference, concentrating on `2.00..5.00 s`:

1. Are all five macro identities distinguishable without sounding like five
   unrelated effects?
2. Does aftertouch feel like temporary Gravitacija/Ruin pressure rather than
   a volume control?
3. Does mod wheel open Bloom/Swarm without rewriting the stored identity?
4. Is the pitch-bend motion clean through both the analog and Mozaik sources?
5. Is poly pressure audible as local brightness and ten-percent VCA lift?

This is listening evidence only. Stereo body, DC blocking and safety remain
owned by MA1-7.
