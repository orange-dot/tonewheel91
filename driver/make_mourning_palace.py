#!/usr/bin/env python3
"""Write the 2026-11-08 Mamut arrangement of Mourning Palace (Dimmu Borgir)
as a nine-channel SMF.

Source: the full-band BitMidi transcription 39544
(notes-midi/local/dimmu-borgir/dimmu-borgir-mourning_palace-bitmidi-39544.mid):
two guitars, bass, four keyboard tracks and a kit, 480 PPQ, a four-entry
tempo map (120, 195, 120, 180 BPM) and flat velocity 95 on every pitched
note. Each output channel is one Mamut part of
driver/exhibit_ma3_mourning_palace.c; the tempo map is copied unchanged.

The kit becomes the MA3 Hurt pair (driver/ma3_hurt_material.h): a tonal
kick and a pitched pulse. The source kick and snare keep their rhythm; toms,
cymbals and hats are dropped. Accents are derived, because the source has
none:
  - kick: the Hurt accents (113 on the one, 94 elsewhere) where hits are
    spaced; in double-kick runs (a neighbour within 160 ticks) the one, the
    beats and the eighths are lifted over the in-between hits;
  - kick gate: the Hurt kick's 148 ms, or up to the next hit; the exhibit
    plays the kick on a one-slot pool, so each hit chokes the previous one;
  - pulse: each source snare, at the half-bar root an octave up (the Hurt
    rule); in the blast bars, where the snare alternates with the kick on
    sixteenths, the pulse takes the backbeat (beats 2 and 4) instead;
  - pulse gate: the Hurt pulse's 401 ms, or up to the next pulse.
Standard library only.
"""

import argparse
import hashlib
from pathlib import Path
import struct

SOURCE_SHA256 = "7b5b3ac723a862683c038ef494d81cf73e3229bab00b6493e42a7f268b7e7f16"
PPQ, BAR, BEAT = 480, 1920, 480
TEMPOS = [(0, 500000), (178080, 307692), (193440, 500000), (224160, 333333)]
# Source tracks by index (track 0 is the conductor).
SOURCE_LAYOUT = {1: ("Guitar I", 2257), 2: ("Guitar II", 2977), 3: ("Bass", 907),
                 4: ("Keyboards", 229), 5: ("Keyboards", 334), 6: ("Keyboards", 184),
                 7: ("Keyboards", 441), 8: ("Drums", 3327)}

GTR1, GTR2, BASS, SYNTH, STRINGS, PAD, PIZZ, KICK, PULSE = range(9)
NAMES = ("gtr1", "gtr2", "bass", "synth", "strings", "pad", "pizz", "kick", "pulse")
SRC_KICK, SRC_SNARE = 36, 38
KICK_NOTE = 37          # C#2 (69 Hz), the fifth of F# minor (the Hurt kick sat on B1)
KICK_GATE_US = 148_133  # 96 ticks at the Hurt tempo, 740740 us per quarter
PULSE_GATE_US = 401_167  # 260 ticks at the Hurt tempo
RUN_GAP = 160           # ticks; sixteenths at any tempo here, and sextuplets


def read_smf(data: bytes):
    if data[:4] != b"MThd":
        raise ValueError("not an SMF file")
    fmt, count, ppq = struct.unpack(">HHH", data[8:14])
    pos = 8 + int.from_bytes(data[4:8], "big")
    tracks = []
    for _ in range(count):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("missing track chunk")
        size = int.from_bytes(data[pos + 4:pos + 8], "big")
        b = data[pos + 8:pos + 8 + size]
        pos += 8 + size
        i = tick = running = 0
        name, tempos, events = "", [], []

        def vlq():
            nonlocal i
            n = 0
            while True:
                v = b[i]
                i += 1
                n = (n << 7) | (v & 127)
                if v < 128:
                    return n

        while i < len(b):
            tick += vlq()
            status = b[i]
            if status >= 128:
                i += 1
            else:
                status = running
            if status == 0xFF:
                kind = b[i]
                i += 1
                length = vlq()
                payload = b[i:i + length]
                i += length
                if kind == 3 and not name:
                    name = payload.decode("latin1")
                if kind == 0x51:
                    tempos.append((tick, int.from_bytes(payload, "big")))
            elif status in (0xF0, 0xF7):
                i += vlq()
            else:
                running = status
                kind, n = status >> 4, 1 if status >> 4 in (12, 13) else 2
                d = b[i:i + n]
                i += n
                if kind == 9 and d[1]:
                    events.append((tick, d[0], d[1], True))
                elif kind == 8 or kind == 9:
                    events.append((tick, d[0], 0, False))
        tracks.append({"name": name, "tempos": tempos, "events": events})
    return fmt, ppq, tracks


def pair_notes(events):
    """FIFO pairing per pitch."""
    held, notes = {}, []
    for tick, note, velocity, on in events:
        if on:
            held.setdefault(note, []).append((tick, velocity))
        elif held.get(note):
            start, velocity = held[note].pop(0)
            notes.append([start, tick, note, velocity])
    if any(held.values()):
        raise ValueError("unpaired note-on in source")
    return notes


