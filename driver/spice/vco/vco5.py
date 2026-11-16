#!/usr/bin/env python3
"""V5: sync. Two VCO-2 cores (boards A and B, each trimmed as in step 2) on
ideal rails; A is the master, and each of its resets reaches B through one of
two study networks, neither a board design:
  reset   A's reset pulse (SQUARE_A) into the base of B's reset driver through
          a resistor: B's own reset switch is turned on from outside
  thresh  A's reset pulse into B's comparator threshold (VTH_B) through a
          capacitor: B's comparator is asked to trip early
B runs at `ratio` times A, so A's resets land on every phase of B's ramp.
  vco5.py case NAME KIND VALUE NOTE RATIO SECONDS [keep]  -> <out>/NAME.json
  vco5.py analyze NAME [NAME ...]                         (a kept trace again)
  vco5.py steps NAME KIND VALUE NOTE SECONDS I1,I2,...    -> <out>/runs/NAME/trace.raw
`steps` is for listening: B steps through the intervals (semitones above A),
SECONDS each, traced at 1.92 MHz like the other studies.
KIND reset: VALUE in ohms; thresh: in farads; free: no network (VALUE ignored).
The trace (20 MHz, <out>/runs/NAME/trace.raw) is removed once analyzed unless
`keep` is given. <out> is build/vco/vco5 (or $VCO_OUT_VCO5)."""
import json, subprocess, sys
import numpy as np
import paths, vco2, vco4

OUT = paths.out('vco5')
FS = 2.0e7                       # the trace's grid: a reset lasts about a microsecond

def bench(name, kind, value, note, cv_b, seconds, settle, grid, maxstep, vecs, record):
    """Both cores, the network, and a transient to <out>/runs/name/trace.raw.
    `cv_b` is B's CV source after its two nodes."""
    a = dict(vco2.DEFAULTS); a.update(vco4.BOARDS['A']); a.update(vco4.trims('A'))
    b = dict(vco2.DEFAULTS); b.update(vco4.BOARDS['B']); b.update(vco4.trims('B'))
    lines = ['* V5: VCO-2 master and slave', '.temp 25']
    lines += [f'.include "{paths.SRC}/models/{m}"' for m in ('bc547c.lib', 'bc557c.lib')]
    lines += ['VCC VCC 0 12', 'VTOP VTOP 0 8']
    lines += vco4.core('A', a) + vco4.core('B', b)
    lines += [f'VCV_A CV_A 0 {vco2.note_cv(note):.9g}', f'VCV_B CV_B 0 {cv_b}']
    if kind == 'reset':
        lines += [f'RSY SQUARE_A DBASE_B {value:.6g}']
    elif kind == 'thresh':
        lines += [f'CSY SQUARE_A VTH_B {value:.6g}']
    elif kind != 'free':
        raise ValueError(kind)
    lines += ['.options reltol=1e-5 vntol=1e-8 abstol=1e-13 method=gear', '.control',
              'set wr_singlescale', 'set wr_vecnames', f'save {vecs}',
              f'tran {grid:.10g} {settle + seconds:.9g} {settle:.9g} {maxstep:.6g}',
              f'linearize {vecs}', 'set filetype=binary', f'write trace.raw {vecs}',
              'rusage time', 'quit', '.endc', '.end']
    folder = OUT / 'runs' / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'bench.cir').write_text('\n'.join(lines) + '\n')
    (folder / 'case.json').write_text(json.dumps(dict(kind=kind, value=value, note=note, seconds=seconds,
                                                      **record)))
    r = subprocess.run(['ngspice', '-b', 'bench.cir'], cwd=folder, capture_output=True, text=True)
    (folder / 'ngspice.log').write_text(r.stdout + r.stderr)

def case(name, kind, value, note, ratio, seconds, settle=.01):
    bench(name, kind, value, note, f'{vco2.note_cv(note) + np.log2(ratio):.9g}', seconds, settle,
          1 / FS, 1 / FS, 'v(SAW_A) v(SAW_B) v(SQUARE_A) v(SQUARE_B)', dict(ratio=ratio))

