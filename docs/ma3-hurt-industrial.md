# MA3 Hurt, industrial, 2026-11-07

Operator request: base the arrangement on the original performance, make it
stronger and closer to the original than the orchestration edition 2, add a
heavy industrial rhythm, and keep the orchestration simple.
`driver/exhibit_ma3_hurt_industrial.c` renders it in one pass with
`ma3_show`. It is hosted only; it uses the Glas core unit (`ma-constants.md`
§10C) and the Orbita primitive (`driver/ma_orbita.{h,c}`).

```sh
python3 -I driver/make_hurt_industrial.py        # notes-midi/local/hurt-industrial-20261107.mid
make check-ma3-hurt-industrial                   # score and pools, no audio
make audition-ma3-hurt-industrial                # build/ma3-hurt-industrial/hurt_industrial_{stem}.wav
build/exhibit_ma3_hurt_industrial --render PREFIX --parts kick,snare   # any subset of parts
```

## Score

`driver/make_hurt_industrial.py` reads the Songparts transcription of the
original recording (`notes-midi/local/hurt-songparts-vocals.mid`, SHA256
`1a81d568…`; local, ignored, not redistributed). It writes
`notes-midi/local/hurt-industrial-20261107.mid` (SHA256 `5f0431c4…`). The
output is identical on every run.

The form is the original's, in source bars:

| Bars | Section |
| --- | --- |
| 8–23 | verse 1 |
| 24–39 | bass and pad enter |
| 40–47 | piano and the original toms |
| 48–67 | quiet second verse |
| 68–75 | distorted guitar |
| 76–90 | climax |
| 91 | the hit |
| 92–100 | held E ending |

The render starts at bar 8; the FX wash intro is left out.

| Ch | Part | Source | Changes |
| ---: | --- | --- | --- |
| 0 | arp | Guitar 2 | ring capped at 1.5 bars |
| 1 | keys | Piano | ≤ 1 bar |
| 2 | bass | Bass | B0/D1 an octave up |
| 3 | vocal | Lead Vocals | unchanged |
| 4 | pad | Synth 2 + Synth 1 | — |
| 5 | gtr | Guitar 1 | — |
| 6–8 | kick, snare, hat | derived | industrial kit per section (below) |
| 9 | metal | Drums 41/45/47 | the original toms' rhythm, as metal sizes 50/55/59 |

Left out: the FX wash, Synth 3 (the climax lead line) and the side-stick.
Within each part, a re-strike of a held pitch ends the earlier note, and
every note is released by bar 100.

The kit runs on a 16th grid. The fourth column is the hat; "metal" lists
strikes added on top of the toms' rhythm.

| Section | Kick | Snare | Hat | Extra metal |
| --- | --- | --- | --- | --- |
| verse 1, bars 8–15 | 1 | — | — | beat 4, every other bar |
| verse 1, bars 16–23 | 1, 3 | — | off-beat 16ths | beat 4, every other bar |
| build | 1, 2&, 3 | 2, 4 | 8ths | beat 4, every 4th bar |
| peak | 1, 2&, 3, 3a | 2, 4 | 16ths | toms' rhythm |
| verse 2 | 1, 3& | 3 (half-time) | — | beat 4, every other bar |
| chorus 2 | 1, 2&, 3, 3& | 2, 4 | 8ths | — |
| climax | 1, 1a, 2&, 3, 3a, 4& | 2, 4, plus a 16th fill into every 4th bar | 16ths | toms' rhythm |
| bar 91 | one hit | one hit | — | one hit |
| ending | — | — | — | — |

## Sound

- **Kit:** distorted Mamut drums.
  - kick: sine and triangle with noise .06, filter drive .45, body drive
    .55, short envelope;
  - snare: noise .85, resonance .22, mixer pressure .60, filter drive .70,
    body drive .80, 3 ms attack;
  - hat: noise .95 at 9 kHz, 45 ms.