def us_per_quarter(tick: int) -> int:
    us = TEMPOS[0][1]
    for at, value in TEMPOS:
        if at <= tick:
            us = value
    return us


def gate_ticks(tick: int, us: int) -> int:
    """Ticks spanning `us` microseconds at the tempo in force at `tick`."""
    return us * PPQ // us_per_quarter(tick)


def half_bar_roots(src, halves):
    """Half-bar harmony root as the MA3 Hurt material derives it: the lowest
    bass attack, else the lowest guitar attack, else the previous root,
    folded into MIDI 31..42."""
    bass, low = [0] * halves, [0] * halves
    for idx, table in ((3, bass), (1, low), (2, low)):
        for start, _, note, _ in src[idx]:
            h = start // (BAR // 2)
            if h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 42
    for h in range(halves):
        root = bass[h] or low[h] or previous
        while root > 42:
            root -= 12
        while root < 31:
            root += 12
        roots.append(root)
        previous = root
    return roots


def wobble(tick: int, salt: int) -> int:
    """Deterministic +-3 velocity scatter on the sixteenth grid."""
    return ((tick // 40) * 7 + salt) % 7 - 3


def kit(src, parts, end_tick):
    drums = src[8]
    kicks = sorted({s for s, _, n, _ in drums if n == SRC_KICK})
    snares = sorted({s for s, _, n, _ in drums if n == SRC_SNARE})
    per_bar = {}
    for s in snares:
        per_bar[s // BAR] = per_bar.get(s // BAR, 0) + 1
    blast = sorted(b for b, k in per_bar.items() if k >= 7)  # bar 180 ends on its 7th
    if blast != list(range(165, 181)):
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
        gate = gate_ticks(t, KICK_GATE_US)
        if next_gap is not None:
            gate = min(gate, next_gap)
        parts[KICK].append([t, t + gate, KICK_NOTE, v])

    roots = half_bar_roots(src, end_tick // (BAR // 2) + 1)
    hits = []
    accent = {s for s, _, n, v in drums if n == SRC_SNARE and v == 127}
    for t in snares:
        if t // BAR in blast:
            continue
        if t in accent:
            v = 96
        elif t % BEAT == 0:
            v = 84
        elif t % (BEAT // 2) == 0:
            v = 80
        else:
            v = 72
        hits.append((t, v))
    for bar in blast:
        for step in (1, 3):
            hits.append((bar * BAR + step * BEAT, 92))
    hits.sort()
    for i, (t, v) in enumerate(hits):
        gate = gate_ticks(t, PULSE_GATE_US)
        if i + 1 < len(hits):
            gate = min(gate, hits[i + 1][0] - t)
        note = roots[t // (BAR // 2)] + 12
        parts[PULSE].append([t, t + gate, note, min(127, v + wobble(t, 4))])


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
    take(4, SYNTH)     # program 82 lead, doubling the low strings
    take(5, STRINGS)   # program 48 low ensemble
    take(6, PAD)       # program 48 held chords
    take(7, PIZZ)      # program 45 keyboard figure on sixteenths
    kit(src, parts, end_tick)

    for part, notes in parts.items():
        # One voice per pitch inside a part: a re-strike ends the previous
        # note, and an exact duplicate keeps the louder velocity.
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
    return parts


def vlq(n: int) -> bytes:
    out = [n & 127]
    n >>= 7
    while n:
        out.append(128 | (n & 127))
        n >>= 7
    return bytes(reversed(out))


def track(events) -> bytes:
    body, last = bytearray(), 0
    for tick, payload in events:
        body += vlq(tick - last) + payload
        last = tick
    body += vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def write_smf(path: Path, parts) -> None:
    title = b"Mourning Palace Mamut"
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
        timed.sort(key=lambda x: (x[0], x[1]))  # releases before attacks
        events += [(t, p) for t, _, p in timed]
        chunks.append(track(events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-i", type=Path,
                        default=Path("notes-midi/local/dimmu-borgir/dimmu-borgir-mourning_palace-bitmidi-39544.mid"))
    parser.add_argument("-o", type=Path, default=Path("notes-midi/local/mourning-palace-20261108.mid"))
    args = parser.parse_args()
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected BitMidi 39544 source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != TEMPOS or len(tracks) != 9:
        raise SystemExit("unexpected source layout")
    for idx, (name, count) in SOURCE_LAYOUT.items():
        if tracks[idx]["name"] != name or len(pair_notes(tracks[idx]["events"])) != count:
            raise SystemExit(f"source track {idx} differs from the inspected layout")
    parts = arrange(tracks)
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
        lo = min(n for _, _, n, _ in notes)
        hi = max(n for _, _, n, _ in notes)
        print(f"  ch{part} {NAMES[part]:8} {len(notes):5} notes  held peak {peak}"
              f"  range {lo}-{hi}  bars {notes[0][0] / BAR:.2f}-{max(e for _, e, *_ in notes) / BAR:.2f}")
    print(f"  total {total} notes, {2 * total} events")


if __name__ == "__main__":
    main()
