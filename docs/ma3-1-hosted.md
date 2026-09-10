# MA3-1 — pool-backed hosted MIDI path

2026-09-08. Implemented and verified. `render_midi -I mamutanalog` renders
Standard MIDI Files through the MA2-7 digital pool. This closes the dry hosted
slice; MA3 still needs effects, ALSA live integration and its live acceptance.

## Run

```sh
make CC=/usr/bin/gcc build/render_midi
build/render_midi -I mamutanalog --patch Tepih --voice-limit 32 --unison 1 \
  -r 48000 -t 4 -o build/mamut-dry.wav input.mid
```

| Option | Contract |
| --- | --- |
| `-I mamutanalog` | Explicit Mamut selection; organ remains the default |
| `--patch` | Tepih (default), Lead, Dubina, Raster, Prizma, Granica; case-sensitive |
| `--voice-limit` | 1–64, default 32, includes release slots |
| `--unison` | 1/2/4/8 slots per note, default 1; must divide limit |
| `--detune` | Finite ±range in cents, 0–100, default 7 |
| `--seed` | Decimal uint64, default MA2-7 session seed |
| `--block-frames` | 1–4096, default 128; event positions and PCM are invariant |
| `-c` | Existing zero-based channel list; tempo events survive filtering |
| `-r`, `-t`, `-o` | Existing sample-rate, tail duration and WAV path options |
| `-g` | External WAV gain; Mamut default 1, organ/EP retain .125 |

The patch's master level and fixed pool/unison gain apply before `-g`.
CC7 changes the patch master, not the external WAV gain. Gains above one can
produce WAV samples above unity; there is no automatic gain adjustment.
Organ/EP-specific controls such as `-f`, `-R` and `-D` are rejected for Mamut.
Pool/patch options are rejected for the other instruments. All option/config
errors return 2 before opening the input or modifying the WAV destination.
Repeated `-I` options use the last selection.

The renderer preserves SMF tempo changes and equal-time event order. It splits
blocks before dispatching each event, so block size does not quantize MIDI.
The Mamut zero-tail case includes one sample at the last event frame so that
event is actually dispatched. Empty zero-tail input produces an empty WAV.
Other cases keep the existing last-event-frame plus tail duration. Tail time
does not synthesize a panic or missing NoteOff; diagnostics report remaining
active slots. Organ/EP retain their previous duration and PCM behavior.

## MIDI and ownership

`driver/ma_midi_map.{h,c}` owns one pool, the authored patch and MIDI counters.
Its allocation-free entry point accepts the same decoded channel messages as
`tw_midi_parse`; the SMF renderer and future live loop share this map. The pool
owns repeated notes directly. A boolean organ/EP owner table would collapse
repeated attacks and is not used for Mamut.

All notes 0–127 are valid. Each positive NoteOn allocates a complete group;
velocity-zero NoteOn releases the oldest held matching channel/note instance.
Sustain, bend, channel pressure and modwheel are per channel. Poly pressure
addresses all live matching channel/note instances, including release tails.
Global direct controls and macros are single-timbre controls from any selected
channel. Panic CC120/123 releases all pool groups and pedals, preserving shared
output history. Filtered messages cannot change controls or panic other notes.

The dry CC map implements the generic table in `analog-backlog.md`, including
CC5/65 glide, CC77/78 LFO and CC109–113 macros. The older Patchlab CC16–20 macro
mapping is not the generic map: CC17–20 here control envelopes. MIDI logarithmic
cutoff/time/LFO tables are copied verbatim from `ma-constants.md` Appendix C.
Bend has exact −2/0/+2 endpoints/center; VCO fine tuning has exact −50/0/+50.

At the MA3-1 boundary CC107/108 effects were ignored. MA3-2 now implements
them; see `ma3-2-effects.md`. CC115 GFM remains ignored and counted.
CC114 is also ignored and counted: MA2-7 fixes unison at initialization and a
runtime CC must not erase voices to reconfigure the pool. `--unison` is the
explicit session choice. Other unassigned CCs and Program Change are counted
as unsupported. Data bytes outside 0–127 are rejected before state mutation;
one-byte messages ignore the unused second byte. System/real-time bytes handled
by the raw byte parser do not reach the map; direct system messages are counted
as unsupported.

Counters distinguish note-ons/offs, unmatched releases, failed allocations,
bends, channel/poly pressure, applied/ignored CCs, unsupported/invalid messages
and filtered channels. CLI output also reports active/peak slots, steals per
category and sleep/wake counts. The SMF parser filters channels before mapping,
so its CLI channel selection does not increment the map's raw-input filter count.

## Pool glide and LFO

Two pool setters expose the existing voice glide/LFO without changing old bank
layouts. Glide starts a slot's first note at its target; on later allocation it
starts from that slot's current, possibly unfinished pitch. Pitch freezes during
sleep. Disabling glide or setting zero time snaps stored pitch to its target;
pool reset forgets that history. Wake still resets oscillator/filter histories
and derives phases from seed, slot and allocation ID. With glide disabled,
the MA2-7 four pinned PCM signatures remain unchanged.

LFO rate/depth use existing smoothers. Active slots update immediately; new
allocations inherit current settings and start at phase zero. Exact depth-zero
bypass and the existing DSP quality remain intact.

## Verification

```sh
make CC=/usr/bin/gcc test
make BUILD=build/ma3-1-clang CC=/usr/bin/clang test
ASAN_OPTIONS=detect_leaks=0 make BUILD=build/ma3-1-sanitize \
  CC=/usr/bin/clang TEST_EXTRA= \
  CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test
```

Focused command: `make CC=/usr/bin/gcc check-ma3-1`.

`test/ma3_midi.c` checks full-group allocation, stealing, repeated/channel
ownership, sustain/panic, expression before and after allocation, macro/direct
CC coexistence, all CC classifications, exact conversion boundaries, invalid
data, glide across replacement/sleep/reset, all MIDI pitches at
44.1/48/96/192 kHz and bounded PCM. It compares direct pool calls, raw MIDI byte
parsing (running status and interleaved real-time bytes) and arbitrary renderer
block splits across all unison modes and supported sample-rate anchors.

The format-1 SMF fixture has a separate tempo track, repeated same-time notes,
running status, a second MIDI channel and panic. Expected sample positions are
independently pinned at `0,0,48,96,96,120,132`. The 48 kHz, eight-slot, two-way
unison direct/SMF twin emits 1,092 frames with FNV-64 `fbe7ee8dede78c92`.

`test/ma3_cli.py` checks the actual executable against that direct-pool PCM,
block sizes 1/17/128/256/4096, all factory patches, channel selection, rates,
seed boundaries, zero-tail and empty files, conflicting/hostile CLI controls
and preservation of an existing output on validation failure. It pins organ
and EP fixture PCM SHA256 values obtained from the renderer at `56eb8fd`:

- organ: `8aae1ee6d6b08cedc92fb36d9e03c1299b23bb30edbef4406191063e037da4f7`
- ep73: `d0e458f77e05f256d00dedd10d1761c98cda203fbef79590b4bd69624639c4e9`

These are short functional renders. No new host benchmark, live soak or musical
listening verdict is claimed. Existing arrangements and patch files retain
their paths and behavior. MA3-PERF must establish a measured live limit and
cost budget before live acceptance; the default pool limit 32 is not a realtime
guarantee.