- **Orbita metal** (hosted layer):
  - Every strike re-seeds a five-moon bank on one of four voices and starts
    it in full ring breakup, so it is inharmonic and clangorous.
  - While it rings, the section's dissipation pulls the moons onto
    small-integer ratios: 0 in verse 1, .30 in the build, .60 in the peak,
    .20 in verse 2, .60 in chorus 2 and .90 in the climax. The metal goes
    from out of tune to in tune as the song climbs.
  - Decay τ is .38, .28 or .20 s by size. Drive is 1.8 → 3.6 into an offset
    `tw_sat`, then a 20 Hz DC blocker, a 6 kHz lowpass and its own reverb
    (.26).
- **Pad:** Orbita on the VCO2 ratio.
  - It enters 16 c sharp at bar 24 and locks by bar 28.
  - Escape kicks at bar 32 (+2.0 %) and bar 68 (+2.4 %; sour until 70,
    then it relocks).
  - On the closing chord, a −2.4 % kick at bar 92 and a slow final lock.
- **Vocal:** Glas v5 take a (`docs/ma3-hurt-orbita.md`) in its wet, dark
  room. The automation runs over bars 8–92.
- **Distorted guitar:** saw and pulse, BCS .40, filter drive .55, body
  drive .50, heat .20 → .45 with the form.
- **Bass:** the dark MA3 bass with filter drive .40 and body drive .35.

## Balance

The part gains come from single-part renders (`--parts`). Pools are
independent, so each such render is that part's exact contribution. The
measurements were per-section RMS, with the climax kick (−43.9 dBFS) as the
reference. Targets: snare −3 dB, metal −6 dB, hat −12 dB, guitar +2 dB,
bass about −1.5 dB.

The first pass measured, in dB: metal +26.7 over the kick, snare −21.5,
hat −29.1. The trims that followed:
- metal ×.023;
- snare master 1.0;
- hat master .90 and gain 1.21;
- guitar gain 2.48, keys 2.13, arp 1.26.

The snare then set the master's headroom.
- Its climax hits peaked at −16.8 dBFS in the raw mix, while 99.9 % of
  samples sat below −24.6 dBFS. With a static master, those peaks capped
  the master at +14.6 dB (RMS −26.2 dBFS).
- A softer attack (1 → 3 ms) did not lower the peak: the peaks are in the
  noise body.
- Saturating the snare inside the voice did. Mixer pressure is now .60,
  filter drive .70 and body drive .80, and the snare was re-trimmed to the
  same climax RMS, part gain 1.49.
- The master then gains +17.08 dB (RMS −23.7 dBFS) at the same −1 dBFS
  peak. This is sound design in one voice, not compression of the mix.

## Results

Single pass, GCC 16.2.1, `-O2 -ffp-contract=off`, 48 kHz stereo, 283 s,
wall time 9 min 29 s.

| Stem | Peak | RMS | FNV-64 |
| --- | ---: | ---: | --- |
| tonal | .018116 | .002836 | `1abb63ee3bd2f986` |
| bass | .024889 | .003831 | `f112d065c5303e05` |
| pads | .019288 | .001228 | `999d0d448ba35970` |
| vocal | .030792 | .003358 | `6d5da6dde77698db` |
| guitar | .060832 | .005122 | `96b1cef2ea3aab0c` |
| rhythm | .119437 | .005407 | `40b939b82d4d23d0` |
| mix | .125286 | .009550 | `10cafff0c99bd4e3` |

- No FX clips, and no held or sustained steals. The arp has 50 released
  steals. Every pool sleeps by the end.
- The pad moon has 4 captures, 3 escapes and 0 fine clamps. There are 299
  metal strikes.
- The rhythm stem of a `--parts kick,snare,hat,metal` render matches the
  full render's rhythm stem (`40b939b8…`).

Master: `driver/remix_stems.py`, vocal −6 dB, one static gain of +17.0786 dB
to a −1 dBFS peak, RMS −23.74 dBFS, with no compression or limiting.
`hurt_industrial_master_vocal-6.wav` has SHA256 `36d82cc9…` and is copied to
the internal listening folder.

Section RMS of the master (before the snare change, in dBFS): verse 1
−32.7, build −27.2, peak −25.4, verse 2 −31.2, chorus 2 −24.8, climax −22.7,
ending −23.4. The snare change raised the whole master by 2.47 dB.

## Revision: kick and hat only

