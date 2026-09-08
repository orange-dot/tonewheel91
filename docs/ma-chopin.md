# Chopin Op. 28 No. 4 — Mamut Analog

Status: Prophet/CS air/spread lead pass rendered and listening copy prepared.

## Tasks

- [x] Inspect the score and both local MIDI editions; select Mutopia.
- [x] Arrange the piano material into four Mamut parts and a free timing map.
- [x] Add dark patches, phrase dynamics, stereo character and damped room returns.
- [x] Add a score-only mode, CSV export and explicit render switch.
- [x] Prepare a constant-gain listening-copy command.
- [x] Build with GCC/Clang; validate score, CLI boundaries and memory safety without audio.
- [x] Render after approval, measure levels and audition the balance/tails.

## Interpretation

202.60 seconds at 48 kHz. A 12-second E-minor introduction precedes the
source; the last release is followed by a 12-second tail, fading over its
last four seconds. Zero-based source bars 15–17 tighten into the stretto;
the end broadens into the cadential pauses. Source tempo events and piano
sustain CCs are replaced by the exhibit's timing and explicit synth gates.

| Part | Treatment | Attacks | Held peak |
|---|---|---:|---:|
| Lead | All 92 upper-staff notes, including cadential chords; rounded triangle/sine with a driven saw edge | 92 | 4 |
| Chords | Preserve changed left-hand voicings; thin identical eighth-note repetitions to quarters, restore eighths in the stretto | 296 | 4 |
| Bass | Follow low chord tones in MIDI 28–43; reconsider pitch every two beats and at closing cadences | 41 | 1 |
| Echo | One introductory note and six brief, lower-register phrase recollections | 7 | 1 |

The melody keeps source pitches and order with new timing and dynamics.
The bass is a free interpretation of the descending harmony, not a literal
copy of the piano left hand. Lower source notes are assigned to bass rather
than duplicated in the chord layer. Cadential silences retain explicit gates.

All four parts use MA2 five-card banks, card character and shared stereo
output. Bass width is zero. The current lead pass is voiced as a large Prophet
meets Yamaha CS poly-synth: two audible saw/pulse oscillator layers, a 6.5
cent second-oscillator spread, moderate crossmodulation, resonant low-pass
filter tracking, and slow wide vibrato. Card character is .46, with restrained
BCS and body drive so the Mamut texture sits inside the analog mass. The new
pass widens the lead to .62, sends more of it into the damped room, and opens
the filter from 1380 toward 2380 Hz for a more ventilated top. Direct gain is
slightly reduced to keep that larger image from jumping forward. Other layers
remain darker. Pulse and noise stay disabled. The damped room follows the
Hurt exhibit's hosted reverb.
Each stem owns its room return and contributes directly to the mix sum.

## Source

[Mutopia, Peters/Scholtz edition](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=921),
public domain, local files excluded from Git:

- `notes-midi/local/chopin-prelude-op28-no4-mutopia.mid`
- `notes-midi/local/chopin-prelude-op28-no4-mutopia.pdf`

MIDI SHA-256:
`b19f8e0d783915e80e33cc4f622ef9623994ab89f114a53983c3f628265b798b`.
PDF SHA-256:
`8a7b5a44b9421351e00364c9b38ae587d1f9daa6fe913b26b0aaf01f6b91ae30`.

The driver expects the Mutopia layout: MIDI channels 2/3 (zero-based 1/2),
92 upper and 512 lower note pairs, ending within 103 quarter-note beats.
The alternative Pianovera file has different channel/count conventions and
is intentionally rejected. `-i` changes the path, not the arrangement contract.

## Build and inspect — no audio

```sh
make build/exhibit_ma_chopin
./build/exhibit_ma_chopin --check --score build/ma_chopin_op28_4_score.csv
```

No arguments also means check only. `--check` and `--render` are mutually
exclusive. The 872-event check validates note pairing, overlap, bounds,
held polyphony and actual bank event ownership without calling a DSP tick.
The current air/spread render is in `build/ma_chopin_op28_4_air_*`. Its raw
lead peak is 0.01633521 (-35.7341 dBFS), RMS -53.5439 dBFS; the mix peak is
0.02949389 (-30.6051 dBFS), RMS -46.3499 dBFS. The constant-gain listening
copy uses 27.6054 dB of gain, peaks at -3.0000 dBFS and measures -18.7455
dBFS RMS. Stereo correlation is 0.4040 and mono loss is 1.5372 dB.
Its last 100 ms is effectively silent after the tail fade. Stereo correlation
is 0.4818 and mono loss is 1.3237 dB. The four raw stems sum to the mix with
maximum absolute error 0.0.

Listening file:
`build/ma_chopin_op28_4_air_listening.wav`

Raw stem files are `build/ma_chopin_op28_4_air_{lead,chords,bass,echo,mix}.wav`.
The current listening-copy SHA-256 is
`cfffbdf49c21505da30159d9c60dcece1a24b834f86adccc1cfd86852376ae18`.

Validation on 2026-09-07: `make check-ma-chopin` passed; Clang produced the
same score CSV as GCC. GCC `-fanalyzer -Werror` and Clang ASan/UBSan with
float-cast-overflow checks passed on the score-only path. Independent CSV
checks confirmed balanced gates and the exact source lead pitch order.
CLI checks covered default check-only behavior, conflicting modes, missing
input and rejection of the alternate MIDI. The listening helper's syntax,
imports and help path were checked; its audio path is pending.

Prepared CSV: `build/ma_chopin_op28_4_score.csv`, SHA-256
`ed0b1f5ecdb9ac95bd4d06f12ecd7b5fe6b75ec545c4cb9d72c00effc3749eeb`.
No Chopin WAV was generated during these checks.

## Render — only after approval

```sh
./build/exhibit_ma_chopin --render -o build/ma_chopin_op28_4
python3 driver/prepare_chopin_listening.py build/ma_chopin_op28_4
```

The explicit Make target is `make audition-ma-chopin`. Five stereo float32
WAVs are written: `_lead`, `_chords`, `_bass`, `_echo`, `_mix`. About 389 MB
for the raw files, another 78 MB for the listening copy. The renderer rejects
nonfinite/clipped output or loss of a held card and prints peak/RMS/FNV64.

The Python step uses NumPy and the existing Hurt WAV/measurement helpers.
It verifies alignment and the stem sum, then scales the entire mix by one
constant gain to a -3 dBFS sample peak. It writes `_listening.wav` and
`_levels.json`, including RMS, stereo correlation, mono loss and tail level.
Run this only after a successful full render; preserve the raw stems.
