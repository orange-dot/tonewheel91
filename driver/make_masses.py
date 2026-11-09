#!/usr/bin/env python3
"""Write the 2026-11-08 Mamut arrangement of Masses for the New Messiah
(Dimmu Borgir) as a seven-channel SMF.

Source: the full-band BitMidi transcription 78711
(notes-midi/local/dimmu-borgir/dimmu-borgir-masses-for-the-new-messiah-bitmidi-78711.mid):
two guitars, bass, two keyboard tracks (choir, strings) and a kit, 480 PPQ.
The tempo map alternates 165 and 150 BPM (bar 38, 54, 150, 182); two of its
seven entries repeat the tempo already in force and are dropped from the
derived map. Pitched notes keep their source velocities (95, and 79 on 184
choir notes). Each output channel is one Mamut part of
driver/exhibit_ma3_masses.c.

Kit rules as in driver/make_spellbound.py (the 2026-11-08 kit), with two
differences: the kick sits on the tonic A1 (MIDI 33, 55 Hz), because the
fifth of A minor has no E inside the kick register MIDI 32..37 used by the
other shows; and blast bars are bars with eight or more snares over eight
or more kicks (86-101 and 134-149, kick and snare alternating at 165 BPM),
so the seven-snare fill in bar 201 stays a fill under the 200 ms rule.
Toms, cymbals and hats are dropped. Standard library only.
"""

import argparse
import hashlib
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True  # the sibling import leaves no __pycache__ in driver/
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_mourning_palace import read_smf, pair_notes, track, vlq, wobble  # noqa: E402

SOURCE_SHA256 = "ea98a956e93ede143bbbd6df2bc20caac5c286dac8a2c349b69449d3cba0c6cc"
PPQ, BAR, BEAT = 480, 1920, 480
SOURCE_TEMPOS = [(0, 363636), (72960, 400000), (88320, 400000), (103680, 363636),
                 (288000, 400000), (318720, 400000), (349440, 363636)]
TEMPOS = [(0, 363636), (72960, 400000), (103680, 363636), (288000, 400000), (349440, 363636)]
SOURCE_LAYOUT = {1: ("Guitar I", 2428), 2: ("Guitar II", 2487), 3: ("Bass", 1563),
                 4: ("Keyboard", 696), 5: ("Keyboard", 1029), 6: ("Drums", 3706)}

GTR1, GTR2, BASS, CHOIR, STRINGS, KICK, PULSE = range(7)
NAMES = ("gtr1", "gtr2", "bass", "choir", "strings", "kick", "pulse")
SRC_KICK, SRC_SNARE = 36, 40
KICK_NOTE = 33            # A1 (55 Hz), the tonic of A minor
KICK_GATE_US, PULSE_GATE_US, PULSE_MIN_US = 148_133, 401_167, 200_000
RUN_GAP = 160
BLAST_BARS = list(range(86, 102)) + list(range(134, 150))


def us_per_quarter(tick: int) -> int:
    us = TEMPOS[0][1]
    for at, value in TEMPOS:
        if at <= tick:
            us = value
    return us


def ticks_for(tick: int, us: int) -> int:
    return us * PPQ // us_per_quarter(tick)


def half_bar_roots(src, halves):
    """The MA3 Hurt rule: lowest bass attack, else lowest guitar attack,
    else the previous root, folded into MIDI 31..42."""
    bass, low = [0] * halves, [0] * halves
    for idx, table in ((3, bass), (1, low), (2, low)):
        for start, _, note, _ in src[idx]:
            h = start // (BAR // 2)
            if h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 33
    for h in range(halves):
        root = bass[h] or low[h] or previous
        while root > 42:
            root -= 12
        while root < 31:
            root += 12
        roots.append(root)
        previous = root
    return roots


