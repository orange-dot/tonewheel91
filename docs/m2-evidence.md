# M2 evidence — contacts, click, merge law, and the live driver

Date: 2026-07-16. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, `./build/tw91 -d hw:CARD=AG06AG03 -e 4`.

## Changes

- `src/midi.c` — MIDI byte parser in the freestanding core (running
  status, real-time transparency, SysEx cancellation, 1- and 2-byte
  channel messages). The same parser later eats UART bytes on the
  instrument build.
- `src/organ.c` — the contact/busbar layer over the generator:
  - nine contacts per key, velocity -> stagger only (127 ~ 0 ms, 1 ~
    15 ms; release ~3 ms), never loudness;
  - deterministic bounce, <= 3 toggle pairs inside a 2 ms window,
    fixed-seed splitmix64 advanced only at note events;
  - per-(wheel, bus) contact counts folded through the patent-derived
    merge law a(k) = 4k/(k+3) and the drawbar tap gains;
  - swell (x^2 taper, 10 ms smoothing), panic (instant electrical
    all-off), out-of-compass counting; registration default 888000000.
- `driver/main.c` — `tw91`, the live driver: one thread, nonblocking
  rawmidi read -> parser -> organ, `snd_pcm_writei` as the loop clock,
  xrun recovery via `snd_pcm_recover`, S32_LE native to the device.
  MIDI map: CC11 swell, CC70..78 drawbars, CC120/123 panic.
- `driver/exhibit_contacts.c` — the M2 evidence renders.

## Test result

    6092 checks, 0 failures

New coverage: parser truth table (running status, vel-0 note-off,
real-time byte mid-message, SysEx, program change, stray data); merge law
on the real foldback collision (keys 36+48 -> wheel 13 on the 16' bus:
targets 1.0 then 1.6); velocity never leaks into loudness (bitwise-equal
targets for vel 1 vs 127); out-of-compass ignored+counted; mid-stagger
release settles to silence; panic; swell mute/reopen; scripted two-run
bit determinism (memcmp + FNV).

## Exhibit result

    click (2nd-difference peak, attack vs sustain):
      vel 127 (stagger ~0 ms):  +44.9 dB
      vel 25  (stagger ~12 ms): +43.4 dB
    merge law (16' bus, steady RMS ratios):
      same wheel pair / solo:   1.599 (law a(2) = 1.600)
      distinct pair / solo:     1.416 (power sum = 1.414)
    scripted determinism: FNV64 67159f6aecb0bc91 (two runs identical)
    cost: ~1.15-1.2 us/frame, ~5.7% of one core at 48 kHz

The click is measured on a dark registration (888800000) at the lowest
key, where it musically stands out; on bright registrations the metric
is swamped by legitimate high sustained partials (a bright registration
measures -1.3 dB there). The merge
table is the M2 counterpart of M1's phase-coherence exhibit: colliding
taps merge to 1.6, non-colliding pairs power-sum to 1.41, nothing
doubles.

WAVs (in `build/`): `m2_click_vel127.wav`, `m2_click_vel25.wav`,
`m2_merge_pair.wav`.

## Live smoke (the reference rig)

    pcm: hw:CARD=AG06AG03, S32_LE stereo 48000 Hz, period 128,
         buffer 384 (8.0 ms)
    stopped: 4.0 s rendered, ... 0 xruns

Direct `hw:` open on the AG03, no plug layer, demo chord audible on the
monitor path; a second run with `-m hw:2,0,0` also exercised the rawmidi
open/read path (the AG03's own port is its DSP-control port, so no notes
arrive through it; a keyboard lands on its own rawmidi port).

## Caveats

- Taper is still flat: no primary source for the per-(key, bus)
  resistance classes is in `docs/externalDocs/` yet. Registration
  balance across the compass is the biggest known gap to the reference
  sound at this point.
- "Robbing" here is the per-(bus, wheel) merge law, not a global
  per-bus curve: the patent's own numbers say distinct generators sum
  ~independently while same-wheel taps merge. If ears disagree with
  physics later, the harness measures it then.
- Click tau default is 0.25 ms (bright), stagger/bounce numbers are the
  pinned starting points — all remain by-ear tunables at the first real
  keyboard session.
- Percussion, scanner, drive, rotary: not in yet (M3-M6). The organ is
  playable and clicky, not yet the full machine.
