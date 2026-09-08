#!/usr/bin/env python3
"""Validate Chopin stems and create a constant-gain -3 dBFS listening copy."""

import argparse
import json
from pathlib import Path

import numpy as np

from prepare_hurt_listening import BLOCK, db, measure, read_wav, sha256, write_scaled


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()

    def output(name: str, suffix: str = ".wav") -> Path:
        return Path(f"{args.prefix}_{name}{suffix}")

    stems = ("lead", "chords", "bass", "echo")
    audio = {name: read_wav(output(name)) for name in (*stems, "mix")}
    mix = audio["mix"]
    if any(x.shape != mix.shape for x in audio.values()):
        raise ValueError("unaligned stems")
    report = {name: measure(x) for name, x in audio.items()}
    residual = 0.0
    for begin in range(0, len(mix), BLOCK):
        span = slice(begin, begin + BLOCK)
        summed = np.zeros_like(mix[span])
        for name in stems:
            summed += audio[name][span]
        residual = max(residual, float(np.max(np.abs(summed - mix[span]))))
    if residual > 1e-7 or any(item["peak"] >= 1 for item in report.values()):
        raise ValueError("stem sum or clipping check failed")
    if any(item["peak"] <= 0 for item in report.values()):
        raise ValueError("silent stem or mix")
    target = float(np.nextafter(np.float32(10 ** (-3 / 20)), np.float32(0)))
    gain = target / report["mix"]["peak"]
    write_scaled(output("listening"), mix, gain)
    report["listening"] = {
        **measure(read_wav(output("listening"))),
        "gain": gain,
        "gain_db": db(gain),
        "sha256": sha256(output("listening")),
    }
    report["stem_sum_max_error"] = residual
    report["method"] = "One constant gain for the full mix; no compression or limiting."
    report["raw_sha256"] = {name: sha256(output(name)) for name in audio}
    output("levels", ".json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({"listening": report["listening"], "stem_sum_max_error": residual}, indent=2))


if __name__ == "__main__":
    main()
