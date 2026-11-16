#!/usr/bin/env python3
"""V5 listening renders, 48 kHz 24-bit mono, under build/vco/vco5/listen/.
The slave alone, stepping through intervals above a master at A2 (the `steps`
runs of vco5.py; the master is not heard).
- sync-*-AB.wav: the slave in SPICE (its ramp, band-limited to 20 kHz and
  taken to 48 kHz), 0.4 s of silence, the core's slave with today's sync at
  the amount named (build/vco_ma_ref sync@...).
- spice-*.wav: SPICE alone."""
import json, subprocess, wave
import numpy as np
import paths, vco2

here = paths.out('vco5')
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

STEPS = 6       # the runs' seventh interval is past the core's two octaves

def spice(name):
    """The slave's ramp, turned over so it rises like the core's saw."""
    c = json.loads((here / 'runs' / name / 'case.json').read_text())
    c['intervals'] = c['intervals'][:STEPS]
    d = vco2.read_raw(here / 'runs' / name / 'trace.raw')
    return to_48k(-d[:, 2], vco2.FS_SPICE)[:int(STEPS * c['each'] * FS)], c

def core(amount, c):
    """Today's sync in the core: one run per interval, joined."""
    hz = vco2.note_hz(c['note'])
    parts = []
    for i in c['intervals']:
        f = here / 'ma.f32'
        subprocess.run([paths.MA_REF, f'sync@0,{amount},0,{i},0', '0.5', f, f"{hz:.6f}:{c['each'] + .3}"],
                       check=True)
        parts.append(np.fromfile(f, dtype='<f4').astype(float)[int(.3 * FS):])
    return np.concatenate(parts)

def level(x, peak_db=-12):
    x = x - x.mean()
    return x * 10**(peak_db / 20) / abs(x).max()

def pair(name, a, b):
    a = a - a.mean(); b = b - b.mean()
    b = b * np.sqrt(np.mean(a**2) / np.mean(b**2))
    g = 10**(-12 / 20) / max(abs(a).max(), abs(b).max())
    wav(out / f'{name}.wav', np.concatenate([fade(a * g), np.zeros(int(.4 * FS)), fade(b * g)]))

def main():
    hard, c = spice('listen-hard')
    pair('sync-hard-AB', hard, core(1, c))
    for run, name, amount in (('listen-window-4p7', 'sync-window-47-AB', .47),
                              ('listen-window-2p2', 'sync-window-26-AB', .26)):
        y, c = spice(run)
        pair(name, y, core(amount, c))
    y, c = spice('listen-reset-100k')
    pair('spice-hard-two-networks', hard, y)
    y, c = spice('listen-reset-470k')
    wav(out / 'spice-reset-470k.wav', fade(level(y)))
    for f in sorted(out.glob('*.wav')):
        print(f.name, flush=True)

if __name__ == '__main__':
    main()
