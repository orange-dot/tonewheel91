#!/usr/bin/env python3
"""VCO-2 bench helper: run vco2.cir in ngspice and read the traces (<out>/runs/).
sim(name, cv=..., seconds=..., settle=..., **params) -> dict of arrays at 1.92 MHz."""
import pathlib, subprocess, json
import numpy as np

import paths
here = paths.SRC; OUT = paths.out('vco2')
FS_SPICE = 1.92e6
DEFAULTS = dict(rscale=1800.0, vtune=-8.55, tcs=0.0, ct=3.3e-9, rlim=100.0, hfe=600, early=100,
                is2=1.0, dt2=0.0, temp=25.0)
C0 = 16.351597831287414

def note_cv(note): return (note - 12) / 12.0
def note_hz(note): return 440.0 * 2 ** ((note - 69) / 12)

def bench(folder, cv, seconds, settle, maxstep, vecs, binary, p, grid=1/FS_SPICE):
    lines = ['* VCO-2 bench', f'.temp {p["temp"]}']
    lines += [f'.include "{here}/models/{m}"' for m in ('bc547c.lib', 'bc557c.lib')]
    lines += ['.param ' + ' '.join(f'{k}={v:.12g}' for k, v in p.items() if k != 'temp')]
    lines += [f'.include "{here}/vco2.cir"']
    lines += [f'VCV CV 0 PWL({cv})' if isinstance(cv, str) else f'VCV CV 0 {cv:.9g}']
    lines += ['.options reltol=1e-5 vntol=1e-8 abstol=1e-13 method=gear', '.control',
              'set wr_singlescale', 'set wr_vecnames', f'save {vecs}',
              f'tran {grid:.10g} {settle + seconds:.9g} {settle:.9g} {maxstep:.6g}',
              f'linearize {vecs}']
    lines += (['set filetype=binary', f'write trace.raw {vecs}'] if binary else
              ['set numdgt=12', f'wrdata trace.tsv {vecs}'])
    lines += ['rusage time', 'quit', '.endc', '.end']
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'bench.cir').write_text('\n'.join(lines) + '\n')

def read_raw(path):
    with open(path, 'rb') as f:
        head = b''
        while not head.endswith(b'Binary:\n'):
            head += f.read(1)
        text = head.decode()
        nv = int(text.split('No. Variables:')[1].split()[0]); n = int(text.split('No. Points:')[1].split()[0])
        return np.fromfile(f, dtype='<f8', count=nv*n).reshape(n, nv)

def sim(name, cv, seconds, settle, maxstep=0.5e-6, vecs='v(SAW) v(BUF) v(SQUARE)', binary=False, reuse=True,
        grid=1/FS_SPICE, **params):
    p = dict(DEFAULTS); p.update(params)
    folder = OUT / 'runs' / name
    tag = json.dumps([cv, seconds, settle, maxstep, vecs, binary, p, (here / 'vco2.cir').read_text()], sort_keys=True)
    if grid != 1/FS_SPICE: tag += f' grid={grid!r}'
    out = folder / ('trace.raw' if binary else 'trace.tsv')
    if not (reuse and out.exists() and (folder / 'tag.json').exists() and (folder / 'tag.json').read_text() == tag):
        bench(folder, cv, seconds, settle, maxstep, vecs, binary, p, grid)
        r = subprocess.run(['ngspice', '-b', 'bench.cir'], cwd=folder, capture_output=True, text=True)
        (folder / 'ngspice.log').write_text(r.stdout + r.stderr)
        if not out.exists():
            raise RuntimeError(f'{name}: ngspice produced no trace; see {folder}/ngspice.log')
        (folder / 'tag.json').write_text(tag)
    names = vecs.split()
    d = read_raw(out) if binary else np.loadtxt(out, skiprows=1)
    return {'t': d[:, 0], **{n: d[:, i + 1] for i, n in enumerate(names)}}

def crossings(y, level, fs=FS_SPICE):
    """Times of rising crossings of level."""
    i = np.nonzero((y[:-1] < level) & (y[1:] >= level))[0]
    return (i + (level - y[i]) / (y[i+1] - y[i])) / fs

def frequency(saw):
    """From the slow falling ramp through mid-swing (the reset edge is a microsecond
    long and the 1.92 MHz grid cannot time it)."""
    x = crossings(-saw, -.5 * (saw.min() + saw.max()))
    per = np.diff(x)
    return 1 / per.mean(), per.std() / per.mean(), len(per)

def measure(name, cv, cycles=8, guess=None, **params):
    """Frequency at a CV: settle three periods, measure `cycles`, step <= period/4000."""
    f = guess or C0 * 2 ** cv
    T = 1 / f
    v = sim(name, cv, seconds=(cycles + 1.5) * T, settle=3 * T, maxstep=min(.5e-6, T / 4000), **params)
    return frequency(v['v(SAW)']), v
