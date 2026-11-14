#!/usr/bin/env python3
"""V3: the two shapers B1 lacks (shapers.cir) on B1's buffered triangle.
  shapers.py static        DC transfer of both stages            <out>/static.json
  shapers.py trim          the technician's trim at A4            <out>/trim.json
  shapers.py bench         B1 + both shapers, steady CVs          <out>/spice/cv*/bench.cir
                           (then b1_run.sh <out>)
  shapers.py analyze       harmonics, steps, static model check   <out>/shapers.json
<out> is build/vco/shapers (or $VCO_OUT_SHAPERS)."""
import json, subprocess, sys
import numpy as np
import paths
from b1_analyze import crossings, harmonics, refine_f0, bh4

out = paths.out('shapers')
FS = 1.92e6
RE_SIN = 47.0              # sine pair degeneration, chosen in `static` (lowest trimmed THD)
RA = 10e3                  # sine drive: 10k series, rb to ground (the trimmer)
CV_A4 = 440.0 / 383.7 - 0.086
CASES = ['-0.04', '0', '0.08', '0.25', '0.5', f'{CV_A4:.4f}', '2', '3', '5']
MODELS = [f'.include "{paths.SRC}/models/{m}"' for m in ('bc547c.lib', 'bc557c.lib', 'support.lib')]

def spice(path, lines):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n')
    r = subprocess.run(['ngspice', '-b', path.name], cwd=path.parent, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stdout + r.stderr)

def static_curves(re_sin):
    d = out / 'static' / f're{re_sin:g}'
    spice(d / 'dc.cir', ['* shaper DC transfer'] + MODELS + [
        f'.param re_sin={max(re_sin, 1e-3)} vref=0', '.temp 25', f'.include "{paths.SRC}/shapers.cir"',
        'VSB SIN_B 0 0', 'VSR SIN_R 0 0', 'VWB W_B 0 0', 'VWSW W_SW 0 6.35',
        '.options reltol=1e-6 method=gear', '.control', 'set wr_singlescale', 'set numdgt=12',
        'dc VSB -0.6 0.6 0.5m', 'wrdata sine.tsv v(SINE)',
        'dc VWB -3.2 0.4 1m', 'wrdata sawhi.tsv v(W_ON)-v(W_OP)',
        'alter VWSW dc = 5.65', 'dc VWB -3.2 0.4 1m', 'wrdata sawlo.tsv v(W_ON)-v(W_OP)',
        'quit', '.endc', '.end'])
    load = lambda n: np.loadtxt(d / n)
    return load('sine.tsv'), load('sawhi.tsv'), load('sawlo.tsv')

def thd(y, fs, f0):
    c = harmonics(y, fs, f0, top=12 * f0)
    return c, float(10 * np.log10(np.sum(abs(c[2:])**2) / abs(c[1])**2))

def b1_case(name, cv, vref, rb, vofs, periods=None, vecs='v(TRI) v(TBUF) v(SQUARE) v(SINE) v(W_ON) v(W_OP)',
            range_div=1):
    """range_div > 1: B1 with its timing capacitor divided (a range switch,
    V2b). The sine's coupling capacitor is then 1 uF (14 Hz corner, nothing
    at these pitches) so the bench settles in 60 ms, and the step is fine
    enough for the shorter period."""
    hz = 383.7 * (float(cv) + 0.086) * range_div
    periods = periods or (14 if hz < 200 else 40)
    extra = '|'.join([f'.param re_sin={RE_SIN} vref={vref:.6f}', f'.include "{paths.SRC}/shapers.cir"',
                      'RWIN TBUF W_B 10k',
                      'XQWB VSH SQUARE W_SQE BC547C', 'RWQE W_SQE 0 10k',
                      'RWQ1 W_SQE W_SW 47k', 'RWQ2 W_SW W_V0 4.7k', 'VWV0 W_V0 0 6.245',
                      f"CSIN TBUF SIN_A {'1u' if range_div > 1 else '10u'}", f'RSA SIN_A SIN_B {RA:g}',
                      f'RSB SIN_B 0 {rb:.4f}',
                      f'VSR SIN_R 0 {vofs:.6f}'])
    path = out / 'spice' / name / 'bench.cir'
    settle = 0.4 if range_div == 1 else 0.06
    fine = [] if range_div == 1 else [f'ct={22 / range_div:.6g}n', 'grid=65.1041667n',
                                      f'maxstep={min(0.5e-6, 1 / hz / 400):.4g}']
    subprocess.run([sys.executable, str(paths.SRC / 'b1_gen.py'), str(path), f'cv={cv}',
                    f'tstop={settle + periods / hz:.5f}', f'tstart={settle}', f'vecs={vecs}',
                    f'extra={extra}'] + fine, check=True)
    return path

