#!/usr/bin/env python3
"""B1 below its studied range (V2): how unequal the triangle's two ramps are from
about 17 Hz up, where step 1 stopped at 225 Hz. The B1-THT DUT unchanged, a fixed
CV per case, only the capacitor node and the square saved.
  b1_low.py gen        cases under <out>/spice/ (then b1_run.sh <out>)
  b1_low.py analyze    <out>/low.json: f, swing, slopes, rise fraction per case
<out> is build/vco/b1low (or $VCO_OUT_B1LOW)."""
import json, subprocess, sys
import numpy as np
import paths
from b1_analyze import crossings, shape

out = paths.out('b1low')
FS = 1.92e6
# Hz ~ 383.7 * (cv + 0.086) near the bottom (step 1's straight line)
CASES = ['-0.04', '-0.02', '0', '0.03', '0.08', '0.15', '0.25', '0.35', '0.5', '1', '2', '3', '4', '5']
SETTLE = 0.05

def hz_guess(cv): return 383.7 * (float(cv) + 0.086)

def gen():
    g = [sys.executable, str(paths.SRC / 'b1_gen.py')]
    for cv in CASES:
        periods = 14 if hz_guess(cv) < 200 else 40
        tstop = SETTLE + periods / hz_guess(cv)
        subprocess.run(g + [str(out / 'spice' / f'cv{cv}' / 'bench.cir'), f'cv={cv}',
                            f'tstop={tstop:.5f}', f'tstart={SETTLE}', 'vecs=v(TRI) v(SQUARE)'],
                       check=True)

def analyze():
    rows = []
    for cv in CASES:
        d = np.loadtxt(out / 'spice' / f'cv{cv}' / 'trace.tsv', skiprows=1)
        tri = d[:, 1]
        r = shape(tri, FS)
        mid = .5 * (tri.min() + tri.max())
        up = crossings(tri, FS, mid)
        hz = (len(up) - 1) / (up[-1] - up[0])
        r['swing'] = r['vertex_hi'] - r['vertex_lo']
        rows.append(dict(cv=float(cv), hz=hz, **r))
        print(f"cv {cv:>6}  {hz:9.3f} Hz  rise {r['rise_fraction']:.5f}  "
              f"up {r['slope_up']:9.2f} V/s  down {-r['slope_down']:9.2f} V/s  "
              f"swing {r['swing']:.4f} V  pwl {r['pwl_residual_db']:.1f} dB")
    (out / 'low.json').write_text(json.dumps(rows, indent=1))

if __name__ == '__main__':
    {'gen': gen, 'analyze': analyze}[sys.argv[1]]()
