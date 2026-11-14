#!/usr/bin/env python3
"""B1 listening renders, 48 kHz 24-bit mono, under <out>/listen/.
Tones: the SPICE steady state is periodic to 1e-7, so a tone is its harmonics up
to 20 kHz, resynthesized (equal to an ideal resampling of the trace). The MA
side is build/vco_ma_ref at the measured frequency and duty. Each file: SPICE, a gap, MA.
Phrase: the real SPICE transient (stepped CV), decimated 1.92 MHz -> 48 kHz with
a 100 dB Kaiser low-pass; MA plays the frequencies measured from that trace."""
import json, pathlib, subprocess, wave, sys
import numpy as np

import paths
here = paths.out('b1')
FS, FS_SPICE = 48000, 1.92e6
out = here / 'listen'; out.mkdir(exist_ok=True); (here / 'ma').mkdir(exist_ok=True)

def wav(path, x):
    x = np.clip(x, -1, 1)
    q = np.round(x * (2**23 - 1)).astype('<i4')
    b = q.astype('<i4').tobytes()
    a = np.frombuffer(b, dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(FS); w.writeframes(a)

def fade(x, ms=15):
    n = int(FS * ms / 1000); r = .5 - .5*np.cos(np.pi*np.arange(n)/n)
    x = x.copy(); x[:n] *= r; x[-n:] *= r[::-1]; return x

def pair(name, a, b, peak_db=-12):
    """a = SPICE, b = MA: same RMS, common peak, a then b."""
    a = a - a.mean(); b = b - b.mean()
    b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
    g = 10**(peak_db/20) / max(abs(a).max(), abs(b).max())
    gap = np.zeros(int(.4*FS))
    wav(out / f'{name}-AB.wav', np.concatenate([fade(a*g), gap, fade(b*g)]))
    wav(out / f'{name}-spice.wav', fade(a*g)); wav(out / f'{name}-ma.wav', fade(b*g))
    return float(20*np.log10(np.sqrt(np.mean((a*g)**2))))

def tone(c, f0, seconds):
    t = np.arange(int(seconds*FS)) / FS
    y = np.zeros(len(t))
    for k in range(1, len(c)):
        y += (c[k] * np.exp(2j*np.pi*k*f0*t)).real
    return y

def ma(shape, width, segs):
    f = here / 'ma' / 'render.f32'
    subprocess.run([paths.MA_REF, shape, f'{width:.6f}', f] + [f'{hz:.6f}:{sec:.6f}' for hz, sec in segs], check=True)
    return np.fromfile(f, dtype='<f4').astype(float)

def tones(summary):
    for case in ('cv-0.5', 'cv-2', 'cv-5'):
        r = next(x for x in summary if x['case'] == case)
        f0 = r['f_hz']
        for shape, harm, width in (('tri', 'harm_tri.npy', .5), ('pulse', 'harm_pulse.npy', r['pulse_duty'])):
            a = tone(np.load(here / 'spice' / case / harm), f0, 3.0)
            b = ma(shape, width, [(f0, 3.5)])[int(.5*FS):]
            rms = pair(f'tone-{shape}-{f0:.0f}Hz', a, b)
            print(f'tone {shape} {f0:.1f} Hz: rms {rms:.1f} dBFS', flush=True)

def read_raw(path):
    with open(path, 'rb') as f:
        head = b''
        while not head.endswith(b'Binary:\n'):
            head += f.read(1)
        text = head.decode()
        nv = int(text.split('No. Variables:')[1].split()[0]); n = int(text.split('No. Points:')[1].split()[0])
        d = np.fromfile(f, dtype='<f8', count=nv*n).reshape(n, nv)
    return d

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
    return y, (N - 1)/2 / FS_SPICE

def cross(y, level):
    i = np.nonzero((y[:-1] < level) & (y[1:] >= level))[0]
    return (i + (level - y[i]) / (y[i+1] - y[i])) / FS_SPICE

def phrase():
    p = json.loads((here / 'phrase.json').read_text())
    d = read_raw(here / 'spice' / 'phrase' / 'trace.raw')
    step = np.diff(d[:1000, 0]); assert abs(step.mean()*FS_SPICE - 1) < 1e-6, 'grid is not 1.92 MHz'
    tri, pul = d[:, 1], d[:, 2]
    t0 = p['tstart']
    segs, rows = [], []
    for s in p['segments']:
        a = int((s['start'] - t0 + .03) * FS_SPICE); b = int((s['start'] - t0 + s['dur'] - .005) * FS_SPICE)
        x = cross(tri[a:b] - tri[a:b].mean(), 0.0)
        f = 1 / np.diff(x).mean()
        pm = .5*(pul[a:b].min() + pul[a:b].max())
        r_ = cross(pul[a:b], pm); f_ = cross(-pul[a:b], -pm); f_ = f_[f_ > r_[0]]; n = min(len(r_) - 1, len(f_))
        duty = float(np.mean((f_[:n] - r_[:n]) / np.diff(r_)[:n]))
        segs.append((f, s['dur'])); rows.append(dict(note=s['note'], target_hz=s['hz'], spice_hz=f,
                    cents=1200*np.log2(f / s['hz']), duty=duty))
        print(f"note {s['note']}: target {s['hz']:.2f} Hz, SPICE {f:.2f} Hz ({rows[-1]['cents']:+.2f} cents), duty {duty:.4f}", flush=True)
    (here / 'phrase-measured.json').write_text(json.dumps(rows, indent=1))
    duty = float(np.mean([r['duty'] for r in rows]))
    for shape, x, width in (('tri', tri, .5), ('pulse', pul, duty)):
        y, delay = decimate40(x)
        b = ma(shape, width, segs)
        n = min(len(y), len(b))
        rms = pair(f'phrase-{shape}', y[:n], b[:n])
        print(f'phrase {shape}: {n/FS:.2f} s, rms {rms:.1f} dBFS', flush=True)

if __name__ == '__main__':
    what = sys.argv[1:] or ['tones', 'phrase']
    if 'tones' in what: tones(json.loads((here / 'summary.json').read_text()))
    if 'phrase' in what: phrase()
