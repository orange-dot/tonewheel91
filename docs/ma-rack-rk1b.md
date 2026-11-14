# RK1b — parametric EQ, its editor and the AVX2 wavefront

2026-11-11. Decisions RKD21..RKD26 are in `docs/ma-rack-plan.md`. This
replaces RK1's fixed four-section EQ (`docs/ma-rack-rk1.md`) before anything
was committed. Everything else in RK1 stands: the host, utility, files,
`render_midi --rack` and the Patchbench rack row. Uncommitted on top of
`4aa1099`.

Operator requests, in order: a more advanced EQ with a defined number of
bands and their width ("aj ma neka bude 42"), then "ima li ovde mesta za
avx2 vektorsku obradu? ako ima, ok mi je da smanjimo broj sa 42 na 8". There
is room, so the EQ has 8 bands.

## The EQ

- Up to 8 bands in series, then an output gain. The core layout is
  `MA_EQ_BAND(b, field)` with five fields per band: type, freq_hz, gain_db,
  q, on. That makes 42 parameters per slot (`MA_RACK_PARAMS`).
- Six shapes, Simper's TPT forms: bell, low shelf, high shelf, low cut, high
  cut, notch.
- On/off, shape and band-count changes crossfade per band over the 6 ms
  ramp. A shape change fades out, swaps at zero and fades in. Continuous
  values ramp.
- A canonical spec keeps bands past the count at their initial values.
  `.mapatch` v5 and `.wbpatch` write only the bands in use; the type is
  written by name.

## The editor (Patchbench)

The editor opens from the rack row ("open EQ editor", or double-click an EQ
cell or picture) and takes the place of the two module rows; Esc closes it.

- A log-frequency and dB grid with display ranges of ±6, 12, 24 and 36 dB.
- The summed curve, the selected band's own curve, and the live output
  spectrum while audio runs.
- Numbered nodes in eight fixed hues that read on both themes. Drag sets
  frequency and gain, Shift+drag or the wheel sets width (one undo step per
  wheel gesture), right-click switches a band on or off, double-click on
  empty space adds a bell, Del removes the selected band.
- A panel with the band count, add and remove, enabled, mix, output, the
  selected band's shape buttons, frequency, gain and Q (labelled slope or
  resonance where that is what it is), width in octaves, and a list of every
  band.
- An inserted EQ starts with four neutral bands (low shelf 100 Hz, bells at
  400 Hz and 2.5 kHz, high shelf 8 kHz, all at 0 dB), so its nodes are in
  view.

Curves are evaluated from the coefficients the core computed (RKD26): a
private rack, settled, ticked once, then each band's SVF transfer at each
pixel. Smoke screenshots of the editor are in `build/rk1b-shots2/`:
`pb-eq-{dark,light}-{1920,2560}.bmp`.

## Gates

All three builds pass `make test` (GCC 16.2.1 with LTO; Clang 22.1.8;
`make sanitize`) with 0 warnings, after the AVX2 step as well: block parity
runs 543 wavefront runs with 0 mismatches in each build, and both
audio-thread and core symbol audits pass. Every pinned signature is unchanged; engine
parity is `cfa5a2329dacba61` and bounce parity `f2b0813223e7cf07`.

`test/ma_rack.c` (412 checks):

- **Analytic response.** Eight configurations at 44.1, 48, 96 and 192 kHz:
  each shape alone, all eight bands with every shape plus output gain, and
  Q .1 and 40. The worst deviation is **0.0007 dB** (tolerance 0.1).
  - Intent checks: bell −9 dB at its centre, low cut −3.01 dB at its corner,
    shelf plateaus, and the notch below −40 dB at its centre.
- **Clicks.** Structure: insert, reorder, remove, kind change, 5 adoptions.
  Bands: a shape swap at the signal frequency, on/off, count 1→3→1 and a
  gain jump of 33 dB. In both runs the largest step equals the steady
  bound.
- **Moving and settle.** The ramp is 288 frames at 48 kHz, a shape change
  takes two ramps, and settle ends it at once.
- **Sweeps.** A random valid spec every 64 frames for 400k frames at 44.1 and
  192 kHz: finite, 0 sanitizations. 8 EQs × 8 bands held on full-scale noise
  stay bounded.
- **Validation.** Fractional or unknown type, a count past 8, a band past
  the count off its initial values, and Q out of range are all rejected.
- **Block parity (AVX2).** Below.

Files: `test/hosted.c` checks the v5 band keys and 16 malformed cases
(unknown type, fractional count, a band past the count, a missing band key,
…). `workbench/test/wb_test_w2w3.c` checks `.wbpatch` rack inheritance and
replacement. `test/ma3_cli.py` covers `render_midi --rack` with a band file.

