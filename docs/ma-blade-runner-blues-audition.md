# Mamut Analog — Blade Runner Blues audition

Date: 2026-08-26  
Status: hosted listening companion to MA1-6R; no MA1-7 claim

The requested “Blade Runner Blues” MIDI was not available as a free, complete
file. The public MIDI candidates labelled only “Blade Runner” are Main/End
Titles or other arrangements; the complete 10:19 transcription found during
the search is a paid delivery. This audition therefore uses a hand-authored
F-sharp-minor study, guided by the track's documented i-m7 centre and its
slow, improvised synth-blues character. It is explicitly an interpretation,
not a claim of note-for-note transcription.

The source characteristics used for the study are the public
[Blade Runner Blues ambient reference](https://huikku.github.io/bladerunner-blues/)
and the track listing/duration in the
[Blade Runner soundtrack notes](https://en.wikipedia.org/wiki/Blade_Runner_%28soundtrack%29).

## Render

```text
make audition-ma-blues
```

The command builds and runs `driver/exhibit_ma_blues.c`, writing
`build/ma_blade_runner_blues.wav`. The MIDI-like event desk is intentionally
small and concrete: nine long chord changes, nine Dubina roots, and nine widely
spaced Lead phrases. There are no bell events, leaving long passages for Tepih
and Dubina alone. Their source bank files are not modified by the wrapper.

Lead is a listening-only overlay in a low register (MIDI 54–64), close in
colour to Tepih and Dubina: triangle and sine dominate, with an 880 Hz filter,
520 ms attack and 9 s release. Small sync (`.055`), crossmod (`.035`) and
filter drive (`.085`) add motion without bringing back a bright or dirty edge.
The wrapper adds fixed stereo pan positions and a bounded four-comb/two-allpass
reverb. It is not the future MA2 allocator and does not define product
polyphony or stealing semantics.

## Evidence

The regenerated GCC render is 180 seconds (168 seconds of material plus a
12-second tail), 48 kHz stereo float:

```text
54 notes (including nine Lead entries), with the same bounded hosted desk and
deterministic two-pass render check.
```

The WAV is 48 kHz, 32-bit float, stereo, 180 seconds, SHA-256
`c2c9ce7dc966009798197ff8784b019785541a2b931eaa5a3b0542779811cbcf`.
`ffmpeg ebur128` reports −16.1 LUFS integrated and −1.7 dBFS true peak.

Build/test gates: GCC `make test` (111301 + 102 + 22 checks), Clang equivalent
with `CCACHE_DISABLE=1`, Clang warning-clean exhibit build, and `git diff --check`.
