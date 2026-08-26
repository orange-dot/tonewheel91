# MA1-7 output body, DC block and safety

MA1-7 completes the production output path of the one-card Mamut Analog
voice. It does not start the MA1-8 evidence rollup or close the public MA1
listening gate.

## Landed path

The mono VCA result is the centered card sum. Positive body drive sends
`4 * body_load * mid` through the shared `tw_drive` state and scales the
result by `.25`. Literal zero body drive branches around the scales and the
kernel, ignores body load, returns the input bits unchanged and leaves the
body state untouched.

The body result feeds independent 10 Hz left/right DC trackers. Both begin
from identical state, so MA1 remains exact dual mono. Nonzero tracker state
below `1e-9` snaps to zero and increments the tiny-flush diagnostic. Master
level follows the blockers and precedes the safety section.

The safety curve is identity through magnitude `.98`. Above the knee it uses
the pinned polynomial, reaches magnitude one with zero slope, and remains
bounded there. Non-finite channel samples become zero. Caller-owned output
diagnostics retain sanitization, knee-hit and tiny-flush counts, pre/post
safety peaks and maximum positive reduction.

`ma_output_state.pre_body` and `post_body` are explicit stage taps. They keep
the earlier MA1 source/filter/envelope PCM anchors testable after output
conditioning lands and make the exact body bypass directly auditable.

## Audition

```sh
make audition-ma1-7
```

The exhibit renders each take twice and requires byte-identical PCM and
diagnostics:

| Take | Peak | RMS | DC | Body-changed frames | FNV64 |
| --- | ---: | ---: | ---: | ---: | --- |
| body bypass | `.177787` | `.044127` | `+0.0000007` | `0` | `9d74d51119c9a551` |
| direct body | `.174124` | `.044030` | `-0.0000003` | `360000` | `124cc7e5fd4e7181` |
| identity load | `.228690` | `.065449` | `-0.0000000` | `360000` | `d32e24b085a54d15` |

The WAVs are `build/ma1-7_body_bypass.wav`,
`build/ma1-7_body_direct.wav` and `build/ma1-7_identity_load.wav`.

## Executable checks

Permanent tests cover the 10 Hz coefficient, polynomial anchors,
monotonicity, bounds and boundary slopes, exact zero-drive/state bypass,
resolved body load, master ordering, stereo non-finite sanitization,
diagnostic counters, tiny flushes, deterministic output and hostile sweeps at
44.1, 48, 96 and 192 kHz. Earlier MA1 PCM signatures now hash the explicit
pre-body tap, so output conditioning cannot conceal an upstream regression.

MA1-8 remains queued. It owns the canonical `make exhibit-ma1`, complete
cost/evidence rollup and the operator listening ballot.