Patchbench: the smoke draws the editor on both themes at 1920 and 2560
wide, with eight bands of every shape and band 3 selected. Undo restores
patch and rack, and the largest frame is 48669 vertices (gate 50000).

The RK1 listening pairs re-render **bit-identically** with their racks
rewritten as bands: Tepih `5c9bba2487abd91a`, Lead `25589c1053cbc512`. The
parametric EQ in the RK1 shape is the RK1 EQ, so the pairs in
`build/rk1-listen/` still stand.

## Cost of the scalar EQ

Run `build/rk1b-perf-20261111-01/` (one binary, two epochs; `summary.md`).
The rows are no rack, an empty rack, F = eight EQs × 4 bands and S = eight
EQs × 8 bands. 8 slots, 128-frame blocks, user-space PMU cycles.

| | auto (lanes) | scalar engine |
|---|---|---|
| empty rack | 1.003× | 1.000× |
| F, 32 bands | 1.172× (+0.228 M cycles/block) | 1.045× (+0.197 M) |
| S, 64 bands | 1.291× (+0.385 M cycles/block) | 1.080× (+0.354 M) |

## The AVX2 wavefront (RKD25)

`src/ma_rack_lanes.c`, `ma_rack_process` in `src/ma_rack.c`, and the pool
render in `src/ma_pool.c`.

- `ma_output_render` is split into a front (body, DC) and a back (master,
  safety). The pool's vector render computes the voices and the front for a
  run of up to 256 frames, runs the rack over the run, then master, safety
  and effects. `ma_voice_pool_tick` stays frame by frame.
- A settled EQ block runs as a wavefront. Lane k is band k on sample t − k,
  fed by lane k − 1's output of the previous step, so a run of n frames
  takes n + 7 steps. Each lane does the scalar `svf_tick` operation for
  operation.
- Every other block runs per frame. A block is settled when it has no ramp
  and no fade, every band sits at weight 0 or 1 with its shape in place and
  its coefficients computed, and nothing is pending. Blocks share no state,
  so block after block gives the frame-after-frame bits.
- Before each run the band states are saved. A non-finite result restores
  them and replays the run per frame, which then sanitizes at the same frame
  the scalar tick would.

Parity: `test_block_parity` gives two racks the same random specs and input.
One ticks per frame; the other runs `ma_rack_process` over random run
lengths with edits, ramps, fades, skipped frames and gains up to +24 dB per
band. Over 138989 frames and 543 wavefront runs there are **0 mismatched
frames**. The pool's block render equals the per-frame `ma_voice_pool_tick`
with an active rack for Tepih, Lead and Granica, dry and wet.

Performance: run `build/rk1c-perf-20261111-02/` (`run.sh`, `table.py`,
`summary.md`, binary SHA256s). A = the scalar-EQ binary of RK1b, B = this
tree. The rows are N no rack, E empty rack, F eight EQs × 4 bands and S eight
EQs × 8 bands. The lanes engine runs all four, the scalar engine N only. 8
slots, 128-frame blocks, scenario 3, effects arm 3, 2048 measured blocks,
user-space PMU cycles, `taskset -c 2`, two epochs in opposite order, quiet
host. The PCM of A and B is identical in all 50 cases.

| | B/A median | B/A worst |
|---|---:|---:|
| N, lanes | 0.987× | 1.005× |
| E, lanes | 0.983× | 0.989× |
| N, scalar engine | 1.005× | 1.009× |

The EQ's own cost (the row minus N, same binary, median over the 10 cases):

| | scalar EQ (A) | wavefront (B) |
|---|---:|---:|
| F: 32 active bands | 0.222 M cycles/block | **0.048 M** (4.6×) |
| S: 64 active bands | 0.383 M cycles/block | **0.048 M** (8.0×) |

The wavefront always runs eight lanes, so 4 and 8 bands per EQ cost the
same. An EQ block with all 8 bands active costs about 6 k cycles per
128-frame block, roughly 0.5 % of an 8-voice block.

A first run (`build/rk1c-perf-20261111-01/`) showed the scalar engine at
+2.1 % median and +4.5 % worst. That is outside the refactor gate, and the
vector path was not involved. The new `pool_front` had stopped being inlined
and specialized for `lanes = false`, as `pool_tick` had been.
`[[gnu::always_inline]]` on `pool_front` and `pool_back` brought it back
(`build/rk1c-fix-check2/`: 0.999–1.009×), and the run above is of that
build.

## Open

- Operator: the editor on the real displays, and by ear: a drag, a shape
  change, and a band switched off while a note sounds.
- Bench snapshots still store the patch only.