def steps(name, kind, value, note, seconds, intervals, settle=.02):
    """B at each interval in turn, a millisecond between them."""
    cv = vco2.note_cv(note)
    points = []
    for k, i in enumerate(intervals):
        t0 = settle + k * seconds
        points += [f'{t0 + (1e-3 if k else 0):.6g} {cv + i / 12:.9g}', f'{t0 + seconds:.6g} {cv + i / 12:.9g}']
    points[0] = f'0 {cv + intervals[0] / 12:.9g}'
    bench(name, kind, value, note, 'PWL(' + ' '.join(points) + ')', seconds * len(intervals), settle,
          1 / vco2.FS_SPICE, .5e-6, 'v(SAW_A) v(SAW_B)', dict(intervals=intervals, each=seconds))

def jumps(saw, rise=.5):
    """Sample index where each fast upward move of the ramp starts: more than
    `rise` volts within two microseconds."""
    w = int(2e-6 * FS)
    up = saw[w:] - saw[:-w] > rise
    start = np.nonzero(up[1:] & ~up[:-1])[0] + 1
    return start + w - int(.3e-6 * FS) if len(start) else start

def analyze(name):
    folder = OUT / 'runs' / name
    c = json.loads((folder / 'case.json').read_text())
    d = vco2.read_raw(folder / 'trace.raw')
    sa, sb, qa, qb = d[:, 1], d[:, 2], d[:, 3], d[:, 4]
    top, bottom = np.percentile(sb, 99.9), sb.min()
    phase = lambda v: (top - v) / (top - bottom)          # B's ramp: 0 after a reset, 1 at its own
    before, after = int(1e-6 * FS), int(12e-6 * FS)
    events, shapes = [], []
    for i in jumps(sa):                                    # each of A's resets
        if i - before < 0 or i + after >= len(sb): continue
        p0, p1 = phase(sb[i - before]), phase(sb[i + after])
        own = bool(qb[i - before:i + after].max() > 6.0)   # B's comparator fired
        seg = sb[i - before:i + after]
        moved = seg.max() - seg[0]
        t90 = None
        if moved > .05:                                    # time from A's turn to 90 % of B's move
            t90 = float((np.argmax(seg >= seg[0] + .9 * moved) - before) / FS * 1e6)
        events.append(dict(t=float(d[i, 0]), before=float(p0), after=float(p1), own=own, t90_us=t90))
        if len(shapes) < 3 and moved > .05:                # B's ramp around the event, in volts
            a0, a1 = i - int(2e-6 * FS), i + int(14e-6 * FS)
            if a0 >= 0 and a1 < len(sb):
                shapes.append(dict(before=float(p0), dt_us=1e6 / FS, start_us=-2.0,
                                   saw_b=[round(float(v), 4) for v in sb[a0:a1]],
                                   saw_a=[round(float(v), 4) for v in sa[a0:a1]]))
    fa = 1 / np.diff([e['t'] for e in events]).mean() if len(events) > 1 else None
    # B runs on by this much of a turn between the two readings of an event
    advance = (before + after) / FS * fa * c['ratio'] if fa else None
    res = dict(case=c, f_a=fa, swing=[float(bottom), float(top)], advance=advance, events=events,
               shapes=shapes)
    (OUT / f'{name}.json').write_text(json.dumps(res, indent=1))
    # a first reading: the share of the phase left, and the phases that reset at all
    ev = [e for e in events if .08 < e['before'] < .97]
    left = np.array([e['after'] / e['before'] for e in ev] or [np.nan])
    own = np.array([e['own'] for e in ev], dtype=bool)
    print(f"{name}: {len(events)} resets of A at {fa:.2f} Hz; phase left after one: median {np.median(left):.3f} "
          f"(from {left.min():.3f} to {left.max():.3f}); B's comparator fired in {own.sum()} of {len(ev)}",
          flush=True)
    return res

if __name__ == '__main__':
    if sys.argv[1] == 'case':
        n, kind, value, note, ratio, sec = sys.argv[2:8]
        case(n, kind, float(value), int(note), float(ratio), float(sec))
        analyze(n)
        if 'keep' not in sys.argv[8:]:
            (OUT / 'runs' / n / 'trace.raw').unlink()
    elif sys.argv[1] == 'steps':
        n, kind, value, note, sec, ivs = sys.argv[2:8]
        steps(n, kind, float(value), int(note), float(sec), [int(i) for i in ivs.split(',')])
    else:
        for n in sys.argv[2:]: analyze(n)