def load(name):
    p = out / 'spice' / name / 'trace.tsv'
    names = p.open().readline().split()
    d = np.loadtxt(p, skiprows=1)
    return {n: d[:, i] for i, n in enumerate(names)}

def run(path):
    r = subprocess.run(['ngspice', '-b', 'bench.cir'], cwd=path.parent, capture_output=True, text=True)
    (path.parent / 'ngspice.log').write_text(r.stdout + r.stderr)

def f0_of(tri):
    up = crossings(tri, FS, .5 * (tri.min() + tri.max()))
    return refine_f0(tri, FS, (len(up) - 1) / (up[-1] - up[0]))

def vertex_tbuf(t):
    """TBUF where the square falls: the top vertex, where the saw's quad swaps."""
    sq = t['v(SQUARE)']
    x = crossings(-sq, FS, -.5 * (sq.min() + sq.max()))
    i = np.round(x * FS).astype(int)
    i = i[(i > 0) & (i < len(sq))]
    return float(np.mean(t['v(TBUF)'][i]))

def mid_step(t, f0):
    """The saw's step at the top vertex: the level just after the swap minus the
    line through the ramp before it, both from straight fits 2..12 % of a period away."""
    saw = t['v(W_ON)'] - t['v(W_OP)']
    sq = t['v(SQUARE)']
    x = crossings(-sq, FS, -.5 * (sq.min() + sq.max()))
    tt = np.arange(len(saw)) / FS
    T = 1 / f0
    steps = []
    for xc in x[1:-1]:
        b = (tt > xc - .12*T) & (tt < xc - .02*T)
        a = (tt > xc + .02*T) & (tt < xc + .12*T)
        pb, pa = np.polyfit(tt[b] - xc, saw[b], 1), np.polyfit(tt[a] - xc, saw[a], 1)
        steps.append(pa[1] - pb[1])
    return float(np.mean(steps)), float(saw.max() - saw.min())

def static():
    """Static curves for four degenerations; for each, the trimmed THD on B1's A4
    triangle (TBUF, from a plain B1 run). The lowest wins (RE_SIN above)."""
    p = b1_case('a4-plain', f'{CV_A4:.4f}', 0.0, 500.0, 0.0, vecs='v(TBUF) v(SQUARE)')
    if not (p.parent / 'trace.tsv').exists(): run(p)
    t = load('a4-plain')
    tac = t['v(TBUF)'] - t['v(TBUF)'].mean()
    f0 = f0_of(t['v(TBUF)'])
    res = {}
    for re in (0.0, 22.0, 47.0, 100.0):
        sine, hi, lo = static_curves(re)
        best = trim_sine(sine, tac, f0)
        res[re] = best
        print(f're {re:5g}  k {best["k"]:.5f}  vofs {1e3*best["vofs"]:+.3f} mV  THD {best["thd_db"]:.1f} dB')
    (out / 'static.json').write_text(json.dumps({'f0': f0, 'trims': {str(k): v for k, v in res.items()}}, indent=1))

def sine_model(sine, v):
    return np.interp(v, sine[:, 0], sine[:, 1])

def trim_sine(sine, tac, f0):
    """Drive k (the trimmer) for the lowest odd distortion, then the offset for the
    lowest even, twice: a technician on a distortion meter."""
    k, vofs = 0.05, 0.0
    def d(k, vofs, which):
        c, _ = thd(sine_model(sine, k * tac - vofs), FS, f0)
        h = abs(c[2:]) ** 2
        odd, even = h[1::2].sum(), h[0::2].sum()
        return {'odd': odd, 'even': even, 'all': h.sum()}[which] / abs(c[1])**2
    for _ in range(3):
        ks = np.linspace(.3 * k, 1.7 * k, 81); k = ks[np.argmin([d(x, vofs, 'odd') for x in ks])]
        for span in (.02, .004, .0008):
            ks = np.linspace(k - span * k * 10, k + span * k * 10, 41)
            k = ks[np.argmin([d(x, vofs, 'odd') for x in ks])]
        vs = np.linspace(vofs - .01, vofs + .01, 81); vofs = vs[np.argmin([d(k, x, 'even') for x in vs])]
        vs = np.linspace(vofs - .0005, vofs + .0005, 81); vofs = vs[np.argmin([d(k, x, 'even') for x in vs])]
    return {'k': float(k), 'vofs': float(vofs), 'thd_db': float(10 * np.log10(d(k, vofs, 'all')))}

