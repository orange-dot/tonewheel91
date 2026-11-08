# MA3 Hurt with Orbita, 2026-11-07

`driver/exhibit_ma3_hurt_orbita.c` builds on the all-Mamut MA3 Hurt
arrangement, unchanged: 10 pools, 2167 backing and vocal notes, all 200 vocal
attacks, 253 s. It adds four things:

- Orbita resonance capture on two pools' VCO2 ratios;
- an Orbita moon layer as a sixth stem;
- a dirtier vocal;
- a single-pass render.

It is hosted only. The core is used through its public API, and no core file
changed.

```sh
make check-ma3-hurt-orbita      # score, pools and the Orbita timeline, no audio
make audition-ma3-hurt-orbita   # build/ma3-hurt-orbita/hurt_orbita_{stem}.wav
```

The inputs are the two local MIDI files and SHA256 values listed for MA3 Hurt
in the internal exhibits note. They are ignored and are not redistributed.

## Shared changes

- `driver/ma_orbita.{h,c}`: the Orbita primitive, moved unchanged out of the
  offline spike ([ma-orbita-spike-20261107.md](ma-orbita-spike-20261107.md)).
  All ten spike signatures are unchanged.
  - New `orbit_kick_relative`: `r *= 1 + .05 · amount · u`. The donor kick is
    absolute, so moons at 7/2 and above could never escape it (spike finding
    1). This exhibit uses the relative form. The spike keeps the donor form.
- `driver/ma3_hurt_parts.h`: the part setup of `exhibit_ma3_hurt.c`, moved out
  so both Hurt exhibits share it.
- `driver/ma3_show.{h,c}`:
  - an optional `layer` hook adds hosted audio to the stems every frame;
    `NULL` leaves a show unchanged;
  - `MA3_STEMS` goes from 5 to 6;
  - render is now **one pass that writes each WAV to its final path as it
    renders** (operator decision). There is no reference pass and no twin
    comparison. A failed pass removes every incomplete file.
- Blues and the base Hurt produce the same PCM after all three changes
  (first 30 s, every stem and mix): Blues mix `a3b077225ae39950`, Hurt mix
  `9f1187bf7cfa5820`. `make check-ma3-shows` passes.

## Orbita on VCO2

One moon per pool is the VCO2/VCO1 ratio. For the harmony, whose VCO2 sits an
octave down, the moon is VCO1/VCO2. It is written to the pool patch's fine
tune every 10 ms control step, as 15 Orbita ticks per step. The core applies
the integer interval unsmoothed, so the interval stays fixed. The fine must
stay within ±50 cents; the check fails on any clamp.

Kicks are deterministic relative displacements on bar lines. Kicks below the
1.5 % exit threshold would only make a 20 ms blip, because a captured moon
returns at a fixed slew. So every kick here is an escape, and it glides back
at the section's dissipation.

| Bars | Pad (interval 0, starts +16 c) | Harmony (Dubina, −12, starts 24 c flat) |
| --- | --- | --- |
| 0–22 | free at +16 c (d = 0): slow beating | silent until bar 64 |
| 22–24 | d 0 → .07: glides into exact unison | |
| 24–40 | locked; bar 32 kick +2.0 %, glides back | |
| 40–48 | bar 40 kick +2.4 % (+41 c), d = 0: stays sour | |
| 48–56 | d 0 → .06: relocks | |
| 56–64 | locked; bar 60 kick −2.0 % | |
| 64–80 | d = .10; ±2.2 % kicks every 2 bars, each escape gliding back | d = .08 from bar 64: the swell locks the octave |
| 80–85 | bar 80 kick −2.4 %, d = .04: one slow final lock | locked |

Timeline check: the pad moon has 13 captures, 12 escapes and 0 fine clamps,
and ends at +0.000 c. The harmony moon has 1 capture and 0 clamps, and ends
locked at exactly −1200 c.

## Orbita moon layer (stem `orbita`)

The layer has five moons (seed `0x48555254524f4231`). Their f0 is the half-bar
harmony root one octave up, MIDI 43–54, with a 60 ms glide. The chain is:

- the moon bank;
- a drive into an offset `tw_sat` (bias .35), so the inharmonic sum
  intermodulates instead of staying a clean sine cluster;
- a 20 Hz DC blocker;
- a one-pole lowpass at 4.2 kHz;
- its own chorus (mix .30) and reverb (mix .34, size .70).

Stem trim is .020, set by balance. Over the whole song the stem RMS is about
0.6× the tonal stem.

