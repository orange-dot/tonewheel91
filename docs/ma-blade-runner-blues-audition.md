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
`build/ma_blade_runner_blues_expanded.wav`. The 234-second performance plus
14-second tail uses a fixed hosted overdub desk: dark and haze Tepih layers,
root and moving Dubina layers, and sparse Lead phrases. There are no bell
events, leaving long passages for the two Tepih and two Dubina colours to carry
the arrangement. Their source bank files are not modified by the wrapper.

Lead is a listening-only overlay in a low register (MIDI 54–64), close in
colour to Tepih and Dubina: triangle and sine dominate, with an 880 Hz filter,
520 ms attack and 9 s release. Small sync (`.055`), crossmod (`.035`) and
filter drive (`.085`) add motion without bringing back a bright or dirty edge.
The wrapper adds fixed stereo pan positions and a bounded four-comb/two-allpass
reverb. It is not the future MA2 allocator and does not define product
polyphony or stealing semantics.

## MA1 long listening arm

```text
make exhibit-ma1
```

The MA1 exhibit does not introduce another short render or another driver. It
registers and hashes the reviewed source/WAV pair:

```text
driver/exhibit_ma_blues.c
build/ma_blade_runner_blues_expanded.wav
```

It intentionally fails when the WAV is absent. Regeneration remains the
explicit `make audition-ma-blues` operation, so running the evidence check does
not silently replace the artifact that received the listening verdict.

## Evidence

The original MA1-6R GCC render at commit `6eb8563` is 180 seconds (168 seconds
of material plus a 12-second tail), 48 kHz stereo float. Its WAV SHA-256 was
`c2c9ce7dc966009798197ff8784b019785541a2b931eaa5a3b0542779811cbcf`;
`ffmpeg ebur128` reported −16.1 LUFS integrated and −1.7 dBFS true peak.

Commit `7fe5582` subsequently put the MA1-7 body, DC blocker, master level and
safety transfer into every regular `ma_synth_tick`. That core change necessarily
moved the hosted exhibit before the dark-lead helper was extracted. At current
HEAD, a complete regeneration reports:

```text
54 notes; 11-voice peak; 0 steals
sample peak 0.114823; RMS 0.022672; finite, with headroom
FNV64 6dce29d5d0521e87 (both runs)
SHA-256 e6743424dfdebf8c9a9dd6fb1d942a8a6fb1fb578640bc859254c4002a6d745b
```

The MA architecture work moves the dark patch byte-for-byte from this source
into `ma_dark_lead_patch()`. A focused regression compares the full patch and
20,000 synthesized frames against a copy of the former local construction; it
therefore distinguishes that source-only extraction from the earlier MA1-7
core-output change.

Build/test gates: GCC and Clang `make test`, sanitizer coverage, a warning-clean
exhibit build, and `git diff --check`.

The expanded long-form take is 248 seconds, 48 kHz stereo float, and
95,232,056 bytes. At commit `db6271fa258fb0f1b0236b6a190de86d69e94720`,
the registered pair is:

```text
9ed126191bafa58444a4fb2cf8680b413826e53961438253d4eb219bbb45ff05  driver/exhibit_ma_blues.c
1ec97582ec543c3a00ca0f215b54bccf3f6b13ea44396b72a313cabc41de4ac7  build/ma_blade_runner_blues_expanded.wav
```

Operator verdict, 2026-08-27: accepted; the renders passed the listening gate.
This long take answers the musical-coherence part of the MA1 experiment. It
does not replace the separate numeric, hostile-input, bypass, cost or safety
evidence required for final MA1-8 closure.
