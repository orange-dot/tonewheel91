#!/usr/bin/env python3
"""Check Hurt stems and write constant-gain listening/A-B WAVs (requires NumPy)."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np


RATE = 48000
BLOCK = 65536


def read_wav(path: Path) -> np.memmap:
    with path.open("rb") as file:
        header = file.read(12)
        if header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise ValueError(f"{path}: expected RIFF WAVE")
        layout = None
        while chunk := file.read(8):
            if len(chunk) != 8:
                raise ValueError(f"{path}: truncated chunk header")
            tag, size = struct.unpack("<4sI", chunk)
            offset = file.tell()
            if tag == b"fmt ":
                if size < 16:
                    raise ValueError(f"{path}: short format chunk")
                layout = struct.unpack("<HHIIHH", file.read(16))
            if tag == b"data":
                if layout != (3, 2, RATE, RATE * 8, 8, 32):
                    raise ValueError(f"{path}: expected 48 kHz stereo float32")
                if not size or size % 8 or offset + size > path.stat().st_size:
                    raise ValueError(f"{path}: invalid data length")
                return np.memmap(path, dtype="<f4", mode="r", offset=offset,
                                 shape=(size // 8, 2))
            file.seek(offset + size + (size & 1))
    raise ValueError(f"{path}: no data chunk")


def db(value: float) -> float | None:
    return 20 * math.log10(value) if value > 0 else None


def measure(samples: np.ndarray) -> dict:
    peak = square = mono_square = cross = left_square = right_square = 0.0
    total = np.zeros(2, dtype=np.float64)
    for begin in range(0, len(samples), BLOCK):
        x = np.asarray(samples[begin:begin + BLOCK], dtype=np.float64)
        if not np.isfinite(x).all():
            raise ValueError("nonfinite audio")
        peak = max(peak, float(np.abs(x).max()))
        square += float(np.sum(x * x))
        mono_square += float(np.sum(np.mean(x, axis=1) ** 2))
        cross += float(np.sum(x[:, 0] * x[:, 1]))
        left_square += float(np.sum(x[:, 0] ** 2))
        right_square += float(np.sum(x[:, 1] ** 2))
        total += x.sum(axis=0)
    rms = math.sqrt(square / samples.size)
    mono_rms = math.sqrt(mono_square / len(samples))
    correlation = cross / math.sqrt(left_square * right_square) if left_square * right_square else None
    return {"frames": len(samples), "peak": peak, "peak_dbfs": db(peak),
            "rms": rms, "rms_dbfs": db(rms), "mono_rms_dbfs": db(mono_rms),
            "mono_loss_db": db(mono_rms / rms) if rms else None,
            "lr_correlation": correlation, "mean": (total / len(samples)).tolist(),
            "last_100ms_peak": float(np.abs(samples[-4800:]).max())}


def write_scaled(path: Path, samples: np.ndarray, gain: float) -> None:
    size = samples.size * 4
    header = (struct.pack("<4sI4s4sIHHIIHH", b"RIFF", size + 48, b"WAVE", b"fmt ",
                          16, 3, 2, RATE, RATE * 8, 8, 32)
              + struct.pack("<4sII4sI", b"fact", 4, len(samples), b"data", size))
    temporary = path.with_suffix(".wav.tmp")
    try:
        with temporary.open("wb") as file:
            file.write(header)
            for begin in range(0, len(samples), BLOCK):
                x = np.asarray(samples[begin:begin + BLOCK] * np.float32(gain), dtype="<f4")
                if not np.isfinite(x).all() or np.abs(x).max() >= 1:
                    raise ValueError("unsafe output gain")
                file.write(x.tobytes())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def sha256(path: Path) -> str:
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--stems", nargs="+", choices=("tonal", "rhythm", "ep", "organ", "vocal", "harmony", "right", "left", "pedals"),
                        default=["tonal", "rhythm"], help="list every source stem; default is the historical dark take")
    parser.add_argument("--previews", action="store_true",
                        help="extract full-render sections and melody excerpts")
    args = parser.parse_args()

    def output(name: str, suffix: str = ".wav") -> Path:
        return Path(f"{args.prefix}_{name}{suffix}")

    if len(set(args.stems)) != len(args.stems):
        parser.error("duplicate stem name")
    audio = {name: read_wav(output(name)) for name in (*args.stems, "mix")}
    mix = audio["mix"]
    if any(x.shape != mix.shape for x in audio.values()):
        raise ValueError("unaligned stems")
    report = {name: measure(x) for name, x in audio.items()}
    residual = 0.0
    for begin in range(0, len(mix), BLOCK):
        sl = slice(begin, begin + BLOCK)
        summed = np.zeros_like(mix[sl])
        for name in args.stems:
            summed += audio[name][sl]
        residual = max(residual, float(np.max(np.abs(summed - mix[sl]))))
    if residual > 1e-7 or any(v["peak"] >= 1 for v in report.values()):
        raise ValueError("stem sum or clipping check failed")
    report["stem_sum_max_error"] = residual
    target = float(np.nextafter(np.float32(10 ** (-3 / 20)), np.float32(0)))
    if report["mix"]["peak"] <= 0:
        raise ValueError("silent mix")
    gain = target / report["mix"]["peak"]
    write_scaled(output("listening"), mix, gain)
    report["listening"] = measure(read_wav(output("listening")))
    report["listening"]["gain"] = gain
    report["listening"]["gain_db"] = db(gain)
    report["listening"]["sha256"] = sha256(output("listening"))
    report["sections"] = []
    bars = (0, 8, 24, 40, 48, 64, 68, 80, 85)
    for first, last in zip(bars, bars[1:]):
        begin = round(first * 4 * .714285 * RATE)
        end = min(round(last * 4 * .714285 * RATE), len(mix))
        if begin >= end:
            continue
        report["sections"].append({"bars_zero_based": [first, last],
                                    **{name: measure(x[begin:end]) for name, x in audio.items()}})
    if args.reference:
        reference = read_wav(args.reference)
        if reference.shape != mix.shape:
            raise ValueError("reference duration differs from full mix")
        baseline = measure(reference)
        if not baseline["peak"] or not baseline["rms"]:
            raise ValueError("silent reference")
        common_rms = min(report["mix"]["rms"] * gain,
                         baseline["rms"] * target / baseline["peak"])
        new_gain = common_rms / report["mix"]["rms"]
        old_gain = common_rms / baseline["rms"]
        write_scaled(output("comparison_new"), mix, new_gain)
        write_scaled(output("comparison_noir"), reference, old_gain)
        report["comparison"] = {"method": "whole-file RMS, constant gain; no compression",
                                 "common_rms_dbfs": db(common_rms),
                                 "new_gain": new_gain, "noir_gain": old_gain,
                                 "reference": str(args.reference), "reference_sha256": sha256(args.reference)}
    if args.previews:
        for name, start, duration in (("intro", 0, 18), ("answer", 31, 12),
                                      ("groove", 69, 18), ("melody", 91, 24),
                                      ("breakdown", 115, 18), ("climax", 194, 18),
                                      ("outro", 229, 24)):
            if (start + duration) * RATE <= len(mix):
                write_scaled(output(f"preview_{name}"), mix[start * RATE:(start + duration) * RATE], gain)
    report["raw_sha256"] = {name: sha256(output(name)) for name in audio}
    output("levels", ".json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({"listening": report["listening"], "stem_sum_max_error": residual}, indent=2))


if __name__ == "__main__":
    main()
