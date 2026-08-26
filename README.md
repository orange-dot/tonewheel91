# tonewheel91

A standalone, component-modeled electromechanical tonewheel-organ instrument
in pure C23. Ninety-one always-running tonewheels behind a gear-ratio table, a
key-contact/busbar network with taper, loudness robbing and key click,
percussion, a vibrato/chorus scanner, stateful preamp drive, and a two-rotor
rotary speaker. Live-first: the primary driver is a Linux ALSA MIDI-in /
PCM-out loop; an offline Standard MIDI File renderer is the deterministic
test twin.

Status: M7 landed — wear, the structured deviations: per-wheel level
spread, tooth-profile harmonics, motion AM at each wheel's own rotation
rate, an asymmetric pickup nonlinearity, leakage structured by the
generator's bin/shaft layout (not the musical order), and a 60 Hz mains
line, all behind one `wear` knob. `wear = 0` is the idealized reference
and reproduces every pre-M7 render bit-for-bit — asserted against
pinned pre-M7 signatures in test.c and the recorded exhibit hashes;
the shipped default is 0.2 (tolerance effects exist on a factory-new
unit), so an idle organ now carries its ~-44 dB noise floor. Every
depth except the pickup's measured alpha is a [FOLK]/[decision] working
value (`docs/constants.md` secs 11.1/12.1/13.1) — the by-ear pass
against reference recordings owns them. Evidence: `docs/m7-evidence.md`
back through `docs/m1-evidence.md`. Post-M7 warmth pass: the preamp
drive kernel is now derived from a circuit-true triode reference
(`docs/warmth-evidence.md`; the rotary's 40 W ceiling keeps the M5 odd
kernel) — `make warmth` scores the stage against the reference,
`./build/exhibit_warmth render` bakes A/B wavs of the old vs derived
voice. Post-M7 depth pass: a key's nine contacts are now addressable by
**how far the key is held down**, not only by how fast it was struck
(`docs/depth-evidence.md`) — poly key pressure carries the travel,
nine make points with a break-below-make band, and the section 6.1
merge law folds the partial contact set with no new DSP. Depth also
corrected the percussion trigger, which now follows the 1' contact —
the sensing line the sources always described — instead of the note
event, with the 34 ms re-arm RC modeled so the sensing line's own
bounce cannot retrigger it. A half-press therefore fires no percussion,
and a held key's travel can fire it with no note event at all. That
moved the percussion-on baselines and nothing else: every
percussion-off signature, the M7 wear identity anchor included,
reproduces bit-for-bit.

A second instrument shares the repo and the kernels: **ep73**, a tine
electric piano (`docs/piano-backlog.md`). It is a sibling core, not a
framework — 73 struck voices of three clamped-free tine modes each, a
per-voice asymmetric pickup, and velocity that scales loudness and timbre
for the first time in this codebase. EP0 pins its constants
(`docs/ep-constants.md`), EP1 lands the voice bank offline
(`docs/ep1-evidence.md`), and EP2 adds dampers, the sustain pedal, panic,
the live `ep73` binary and an instrument switch on `render_midi`
(`docs/ep2-evidence.md`). Later passes add the pinned hammer voicing,
tremolo, shared drive stage, cabinet and per-note condition model. No organ
core translation unit depends on the EP core, the instrument switch defaults
to the organ, and the organ's pinned signatures — the whole-song render
hashes included — reproduce bit-for-bit.

    make test      # core, hosted-boundary and MIDI-dispatch checks
    make exhibit   # renders the evidence WAVs into build/
    make viz       # renders the evidence PNGs into docs/viz/
    make audition-ma1-5  # renders the provisional Mamut Analog listening WAVs
    make audition-ma1-6r # Mamut sine / Tepih / Lead / Dubina WAV evidence
    make audition-ma1-7  # output body / DC blocker / safety A/B WAVs
    make audition-ma-blues # hosted F#-minor Blade Runner Blues performance study
    make           # also builds the live driver

    ./build/tw91 -d hw:CARD=AG06AG03 -e 4      # demo chord on the rig
    ./build/tw91 -d hw:CARD=AG06AG03 -m hw:X,Y,Z   # live: amidi -l
                   # poly key pressure (0xA0) = per-note key depth:
                   #   how far the key is held, 0..127 over the travel,
                   #   deciding how many of its nine contacts are made
                   # CC11 swell, CC70..78 drawbars, CC120/123 panic,
                   # CC80..83 percussion (on/off, 2nd/3rd, fast/slow, soft/normal)
                   # CC84 vibrato/chorus (value/19: off, V1..V3, C1..C3)
                   # CC85 drive (value/127)
                   # CC86 rotary mode (value/32: bypass, chorale, tremolo, brake)
                   # CC87 rotary speed switch (chorale/tremolo), CC88 balance,
                   # CC89 width, CC90 rotary drive
                   # -r rate -p period -n periods -g gain
                   # -2 two-manual touch-surface protocol: notes and key
                   #    depth on ch1+ch2 merge onto the one manual; CCs are
                   #    honored on ch1 only

    ./build/ep73 -d hw:CARD=AG06AG03 -e 4       # the electric piano
                   # notes 28..100, velocity -> loudness and timbre
                   # CC64 sustain pedal, CC120/123 panic
                   # CC85 drive, CC91 tremolo, CC92 cabinet, CC93 condition
    ./build/render_midi -I ep73 ...             # its offline twin

    ./build/patchlab --list                     # Mamut Analog patch bank
    ./build/patchlab --dump Dubina              # canonical .mapatch text
    ./build/patchlab --render Dubina build/dubina.wav
    ./build/exhibit_ma_blues # writes build/ma_blade_runner_blues.wav
    ./build/patchlab -d hw:CARD=AG06AG03 -m hw:X,Y,Z --patch Tepih
                   # ANSI/termios editor; arrows edit, p/P changes patch,
                   # zsxdcvgbhnjm, plays C3..C4, space panics, q quits
                   # MIDI: notes, bend, channel/poly pressure, mod wheel;
                   # CC16..20 edit the five Mamut macros

## Layout

    src/     freestanding core (no OS, no libm, no allocation): generator,
             contacts, percussion, vibrato/chorus scanner, preamp drive,
             rotary speaker, MIDI byte parser; and the ep73 struck-voice
             bank, which includes the organ's header for the shared kernels
             and is included by nothing in the organ
    driver/  hosted layer: WAV and PNG writers, offline exhibits, Patchlab,
             strict .mapatch file I/O, and
             render_midi (SMF in -> stereo WAV out, the live driver's
             deterministic twin for whole songs; renders logged in
             docs/renders.md); the Linux ALSA live driver (the one
             permitted dependency: libasound)
    test/    assert-based core, hosted-boundary and MIDI-dispatch tests;
             two-run FNV-64 render-signature determinism; SMF fuzz entrypoint
    docs/    design notes, pinned constants, per-milestone evidence

## Policy

- The core takes no third-party code; drivers may link platform libraries
  when a real capability demands it. libc and POSIX getopt are standard, not
  dependencies. See `docs/design.md` for the full policy.

## Targets

Any Linux/ALSA box for development; the instrument target is a Linux SBC
(Raspberry Pi 3B-class, aarch64).

## License

MIT — see [LICENSE](LICENSE). Free to use, modify, and distribute, including
commercially, with attribution.
