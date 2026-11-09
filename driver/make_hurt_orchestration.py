#!/usr/bin/env python3
"""Write the 2026-11-07 Mamut orchestration of Hurt as a ten-channel SMF.

The score is the full Songparts transcription already used for the MA3 vocal
(notes-midi/local/hurt-songparts-vocals.mid). Its eleven tracks are natively
aligned with the vocal, so this edition needs no eight-bar shift and no
second backing file. Each output channel is one Mamut part of
driver/exhibit_ma3_hurt_orch.c. No dependency beyond the standard library.
"""

import argparse
import hashlib
from pathlib import Path
import struct

SOURCE_SHA256 = "1a81d568110849398e6baac84b4a73c8d728766cf48725128b362eaa3c5bb8c0"
PPQ, BAR = 480, 1920
US_PER_QUARTER = 740740
END_TICK = 100 * BAR  # every note is released here; the renderer adds the tail

# Output channels, in exhibit part order.
ARP, KEYS, BASS, VOCAL, PAD, WASH, DRIVE, LEAD, TOMS, STICK = range(10)
PART_NAMES = ("arp", "keys", "bass", "vocal", "pad", "wash", "drive", "lead", "toms", "stick")
# Edition 2 drops the noise wash and its eight-bar intro and gives the
# freed channel to a derived kick; channel 9 becomes a backbeat snare.
KICK, SNARE = WASH, STICK
PART_NAMES_2 = ("arp", "keys", "bass", "vocal", "pad", "kick", "drive", "lead", "toms", "snare")
INTRO_TICKS_2 = 8 * BAR


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
                elif kind == 8 or kind == 9:
                    events.append((tick, d[0], 0, False))
        tracks.append({"name": name, "tempos": tempos, "events": events})
    return fmt, ppq, tracks


def pair_notes(events):
    """FIFO pairing per pitch, as the transcription's player would."""
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


def groove(parts) -> None:
    """Edition 2 kit on the 16th grid of each source bar (120-tick steps).

    Density follows the score's own sections: a light pulse under the first
    verse, a backbeat from the bass entry, the full pattern wherever the
    source drums play (40-48, 68-91), one shared hit on the bar-91 accent and
    no kit over the held ending."""
    def level(bar):
        if bar < 16: return None if bar < 8 else ((0, 8), (), 66, 0)
        if bar < 24: return ((0, 8), (4, 12), 74, 62)
        if bar < 40 or 56 <= bar < 68: return ((0, 6, 8), (4, 12), 92, 86)
        if bar < 48 or 68 <= bar < 91: return ((0, 6, 8, 11), (4, 12), 108, 104)
        if bar < 56: return ((0, 8), (4, 12), 78, 70)
        return None
    for bar in range(8, 92):
        at = bar * BAR
        if bar == 91:
            parts[KICK].append([at, at + 96, 35, 124])
            parts[SNARE].append([at, at + 96, 52, 120])
            continue
        spec = level(bar)
        if spec is None:
            continue
        kicks, snares, kv, sv = spec
        for step in kicks:
            lift = 6 if step == 0 else 0
            wobble = (bar * 7 + step * 3) % 9 - 4
            parts[KICK].append([at + step * 120, at + step * 120 + 96, 35, min(127, kv + lift + wobble)])
        for step in snares:
            wobble = (bar * 5 + step) % 7 - 3
            parts[SNARE].append([at + step * 120, at + step * 120 + 96, 52, min(127, sv + wobble)])


def orchestrate(tracks, edition=1):
    by_name = {t["name"]: pair_notes(t["events"]) for t in tracks}
    parts = {p: [] for p in range(10)}

    def take(name, part, *, longest=None, transpose=0, only=None):
        for start, end, note, velocity in by_name[name]:
            if only is not None and note not in only:
                continue
            if longest is not None:
                end = min(end, start + longest)
            parts[part].append([start, end, note + transpose, velocity])

    # Fingered guitar becomes the plucked bed; ring is capped like a string.
    take("Guitar 2", ARP, longest=3 * BAR // 2)
    # The piano figure stays in its written octave on a short struck voice.
    take("Piano", KEYS, longest=BAR)
    take("Bass", BASS)
    take("Lead Vocals", VOCAL)
    # Held synth chords: the verse clusters and the closing E chord share
    # one slow pad (their pitches never coincide).
    take("Synth 2", PAD)
    take("Synth 1", PAD)
    # The surf-noise effect becomes a filtered noise swell; pitch is inert.
    if edition == 1:
        take("FX", WASH)
    take("Guitar 1", DRIVE)
    take("Synth 3", LEAD)
    take("Drums", TOMS, only={41, 45, 47})
    if edition == 1:
        take("Drums", STICK, transpose=81 - 37, only={37})
    else:
        # Source side-stick hits become ghost notes on the snare voice.
        for start, end, note, velocity in by_name["Drums"]:
            if note == 37:
                parts[SNARE].append([start, end, 52, max(1, velocity * 11 // 20)])
        groove(parts)

    for part, notes in parts.items():
        # The two lowest bass pickups (B0, D1) sit an octave up on the synth.
        if part == BASS:
            for n in notes:
                if n[2] < 26:
                    n[2] += 12
        # Release everything by the fixed ending; drop later attacks.
        notes[:] = [[s, min(e, END_TICK), n, v] for s, e, n, v in notes if s < END_TICK]
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
        kept = [x for x in kept if x[1] > x[0]]
        if edition == 2:
            kept = [[s - INTRO_TICKS_2, e - INTRO_TICKS_2, n, v] for s, e, n, v in kept if s >= INTRO_TICKS_2]
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


def write_smf(path: Path, parts, names=PART_NAMES) -> None:
    title = b"Hurt Mamut orchestra"
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
        timed.sort(key=lambda x: (x[0], x[1]))  # releases before attacks
        events += [(t, p) for t, _, p in timed]
        chunks.append(track(events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-i", type=Path, default=Path("notes-midi/local/hurt-songparts-vocals.mid"))
    parser.add_argument("-o", type=Path)
    parser.add_argument("--edition", type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    if args.o is None:
        args.o = Path("notes-midi/local/hurt-orch-20261107.mid" if args.edition == 1
                      else "notes-midi/local/hurt-orch-20261107-v2.mid")
    names = PART_NAMES if args.edition == 1 else PART_NAMES_2
    data = args.i.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit(f"{args.i}: not the inspected Songparts source")
    fmt, ppq, tracks = read_smf(data)
    tempos = [x for t in tracks for x in t["tempos"]]
    if fmt != 1 or ppq != PPQ or tempos != [(0, US_PER_QUARTER)]:
        raise SystemExit("unexpected source layout")
    parts = orchestrate(tracks, args.edition)
    write_smf(args.o, parts, names)
    print(f"{args.o}: sha256 {hashlib.sha256(args.o.read_bytes()).hexdigest()}")
    for part, notes in parts.items():
        edges = sorted([(s, 1) for s, *_ in notes] + [(e, -1) for _, e, *_ in notes])
        active = peak = 0
        for _, d in edges:
            active += d
            peak = max(peak, active)
        lo = min(n for _, _, n, _ in notes)
        hi = max(n for _, _, n, _ in notes)
        print(f"  ch{part} {names[part]:6} {len(notes):4} notes  held peak {peak}"
              f"  range {lo}-{hi}  bars {notes[0][0] / BAR:.2f}-{max(e for _, e, *_ in notes) / BAR:.2f}")


if __name__ == "__main__":
    main()
