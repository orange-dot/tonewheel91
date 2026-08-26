# MA1-6P VCO1 sine and compiled patches

Date: 2026-08-26. Status: implementation and listening evidence complete.

Historical note: MA1-6R subsequently generalized the enriched Mamut sine to
both VCOs and added the concrete patch-file/Patchlab layer. The values and
hashes below remain the closing record for the earlier VCO1-only slice.

MA1-6P adds sine only to VCO1 and two fixed initialization patches. The core
still has no patch files, live loader, registry, allocation or hosted I/O.
`ma_synth_init` selects Tepih; `ma_synth_init_patch` can select Tepih or Lead.

## Pinned patches

Tepih is the former factory dark pad with one intentional change: VCO1 sine
is `.20`. Every other direct value stays as listed in `ma-constants.md`, and
all five identity macros remain exact zero.

Lead uses these initialization values:

| Group | Value |
| --- | --- |
| VCO1 | saw `.75`, pulse `.30`, triangle `.05`, sine `.10`, PW `.43` |
| VCO2 | saw `.55`, pulse `.25`, triangle `.10`, level `.55`, interval `0`, fine `+7 cents`, PW `.57` |
| Source | sync `.22`, softness `.18`, cross-mod `.12`, noise `.01` |
| Mozaik | mix `.08`, golden slope, contrast control `.60`, phason `0`, drift `.03` |
| Filter | pressure `.32`, cutoff `1900 Hz`, resonance `.30`, drive `.35`, envelope `.62`, keytrack `.62` |
| Amp ADSR | `12 / 160 / .68 / 240 ms` |
| Filter ADSR | `6 / 260 / .18 / 220 ms` |
| Identity | Gravitacija `.12`, Bloom `.04`, Heat `.25`, Ruin `.22`, Swarm `.03` |
| Output | body `.15`, width `.45`, crossfeed `.08`, master `.18` |

## Run and results

```sh
make audition-ma1-6p
```

The hosted renderer writes four 14-second stereo float WAV files at 48 kHz
with fixed `.5` monitoring gain. Tepih uses two long notes. Lead uses a short
melodic phrase plus mod wheel, channel pressure and bipolar pitch bend. The
sine A/B uses the same sustained note after the 6 ms setter ramp has settled.

| Take | Peak | RMS | DC | Stereo FNV-64 |
| --- | ---: | ---: | ---: | --- |
| `ma1-6p_tepih.wav` | `.214303` | `.048740` | `+.0065185` | `b76c9c420a960925` |
| `ma1-6p_lead.wav` | `.256605` | `.051593` | `+.0043240` | `fb26440d488b857d` |
| `ma1-6p_tepih_sine_off.wav` | `.237115` | `.048398` | `+.0056423` | `c9df75040b17c751` |
| `ma1-6p_tepih_sine_on.wav` | `.213503` | `.044196` | `+.0052613` | `6c1409f71d818069` |

Every take is finite, dual-mono, below monitoring headroom and byte-identical
on repeat. The sine A/B differs in `472767` frames.

The FFT referee retains all eight saw/pulse/sync/cross-mod cases and adds pure
VCO1 sine at 44.1, 48, 96 and 192 kHz. Non-fundamental energy is respectively
`-96.36`, `-96.56`, `-101.21` and `-95.89 dBc`, passing the `-80 dBc` gate.
