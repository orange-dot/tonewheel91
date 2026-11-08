#!/usr/bin/env python3
"""Remix rendered MA3 stems with constant per-stem gains (requires NumPy).

No compression, limiting or gain riding: every stem gets one static gain,
the stems are summed, and the sum gets one static gain that puts its sample
peak at --peak-dbfs. With all stem gains at 0 dB the float32 re-sum must
reproduce the rendered mix exactly; the script checks that first.

--replace STEM=PATH mixes another render of one stem in place of the
rendered one (the exact re-sum check still runs on the rendered stems).
With --keep-master the master gain is the one the same stem gains would get
without replacements, so every other stem keeps its exact output level.
--trim STEM=DB adds a static gain to a replaced stem only (level match); it
does not enter the --keep-master reference. --master-db fixes the master
gain outright (the output must still stay below full scale).

usage: remix_stems.py PREFIX OUT.wav --stems tonal rhythm ... [--gain vocal=-6]
       [--replace vocal=PATH] [--keep-master] [--peak-dbfs -1]
"""

import argparse
import hashlib
import math
from pathlib import Path
import struct

import numpy as np

RATE = 48000
BLOCK = 1 << 16


def read_wav(path: Path) -> np.memmap:
    with path.open("rb") as file:
        if file.read(12)[8:] != b"WAVE":
            raise ValueError(f"{path}: expected RIFF WAVE")
        layout = None
        while chunk := file.read(8):
            tag, size = struct.unpack("<4sI", chunk)
            offset = file.tell()
            if tag == b"fmt ":
                layout = struct.unpack("<HHIIHH", file.read(16))
            if tag == b"data":
                if layout != (3, 2, RATE, RATE * 8, 8, 32):
                    raise ValueError(f"{path}: expected 48 kHz stereo float32")
                return np.memmap(path, dtype="<f4", mode="r", offset=offset,
                                 shape=(size // 8, 2))
            file.seek(offset + size + (size & 1))
    raise ValueError(f"{path}: no data chunk")


def write_wav(path: Path, frames: int, blocks) -> None:
    size = frames * 8
    header = (struct.pack("<4sI4s4sIHHIIHH", b"RIFF", size + 48, b"WAVE", b"fmt ",
                          16, 3, 2, RATE, RATE * 8, 8, 32)
              + struct.pack("<4sII4sI", b"fact", 4, frames, b"data", size))
    temporary = path.with_suffix(".wav.tmp")
    try:
        with temporary.open("wb") as file:
            file.write(header)
            for x in blocks:
                file.write(np.asarray(x, dtype="<f4").tobytes())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def db(x: float) -> float:
    return 20 * math.log10(x) if x > 0 else float("-inf")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("prefix", help="render prefix, e.g. build/ma3-hurt-orbita/hurt_orbita")
    parser.add_argument("output", type=Path)
    parser.add_argument("--stems", nargs="+", required=True)
    parser.add_argument("--gain", action="append", default=[], metavar="STEM=DB")
    parser.add_argument("--replace", action="append", default=[], metavar="STEM=PATH")
    parser.add_argument("--keep-master", action="store_true")
    parser.add_argument("--trim", action="append", default=[], metavar="STEM=DB",
                        help="extra static gain on a replaced stem only (level match)")
    parser.add_argument("--peak-dbfs", type=float, default=-1.0)
    parser.add_argument("--master-db", type=float,
                        help="fixed master gain in dB instead of peak normalization")
    args = parser.parse_args()

    gains_db = {name: 0.0 for name in args.stems}
    for item in args.gain:
        name, _, value = item.partition("=")
        if name not in gains_db:
            parser.error(f"--gain names an unlisted stem: {name}")
        gains_db[name] = float(value)
    if not -40.0 < args.peak_dbfs < 0.0:
        parser.error("--peak-dbfs must be in (-40, 0)")

    stems = {n: read_wav(Path(f"{args.prefix}_{n}.wav")) for n in args.stems}
    mix = read_wav(Path(f"{args.prefix}_mix.wav"))
    replaced = dict(stems)
    for item in args.replace:
        name, _, path = item.partition("=")
        if name not in stems:
            parser.error(f"--replace names an unlisted stem: {name}")
        replaced[name] = read_wav(Path(path))
    if any(x.shape != mix.shape for x in (*stems.values(), *replaced.values())):
        raise ValueError("unaligned stems")
    trims_db = {}
    for item in args.trim:
        name, _, value = item.partition("=")
        if name not in dict(r.partition("=")[::2] for r in args.replace):
            parser.error(f"--trim names a stem that is not replaced: {name}")
        trims_db[name] = float(value)
    frames = len(mix)

    # 1. The listed stems must be the whole mix: exact float32 re-sum.
    for begin in range(0, frames, BLOCK):
        s = slice(begin, begin + BLOCK)
        acc = np.zeros_like(mix[s])
        for n in args.stems:
            acc = acc + stems[n][s]
        if not np.array_equal(acc, mix[s]):
            raise ValueError("stems do not re-sum to the rendered mix")

    # 2. Static per-stem gains, summed in float64; one pass for the peak.
    linear = {n: 10 ** (g / 20) for n, g in gains_db.items()}

    def remixed(sources):
        trimmed = sources is replaced
        for begin in range(0, frames, BLOCK):
            s = slice(begin, begin + BLOCK)
            acc = np.zeros(mix[s].shape, dtype=np.float64)
            for n in args.stems:
                gain = linear[n] * (10 ** (trims_db.get(n, 0.0) / 20) if trimmed else 1.0)
                acc += gain * np.asarray(sources[n][s], dtype=np.float64)
            yield acc

    def peak_and_square(sources):
        peak = square = 0.0
        for x in remixed(sources):
            peak = max(peak, float(np.abs(x).max()))
            square += float(np.sum(x * x))
        if peak <= 0:
            raise ValueError("silent remix")
        return peak, square

    peak, square = peak_and_square(replaced)
    reference_peak = peak_and_square(stems)[0] if args.keep_master else peak
    master = 10 ** (args.peak_dbfs / 20) / reference_peak
    if args.master_db is not None:
        master = 10 ** (args.master_db / 20)

    # 3. One static master gain; nothing else touches the signal.
    def output_blocks():
        for x in remixed(replaced):
            y = (x * master).astype(np.float32)
            if not np.isfinite(y).all() or np.abs(y).max() >= 1:
                raise ValueError("unsafe output")
            yield y

    write_wav(args.output, frames, output_blocks())
    with args.output.open("rb") as file:
        digest = hashlib.file_digest(file, "sha256").hexdigest()
    rms = math.sqrt(square / (2 * frames))
    print(f"{args.output}: stem gains dB {gains_db}; replaced {args.replace or 'none'}; "
          f"trim {trims_db or 'none'}; "
          f"master {db(master):+.4f} dB; peak {db(peak * master):.2f} dBFS; "
          f"RMS {db(rms * master):.2f} dBFS; sha256 {digest[:16]}")


if __name__ == "__main__":
    main()
