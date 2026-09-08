# Hurt industrial arrangement

Historical listening take, superseded on 2026-09-07 by
[the dark Mamut revision](ma-hurt-dark.md) after listening feedback rejected
the EP and metallic layers. The old WAVs remain available. Its renderer was
reworked as `exhibit_ma_hurt_dark`; the build commands below record the old
take and no longer describe the current target. To prepare the old three-stem
WAV set with the current Python tool, pass `--stems tonal rhythm ep` explicitly.

Hosted reinterpretation of the local Hurt MIDI: Mamut percussion and bass,
MA2 shared stereo pads/textures, and ep73 responses. The source and generated
audio stay local. No public DSP API changes are needed.

## Tasks

- [x] Inspect source meter, phrases, note ranges and simultaneous ownership.
- [x] Implement a deterministic tick-based arrangement and separate renderer.
- [x] Add Mamut kick, noise backbeat, Raster metal, and syncopated bass.
- [x] Add EP voicings/responses and phrase-controlled MA2 character/stereo.
- [x] Build and check scheduling, repeatability, boundaries and mix levels.
- [ ] Render previews, full mix, aligned stems and constant-gain listening copy.
- [ ] Record commands, measured results and remaining listening limitations.

The input has 480 PPQ, 4/4, one 714285 us/quarter tempo and 1490 note-ons.
Zero-based channels: 0 pluck (six simultaneous notes), 2 high melody,
4 bass, 6 late guitar chords, 9 bell/drum notes. Source velocity is uniformly
80; the new performance supplies deliberate accent dynamics.

Zero-based bar boundaries: 0 intro, 8 groove entrance, 24 first full section,
40 breakdown, 48 return, 64 climax, 80 withdrawal, 85 release; 253 s total.
The existing Noir render remains the level-matched comparison reference.

## Performance

The six original pluck voices and three late tenor/guitar voices retain their
Noir patches. Channel 2 becomes a more articulate two-voice Mamut melody;
new harmonic pads support it instead of turning every melody change into a
long parallel chord. Original channel 9 events feed a separate tonal texture.
The source bass is replaced by a syncopated part using its pitch classes.

Harmony is derived every half bar from source bass/guitar events. Where only
the high melody remains, the most recent matching bass voicing supplies the
chord. Sparse passages use open fifth/octave voicings rather than inventing
a major/minor third. EP plays lightly rolled three-note voicings, with high
answers every fourth bar outside the climax; repeated EP pitches cannot
overlap. This is a deterministic arrangement heuristic, not a general chord
recognition engine.

The industrial groove has a backbeat on beat three, kicks on sixteenth-grid
steps 0/6 (plus 11 in full sections), and bass on 2/5/9/14. Alternate bars add
a quiet snare pickup; fourth bars add snare/metal fills. Metal eighth notes
expand from offbeats to a full pulse. The kick briefly reduces bass gain by
up to 20%, with an approximately 69 ms exponential decay. Other stems are
unaffected by that envelope.

| Zero-based bars | Approximate seconds | Role |
| --- | --- | --- |
| 0–8 | 0–22.86 | EP/pluck opening, pads and sparse kicks enter at bar 4 |
| 8–24 | 22.86–68.57 | Backbeat, syncopated bass, offbeat metal |
| 24–40 | 68.57–114.29 | Full eighth pulse and first melodic high section |
| 40–48 | 114.29–137.14 | EP breakdown, one quiet metal accent per bar |
| 48–64 | 137.14–182.86 | Groove returns and thickens at bar 56 |
| 64–80 | 182.86–228.57 | Climax; original guitar chords enter at bar 68 |
| 80–85 | 228.57–242.86 | Groove withdraws, sparse kick and EP return |
| Release | 242.86–253 | Natural tails, final three-second output fade |

Three five-card banks use `ma_card_bank_tick_stereo` exclusively: pad,
source texture and metal. Character is .23/.16/.12 respectively. The pad
width develops from .30 toward .92; Raster/BCS controls follow interpolated
phrase energy at 100 Hz through the existing core smoothing. There are 30
MA voices total, plus the existing gated EP bank. All are hosted offline;
this exhibit makes no new live CPU-budget claim.

EP uses condition .12, drive .10, cabinet .32 and shallow stereo tremolo
at 2.8 Hz. The new percussion uses MA oscillators/noise/Raster, with no
external samples. Kick and backbeat remain centered. Linear reverb returns
belong to their source stems, and the final fade is shared by all three.

## Reproduce

```sh
make CC=/usr/bin/gcc build/exhibit_ma_hurt_industrial
./build/exhibit_ma_hurt_industrial --check
./build/exhibit_ma_hurt_industrial \
  -i notes-midi/local/nine-inch-nails-hurt.mid \
  -o build/ma_hurt_industrial_2026-09-06
python3 driver/prepare_hurt_listening.py build/ma_hurt_industrial_2026-09-06 \
  --reference build/ma_nin_hurt_noir_ma2_full_local_2026-09-06.wav --previews
```

The Python preparation tool requires NumPy; the C renderer uses the existing
host/core dependencies. The source MIDI is intentionally local and is not
required by `make test` or downloaded by any target.

The renderer writes `PREFIX_{tonal,rhythm,ep,mix}.wav`: aligned stereo
float32, 48 kHz. It checks paired releases, EP compass/ownership and maximum
held polyphony before rendering. The full schedule has 2559 MA attacks and
255 EP attacks. Completed output is published from temporary WAVs only after
finite/clipping/held-steal checks and successful closes.

`-s START -d DURATION` makes a short preview with ten seconds of DSP warm-up
and reconstructed note ownership. Such previews are deterministic but are
not bit-identical excerpts of a full render: free oscillator and character
clocks have different histories. The Python `--previews` outputs are exact
excerpts from the full mix, with its listening gain.

Preparation validates stem alignment and sum, writes `PREFIX_listening.wav`
using one constant gain to a -3 dBFS sample peak, and records levels, mono
energy, correlation and SHA-256 in `PREFIX_levels.json`. With `--reference`,
it also writes `PREFIX_comparison_{new,noir}.wav` at a common whole-file RMS;
the louder required peak limits that common level. This is RMS matching,
not a perceptual loudness or LUFS match. No master compression is applied.

## Validation

- GCC and Clang warning-clean builds; Clang static analyzer clean.
- Native suite: 123054 checks, zero failures, including the core symbol audit.
- ASan/UBSan/float-cast-overflow short render passes. LeakSanitizer required
  running outside the sandbox because the sandbox's ptrace prevents it.
- Invalid/nonfinite durations, ranges beyond 253 seconds, missing input and
  unavailable output directory return errors.
- Two independent two-second intro WAVs match byte-for-byte; so do two
  one-second groove previews at 22.85 s after ten seconds of warm-up.
  Groove mix FNV64: `84acb4be0f477095`.
- Initial groove preview: exact stem sum, -0.19 dB mono RMS loss; the final
  full-render report is the authority for delivered levels.

The intro EP gain was reduced after preview measurements showed that it
obscured the pluck and erased too much of the opening/climax level contrast.
Musical balance has been assessed through arrangement and signal measurements;
an independent listening judgment is still needed.
