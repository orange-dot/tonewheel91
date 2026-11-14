#!/usr/bin/env python3
"""VCO-2 listening renders, 48 kHz 24-bit mono, under <out>/listen/. Each -AB.wav is
SPICE, 0.4 s of silence, then MA, at the same RMS.
  tone-saw-<note>   board A's steady state, resynthesized from its harmonics, against the MA saw
  phrase-saw        board A's real transient (stepped CV) against MA at the frequencies A played
  phrase-pair       boards A + B together against two MA saws a fixed number of cents apart
                    (the RMS of the A-B difference over the phrase)
  phrase-pair-warm  the same with board B 2 K above its trim temperature (tempco resistor fitted)"""
import json, pathlib, subprocess, wave
import numpy as np
import vco2
from vco2_runs import PHRASE
import paths

here = vco2.OUT
FS, FS_SPICE = 48000, vco2.FS_SPICE
out = here / 'listen'; out.mkdir(exist_ok=True); (here / 'ma').mkdir(exist_ok=True)

def wav(path, x):
    q = np.round(np.clip(x, -1, 1) * (2**23 - 1)).astype('<i4')
    b = np.frombuffer(q.tobytes(), dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(FS); w.writeframes(b)

def fade(x, ms=15):
    n = int(FS * ms / 1000); r = .5 - .5*np.cos(np.pi*np.arange(n)/n)
    x = x.copy(); x[:n] *= r; x[-n:] *= r[::-1]; return x

def pair(name, a, b, peak_db=-12):
    a = a - a.mean(); b = b - b.mean()
    b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
    g = 10**(peak_db/20) / max(abs(a).max(), abs(b).max())
    wav(out / f'{name}-AB.wav', np.concatenate([fade(a*g), np.zeros(int(.4*FS)), fade(b*g)]))
    wav(out / f'{name}-spice.wav', fade(a*g)); wav(out / f'{name}-ma.wav', fade(b*g))

def ma(segs, name='render'):
    f = here / 'ma' / f'{name}.f32'
    subprocess.run([paths.MA_REF, 'saw', '0.5', f] + [f'{hz:.6f}:{sec:.6f}' for hz, sec in segs], check=True)
    return np.fromfile(f, dtype='<f4').astype(float)

def decimate40(x):
    A, width = 100.0, 4000.0
    N = int(np.ceil((A - 8) / (2.285 * 2*np.pi*width/FS_SPICE))) | 1
    n = np.arange(N) - (N - 1)/2
    h = np.sinc(2*22000/FS_SPICE*n) * 2*22000/FS_SPICE * np.kaiser(N, .1102*(A - 8.7))
    h /= h.sum()
    v = np.lib.stride_tricks.sliding_window_view(x, N)[::40]
    y = np.empty(len(v))
    for i in range(0, len(v), 2000):
        y[i:i+2000] = v[i:i+2000] @ h
    return y

def tones():
    for note in (36, 60, 84):
        c = np.load(here / f'harm-A-n{note}.npy')
        f0 = next(r['f_hz'] for r in json.loads((here / 'shape-A.json').read_text())['shapes'] if r['note'] == note)
        t = np.arange(int(3.0*FS)) / FS
        y = sum((c[k] * np.exp(2j*np.pi*k*f0*t)).real for k in range(1, len(c)))
        pair(f'tone-saw-n{note}-{f0:.0f}Hz', y, ma([(f0, 3.5)])[int(.5*FS):])
        print(f'tone saw note {note} {f0:.2f} Hz', flush=True)

def phrase_freqs(buf):
    rows, t = [], 0.0
    for note, dur in PHRASE:
        a = int((t + .05) * FS_SPICE); b = int((t + dur - .01) * FS_SPICE)
        seg = buf[a:b]
        f, cv, n = vco2.frequency(seg)
        rows.append(f); t += dur
    return rows

def phrases():
    A = vco2.read_raw(here / 'runs' / 'phrase-A' / 'trace.raw')[:, 1]
    B = vco2.read_raw(here / 'runs' / 'phrase-B' / 'trace.raw')[:, 1]
    fa, fb = phrase_freqs(A), phrase_freqs(B)
    rows = []
    for (note, dur), a, b in zip(PHRASE, fa, fb):
        et = vco2.note_hz(note)
        rows.append(dict(note=note, seconds=dur, et_hz=et, a_hz=a, b_hz=b, a_cents=1200*np.log2(a/et), b_cents=1200*np.log2(b/et),
                         diff_cents=1200*np.log2(b/a), beat_hz=abs(b - a)))
        print(f"note {note}: A {rows[-1]['a_cents']:+.2f} cents, B {rows[-1]['b_cents']:+.2f} cents, A-B beat {rows[-1]['beat_hz']:.3f} Hz", flush=True)
    d = float(np.sqrt(np.mean([r['diff_cents']**2 for r in rows])))
    (here / 'phrase-measured.json').write_text(json.dumps(dict(notes=rows, ma_pair_detune_cents=d), indent=1))
    ya, yb = decimate40(-A), decimate40(-B)
    n = min(len(ya), len(yb))
    solo = ma([(f, dur) for (note, dur), f in zip(PHRASE, fa)])
    pair('phrase-saw', ya[:n], solo[:n])
    m1 = ma([(vco2.note_hz(note), dur) for note, dur in PHRASE], 'pair1')
    m2 = ma([(vco2.note_hz(note) * 2**(d/1200), dur) for note, dur in PHRASE], 'pair2')
    ya0 = ya[:n] - ya[:n].mean(); yb0 = yb[:n] - yb[:n].mean()
    yb0 *= np.sqrt(np.mean(ya0**2) / np.mean(yb0**2))
    pair('phrase-pair', ya0 + yb0, (m1 + m2)[:n])
    print(f'phrase: {n/FS:.2f} s, MA pair detune {d:.2f} cents', flush=True)
    w = here / 'runs' / 'phrase-Bw' / 'trace.raw'
    if w.exists():
        W = vco2.read_raw(w)[:, 1]
        fw = phrase_freqs(W)
        warm = [dict(note=note, a_cents=1200*np.log2(a/vco2.note_hz(note)), b_cents=1200*np.log2(b/vco2.note_hz(note)),
                     diff_cents=1200*np.log2(b/a), beat_hz=abs(b - a)) for (note, dur), a, b in zip(PHRASE, fa, fw)]
        dw = float(np.sqrt(np.mean([r['diff_cents']**2 for r in warm])))
        for r in warm: print(f"warm note {r['note']}: B {r['b_cents']:+.2f} cents, A-B {r['diff_cents']:+.2f} cents, beat {r['beat_hz']:.3f} Hz", flush=True)
        (here / 'phrase-measured.json').write_text(json.dumps(dict(notes=rows, ma_pair_detune_cents=d, warm_notes=warm,
                                                                   ma_pair_warm_detune_cents=dw), indent=1))
        yw = decimate40(-W)[:n]; yw = yw - yw.mean(); yw *= np.sqrt(np.mean(ya0**2) / np.mean(yw**2))
        m3 = ma([(vco2.note_hz(note) * 2**(dw/1200), dur) for note, dur in PHRASE], 'pair3')
        pair('phrase-pair-warm', ya0 + yw, (m1 + m3)[:n])
        print(f'warm pair: MA detune {dw:.2f} cents', flush=True)

if __name__ == '__main__':
    import sys
    what = sys.argv[1:] or ['tones', 'phrases']
    if 'tones' in what: tones()
    if 'phrases' in what: phrases()