| Bars | Dissipation | Kirkwood | Proximity | Drive | Gain | Kicks (relative) |
| --- | --- | --- | --- | --- | --- | --- |
| 0–8 | 0 (free) | 0 | 0 | 1.6 | 0 until bar 4, then → .50 | — |
| 8–24 | .15 → .35 | 0 | 0 | 1.6 | .55 | — |
| 24–40 | .60 | 0 | 0 | 2.6 | .75 | .4 every 4 bars |
| 40–48 | .60 | 0 → 1 over 40–44 | 0 | 2.2 | .90 | — |
| 48–56 | .50 | 1 → 0 over 48–52 | 0 | 2.6 | .65 | 56 |
| 56–64 | .60 | 0 | 0 | 2.6 | .75 | 60 |
| 64–80 | .60 | 0 | 0 → 1 over 64–76 | 3 → 6 | .80 → 1 | .8 every 2 bars |
| 80–85 | .80 | 0 | 1 → 0 over 80–82 | 6 → 2 | 1 → 0 by 84.5 | — |

Timeline check:
- the layer has 31 captures, 24 escapes, 2 Kirkwood releases and 0 resets;
- all five moons are held by bar 16;
- the mean ring is 0.99 at bar 76, and every moon is held again by bar 80.

## Vocal

Per-note harmonic purity is `H1² / ΣHk²` over harmonics 1–10, measured on the
middle 60 % of each vocal note in the vocal stem. A note at ≥ .90 is
sine-like. The measurement script is hosted Python, kept outside the repo.

| Render | Median purity | Sine-like (≥ .90) |
| --- | ---: | ---: |
| base MA3 Hurt (`build/ma3-hurt-base-20261107`) | .993 | 200 / 200 |
| this exhibit | .537 | 3 / 200 (see Results) |

The base vocal is saw .16, triangle .50 and sine .34 into a filter that closes
from 850 to 240 Hz, so every note is close to a sine. The new vocal:

- **VCO1:** saw .44, pulse .19 (PW .38), triangle .18, sine .18.
- **VCO2:** saw .26, pulse .16 (PW .30), triangle .12, sine .10; level .46,
  fine −9 c.
- **Modulation and noise:** sync .06 (softness .40), cross-mod .05, noise .02,
  BCS .26/.32.
- **Filter:** cutoff 1220 → 700 Hz over the form, keytrack .40, resonance
  .18 → .26, drive .26 → .41, mixer pressure .20 → .32.
- **Output:** body drive .33 → .45 over the form, master .30. Channel
  pressure is .14 + .20 · energy.

Tuning ran on the first 60 s. An over-driven first take had median purity .03:
the fundamental sat 11 dB below the third harmonic, and the vocal was 7 dB
quieter. Takes at .744 and .214 bracketed the target. The full song then
still left four high climax notes (MIDI 62–69, bars 69–73) sine-like, where
the filter is most closed.
- Keytrack cannot fix this. It scales cutoff by
  `1 + (note−60)/48 · keytrack · .42` (`ma-constants.md` §7), only about
  +3 % at MIDI 64, so .80 left three of them.
- The fix is grit after the filter instead: body drive rises with the form,
  and the cutoff stops at 700 Hz instead of 600.

## Results

Single-pass render, GCC 16.2.1, `-O2 -ffp-contract=off`, 48 kHz stereo,
253 s:

| Stem | Peak | RMS | FNV-64 |
| --- | ---: | ---: | --- |
| tonal | .029572 | .005347 | `11baebe3a6c3f839` |
| rhythm | .026016 | .003199 | `20af68883dca23cd` |
| melody | .014068 | .001445 | `9a80cb567592c4de` |
| vocal | .020886 | .002664 | `d7114b29ea264a6b` |
| harmony | .009012 | .000399 | `9dbbf1880014695f` |
| orbita | .010409 | .003104 | `545f254c2c3a43c2` |
| mix | .046368 | .007630 | `0b90ecc2c41a6566` |

- Rhythm and melody are bit-identical to the base Hurt render of the same
  day (`20af68883dca23cd`, `9a80cb567592c4de`), since neither changed.
- Tonal differs only through the pad moon.
- No held or sustained steals, no FX clips, and every voice is asleep at the
  end.
- The render took 14 min 51 s on the 4-thread host, with no other render
  running.

Vocal purity over the full song:

| Bars | Notes | Median purity (base → this) | Sine-like (≥ .90) |
| --- | ---: | --- | ---: |
| 0–24 | 52 | .977 → .515 | 0 |
| 24–40 | 41 | .991 → .539 | 0 |
| 40–56 | 34 | .991 → .520 | 0 |
| 56–64 | 19 | .997 → .528 | 0 |
| 64–80 | 44 | 1.000 → .559 | 3 |
| 80–85 | 10 | 1.000 → .552 | 0 |
| all | 200 | .993 → .537 | 3 (base 200) |

