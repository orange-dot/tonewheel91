#!/usr/bin/env python3
"""Write the 2026-11-08 Mamut arrangement of The Cry of Mankind (My Dying
Bride) as a seven-channel SMF.

Source: the FreeMidi transcription 20724
(notes-midi/local/my-dying-bride/my-dying-bride-the-cry-of-mankind-freemidi-20724.mid):
unnamed tracks for a rhythm guitar, the high guitar line that carries the
motif through the song, bass, piano and a kit; 960 PPQ, one tempo
(600000 us, 100 BPM), 198 bars. Pitched notes are a flat 95 and keep it.
The derived MIDI keeps 960 PPQ. Each output channel is one Mamut part of
driver/exhibit_ma3_cry_of_mankind.c.

Kit rules as in driver/make_spellbound.py (the 2026-11-08 kit): the MA3
Hurt kick on the fifth of the key (G#1, MIDI 32, 52 Hz; the song is in C#
minor) with derived accents, a 148 ms gate and a one-slot choke pool; the
pulse at the half-bar root an octave up for each snare, with the 401 ms
gate; a pulse closer than 200 ms to the previous kept one is not played.
No bar qualifies as a blast. One change for this song: bars 20-35 and
160-167 carry their groove on the toms (eleven hits a bar against one
snare), so the toms are played instead of being dropped: on the pulse voice
in a channel of their own (balanced separately in the exhibit), quieter
than the snare, the floor toms (41, 43) on the half-bar root and the rack
toms (45, 47) on its fifth. Snare and toms are thinned as one stream by the
200 ms rule. Cymbals and hats are dropped.

Edition 2 (--edition 2, operator: "too robotic") humanizes the edition-1
arrangement, deterministically (a fixed-seed hash per note, no global RNG):
  - timing: every note moves by a per-part offset plus a triangular jitter
    (standard deviation per part, 3 ms on the kick to 7 ms on the lead);
    the note keeps its length. Chords strum upward in pitch order, 7 ms per
    string on the guitar and a 3 ms roll on the piano;
  - velocity: pitched parts get a metric accent (+6 on the one, +2 on other
    beats, -2 on offbeat eighths, -4 elsewhere), a four-bar swell of +-4 and
    a triangular scatter; the kit keeps its derived accents and gets a
    triangular scatter on top;
  - the kick's gate is clipped to the next (moved) hit, so the one-slot
    choke still holds; same-pitch overlaps are resolved as in edition 1.
Edition 3 (--edition 3, operator: the octave colour of bars 93-99, where
the source guitar line plays in octaves, on the guitar for the whole song)
is edition 2 with every guitar note doubled an octave up wherever the source
has no octave partner at the same tick. The added note goes through the same
humanization, so it gets its own timing and velocity.
Editions 1 and 2 are unchanged. Standard library only.
"""

import argparse
import hashlib
from pathlib import Path
import math
import struct
import sys

sys.dont_write_bytecode = True  # the sibling import leaves no __pycache__ in driver/
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_mourning_palace import read_smf, pair_notes, track, vlq  # noqa: E402

SOURCE_SHA256 = "fece83fc12ab9ce2c9dcb1b7c635e56ac24a8772b02c65a3253d18171b4fe8a3"
PPQ = 960
BAR, BEAT = 4 * PPQ, PPQ
US_PER_QUARTER = 600000
SOURCE_NOTES = {1: 376, 2: 1188, 3: 1384, 4: 624, 5: 2262}

GTR, LEAD, BASS, PIANO, KICK, PULSE, TOMS = range(7)
NAMES = ("gtr", "lead", "bass", "piano", "kick", "pulse", "toms")
SRC_KICK, SRC_SNARE = 35, 40
LOW_TOMS, HIGH_TOMS = (41, 43), (45, 47)
KICK_NOTE = 32            # G#1 (52 Hz), the fifth of C# minor


def ticks(us: int) -> int:
    return us * PPQ // US_PER_QUARTER


