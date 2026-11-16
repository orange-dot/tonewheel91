# Oscillator study (SPICE)

Two circuits simulated in ngspice and compared with the Mamut Analog
oscillator. The results are in `docs/ma-vco-b1-spice.md` and
`docs/ma-vco2-spice.md`; what the core takes from them is in
`docs/ma-vco-plan.md`. Dev-side tooling, run by hand: nothing here is a build
or test dependency.

| | circuit | source |
|---|---|---|
| B1 | triangle core, 24 transistors | `b1-bench-cv-2.cir`: the B1-THT bench of the lab's 2026-10-16 run (`mamut-synthesis-research`), copied unchanged |
| VCO-2 | exponential converter and reset-type saw core, 16 transistors | `vco2.cir`: a study circuit written for the digital model, not a board design |
| VCO-2 pair | two VCO-2 cores on one supply with a series resistance and bus capacitor | built by `vco4.py` from `vco2.cir` |
| sync | a VCO-2 master and slave on ideal rails, the master's reset pulse into the slave's reset driver (a resistor) or its comparator threshold (a capacitor) | built by `vco5.py` from `vco2.cir`; study networks, not a board design |
| shapers | triangle to sine (degenerated long-tailed pair) and triangle to saw (a linear pair swapped by a switching quad on B1's square), 10 transistors | `shapers.cir`: study circuits hung on B1's buffered triangle, not a board design |

`models/` holds the lab's declared engineering transistor and support models,
copied from the same run. `SHA256SUMS` covers the copied files.

## Running

Needs ngspice and Python with numpy. Everything generated goes under
`build/vco/b1/` and `build/vco/vco2/` (`VCO_OUT_B1` and `VCO_OUT_VCO2` point
elsewhere). Wrap the long runs in `systemd-inhibit`.

```sh
make build/vco_ma_ref                 # the MA oscillator alone, for the comparisons

python3 driver/spice/vco/b1_cases.py  # benches: CV sweep, two temperatures, the phrase
driver/spice/vco/b1_run.sh build/vco/b1          # about 25 minutes
python3 driver/spice/vco/b1_analyze.py && python3 driver/spice/vco/b1_render.py
python3 driver/spice/vco/b1_report.py            # build/vco/b1/summary.md

python3 driver/spice/vco/vco2_runs.py calibrate A    # then B: the technician's trim
python3 driver/spice/vco/vco2_runs.py track A        # then B: C0..C9
python3 driver/spice/vco/vco2_runs.py temp A
python3 driver/spice/vco/vco2_runs.py phrase A       # then B, and phrasewarm B
python3 driver/spice/vco/vco2_analyze.py && python3 driver/spice/vco/vco2_render.py
python3 driver/spice/vco/vco2_report.py              # build/vco/vco2/summary.md

# V2: B1's two ramps below the step-1 range (27 Hz .. 1.94 kHz)
python3 driver/spice/vco/b1_low.py gen && driver/spice/vco/b1_run.sh build/vco/b1low
python3 driver/spice/vco/b1_low.py analyze           # build/vco/b1low/low.json

# V3: the shapers on B1 (outputs under build/vco/shapers/)
python3 driver/spice/vco/shapers.py static   # DC curves, the sine's degeneration
python3 driver/spice/vco/shapers.py trim     # the technician's trim at A4, in place
python3 driver/spice/vco/shapers.py fit      # the sine curve for the core (fit.json)
python3 driver/spice/vco/shapers.py bench && driver/spice/vco/b1_run.sh build/vco/shapers
python3 driver/spice/vco/shapers.py analyze  # shapers.json
python3 driver/spice/vco/shapers.py bench_high && driver/spice/vco/b1_run.sh build/vco/shapers
python3 driver/spice/vco/shapers.py analyze_high   # V2b, B1 with a range switch: high.json
python3 driver/spice/vco/shapers_render.py         # listen/

# V4: two VCO-2 cores on one supply (build/vco/vco4/)
python3 driver/spice/vco/vco4.py case NAME RS CB NOTE DETUNE_CENTS SECONDS [INTERVAL]
python3 driver/spice/vco/vco4.py analyze NAME ...
build/vco/vco4/run_set3.sh                         # the other simple ratios (P:Q as the interval)
python3 driver/spice/vco/vco4_fit.py NAME ...      # sawtooth per pair -> fits4.json
python3 driver/spice/vco/b1_low.py analyze         # (and the ramp bows: build/vco/b1low/bow.json)
python3 driver/spice/vco/vco4_render.py            # listen/

# V5: sync, a master and a slave core on ideal rails (build/vco/vco5/)
python3 driver/spice/vco/vco5.py case NAME reset|thresh|free VALUE NOTE RATIO SECONDS [keep]
build/vco/vco5/run_set2.sh                         # the slave's ratio swept, per network and value
python3 driver/spice/vco/vco5_fit.py               # what a master reset does -> fits5.json
python3 driver/spice/vco/vco5.py steps NAME KIND VALUE NOTE SECONDS I1,I2,...   # listening runs
make build/vco_ma_ref && python3 driver/spice/vco/vco5_render.py                # listen/
```

The VCO-2 runs are cached by their inputs (`runs/<name>/tag.json`), so a
repeated command only simulates what changed.

## Limits

The transistor models have no noise, no high-current roll-off and 0.1 ohm
bulk resistances, so neither study says anything about jitter or drift within
a note. No board has been measured. VCO-2's rails, CV and trims are ideal
sources.
