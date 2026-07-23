# Depth evidence — per-note key depth, and the trigger it corrected

Date: 2026-07-22. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test` (9242 checks, 0 failures), `make exhibit` (all
nine exhibits PASS), one whole-song `render_midi` re-run against a
`docs/renders.md` entry. Every number below is from that one run set.

Two coupled changes: key depth (sections 1-4 below), and the percussion
trigger correction that depth forced (section 5). The second moves the
percussion-on baselines; nothing else in the repo moves.

## Changes

A key's nine contacts are a stack, and section 7 already models that
stack in time: velocity spreads the buses over ~0-15 ms. This pass
models the same stack in **position** — how far the key is held decides
how many of its nine contacts are made — which is the physically prior
quantity. Velocity stagger is what depth looks like when the only thing
known about a press is how fast it was.

Constants and provenance: `docs/constants.md` section 7.1 (new). The
carrier is poly key pressure (0xA0), the only per-note continuous
channel MIDI 1.0 has.

- `src/tonewheel.h` — `tw_organ` grows `made[TW_KEYS]` (contacts the
  travel commands, 0..9, against `contact[][]` which is where they
  actually are, bounce and stagger included); `tw_organ_note_depth` on
  the public surface; the `rng` comment now says note **and depth**
  events.
- `src/organ.c` — `schedule_key` generalizes from a `bool down` to an
  `int target` contact count, and the velocity→span computation moves
  out to its caller. Note-on is `target = 9`, note-off `target = 0`,
  depth everything between; the three share one path. Make points sit
  evenly over the 0..127 travel with a ±4 band (sec 7.1).
  `tw_organ_note_depth` walks the made count, ignores a key that is not
  held, and schedules with **zero span** — under depth the position
  stream *is* the stagger, so the transitions carry only their bounce.
  `tw_organ_panic` clears `made[]` with the rest.
- `driver/main.c`, `driver/render_midi.c` — case `0xA0` in both message
  maps, so the offline twin stays byte-compatible with the live driver;
  a `depths` counter in both stat lines. In `-2` two-manual mode depth
  rides the **note** gate (channels 1-2), not the console CC gate:
  depth is a manual gesture, not a console control. `render_midi`'s
  `-f` octave-fold now covers 0xA0 too, or a folded note and its depth
  would address different keys.
- `test/test.c` — `test_key_depth`, the percussion tests rebuilt around
  the contact timeline, and a 24 000-tick stress run (overlapping notes
  at mixed velocities, depth moves across the ninth make point, panics
  mid-flight) asserting after every tick that the sensing-line count
  still equals the closed 1' contacts — a stuck count would silently
  arm or disarm percussion forever. +331 checks (8911 -> 9242).
- `driver/exhibit_depth.c` + `Makefile` — the exhibit, below.

## What is *not* here

`refold_wheel` was not touched, and no signal path was added. The
section 6/6.1 merge law already folds an arbitrary set of closed
contacts per wheel — foldback collisions included — so depth changes
only which cells of `contact[key][bus]` are closed. The exhibit asserts
this directly rather than claiming it: at every one of the ten steps,
each made bus passes **exactly** its full-press contribution and each
unmade bus exactly zero (bit-equality, not a tolerance). Had that
needed a new DSP block, the approach would have been wrong.

## Inertness of the depth half

Depth alone is inert without a depth message, verified rather than
assumed:

- the four pinned pre-M7 scripted signatures still reproduce at
  `wear = 0` — `f0b4c7c3f7705480`, `b01485a1702721a3`,
  `a3c0070288f0a1cd`, `f1d10bfe4b6cab4d`;
- a whole-song re-render of the `docs/renders.md` Karn Evil 9 entry
  (`-c 2,3 -R 888888888 -v 0 -D 0 -m 1 -w 0 -g 0.030 -f`) reproduces
  `d5178780f16e6bd3` and peak 0.805 — the logged M7 values — with
  `0 depths` applied;
- the depth exhibit renders the same passage with and without a depth
  stream and hashes both: no message gives `ea6d5a4bfd14f58e`, equal to
  the pre-depth passage.

Structurally that is guaranteed, not lucky: the generalized
`schedule_key` emits byte-identical events and draws the same RNG
sequence for `target = 9` and `target = 0`, so the only new draws in
the machine happen inside a depth-triggered call. A depth message that
lands on the count already made returns before touching anything, RNG
included — a surface may stream at whatever rate it likes.

## Exhibit result

    travel (0..127) -> contacts made, with the +-4 band:
      1@12 2@25 3@38 4@51 5@64 6@76 7@89 8@102 9@115
      position 64 reached from above -> 5 contacts, from below -> 4
      400 messages dithering +-3 across it: no contact moved, no RNG drawn

    the staircase (one key at 888888888, steady rms per depth,
    and the partial each contact adds as it makes):
      0 contacts: rms 0.0000  (held, and silent)
      1 contact : rms 0.7077   +   130.8 Hz  (16')
      2 contacts: rms 1.0006   +   392.0 Hz  (5-1/3')
      3 contacts: rms 1.2269   +   261.5 Hz  (8')
      4 contacts: rms 1.4142   +   523.1 Hz  (4')
      5 contacts: rms 1.5824   +   784.0 Hz  (2-2/3')
      6 contacts: rms 1.7326   +  1046.2 Hz  (2')
      7 contacts: rms 1.8718   +  1318.4 Hz  (1-3/5')
      8 contacts: rms 2.0010   +  1568.0 Hz  (1-1/3')
      9 contacts: rms 2.1222   +  2092.3 Hz  (1')
      made buses pass exactly their full-press contribution: yes
      the travel is a timbre control, not a level one: 9.5 dB of
      level across the whole of it, against a spectrum that grows
      from the 16' fundamental to the 1' -- four octaves of reach

    one contact arriving mid-note (3 -> 4): +24.8 dB over the sustain
    the smear: 334 contact transitions in one second of dither at ~7 Hz

    ninth-contact theft: with percussion on, depth 8 and depth 9 are
      identical, bit for bit. With it off the same step is worth 0.51 dB

    inertness: no depth message -> FNV64 ea6d5a4bfd14f58e
      == the pre-depth passage
    scripted determinism (the smear render): FNV64 d5c5222dbdf54110

Three readings worth stating plainly:

**The travel is a timbre control.** 9.5 dB of level across the whole of
it, against a spectrum growing from the 16' fundamental to the 1' — four
octaves. The level column is nearly flat because the first contact
carries the sub-octave, which dominates RMS; the *sound* changes far
more than the number does. A half-press is a registration the drawbars
cannot reach, because the drawbars set nine gains and the travel sets
which nine buses exist.

**The smear is contact machinery, not a filter.** A finger dithering
across the make points at ~7 Hz produces **334 contact transitions per
second** on one key — each make and each break carrying its own section-7
bounce (up to three toggles in 2 ms). That is why the gesture sounds
gritty rather than smooth, and it is why one contact arriving into a
sustaining note measures **+24.8 dB** on the M2 click metric: the smear
is a dense stream of key clicks, which is exactly what the machine does
under that gesture.

**The ninth contact is stolen while percussion is on — and it is also
the trigger.** Section 8's sensing line is the 1' bus, so with
percussion enabled the top step of the travel adds no sustained tone at
all: depth 8 and depth 9 come out bit-for-bit identical. Off, the same
step is worth 0.51 dB. So with percussion on, the top of the travel
becomes a *pure percussion control* — inaudible in the tone, and the
only thing that fires the envelope. That is not a design; it is two
sourced facts about the same wire meeting each other.

## The trigger correction

Depth made an existing simplification untenable. Section 8 has always
said the trigger-sensing line is the 1' bus — closing a contact grounds
terminal K, which unclamps the control-tube grid and releases the
envelope. The model nonetheless fired the envelope on the **note
event**. Those are the same thing only if every press bottoms out, and
section 7.1 is precisely the case where it does not: a half-press never
reaches the ninth contact, so it must not fire percussion, and no
amount of special-casing at the note event expresses that.

So the state machine now follows the contact:

- `sense_contact` in `src/organ.c` runs off `apply_contact` whenever bus
  8 moves. Closing grounds K: if the grid has recovered, the envelope
  fires on that key's wheel. While any ninth contact is closed the grid
  stays clamped — **that is** the single-trigger rule, not a separate
  rule about keys. `tw_organ_note` no longer touches percussion at all.
- **The 34 ms re-arm RC is now modeled, and had to be.** With the
  trigger on the sensing line, that line's own contact bounce (section
  7: up to three toggles inside 2 ms) would have retriggered the
  envelope several times per press. The R55/R56 recovery — already
  derived in section 8, previously dismissed as "essentially immediate"
  — is exactly what separates bounce from detachment: an order above
  the bounce window, two orders below a playable staccato gap. It cost
  one scalar test per sample against a bank loop that already runs 61
  wide.

Measured (`exhibit_percussion`, `exhibit_depth`):

    single-trigger vs naive retrigger:
      legato three-note chord:   1 trigger(s), expected 1
      staccato three notes:      3 trigger(s), expected 3

    detachment threshold (two notes, gap between release and attack):
          0 ms gap: 1 hit(s)  (inside the ~34 ms recovery)
         10 ms gap: 1 hit(s)  (inside the ~34 ms recovery)
         20 ms gap: 1 hit(s)  (inside the ~34 ms recovery)
         40 ms gap: 2 hit(s)
        100 ms gap: 2 hit(s)

    decay fast: 17999 samples (0.375 s), slow: 74448 (1.551 s),
      ratio 4.136 against the derived 4.133

    a half-press fires nothing (armed, no wheel), bottoming out fires
    it (wheel picked), and riding back onto the ninth contact
    retriggers with no note event (envelope jumps)

Four things that were previously unrepresentable are now behaviour:
the trigger lands at the *end* of a slow press rather than its start;
one press is one hit through its own bounce (asserted directly — the
envelope rises exactly once); detachment is a duration with a
measurable threshold sitting where the RC says it should, between 20
and 40 ms; and a held key's travel can fire percussion on its own.

The exhibit's old "staccato" phrase released each note and attacked the
next in the same frame. Under the old model that still counted as three
triggers; under this one it is one, correctly — a release the next
attack treads on is not a detachment. The phrase now leaves a real gap,
which is what it always claimed to be demonstrating.

## New baselines

This is the part that is not inert, and the reason for the re-pin. The
trigger moved on the timeline, so **every percussion-on render moves**;
nothing else does. Verified against the full suite:

| render | before | now |
|--------|--------|-----|
| `exhibit_percussion` legato | `69ae12dcd88cd2ea` | `d938deec1b900ba2` |
| `exhibit_drive` drive-0 identity | `c5c2f6ce4161ca74` | `a52bd74768a20d76` |
| `exhibit_drive` driven passage | `edf3907a850d8ae4` | `690d928061078295` |

Unchanged, checked rather than assumed — every one still prints its
recorded hash and PASSes:

- the M7 wear identity anchor `f961d056e8b12e32` (the M6 transition at
  wear 0), which is the repo's most load-bearing pin;
- all four pinned pre-M7 scripted signatures in `test.c`;
- `exhibit_phase` (`b71cbb09b1ecd064` / `012442c11623cab8` /
  `96b17679450dec1b`), `exhibit_contacts` (`3f25ffe656644fd6`),
  `exhibit_taper` (`0565b81fd82c84a7`), `exhibit_scanner`
  (`079088b2a0394053`), `exhibit_rotary` (`f961d056e8b12e32`);
- the whole-song `renders.md` hash `d5178780f16e6bd3`.

The percussion exhibit's own render also changed length (the staccato
phrase gained its detachment gaps), so its wavs are new material rather
than a re-hash of the same audio. The `exhibit_drive` numbers moved
purely by trigger timing — that passage runs percussion on and holds
its chord for 6.5 s, so the audible difference is ~3 ms of envelope
placement at the attack.

The earlier milestone evidence docs (`m3`, `m4`, `m5`, `m6`, `m7`) keep
their original figures. They record runs that really happened on those
trees; this doc owns the current values, which is how the warmth pass
was handled before it.

## Renders

    build/depth_steps.wav  one key at 888888888, the travel walked out
                           to nothing and back in half-second steps —
                           each contact heard arriving and leaving alone
    build/depth_smear.wav  a chord held while the finger rides the
                           travel: two slow sweeps (the harmonics peel
                           off, then wash back in), then ~5 s of dither
                           across four make points at ~7 Hz
    build/depth_theft.wav  percussion on: a full press, then the travel
                           toggled 9 -> 8 -> 9 -> 8 -> 9 at one second a
                           step. Measured rms per 0.25 s holds 0.234-0.235
                           through every depth-8 stretch and through the
                           tail of every depth-9 one — the tone does not
                           move — while each return to 9 opens at 0.247
                           and decays: the envelope firing, with no note
                           event anywhere in the render

## The [decision] caveat and the by-ear open items

**Every number in section 7.1 is a working value.** The mechanism is
sourced only in the weak sense — [SM 5-32] draws nine springs at nine
heights, which is what makes a half-press possible at all — but this
manual edition tabulates neither the spring heights nor their order:

- even make-point spacing [decision], taken from the stagger model's own
  linear spread rather than from a keybed drawing;
- closure order bus 0 → 8 [decision], following section 7;
- the ±4 make/break band [decision] — a wiping contact really does break
  lower than it makes, but this width is sized for the control problem
  (nine steps ~12.8 apart, a parked finger must not chatter), not
  measured;
- poly key pressure as the carrier [decision]; a MIDI 2.0 per-note
  controller is the honest transport if one ever arrives
  (`docs/gesture-control.md`).

On the trigger side the mechanism *is* sourced ([SM 5-51]: the sensing
line, terminal K, the grid clamp) and the RC values are read off the
schematic, so only one number there is a judgement call:

- reading recovery as **one tau (34 ms)** [decision]. Full recovery is
  a few tau; one tau is what the model uses. Anywhere between roughly
  10 and 100 ms would satisfy the job it has to do (separate 2 ms of
  bounce from a played detachment), so the ear is unlikely to settle
  this — a measurement on a real unit would.

Still open above it, unchanged by this pass: **absolute decay seconds
are not derived** (section 8 — that needs V7's transfer behaviour), and
every by-ear verdict on peak level, SOFT pad, and NORMAL attenuation.

The by-ear pass against reference recordings owns all of it. No
touch-surface work is in this change: the engine receives depth and
sounds it, and the surface that will send it is a separate step.