Operator: take the snare out entirely, keep only kick and hat, raise the
hat (it was far too weak), and lower the whole rhythm a little in the final
mix. The other lines stay as they are.

- **Defaults:** the exhibit now renders all parts except snare and metal.
  Both stay in the score and render only when `--parts` names them.
- **Hat:** part gain 1.21 → 3.04 (+8 dB).
- **Render:** only the rhythm group was re-rendered
  (`--parts kick,hat`): rhythm stem FNV `2f80468a751c5d34`, peak .0978,
  RMS .00479. It is saved as `hurt_industrial_rhythm_kick_hat.wav`. Pools
  are independent, so the other stems are unchanged.
- **Mix:** `hurt_industrial_master_kick_hat.wav`, SHA256 `162febfb…`.
  Vocal −6 dB, rhythm −3 dB, and the previous master gain fixed with
  `--master-db 17.0786`, so every other stem keeps its output level. Peak
  −3.95 dBFS, RMS −24.79 dBFS, no compression.

### Hat: quieter and wetter

Operator: the hat must be much quieter and wetter.
- **Level:** part gain 3.04 → 1.21, undoing the +8 dB.
- **Room:** chorus .15 (depth .35, rate .25 Hz) and reverb .45 (size .70,
  damping .50). Before, the room was reverb .08 with no chorus.
- **Rhythm stem:** `--parts kick,hat`, FNV `7a2b18de1c2288c4`, peak .0513,
  RMS .00420, saved as `hurt_industrial_rhythm_kick_hat_wet.wav`.
- **Mix:** `hurt_industrial_master_kick_hat_wet.wav`, SHA256 `a03359e1…`,
  with the same gains and fixed master as above. Peak −6.49 dBFS, RMS
  −24.98 dBFS.

## Revision: the MA3 Hurt rhythm

Operator request: use the kick and rhythm of the first MA3 Hurt
(`exhibit_ma3_hurt`) instead of the industrial kit and its revisions.

- **Score:** `python3 -I driver/make_hurt_industrial.py --rhythm ma3` →
  `notes-midi/local/hurt-industrial-ma3rhythm-20261107.mid` (SHA256
  `60743da4…`). The first-edition score still reproduces (`5f0431c4…`).
- **The MA3 rhythm, on the original form:**
  - kick: note 35 on 16th steps 0 and 6, plus 11 in the build, peak,
    chorus 2 and climax. Velocity 113 on the one and 94 otherwise, +8 in
    the climax;
  - pulse: on beat 3, at the half-bar root an octave up (MIDI 43–52),
    velocity 78, or 92 in the climax;
  - bar 91: one kick (121) and one pulse;
  - ending: single kicks (78) on bars 92–94.
  - The half-bar root follows the MA3 material's rule: the lowest bass
    note, else the lowest guitar note, else the previous root, folded into
    MIDI 31–42.
- **Exhibit:** `--rhythm ma3` uses `drum_patch(KICK)` and `drum_patch(PULSE)`
  unchanged. Kick gain 1.1 (reverb .04); pulse gain 1.7 (chorus .06, reverb
  .14). Hat and metal are empty, and the layer is off.
- **Rhythm stem:** `--rhythm ma3 --parts kick,pulse`, FNV
  `de1f65c0459dc3d6`, saved as `hurt_industrial_rhythm_ma3.wav`.
- **Level:**
  - Rhythm relative to everything except the vocal is −4.79 dB RMS in the
    base MA3 Hurt render and −6.33 dB here, so the remix trims the rhythm
    +1.54 dB.
  - Master: `hurt_industrial_master_ma3rhythm.wav`, SHA256 `49f1472f…`.
    Vocal −6 dB, master +17.0786 dB fixed, peak −4.73 dBFS, RMS
    −24.50 dBFS, no compression.

## Revision: a thinner, angrier first verse

Operator: in the vocal up to about 48 s (the first verse, bars 8–24), keep
the melody but add a little dirt and a "thin anger".

`--vocal-edge` blends the vocal toward edge settings. The blend is full in
bars 8–23 and fades out over bars 23–25.

