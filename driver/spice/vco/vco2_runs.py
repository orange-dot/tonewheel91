#!/usr/bin/env python3
"""VCO-2 runs. Boards are parameter sets of vco2.cir; each is trimmed the way a
technician would: scale so that C2 -> C6 is four octaves, pitch so that A4 is 440 Hz.
  vco2_runs.py calibrate A|B -> cal-A.json
  vco2_runs.py track A|B       -> track-A.json   (C0..C9 by half octaves, cents from equal temperament)
  vco2_runs.py temp A         -> temp-A.json    (10/25/40 C, with and without the tempco resistor)
  vco2_runs.py phrase A|B      -> runs/phrase-A/trace.raw
  vco2_runs.py phrasewarm B    -> runs/phrase-Bw/trace.raw  (2 K above the trim temperature, tempco resistor fitted)"""
import json, sys, pathlib
import numpy as np
import vco2

here = vco2.OUT
BOARDS = {
    'A': dict(),
    # A second board inside ordinary part spread: gain, Early voltage, timing
    # capacitor +1 %, reset resistor, and the converter pair 4 % apart in Is.
    'B': dict(hfe=450, early=80, ct=3.3e-9 * 1.01, rlim=110.0, is2=1.04),
}
cents = lambda f, ref: 1200 * np.log2(f / ref)

def f_at(board, tag, note, p):
    (f, cv, n), v = vco2.measure(f'{board}-{tag}-n{note}', vco2.note_cv(note), guess=vco2.note_hz(note), **p)
    return f, v

def calibrate(board):
    p = dict(BOARDS[board])
    p.setdefault('rscale', vco2.DEFAULTS['rscale']); p.setdefault('vtune', vco2.DEFAULTS['vtune'])
    log = []
    for it in range(8):
        f36, _ = f_at(board, f'cal{it}', 36, p); f84, _ = f_at(board, f'cal{it}', 84, p)
        octaves = np.log2(f84 / f36)
        p['rscale'] *= 4 / octaves
        f69, _ = f_at(board, f'cal{it}s', 69, p)
        p['vtune'] -= np.log2(f69 / 440.0)
        log.append(dict(iteration=it, scale_error_cents=1200 * (octaves - 4), a4_error_cents=float(cents(f69, 440))))
        print(f"cal {board} {it}: C2..C6 {1200*(octaves-4):+.3f} cents, A4 {cents(f69, 440):+.3f} cents -> rscale {p['rscale']:.3f} vtune {p['vtune']:.5f}", flush=True)
        if it and abs(log[-1]['scale_error_cents']) < .05 and abs(log[-1]['a4_error_cents']) < .05:
            break
    (here / f'cal-{board}.json').write_text(json.dumps(dict(params=p, log=log), indent=1))
    return p

def cal(board):
    return json.loads((here / f'cal-{board}.json').read_text())['params']

def track(board):
    p = cal(board); rows = []
    for note in range(12, 121, 6):
        f, v = f_at(board, 'track', note, p)
        s = v['v(SAW)']
        rows.append(dict(note=note, cv=vco2.note_cv(note), target_hz=vco2.note_hz(note), f_hz=f,
                         cents=float(cents(f, vco2.note_hz(note))), top=float(s.max()), bottom=float(s.min())))
        print(f"track {board} note {note}: {f:.4f} Hz {rows[-1]['cents']:+.2f} cents, swing {s.min():.4f}..{s.max():.4f}", flush=True)
    (here / f'track-{board}.json').write_text(json.dumps(rows, indent=1))

def temp(board):
    base = cal(board); rows = []
    for tcs in (0.0, 3.3e-3):
        for t in (10.0, 25.0, 40.0):
            for note in (36, 69, 84):
                p = dict(base, tcs=tcs, temp=t)
                # RSCALE's tc1 is referred to the simulator's TNOM (27 C): retrim the value so that 25 C is unchanged
                p['rscale'] = base['rscale'] / (1 + tcs * (25.0 - 27.0))
                f, _ = f_at(board, f'temp{t:g}-tc{tcs:g}', note, p)
                rows.append(dict(tcs=tcs, temp=t, note=note, f_hz=f, cents=float(cents(f, vco2.note_hz(note)))))
                print(f"temp {board} tcs {tcs:g} {t:g} C note {note}: {rows[-1]['cents']:+.2f} cents", flush=True)
    (here / f'temp-{board}.json').write_text(json.dumps(rows, indent=1))

PHRASE = [(36, .6), (43, .6), (48, .6), (51, .6), (55, .6), (60, .6), (63, .6), (67, .6), (72, 1.2)]

def phrase(board, warm=0.0):
    """warm > 0: the board `warm` kelvin above its trim temperature, with the +3300 ppm/K scale resistor."""
    p = cal(board)
    if warm:
        p = dict(p, tcs=3.3e-3, temp=25.0 + warm, rscale=p['rscale'] / (1 + 3.3e-3 * (25.0 - 27.0)))
    t = 0.05; pwl = [0.0, vco2.note_cv(PHRASE[0][0])]
    for i, (note, dur) in enumerate(PHRASE):
        if i: pwl += [t, vco2.note_cv(PHRASE[i-1][0]), t + 20e-6, vco2.note_cv(note)]
        t += dur
    pwl += [t, vco2.note_cv(PHRASE[-1][0])]
    vco2.sim(f'phrase-{board}' + ('w' if warm else ''), ' '.join(f'{x:.9g}' for x in pwl), seconds=t - .05, settle=.05, maxstep=.5e-6,
             vecs='v(BUF)', binary=True, **p)
    print(f'phrase {board}: {t - .05:.2f} s', flush=True)

if __name__ == '__main__':
    if sys.argv[1] == 'phrasewarm': phrase(sys.argv[2], 2.0)
    else: {'calibrate': calibrate, 'track': track, 'temp': temp, 'phrase': phrase}[sys.argv[1]](sys.argv[2])
