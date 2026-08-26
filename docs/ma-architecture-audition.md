# Mamut Arhitektura I — synth symphony audition

Date: 2026-08-26  
Status: hosted MA1 composition and deterministic renderer; 180 s preview
rendered, full 960 s audition intentionally not run

This is an original F-sharp-minor composition for ten fixed monophonic MA1
lines. Bach supplies only the large-scale architectural vocabulary:
invention, passacaglia, chorale and fugue. No Bach theme, MIDI event stream or
runtime asset is used. `notes-midi/` remains an untracked research source and
is not part of any build rule.

## Form and source material

The hosted score uses integer time at 480 PPQ. Exact integer tempo segments
land every section on its stated second boundary:

| Time | Section | Construction |
|---|---|---|
| 0–72 s | Exordium | twelve free six-second arcs, growing from one to eight lines |
| 72–216 s | Invention | 48 bars of 4/4 at 80 BPM; canonical subject entries |
| 216–486 s | Passacaglia | twelve eight-bar variations in 3/4 at 64 BPM |
| 486–630 s | Chorale | 24 bars of 3/2 at 60 BPM; long reduced lines |
| 630–886 s | Fugue | 80 bars of 4/4 at 75 BPM; exposition, augmentation and stretto |
| 886–950 s | Coda | 16 bars of 4/4 at 60 BPM |
| 950–960 s | Tail | existing envelopes and reverb only |

The ground, subject, counterpoint and augmented cantus are defined directly in
`driver/ma_architecture_score.c`. Score validation pins ten lines, sorted and
paired note spans, the section boundaries, MIDI domains, monophonic ownership,
all twelve passacaglia density values, twelve ground cycles and four marked
fugue exposition entries. The passacaglia's first dark line enters in variation
7 and the second in variation 11. In the fugue, lines 8–9 contain augmented
subject spans only, never the eighth-note counterpoint.

The invention reaches a cadence at 179 seconds and leaves the final second to
the current releases and reverb. The full score resumes at exactly 180 seconds;
the preview is still the unmodified sample prefix of every longer render.

## Hosted renderer

`render_ma_architecture` accepts `-d seconds`, `-r rate`, `-o path` and `-h`.
Defaults are 960 seconds, 48 kHz and
`build/mamut_architecture_v1.wav`; duration is restricted to `0 < d <= 960`.

```text
make audition-ma-architecture-preview
make audition-ma-architecture
```

The preview target renders 180 seconds. The second target is the explicit full
sixteen-minute audition and is not part of `make test`.

Audio is produced in 4096-frame blocks without duration-sized sample storage.
The first pass hashes and measures without writing. A completely reset second
pass writes a temporary float32 WAV and recomputes the same evidence. Only an
exact hash/metric match and a complete checked close publish the file by rename.
No duration-dependent normalization, truncation NoteOff or fade is applied.

Ten synths are permanently owned by the ten score lines; there is no allocator
and therefore no stealing. Lines 0, 6 and 7 are tagged as future organ
candidates in hosted metadata only. The core headers and the MA/tonewheel
implementations are unchanged. Lines 8–9 use the shared `ma_dark_lead_patch()`
helper. The same helper now supplies the existing Blade Runner Blues exhibit;
its oscillator, filter, ADSR, macro and output values are unchanged.

## Preview evidence

Command:

```text
./build/render_ma_architecture -d 180 -r 48000 \
    -o build/mamut_architecture_180s.wav
```

Result:

```text
8,640,000 stereo frames; 69,120,056-byte RIFF/WAVE
512 note starts; 8-voice peak
sample peak 0.026675; RMS 0.005778; no clipping or non-finite samples
FNV64 eb19f57e847203b2 (both complete block passes)
SHA-256 6b1258aecc4a3d56e9689121119342bce881eb0d8cfac38a6a7a52d45bbd3ee8
```

`ffprobe` identifies IEEE float32 little-endian, 48 kHz, two channels and
exactly 180.000000 seconds. The test suite also renders two very short files
from frame zero and compares the shorter WAV data chunk byte-for-byte against
the corresponding prefix of the longer one. The full 960-second file has not
been rendered; it remains pending explicit operator approval.
