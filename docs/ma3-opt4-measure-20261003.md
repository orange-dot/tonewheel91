# MA3-OPT4 matched performance screen — 2026-10-03

The first matched A/B measurement of the 31-tap FIR traversal is **mixed**.
Some full-pool patches improved, others regressed, and four of ten changed
direction between epochs. The screen does not support a general speedup or a
keep decision. Both variants missed the 48 kHz / 128-frame deadline in every
measured 32-slot block.

Named run: `build/ma3-opt4-measure-20261003-01/`. A is the preserved OPT3
baseline voice; B is the OPT4 FIR candidate. Binary SHA256 identities are
`9b5790ce7ef9e6cffc3e0dafd43315cd874a3763d1867cbce8d017ed122ff23b`
for A and
`bb0d1467456a8b2a646e9885bdcd6af2c21a7a2c8067f618d6a79cda9b5f880c`
for B. Both use the same benchmark harness (SHA256
`aeba7d679d2e4db15fce80db3208b09a5cd08153ee4a5f79d370633826425f7a`),
GCC 16.2.1 source objects, flags, and runtime objects except `ma_voice.o`.
Exact source snapshots, executable copies, hashes, commands and host context
are preserved under the named run. Implementation and correctness checks are
in [MA3-OPT4](ma3-opt4.md).

## Protocol and validity

At 48 kHz, 128 frames, limit 32 and wet effects, the screen covered every
patch with BCS off/on at full 32-slot occupancy. It also covered one-slot
Tepih/BCS-off and Raster/BCS-on cases. Every process performed 64 warmup
blocks and two in-process determinism passes of 256 blocks each. Per case,
epoch 1 used A/B/B/A process order; epoch 2 used B/A/A/B and reversed case
order. CPU 0 affinity and the same render-only timing boundary were used.

The completed recipe has 96 processes, 192 pass summaries and 49,152 raw
measured blocks. Each process was checked against its raw CPU/wall samples,
nearest-rank quantiles, slot occupancy and deadline counters. PCM hashes,
effect fingerprints and deterministic diagnostics matched between A and B
for every compared case. The two passes inside one process are determinism
repeats; paired comparisons use independent process/epoch results. No clock
overhead estimate was subtracted. The run also preserved four empty-bracket
checks and readable frequency, pressure, temperature and process context.

Positive percentages below mean that B used less thread CPU time than A.
Each epoch percentage compares the mean of its two A process means with the
mean of its two B process means; these are screen observations, not confidence
intervals. `screen-summary.md`, `paired-epoch-metrics.csv`,
`process-metrics.csv`, `all-pass-summaries.csv` and every raw CSV are preserved
under the run directory.

Executed commands were `python3 build/ma3-opt4-measure-20261003-01/run_screen.py
run` and then the same script with `summarize`. The run directory has
exclusive-create output files, so it preserves this experiment rather than
serving as a rerun destination. A new run requires a new named directory and
fresh identities. `recipe.json`, `execution-ledger.json`, the frozen runner
and `final-verification.json` record the exact recipe and final checks.
The lab evidence-pack inventory checked all 600 run files present before the
inventory itself was written; `evidence-pack-check.json` records zero omissions.

| Patch | BCS | Epoch 1 CPU change | Epoch 2 CPU change |
| --- | ---: | ---: | ---: |
| Tepih | 0 | −6.97% | −2.65% |
| Tepih | 1 | −1.16% | −2.87% |
| Lead | 0 | +9.63% | +8.94% |
| Lead | 1 | −8.21% | −11.51% |
| Raster | 0 | −2.38% | +22.27% |
| Raster | 1 | +6.78% | +24.25% |
| Prizma | 0 | +18.02% | +3.36% |
| Prizma | 1 | −7.04% | +12.68% |
| Granica | 0 | +19.54% | −13.43% |
| Granica | 1 | −3.59% | +16.11% |

The two sparse cases also changed direction: Tepih/BCS-off was −6.12% then
+8.51%, and Raster/BCS-on was −3.41% then +18.90%. Across the full cases,
adjacent process-pair changes ranged from −23.60% to +29.52%. Lead without
BCS improved in both epochs and all four adjacent pairs; Lead with BCS
regressed in both epochs. This is insufficient to accept the change across
the required patch matrix.

Both A and B missed the 2.667 ms CPU and wall deadlines in all 20,480
full-pool blocks per variant. Full-pool process CPU means ranged from
5.579 to 12.567 ms for A and 5.754 to 11.882 ms for B. The sparse cases
had zero CPU or wall misses in 4,096 blocks per variant during this window.
The empty-bracket wall maxima ranged from 2.549 to 21.149 microseconds,
lower than the extreme outlier in the previous baseline run; this does not
establish that the host was stable. Readable CPU 0 frequency snapshots across
process boundaries ranged from about 0.8 to 3.3 GHz under the recorded
`schedutil` governor. Visible CPU pressure varied across the window, and the
visible cgroup recorded 54.1 seconds of cumulative CPU pressure during
roughly 500 seconds of elapsed run time. It recorded no quota-throttled
periods. One candidate block took 111.376 ms wall time while its thread CPU
time was 12.757 ms. These observations show substantial environmental
variation; they do not identify the frequency or cause during each block.

## Decision and next evidence

**OPT4 is not accepted as a measured improvement.** It remains a source
candidate in the working tree because the current screen cannot separate a small code effect
from frequency/environment variation across all cases. In particular, the
repeated Lead/BCS-on regression and four epoch sign reversals fail the plan's
retention screen. There is no basis to claim MA3-LIVE readiness; the 32-slot
budget gap remains large in both variants.

A later keep/reject decision needs an environment with more stable operating
conditions and focused, balanced confirmation of both gains and regressions.
The plan's long 256-warmup/2,048-block confirmation was not run automatically
after this mixed screen. This document preserves the screen and its limits
without treating individual blocks as independent evidence of a speedup.

An independent reviewer reported recomputing all 96 process results and
49,152 raw blocks, confirming A/B PCM and diagnostics, paired arithmetic and
the host context. The coordinator also checked frozen hashes, the execution
ledger, row counts and all deadline-miss totals in `final-verification.json`.
