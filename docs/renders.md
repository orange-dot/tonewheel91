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
own **reference audio**: the library ships a rendered WAV beside each MIDI,
recorded from the instrument the loops were written for. That makes it the
first material in this repo where the electric-piano line can be heard
directly against the thing it models, on identical notes — which is what
`piano-backlog.md` names as the precondition for EP3 and what the EP1
evidence recorded as an open item.

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
