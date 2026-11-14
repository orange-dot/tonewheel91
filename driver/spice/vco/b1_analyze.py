#!/usr/bin/env python3
"""B1 analysis: B1-THT in SPICE against the Mamut Analog oscillator.
Reads <out>/spice/cv-*/trace.tsv (uniform 1.92 MHz), renders the MA reference at
the measured frequency and pulse duty with build/vco_ma_ref, and writes
summary.json and temp.json."""
import json, pathlib, subprocess, sys
import numpy as np

import paths
here = paths.out('b1')
FS_SPICE, FS = 1.92e6, 48000.0
TOP_HZ = 20000.0

def bh4(n):
    k = np.arange(n) / n
    return .35875 - .48829*np.cos(2*np.pi*k) + .14128*np.cos(4*np.pi*k) - .01168*np.cos(6*np.pi*k)

def harmonics(y, fs, f0, top=TOP_HZ):
    """Complex amplitudes c[k], k = 0..K, of y ~ sum Re(c[k] exp(j 2 pi k f0 t)), by a
    Blackman-Harris weighted projection. Needs at least 8 cycles in the window."""
    n = len(y)
    assert n * f0 / fs >= 8.0, 'window shorter than 8 cycles'
    w = bh4(n)
    t = np.arange(n) / fs
    K = int(top // f0)
    c = np.zeros(K + 1, complex)
    yw = (y - y.mean()) * w
    for k in range(1, K + 1):
        c[k] = 2 * np.sum(yw * np.exp(-2j*np.pi*k*f0*t)) / w.sum()
    return c

def crossings(y, fs, level):
    i = np.nonzero((y[:-1] < level) & (y[1:] >= level))[0]
    return (i + (level - y[i]) / (y[i+1] - y[i])) / fs

def refine_f0(y, fs, f0):
    """Refine f0 from the fundamental's phase advance between the two halves."""
    for _ in range(4):
        h = len(y) // 2
        t = np.arange(h) / fs
        w = bh4(h)
        e = np.exp(-2j*np.pi*f0*t)
        a = np.sum((y[:h] - y[:h].mean()) * w * e)
        b = np.sum((y[h:2*h] - y[h:2*h].mean()) * w * e)
        f0 += np.angle(b / a * np.exp(-2j*np.pi*f0*h/fs)) / (2*np.pi*h/fs)
    return f0

def align(a, b):
    """b scaled and time-shifted onto a, both chosen for the least difference over all
    harmonics (a shift taken from the fundamental's phase alone turns a slight
    high-pass, such as the output coupling, into a large error at the top harmonics).
    Returns the difference's power in dB relative to a, and the moved b."""
    K = min(len(a), len(b)) - 1
    k = np.arange(K + 1)
    a, b = a[:K+1], b[:K+1]
    def moved(phi):
        bb = b * np.exp(-1j * k * phi)
        g = np.sum((a[1:] * np.conj(bb[1:])).real) / np.sum(abs(bb[1:])**2)
        return g * bb
    def err(phi): return np.sum(abs(a[1:] - moved(phi)[1:])**2)
    phi = np.angle(b[1]) - np.angle(a[1])
    span = np.pi / K
    for _ in range(6):                      # shrinking grid around the best shift
        grid = phi + np.linspace(-span, span, 41)
        phi = grid[np.argmin([err(x) for x in grid])]
        span /= 10
    return 10*np.log10(err(phi) / np.sum(abs(a[1:])**2)), moved(phi)

def db(x): return 20*np.log10(np.maximum(abs(x), 1e-12))

def ideal_triangle(K):
    c = np.zeros(K + 1, complex)
    for k in range(1, K + 1, 2): c[k] = (8/np.pi**2) / k**2 * (1 if (k//2) % 2 == 0 else -1) * -1j
    return c

def ideal_pulse(K, d):
    k = np.arange(K + 1)
    c = np.zeros(K + 1, complex)
    c[1:] = 4/(np.pi*k[1:]) * np.sin(np.pi*k[1:]*d) * np.exp(-1j*np.pi*k[1:]*d)
    return c

def shape(tri, fs):
    """The raw capacitor triangle as two straight ramps: slopes from the central 60 %
    of each ramp, vertices where the lines meet, and what the lines leave over."""
    lo, hi = tri.min(), tri.max()
    mid = .5 * (lo + hi)
    up = crossings(tri, fs, mid)
    dn = crossings(-tri, fs, -mid)
    dn = dn[dn > up[0]]
    n = min(len(up) - 1, len(dn))
    t = np.arange(len(tri)) / fs
    su, sd, vhi, vlo, tu = [], [], [], [], []
    # each ramp is centred on a mid crossing: up ramps at up[i], down ramps at dn[i]
    def fit(tc):
        half = .3 * (hi - lo)
        s0 = (hi - lo) * 2 / (up[1] - up[0])           # rough slope magnitude
        w = half / s0
        m = (t > tc - w) & (t < tc + w)
        p = np.polyfit(t[m] - tc, tri[m], 1)
        return p                                        # slope, value at tc
    ups = [fit(tc) for tc in up[:n+1]]
    dns = [fit(tc) for tc in dn[:n]]
    model = np.full(len(tri), np.nan)
    for i in range(n):
        # top vertex between up[i] and dn[i]; bottom vertex between dn[i] and up[i+1]
        (a1, b1), (a2, b2) = ups[i], dns[i]
        tt = (b2 - b1 + a1*up[i] - a2*dn[i]) / (a1 - a2)
        vt = b1 + a1 * (tt - up[i])
        (a3, b3) = ups[i+1]
        tb = (b3 - b2 + a2*dn[i] - a3*up[i+1]) / (a2 - a3)
        vb = b2 + a2 * (tb - dn[i])
        m = (t >= up[i]) & (t < tt); model[m] = b1 + a1 * (t[m] - up[i])
        m = (t >= tt) & (t < tb);    model[m] = b2 + a2 * (t[m] - dn[i])
        m = (t >= tb) & (t < up[i+1]); model[m] = b3 + a3 * (t[m] - up[i+1])
        su.append(a1); sd.append(a2); vhi.append(vt); vlo.append(vb)
        if i: tu.append((tt - tb_prev) / (up[i] - up[i-1]))
        tb_prev = tb
    ok = ~np.isnan(model)
    ac = tri[ok] - tri[ok].mean()
    r = tri[ok] - model[ok]
    return dict(slope_up=float(np.mean(su)), slope_down=float(np.mean(sd)),
                peak_hi=float(hi), peak_lo=float(lo), vertex_hi=float(np.mean(vhi)),
                vertex_lo=float(np.mean(vlo)), rise_fraction=float(np.mean(tu)),
                pwl_residual_db=float(10*np.log10(np.mean(r**2) / np.mean(ac**2))),
                pwl_residual_peak_mv=float(1e3 * np.abs(r).max()))

def load(case):
    d = np.loadtxt(here / 'spice' / case / 'trace.tsv', skiprows=1)
    names = (here / 'spice' / case / 'trace.tsv').open().readline().split()
    return {n: d[:, i] for i, n in enumerate(names)}

def ma(shape_name, width, hz, seconds=2.0):
    (here / 'ma').mkdir(exist_ok=True)
    out = here / 'ma' / f'{shape_name}-{hz:.3f}.f32'
    subprocess.run([paths.MA_REF, shape_name, f'{width:.6f}', out, f'{hz:.6f}:{seconds}'], check=True)
    return np.fromfile(out, dtype='<f4').astype(float)[int(.5*FS):]

def temps():
    """Transistor temperature alone (reference ideal), CV 2 V."""
    out = []
    for t, case in ((10, 'temp-10'), (25, 'cv-2'), (40, 'temp-40')):
        if not (here / 'spice' / case / 'trace.tsv').exists(): return
        tri = load(case)['v(TRI)']
        x = crossings(tri, FS_SPICE, .5 * (tri.min() + tri.max()))
        s = shape(tri, FS_SPICE)
        out.append(dict(temp=t, f_hz=float(1 / np.diff(x).mean()), rise_fraction=s['rise_fraction'],
                        swing=s['vertex_hi'] - s['vertex_lo']))
    (here / 'temp.json').write_text(json.dumps(out, indent=1))

def main():
    cases = sorted((p.name for p in (here / 'spice').glob('cv-*') if (p / 'trace.tsv').exists()),
                   key=lambda s: float(s[3:]))
    out = []
    for case in cases:
        v = load(case)
        tri = v['v(TRI)']
        mid = .5 * (tri.min() + tri.max())
        x = crossings(tri, FS_SPICE, mid)
        per = np.diff(x)
        f0 = 1 / per.mean()
        r = dict(case=case, cv=float(case[3:]), f_hz=f0, period_cv=float(per.std() / per.mean()),
                 cycles=len(per), raw_vpp=float(tri.max() - tri.min()))
        r['shape'] = shape(tri, FS_SPICE)
        pw = v['v(PULSE_OUT)']
        pmid = .5 * (pw.min() + pw.max())
        pr = crossings(pw, FS_SPICE, pmid); pf = crossings(-pw, FS_SPICE, -pmid)
        pf = pf[pf > pr[0]]; n = min(len(pr) - 1, len(pf))
        r['pulse_duty'] = float(np.mean((pf[:n] - pr[:n]) / np.diff(pr)[:n]))   # whole cycles only
        # 10-90 % edge times of the pulse output
        span = pw.max() - pw.min()
        def edge(sig):
            a = crossings(sig, FS_SPICE, sig.min() + .1*span); b = crossings(sig, FS_SPICE, sig.min() + .9*span)
            b = b[b > a[0]]; n = min(len(a), len(b)); return float(np.median(b[:n] - a[:n]))
        r['pulse_rise_us'] = 1e6 * edge(pw); r['pulse_fall_us'] = 1e6 * edge(-pw)
        K = int(TOP_HZ // f0)
        st = harmonics(v['v(TRI_OUT)'], FS_SPICE, f0)
        sp = harmonics(pw, FS_SPICE, f0)
        mt_y = ma('tri', .5, f0); mp_y = ma('pulse', r['pulse_duty'], f0)
        fm = refine_f0(mt_y, FS, f0)
        r['ma_f_hz'] = fm; r['ma_cents'] = float(1200*np.log2(fm / f0))
        mt = harmonics(mt_y, FS, fm); mp = harmonics(mp_y, FS, refine_f0(mp_y, FS, f0))
        it, ip = ideal_triangle(K), ideal_pulse(K, r['pulse_duty'])
        r['tri'] = dict(null_vs_ma_db=float(align(st, mt)[0]), null_vs_ideal_db=float(align(st, it)[0]),
                        ma_vs_ideal_db=float(align(it, mt)[0]),
                        spice_dbc=[float(x) for x in db(st[1:]) - db(st[1])],
                        ma_dbc=[float(x) for x in db(mt[1:]) - db(mt[1])],
                        ideal_dbc=[float(x) for x in db(it[1:]) - db(it[1])],
                        even_db=float(10*np.log10(np.sum(abs(st[2::2])**2) / np.sum(abs(st[1:])**2))))
        r['pulse'] = dict(null_vs_ma_db=float(align(sp, mp)[0]), null_vs_ideal_db=float(align(sp, ip)[0]),
                          ma_vs_ideal_db=float(align(ip, mp)[0]),
                          spice_dbc=[float(x) for x in db(sp[1:]) - db(sp[1])],
                          ma_dbc=[float(x) for x in db(mp[1:]) - db(mp[1])],
                          ideal_dbc=[float(x) for x in db(ip[1:]) - db(ip[1])])
        np.save(here / 'spice' / case / 'harm_tri.npy', st); np.save(here / 'spice' / case / 'harm_pulse.npy', sp)
        out.append(r)
        print(f"{case}: f {f0:.3f} Hz, period cv {r['period_cv']:.1e}, tri null vs MA {r['tri']['null_vs_ma_db']:.1f} dB, "
              f"vs ideal {r['tri']['null_vs_ideal_db']:.1f} dB, pulse null vs MA {r['pulse']['null_vs_ma_db']:.1f} dB",
              flush=True)
    (here / 'summary.json').write_text(json.dumps(out, indent=1))
    temps()

if __name__ == '__main__':
    main()
