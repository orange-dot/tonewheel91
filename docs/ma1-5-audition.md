# MA1-5 one-voice audition

Date: 2026-08-26. Status: operator listening verdict recorded.

This interstitial exhibit makes the landed MA1-5 signal path audible before
identity, output conditioning and the full MA1 evidence gate. It changes no
core behavior and does not close any part of MA1-6 through MA1-8.

Provenance: `tonewheel91` commit `96cb1b9` plus this hosted exhibit. The
pre-slice core anchor is the MA1-5 zero-Mozaik signature `48685bb104788f11`.
There is no external input fixture: the four note events below are compiled
into the exhibit.

## Run

```sh
make audition-ma1-5
```

The hosted exhibit renders the same fixed 14-second, 48 kHz note script twice
per take. It requires byte-identical stereo samples, finite output, centered
dual mono, nonzero signal and no clipping after the fixed `0.5` monitoring
gain. The gain is not part of the instrument DSP.

The script plays MIDI 48 at velocity 78 and MIDI 55 at velocity 96. Each note
has room for the factory 600 ms attack and 3 s release. Large WAV files remain
under `build/` and are not tracked.

## Takes

- `build/ma1-5_factory.wav`: compiled MA1-5 defaults.
- `build/ma1-5_analog_only.wav`: the same state with only
  `mozaik.mix = 0`; this is the clean Mozaik A/B.
- `build/ma1-5_mozaik_focus.wav`: Mozaik mix `.60`, slope `.5601133`,
  contrast control `.75`, drift `.35`; cutoff `1400 Hz`, resonance `.35`,
  filter drive `.40` and mixer pressure `.35`.

The command prints raw peak, RMS, DC mean and the FNV-64 signature of each
emitted interleaved buffer.

## Checked result

GCC and Clang produced the same figures and byte-identical PCM signatures:

| Take | Raw peak | RMS | DC mean | Emitted stereo FNV-64 |
| --- | ---: | ---: | ---: | --- |
| factory | `.245351` | `.053287` | `+.0070261` | `ff6f374aa5f6d149` |
| analog-only | `.235781` | `.054212` | `+.0013814` | `af9bcbea779b3359` |
| mozaik-focus | `.326470` | `.085680` | `+.0230711` | `018d9ab2064a3fe1` |

All takes are finite, dual-mono, nonzero, repeat exactly and remain below
full scale after monitoring gain. The reported DC is intentionally not fixed
in this hosted slice; the core DC blocker belongs to MA1-7.

Validation on 2026-08-26: GCC and Clang aggregate suites each passed core
`111261`, hosted `77` and MIDI map `22`, with zero failures and clean
freestanding symbol audits. The audition path also passed
ASan/UBSan/float-cast-overflow. `git diff --check` passed.

## Listening ballot

Further listening can refine:

1. Does the factory take already read as one coherent analog voice?
2. Does the factory/analog-only pair place Mozaik inside the source rather
   than beside it as a separate layer?
3. Are the attack, held body and release musically usable at both notes?
4. What should MA1-6 or MA1-7 correct without retuning this provisional
   exhibit?

Verdict: **accepted as an audible MA1-5 handoff**. The operator described the
result as an "odličan jeziv zvuk" (excellent eerie sound). This is a positive
identity finding, not permission to retune the provisional voice or a claim
that the finer factory/Mozaik A/B questions have been answered.

Remaining unknowns are the finer factory/Mozaik A/B judgment, MA1-6
macro/performance motion, and MA1-7 DC/output conditioning. Polyphony, stereo
body and live MIDI remain owned by later public milestones.

## Post-MA1-6 compatibility note

The table above remains the historical MA1-5 result at its stated provenance.
After MA1-6 added the required continuous-control smoothers, GCC and Clang
still produce the exact factory and analog-only signatures. The explicitly
reconfigured Mozaik-focus take now produces `10c5d8f963d0a2ed`: its controls
move through the new 6 ms state while the pre-note DSP timeline advances.
Peak, RMS and DC figures are unchanged at the displayed precision, and the
take remains finite, dual-mono, below the monitoring headroom limit and
byte-identical on repeat.
