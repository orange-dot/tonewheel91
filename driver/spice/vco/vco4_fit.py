#!/usr/bin/env python3
"""V4: the sawtooth of each coupled pair (vco4.py runs) -> build/vco/vco4/fits4.json.
The jump is located directly (the largest drop between neighbouring bins of
the phase-difference rate) and given in the core's phase (0 at the reset;
SPICE times cycles mid-ramp, which moves psi = q B - p A by (q - p) / 2);
the half height L is then a straight line fitted on the bins away from the
jump, `rate = 2 L (wrap(psi - jump) - 1/2) + c`. The rate is taken over p
of A's cycles, one turn of the p:q pattern. L is also given over the
lower core's frequency, and the residual over L.
  vco4_fit.py NAME ..."""
import json, sys
import numpy as np
import vco2, vco4

def fit(name, bins=48):
    folder = vco4.OUT / 'runs' / name
    c = json.loads((folder / 'case.json').read_text())
    p, q = c['ratio'] if c.get('ratio') else vco4.RATIOS[c.get('interval', 0)]
    d = vco2.read_raw(folder / 'trace.raw')
    ta, tb = vco4.resets(d[:, 1]), vco4.resets(d[:, 2])
    ta = ta[(ta > tb[0]) & (ta < tb[-1])]
    fa = 1 / np.diff(ta).mean()
    ph = q * np.interp(ta, tb, np.arange(len(tb))) - p * np.arange(len(ta))
    # over p of A's cycles B is back where it was in its own cycle, so the
    # interpolation of its phase between resets cancels
    rate = (ph[p:] - ph[:-p]) * fa / p
    w = (ph[p:] + ph[:-p]) / 2
    w -= np.floor(w)
    k = (w * bins).astype(int)
    g = np.array([rate[k == i].mean() if np.any(k == i) else np.nan for i in range(bins)])
    for i in range(bins):
        if np.isnan(g[i]): g[i] = g[i - 1]
    j = int(np.argmax(g - np.roll(g, -1)))
    jump_spice = (j + 1) / bins
    ctr = (np.arange(bins) + .5) / bins
    u = (ctr - jump_spice) % 1
    m = (u > .06) & (u < .94)
    A = np.stack([u[m] - .5, np.ones(m.sum())], 1)
    (s, c0), *_ = np.linalg.lstsq(A, g[m], rcond=None)
    r = float(np.sqrt(np.mean((A @ [s, c0] - g[m]) ** 2)))
    low = fa * min(1, p / q)
    return dict(case=name, p=p, q=q, rs=c['rs'], cb=c['cb'], f_a=fa, f_low=low, lock_hz=s / 2,
                rel=s / 2 / low, jump=(jump_spice + (q - p) / 2) % 1, residual_of_lock=r / abs(s / 2))

if __name__ == '__main__':
    out = vco4.OUT / 'fits4.json'
    res = json.loads(out.read_text()) if out.exists() else {}
    for n in sys.argv[1:]:
        f = fit(n)
        res[n] = f
        print(f"{n:20s} {f['p']}:{f['q']}  L {f['lock_hz']:+.4f} Hz  L/f_low {f['rel']:+.3e}  jump {f['jump']:.3f}"
              f"  residual {f['residual_of_lock']:.2f} of L", flush=True)
    out.write_text(json.dumps(res, indent=1))