def kit(src, parts, end_tick):
    drums = src[6]
    kicks = sorted({s for s, _, n, _ in drums if n == SRC_KICK})
    snares = sorted({s for s, _, n, _ in drums if n == SRC_SNARE})
    count = {}
    for s in kicks:
        count.setdefault(s // BAR, [0, 0])[0] += 1
    for s in snares:
        count.setdefault(s // BAR, [0, 0])[1] += 1
    blast = sorted(b for b, (k, sn) in count.items() if sn >= 8 and k >= 8)
    if blast != BLAST_BARS:
        raise SystemExit(f"blast bars differ from the inspected source: {blast}")

    for i, t in enumerate(kicks):
        prev_gap = t - kicks[i - 1] if i else None
        next_gap = kicks[i + 1] - t if i + 1 < len(kicks) else None
        run = (prev_gap is not None and prev_gap <= RUN_GAP) or (next_gap is not None and next_gap <= RUN_GAP)
        if t % BAR == 0:
            v = 116 if run else 113
        elif not run:
            v = 94
        elif t % BEAT == 0:
            v = 106
        elif t % (BEAT // 2) == 0:
            v = 92
        else:
            v = 74
        v = min(127, v + wobble(t, 1))
        gate = ticks_for(t, KICK_GATE_US)
        if next_gap is not None:
            gate = min(gate, next_gap)
        parts[KICK].append([t, t + gate, KICK_NOTE, v])

    roots = half_bar_roots(src, end_tick // (BAR // 2) + 1)
    accent = {s for s, _, n, v in drums if n == SRC_SNARE and v == 127}
    hits = []
    for t in snares:
        if t // BAR in blast:
            continue
        v = 96 if t in accent else 84 if t % BEAT == 0 else 80 if t % (BEAT // 2) == 0 else 72
        hits.append((t, v))
    for bar in blast:
        for step in (1, 3):
            hits.append((bar * BAR + step * BEAT, 92))
    hits.sort()
    kept = []
    for t, v in hits:
        if kept and t - kept[-1][0] < ticks_for(t, PULSE_MIN_US):
            continue
        kept.append((t, v))
    for i, (t, v) in enumerate(kept):
        gate = ticks_for(t, PULSE_GATE_US)
        if i + 1 < len(kept):
            gate = min(gate, kept[i + 1][0] - t)
        note = roots[t // (BAR // 2)] + 12
        parts[PULSE].append([t, t + gate, note, min(127, v + wobble(t, 4))])
    return len(hits) - len(kept)


def arrange(tracks):
    src = {i: pair_notes(tracks[i]["events"]) for i in SOURCE_LAYOUT}
    end_tick = max(e for notes in src.values() for _, e, _, _ in notes)
    parts = {p: [] for p in range(len(NAMES))}

    def take(idx, part):
        for start, end, note, velocity in src[idx]:
            parts[part].append([start, end, note, velocity])

    take(1, GTR1)
    take(2, GTR2)
    take(3, BASS)
    take(4, CHOIR)    # program 52, held chords
    take(5, STRINGS)  # program 48
    dropped = kit(src, parts, end_tick)

    for part, notes in parts.items():
        notes.sort(key=lambda x: (x[2], x[0], -x[3]))
        kept = []
        for s, e, n, v in notes:
            if kept and kept[-1][2] == n and s < kept[-1][1]:
                if s == kept[-1][0]:
                    kept[-1][1] = max(kept[-1][1], e)
                    continue
                kept[-1][1] = s
            kept.append([s, e, n, v])
        parts[part] = sorted([x for x in kept if x[1] > x[0]], key=lambda x: (x[0], x[2]))
    return parts, dropped


def write_smf(path: Path, parts) -> None:
    title = b"Masses for the New Messiah Mamut"
    conductor = [(0, b"\xff\x03" + vlq(len(title)) + title),
                 (0, b"\xff\x58\x04\x04\x02\x18\x08")]
    conductor += [(t, b"\xff\x51\x03" + us.to_bytes(3, "big")) for t, us in TEMPOS]
    chunks = [track(conductor)]
    for part, notes in parts.items():
        name = NAMES[part].encode()
        events = [(0, b"\xff\x03" + vlq(len(name)) + name)]
        timed = []
        for s, e, n, v in notes:
            timed.append((s, 1, bytes((0x90 | part, n, v))))
            timed.append((e, 0, bytes((0x80 | part, n, 0))))
        timed.sort(key=lambda x: (x[0], x[1]))
        events += [(t, p) for t, _, p in timed]
        chunks.append(track(events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-i", type=Path, default=Path(
        "notes-midi/local/dimmu-borgir/dimmu-borgir-masses-for-the-new-messiah-bitmidi-78711.mid"))
    parser.add_argument("-o", type=Path, default=Path("notes-midi/local/masses-20261108.mid"))
    args = parser.parse_args()
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected BitMidi 78711 source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != SOURCE_TEMPOS or len(tracks) != 7:
        raise SystemExit("unexpected source layout")
    for idx, (name, count) in SOURCE_LAYOUT.items():
        if tracks[idx]["name"] != name or len(pair_notes(tracks[idx]["events"])) != count:
            raise SystemExit(f"source track {idx} differs from the inspected layout")
    parts, dropped = arrange(tracks)
    write_smf(args.o, parts)
    print(f"{args.o}: sha256 {hashlib.sha256(args.o.read_bytes()).hexdigest()}")
    total = 0
    for part, notes in parts.items():
        edges = sorted([(s, 1) for s, *_ in notes] + [(e, -1) for _, e, *_ in notes])
        active = peak = 0
        for _, d in edges:
            active += d
            peak = max(peak, active)
        total += len(notes)
        print(f"  ch{part} {NAMES[part]:8} {len(notes):5} notes  held peak {peak}"
              f"  range {min(n for _, _, n, _ in notes)}-{max(n for _, _, n, _ in notes)}"
              f"  bars {notes[0][0] / BAR:.2f}-{max(e for _, e, *_ in notes) / BAR:.2f}")
    print(f"  total {total} notes, {2 * total} events; {dropped} snares under 200 ms not played")


if __name__ == "__main__":
    main()
