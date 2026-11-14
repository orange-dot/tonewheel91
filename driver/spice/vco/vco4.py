#!/usr/bin/env python3
"""V4: two VCO-2 cores (boards A and B, each trimmed as in step 2) on one
supply. The rails VCC (12 V) and VTOP (8 V) come from ideal sources through a
series resistance `rs` into a bus decoupled by `cb`; the CV, the pitch trim and
the scale trim stay ideal (a separate precision reference). Core B is detuned
by `detune` cents, so the phase difference walks once round in 1/beat seconds;
the rate it walks at, against the phase, is the coupling.
  vco4.py case NAME rs cb note detune seconds   -> <out>/runs/NAME/trace.raw
  vco4.py analyze NAME [NAME ...]               -> <out>/NAME.json
<out> is build/vco/vco4 (or $VCO_OUT_VCO4)."""
import json, re, subprocess, sys
import numpy as np
import paths, vco2

OUT = paths.out('vco4')
STEP2 = paths.ROOT / 'build' / 'vco-step2-20261112'
BOARDS = {'A': dict(), 'B': dict(hfe=450, early=80, ct=3.3e-9 * 1.01, rlim=110.0, is2=1.04)}
GLOBAL = {'0', 'VCC', 'VTOP'}
RATIOS = {0: (1, 1), 12: (2, 1), 7: (3, 2), -12: (1, 2)}

def trims(board):
    return json.loads((STEP2 / f'cal-{board}.json').read_text())['params']

def core(suffix, p):
    """vco2.cir as one core: its rails and trim source dropped, every node and
    element renamed with `suffix` except ground and the two rails, every
    parameter written in."""
    out = []
    for line in (paths.SRC / 'vco2.cir').read_text().splitlines():
        if not line.strip() or line.startswith('*') or line.split()[0] in ('VCC', 'VTOP'):
            continue
        line = re.sub(r'\{(\w+)\}', lambda m: f'{p[m.group(1)]:.12g}', line)
        f = line.split()
        name, rest = f[0], f[1:]
        if name[0] == 'X':
            nodes, tail = rest[:3], rest[3:]
        elif name[0] in 'RCV':
            nodes, tail = rest[:2], rest[2:]
        else:
            raise ValueError(line)
        nodes = [n if n in GLOBAL else f'{n}_{suffix}' for n in nodes]
        out.append(' '.join([f'{name}_{suffix}'] + nodes + tail))
    return out

def case(name, rs, cb, note, detune, seconds, settle=.05, interval=0):
    a = dict(vco2.DEFAULTS); a.update(BOARDS['A']); a.update(trims('A'))
    b = dict(vco2.DEFAULTS); b.update(BOARDS['B']); b.update(trims('B'))
    cv = vco2.note_cv(note)
    T = 1 / vco2.note_hz(note)
    vecs = 'v(SAW_A) v(SAW_B) v(VCC) v(VTOP)'
    lines = ['* V4: two VCO-2 cores on one supply', '.temp 25']
    lines += [f'.include "{paths.SRC}/models/{m}"' for m in ('bc547c.lib', 'bc557c.lib')]
    # the sources sit above the bus by the pair's mean draw (12.0 mA from VCC,
    # 21 uA from VTOP), so the bus is the 12 V and 8 V the boards were trimmed on
    lines += [f'VSUP SUP 0 {12 + 0.0120 * rs:.9g}', f'VSTOP STOP 0 {8 + 21e-6 * rs:.9g}',
              f'RSUP SUP VCC {rs:.6g}', f'RSTOP STOP VTOP {rs:.6g}',
              f'CBUS VCC 0 {cb:.6g}', f'CBTOP VTOP 0 {cb:.6g}']
    lines += core('A', a) + core('B', b)
    lines += [f'VCV_A CV_A 0 {cv:.9g}', f'VCV_B CV_B 0 {cv + interval / 12 + detune / 1200:.9g}']
    lines += ['.options reltol=1e-5 vntol=1e-8 abstol=1e-13 method=gear', '.control',
              'set wr_singlescale', 'set wr_vecnames', f'save {vecs}',
              f'tran {1 / vco2.FS_SPICE:.10g} {settle + seconds:.9g} {settle:.9g} {min(.5e-6, T / 4000):.6g}',
              f'linearize {vecs}', 'set filetype=binary', f'write trace.raw {vecs}',
              'rusage time', 'quit', '.endc', '.end']
    folder = OUT / 'runs' / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'bench.cir').write_text('\n'.join(lines) + '\n')
    (folder / 'case.json').write_text(json.dumps(dict(rs=rs, cb=cb, note=note, detune=detune, seconds=seconds,
                                                      interval=interval)))
    r = subprocess.run(['ngspice', '-b', 'bench.cir'], cwd=folder, capture_output=True, text=True)
    (folder / 'ngspice.log').write_text(r.stdout + r.stderr)

def resets(saw):
    """Each cycle's time: the falling ramp through mid-swing."""
    return vco2.crossings(-saw, -.5 * (saw.min() + saw.max()))

def analyze(name):
    folder = OUT / 'runs' / name
    c = json.loads((folder / 'case.json').read_text())
    d = vco2.read_raw(folder / 'trace.raw')
    sa, sb, vcc, vtop = d[:, 1], d[:, 2], d[:, 3], d[:, 4]
    ta, tb = resets(sa), resets(sb)
    ta = ta[(ta > tb[0]) & (ta < tb[-1])]          # A's cycles inside B's run
    fa = 1 / np.diff(ta).mean()
    # B's phase at each of A's cycles, in turns; its rate per cycle of A
    # B against A near p:q (B's frequency over A's): the phase q*B - p*A
    p, q = RATIOS[c.get('interval', 0)]
    pb = np.interp(ta, tb, np.arange(len(tb)))
    phase = q * pb - p * np.arange(len(ta))
    rate = np.diff(phase) * fa                    # Hz: B's frequency minus A's, cycle by cycle
    wrap = phase[1:] - np.floor(phase[1:])
    order = np.argsort(wrap)
    bins = np.linspace(0, 1, 33)
    g = [float(rate[(wrap >= lo) & (wrap < hi)].mean()) if np.any((wrap >= lo) & (wrap < hi)) else None
         for lo, hi in zip(bins[:-1], bins[1:])]
    res = dict(case=c, f_a=fa, beat_mean=float(rate.mean()), beat_min=float(rate.min()),
               beat_max=float(rate.max()), turns=float(phase[-1] - phase[0]),
               ripple_vcc_mv=float(1e3 * (vcc.max() - vcc.min())),
               ripple_vtop_mv=float(1e3 * (vtop.max() - vtop.min())), g=g)
    (OUT / f'{name}.json').write_text(json.dumps(res, indent=1))
    print(f"{name}: A {fa:.4f} Hz, B - A mean {res['beat_mean']:+.4f} Hz, from {res['beat_min']:+.4f} "
          f"to {res['beat_max']:+.4f}, {res['turns']:+.2f} turns; rail ripple VCC {res['ripple_vcc_mv']:.2f} mV, "
          f"VTOP {res['ripple_vtop_mv']:.2f} mV", flush=True)
    return res

if __name__ == '__main__':
    if sys.argv[1] == 'case':
        n, rs, cb, note, det, sec = sys.argv[2:8]
        iv = int(sys.argv[8]) if len(sys.argv) > 8 else 0
        case(n, float(rs), float(cb), int(note), float(det), float(sec), interval=iv)
    else:
        for n in sys.argv[2:]: analyze(n)
