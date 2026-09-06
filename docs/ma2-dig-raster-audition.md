# MA2-DIG — Raster oscillator and spectral morph

Date: 2026-08-27. Status: implementation and automated gates complete;
operator listening remains available but does not block return to MA2-4.

MA2-DIG is an ad hoc vertical slice through the existing five-card voice. It
adds an intentionally digital oscillator before the analog pressure/filter
path, two usable patches, Patchlab recall/editing and one long-form audition.
It does not alter the MA2-4 through MA2-6 order.

## Landed boundary

- Eight immutable, phase-aligned 256-sample wavetable families each have
  seven harmonic mip levels. Linear table interpolation, adjacent-family
  morph and adjacent-mip crossfade keep position and pitch movement
  continuous.
- `raster.position` scans the eight families. `raster.warp` applies the
  bounded sinusoidal phase deformation pinned in `ma-constants.md`.
- Every card owns only a Q48 phase accumulator and current mip. The core
  performs no allocation, I/O or runtime table generation.
- Literal `raster.mix=0` follows the former source-mix arithmetic byte for
  byte and leaves Raster phase/mip untouched. Mix, position and warp use the
  common 6 ms control smoother.
- Raster is the pure digital factory patch. Prizma blends Raster with the
  analog sine/triangle/saw and a restrained Mozaik contribution.
- At MA2-DIG closure, `.mapatch` version 2 required 48 fields. Strict
  version-1 files remained readable and acquired zero Raster controls.
  MA2-BCS later advanced the writer to v3 while retaining this v2 reader.

## Listening

```sh
make audition-ma2-dig
```

The command renders a 56-second stereo float WAV with progress from both
determinism passes. A pure Raster line moves between upper and lower
registers while a separate five-card Prizma bank holds twelve four-note
chords. Both position and warp move continuously; the final eight seconds
contain release tails.

Output: `build/ma2_dig_raster.wav`

| Property | Result |
| --- | ---: |
| Duration | `56.000 s` |
| Peak | `.522408` |
| RMS | `.097373` |
| FNV-64, both passes | `f70e00dd1cf6d64a` |
| SHA-256 | `a1687f4a59a8a84bb6bef7599fa2fb5da65c1a43de41e100d9659a131e7a6142` |
| Non-finite / clipped frames | `0 / 0` |

Patchlab's standard deterministic scripts additionally render Raster at
peak `.093070`, FNV `bdb656662268bc41`, and Prizma at peak `.089922`, FNV
`3a2bc4bdde9f73f1`.

## Automated boundary

Permanent tests cover zero-mix PCM/state bypass, distinct endpoint/midpoint
morph signatures, repeat determinism, low/high-note mip choice, hostile
control sanitization, factory patch identity and finite MIDI-compass sweeps
at 44.1, 48, 96 and 192 kHz. Hosted tests cover v2 round trips, exact shipped
mirrors and v1 migration.

This exhibit is a listening handoff, not a claim that Raster imitates analog
hardware. MA2-4 remains the next queued implementation task.
