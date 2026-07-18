# Reference renders — the cross-milestone comparison log

`renders/` holds whole-song WAVs rendered offline through the current
engine, kept as **permanent listening baselines**: after a later
milestone lands (M6 rotary, M7 wear), re-running the same command on the
same input A/Bs the two engine states directly. The directory is
untracked (operator-supplied MIDI transcriptions stay out of git, like
`docs/externalDocs/`); this tracked log pins the provenance, the exact
commands, and the FNV-64 signatures, so every render is reproducible
bit-for-bit from its entry.

Tool: `build/render_midi` (driver layer) — SMF format 0/1 in, f32 WAV
out (mono through M5, interleaved stereo since M6 rotary); the same
channel-message map as the live driver, ticks converted through the
file's tempo map, two-run FNV-checked per render. The FNV hashes the
sample buffer the tool emits — mono through M5, the interleaved stereo
buffer since M6 — so M5 and M6 signatures are not comparable by value;
the A/B is by ear and by peak.

## 2026-07-18 — hstar.mid at M5

- Input: `renders/hstar.mid`, md5 `e96a6c0104942313e2acebb41862d992`
  (55074 bytes; SMF format 1, 10 tracks, 96 tpq, one tempo = 190 bpm).
  The two organ parts of the arrangement are channels 0 and 4; channel 4
  carries a ridden expression pedal (191 x CC11), which the render
  honors. All other channels (guitars, bass, drums) are excluded.
- Engine: M5 landed (uncommitted work atop `aee5086`), `make test`
  7710 checks green. Chain: generator -> contacts/taper -> percussion
  (off here) -> scanner C3 -> swell (CC11) -> drive. No rotary yet —
  that is the point of keeping these for M6 comparison.