| Control | Before | Edge |
| --- | ---: | ---: |
| formant shift | −.40 | −.22 |
| noise | .012 | .05 |
| grain mix | .30 | .38 |
| BCS | .06 / .20 | .25 / .36 |
| cutoff | 1100 Hz | 1200 Hz |
| filter drive | ≈ .30 | .50 |
| mixer pressure | ≈ .30 | .45 |
| body drive | .10 | .16 |
| level | 1 | ×.733 |

The grit sits before the steep lowpass. Post-filter body drive is what made
the earlier vocals sound like a duck call, so it only rises a little.

| Bars 8–24 | RMS | Centroid | 0.8–2.5 kHz | > 2.5 kHz |
| --- | ---: | ---: | ---: | ---: |
| before | −48.2 dB | 362 Hz | −21.9 dB | −58.9 dB |
| first try (shift −.10, cutoff 1400, grains .45) | −43.5 dB | 418 Hz | −12.8 dB | −54.3 dB |
| kept | −47.5 dB | 382 Hz | −16.6 dB | −57.8 dB |

- The first try reached the duck-call range (v2 measured −11 to −15 dB)
  and was too loud.
- The kept take is 0.7 dB louder than before.
- Bars 40–91 are unchanged.

Vocal stem: `--rhythm ma3 --vocal-edge --parts vocal`, FNV
`5080162d22aeb435`, saved as `hurt_industrial_vocal_edge.wav`.

Mix: `hurt_industrial_master_ma3rhythm_edge.wav`, SHA256 `83ea5bcb…`.
- It replaces both the rhythm stem (MA3 rhythm) and the vocal stem (edge).
- Vocal −6 dB, rhythm +1.54 dB, master +17.0786 dB fixed.
- Peak −4.73 dBFS, RMS −24.49 dBFS.

## Revision: a fat first verse

Operator request: make the first verse thicker, fat and huge,
like the big late-70s polyphonic analog flagships.

Unison is a pool setting and cannot be automated inside one pool. With
`--vocal-fat`, vocal notes that start before source bar 24 therefore go to a
tenth pool, `vocalfat`. Note-offs follow their note-on through a per-pitch
FIFO.
- **Voices:** four per note, detuned by 10 cents, limit 16, width .90.
- **Oscillators:** VCO1 saw .60, pulse .18 (PW .45), triangle .10, sine
  .22. VCO2 an octave down: saw .50, triangle .10, sine .15, level .85.
  Noise .03, BCS .15/.30.
- **Filter:** ladder 900 Hz, resonance .20, filter drive .45, mixer
  pressure .40.
- **Glas:** kept light, formant mix .25 and grains .10.
- **Room:** the vocal's own room. Pool gain is 2.2 × 1.995.
- The `--vocal-edge` blend still applies to the main vocal pool around
  bar 24.

| Bars 8–24 | RMS | Centroid | 0.8–2.5 kHz | > 2.5 kHz | < 250 Hz share | Side/mid |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| edge take | −47.5 dB | 382 Hz | −16.6 dB | −57.8 dB | −11.0 dB | −16.3 dB |
| fat, first try (formants .50, grains .20, cutoff 1300) | −49.4 dB | 411 Hz | −14.0 dB | −48.5 dB | −11.7 dB | −10.1 dB |
| fat, kept | −46.5 dB | 357 Hz | −16.1 dB | −53.9 dB | −5.9 dB | −10.2 dB |

- The first try was only wider. The formant bank and the grains cut the
  low end and added buzz.
- With them reduced, a darker ladder and a stronger sub-octave, the
  low-end share rises by 5 dB and the image stays 6 dB wider.
- Bars 40–91 of the vocal are unchanged.

Vocal stem: `--rhythm ma3 --vocal-edge --vocal-fat --parts vocal`, FNV
`12efe8ae47663b38`, saved as `hurt_industrial_vocal_fat.wav`. The fat pool
peaks at 16 slots with no steals.

Mix: `hurt_industrial_master_ma3rhythm_fat.wav`, SHA256 `08c44b52…`. Same
gains and fixed master as the edge mix. Peak −4.29 dBFS, RMS −24.48 dBFS.

### The whole vocal fat

Operator: the thickness has to be heard across the whole vocal.
`--vocal-fat-all` routes all 200 vocal notes to the `vocalfat` pool, with
the same static patch. The first verse is bit-identical to the fat take up
to the bar-24 tails.

