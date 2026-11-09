#!/usr/bin/env python3
"""Compare two directories of raw float32 PCM written by dump_ma_pcm.

For every file in A: peak level, peak absolute difference, the difference
RMS relative to the signal RMS (dB), the same in the 0-20 kHz band only, and
the first differing frame. Prints a Markdown table. Exit status 1 when a
file is missing in B or, with --max-db, when a case exceeds that level."""
import argparse
import math
import pathlib
import sys

import numpy as np

RATE = 48000


def band_db(err, sig, hi_hz=20000.0):
    """Error/signal power ratio in dB below hi_hz (per channel, summed)."""
    e = np.fft.rfft(err, axis=0)
    s = np.fft.rfft(sig, axis=0)
    f = np.fft.rfftfreq(err.shape[0], 1.0 / RATE)
    keep = f <= hi_hz
    pe = float(np.sum(np.abs(e[keep]) ** 2))
    ps = float(np.sum(np.abs(s[keep]) ** 2))
    return 10 * math.log10(pe / ps) if pe > 0 and ps > 0 else -math.inf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--max-db", type=float, help="fail when error/signal RMS exceeds this (dB)")
    args = ap.parse_args()
    a_dir, b_dir = pathlib.Path(args.a), pathlib.Path(args.b)
    names = sorted(p.name for p in a_dir.glob("*.f32"))
    bad, worst = 0, -math.inf
    print("| Case | peak | max abs diff | diff/signal dB | in 0-20 kHz dB | first diff (frame) |")
    print("|---|---:|---:|---:|---:|---:|")
    for name in names:
        bp = b_dir / name
        if not bp.exists():
            print(f"| {name[:-4]} | missing in B | | | | |")
            bad = 1
            continue
        x = np.fromfile(a_dir / name, dtype="<f4")
        y = np.fromfile(bp, dtype="<f4")
        if x.shape != y.shape:
            print(f"| {name[:-4]} | length {x.size} vs {y.size} | | | | |")
            bad = 1
            continue
        differ = np.nonzero(x.view("<u4") != y.view("<u4"))[0]
        peak = float(np.max(np.abs(x)))
        if differ.size == 0:
            print(f"| {name[:-4]} | {peak:.3f} | 0 | identical | | |")
            continue
        d = x.astype(np.float64) - y.astype(np.float64)
        sig = float(np.sum(x.astype(np.float64) ** 2))
        err = float(np.sum(d * d))
        if err == 0.0:
            print(f"| {name[:-4]} | {peak:.3f} | 0 | sign of zero only | | {differ[0] // 2} |")
            continue
        db = 10 * math.log10(err / sig)
        bdb = band_db(d.reshape(-1, 2), x.astype(np.float64).reshape(-1, 2))
        worst = max(worst, db)
        if args.max_db is not None and db > args.max_db:
            bad = 1
        print(f"| {name[:-4]} | {peak:.3f} | {float(np.max(np.abs(d))):.3g} | {db:.1f} | {bdb:.1f} | {differ[0] // 2} |")
    if worst > -math.inf:
        print(f"\nWorst diff/signal: {worst:.1f} dB over {len(names)} cases")
    return bad


if __name__ == "__main__":
    sys.exit(main())
