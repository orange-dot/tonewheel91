#!/usr/bin/env python3
"""Write the industrial Hurt score (2026-11-07) as a ten-channel SMF.

The base is the original performance as transcribed in the Songparts file
(notes-midi/local/hurt-songparts-vocals.mid): its guitars, bass, piano, pad
synths and lead vocal keep their notes, durations and form. The FX intro
(bars 0-7) and the climax lead synth are left out; the form starts at source
bar 8. The source toms keep their rhythm as Orbita metal hits, and an
industrial kit (kick, snare, hat) follows the original sections. Each output
channel is one part of driver/exhibit_ma3_hurt_industrial.c. Standard
library only.
"""

import argparse
import hashlib
from pathlib import Path
import struct

SOURCE_SHA256 = "1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0"
PPQ, BAR, STEP = 480, 1920, 120
US_PER_QUARTER = 740740
START_BAR, END_BAR = 8, 100
ARP, KEYS, BASS, VOCAL, PAD, GTR, KICK, SNARE, HAT, METAL = range(10)
NAMES = ("arp", "keys", "bass", "vocal", "pad", "gtr", "kick", "snare", "hat", "metal")
KICK_NOTE, SNARE_NOTE, HAT_NOTE = 35, 52, 81
TOM_TO_METAL = {41: 50, 45: 55, 47: 59}   # floor, mid, high tom -> metal size
METAL_NOTE = 57


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
                if kind == 3:
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
                elif kind in (8, 9):
                    events.append((tick, d[0], 0, False))
        tracks.append({"name": name, "tempos": tempos, "events": events})
    return fmt, ppq, tracks


def pair_notes(events):
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


def section(bar: int) -> str:
    """The original form, in source bars."""
    if bar < 16: return "verse1a"
    if bar < 24: return "verse1b"
    if bar < 40: return "build"
    if bar < 48: return "peak"
    if bar < 68: return "verse2"
    if bar < 76: return "chorus2"
    if bar < 91: return "climax"
    if bar == 91: return "hit"
    return "ending"


# Kick / snare / hat steps (16th grid) and base velocities per section.
KIT = {
    "verse1a": ((0,),                    (),        (),                     96, 0, 0),
    "verse1b": ((0, 8),                  (),        (2, 6, 10, 14),         100, 0, 52),
    "build":   ((0, 6, 8),               (4, 12),   (0, 2, 4, 6, 8, 10, 12, 14), 108, 98, 60),
    "peak":    ((0, 6, 8, 11),           (4, 12),   tuple(range(16)),       118, 112, 66),
    "verse2":  ((0, 10),                 (8,),      (),                     100, 96, 0),
    "chorus2": ((0, 6, 8, 10),           (4, 12),   (0, 2, 4, 6, 8, 10, 12, 14), 116, 110, 64),
    "climax":  ((0, 3, 6, 8, 11, 14),    (4, 12),   tuple(range(16)),       124, 120, 72),
}


def kit(parts) -> None:
    for bar in range(START_BAR, 92):
        at, name = bar * BAR, section(bar)
        if name == "hit":
            parts[KICK].append([at, at + 240, KICK_NOTE, 127])
            parts[SNARE].append([at, at + 240, SNARE_NOTE, 127])
            parts[METAL].append([at, at + 480, METAL_NOTE, 127])
            continue
        kicks, snares, hats, kv, sv, hv = KIT[name]
        for step in kicks:
            v = kv + (6 if step == 0 else 0) + (bar * 7 + step * 3) % 7 - 3
            parts[KICK].append([at + step * STEP, at + step * STEP + 96, KICK_NOTE, min(127, v)])
        for step in snares:
            v = sv + (bar * 5 + step) % 5 - 2
            parts[SNARE].append([at + step * STEP, at + step * STEP + 96, SNARE_NOTE, min(127, v)])
        for step in hats:
            accent = 14 if step % 4 == 2 else 0
            v = hv + accent + (bar * 3 + step * 5) % 9 - 4
            parts[HAT].append([at + step * STEP, at + step * STEP + 60, HAT_NOTE, max(1, min(127, v))])
        # A climax fill: snare 16ths into every fourth bar.
        if name == "climax" and bar % 4 == 3:
            for step in (13, 14, 15):
                parts[SNARE].append([at + step * STEP, at + step * STEP + 80, SNARE_NOTE, 96 + 8 * (step - 13)])
        # Verse metal: one quiet strike on beat 4 every other bar.
        if name in ("verse1a", "verse1b", "verse2") and bar % 2 == 1:
            parts[METAL].append([at + 12 * STEP, at + 12 * STEP + 480, METAL_NOTE, 70])
        if name == "build" and bar % 4 == 3:
            parts[METAL].append([at + 12 * STEP, at + 12 * STEP + 480, METAL_NOTE, 92])


def half_bar_roots(by_name):
    """Half-bar harmony root, as the MA3 Hurt material derives it: the
    lowest bass note, else the lowest guitar note, else the previous root,
    folded into MIDI 31..42."""
    halves = END_BAR * 2
    bass, low = [0] * halves, [0] * halves
    for name, table in (("Bass", bass), ("Guitar 2", low), ("Guitar 1", low)):
        for start, _, note, _ in by_name[name]:
            h = start // (BAR // 2)
            if h < halves and (not table[h] or note < table[h]):
                table[h] = note
    roots, previous = [], 35
    for h in range(halves):
        root = bass[h] or low[h] or previous
        while root > 42:
            root -= 12
        while root < 31:
            root += 12
        roots.append(root)
        previous = root
    return roots


