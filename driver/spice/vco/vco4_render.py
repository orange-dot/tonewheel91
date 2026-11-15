#!/usr/bin/env python3
"""V4 listening renders, 48 kHz 24-bit mono, under build/vco/vco4/listen/.
- spice-*-AB.wav: the coupled VCO-2 pair in SPICE (both saws summed, band-
  limited to 20 kHz and taken to 48 kHz), 0.4 s of silence, the core's pair
  with the same detune and supply (build/vco_ma_ref pair@...).
- ma-*.wav: the core alone, the same pair without coupling (analog 0), a gap,
  with it."""
import json, subprocess, wave
import numpy as np
import paths, vco2

here = paths.out('vco4')
FS = 48000
out = here / 'listen'; out.mkdir(exist_ok=True)

def wav(path, x):
    q = np.round(np.clip(x, -1, 1) * (2**23 - 1)).astype('<i4')
    a = np.frombuffer(q.tobytes(), dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(FS); w.writeframes(a)

def fade(x, ms=15):
    n = int(FS * ms / 1000); r = .5 - .5 * np.cos(np.pi * np.arange(n) / n)
    x = x.copy(); x[:n] *= r; x[-n:] *= r[::-1]; return x

def to_48k(y, fs):
    """Band-limit to 20 kHz and resample, in one FFT."""
    n = len(y); m = int(round(n * FS / fs))
    c = np.fft.rfft(y - y.mean())
    f = np.fft.rfftfreq(n, 1 / fs)
    c[f > 20000] = 0
    return np.fft.irfft(c[:m // 2 + 1], m) * m / n

def ma(spec, hz, seconds):
    f = here / 'ma.f32'
    subprocess.run([paths.MA_REF, spec, '0.5', f, f'{hz:.6f}:{seconds + .5}'], check=True)
    return np.fromfile(f, dtype='<f4').astype(float)[int(.5 * FS):]

def pair(name, a, b, peak_db=-12):
    a = a - a.mean(); b = b - b.mean()
    b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
    g = 10**(peak_db / 20) / max(abs(a).max(), abs(b).max())
    wav(out / f'{name}.wav', np.concatenate([fade(a * g), np.zeros(int(.4 * FS)), fade(b * g)]))

def main():
    for case, supply in (('r10c100n-a4', 1), ('r1c10u-a4', 0)):
        c = json.loads((here / 'runs' / case / 'case.json').read_text())
        d = vco2.read_raw(here / 'runs' / case / 'trace.raw')
        spice = to_48k(-(d[:, 1] + d[:, 2]), vco2.FS_SPICE)
        core = ma(f"pair@1,{supply},0,{c['detune']}", 440.0, c['seconds'])
        pair(f'spice-{case}-AB', spice, core)
        print(case, flush=True)
    for name, interval, cents, supply in (('unison-1c-soft', 0, 1, 1), ('unison-1c-good', 0, 1, 0),
                                          ('unison-3c-soft', 0, 3, 1), ('octave-2c-soft', 12, 2, 1),
                                          ('fifth-2c-soft', 7, 2, 1),
                                          ('fourth-just-soft', 5, -1.955 + .3, 1),
                                          ('major-third-et-soft', 4, 0, 1)):
        hz = 220.0
        pair(f'ma-{name}', ma(f'pair@0,0,{interval},{cents}', hz, 6.0),
             ma(f'pair@1,{supply},{interval},{cents}', hz, 6.0))
        print(name, flush=True)

if __name__ == '__main__':
    main()