Three notes stay above .90. They are MIDI 62 at bars 69.6 and 69.9, and
MIDI 69 at bar 73.4. All three are upper notes of a dyad over a held 64,
where the filter is most closed. Their second harmonic is −10 to −22 dB, so
they are softer than the rest, but not sines; a sine would sit below −25 dB.
The vocal RMS is .00266, against .00236 in the base.

## Mastered listening mixes

The mixes are rebuilt from the rendered stems, with no re-render
(`driver/remix_stems.py`). The script first checks that the six stems re-sum
in float32 to the rendered mix bit for bit. Then:
- each stem gets one static gain;
- the stems are summed in float64;
- one static master gain puts the sample peak at −1 dBFS.

There is no compression, limiting or gain riding.

```sh
python3 -I driver/remix_stems.py build/ma3-hurt-orbita/hurt_orbita \
  build/ma3-hurt-orbita/hurt_orbita_master_vocal-6.wav \
  --stems tonal rhythm melody vocal harmony orbita --gain vocal=-6
```

| File | Vocal | Master gain | Peak | RMS | SHA256 |
| --- | ---: | ---: | ---: | ---: | --- |
| `hurt_orbita_master_vocal-6.wav` | −6 dB | +26.03 dB | −1.00 dBFS | −16.74 dBFS | `73979265…` |
| `hurt_orbita_master_vocal-9.wav` | −9 dB | +26.13 dB | −1.00 dBFS | −16.72 dBFS | `250b38fe…` |

In the rendered mix the vocal sits about 8.6 dB RMS under the rest of the
mix. The −6 dB remix moves it to about 14.6 dB under; the −9 dB remix to
about 17.6 dB under. The rendered mix peaks at −26.7 dBFS, so the master
gain raises everything else by about 26 dB.

## Vocal v3: darker, wetter, longer

The v2 vocal above (pulses at PW .30–.38, sync .06, BCS .26/.32, cutoff
1220 → 700 Hz) read as nasal. v3 changes only the vocal pool:

- **Sources:** no pulse and no sync. VCO1 is saw .50, triangle .22, sine .14.
  VCO2 is saw .30, triangle .20, sine .08, at −11 c. Cross-mod .02, noise .015,
  BCS .16/.24.
- **Filter:** cutoff 900 → 620 Hz, resonance .10 → .14, filter drive
  .30 → .52, mixer pressure .22 → .32, envelope amount .14.
- **Output:** body drive .30 → .56, master .19 (stem RMS matched to v2).
- **Envelopes:** amp ADSR 40 / 420 / .85 / 900 ms; filter ADSR
  30 / 520 / .40 / 900 ms. Channel pressure .10 + .12 · energy.
- **Room:** chorus .22 (depth .35, rate .25 Hz); reverb .45, size .82,
  damping .72. Before: chorus .08; reverb .18, size .65, damping .55.

`--vocal-only` renders only the vocal events, with no moon layer. Pools are
independent, so its vocal stem is bit-identical to the vocal stem of a full
render (checked on the first 30 s: `f589fdc29f506db2` both ways). The whole
song takes about 85 s instead of about 15 min.

```sh
build/exhibit_ma3_hurt_orbita --render build/ma3-hurt-orbita-vo/hurt_orbita --vocal-only
mv build/ma3-hurt-orbita-vo/hurt_orbita_vocal.wav build/ma3-hurt-orbita/hurt_orbita_vocal_v3.wav
python3 -I driver/remix_stems.py build/ma3-hurt-orbita/hurt_orbita \
  build/ma3-hurt-orbita/hurt_orbita_master_vocal-6_v3.wav \
  --stems tonal rhythm melody vocal harmony orbita --gain vocal=-6 \
  --replace vocal=build/ma3-hurt-orbita/hurt_orbita_vocal_v3.wav --keep-master
```

| Vocal stem | RMS | Centroid | 0.8–2.5 kHz | > 2.5 kHz | Median purity | Sine-like |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| v2 `hurt_orbita_vocal.wav` | .00266 | 334 Hz | −14.7 dB | −47.9 dB | .537 | 3 |
| v3 `hurt_orbita_vocal_v3.wav` (FNV `c256b5af2467a13c`) | .00285 | 266 Hz | −22.4 dB | −57.7 dB | .657 | 8 |

Band levels are relative to the stem's total energy.

- The nasal band drops by 7.7 dB.
- The eight notes above .90 are the highest notes, MIDI 62–69, in the
  darkest stretches (bar 33 and bars 69–73). Their second harmonic is −10
  to −12 dB. The darker filter rounds them off, but they are not sines.
- The dark reverb tail also carries mostly fundamentals, which raises the
  measured purity.