def kit_ma3(parts, roots) -> None:
    """The MA3 Hurt rhythm (driver/ma3_hurt_material.h): a dark kick on
    steps 0 and 6 (plus 11 in full sections), accented on the one, and a
    tonal pulse on beat 3 at the half-bar root an octave up. Mapped onto the
    original form; single soft kicks close the ending."""
    for bar in range(START_BAR, END_BAR):
        at, name = bar * BAR, section(bar)
        climax = name == "climax"
        if name == "hit":
            parts[KICK].append([at, at + 96, KICK_NOTE, 121])
            parts[SNARE].append([at, at + 260, roots[2 * bar] + 12, 100])
            continue
        if name == "ending":
            if bar < 95:
                parts[KICK].append([at, at + 120, KICK_NOTE, 78])
            continue
        full = name in ("build", "peak", "chorus2", "climax")
        for j, step in enumerate((0, 6, 11)[:3 if full else 2]):
            parts[KICK].append([at + step * STEP, at + step * STEP + 96, KICK_NOTE,
                                (94 if j else 113) + (8 if climax else 0)])
        parts[SNARE].append([at + 960, at + 960 + 260, roots[2 * bar + 1] + 12, 92 if climax else 78])


def arrange(tracks, rhythm="industrial"):
    by_name = {t["name"]: pair_notes(t["events"]) for t in tracks}
    parts = {p: [] for p in range(10)}

    def take(name, part, *, longest=None):
        for start, end, note, velocity in by_name[name]:
            if longest is not None:
                end = min(end, start + longest)
            parts[part].append([start, end, note, velocity])

    take("Guitar 2", ARP, longest=3 * BAR // 2)
    take("Piano", KEYS, longest=BAR)
    take("Bass", BASS)
    take("Lead Vocals", VOCAL)
    take("Synth 2", PAD)
    take("Synth 1", PAD)
    take("Guitar 1", GTR)
    if rhythm == "ma3":
        # Channel 7 carries the MA3 pulse; hat and metal stay empty.
        kit_ma3(parts, half_bar_roots(by_name))
    else:
        # The original toms keep their rhythm as metal; side-stick is dropped.
        for start, end, note, velocity in by_name["Drums"]:
            if note in TOM_TO_METAL:
                parts[METAL].append([start, start + 480, TOM_TO_METAL[note], velocity])
        kit(parts)

    end_tick = END_BAR * BAR
    for part, notes in parts.items():
        if part == BASS:
            for n in notes:
                if n[2] < 26:
                    n[2] += 12
        notes[:] = [[s, min(e, end_tick), n, v] for s, e, n, v in notes if s < end_tick]
        notes.sort(key=lambda x: (x[2], x[0], -x[3]))
        kept = []
        for s, e, n, v in notes:
            if kept and kept[-1][2] == n and s < kept[-1][1]:
                if s == kept[-1][0]:
                    kept[-1][1] = max(kept[-1][1], e)
                    continue
                kept[-1][1] = s
            kept.append([s, e, n, v])
        start = START_BAR * BAR
        kept = [[s - start, e - start, n, v] for s, e, n, v in kept if e > s and s >= start]
        parts[part] = sorted(kept, key=lambda x: (x[0], x[2]))
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


def write_smf(path: Path, parts, names=NAMES) -> None:
    title = b"Hurt Mamut industrial"
    conductor = [(0, b"\xff\x03" + vlq(len(title)) + title),
                 (0, b"\xff\x58\x04\x04\x02\x18\x08"),
                 (0, b"\xff\x51\x03" + US_PER_QUARTER.to_bytes(3, "big"))]
    chunks = [track(conductor)]
    for part, notes in parts.items():
        name = names[part].encode()
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-i", type=Path, default=Path("notes-midi/local/hurt-songparts-vocals.mid"))
    parser.add_argument("-o", type=Path)
    parser.add_argument("--rhythm", choices=("industrial", "ma3"), default="industrial",
                        help="industrial kit (first edition) or the MA3 Hurt kick and pulse")
    args = parser.parse_args()
    if args.o is None:
        args.o = Path("notes-midi/local/hurt-industrial-20261107.mid" if args.rhythm == "industrial"
                      else "notes-midi/local/hurt-industrial-ma3rhythm-20261107.mid")
    names = NAMES if args.rhythm == "industrial" else NAMES[:7] + ("pulse", "hat", "metal")
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected Songparts source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != [(0, US_PER_QUARTER)]:
        raise SystemExit("unexpected source layout")
    parts = arrange(tracks, args.rhythm)
    write_smf(args.o, parts, names)
    print(f"{args.o}: sha256 {hashlib.sha256(args.o.read_bytes()).hexdigest()}")
    for part, notes in parts.items():
        if not notes:
            print(f"  ch{part} {names[part]:6}    0 notes")
            continue
        edges = sorted([(s, 1) for s, *_ in notes] + [(e, -1) for _, e, *_ in notes])
        active = peak = 0
        for _, d in edges:
            active += d
            peak = max(peak, active)
        lo = min(n for _, _, n, _ in notes)
        hi = max(n for _, _, n, _ in notes)
        print(f"  ch{part} {names[part]:6} {len(notes):4} notes  held peak {peak}  range {lo}-{hi}"
              f"  bars {notes[0][0] / BAR + START_BAR:.2f}-{max(e for _, e, *_ in notes) / BAR + START_BAR:.2f}")


if __name__ == "__main__":
    main()
