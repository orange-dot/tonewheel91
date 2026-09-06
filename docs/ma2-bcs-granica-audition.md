# MA2-BCS — Granica nonlinear-feedback coordinate

Date: 2026-08-27. Status: implementation and automated gates complete;
operator listening remains open. MA2-4 stays the next regular task.

MA2-BCS is an ad hoc vertical slice through the current five-card voice. It
ports the donor's bounded Hopf/Duffing dynamics into the Mamut Analog feedback
path, adds one usable factory patch and exposes the controls in Patchlab. It
does not import the donor scenario player and does not create a parallel sound
source.

## Landed boundary

- Every card owns four BCS state floats, bounded diagnostics and no dynamic
  storage. Four RK4 substeps run per public frame while amount is positive.
- `bcs.regime` is continuous over stable, edge, subharmonic and recovery
  landmarks. Intermediate values interpolate the complete coefficient set;
  there is no public mode enum or internal timeline.
- The bounded bipolar readout perturbs the existing cross-mod amount and
  ladder resonance. The previous ladder output weakly excites Duffing, closing
  the feedback path. BCS readout is never summed into the source mixer.
- Literal `bcs.amount=0` skips integration and destination changes, preserves
  BCS state and reproduces the pre-slice PCM byte for byte. Amount and regime
  otherwise use the common 6 ms smoother.
- Non-finite or over-ceiling state resets to the pinned seed and increments a
  counter. Full-note/full-rate hostile sweeps remain finite and bounded.
- Granica is a dark sine/triangle patch with VCO2 one octave down, restrained
  Raster, no pulse components and BCS `.72/.62`.
- `.mapatch` version 3 owns 50 fields. Version 1 migrates with Raster and BCS
  at zero; version 2 migrates with BCS at zero. Patchlab reads, edits and writes
  both new controls.

## Listening

```sh
make audition-ma2-bcs
```

The command renders a 56-second stereo float WAV and reports progress for two
complete deterministic passes. A five-card Granica bass line moves
continuously from regime `0` to `1` while a separate five-card Prizma bank
holds twelve dark four-note chords. The final eight seconds contain release
tails.

Output: `build/ma2_bcs_granica.wav`

| Property | Result |
| --- | ---: |
| Duration | `56.000 s` |
| Peak | `.276843` |
| RMS | `.058201` |
| BCS maximum accepted state | `1.165633` |
| BCS safety resets | `0` |
| FNV-64, both passes | `b0ce05ae487a1495` |
| WAV SHA-256 | `459a6be8d3c1e6af002c4573cd02a59bcd8754b398f83f06263bb2c4de0214ba` |
| Non-finite/clipped frames | `0 / 0` |

Automated checks prove distinct deterministic PCM at all four landmarks,
exact amount-zero bypass/state neutrality, hostile-input clamping, smoother
use, bounded recovery and no direct BCS audio with every source disabled.
The remaining verdict is musical: whether Granica's transition through the
subharmonic region earns a permanent place in the instrument.