- Settings: registration 888800000, vibrato C3, drive as below,
  octave-fold on (2 fold events: one sub-compass note's on/off pair),
  48 kHz, 2 s tail. 3856 note events, 0 out-of-compass.

Renders:

    ./build/render_midi -c 0,4 -R 888800000 -v 6 -D 0 -g 0.05 -f \
        -o renders/hstar-m5-20260718-dry.wav renders/hstar.mid
    # peak 0.689, FNV64 9e43ffdf1c6bf80b

    ./build/render_midi -c 0,4 -R 888800000 -v 6 -D 0.75 -g 0.25 -f \
        -o renders/hstar-m5-20260718-drive075.wav renders/hstar.mid
    # peak 0.655, FNV64 ea92ea773d3b351a

The two files differ only in the drive knob (and master gain: the raw
two-part organ sum peaks ~13.8, so the dry reference needs 0.05 where
the compressed driven render takes 0.25). The dry file is the pre-M5
organ bit-for-bit (drive 0 is an exact bypass) and doubles as the M4
baseline; the driven file is the M5 sound on a real song. FNV values
are for the f32 sample buffer before WAV framing, as printed by the
tool.

## 2026-07-18 — hstar.mid at M6 (rotary)

- Input: `renders/hstar.mid`, md5 `e96a6c0104942313e2acebb41862d992`
  — the same file, channels, and tempo map as the M5 entry above.
- Engine: M6 landed (uncommitted work atop `213401f`), `make test`
  7764 checks green. Chain now the full design.md line: generator ->
  contacts/taper -> percussion (off) -> scanner C3 -> swell (CC11) ->
  drive -> **rotary**. Output is interleaved stereo.
- Settings shared with both renders below: registration 888800000,
  vibrato C3, drive 0.75, octave-fold on, 48 kHz, 2 s tail. 3856 note
  events, 191 CCs, 0 out-of-compass. The two renders differ **only** in
  `-m` (rotary mode); master gain is held at 0.15 across both so the
  pair is a clean level-matched A/B.

Renders:

    ./build/render_midi -c 0,4 -R 888800000 -v 6 -D 0.75 -m 0 -g 0.15 -f \
        -o renders/hstar-m6-20260718-drive075-bypass.wav renders/hstar.mid
    # peak 0.393, FNV64 433f4ab26a574a85  (rotary bypass)

    ./build/render_midi -c 0,4 -R 888800000 -v 6 -D 0.75 -m 2 -g 0.15 -f \
        -o renders/hstar-m6-20260718-drive075-tremolo.wav renders/hstar.mid
    # peak 0.637, FNV64 27ddf2784d19c07e  (rotary tremolo)

The bypass render is the pre-rotary chain duplicated onto both channels
bit-for-bit (the scanner-OFF discipline: rotary `bypass` is exactly the
mono M5 sound). Its peak 0.393 is the M5 driven render's 0.655 scaled by
0.15/0.25 to three places, which measures that nothing before the
rotary changed. The tremolo render is the same passage with both rotors
spinning: the amp ceiling and the two-mic sum lift the peak to 0.637
(~+4.2 dB over the pre-rotary sum at the same gain), which is why the
gain drops from M5's 0.25 to 0.15 to keep the peak in the M5 render's
headroom. Master gain is documented per render, as with the M5 pair;
the two M6 files A/B the rotary alone, and against the M5 entry they
A/B the whole rotary stage against its absence.

## 2026-07-18 — an ELP showcase at M6 (rotary, no drive)

Not baselines — these are **listening demos** of the M6 rotary stage on
two whole-arrangement transcriptions, kept for the ear rather than for a
cross-milestone value A/B. These isolate the rotary slice alone: **drive
is 0** (pure tonewheel sum -> rotary), so nothing before the rotors
colours or compresses the sound; master gain is chosen per song to hold
the peak near 0.72 (all six clear full scale — 0 samples over 1.0).

Inputs are operator-supplied SMF transcriptions, untracked like the rest
of `renders/`. Only the organ channels are rendered (the tonewheel91
instrument is the organ; everything else in the arrangement is dropped):

- `renders/karn_evil_9.mid`, md5 `7c08654a78168b89d0b58ef9847de303`
  — organ on channels 2,3 (a second organ layer on ch10,11 is a ~99%
  duplicate of ch2, so it is left out to spare headroom).
- `renders/tarkus.mid`, md5 `d0a432d373d651e61f833f6684b69c54`
  — organ on channels 3,4. The transcription rides CC7 (volume) and
  CC64 (sustain) heavily; tonewheel91 honours neither, so the organ
  plays without that dynamic contour — a flatter, louder wall than the
  source, which is why the gain sits low.

Each song gets a slow (chorale) take, a fast (tremolo) take, and an
**emerson** take that works the half-moon switch mid-song — the rotor
inertia does the rest (horn spins up in ~1 s, the drum lags several
seconds each way). The switch automation is a synthesised extra MIDI
track of CC87 flips (`>= 64` tremolo, else chorale) placed by the organ
note-density profile — fast under the busy solos/riffs, slow under the
sustained passages; `renders/rotoauto.py` is the tool, and the injected
files are kept so the takes reproduce.

Karn Evil 9 — full-organ scream, registration 888888888, drive 0,
gain 0.030:

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -g 0.030 -f \
        -o renders/ke9-chorale.wav renders/karn_evil_9.mid
    # peak 0.805, FNV64 d5178780f16e6bd3  (chorale)

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 2 -g 0.030 -f \
        -o renders/ke9-tremolo.wav renders/karn_evil_9.mid
    # peak 0.730, FNV64 95ace8113be9aeb1  (tremolo)

    # renders/ke9-emerson-automation.mid, md5 b799d70c3fb8698d24cb4017afd13007
    # = karn_evil_9.mid + CC87 track on ch2: tremolo@0, chorale@138,
    #   tremolo@222 (the dense solo), chorale@295, tremolo@358 (into
    #   the ~385 s climax)
    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -g 0.030 -f \
        -o renders/ke9-emerson.wav renders/ke9-emerson-automation.mid
    # peak 0.732, FNV64 ab979ea790e954d8  (chorale <-> tremolo, live)

Tarkus — fat and menacing, registration 888000000, drive 0, gain 0.0349:

    ./build/render_midi -c 3,4 -R 888000000 -v 0 -D 0 -m 1 -g 0.0349 -f \
        -o renders/tarkus-chorale.wav renders/tarkus.mid
    # peak 0.702, FNV64 ee084c15a4a87c1e  (chorale)

    ./build/render_midi -c 3,4 -R 888000000 -v 0 -D 0 -m 2 -g 0.0349 -f \
        -o renders/tarkus-tremolo.wav renders/tarkus.mid
    # peak 0.719, FNV64 169decf85cd08583  (tremolo)

    # renders/tarkus-emerson-automation.mid, md5 dbf6a406cf3b5d36f7cbc903fc530f52
    # = tarkus.mid + CC87 track on ch3: tremolo@0 (Eruption), chorale@130,
    #   tremolo@405 (the driving build), chorale@520 (the long quiet
    #   middle), tremolo@828 (the Aquatarkus finale)
    ./build/render_midi -c 3,4 -R 888000000 -v 0 -D 0 -m 1 -g 0.0349 -f \
        -o renders/tarkus-emerson.wav renders/tarkus-emerson-automation.mid
    # peak 0.681, FNV64 bd366932dedfeba0  (chorale <-> tremolo, live)

The tremolo peak was probed once per song (the loudest mode bounds the
set) to fix the gain. KE9's chorale take peaks a touch higher than its
tremolo (0.805 vs 0.730): tremolo's deeper horn AM digs bigger troughs
and pulls the running peak down, so chorale, not tremolo, is the headroom
case here — relevant if these are ever re-gained. FNV values are
the tool's two-run signatures over the interleaved stereo buffer; they
also recompute bit-for-bit from each WAV's `data` chunk.
