# tonewheel91

A standalone, component-modeled electromechanical tonewheel-organ instrument
in pure C23. Ninety-one always-running tonewheels behind a gear-ratio table, a
key-contact/busbar network with taper, loudness robbing and key click,
percussion, a vibrato/chorus scanner, stateful preamp drive, and a two-rotor
rotary speaker. Live-first: the primary driver is a Linux ALSA MIDI-in /
PCM-out loop; an offline script renderer is the deterministic test twin.

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
voice.

    make test      # table-driven checks (8911)
    make exhibit   # renders the evidence WAVs into build/
    make           # also builds the live driver

    ./build/tw91 -d hw:CARD=AG06AG03 -e 4      # demo chord on the rig
    ./build/tw91 -d hw:CARD=AG06AG03 -m hw:X,Y,Z   # live: amidi -l
                   # CC11 swell, CC70..78 drawbars, CC120/123 panic,
                   # CC80..83 percussion (on/off, 2nd/3rd, fast/slow, soft/normal)
                   # CC84 vibrato/chorus (value/19: off, V1..V3, C1..C3)
                   # CC85 drive (value/127)
                   # CC86 rotary mode (value/32: bypass, chorale, tremolo, brake)
                   # CC87 rotary speed switch (chorale/tremolo), CC88 balance,
                   # CC89 width, CC90 rotary drive
                   # -r rate -p period -n periods -g gain

## Layout

    src/     freestanding core (no OS, no libm, no allocation): generator,
             contacts, percussion, vibrato/chorus scanner, preamp drive,
             rotary speaker, MIDI byte parser
    driver/  hosted layer: WAV writer, offline exhibits, and render_midi
             (SMF in -> stereo WAV out, the live driver's deterministic
             twin for whole songs; renders logged in docs/renders.md);
             the Linux ALSA live driver (the one permitted dependency:
             libasound)
    test/    assert-based tests; two-run FNV-64 render-signature determinism
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