KICK_GATE, PULSE_GATE, PULSE_MIN = ticks(148_133), ticks(401_167), ticks(200_000)
RUN_GAP = ticks(166_667)  # the 160 ticks at 120 BPM of the other shows


def wobble(tick: int, salt: int) -> int:
    """Deterministic +-3 velocity scatter on the sixteenth grid."""
    return ((tick // (PPQ // 4)) * 7 + salt) % 7 - 3


def half_bar_roots(src, halves):
    """The MA3 Hurt rule: lowest bass attack, else lowest guitar attack,
    else the previous root, folded into MIDI 31..42."""
    bass, low = [0] * halves, [0] * halves
    for idx, table in ((3, bass), (1, low)):
        for start, _, note, _ in src[idx]:
            h = start // (BAR // 2)
            if h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 37
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
    drums = src[5]
    kicks = sorted({s for s, _, n, _ in drums if n == SRC_KICK})
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
        gate = KICK_GATE if next_gap is None else min(KICK_GATE, next_gap)
        parts[KICK].append([t, t + gate, KICK_NOTE, v])

    roots = half_bar_roots(src, end_tick // (BAR // 2) + 1)
    hits = {}
    for s, _, n, _ in drums:
        if n == SRC_SNARE:
            v = 84 if s % BEAT == 0 else 80 if s % (BEAT // 2) == 0 else 72
            hits[s] = max(hits.get(s, (0, 0)), (2, v))   # (kind, velocity): snare wins a tie
        elif n in LOW_TOMS or n in HIGH_TOMS:
            kind = 1 if n in HIGH_TOMS else 0
            hits[s] = max(hits.get(s, (0, 0)), (kind, 66))
    kept = []
    for t in sorted(hits):
        if kept and t - kept[-1][0] < PULSE_MIN:
            continue
        kept.append((t, *hits[t]))
    for i, (t, kind, v) in enumerate(kept):
        gate = PULSE_GATE if i + 1 == len(kept) else min(PULSE_GATE, kept[i + 1][0] - t)
        root = roots[t // (BAR // 2)]
        note = root + (0, 7, 12)[kind]   # floor tom, rack tom, snare
        parts[PULSE if kind == 2 else TOMS].append([t, t + gate, note, min(127, v + wobble(t, 4))])
    return len(hits) - len(kept)


def arrange(tracks):
    src = {i: pair_notes(tracks[i]["events"]) for i in SOURCE_NOTES}
    end_tick = max(e for notes in src.values() for _, e, _, _ in notes)
    parts = {p: [] for p in range(len(NAMES))}
    for idx, part in ((1, GTR), (2, LEAD), (3, BASS), (4, PIANO)):
        for start, end, note, velocity in src[idx]:
            parts[part].append([start, end, note, velocity])
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
    title = b"The Cry of Mankind Mamut"
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


# Edition 2 humanization: (offset ms, jitter sd ms, velocity scatter) per part.
HUMAN = {GTR: (2, 6, 6), LEAD: (0, 7, 7), BASS: (4, 5, 5), PIANO: (-1, 6, 7),
         KICK: (0, 3, 5), PULSE: (3, 5, 6), TOMS: (2, 6, 6)}
STRUM_MS = {GTR: 7.0, PIANO: 3.0}
TICKS_PER_MS = PPQ * 1000 / US_PER_QUARTER    # 1.6 at 100 BPM


def mix64(x: int) -> int:
    """splitmix64 finalizer."""
    x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return x ^ (x >> 31)


def triangular(*key: int) -> float:
    """Deterministic triangular variate in (-1, 1), unit peak, sd 1/sqrt(6)."""
    h = 0x43525931  # "CRY1"
    for k in key:
        h = mix64(h ^ (k & 0xFFFFFFFFFFFFFFFF))
    a = (h & 0xFFFFFFFF) / 2**32
    b = (h >> 32) / 2**32
    return a + b - 1.0


def humanize(parts):
    out = {}
    for part, notes in parts.items():
        offset, sd, scatter = HUMAN[part]
        chords = {}
        for s, e, n, v in notes:
            chords.setdefault(s, []).append(n)
        moved = []
        for s, e, n, v in notes:
            ms = offset + sd * 2.449 * triangular(part, s, n, 1)   # sd * sqrt(6)
            if part in STRUM_MS and len(chords[s]) > 1:
                ms += STRUM_MS[part] * sorted(chords[s]).index(n)
            shift = round(ms * TICKS_PER_MS)
            if part in (KICK, PULSE, TOMS):
                vel = v
            else:
                bar_pos = s % BAR
                accent = 6 if bar_pos == 0 else 2 if s % BEAT == 0 else -2 if s % (BEAT // 2) == 0 else -4
                swell = 4.0 * math.sin(2 * math.pi * s / (4 * BAR))
                vel = v + accent + swell
            vel = round(vel + scatter * triangular(part, s, n, 2))
            start = max(0, s + shift)
            moved.append([start, max(start + 1, e + shift), n, max(1, min(127, vel))])
        moved.sort(key=lambda x: (x[2], x[0], -x[3]))
        kept = []
        for s, e, n, v in moved:
            if kept and kept[-1][2] == n and s < kept[-1][1]:
                if s == kept[-1][0]:
                    kept[-1][1] = max(kept[-1][1], e)
                    continue
                kept[-1][1] = s
            kept.append([s, e, n, v])
        kept = sorted([x for x in kept if x[1] > x[0]], key=lambda x: (x[0], x[2]))
        if part == KICK:
            for i in range(len(kept) - 1):
                kept[i][1] = min(kept[i][1], kept[i + 1][0])
            kept = [x for x in kept if x[1] > x[0]]
        out[part] = kept
    return out


def octave_double(notes):
    """Add the octave above each guitar note that has no octave partner
    starting at the same tick (edition 3)."""
    starts = {(s, n) for s, _, n, _ in notes}
    added = [[s, e, n + 12, v] for s, e, n, v in notes
             if (s, n + 12) not in starts and (s, n - 12) not in starts and n + 12 <= 127]
    return sorted(notes + added, key=lambda x: (x[0], x[2]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-i", type=Path, default=Path(
        "notes-midi/local/my-dying-bride/my-dying-bride-the-cry-of-mankind-freemidi-20724.mid"))
    parser.add_argument("-o", type=Path)
    parser.add_argument("--edition", type=int, choices=(1, 2, 3), default=1)
    args = parser.parse_args()
    if args.o is None:
        args.o = Path({1: "notes-midi/local/cry-of-mankind-20261108.mid",
                       2: "notes-midi/local/cry-of-mankind-20261108-v2.mid",
                       3: "notes-midi/local/cry-of-mankind-20261108-v3.mid"}[args.edition])
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected FreeMidi 20724 source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != [(0, US_PER_QUARTER)] or len(tracks) != 6:
        raise SystemExit("unexpected source layout")
    for idx, count in SOURCE_NOTES.items():
        if len(pair_notes(tracks[idx]["events"])) != count:
            raise SystemExit(f"source track {idx} differs from the inspected layout")
    parts, dropped = arrange(tracks)
    if args.edition == 3:
        parts[GTR] = octave_double(parts[GTR])
    if args.edition >= 2:
        parts = humanize(parts)
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
        print(f"  ch{part} {NAMES[part]:6} {len(notes):5} notes  held peak {peak}"
              f"  range {min(n for _, _, n, _ in notes)}-{max(n for _, _, n, _ in notes)}"
              f"  bars {notes[0][0] / BAR:.2f}-{max(e for _, e, *_ in notes) / BAR:.2f}")
    print(f"  total {total} notes, {2 * total} events; {dropped} snare/tom hits under 200 ms not played")


if __name__ == "__main__":
    main()
