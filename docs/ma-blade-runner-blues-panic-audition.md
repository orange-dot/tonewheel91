# Mamut Analog — Blade Runner Blues Panic audition

Date: 2026-08-27
Status: MA2-2 hosted listening companion

```text
make audition-ma-blues-panic
```

The command builds `driver/exhibit_ma_blues_panic.c` and writes the
230-second, 48 kHz stereo-float take
`build/ma_blade_runner_blues_panic.wav`. The existing
`driver/exhibit_ma_blues.c` and its accepted WAV remain unchanged.

Three fixed five-card banks form the hosted overdub desk: a sustained Tepih
bank, a moving Dubina bank and a sparse dark Lead bank. Their allocation,
repeated-note release order, sustain phases, oldest-card stealing and panic
release paths are the landed MA2-2 core behavior. Role mixing and reverb are
hosted presentation code; they do not claim the future MA2 card-pan/shared-body
topology.

The score has three 72-second arcs. Each ends with a bank panic that starts
ordinary envelope release without zeroing oscillator, filter or output state,
so the transition leaves an audible tail rather than a hard digital mute.
Sustain groups deliberately carry more assignments than each five-card bank
can hold, making the frozen oldest-age stealing rule part of the performance.

## Current render

```text
216 s + 14 s tail, 88 notes, 12-card peak, 39 steals
3 musical panic transitions
peak 0.126336, RMS 0.016443, finite yes, headroom yes
FNV64 43a8af4dd33b877e (two runs identical)
```

The resulting WAV is 88,320,056 bytes. The registered files are:

```text
533cd818bfff2d77db7b5d3589ec6a0288688266dfb6a9df3662d4fa62f11ad3  driver/exhibit_ma_blues_panic.c
8b97c6c459df249f17c45bc87b0b5a2008a720d10ce2a46c2e0a024b9dc6523c  build/ma_blade_runner_blues_panic.wav
```
