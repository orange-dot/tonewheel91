#!/usr/bin/env python3
"""B1 cases under <out>/spice/: the steady-state CV sweep, two temperatures and the
phrase with a stepped CV (per-note CV from a cubic through the 2026-10-16
frequencies). Then: b1_run.sh <out>, b1_analyze.py, b1_render.py, b1_report.py."""
import json, subprocess, sys
import numpy as np
import paths
out = paths.out('b1'); gen = [sys.executable, str(paths.SRC / 'b1_gen.py')]

for cv in ('0.5', '0.75', '1', '1.5', '2', '3', '4', '5'):
    subprocess.run(gen + [str(out / 'spice' / f'cv-{cv}' / 'bench.cir'), f'cv={cv}', 'tstop=0.76', 'tstart=0.70'], check=True)
for t in ('10', '40'):
    subprocess.run(gen + [str(out / 'spice' / f'temp-{t}' / 'bench.cir'), 'cv=2', f'temp={t}', 'tstop=0.16', 'tstart=0.10'], check=True)

ref = {r['case']['id']: r['metrics'] for r in json.loads((paths.SRC / 'b1-results-20261016.json').read_text())['results']}
cv = np.array([.5, 1, 2, 3, 4, 5]); f = np.array([ref[f'cv-{c:g}']['frequency_hz'] for c in cv])
p = np.polyfit(cv, f, 3)
def cv_of(hz):
    r = np.roots(p - np.array([0, 0, 0, hz])); r = r[np.isreal(r)].real
    return float(r[(r > .3) & (r < 5.5)][0])
notes = [60, 63, 67, 72, 75, 79, 84, 79, 75, 74, 72, 67, 63, 62, 60]; dur = [.25] * 14 + [.9]
t, pwl, segs, prev = 0.7, [], [], None
for n, d in zip(notes, dur):
    hz = 440.0 * 2 ** ((n - 69) / 12); c = cv_of(hz)
    pwl += [0, c] if prev is None else [t, prev, t + 20e-6, c]
    segs.append(dict(note=n, hz=hz, cv=c, start=t, dur=d)); prev = c; t += d
pwl += [t, prev]
(out / 'phrase.json').write_text(json.dumps(dict(segments=segs, tstart=0.7, tstop=t), indent=1))
subprocess.run(gen + [str(out / 'spice' / 'phrase' / 'bench.cir'), 'pwl=' + ' '.join(f'{x:.9g}' for x in pwl),
                      f'tstop={t:.4f}', 'tstart=0.7', 'vecs=v(TRI_OUT) v(PULSE_OUT)', 'binary=1'], check=True)
