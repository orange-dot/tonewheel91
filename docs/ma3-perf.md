# MA3-PERF hosted throughput

Resumption plan (2026-10-02): [MA3-PERF execution plan](ma3-perf-plan.md)
defines agent/model assignments, fresh baseline and harness audit, profiling,
candidate gates, and the handoff to live acceptance. The
[first execution batch](ma3-perf-20261002.md) now has fresh results and explicit
host-noise limits. The results below remain the historical measurement cohort.

MA3-PERF is a hosted timing slice for the pool and MA3-2 effects. It is a
measurement artifact, not a realtime acceptance claim. The benchmark pins the
process with `taskset`, records thread CPU time and monotonic wall time around
`ma_voice_pool_render`, and performs a second deterministic pass for every
case.

The reference run used GCC 16.1.1 on host `192.168.1.16`, kernel
`7.1.8-200.fc44.x86_64`, x86-64, CPU affinity 0, 48 kHz, four warmup blocks
and sixteen measured blocks. It covered Tepih, Lead, Raster, Prizma and
Granica, BCS on/off, limits 16/32/64, 128/256/512 frames, zero/one/eight/full
slots, sustain/release and unison 2/4/8, with dry, chorus, reverb and wet
arms. The complete CSV is `build/ma3-perf-default32.csv` (ignored generated
output); the smoke matrix is `build/ma3-perf-smoke.csv`.

The run emitted 1,920 rows (two passes for each case), with no PCM hash
mismatch, sanitization or safety intervention. The largest full-pool wet CPU
means were:

| limit | block | CPU mean | CPU / block budget |
| ---: | ---: | ---: | ---: |
| 32 | 128 | 9.884 ms | 3.707x |
| 32 | 256 | 19.753 ms | 3.704x |
| 32 | 512 | 40.265 ms | 3.775x |

The ratios are above one on this host, so the default 32-slot pool is not a
48 kHz realtime configuration yet. This slice records the cost and keeps the
quality, oversampling and polyphony fixed; it does not silently reduce any of
them. The next performance task should profile the stage costs and implement
measured optimizations before choosing a live limit. Larger buffers change the
per-block ratio but do not establish sufficient total DSP throughput.

Run a focused matrix with:

```sh
make build/bench_ma3_perf
MA3_PERF_WARMUP=8 MA3_PERF_BLOCKS=32 taskset -c 0 build/bench_ma3_perf --run > build/ma3-perf.csv
```

Filters `MA3_PERF_PATCH`, `MA3_PERF_BCS`, `MA3_PERF_LIMIT`, `MA3_PERF_FRAMES`,
`MA3_PERF_SCENARIO` and `MA3_PERF_ARM` select a smaller case. The benchmark
allocates timing arrays before the measured loop; the render call itself has
no benchmark-owned allocation, lock or I/O.
