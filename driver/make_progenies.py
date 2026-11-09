#!/usr/bin/env python3
"""Write the 2026-11-08 Mamut arrangement of Progenies of the Great
Apocalypse (Dimmu Borgir) as an eight-channel SMF.

Source: the full-band BitMidi transcription 39536
(notes-midi/local/dimmu-borgir/dimmu-borgir-progenies-of-the-great-apocalypse-2-bitmidi-39536.mid):
tracks named after the players (Galder, Silenoz: guitars; Vortex: bass;
four Mustis keyboard tracks; Nick Barker: kit), 480 PPQ, one tempo
(428571 us, 140 BPM). The two guitar tracks are note-for-note identical, as
are the first two keyboard tracks. Velocity is a flat 127 everywhere.
Each output channel is one Mamut part of driver/exhibit_ma3_progenies.c.

Shared rules with driver/make_mourning_palace.py (the 2026-11-08 kit):
  - pitched parts: the flat source velocity 127 is played at 95, the level
    the Mourning Palace patches were balanced at;
  - kick: source kick (35) on the MA3 Hurt kick at the fifth of the key
    (A#1, MIDI 34, 58 Hz; the song is in D# minor), Hurt accents on spaced hits,
    lifted one/beats/eighths in runs, 148 ms gate or up to the next hit,
    one-slot choke pool in the exhibit;
  - pulse: each source snare (40) at the half-bar root an octave up, with
    the 401 ms Hurt gate or up to the next pulse. In blast bars (seven or
    more snares over sixteen or more kicks) the pulse takes beats 2 and 4.
    Elsewhere a snare closer than 200 ms to the previous kept pulse is not
    played (the fills in bars 39, 71 and 91 run at 60..160 ticks).
Toms, cymbals and hats are dropped. Standard library only.
"""

import argparse
import hashlib
from pathlib import Path
import sys

sys.dont_write_bytecode = True  # the sibling import leaves no __pycache__ in driver/
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_mourning_palace import read_smf, pair_notes, track, vlq, wobble  # noqa: E402

SOURCE_SHA256 = "8f1b9b923e2e5c1838ab9e6e385cdb4b9c870cf1473302d25e6f69d9143c5b09"
PPQ, BAR, BEAT = 480, 1920, 480
US_PER_QUARTER = 428571
SOURCE_LAYOUT = {1: ("Galder", 2504), 2: ("Silenoz", 2504), 3: ("Vortex", 907),
                 4: ("Mustis", 287), 5: ("Mustis", 287), 6: ("Mustis", 226),
                 7: ("Mustis", 26), 8: ("Nick Barker", 2853)}

GTR1, GTR2, BASS, STRINGS, BRASS, CHOIR, KICK, PULSE = range(8)
NAMES = ("gtr1", "gtr2", "bass", "strings", "brass", "choir", "kick", "pulse")
SRC_KICK, SRC_SNARE = 35, 40
KICK_NOTE = 34           # A#1 (58 Hz), the fifth of D# minor
PITCHED_VELOCITY = 95
KICK_GATE_TICKS = 148_133 * PPQ // US_PER_QUARTER   # the Hurt kick's 148 ms
PULSE_GATE_TICKS = 401_167 * PPQ // US_PER_QUARTER  # the Hurt pulse's 401 ms
PULSE_MIN_TICKS = 200_000 * PPQ // US_PER_QUARTER   # 200 ms
RUN_GAP = 160


def half_bar_roots(src, halves):
    """The MA3 Hurt rule: lowest bass attack, else lowest guitar attack,
    else the previous root, folded into MIDI 31..42."""
    bass, low = [0] * halves, [0] * halves
    for idx, table in ((3, bass), (1, low)):
        for start, _, note, _ in src[idx]:
            h = start // (BAR // 2)
            if h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 39
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
    drums = src[8]
    kicks = sorted({s for s, _, n, _ in drums if n == SRC_KICK})
    snares = sorted({s for s, _, n, _ in drums if n == SRC_SNARE})
    count = {}
    for s in kicks:
        count.setdefault(s // BAR, [0, 0])[0] += 1
    for s in snares:
        count.setdefault(s // BAR, [0, 0])[1] += 1
    blast = sorted(b for b, (k, sn) in count.items() if sn >= 7 and k >= 16)
    if blast != [129, 130, 137, 138, 139, 140, 141, 142]:
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
        gate = KICK_GATE_TICKS if next_gap is None else min(KICK_GATE_TICKS, next_gap)
        parts[KICK].append([t, t + gate, KICK_NOTE, v])

    roots = half_bar_roots(src, end_tick // (BAR // 2) + 1)
    hits = []
    for t in snares:
        if t // BAR in blast:
            continue
        v = 84 if t % BEAT == 0 else 80 if t % (BEAT // 2) == 0 else 72
        hits.append((t, v))
    for bar in blast:
        for step in (1, 3):
            hits.append((bar * BAR + step * BEAT, 92))
    hits.sort()
    kept = []
    for t, v in hits:
        if kept and t - kept[-1][0] < PULSE_MIN_TICKS:
            continue
        kept.append((t, v))
    for i, (t, v) in enumerate(kept):
        gate = PULSE_GATE_TICKS
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
        for start, end, note, _ in src[idx]:
            parts[part].append([start, end, note, PITCHED_VELOCITY])

    take(1, GTR1)
    take(2, GTR2)
    take(3, BASS)
    take(4, STRINGS)   # track 5 is note-for-note the same
    take(6, BRASS)     # program 61
    take(7, CHOIR)     # program 52, held
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
    import struct
    title = b"Progenies Mamut"
    conductor = [(0, b"\xff\x03" + vlq(len(title)) + title),
                 (0, b"\xff\x58\x04\x04\x02\x18\x08"),
                 (0, b"\xff\x51\x03" + US_PER_QUARTER.to_bytes(3, "big"))]
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
        "notes-midi/local/dimmu-borgir/dimmu-borgir-progenies-of-the-great-apocalypse-2-bitmidi-39536.mid"))
    parser.add_argument("-o", type=Path, default=Path("notes-midi/local/progenies-20261108.mid"))
    args = parser.parse_args()
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected BitMidi 39536 source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != [(0, US_PER_QUARTER)] or len(tracks) != 9:
        raise SystemExit("unexpected source layout")
    for idx, (name, count) in SOURCE_LAYOUT.items():
        if tracks[idx]["name"] != name or len(pair_notes(tracks[idx]["events"])) != count:
            raise SystemExit(f"source track {idx} differs from the inspected layout")
    if tracks[1]["events"] != tracks[2]["events"] or tracks[4]["events"] != tracks[5]["events"]:
        raise SystemExit("doubled tracks differ from the inspected source")
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
    print(f"  total {total} notes, {2 * total} events; {dropped} fill snares under 200 ms not played")


if __name__ == "__main__":
    main()
