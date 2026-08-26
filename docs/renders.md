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

## 2026-07-19 — Karn Evil 9 at M7 (wear)

- Input: `renders/karn_evil_9.mid`, md5 `7c08654a78168b89d0b58ef9847de303`
  — the same file and channels (2,3) as the M6 showcase entry above.
- Engine: M7 landed (`521b4e0`), `make test` 8911 checks green. Chain
  unchanged from M6; the new stage is the generator's `wear` bank
  (constants.md 11-13), exposed to the offline renderer by a new
  `render_midi -w <0..1>` flag. The flag defaults to `0` (the
  idealized reference) so every pre-M7 entry in this log still
  reproduces bit-for-bit from its recorded command; M7 wear is opt-in
  per render. Note the live instrument ships `wear = 0.2` by default
  (`TW_WEAR_DEFAULT`); the deterministic twin defaults to the reference
  and pins wear explicitly instead.
- Settings shared with the M6 chorale take: registration 888888888,
  vibrato off, drive 0, rotary chorale (`-m 1`), octave-fold on, master
  gain 0.030, 48 kHz. The **only** change from that take is `-w`, so the
  pair is a clean level-matched A/B that isolates the wear stage alone.
  10182 note events, 2 CCs, 0 folded, 0 out-of-compass.

Identity anchor — `-w 0` reproduces the M6 chorale render bit-for-bit
(the pre-M7 instrument IS wear 0):

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0 -g 0.030 -f \
        -o renders/ke9-chorale.wav renders/karn_evil_9.mid
    # peak 0.805, FNV64 d5178780f16e6bd3  (== the M6 ke9-chorale entry)

The M7 take — shipped wear (0.2), same gain:

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.030 -f \
        -o renders/ke9-m7-20260719-wear020-chorale.wav renders/karn_evil_9.mid
    # peak 0.813, FNV64 f4dbf7118c6478a9  (wear 0.2)

The emerson (live half-moon) take at the same wear, from the same
CC87-automation file as the M6 emerson entry
(`ke9-emerson-automation.mid`, md5 `b799d70c3fb8698d24cb4017afd13007`):

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.030 -f \
        -o renders/ke9-m7-20260719-wear020-emerson.wav renders/ke9-emerson-automation.mid
    # peak 0.738, FNV64 6e56f252d97c240c  (wear 0.2, chorale <-> tremolo live)

Against the wear-0 anchors the peaks lift only +0.008 on the chorale
(0.805 -> 0.813, ~+0.09 dB) and +0.006 on the emerson (the M6 emerson
was 0.732): the note sum is bit-identical, and wear rides on top as the
structured deviations plus the ~-44 dB floor — audible as colour and a
live noise bed, not as a level change, which is why the same 0.030 gain
holds the A/B. Each take differs from its M6 sibling only in `-w`; the
FNV values are the tool's two-run signatures over the interleaved
stereo buffer.

## 2026-07-19 — Deep Purple "Burn" at M7 (Jon Lord voicing)

Listening demo, not a cross-milestone baseline: the tonewheel91 engine
put through a Jon Lord voicing on the "Burn" (1974) organ part — the
driven, Marshall-through-a-Hammond scream that is the whole point of the
drive + rotary chain.

- Input: `renders/Deep_Purple_Burn.mid`, md5
  `005233c79edc8c2b810964baaf9b6e01` (SMF format 1, 16 tracks, 240 tpq).
  An Italian karaoke transcription; the organ is three labelled tracks —
  `Organo acc` (ch0, the comp/riff), `Organo solo` (ch2, the lead), and
  `Organo dbl` (ch10, a doubling layer). Rendered channels **0,2** (comp
  + solo); the ch10 double is dropped to spare headroom, as the KE9
  showcase dropped its duplicate layer. Everything else (drums, bass,
  guitars, strings, vocals, synth) is not the organ and is excluded.