| Vocal stem | Bars 8–24 | Bars 24–40 | Bars 40–91 |
| --- | --- | --- | --- |
| fat first verse | −46.5 dB, 357 Hz, nasal −16.1 dB | −52.8 dB, 392 Hz, nasal −20.3 dB | −48.5 dB, 449 Hz, nasal −21.8 dB |
| fat throughout | −46.5 dB, 357 Hz, nasal −16.1 dB | −50.6 dB, 366 Hz, nasal −15.1 dB | −47.3 dB, 379 Hz, nasal −15.8 dB |

Each cell is RMS, centroid and the 0.8–2.5 kHz share.

- The rest of the vocal now has the first verse's colour and is
  1.2–2.2 dB louder. That matches the +1 dB the fat first verse took over
  the edge take.
- One released-group steal; the pool peaks at 16 slots.
- Vocal stem: FNV `7d28100b09ed230f`, saved as
  `hurt_industrial_vocal_fat_all.wav`.
- Mix: `hurt_industrial_master_ma3rhythm_fat_all.wav` (SHA256 `14dad94b…`), with the same gains
  and fixed master.

Recipe for this edition:

```sh
python3 -I driver/make_hurt_industrial.py --rhythm ma3
build/exhibit_ma3_hurt_industrial --rhythm ma3 --vocal-fat-all --render PREFIX
python3 -I driver/remix_stems.py PREFIX OUT.wav --stems tonal bass pads vocal guitar rhythm \
  --gain vocal=-6 --gain rhythm=1.54 --master-db 17.0786
```

## Revision: the guitar motif fat and wet, with a clear pulse

Operator: the motif from 2:58 (source bar 68) repeats one tone. Make it fat
with a stronger reverb, but keep a clear pulse. Do not raise the level; it
may even come down 1 dB.

The motif is Guitar 1:
- bars 68–75: A3 quarter notes, 360 ticks each;
- bars 76–83: the same A3 with bass notes;
- bars 84–90: A2 quarters.

The part plays only from bar 68, so `--gtr-fat` changes it as a whole:

- **Voices:** unison 4, detuned 10 c, limit 24, and VCO2 an octave down
  (level .70, fine 0). Width .90.
- **Pulse:** filter envelope .60 with ADSR 2 / 160 / .35 / 120 ms, a click
  on every note. Amp ADSR 4 / 200 / .55 / 120 ms. The release was 600 ms.
- **Room:** chorus .20; reverb .38, size .80, damping .65. It was .14
  and .20, size .65.
- **Level:** gain ×1.683, set by measurement.

The measure below is guitar stem RMS. The pulse is the median, per beat, of
the 10 ms envelope peak over the mean of the last 20 ms before the next
beat.

| Bars | Before | First try (reverb .45, release 250 ms) | Kept |
| --- | --- | --- | --- |
| 68–76 | −44.2 dB, pulse 17.0 dB | −46.6 dB, 13.2 dB | −45.2 dB, 14.8 dB |
| 76–84 | −41.2 dB, 7.0 dB | −43.6 dB, 5.9 dB | −42.2 dB, 7.9 dB |
| 84–91 | −43.2 dB, 4.0 dB | −45.0 dB, 5.1 dB | −43.6 dB, 6.7 dB |

- A larger room fills the space between notes. The shorter release, the
  click and the lower sustain bring most of that back in the motif, and
  later in the song the pulse is clearer than before.
- The level is 1 dB under the previous guitar, 0.4 dB in the last section.
- The pool peaks at 24 slots with one released steal.
- Guitar stem: `--rhythm ma3 --gtr-fat --parts gtr`, FNV
  `ab0f4ab5ccc91f67`, saved as `hurt_industrial_guitar_fat.wav`.
- Mix: `hurt_industrial_master_ma3rhythm_fat_all_gtr.wav` (SHA256
  `12c94b0f…`). It replaces the rhythm, vocal (fat throughout) and guitar
  stems; same gains and fixed master. Peak −4.43 dBFS, RMS −24.72 dBFS.

The current recipe adds `--gtr-fat` to the render line above.
