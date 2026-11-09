#!/usr/bin/env python3
"""Write the 2026-11-08 Mamut arrangement of Orion (Metallica) as a
ten-channel SMF.

Source: the BitMidi transcription 73630
(notes-midi/local/metallica/metallica-orion-bitmidi-73630.mid): named
tracks for three solo/melody guitars, bass doubled on two tracks (identical
notes, a lead-bass and a picked-bass program), the machine-gun guitar and
its low doubling, opening chords, two chord tracks, a rhythm guitar, and a
kit split into kit, snare, hi-hat and cymbal tracks. 120 PPQ; tempo 300 BPM
over the silent first bars, then 133 BPM, 83 BPM for the middle section
(source tick 60960) and 133 BPM again (88440).

The derived MIDI is rescaled to 480 PPQ (x4, exact) and starts at the
first note: the four silent source bars (4.2 s) are dropped, so derived
bar b is source bar b + 4. Source dynamics are kept: pitched velocities
are scaled by 95/127 (the opening fade-in runs 24..68 in the source).
Each output channel is one Mamut part of driver/exhibit_ma3_orion.c.

Kit rules as in the 2026-11-08 shows (docs/ma3-mourning-palace.md): the
MA3 Hurt kick on the fifth of the key (B1, MIDI 35, the Hurt kick's own
note; the song is in E minor), derived accents, 148 ms gate, one-slot choke
pool; the snare as the pulse at the half-bar root an octave up, 401 ms gate,
200 ms minimum spacing. No bar qualifies as a blast. The source snare's own
dynamics (crescendo rolls, ghost notes) scale the pulse velocity. Toms,
hi-hat and cymbals are dropped.

Humanized as The Cry of Mankind edition 2 (driver/make_cry_of_mankind.py),
because the operator found the quantized v1 of that show too robotic:
per-part offset and triangular jitter (ms, converted at the local tempo),
upward strum on chords, metric accent, four-bar swell and scatter on
velocity, the kick gate clipped to the next moved hit. Deterministic.
Standard library only.
"""

import argparse
import hashlib
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True  # the sibling import leaves no __pycache__ in driver/
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_mourning_palace import read_smf, pair_notes, track, vlq  # noqa: E402

SOURCE_SHA256 = "98d94a8e1fedf11d5adcbccb1875f27f04f3c9bd7b7846f1309eb56845df1a0f"
SRC_PPQ, SCALE, PPQ = 120, 4, 480
BAR, BEAT = 4 * PPQ, PPQ
SOURCE_TEMPOS = [(0, 200000), (1440, 451127), (60960, 722891), (88440, 451127)]
TRIM = 1920                     # source ticks: bars 0-3, silent
TEMPOS = [(0, 451127), ((60960 - TRIM) * SCALE, 722891), ((88440 - TRIM) * SCALE, 451127)]
SOURCE_LAYOUT = {3: ("Solo 1", 558), 4: ("Solo 2", 313), 5: ("Solo 3", 171),
                 6: ("Bass (left)", 1673), 7: ("Bass (right)", 1689),
                 8: ("'Machine gun' gtr", 596), 9: ("Mach gun 2", 507),
                 10: ("Opening chords", 48), 11: ("Chords 1", 195), 12: ("Chords 2", 509),
                 13: ("Rhythm", 700), 14: ("Drums", 934), 15: ("Snare", 501)}

LEAD1, LEAD2, LEAD3, BASSL, BASSR, GTR1, GTR2, CHORDS, KICK, PULSE = range(10)
NAMES = ("lead1", "lead2", "lead3", "bassl", "bassr", "gtr1", "gtr2", "chords", "kick", "pulse")
TAKE = {LEAD1: (3,), LEAD2: (4,), LEAD3: (5,), BASSL: (6,), BASSR: (7,),
        GTR1: (10, 13), GTR2: (8, 9), CHORDS: (11, 12)}