The third mix, `hurt_orbita_master_vocal-6_v3.wav` (SHA256 `f58f9d18…`),
keeps the stem gains of the vocal −6 dB mix and its master gain
(`--keep-master`, +26.0294 dB). Every other stem therefore has exactly the
same output level as in that mix. The peak is −1.05 dBFS and RMS is
−16.73 dBFS. Re-running the vocal −6 dB mix with the extended script
reproduces its file bit for bit.

## Vocal v4: no nasal band

v3 still read as a duck call, most clearly at the start. Two measurements
locate the cause:

- **Not the attack.** The centroid of the first 60 ms of a note sits only
  +1 to +6 Hz above the note's middle. Velocity still scales the filter
  envelope amount (`.25 · velocity curve`, `ma-constants.md` §7), so v4 also
  holds the filter envelope at 1 to rule this out.
- **The tone itself.** The issue is energy in the 0.8–2.5 kHz band: −20.2 dB
  in bars 0–24 of v3, against −31.2 dB in the base vocal. Post-filter body
  drive regenerates odd harmonics above the cutoff, straight into that band.

v4 moves all grit in front of a steep, resonance-free, nearly fixed lowpass:

- **Sources:** VCO1 saw .55, triangle .25, sine .15. VCO2 saw .32,
  triangle .20, sine .08, −11 c. No cross-mod; noise .01; BCS .08/.20.
- **Filter:** cutoff 620 → 580 Hz, resonance .04, filter drive .50 → .60,
  mixer pressure .42 → .50, envelope amount 0, filter ADSR 1 / 1 / 1 / 900 ms.
- **Output:** body drive .10, master .134 (stem RMS .00285, as v3).
- **Envelopes and room:** amp ADSR 60 / 420 / .85 / 900 ms; chorus .28;
  reverb .55, size .85, damping .75.

| Vocal stem | Nasal band, bars 0–24 / all | Centroid (note middle) | Median purity | Sine-like |
| --- | --- | ---: | ---: | ---: |
| base | −31.2 / −43.8 dB | 187 Hz | .993 | 200 |
| v2 | −11.4 / −15.2 dB | 332 Hz | .537 | 3 |
| v3 | −20.2 / −22.9 dB | 265 Hz | .657 | 8 |
| v4 (FNV `92ec5c03bc5dfbec`) | −28.8 / −28.6 dB | 216 Hz | .817 | 28 |
| v4 with less saw and drive (not kept) | −37.3 / −37.9 dB | 190 Hz | .966 | 191 |

With a single lowpass, harmonic richness and a quiet nasal band trade
against each other directly. v4 sits between the two failure modes. Fourth
mix: `hurt_orbita_master_vocal-6_v4.wav` (SHA256 `0dc166c3…`), with the same
stem gains and master as the vocal −6 dB mix. Peak −1.02 dBFS.

## Vocal v5: Glas formants and grains

v4 showed that one lowpass trades harmonic richness against the nasal band.
v5 uses the new Glas unit (`ma-constants.md` §10C): a four-formant bank after
the ladder, Orbita formants and FOF-style formant grains.

- **Sources:** the v4 saw base plus formant grains at .30.
- **Ladder:** cutoff 1100 → 900 Hz, no resonance, filter drive .30 → .40,
  mixer pressure .30 → .38. It supplies grit and keeps F2–F4 of the grains
  out of the nasal band.
- **Formant bank:** mix .70, shift −.40 (about −0.2 octave). The vowel moves
  from .08 to .30 over the form, from u towards o.
- **Output:** body drive .10, master .124 (stem RMS .00285, as v3).
  Envelopes and room are the same as v4.

There are two takes; `--vocal-orbita` sets the Orbita formant amount:

| Take | Orbita | Stem RMS | Centroid | Nasal band | Mix |
| --- | ---: | ---: | ---: | ---: | --- |
| `hurt_orbita_vocal_v5a.wav` | .55 | .00285 | 415 Hz | −23.1 dB | `hurt_orbita_master_vocal-6_v5a.wav` |
| `hurt_orbita_vocal_v5b.wav` | 0 | .00182 | 382 Hz | −20.5 dB | `hurt_orbita_master_vocal-6_v5b.wav` (`--trim vocal=3.88`) |

- Locking each formant onto a harmonic puts more energy through the bank:
  take b, whose formants sit between harmonics, is 3.9 dB quieter at the
  same settings.
- A locked F1 can make one harmonic dominate (purity .124 on take a).
- Both mixes keep the vocal −6 dB stem gains and master (+26.0294 dB).
  Take b is level-matched with `--trim`, which applies to the replaced stem
  only and does not move the master.
