# Hurt — dark, wet Mamut revision

Historical listening take. The current hosted driver is now
`exhibit_ma_hurt_organ`; see [ma-hurt-organ.md](ma-hurt-organ.md).
The commands below record this earlier render and its retired target.

The 2026-09-07 listening correction returns toward the original Noir sound:
Mamut only, with no EP, metal percussion, bell patch or bright noise backbeat.
It replaces the industrial hosted renderer; earlier WAVs remain listening
references. No instrument core API changes are required.

## Tasks

- [x] Remove EP generation, routing, state, linkage and output stem.
- [x] Remove metal events and bank; replace the backbeat with a soft tonal pulse.
- [x] Darken every patch and give both stems a damped reverb return.
- [x] Retune the original bell/drum track to current half-bar harmony.
- [x] Verify the new schedule, builds, repeatability and preview spectrum.
- [x] Render and verify the complete 253-second take and aligned stems.
- [x] Prepare listening/A-B WAVs, excerpts and measured evidence.

## Sound and arrangement

All eight parts use Mamut. The six-voice pluck retains its original slow
envelope. The high melody moves down an octave into a soft three-voice pad
with a 550 ms attack and 3.2 s release. The original late tenor/guitar part
remains. Harmonic pads and the source texture use two five-card MA2 stereo
banks with character .23/.16; 26 MA voices are clocked in total.

Every patch disables Raster, Mozaik, noise, sync, crossmod and pulse waves.
Saw levels are reduced, cutoff is capped at 650 Hz with key tracking disabled,
and resonance, filter envelope and saturation remain gentle. Phrase automation
changes width, shallow LFO motion and a small bass BCS amount; it cannot reopen
the removed digital/metallic sources.

Source channel 9 pitches are reassigned to the current bass root plus two
octaves. Releases retain the pitch selected at note-on, including across
harmonic boundaries. The separate metal sequencer is removed entirely.
The backbeat is now a sine/triangle pulse at the harmonic root plus one octave,
with an 80 ms attack. Kick attack is 18 ms and its gain is halved. The bass
retains the syncopated groove with a softer attack and longer release.

Both tonal and rhythm stems have their own linear damped reverb. Relative to
the industrial take, feedback increases from .785 to .90; damping increases,
and input plus two return low-pass stages suppress bright ringing. Dry gain
is .72; wet return gain is .48–.56 for tonal and .38 for rhythm. The last three
seconds fade together. Stem summing therefore reproduces the delivered mix.

## Reproduce

```sh
make CC=/usr/bin/gcc build/exhibit_ma_hurt_dark
./build/exhibit_ma_hurt_dark --check
./build/exhibit_ma_hurt_dark -o build/ma_hurt_dark_2026-09-07
python3 driver/prepare_hurt_listening.py build/ma_hurt_dark_2026-09-07 \
  --reference build/ma_nin_hurt_noir_ma2_full_local_2026-09-06.wav --previews
```

The C target links only Mamut, its shared drive, and the existing hosted
SMF/WAV helpers. The preparation tool requires NumPy. Output is 48 kHz stereo
float32: `_tonal.wav`, `_rhythm.wav`, `_mix.wav`; `_listening.wav` applies one
constant gain to a -3 dBFS sample peak. There is no EP stem in this revision.
The comparison files share whole-file RMS with the first Noir render.

Short `-s START -d DURATION` previews warm ten seconds of DSP history and
are deterministic, but free oscillator/character history differs from a
full take. Delivered `_preview_*` WAVs are extracted from the full mix.

The local MIDI source is unchanged: SHA-256
`4c98ab235aaac6917562db89f1e54d942ea93f782419705f06389200779ad554`.
Musical preference remains a listening judgment; checks below report signal
properties, not an audition by the assistant.

## Validation

- Schedule: 2083 MA attacks, zero unmatched releases, held polyphony within
  each part's capacity. EP and metal have no parts or events.
- No EP symbols in the executable; its link line contains no EP objects.
- GCC/Clang builds and final Clang static analysis pass. Hashing reads the
  serialized float pair rather than any struct padding.
- Native suite: 123054 checks, zero failures.
- ASan/UBSan/float-cast-overflow short render passes. Leak detection is disabled
  for this sandbox run because its ptrace environment does not support LSan.
- Two independent two-second intro WAVs match byte-for-byte.
- Preliminary climax preview: zero clipping/nonfinite samples or held-note
  steals. Relative spectral energy above 2.5 kHz was about -79 dB, compared
  with -29 dB in the industrial preview. These different short windows are
  a timbre check, not an exact aligned A/B or a listening verdict; the final
  take additionally zeroes all identity macros.

## Delivered take

Completed 2026-09-07: 253 s, 12144000 stereo frames at 48 kHz. All outputs
share prefix `build/ma_hurt_dark_2026-09-07`. The full render reports 2083 MA
attacks, zero held-note steals, and no clipping/nonfinite samples. Released
voices are reused 1587 times; these are envelope/release retriggers, not
unmatched note ownership.

| Output | Peak | RMS | FNV64 |
| --- | ---: | ---: | --- |
| Tonal stem | .06533146 | .00993297 | `a704dd9a731c793f` |
| Rhythm stem | .04019452 | .00577821 | `26ac4a3e26fb1bdc` |
| Raw mix | .06990932 | .01146251 | `cb702d1c8acb5ab0` |

The two stems reproduce the mix with maximum sample error **0.0**.
Listening gain is +20.1093 dB: sample peak -3.0000 dBFS, RMS -18.7051 dBFS.
The wider wet image has L/R correlation .4346 and mono RMS loss 1.4442 dB.
Final 100 ms peak is below 8e-15 after the shared fade.

Listening WAV SHA-256:
`a117464490bee397d127e028f407a1f32eae34bbc5dbe050f957151f0bc776bc`.

`_levels.json` contains raw WAV hashes, per-section levels, and the common
RMS used for `_comparison_new.wav` / `_comparison_noir.wav`. Five listening
excerpts cover intro, groove, breakdown, climax and outro. The previous
industrial and first Noir audio files remain intact.
