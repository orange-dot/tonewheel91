#!/usr/bin/env python3
"""VCO-2 analysis for board A: the saw's shape and harmonics against the ideal saw
and the MA saw, the reset edge at 20 ns, and a four-number pitch law fitted to the
tracking curve. Writes shape-A.json."""
import json, pathlib, subprocess, sys
import numpy as np
import vco2
import paths
import b1_analyze as A1       # harmonics, align, refine_f0, db
here = vco2.OUT

FS = 48000.0
cal = json.loads((here / 'cal-A.json').read_text())['params']

def ideal_saw(K):
    k = np.arange(K + 1); c = np.zeros(K + 1, complex)
    c[1:] = 2j / (np.pi * k[1:])            # rising saw, -1..1, wrapping at t = 0... sign irrelevant after align
    return c

def ma_saw(hz):
    out = here / 'ma'; out.mkdir(exist_ok=True)
    f = out / f'saw-{hz:.3f}.f32'
    subprocess.run([paths.MA_REF, 'saw', '0.5', f, f'{hz:.6f}:2.0'], check=True)
    return np.fromfile(f, dtype='<f4').astype(float)[int(.5*FS):]

def ramp(saw, f0):
    """Straight-ramp fit of the falling part, cycle by cycle: slope, and what a
    straight ramp between the cycle's own top and bottom leaves over."""
    fs = vco2.FS_SPICE
    mid = .5 * (saw.min() + saw.max())
    x = vco2.crossings(saw, mid)            # reset edges
    t = np.arange(len(saw)) / fs
    res, ac, slopes, bow = [], [], [], []
    for a, b in zip(x[:-1], x[1:]):
        m = (t > a + 6e-6) & (t < b - 3e-6)
        tt, y = t[m] - a, saw[m]
        p = np.polyfit(tt, y, 1)
        r = y - np.polyval(p, tt)
        res.append(np.mean(r**2)); ac.append(np.var(y)); slopes.append(p[0])
        bow.append(r[len(r)//2] - .5 * (r[0] + r[-1]))
    return dict(slope=float(np.mean(slopes)), straight_residual_db=float(10*np.log10(np.mean(res) / np.mean(ac))),
                bow_mv=float(1e3 * np.mean(bow)))

def shapes():
    out = []
    for note in (36, 60, 84, 108):
        (f0, cvp, n), v = vco2.measure(f'A-track-n{note}', vco2.note_cv(note), guess=vco2.note_hz(note), **cal)
        y = -v['v(BUF)']                     # rising saw, like MA's
        K = int(20000 // f0)
        s = A1.harmonics(y, vco2.FS_SPICE, f0)
        my = ma_saw(f0); fm = A1.refine_f0(my, FS, f0); m = A1.harmonics(my, FS, fm)
        i = ideal_saw(K)
        r = dict(note=note, f_hz=f0, **ramp(v['v(SAW)'], f0),
                 null_vs_ma_db=float(A1.align(s, m)[0]), null_vs_ideal_db=float(A1.align(s, i)[0]),
                 ma_vs_ideal_db=float(A1.align(i, m)[0]),
                 spice_dbc=[float(x) for x in A1.db(s[1:]) - A1.db(s[1])],
                 ma_dbc=[float(x) for x in A1.db(m[1:]) - A1.db(m[1])],
                 ideal_dbc=[float(x) for x in A1.db(i[1:]) - A1.db(i[1])])
        np.save(here / f'harm-A-n{note}.npy', s)
        out.append(r)
        print(f"note {note} {f0:.2f} Hz: SPICE-ideal {r['null_vs_ideal_db']:.1f} dB, SPICE-MA {r['null_vs_ma_db']:.1f}, MA-ideal {r['ma_vs_ideal_db']:.1f}, "
              f"straight ramp {r['straight_residual_db']:.1f} dB, bow {r['bow_mv']:.1f} mV", flush=True)
    return out

def reset_edge():
    T = 1 / 440.0
    grid = 20e-9
    v = vco2.sim('A-reset-a4', vco2.note_cv(69), seconds=2.2 * T, settle=3 * T, maxstep=10e-9, grid=grid, **cal)
    s, q = v['v(SAW)'], v['v(SQUARE)']
    fs = 1 / grid
    up = vco2.crossings(q, 6.0, fs); dn = vco2.crossings(-q, -6.0, fs); dn = dn[dn > up[0]]
    i0 = int(up[0] * fs) - int(3e-6 * fs)
    seg = s[i0:i0 + int(20e-6 * fs)]
    lo, hi = seg.min(), s.max()
    tmin = np.argmin(seg) / fs
    def reach(frac): return (np.nonzero(seg >= lo + frac * (hi - lo))[0][0]) / fs - tmin
    r = dict(square_pulse_us=float(1e6 * (dn[0] - up[0])), bottom=float(lo), top=float(hi),
             t10_us=float(1e6 * reach(.1)), t90_us=float(1e6 * reach(.9)), t99_us=float(1e6 * reach(.99)),
             overshoot_below_threshold_mv=None)
    np.save(here / 'reset-A.npy', np.stack([np.arange(len(seg)) / fs - tmin, seg, q[i0:i0 + len(seg)]]))
    print('reset', r, flush=True)
    return r

def pitch_law(track):
    """1/f = 1/(2^(a + s cv) + b) + td: an exponential current, a constant extra
    current b (Hz) and a dead time td per cycle. Gauss-Newton on log2 f."""
    cv = np.array([r['cv'] for r in track]); f = np.array([r['f_hz'] for r in track])
    def model(p):
        a, s, b, td = p
        return 1 / (1 / (2 ** (a + s * cv) + b) + td)
    p = np.array([np.log2(vco2.C0), 1.0, 0.0, 0.0])
    for _ in range(60):
        r = np.log2(model(p)) - np.log2(f)
        J = np.empty((len(cv), 4))
        for j, h in enumerate((1e-6, 1e-6, 1e-5, 1e-9)):
            d = p.copy(); d[j] += h
            J[:, j] = (np.log2(model(d)) - np.log2(model(p))) / h
        step = np.linalg.lstsq(J, -r, rcond=None)[0]
        p = p + step
        if np.max(abs(step * np.array([1, 1, 1, 1e6]))) < 1e-9: break
    res = 1200 * (np.log2(model(p)) - np.log2(f))
    return dict(a=float(p[0]), scale=float(p[1]), extra_hz=float(p[2]), dead_time_us=float(1e6 * p[3]),
                residual_cents=[float(x) for x in res])

if __name__ == '__main__':
    out = dict(shapes=shapes(), reset=reset_edge())
    for b in ('A', 'B'):
        t = here / f'track-{b}.json'
        if t.exists():
            out[f'law_{b}'] = pitch_law(json.loads(t.read_text()))
            print(b, {k: v for k, v in out[f'law_{b}'].items() if k != 'residual_cents'},
                  'residual', np.round(out[f'law_{b}']['residual_cents'], 2), flush=True)
    (here / 'shape-A.json').write_text(json.dumps(out, indent=1))
