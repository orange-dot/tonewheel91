# tonewheel91

A standalone, component-modeled electromechanical tonewheel-organ instrument
in pure C23. Ninety-one always-running tonewheels behind a gear-ratio table, a
key-contact/busbar network with taper, loudness robbing and key click,
percussion, a vibrato/chorus scanner, stateful preamp drive, and a two-rotor
rotary speaker. Live-first: the primary driver is a Linux ALSA MIDI-in /
PCM-out loop; an offline script renderer is the deterministic test twin.

Status: M6 landed — the rotary speaker, and with it the whole design.md
signal chain: an 800 Hz crossover splits the driven organ into a horn
path (Doppler FM through a fractional delay + directivity AM) and a
drum path (band-limited AM, no Doppler — the drum is too large for its
wavelengths), two rotors with per-direction slip inertia and a
front-parking brake, two virtual mics into a stereo field, and the M5
drive stage reused as the 40 W amp ceiling ahead of the rotors. Rotor
speeds are pinned folklore ([FOLK], `docs/constants.md` sec 15) — the
by-ear pass against reference recordings is the open verdict. `rotary =
bypass` (the default) is bit-identical to the M5 mono chain on both
channels. `tw91`, the single-threaded ALSA driver, is now true stereo.
Evidence: `docs/m6-evidence.md` back through `docs/m1-evidence.md`.

    make test      # table-driven checks (7764)
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