- Voicing (from Jon Lord's documented "Burn"-era setup): registration
  **888800000** — his signature full-flutes-plus setting; **drive 0.80**
  — the trademark heavy overdrive (Hammond C3 driven hard, his answer to
  Blackmore's volume); **vibrato/chorus C3** (`-v 6`); percussion off
  (the distortion swamps the perc transient). Wear at the M7 shipped
  0.2. 48 kHz, master gain 0.15, octave-fold on. 1692 note events, 16
  CCs (CC7/CC64 in the source, which tonewheel91 does not honour), 0
  folded, 0 out-of-compass. The two takes differ **only** in `-m`
  (Leslie mode), so the pair is a clean level-matched rotary A/B.

Renders:

    ./build/render_midi -c 0,2 -R 888800000 -v 6 -D 0.8 -m 1 -w 0.2 -g 0.15 -f \
        -o renders/burn-m7-20260719-drive08-chorale.wav renders/Deep_Purple_Burn.mid
    # peak 0.626, FNV64 d4a3f3ebfa88c73a  (chorale)

    ./build/render_midi -c 0,2 -R 888800000 -v 6 -D 0.8 -m 2 -w 0.2 -g 0.15 -f \
        -o renders/burn-m7-20260719-drive08-tremolo.wav renders/Deep_Purple_Burn.mid
    # peak 0.625, FNV64 b4f77822ffac2819  (tremolo)

The two modes peak within 0.001 (0.626 vs 0.625) — unlike the clean KE9
pair, where chorale and tremolo differed by ~0.07. With drive 0.80 the
preamp stage ceilings the signal *before* the rotary, so the horn AM
rides an already-limited wall rather than shaping the peak; the mode
difference is all timbre and movement, not level. FNV values are the
tool's two-run signatures over the interleaved stereo buffer.

## 2026-07-19 — Bach Toccata & Fugue in D minor (BWV 565) at M7 (plenum)

Listening demo, not a baseline: the opposite pole from the "Burn"
voicing — a clean classical *organo pleno*, the tonewheel organ standing
in for a pipe-organ plenum with no drive, no Leslie, no chorus.

- Input: `renders/ToccataFugue.mid`, md5
  `a1c4949752873c8f12a40d49a23189a2` (SMF format 1, 4 tracks, 384 tpq).
  Three note tracks, all the organ: `RH:1` (ch0, 1905 notes), `LH:`
  (ch1, 1417), and the pedal (ch2, 329). All three rendered
  (channels **0,1,2**) — this is a solo-organ piece, nothing to exclude.
- Voicing (from the classical *organo pleno* convention — principals
  16/8/4/2 plus mixtures, full organ start to finish): registration
  **888888888**, the tonewheel "full organ" that stands in for the
  plenum's bright principal chorus. **No vibrato/chorus** (`-v 0`, the
  straight tone a church organ has), **no drive** (`-D 0`, clean), and
  **rotary bypass** (`-m 0` — a pipe organ has no Leslie). Wear at the
  M7 shipped 0.2. 48 kHz, master gain 0.020, octave-fold on. 7302 note
  events, 0 CCs, 0 folded, 0 out-of-compass — the transcription sits
  entirely inside the 61-key compass, so the pedal never needed folding.

Render:

    ./build/render_midi -c 0,1,2 -R 888888888 -v 0 -D 0 -m 0 -w 0.2 -g 0.020 -f \
        -o renders/toccata-m7-20260719-plenum.wav renders/ToccataFugue.mid
    # peak 0.779, FNV64 c1f1bf63a80e9101  (plenum, clean)

No probe: the clean three-part full-organ sum runs hot (no drive stage
to compress it), so the gain was set low (0.020) up front to hold the
peak in headroom, and landed at 0.779. Output is interleaved stereo but
mono in content — with the rotary bypassed both channels carry the same
mono plenum bit-for-bit. FNV is the tool's two-run signature over the
interleaved buffer.

## 2026-07-19 — Bach Toccata & Fugue in D minor (BWV 565) at M7 (Leslie)

Listening demo, not a baseline: the electromechanical counterpart to the
plenum above — the same transcription put *through the instrument* rather
than standing in for a pipe organ. Rotary speaker engaged and played,
a touch of preamp drive, and the iconic opening broadened into a rubato.

- Input: `renders/ToccataFugue-leslie.mid`, md5
  `f9d986af9742801d228c3c8abaa1fbe9`. Derived from `ToccataFugue.mid`
  (`a1c4949752873c8f12a40d49a23189a2`) in two offline passes, both under
  `renders/`:
  1. **`tempo.py`** rewrites the flat 60 bpm map into a broad, rubato
     opening. The original made the famous declamation run twice as fast
     as it should — the mordent-A, the descending run, and its two echoes
     flew by. The new map opens near **30 bpm** with a real *rallentando*
     into each run's landing (down to ~20 bpm on the last notes), then an
     *accelerando* that gathers back to the original **60 bpm** by the
     perpetual-motion figuration (tick 4512 ≈ 21.5 s). Everything from the
     figuration onward keeps the original tempo untouched — only the
     ~21 s intro is reshaped. Result: `ToccataFugue-rit.mid`.
  2. **`rotoauto.py inject`** adds a CC87 automation track that *plays*
     the Leslie: chorale (slow) for the declamatory opening and the fugue
     subject expositions, tremolo (fast) for the toccata figuration and
     the build-ups, with a final chorale so the rotor winds down on the
     last chord. Eleven flips at section boundaries
     (0/21/47/58/119/214/239/347/392/487/572 s), plus CC89 width 110 for
     a wide stereo image. Result: `ToccataFugue-leslie.mid`.
- Voicing: registration **888888888** (unchanged from the plenum), **no
  scanner vibrato** (`-v 0` — the rotary owns the movement now), **drive
  0.10** (`-D 0.10`, a very light preamp warmth, not distortion), base
  rotary **chorale** (`-m 1`, the automation takes it from there). Wear at
  the M7 shipped 0.2. 48 kHz, octave-fold on. 7302 notes, 12 CCs
  (the automation), 0 folded, 0 out-of-compass.

Render:

    ./build/render_midi -c 0,1,2 -R 888888888 -v 0 -D 0.10 -m 1 -w 0.2 -g 0.043 -f \
        -o renders/toccata-m7-20260719-leslie-drive01.wav renders/ToccataFugue-leslie.mid
    # peak 0.723, FNV64 721a67915abf15fe  (Leslie, drive 0.10)

Gain went up to 0.043 (from the plenum's 0.020): with the rotary engaged
the mono chain is spread across the two channels, so per-channel peak
drops by more than half — 0.043 lands the same ~0.72 peak the plenum
held at 0.020. FNV is the tool's two-run signature over the interleaved
stereo buffer.

## 2026-07-19 — Bach Toccata & Fugue in D minor (BWV 565) at M7 (Leslie, no drive)

Listening demo, not a baseline: the same idea as the Leslie render above,
but from a **better transcription** and **clean** — no drive at all, the
Leslie the only colour. A four-part arrangement whose tempo map already
carries the performance, so this render leaves the timing alone and only
plays the rotary.

- Input: `renders/Toccata-and-Fugue-Dm-leslie.mid`, md5
  `0341038e41e6bac5f932e7e8334a183f`. Derived from
  `Toccata-and-Fugue-Dm.mid` (`3b852a14d155c8ca87c17baeb0aa0c84`, SMF
  format 1, 5 tracks, 256 tpq) by one offline pass, `rotoauto.py inject`.
  Unlike the first Leslie render, **no tempo rewrite** — this source
  already opens with a proper rubato (a ~20 bpm declamation with 17-bpm
  breaths, accelerating to ~84 bpm for the figuration around tick 2896),
  so the iconic opening is broad as recorded. Four organ channels carry
  the parts: ch1 (Manual, 2010 notes), ch3 (Manuals, 1315), ch2 (329),
  ch0 (140); all four rendered (`-c 0,1,2,3`).
- Leslie play: a CC87 automation track flips speed at nine section
  boundaries — chorale (slow) for the opening declamation, the toccata's
  pre-fugue wind-down, the fugue's calmer episodes, and the closing
  fermatas; tremolo (fast) for the toccata figuration and cascade, the
  fugue's build-ups, and the two big climaxes — with a final chorale so
  the rotor spins down on the last chord
  (0/35/148/210/235/305/418/460/492 s), plus CC89 width 110 for a wide
  image.
- Voicing: registration **888888888**, **no vibrato** (`-v 0` — the
  rotary owns the movement), **no drive** (`-D 0`, fully clean), base
  rotary **chorale** (`-m 1`). Wear 0.2, 48 kHz, octave-fold on. 7588
  notes, 10 CCs, 234 folded (this arrangement's pedal/manual extremes
  reach past the 61-key compass), 0 out-of-compass.

Render:

    ./build/render_midi -c 0,1,2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.021 -f \
        -o renders/toccata-m7-20260719-leslie-nodrive.wav renders/Toccata-and-Fugue-Dm-leslie.mid
    # peak 0.766, FNV64 e983aea2ca6ecaf2  (Leslie, clean)

Probe: this denser four-part sum with no drive to compress it runs hot —
it hit peak 1.459 at gain 0.040, so the gain was scaled linearly down to
0.021 to land a safe 0.766. FNV is the tool's two-run signature over the
interleaved stereo buffer.

## 2026-07-23 — Karn Evil 9 emerson, re-rendered on the post-M7 depth pass

Not a new baseline — an **identity re-render**: the same emerson take run
through today's engine to show the post-M7 depth pass left this whole
song bit-for-bit unchanged. It reproduces the 2026-07-19 M7 wear-0.2
emerson entry exactly.

- Input: `renders/ke9-emerson-automation.mid`, md5
  `b799d70c3fb8698d24cb4017afd13007` — the same CC87-automation file and
  channels (2,3) as the M6/M7 emerson entries above.
- Engine: post-M7 depth pass (`b0604f3`), `make test` 9242 checks green.
  Two stages landed since the M7 wear entry — per-note **key depth** on
  poly key pressure (0xA0), and the percussion trigger moved onto the 1'
  contact with its 34 ms re-arm RC (`docs/constants.md` secs 7.1/8;
  `docs/depth-evidence.md`). Both are inert here by construction: this
  file carries **no 0xA0** (the render reports `0 depths`) so depth never
  engages, and it plays **percussion off** (`render_midi` defaults
  `-p 0`) where the trigger change moves nothing. The note sum, drive,
  scanner and rotary stages are all untouched.
- Settings identical to the 2026-07-19 M7 wear-0.2 emerson take, so the
  only variable is the engine version: registration 888888888, vibrato
  off, drive 0, rotary chorale (`-m 1`), wear 0.2, octave-fold, gain
  0.030, 48 kHz. 10182 notes, 8 CCs (the CC87 flips), 0 depths, 0 folded,
  0 out-of-compass.

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.030 -f \
        -o renders/ke9-depth-20260723-wear020-emerson.wav renders/ke9-emerson-automation.mid
    # peak 0.738, FNV64 6e56f252d97c240c
    #   (== the 2026-07-19 M7 wear-0.2 emerson entry, bit-for-bit)

The signature matches `6e56f252d97c240c` from the M7 entry and recomputes
bit-for-bit from the new WAV's `data` chunk — a whole-song confirmation
of the `docs/depth-evidence.md` claim that nothing percussion-off moves,
alongside the pinned exhibit and test signatures. The earlier emerson
renders are left in place; this is a new dated file beside them.

## 2026-07-23 — Bach Toccata & Fugue (BWV 565), Leslie, re-rendered on the post-M7 depth pass

The second identity re-render on today's engine — a four-part
arrangement rather than the two-manual ELP scream, so the whole-song
check covers a denser sum and the octave-fold path (234 folded notes).
It reproduces the 2026-07-19 M7 Leslie-clean entry bit-for-bit.

- Input: `renders/Toccata-and-Fugue-Dm-leslie.mid`, md5
  `0341038e41e6bac5f932e7e8334a183f` — the same file and channels
  (0,1,2,3) as the M7 Leslie no-drive entry above.
- Engine: post-M7 depth pass (`b0604f3`), `make test` 9242 green. Inert
  here for the same two reasons as the ke9 re-render above: the file
  carries **no 0xA0** (`0 depths`) and plays **percussion off**
  (`render_midi` `-p 0`), so neither key depth nor the contact-driven
  trigger engages (`docs/constants.md` secs 7.1/8; `docs/depth-evidence.md`).
- Settings identical to the 2026-07-19 take, engine version the only
  variable: registration 888888888, vibrato off, drive 0, rotary chorale
  (`-m 1`), wear 0.2, octave-fold, gain 0.021, 48 kHz. 7588 notes, 10
  CCs, 234 folded (the pedal/manual extremes reach past the 61-key
  compass), 0 depths, 0 out-of-compass.

    ./build/render_midi -c 0,1,2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.021 -f \
        -o renders/toccata-depth-20260723-leslie-nodrive.wav renders/Toccata-and-Fugue-Dm-leslie.mid
    # peak 0.766, FNV64 e983aea2ca6ecaf2
    #   (== the 2026-07-19 M7 Leslie no-drive entry, bit-for-bit)

The signature matches `e983aea2ca6ecaf2` from that entry and recomputes
bit-for-bit from the new WAV's `data` chunk. The earlier Leslie render is
left in place; this is a new dated file beside it.

## 2026-07-23 — full-engine automation: the depth pass, actually used

The two re-renders above proved the post-M7 depth pass is *inert* on the
old automation (no 0xA0, percussion off). These two are the opposite —
the first renders that **exercise** the new engine: per-note key depth on
poly key pressure (0xA0) and the contact-driven percussion trigger,
alongside the whole console (percussion tablets, vibrato, drive, rotary
speed/balance/width/drive, swell) driven from one automation track. So
their signatures are new by design; they are listening demos, not
cross-milestone A/Bs.

- Tool: `renders/fullauto.py` (successor to `rotoauto.py`) — realizes a
  timed automation *script* and, for a `depth` directive, scans the
  parsed note windows and lays a shaped 0..127 press curve (pull / dip /
  swell / smear) on the actual sustained notes it finds, on each note's
  own channel. The scripts and injected MIDIs are kept beside the tool so
  the takes reproduce; both renders are two-run FNV-identical.
- Engine: post-M7 depth pass (`b0604f3`), `make test` 9242 green.
- Both start from the **clean base** transcriptions (no prior CC87), so
  the automation owns the whole console.

Bach Toccata & Fugue — `Toccata-and-Fugue-Dm.mid`
(md5 `3b852a14d155c8ca87c17baeb0aa0c84`), channels 0,1,2,3. Automation
`renders/toccata-full.script.json` -> `renders/toccata-full-automation.mid`
(md5 `2bd59e7eaf093985e6528b995ca7cd6d`): emerson rotary flips, percussion
on the detached toccata figuration (2nd->3rd->slow), C3 chorus in the
fugue, drive and rotary-amp swells at the two climaxes, a swell/width
contour, and **five key-depth gestures** — a "pull" darkening the iconic
opening, a "smear" contact-chop on the fugue pedal, a "dip" on the fugue
climax, a "swell" building the closing pedal from thin to full, and a
final "dip". 6464 depth events on 40 held notes.

    ./build/render_midi -c 0,1,2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.028 -f \
        -o renders/toccata-fullengine-20260723.wav renders/toccata-full-automation.mid
    # peak 0.767, FNV64 c92c194216228d37
    #   applied: 7588 notes, 6464 depths, 1861 ccs, 1323 folded

Karn Evil 9 — `karn_evil_9.mid`
(md5 `7c08654a78168b89d0b58ef9847de303`), channels 2,3. Automation
`renders/ke9-full.script.json` -> `renders/ke9-full-emerson-automation.mid`
(md5 `7b251efe105e81df085da68cace553bd`): the logged emerson flips, drive
up from the downbeat, C3/V3 chorus, percussion through the busy sections,
balance/width/rotary-drive moves, and **five key-depth gestures** — a
"pull", a "swell", two "dip"s, and the showcase: a "smear" over the 9 s
held chord *with percussion live*, so each contact re-close on the 1'
sensing line re-fires the envelope (the two new features interacting).
2083 depth events on the held chords.

    ./build/render_midi -c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0.2 -g 0.042 -f \
        -o renders/ke9-fullengine-20260723-emerson.wav renders/ke9-full-emerson-automation.mid
    # peak 0.771, FNV64 07ba0d054583e048
    #   applied: 10182 notes, 2083 depths, 1443 ccs, 0 folded

Both gain-matched to ~0.77 peak against the earlier takes. astats vs the
inert renders: RMS drops a touch (depth thinning removes upper drawbars —
Toccata -22.0 -> -23.6 dB, KE9 -22.6 -> -24.8 dB) while the RMS-peak
rises (drive and percussion transients punch harder). Spectrograms show
the depth gestures directly: the closing pedal's harmonics build from the
bottom up over ~6 s (the swell), and the KE9 held chord's upper harmonics
are rhythmically gated ~6x (the smear crossing make points). The earlier
renders are untouched; these are new dated files beside them.

## 2026-07-23 — Deep Purple "Lazy": the Jon Lord voicing, full engine

The natural home for a full-engine take: "Lazy" (Machine Head, 1972) is
one long Hammond feature. Voiced from Jon Lord's documented setup rather
than guessed — an **overdriven Hammond through a Marshall into a Leslie**
[Wikipedia; thehighwaystar.com]; drawbars **888272773** for the singing
intro/solo, pulled to **888000000** with percussion off for the riff (the
"rhythmical technique"), and the Leslie held **slow for the "lovely
initial swirl"** before switching fast [organforum.com; Jon Lord
interviews]. The automation drives all of that live.

- Input: `renders/LAZY.MID`, md5 `3597775a6f58385543cd7afb82aa9fb7` — a
  labelled GEMAR (1997) sequence; the organ is Jon Lord's part split
  across channels 0 (intro), 10 (intro double), 6 (accompaniment), 7
  (solo). Rendered `-c 0,6,7,10`; drums/bass/guitar/vocals/harmonica
  dropped. Structure: unaccompanied Hammond intro 0-78 s, riff/verse
  78-198 s, organ solo 198-244 s, verses to 428 s, final held chord.
- Tool: `renders/fullauto.py`, now with a **`drawbars`** directive
  (CC70-78) so the registration moves live. Automation
  `renders/lazy-full.script.json` -> `renders/lazy-full-automation.mid`
  (md5 `bc7b57ad3c18b2cb65527ceb9470f34f`): the drawbar pulls above, a
  chorale->tremolo Leslie following the intro swirl and the solo,
  overdrive up to the ceiling in the solo, percussion on for the intro
  and solo / off for the riff, C3 chorus, swell-pedal contour, and depth
  gestures blooming the long intro notes (the 12 s / 8.9 s / 7.7 s
  opening holds) and the 6.8 s final chord. 4464 depth events, 1964 ccs.

    ./build/render_midi -c 0,6,7,10 -R 888272773 -v 0 -D 0 -m 1 -w 0.2 -g 0.080 -f \
        -o renders/lazy-fullengine-20260723.wav renders/lazy-full-automation.mid
    # peak 0.757, FNV64 d2c11c5c593820ea
    #   applied: 5680 notes, 4464 depths, 1964 ccs, 171 folded (two runs identical)

Gain 0.080 (higher than the 888888888 takes — Lazy's thinner registration
runs a lower raw peak). Spectrograms: the intro blooms as the depth swell
fills the harmonics in under a chorale-then-tremolo Leslie; at 198 s the
drawbars snap to 888800000 and the solo lights the upper spectrum up with
the drive at the ceiling. A brand-new song for the log — nothing prior to
leave alone.

## 2026-07-26 — ep73: the first whole-song render of the electric-piano line

Not an organ entry, and deliberately not a baseline to A/B against
anything: the first proof that `render_midi -I ep73` carries a whole song
through the new instrument's key, damper and pedal path, and that the
deterministic twin still holds on it.

The source is an organ transcription played on a piano, which is exactly
the wrong music for the instrument and exactly the right test for the
plumbing — four voices, wide compass, long held pedal notes that a
struck, decaying voice cannot sustain the way an organ does. It sounds
like a four-part fugue on an electric piano, and it is here to be
listened to for *defects* — dampers cutting wrong, dropped notes,
clicks — not for tone. EP3 owns tone.

- Input: `renders/Toccata-and-Fugue-Dm-leslie.mid`, md5
  `0341038e41e6bac5f932e7e8334a183f` — the same file as the organ entries
  above, channels 0,1,2,3.
- Engine: EP2 (`0a8be7c` plus the EP2 working tree), `make test` 9322
  checks green. Compass 28..100, so only 24 notes fold, against 234 on
  the organ's 61 keys. The file carries no CC64, so **every note is
  stopped by its damper on release** — the pedal path exists and is
  simply never asked for here.
- Settings: octave-fold, gain 0.080, 48 kHz, 6 s tail. No drive, no
  tremolo, no cabinet — none of them exist yet.

    ./build/render_midi -I ep73 -c 0,1,2,3 -f -g 0.080 -t 6 \
        -o renders/ep73-ep2-20260726-toccata.wav renders/Toccata-and-Fugue-Dm-leslie.mid
    # peak 0.682, FNV64 28a8aead10a6895d
    #   applied: 7588 notes, 0 key pressures, 6 ccs, 24 folded (two runs identical)

510 seconds of audio rendered in 9.8 s wall clock, about 52x realtime —
the active-gated bank layout decision D4 closed on, doing what it was
chosen for. The organ path is untouched by the instrument switch: the
`ke9` emerson take above re-renders to `6e56f252d97c240c` on this build,
bit-for-bit.

## 2026-07-26 — ep73 against a reference loop library

Twenty short MIDI loops for the tine electric piano, rendered through
`render_midi -I ep73`. Unlike every entry above, this set arrives with its
own audio: the library ships a rendered WAV beside each MIDI, from the
instrument the loops were written for.

**Correction, 2026-07-27.** This entry originally claimed the two could be
heard against each other "on identical notes". They cannot. Testing the
MIDI note-on times directly against onsets detected in the audio — a test
that does not involve this engine at all — only **25 % of MIDI onsets have
an audio onset within 50 ms**, against the 80 %-plus a shared performance
would give. Lengths, tempi and key names match exactly, so the pairing is
of counterparts rather than of takes: the audio was played separately from
the MIDI. Every per-pair statement below should be read as a comparison of
two different performances of the same idea, which is a far weaker thing
than it sounded.

- Input: an operator-supplied loop library, unpacked into gitignored
  `renders/rhodes-loops-tl/`. Twenty format-0 single-track MIDI files, all
  on channel 0, at 90 BPM except one at 85.
- Compass 38..94, so the whole set sits **inside** the 73-key ep73 compass
  with nothing folded and nothing out of range. No `-f` needed.
- Seven of the twenty carry **CC64**, 8 to 20 events each, so the EP2
  damper and sustain-pedal path is exercised by real material rather than
  only by tests.
- Engine: EP2 working tree, `make test` 9322 checks green. No drive, no
  tremolo, no cabinet, `condition` does not exist yet — this is the bare
  voice bank through its dampers.

    ./build/render_midi -I ep73 -r 44100 -t 6 -g 0.135 \
        -o renders/rhodes-loops-tl/ep73/ep73-<name>.wav <name>.mid

Rendered at **44.1 kHz**, not the house default 48 kHz, to match the
reference loops so an A/B needs no resampling in the middle. One
consequence is pinned in `ep-constants.md` sec 3.1 and is not a defect:
at 44.1 kHz the third tine mode silences from MIDI 86 rather than 87.

Gain 0.135 is **one value for the whole set**, calibrated so the loudest
loop peaks at 0.691 and the quietest at 0.206. Relative loudness between
loops is therefore the model's velocity law, not a per-file normalisation
— which is the point, since the velocity-to-loudness law is exactly what
EP3 has to judge.

Per-loop peaks and FNV-64 signatures, all two-run identical, are in
`renders/rhodes-loops-tl/AB-manifest.txt` together with the reference each
render should be compared against. Nineteen of the twenty pair to a
reference by key and length; `90_C_RhodesDust` has none, and the `Dm` and
`Fm` pairs are marked doubtful (different tempo, and a 1.4 s length
disagreement, respectively).

Two observations about the source material, neither of them ours.
`90_BMaj7_RhodesDust_01.mid` and `90_Gm_RhodesDust.mid` differ as files but
render to the same signature `53ca0687889a37e1`, so they carry the same
event stream under two different key names — one of the two labels is
wrong. And the `Dm` MIDI is named for a different patch than the `Dm`
reference and runs at a different tempo, so it is probably not the same
material at all.

**These renders are not a baseline and nothing is pinned to them.** They
exist to be listened to beside the references, and the constants they will
move — the velocity-to-timbre tables, the pickup alpha, the mode weights —
are all EP3's, still carrying their [FOLK] and [decision] tags in
`ep-constants.md`.

### What the measurement says about the references themselves

A third-octave long-term-average comparison of the twenty pairs, before
any listening:

    band   200  400  800 1008 1270 1600 2016 2540 3200 4032 5080 8063 12800
    diff   0.7 -3.0 -1.8  2.3  4.3  9.2 20.7 23.5 25.7 23.7 18.5 22.0  20.2

ep73 minus reference, dB. Below 800 Hz the two agree within 3 dB, which is
the fundamental region and the part of the model that is derived rather
than fitted. Above a knee near 1.6 kHz the difference jumps to a **flat
plateau of 20-25 dB across three octaves**.

Flat is the informative part. A missing loudspeaker or cabinet rolloff
would make the difference *grow* with frequency; a difference that stays
level means both spectra fall at the same rate up there and ours simply
sits 22 dB higher — a high shelf, not a slope.

Two readings fit that, and they belong to different milestones. Either the
[FOLK] mode weights of `ep-constants.md` section 5.2 are far too high,
which is EP3's business, or the reference chain carries a treatment this
model does not have, which is EP6's.

The reference set cannot settle that, because it is not a clean recording
of the instrument. The patches are named for their production — dust, tape
— and they measure like it: the references themselves fall about 16 dB per
octave above 2 kHz and hold a steady 0.80 channel correlation, which is a
lowpassed, stereo-widened, produced sound rather than a close-miked
instrument. Calibrating the voice's spectral identity against them would
bake somebody's production EQ into constants that are supposed to describe
a tine and a pickup.

So the set is **good evidence for mechanism and bad evidence for tone**.
It is worth keeping and listening to for dampers cutting wrong, dropped
notes, pedal behaviour and clicks. EP3 still needs what it always needed:
dry, close-miked single notes across the compass and across the dynamic
range.

## 2026-07-27 — ep73 compass sweep, and the bass correction it forced

A MIDI written here, not a transcription: one file that walks the whole
compass and exercises every behaviour the model has, in sections separated
by silence. It exists so that when something sounds wrong it is obvious
which constant owns it — and it earned that on its first pass.

Generator: `renders/ep73-sweep.py` (untracked, beside the render). Format
0, one track, channel 0, 120 BPM, 210 notes, compass exactly 28..100 so
nothing folds, 16 CC events.

    section  seconds          what it isolates
    1          0.5 -  16.6    chromatic sweep, all 73 keys at v90 — evenness
    2         18.1 -  44.3    velocity ladder 1..127 at each E — bell to bark
    3         45.7 -  54.5    one phrase dry, then the same pedalled — dampers
    4         55.5 -  62.4    repeated notes accelerating, pedal down — D5
    5         63.2 -  74.4    chords low / mid / high / compass-wide
    6         75.8 - 105.8    lowest then highest note held on the pedal
    7        107.3 - 131.1    a chord under a CC91 sweep — the tremolo

    ./build/render_midi -I ep73 -g 0.1356 -t 6 \
        -o renders/ep73-sweep-EP4-20260727.wav renders/ep73-sweep.mid
    # peak 0.720, FNV64 27231494b9743862
    #   applied: 420 notes, 0 key pressures, 16 ccs, 0 folded (two runs identical)

Engine: EP3 plus EP4, `make test` 9356 checks green, eleven exhibits
passing, and the organ's pinned whole-song baseline still reproducing
`6e56f252d97c240c`.

### What the first pass of this file found

The bass did not sound like a bass note. It sounded like a short hollow
pop at a pitch that had nothing to do with the key played. Measuring one
E1 at velocity 127 said why: the fundamental sat at 41.2 Hz, which almost
nothing reproduces, while the clang partial at 258 Hz sat **2.7 dB under
it** and 9 dB above the pickup's own second harmonic. Whichever partial
tops the harmonic series is the pitch the ear assigns, and here that was an
inharmonic partial two octaves and a third above the note, gone in 2.6 s
while the real note rang inaudibly for eight.

Two corrections, both from documents already in the source list.

The length law was violating one of them. `f1 = K/L^2` asks for a 181 mm
tine at E1, and the longest replacement blank the factory ships is 111 mm
[EP-SM 5-1] — the model wanted a tine 1.6 times longer than the instrument
holds. Capping the length is not a fudge but the physical situation the
founding patent describes: below a certain pitch the tine stops growing and
the counterweight carries it, which is why the springs on the low-pitched
generators are deliberately heavier [EP-P61]. With the cap the bass strike
sits proportionally further out along its tine, at `xi = 0.514` rather than
0.316, and couples far less to the high modes.

The bass contact time then went from 4 to 6 ms on the patent's own
statement of intent: those hammers are "relatively large and thickly
felted, **in order to damp out harmonics**". That is a design goal, and the
model should carry it rather than discover it.

Result at E1: the clang drops from 2.7 dB under the fundamental to 11.8 dB
under, landing **below** the second harmonic instead of above it, so the
harmonic series carries the pitch again. Mode 3 falls 35 dB because the
corrected `xi` lands almost exactly on that mode's node. Across the whole
compass the clang spread narrows from 15.2 dB to 4.8 dB.

The effect on this file is blunt: the raw peak fell from 14.85 to 5.31, so
the same headroom now carries 9 dB more of everything else. A single bass
note used to be louder than the compass-wide six-note chord; it is now
+8.9 dB over the same velocity at E4, against +17.5 dB before.

Section 6 remains the one section that checks a sourced number rather than
inviting an opinion: about 16.6 s to -60 dB at E1 and 4.0 s at E7, against
the 17 s and 3-5 s the founding patent states.

The same file is worth running through any other tine-piano instrument for
a direct A/B on identical notes.

## 2026-07-27 — the loop set re-rendered after the bass correction

The same twenty loops as the 2026-07-26 entry, through the current engine:
the mode-shape strike weights with the length cap, the contact transient,
the per-register bark threshold, and EP4's tremolo present but off. Same
44.1 kHz, same one-gain-for-the-whole-set discipline; the gain moved from
0.135 to 0.154 because the bass correction freed headroom.

    ./build/render_midi -I ep73 -r 44100 -t 6 -g 0.154 \
        -o renders/rhodes-loops-tl/ep73-20260727/ep73-<name>.wav <name>.mid

Twenty renders, all two-run identical; per-file peaks and signatures in
`renders/rhodes-loops-tl/AB-manifest.txt`. The EP2-era set stays in
`ep73/` for a direct three-way A/B against the references.

Mean third-octave difference from the reference set:

    band            200    400    800   1600   2540   4032   6400  10159   rms>1.6k
    EP2             0.7   -3.0   -1.8    9.2   23.5   23.7   20.8   21.1     21.0
    EP3+EP4         1.3   -2.6   -3.8    1.9   15.9   17.4   18.9   22.6     17.9

The band that moved most is 1.6 kHz, from +9.2 dB to +1.9 dB — that is
where the clang partial used to sit across this material, and it is the
same correction the compass sweep forced. Below 800 Hz nothing changed,
which is right: that region was already within a couple of dB and none of
the EP3 work touched the fundamental.

What is left is 16 to 23 dB above the references from 2.5 kHz up, and the
earlier caveat still stands: these references are a produced patch, about
16 dB per octave down above 2 kHz with a steady 0.80 channel correlation,
so an unknown share of that gap is theirs rather than ours. The honest
reading is that the model still has no cabinet stage (EP6) and that its
remaining high-frequency excess cannot be attributed further without a
clean reference recording.

## 2026-07-27 — the loop set through the finished chain

The same twenty loops again, now through everything the line has: the
struck bank with EP3's mode-shape weights and length cap, the contact
transient, the per-register bark threshold, EP5's drive at 0.15 and EP6's
cabinet fully engaged. Tremolo off.

    ./build/render_midi -I ep73 -D 0.15 -C 1.0 -r 44100 -t 6 -g 0.2415 \
        -o renders/rhodes-loops-tl/ep73-cab-20260727/ep73dc-<name>.wav <name>.mid

Mean third-octave difference from the reference set, one stage at a time:

    stage                200    400    800   1600   2540   4032   6400  10159   rms>1.6k  crest
    EP2 (Jul 26)         0.7   -3.0   -1.8    9.2   23.5   23.7   20.8   21.1     21.0    8.7
    EP3+EP4              1.3   -2.6   -3.8    1.9   15.9   17.4   18.9   22.6     17.9    7.7
    + drive 0.15         1.2   -2.7   -4.0    1.7   15.6   17.2   18.6   22.5     17.6    6.4
    + cabinet 1.0        1.3   -2.5   -4.9   -2.7    6.7    2.6   -2.9   -5.4      5.0    6.3
    reference            0.0    0.0    0.0    0.0    0.0    0.0    0.0    0.0      0.0    5.0

The cabinet is the largest single move in the whole line: 17.6 dB of
high-frequency error down to 5.0. **Its corners were pinned from ordinary
loudspeaker behaviour before this was measured and were not adjusted
afterwards**, which is the only reason the number means anything.

It now overshoots slightly at the very top — we sit 5.4 dB *under* the
references at 10 kHz — and +6.7 dB remains at 2.5 kHz, which is where the
mode weights and the pickup still put energy. Neither is worth chasing
against this reference set, for the reason recorded on 2026-07-26: these
are produced patches, not clean instrument recordings, and the MIDI beside
them is not even the same performance.

Crest sits at 6.3 against the references' 5.0. The drive closes most of
that gap and the cabinet, being a filter, closes none of it — as expected.

Earlier sets are kept for comparison: `ep73/` (EP2), `ep73-20260727/`
(EP3+EP4), `ep73-drive-20260727/` (with drive), `ep73-cab-20260727/` (this
one), and `compare-20260727/` holds four rms-matched A/B pairs.

## 2026-07-28 — the pickup field, and the bass envelope it gives back

The by-ear verdict after the finished chain was that the low notes still
carried "a slight vibraphone flavour". Measuring before guessing found the
mechanism, and it was not a voicing question.

**What was wrong.** Section 4 pins E1 at `t60 = 17 s` — 3.53 dB/s. Rendered
through the old pickup the fundamental fell **0.77 dB/s** between 0.3 s and
1 s, a fifth of its own pinned rate, and the timbre did not move either:
`h3` stayed within 1.4 dB of itself for the first 1.5 s. A note that holds
both level and colour for a second is a struck bar under a resonator. The
compression was graded across the compass and worst at the bottom, which is
exactly the register the ear picked out:

    fraction of the pinned decay rate reached, 0.3-1 s, velocity 100
    note     E1     A1     E2     B2     E3     E4
    old    22 %   33 %   34 %   53 %   62 %   84 %
    new    79 %   85 %   93 %   95 %   91 %   96 %

**Why.** `tw_sat` is the organ's power-stage saturator — tonewheel.h keeps
it for "the rotary's 40 W ceiling, where a power stage wants a hard bound".
It clamps flat at `|u| = 3` with exactly zero slope. At the pinned drive
E1 reached `u = 3.18` at velocity 100, and at velocity 127 the bottom
twenty notes were clipped outright. A power amplifier has a rail; a
magnetic field does not, and the borrow had carried one across.

**The replacement** is the field taken from the source the section already
cited: `Psi(u) = (1 + u^2)^(-3/2)`, the inverse-cube law of
[EP-DAFx17 eq 6] over the lateral sweep of eq 8, in units of the pickup
gap. Detail and the three constants in `ep-constants.md` sec 6.

Renders. The compass sweep, gain chosen to land on the previous set's peak
so the two are directly comparable by ear:

    ./build/render_midi -I ep73 -g 0.65 -t 6 \
        -o renders/ep73-sweep-field-20260728.wav renders/ep73-sweep.mid
    # peak 0.719, FNV64 f7e8a836e9701a04
    #   applied: 420 notes, 16 ccs, 0 folded (two runs identical)
    # the EP4-era comparison take is ep73-sweep-EP4-20260727.wav, peak 0.720

The loop set again, same chain as 2026-07-27 (drive 0.15, cabinet 1.0):

    ./build/render_midi -I ep73 -D 0.15 -C 1.0 -r 44100 -t 6 -g 0.52 \
        -o renders/rhodes-loops-tl/ep73-field-20260728/ep73f2-<name>.wav <name>.mid

Twenty renders, all two-run identical. Mean third-octave difference from
the reference set, the two kernels through the same chain, each levelled on
its own 200-800 Hz mean so the comparison is of shape and not of gain:

    kernel              200    400    800   1600   2540   4032   6400  10159   rms>1.6k
    saturator (Jul 27)  3.1   -0.3   -2.8   -7.6    1.6    4.4   -0.2   -5.9      4.8
    field (Jul 28)      5.7   -1.2   -4.5   -7.8   -0.1    3.6   -0.7   -2.2      4.0
    reference           0.0    0.0    0.0    0.0    0.0    0.0    0.0    0.0      0.0

It does not regress against that set and improves it slightly, which is
about all the set can be asked — the standing correction of 2026-07-27
holds, these are produced patches and the MIDI beside them is a different
performance. The measurement that matters here is the decay table above,
and it is against the model's own pinned constants, not against a
recording.

An attack-versus-body descriptor over the same twenty pairs, which is
self-normalising and so survives the references' production EQ:

    reference    2.5 dB     saturator    3.2 dB     field    3.6 dB

**A ballot, and why there is one.** `EP_PICKUP_OFFSET` is the manual's
TIMBRE adjustment — "until the end of the Tine rests on a plane slightly
above dead center of the Pickup ... Let your ear guide you" [EP-SM 4-7].
No source gives it a number, and it is the strongest single control over
the fundamental-to-overtone balance the patent says it is. The compass
sweep is rendered at four settings in
`renders/ep73-ballots-field-20260728/`:

    timbre-u0-0.25.wav   peak 0.709   FNV64 071b33ed3f7c2abe   most octave
    timbre-u0-0.30.wav   peak 0.711   FNV64 34d5c367a6efc594
    timbre-u0-0.35.wav   peak 0.719   FNV64 f7e8a836e9701a04   <- chosen
    timbre-u0-0.40.wav   peak 0.718   FNV64 c443f0360a6c5784   cleanest

0.50 is deliberately not on the ballot: it is the field's inflection point,
where the second harmonic cancels outright and the voice goes hollow.

**Settled 2026-07-28: 0.35**, which is the setting this entry's renders were
already made at, so every signature above stands as pinned and nothing needs
re-rendering.

**The organ is untouched.** No organ translation unit has been edited since
this line began, and both pinned whole-song baselines still render
`6e56f252d97c240c` and `e983aea2ca6ecaf2`. `make test` 9386/0, ten of ten
exhibits PASS.

## 2026-07-28 — the slope ballot

`EP_PICKUP_SLOPE` is the one pickup constant left open (register item 6b,
journal O14). It sets how much further the bass tine sweeps across its
field than the treble one does, so it trades bass growl against treble
cleanliness, and nothing in the sources narrows it: the direction is
sourced — the manual's gap is wider in the bass [EP-SM 4-8], which pulls
the exponent under the momentum value of 1 — but the magnitude is not,
because the manual names two zones without saying how far apart they sit.
Plausible spans give anywhere from 0.12 to 0.45.

**`EP_PICKUP_DRIVE_REF` is re-derived for each take** so that `g(E1)` stays
exactly 1/2, the anchor closed with the field kernel. Without that the
ballot would move the bass and the treble at once and decide nothing. E1 is
therefore bit-identical across all four; everything above it is what moves.

    slope   DRIVE_REF     g(E1)   g(E4)   g(E7)   E1/E7 spread
    1/8     0.38555271    0.500   0.386   0.297      1.68x
    1/4     0.29730178    0.500   0.297   0.177      2.83x     <- shipped
    3/8     0.22925101    0.500   0.229   0.105      4.76x
    1/2     0.17677670    0.500   0.177   0.063      8.00x     (outside the bracket)

What moves, measured — second harmonic against the fundamental at velocity
127, 42.7 ms from the strike:

    slope     E1      E3      E5      E7
    1/8    -13.8   -15.0   -17.2   -19.4     nearly even across the compass
    1/4    -13.8   -17.1   -21.0   -24.7
    3/8    -13.8   -19.1   -24.5   -29.5
    1/2    -13.8   -20.9   -27.7   -34.1     growl confined to the bass

**Level-matched, and this one matters.** As played the four differ by up to
6.6 dB of rms on the loop, because a shallower slope drives the whole
middle harder — the louder take would win any A/B on loudness alone. Each
take is therefore re-rendered at a gain that puts it on the shipped 1/4
take's rms, and all four now sit within 0.01 dB of each other. The gain each
needed is itself the measurement of how much level the slope moves:

    renders/ep73-ballots-slope-20260728/

    slope  sweep gain   peak    FNV64              loop gain   peak    FNV64
    1/8      0.557247  0.655  bb4584e1c00120e6      0.403251  0.293  4c78d745da629bfd
    1/4      0.650000  0.719  f7e8a836e9701a04      0.520000  0.294  f6087f8b24f19325
    3/8      0.743395  0.774  1ce1ef6fba6c4ced      0.680094  0.296  345e7d8a136270a1
    1/2      0.832643  0.816  013824148be30425      0.893403  0.304  35c9087311b3c901

Two takes each: the compass sweep dry, which is where section 2's velocity
ladder at each E makes the bell-to-bark judgement directly, and one loop
through the finished chain (drive 0.15, cabinet 1.0) for musical context —
`90_Am_RhodesDust_03`, the longest in the set at 42.7 s. All eight two-run
identical.

The shipped 1/4 sweep take is byte-identical to the one logged in the
previous entry, so that signature is unchanged and the two entries' takes
are directly comparable.

**What to listen for**: the treble. E1 is the same in all four by
construction, so anything that differs is the middle and top — 1/8 keeps
the growl even all the way up, 1/2 confines it to the bass and leaves the
top nearly a pure tine. The bracket says 1/8 to 3/8 are defensible; 1/2 is
on the ballot as an audible endpoint and would need its own justification
to ship.

## 2026-07-29 — slope closed at 1/8, and what it moved

`EP_PICKUP_SLOPE` settled off the previous entry's ballot: **1/8**, the
shallow end of the sourced bracket. `EP_PICKUP_DRIVE_REF` is re-derived
with it so `g(E1)` stays exactly 1/2 — `2^-1 * 2^(-3/8) = 2^(-11/8) =
0.38555271` — which is the whole reason the ballot was built that way.

The shipped sweep take is byte-identical to the ballot's `slope-1-8-sweep`,
which is the check that the tree and the ballot are the same instrument:

    ./build/render_midi -I ep73 -g 0.557247 -t 6 \
        -o renders/ep73-sweep-slope18-20260729.wav renders/ep73-sweep.mid
    # peak 0.655, FNV64 bb4584e1c00120e6
    #   applied: 420 notes, 16 ccs, 0 folded (two runs identical)

The loop set again, same chain, gain dropped from 0.52 to 0.4033 because
the shallower slope drives the middle harder and the set is mid-register:

    ./build/render_midi -I ep73 -D 0.15 -C 1.0 -r 44100 -t 6 -g 0.4033 \
        -o renders/rhodes-loops-tl/ep73-slope18-20260729/ep73s8-<name>.wav <name>.mid

Twenty renders, all two-run identical. Third-octave difference from the
reference set, each levelled on its own 200-800 Hz mean:

    kernel / slope        200    400    800   1600   2540   4032   6400  10159   rms>1.6k
    saturator (Jul 27)    3.1   -0.3   -2.8   -7.6    1.6    4.4   -0.2   -5.9      4.8
    field, slope 1/4      5.7   -1.2   -4.5   -7.8   -0.1    3.6   -0.7   -2.2      4.0
    field, slope 1/8      4.8   -1.1   -3.7   -7.1   -0.1    3.4   -1.5   -4.0      4.0
    reference             0.0    0.0    0.0    0.0    0.0    0.0    0.0    0.0      0.0

Unchanged at 4.0 against the reference set — the slope moved the middle,
which is where that set is closest to us already, so it barely registers
there. The measurement that shows what the slope did is the ladder across
the compass, in `ep-constants.md` sec 6.1.

**What moved besides the two constants.** The bark — the register where the
tine crosses dead centre at full strike — went from twenty-five notes
(E1..E3) to fifty (E1..F5). That is a real change of character and it is
documented rather than smoothed: at 1/8 this instrument growls nearly all
the way up rather than only underneath.

**One check had to be re-derived, not loosened.** `test.c` asserted the
crossing ended "in the lower middle", which encoded the 1/4 setting rather
than the property, and failed the moment the ballot moved it. It now
asserts the invariant the drive law actually exists to produce — lowest
tine crosses at full strike, highest never does, crossing ends inside the
compass — and where it ends is reported in the constants doc instead.

A second check moved for a better reason. The EP1 exhibit judged the
kernel against its own quadrature, one-sided, on the argument that the
contact transient could only ever *add* to the `2*f1` bin. That was wrong:
the transient's corner falls as the fourth root of level, so at a soft blow
it sits *below* `f1` and inflates the denominator instead, pushing the
measured ratio under the prediction — which is exactly what it did at
velocity 1 here, by 5.5 dB. The kernel is fine: checked directly, f32
against the transcendental, it tracks H2/H1 to 0.17 dB at that same
velocity. So the kernel claim moved into `test.c` where no burst is in the
way, and the exhibit now asserts only what a rendered note can honestly
show — that velocity moves timbre monotonically, 51.3 dB of it.

Gate: `make test` 9389/0, ten of ten exhibits PASS, both organ whole-song
baselines unmoved at `6e56f252d97c240c` and `e983aea2ca6ecaf2`, no organ
translation unit edited.

The 2026-07-28 sweep take above it is kept at the 1/4 setting it documents
(`f7e8a836e9701a04`), so the two entries remain a direct A/B.

## 2026-07-29 — the second polarisation, summed rather than multiplied

The stage of `ep-constants.md` sec 16 did not do what that section said it
did, and the check that found it is one no render in this log had run: two
takes of a single note differing in nothing but the second plane's depth,
compared as an envelope ratio at the bus. On the shipped build the envelope
moved **0.99 dB over two seconds** and 0.00 dB after the first — against a
published table claiming up to 3.48 dB of modulation.

The cause is spectral, not a value. A gain of `1 + a·sin(2π·fh·t)` on a
voice at `f1` puts its energy at `f1 ± fh`, and with the two frequencies a
fraction of a percent apart that is an octave line and a subsonic one, not a
beat. Measured at MIDI 88, `condition = 1.0`: the whole contribution was
2630 Hz at −19.5 dB relative to the note, plus a 0.54 Hz line that the
section 6.2 coupling capacitor removes — confirmed by rendering the same
note with the 10 Hz highpass bypassed, where the 0.54 Hz line reappears at
−0.4 dB of the octave line and the envelope still does not move.

Beating needs the two planes **summed at the sensor**. The pickup's chisel
edge is perpendicular to the hammer's plane and sits off-centre [EP-P61],
and its rotation about the tine is an unpinned setup tolerance, so its
sensitivity axis is not exactly that plane and a small share of the
horizontal motion is sensed alongside the vertical. Summed, the same note at
the same depth gives **3.68 dB**. `test.c` now asserts that swing, and the
assertion fails on the previous build, which is the property a regression
test has to have.

The second plane also stopped borrowing the fundamental's decay. The tine
and the inertia bar form a fork in one plane only, so the base reaction
cancels there and nowhere else [EP-P61], [AT20]; `POLAR_T60_RATIO = 0.60`
is the horizontal dwell as a fraction of the vertical's. Direction sourced,
magnitude on a ballot.

**The ballot**, `renders/ep73-ballots-polar-20260729/`. Two takes each, the
same pair the slope ballot used — the compass sweep dry, and the longest
loop in the set through the finished chain:

    ratio   sweep peak   FNV64              loop peak   FNV64
    1.00      0.689     0256b8ef1c968cbe      0.296     f71496f7d66e7369
    0.60      0.689     d70c2ca0a521117f      0.296     f1717c2046853571   <- shipped
    0.35      0.689     4f49a724bc7600ef      0.296     0d04796920f48f6d
    0.20      0.688     b47b3aaabac604b5      0.293     6baa8c136bd7fea9

    ./build/render_midi -I ep73 -g 0.557247 -t 6 \
        -o .../polar-t60-<r>-sweep.wav renders/ep73-sweep.mid
    ./build/render_midi -I ep73 -D 0.15 -C 1.0 -r 44100 -t 6 -g 0.403251 \
        -o .../polar-t60-<r>-loop.wav 90_Am_RhodesDust_03.mid

All eight two-run identical, and all four within 0.23 dB of each other in
rms, so no take needed a gain match — the ratio shortens a small modulation
without moving level. **1.00 is the previous law** and is on the ballot as
the audible endpoint: the beat holds its depth for as long as the note
lasts. The shipped 0.60 sweep take is byte-identical to the tree's own
render, which is the check that the ballot and the tree are one instrument.

**What to listen for**: sustained single notes, not the loop's chords. The
per-note draw means some notes breathe and some do not, and at `condition =
0.5` the bus figures run 0.02 to 1.23 dB — this is a texture, not an effect.

**Settled 2026-07-29: 0.60**, which is the arm this entry's shipped renders
were already made at, so every signature above stands as pinned and nothing
needs re-rendering.

**`condition = 0` is bit-identical across the rework**, which is the
scanner-OFF discipline every stage on this instrument is held to. The same
sweep rendered with `-N 0` gives `387c2126c2f5d7ca` on the committed build
and on this one; the shipped-condition take is what moves.

**Re-pinned.** The standing compass-sweep take moves with the stage:

    ./build/render_midi -I ep73 -g 0.557247 -t 6 \
        -o renders/ep73-sweep-polar-20260729.wav renders/ep73-sweep.mid
    # peak 0.689, FNV64 d70c2ca0a521117f (two runs identical)
    # was bb4584e1c00120e6 at the slope entry above

**The twenty-loop set was not re-rendered.** Its job in this log is the
third-octave comparison against the reference audio, and a modulation of
about a decibel that decays inside two seconds does not move a long-term
average spectrum — the same reasoning that closed O9 about onset shapes.
The set therefore still stands at the `ep73-slope18-20260729/` take, and any
future spectral claim should use that one rather than re-deriving it here.

Gate: `make test` 9391/0, ten of ten exhibits PASS, both organ whole-song
baselines unmoved at `6e56f252d97c240c` and `e983aea2ca6ecaf2`, no organ
translation unit edited.

## 2026-07-29 — the depth and the rate of the beat, on ballots

`POLAR_T60_RATIO` settled at 0.60 in the entry above. The two constants
beside it did not, and the reason is the same one that produced the entry
above: both were pinned while the stage put nothing on the bus, so neither
`COND_POLAR_DEPTH` nor `COND_POLAR_SPLIT` has ever been heard. (The split
is `EP_POLAR_SPLIT` from 2026-07-30; it is named as it stood at this entry.)

**New material, because the compass sweep cannot decide this.** The sweep's
median note is 0.30 s against beat periods of seconds, so an A/B on it
compares attack transients. `renders/ep73-hold.mid` holds seven E's for
sixteen seconds each at velocity 110, the same seven at velocity 45, and a
chord — 266 s. Generator `renders/ep73-hold.py`, untracked beside it. The
compass top is in it on purpose: it is where a wide split stops beating.

Each ballot holds the other constant at its shipped value. **Both are
level-matched**: the depth moves rms up to 0.63 dB and the split up to
0.26 dB, and after matching all fourteen takes sit within 0.0001 dB of the
shipped arm. The gain each needed is itself the measurement.

    renders/ep73-ballots-polar2-20260729/

    arm                  hold gain   peak    FNV64              loop gain   peak    FNV64
    shipped 0.30/0.004    0.557247  0.686  a713a8efba077929      0.403251  0.292  2f1ad83632e26c4d
    depth-0.60            0.539853  0.694  07e5ed3c3bca6105      0.400921  0.294  7d7c9822730a31c1
    depth-0.85            0.526242  0.700  b754850ad380fb5d      0.398592  0.298  c81393cab2bb1261
    depth-1.00            0.518457  0.703  d4b583edc2dbc9b5      0.397037  0.301  04ee47851f299795
    split-0.010           0.566537  0.697  affa62983710e11d      0.404118  0.292  015c1f3d214db265
    split-0.020           0.571984  0.704  a2913e792fc97aa9      0.404528  0.296  4f78109bc59faf41
    split-0.040           0.574254  0.707  b48136289c4b5a91      0.404477  0.292  6dbd65b576c4df71

**These are the re-cut takes.** The ballot was first rendered with the depth
at 0.60; when the depth closed at 0.30 the split arms were left sitting on a
setting the ear had just rejected, so all seven arms were re-rendered
against a shipped reference of 0.30 / 0.004 and named after the constant
they carry rather than after which ballot they belong to.

    ./build/render_midi -I ep73 -g <gain> -t 6 \
        -o .../<arm>-hold.wav renders/ep73-hold.mid
    ./build/render_midi -I ep73 -D 0.15 -C 1.0 -r 44100 -t 6 -g <gain> \
        -o .../<arm>-loop.wav 90_Am_RhodesDust_03.mid

All fourteen two-run identical. `shipped-0.30-0.004` serves both ballots and
was rendered by the tree's own binary rather than a ballot build, which is
the check that the two are one instrument.

**What the measurements say**, in full in `ep-constants.md` sec 16.3. The
short version is that **the rate turned out to be the stronger lever, which
was not the expectation**: going from 0.004 to 0.040 moves the bus figure
further than raising the depth from 0.30 to 1.00 does, because at the
shipped rate the envelope only traverses part of a cycle before the dwell of
sec 16.2 has taken the breath away. Depth sets how deep the swing is; the
rate sets whether there is time for one.

**What to listen for**, and it differs per ballot. For the depth: whether
the breath is present at all on the notes whose draw is small — the per-note
spread is the point of the stage and a depth that fixes the quiet notes may
make the loud ones a wobble. For the split: **the last held note**.
(The depth question is now answered — see the entry below.) The
widest-drawn note in the compass sits at 4.07 Hz from its twin at the
shipped setting and 40.67 Hz at `split 0.040`, which is no longer a beat but
roughness, and ten of seventy-three notes are past that boundary at the
shipped condition on that arm. 0.010 is the widest arm with nothing past it
at `condition = 0.5`.

Gate unchanged from the entry above: `make test` 9391/0, ten of ten exhibits
PASS, both organ whole-song baselines unmoved, no organ translation unit
edited. Nothing in the tree moved for this entry — the ballot is renders
only, and the shipped constants are untouched.

## 2026-07-29 — the depth settled at 0.30, and what that moved

**`COND_POLAR_DEPTH`: 0.30**, off the ballot above — the quietest arm, and
half what the section shipped before it. Worth recording as more than a
preference: the ear went to the bottom of a bracket whose top was picked to
be audible, which says the second polarisation earns its place as a
disturbance in the texture rather than as anything a listener should be able
to name.

Three things moved with it, and none of them silently.

**The split ballot was re-cut.** Its arms had been rendered with the depth
at 0.60, so the moment the depth closed they were an open ballot sitting on
a rejected setting. All seven arms were re-rendered against a shipped
reference of 0.30 / 0.004; the table in the entry above is the re-cut one
and the files are named after the constant they carry. The split remains
open, register item 21.

**The measurements in `ep-constants.md` sec 16 were re-taken**, not scaled.
The bus figures at `condition = 0.5` now run 0.01 to 0.65 dB across the
compass against 0.02 to 1.23 before. Section 16.2's own ballot table is the
exception and is left at the depth it was taken at, with a line saying so:
it compared four arms against each other, that comparison does not move, and
re-rendering a closed decision to make a table prettier is not evidence.

**The bus-swing assertion in `test.c` was re-derived, not relaxed.** It had
been calibrated against the shipped depth and would have failed at 0.30 —
which is exactly the moment a threshold gets quietly lowered until it
passes. Instead the check now pins its own depth on the bank under test, so
it measures the mechanism rather than the calibration: 4.89 dB summed
against 1.32 dB multiplied, and it still fails on the previous build.

**Re-pinned.** The standing compass-sweep take:

    ./build/render_midi -I ep73 -g 0.557247 -t 6 \
        -o renders/ep73-sweep-polar-20260729.wav renders/ep73-sweep.mid
    # peak 0.640, FNV64 6c28ea4d9953c671 (two runs identical)
    # was d70c2ca0a521117f at depth 0.60

**One slip, recorded where it happened, because it is the second time.**
`build/render_midi` was stale again — linked against an `ep_voice.o` from
before the depth moved — and produced a sweep signature identical to the
0.60 take, which is what gave it away. The same trap in the entry above cost
a ballot arm. A render that is meant to prove a change and comes back
unchanged is a build question before it is an engine question. A second
slip in the same round: a shell loop built the split arms with
`-DSPLIT_V=0.0${v}f` over `v` in `010 020 040`, which is 0.0010 and not
0.010, so one arm came out byte-identical to the shipped setting. Caught by
checking each arm's beat period against the ratio it was supposed to carry
before rendering anything — the arms are now verified that way as a matter
of course.

Gate: `make test` 9391/0, ten of ten exhibits PASS, both organ whole-song
baselines unmoved at `6e56f252d97c240c` and `e983aea2ca6ecaf2`, no organ
translation unit edited.

## 2026-07-30 — the rate settled at 0.040, and the depth reopened

**`EP_POLAR_SPLIT`: 0.040**, off the ballot two entries above — the widest
arm, and the one the ballot's own analysis had flagged as crossing out of
beating and into roughness in the top two octaves. That crossing is why the
compass top was in the hold material, so it was heard rather than predicted,
and the verdict is that it is acceptable: the notes past fifteen hertz of
separation are the widest per-note draws and not the typical ones, and the
top two octaves decay in under five seconds. Register item 21 closed.

Four things moved with it.

**The depth reopened, as register item 24.** Section 16.3 had written down
in advance that the depth verdict was taken with the rate at its slowest arm
and would be worth re-hearing if the rate moved. The rate has now moved to
its fastest arm, a tenfold step, and at 0.30 / 0.040 the bus figures are up
everywhere: about twice through the middle and top of the compass, seven
times at MIDI 40, and more than twentyfold at MIDI 28, where the old rate
could not complete a swing before the breath had faded. The depth was
picked as the quietest arm of a bracket, against a stage that could barely
complete one swing; that reasoning does not carry over to a stage that
completes several. The ear's verdict is not overruled — its premise expired.
Item 24 needs a depth ballot re-cut at 0.040, and that ballot does not exist
yet, so this is an open item and not a pending edit.

**The split bound in `test.c` was re-derived, not widened.** It read
`split > 0.005`, which is the shipped 0.004 plus headroom — a calibration
wearing a bound's clothes, and it would have failed outright at 0.040. The
tempting fix is to type a larger number. Instead the constant is now
exported as `EP_POLAR_SPLIT` and the bound *is* the constant: the split is
that scale times a per-note draw in [-1, 1), so no note can reach it, the
bound is exact rather than approximate, and it moves whenever section 16.3
moves. Two checks were added beside it — that the widest drawn split
approaches its scale, which is what keeps the ceiling from being vacuous,
and that every split scales linearly with condition. Neither catches a
mis-typed constant, and no check in the suite can: the bound and the value
move together by construction. Guarding the constant itself is the ballot's
job, through the beat period each arm has to carry before it is rendered.
9391 checks became 9393.

**Section 16's bus tables were re-measured, and the method was written
down.** The figures at `condition = 0.5` now run 0.02 to 1.65 dB across the
compass against 0.01 to 0.65 before. The beat-period column of the analytic
table is exactly a tenth of what it was at every one of its eight rows,
which is the check that the column follows the ratio rather than being a
second calibration. The split ballot's own table was re-measured too — all
four arms together under one rule, so no arm carries a method the others do
not; the arms keep their order and the verdict is unaffected.

The method had never been recorded, and reconstructing it cost real work:
the first attempt measured the note's own decay rather than the beat, and
the second read waveform phase as envelope at the bottom of the compass,
where a 256-sample window is a fifth of one cycle at MIDI 28 and reports
about a decibel that is not there. It is now stated in section 16 — two
banks differing only in depth, the ratio of their windowed rms, floors off,
window locked to whole cycles of the fundamental. Section 16.2's ballot
table stays at the depth *and* rate it was decided at, with its caveat line
extended to name both.

**Re-pinned.** The standing compass-sweep take:

    ./build/render_midi -I ep73 -g 0.557247 -t 6 \
        -o renders/ep73-sweep-polar-20260730.wav renders/ep73-sweep.mid
    # peak 0.644, FNV64 f7ff238efa784994 (two runs identical)
    # was 6c28ea4d9953c671 at rate 0.004

**The standing take is dated per era from here.** The two entries above both
wrote to `ep73-sweep-polar-20260729.wav`, so that one name held two renders
in turn and the file on disk now carries only the later of them — the
`d70c2ca0a521117f` artifact of the depth-0.60 entry is gone, and that entry
names a file whose contents moved out from under it. Recomputing the data
chunk of what is there gives `6c28ea4d9953c671`, the depth-0.30 render. The
new take took its own date rather than extending the collision.

**`condition = 0` is bit-identical across the rate change**, the same
scanner-OFF discipline every stage here is held to: the sweep rendered with
`-N 0` gives `387c2126c2f5d7ca` on the committed build and on this one.

**One slip, and it is the third of this kind.** The measurement probe failed
to compile and the shell ran the previous binary, which printed a full table
of plausible numbers that were silently one revision old. It was caught
because the validation run was placed first and reproduced the committed
0.004 figures too well for a build that was supposed to have changed. The
probe build now removes its output first and stops the shell on a non-zero
compiler exit. Stale binaries have now cost this project a ballot arm, a
sweep signature, and a measurement table.

Gate: `make test` 9393/0, ten of ten exhibits PASS, both organ whole-song
baselines unmoved at `6e56f252d97c240c` and `e983aea2ca6ecaf2`, no organ
translation unit edited.

## 2026-08-26 — Mamut Analog MA1-5 one-voice audition

Interstitial listening exhibit, not a public milestone baseline. It exposes
the landed source -> pressure -> ladder -> ADSR/VCA path without pulling
MA1-6 identity, MA1-7 output conditioning or MA3 live ownership forward.
The four-event input script is compiled into `exhibit_ma_voice`; each take is
14 seconds, 48 kHz, dual mono, with the same fixed `.5` hosted monitoring
gain.

    make audition-ma1-5
    # build/ma1-5_factory.wav       FNV64 ff6f374aa5f6d149, raw peak .245351
    # build/ma1-5_analog_only.wav   FNV64 af9bcbea779b3359, raw peak .235781
    # build/ma1-5_mozaik_focus.wav  FNV64 018d9ab2064a3fe1, raw peak .326470

GCC, Clang and the sanitized build agree on the PCM signatures. Every take
is finite, nonzero, byte-identical on its second render and below full scale
after monitoring gain. WAVs remain ignored under `build/`; the configuration,
metrics and operator ballot are in `docs/ma1-5-audition.md`.

Operator verdict: accepted as an audible MA1-5 handoff — "odličan jeziv
zvuk". This first reaction records the overall identity finding; it does not
claim a completed factory/Mozaik A/B ballot or authorize retuning.