SRC_KICKS, SRC_SNARE = (35, 36), 38
KICK_NOTE = 35            # B1 (62 Hz), the fifth of E minor
PITCHED_SCALE = 95 / 127

# Humanization: (offset ms, jitter sd ms, velocity scatter) per part.
HUMAN = {LEAD1: (0, 7, 7), LEAD2: (0, 7, 7), LEAD3: (0, 7, 7), BASSL: (4, 5, 5),
         BASSR: (4, 5, 5), GTR1: (2, 6, 6), GTR2: (1, 4, 5), CHORDS: (2, 6, 6),
         KICK: (0, 3, 5), PULSE: (3, 5, 6)}
STRUM_MS = {GTR1: 7.0, CHORDS: 7.0}


def us_per_quarter(tick: int) -> int:
    us = TEMPOS[0][1]
    for at, value in TEMPOS:
        if at <= tick:
            us = value
    return us


def ticks_for(tick: int, us: float) -> int:
    return round(us * PPQ / us_per_quarter(tick))


def mix64(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return x ^ (x >> 31)


def triangular(*key: int) -> float:
    """Deterministic triangular variate in (-1, 1)."""
    h = 0x4F52494F  # "ORIO"
    for k in key:
        h = mix64(h ^ (k & 0xFFFFFFFFFFFFFFFF))
    return (h & 0xFFFFFFFF) / 2**32 + (h >> 32) / 2**32 - 1.0


def half_bar_roots(src, halves):
    """The MA3 Hurt rule: lowest bass attack, else lowest guitar attack,
    else the previous root, folded into MIDI 31..42."""
    bass, low = [0] * halves, [0] * halves
    for notes, table in ((src[7], bass), (src[13] + src[8], low)):
        for start, _, note, _ in notes:
            h = start // (BAR // 2)
            if 0 <= h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 40
    for h in range(halves):
        root = bass[h] or low[h] or previous
        while root > 42:
            root -= 12
        while root < 31:
            root += 12
        roots.append(root)
        previous = root
    return roots


def dedupe(notes):
    notes.sort(key=lambda x: (x[2], x[0], -x[3]))
    kept = []
    for s, e, n, v in notes:
        if kept and kept[-1][2] == n and s < kept[-1][1]:
            if s == kept[-1][0]:
                kept[-1][1] = max(kept[-1][1], e)
                continue
            kept[-1][1] = s
        kept.append([s, e, n, v])
    return sorted([x for x in kept if x[1] > x[0]], key=lambda x: (x[0], x[2]))


def kit(src, parts, end_tick):
    kicks = sorted({s for s, _, n, _ in src[14] if n in SRC_KICKS})
    snare_vel = {}
    for s, _, n, v in src[15]:
        if n == SRC_SNARE:
            snare_vel[s] = max(v, snare_vel.get(s, 0))
    snares = sorted(snare_vel)
    count = {}
    for s in kicks:
        count.setdefault(s // BAR, [0, 0])[0] += 1
    for s in snares:
        count.setdefault(s // BAR, [0, 0])[1] += 1
    if any(sn >= 8 and k >= 8 for k, sn in count.values()):
        raise SystemExit("a blast bar appeared; the inspected source has none")
    run_gap = 0
    for i, t in enumerate(kicks):
        prev_gap = t - kicks[i - 1] if i else None
        next_gap = kicks[i + 1] - t if i + 1 < len(kicks) else None
        run_gap = ticks_for(t, 166_667)
        run = (prev_gap is not None and prev_gap <= run_gap) or (next_gap is not None and next_gap <= run_gap)
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
        gate = ticks_for(t, 148_133)
        if next_gap is not None:
            gate = min(gate, next_gap)
        parts[KICK].append([t, t + gate, KICK_NOTE, v])
    roots = half_bar_roots(src, end_tick // (BAR // 2) + 1)
    kept = []
    for t in snares:
        if kept and t - kept[-1] < ticks_for(t, 200_000):
            continue
        kept.append(t)
    for i, t in enumerate(kept):
        base = 84 if t % BEAT == 0 else 80 if t % (BEAT // 2) == 0 else 72
        v = max(1, round(base * snare_vel[t] / 127))
        gate = ticks_for(t, 401_167)
        if i + 1 < len(kept):
            gate = min(gate, kept[i + 1] - t)
        parts[PULSE].append([t, t + gate, roots[t // (BAR // 2)] + 12, v])
    return len(snares) - len(kept)


def humanize(parts):
    out = {}
    for part, notes in parts.items():
        offset, sd, scatter = HUMAN[part]
        chords = {}
        for s, _, n, _ in notes:
            chords.setdefault(s, []).append(n)
        moved = []
        for s, e, n, v in notes:
            ms = offset + sd * 2.449 * triangular(part, s, n, 1)
            if part in STRUM_MS and len(chords[s]) > 1:
                ms += STRUM_MS[part] * sorted(chords[s]).index(n)
            shift = ticks_for(s, ms * 1000)
            if part in (KICK, PULSE):
                vel = v
            else:
                accent = 6 if s % BAR == 0 else 2 if s % BEAT == 0 else -2 if s % (BEAT // 2) == 0 else -4
                vel = v + accent + 4.0 * math.sin(2 * math.pi * s / (4 * BAR))
            vel = round(vel + scatter * triangular(part, s, n, 2))
            start = max(0, s + shift)
            moved.append([start, max(start + 1, e + shift), n, max(1, min(127, vel))])
        kept = dedupe(moved)
        if part == KICK:
            for i in range(len(kept) - 1):
                kept[i][1] = min(kept[i][1], kept[i + 1][0])
            kept = [x for x in kept if x[1] > x[0]]
        out[part] = kept
    return out


def arrange(tracks):
    src = {}
    for i in SOURCE_LAYOUT:
        src[i] = [[(s - TRIM) * SCALE, (e - TRIM) * SCALE, n, v]
                  for s, e, n, v in pair_notes(tracks[i]["events"])]
        if any(s < 0 for s, *_ in src[i]):
            raise SystemExit("a note starts in the trimmed silent bars")
    end_tick = max(e for notes in src.values() for _, e, _, _ in notes)
    parts = {p: [] for p in range(len(NAMES))}
    for part, idxs in TAKE.items():
        for idx in idxs:
            for s, e, n, v in src[idx]:
                parts[part].append([s, e, n, max(1, round(v * PITCHED_SCALE))])
    dropped = kit(src, parts, end_tick)
    parts = {p: dedupe(notes) for p, notes in parts.items()}
    return humanize(parts), dropped


def write_smf(path: Path, parts) -> None:
    title = b"Orion Mamut"
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
    parser.add_argument("-i", type=Path, default=Path("notes-midi/local/metallica/metallica-orion-bitmidi-73630.mid"))
    parser.add_argument("-o", type=Path, default=Path("notes-midi/local/orion-20261108.mid"))
    args = parser.parse_args()
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected BitMidi 73630 source")
    fmt, ppq, tracks = read_smf(data)
    tempos = sorted(x for t in tracks for x in t["tempos"])
    if fmt != 1 or ppq != SRC_PPQ or tempos != SOURCE_TEMPOS or len(tracks) != 32:
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
        print(f"  ch{part} {NAMES[part]:7} {len(notes):5} notes  held peak {peak}"
              f"  range {min(n for _, _, n, _ in notes)}-{max(n for _, _, n, _ in notes)}"
              f"  vel {min(v for *_, v in notes)}-{max(v for *_, v in notes)}"
              f"  bars {notes[0][0] / BAR:.2f}-{max(e for _, e, *_ in notes) / BAR:.2f}")
    print(f"  total {total} notes, {2 * total} events; {dropped} snares under 200 ms not played;"
          f" last note-off tick {max(e for n in parts.values() for _, e, _, _ in n)}")


if __name__ == "__main__":
    main()