def trim():
    """Sine: the static trim on A4's triangle as resistor and offset, then the
    offset again in place (its second harmonic is linear in it). Saw: the
    reference at A4's top vertex, corrected on the measured step until it is
    under 0.1 mV (a scope at the swap)."""
    s = json.loads((out / 'static.json').read_text())['trims'][str(RE_SIN)]
    rb = s['k'] * RA / (1 - s['k'])
    vref = vertex_tbuf(load('a4-plain'))
    vofs, h2 = s['vofs'], []
    for i in range(8):
        name = f'a4-trim{i}'
        p = b1_case(name, f'{CV_A4:.4f}', vref, rb, vofs)
        if not (p.parent / 'trace.tsv').exists(): run(p)
        t = load(name)
        f0 = f0_of(t['v(TRI)'])
        step, pp = mid_step(t, f0)
        sine = t['v(SINE)']
        c, th = thd(sine - sine.mean(), FS, f0)
        h2.append((vofs, c[2] / c[1]))
        print(f'trim {i}: vref {vref:.6f} V  step {1e3*step:+.3f} mV of {pp:.3f} V  '
              f'vofs {1e3*vofs:+.3f} mV  H2 {20*np.log10(abs(c[2]/c[1])):.1f} dB  THD {th:.1f} dB',
              flush=True)
        done_sine = len(h2) >= 3 and abs(h2[-1][0] - h2[-2][0]) < 2e-6
        if abs(step) < 1e-4 and done_sine: break
        # step = 2 g (vref - top), g the stage's gain (1.207 in `static`)
        vref -= step / (2 * 1.2068)
        if len(h2) == 1:
            vofs += 5e-4
        else:
            (v0, a0), (v1, a1) = h2[-2], h2[-1]
            b = (a1 - a0) / (v1 - v0)
            vofs = v1 - (np.conj(b) * a1).real / abs(b)**2
    (out / 'trim.json').write_text(json.dumps(dict(re_sin=RE_SIN, rb=rb, vofs=vofs, k=s['k'],
                                                   vref=vref, step_mv=1e3*step, thd_db=th,
                                                   case=name), indent=1))

def fit(vofs=None, k=None):
    """The core's curves. Sine: the static curve under the trim, on the
    triangle normalized to +-1 at A4's TBUF vertices, scaled to peaks of +-1;
    fitted as x * odd(x^2) + even(x^2), six terms each, over +-1.1. Saw: the
    static stage is linear to 2.7 mV in 3.2 V (-61 dB), so the core takes it
    as linear and only the triangle and the trim shape it."""
    tr = json.loads((out / 'trim.json').read_text()) if (out / 'trim.json').exists() else {}
    st = json.loads((out / 'static.json').read_text())['trims'][str(RE_SIN)]
    k = k or tr.get('k', st['k']); vofs = tr.get('vofs', st['vofs']) if vofs is None else vofs
    t = load('a4-plain')
    tb = t['v(TBUF)']
    sq = t['v(SQUARE)']
    top = vertex_tbuf(t)
    x = crossings(sq, FS, .5 * (sq.min() + sq.max()))     # rising square: bottom vertex
    i = np.round(x * FS).astype(int); i = i[(i > 0) & (i < len(tb))]
    bottom = float(np.mean(tb[i]))
    amp, mid = .5 * (top - bottom), .5 * (top + bottom)
    sine, _, _ = static_curves(RE_SIN)
    curve = lambda xx: sine_model(sine, k * amp * xx - vofs)
    g = .5 * (curve(1.0) - curve(-1.0)); c0 = .5 * (curve(1.0) + curve(-1.0))
    xx = np.linspace(-1.1, 1.1, 2201)
    y = (curve(xx) - c0) / g
    n = 6
    A = np.concatenate([np.stack([xx * xx**(2*j) for j in range(n)], 1),
                        np.stack([xx**(2*j) for j in range(n)], 1)], 1)
    coef, *_ = np.linalg.lstsq(A, y, rcond=None)
    err = A @ coef - y
    res = dict(k=k, vofs=vofs, tbuf_top=top, tbuf_bottom=bottom, tbuf_amp=amp, tbuf_mid=mid,
               sine_odd=coef[:n].tolist(), sine_even=coef[n:].tolist(),
               sine_fit_max_err=float(abs(err).max()), sine_fit_err_in_range=float(abs(err[abs(xx) <= 1]).max()))
    (out / 'fit.json').write_text(json.dumps(res, indent=1))
    print(json.dumps(res, indent=1))
    return res

