# MA1-6R — Mamut sine, patch bank and Patchlab

Date: 2026-08-26. Status: implementation and hosted evidence complete.

MA1-6R is an interstitial vertical slice before MA1-7. It makes the current
one-card voice useful for sound design without pulling allocation, effects or
final output safety forward. The freestanding core remains fixed-state C23;
file I/O, directory scanning, ALSA and terminal handling remain in `driver/`.

## Landed boundary

- Both VCOs expose the same enriched Mamut-sine weight. It combines a softly
  driven fundamental with a quiet phase-offset second harmonic and uses a
  pinned peak normalization.
- `ma_patch` is one concrete value object, not a generic parameter registry.
  `ma_synth_init_patch` and `ma_synth_apply_patch` sanitize it; apply resets
  the one-card voice and DSP state while retaining the sample rate.
- Three compiled constants are exact public defaults: Tepih, Lead and Dubina.
  Their files in `patches/mamutanalog/` must decode byte-for-byte to the C
  values in hosted tests.
- `.mapatch` version 1 is strict `key=value` text: one name and all 45 fields
  are required; unknown, duplicate, missing, non-finite, fractional-integer
  and out-of-domain values fail. Writing uses enough digits for exact f32
  round trips. Save writes, flushes and `fsync`s a sibling temporary file,
  then renames it over the destination.
- Patchlab is a concrete C program. Headless modes do not open ALSA. Live mode
  uses the existing synchronous ALSA writer as its clock and adds a small
  ANSI/termios editor; there is no ncurses, GUI toolkit, background thread,
  callback graph or plug-in layer.

## Patches

Tepih remains the factory sound. Lead retains the MA1-6P direct, short-
envelope voice. Dubina is the first VCO2-sine-dominant sound:

| Group | Dubina |
| --- | --- |
| VCO1 | saw `.10`, triangle `.10`, sine `.15`, level implicit `1` |
| VCO2 | saw `.05`, triangle `.10`, sine `.85`, level `.90`, interval `-12`, fine `0` |
| Source | sync `0`, cross-mod `.04`, noise `.01` |
| Mozaik | mix `.05`, golden slope/contrast, phason `0`, drift `.02` |
| Filter | pressure `.18`, cutoff `750 Hz`, resonance `.20`, drive `.20`, envelope `.38`, keytrack `.35` |
| Amp ADSR | `30 / 300 / .80 / 700 ms` |
| Filter ADSR | `20 / 450 / .45 / 600 ms` |
| Identity | Gravitacija `.20`, Bloom `0`, Heat `.18`, Ruin `.04`, Swarm `0` |
| Output | body `.18`, width `.35`, crossfeed `.12`, master `.18` |

The shipped files, rather than this summary table, are the exhaustive patch
record.

## Patchlab

```sh
./build/patchlab --list
./build/patchlab --dump Tepih
./build/patchlab --render Dubina build/dubina.wav
./build/patchlab -d hw:CARD=AG06AG03 -m hw:X,Y,Z --patch Tepih
```

Live controls:

- `p` / `P`: next / previous patch; selection performs the documented full
  reset at the next period boundary;
- arrows or `+` / `-`: select and fine-edit a field; `[` / `]` use the field's
  coarse step. Grouped core setters and MA1-6 smoothers receive edits;
- `zsxdcvgbhnjm,`: toggle C3 through C4, and space releases the current note;
- Ctrl-S saves, Ctrl-N starts non-blocking save-as, Ctrl-R reloads, `q` quits;
- raw MIDI handles notes, channel pressure, poly pressure, mod wheel and exact
  endpoint pitch bend. CC16..20 edit Gravitacija through Swarm; CC120/123
  release the active one-card voice.

## Evidence

`make audition-ma1-6r` writes the five common 14-second comparison WAVs and
three 12-second Patchlab renders at 48 kHz. The closing GCC signatures are:

| Take | Peak | RMS | Stereo FNV-64 |
| --- | ---: | ---: | --- |
| Tepih reel | `.218247` | `.048227` | `86bd2977cfdfda45` |
| Lead reel | `.251792` | `.050829` | `c62c23f3766f6955` |
| Dubina reel | `.234811` | `.110317` | `57b228c4e2a8de39` |
| Tepih sine off | `.237115` | `.048398` | `eb7d0c1253507751` |
| Tepih sine on | `.207588` | `.043748` | `ed3516126ed8d7e9` |
| Patchlab Tepih | `.117800` | `.031598` | `28787fba70a2e465` |
| Patchlab Lead | `.127797` | `.029792` | `7fbb7fa6dc495b39` |
| Patchlab Dubina | `.126112` | `.058553` | `8629294ca611fe05` |

Every reel is finite, dual-mono, below the fixed `.5` monitoring headroom and
byte-identical on repeat. The matched Tepih sine pair differs in `475322`
frames. Both VCOs pass the same 44.1/48/96/192 kHz spectral gates: H2
`-24.00`, H3 `-19.08`, H5 `-36.67 dBc`, with out-of-contract energy below
`-94 dBc`. The live loop was smoke-tested through the ALSA `null` PCM in a
real pseudo-terminal (screen, initial patch, render clock, quit, zero xruns);
physical audio/MIDI hardware remains an operator check, not automated proof.

MA1-7 is still unstarted. Body, width and crossfeed remain stored/smoothed
destinations, while master is stored but not yet in the render path; this
slice does not claim final stereo, DC-block or safety behavior.
