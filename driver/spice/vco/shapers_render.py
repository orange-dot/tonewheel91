#!/usr/bin/env python3
"""V2/V3 listening renders, 48 kHz 24-bit mono, under build/vco/shapers/listen/.
SPICE tones are the steady state of B1 with both shapers (shapers.py bench),
resynthesized from their harmonics up to 20 kHz; the MA side is
build/vco_ma_ref at the measured frequency with the circuit amount at 1.
Each -AB file: SPICE, 0.4 s of silence, MA, at the same RMS. The ma- files
are MA alone: the old shape, a gap, the circuit shape."""
import subprocess, wave
import numpy as np
import paths
from b1_analyze import harmonics
from shapers import load, f0_of

here = paths.out('shapers')
FS = 48000
out = here / 'listen'; out.mkdir(exist_ok=True); (here / 'ma').mkdir(exist_ok=True)

def wav(path, x):
    q = np.round(np.clip(x, -1, 1) * (2**23 - 1)).astype('<i4')
    a = np.frombuffer(q.tobytes(), dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(FS); w.writeframes(a)

def fade(x, ms=15):
    n = int(FS * ms / 1000); r = .5 - .5 * np.cos(np.pi * np.arange(n) / n)
    x = x.copy(); x[:n] *= r; x[-n:] *= r[::-1]; return x

def pair(name, a, b, peak_db=-12):
    a = a - a.mean(); b = b - b.mean()
    b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
    g = 10**(peak_db / 20) / max(abs(a).max(), abs(b).max())
    wav(out / f'{name}-AB.wav', np.concatenate([fade(a * g), np.zeros(int(.4 * FS)), fade(b * g)]))

def tone(c, f0, seconds=3.0):
    t = np.arange(int(seconds * FS)) / FS
    y = np.zeros(len(t))
    for k in range(1, len(c)):
        y += (c[k] * np.exp(2j * np.pi * k * f0 * t)).real
    return y

def ma(shape, hz, seconds=3.5):
    f = here / 'ma' / 'render.f32'
    subprocess.run([paths.MA_REF, shape, '0.5', f, f'{hz:.6f}:{seconds}'], check=True)
    return np.fromfile(f, dtype='<f4').astype(float)[int(.5 * FS):]

def main():
    import shapers
    for case in ('cv0', 'cv0.25', 'cv1.0607', 'cv3', 'r8-cv1.2', 'r8-cv2'):
        t = load(case)
        shapers.FS = 1 / (t['time'][1] - t['time'][0])
        f0 = f0_of(t['v(TRI)'])
        for name, y, shape in (('tri_sine', t['v(SINE)'], 'tri_sine@1'),
                               ('tri_saw', t['v(W_ON)'] - t['v(W_OP)'], 'tri_saw@1'),
                               ('tri', t['v(TRI)'], 'tri@1')):
            if case.startswith('r') and name == 'tri':
                continue        # the range switch's own skew is not the core's
            c = harmonics(y, shapers.FS, f0, top=20000.0)
            pair(f'tone-{name}-{f0:.0f}Hz', tone(c, f0), ma(shape, f0))
            print(f'{name} {f0:.1f} Hz', flush=True)
    # MA alone: the old shape, then the circuit's
    for name, old, new, hz in (('tri', 'tri', 'tri@1', 32.70), ('tri', 'tri', 'tri@1', 65.41),
                               ('saw', 'saw', 'saw@1', 65.41), ('sine', 'sine', 'tri_sine@1', 130.81),
                               ('saw-from-tri', 'saw', 'tri_saw@1', 65.41)):
        a, b = ma(old, hz), ma(new, hz)
        b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
        g = 10**(-12 / 20) / max(abs(a).max(), abs(b).max())
        wav(out / f'ma-{name}-{hz:.0f}Hz.wav', np.concatenate([fade(a * g), np.zeros(int(.4 * FS)), fade(b * g)]))
        print(f'ma {name} {hz} Hz', flush=True)

if __name__ == '__main__':
    main()