def bench():
    tr = json.loads((out / 'trim.json').read_text())
    for cv in CASES:
        b1_case(f'cv{cv}', cv, tr['vref'], tr['rb'], tr['vofs'])

HIGH = [('r8-cv0.5', 8, '0.5'), ('r8-cv1.2', 8, '1.2'), ('r8-cv2', 8, '2'), ('r8-cv3', 8, '3'),
        ('r8-cv4', 8, '4'), ('r8-cv5', 8, '5'), ('r16-cv3.3', 16, '3.3')]

def bench_high():
    """V2b: B1 with a range switch (C3 / 8, C3 / 16), both shapers, the A4 trim
    unchanged, 1.8 to 20 kHz."""
    tr = json.loads((out / 'trim.json').read_text())
    for name, div, cv in HIGH:
        b1_case(name, cv, tr['vref'], tr['rb'], tr['vofs'], range_div=div)

def analyze(names=None, result='shapers.json'):
    """Per case: B1's rise and TBUF swing; the sine's THD, H2, H3, and the same
    from the static curve on that case's TBUF (what the core does); the saw's
    step at the top vertex relative to half its span, and the same from the
    vertex alone (2 (vref - top) g over the span)."""
    global FS
    tr = json.loads((out / 'trim.json').read_text())
    sine_dc, _, _ = static_curves(RE_SIN)
    rows = []
    for name in names or [f'cv{cv}' for cv in CASES]:
        t = load(name)
        FS = 1 / (t['time'][1] - t['time'][0])
        cv = name
        tri = t['v(TRI)']
        f0 = f0_of(tri)
        from b1_analyze import shape
        rise = shape(tri, FS)['rise_fraction']
        top = vertex_tbuf(t)
        sine = t['v(SINE)']
        c, th = thd(sine - sine.mean(), FS, f0)
        tac = t['v(TBUF)'] - t['v(TBUF)'].mean()
        cm, thm = thd(sine_model(sine_dc, tr['k'] * tac - tr['vofs']), FS, f0)
        step, pp = mid_step(t, f0)
        r = dict(case=name, hz=f0, rise=rise, tbuf_top=top,
                 sine_thd=th, sine_h2=20*np.log10(abs(c[2]/c[1])), sine_h3=20*np.log10(abs(c[3]/c[1])),
                 static_thd=thm, static_h2=20*np.log10(abs(cm[2]/cm[1])), static_h3=20*np.log10(abs(cm[3]/cm[1])),
                 saw_step=step / (.5 * pp), saw_pp=pp, sine_h1=float(abs(c[1])),
                 saw_h1=float(abs(harmonics(t['v(W_ON)'] - t['v(W_OP)'], FS, f0, top=1.5 * f0)[1])),
                 swing=shape(tri, FS)['vertex_hi'] - shape(tri, FS)['vertex_lo'])
        rows.append(r)
        print(f"cv {cv:>7} {f0:9.2f} Hz rise {rise:.5f}  sine THD {th:6.1f} (static {thm:6.1f}) "
              f"H2 {r['sine_h2']:6.1f} ({r['static_h2']:6.1f}) H3 {r['sine_h3']:6.1f} ({r['static_h3']:6.1f})  "
              f"saw step {r['saw_step']:+.5f}  swing {r['swing']:.4f}  H1 sine {r['sine_h1']:.3f} saw {r['saw_h1']:.3f}",
              flush=True)
    (out / result).write_text(json.dumps(rows, indent=1))

def analyze_high():
    analyze([n for n, _, _ in HIGH], 'high.json')

if __name__ == '__main__':
    {'static': static, 'trim': trim, 'fit': fit, 'bench': bench, 'analyze': analyze,
     'bench_high': bench_high, 'analyze_high': analyze_high}[sys.argv[1]]()
