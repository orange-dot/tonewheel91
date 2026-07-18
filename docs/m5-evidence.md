# M5 evidence — the stateful preamp drive

Date: 2026-07-18. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, one clean-build run of each binary;
all numbers below are from that run.

## Changes

The section 14.1 drive stage, whole: the top-level `tw_instrument` chain,
the tanh-shaped saturator kernel, the bias-excursion follower, the
coupling-cap highpass, wiring after swell, the CC85 control, and the
bias-vs-bare exhibit — M5-1..M5-7 in one change.

- `tonewheel.h` — `tw_instrument` (organ -> drive; mono tick until the
  rotary's stereo field lands at M6) [decision, sec 14.1], and the
  `tw_sat` kernel: the odd rational `x(27 + x^2)/(27 + 9x^2)` clamped to
  `|x| <= 3`, where it reaches +-1 tangentially — C1 clamp, monotone
  (`r' = 9(x^2-9)^2 / (27+9x^2)^2`), unit slope at 0, within 0.0235 of
  true tanh [derived, sec 14.1]. No libm anywhere in the path.
- `src/drive.c` — `tw_drive`: a full-wave envelope follower (attack
  5 ms, release 50 ms) rides the shaper input and drags the operating
  point toward cutoff by `-0.5 * env`; a one-pole 10 Hz highpass after
  the shaper is the coupling cap that blocks the DC the shift creates.
  All three time constants are RC-order working values [decision,
  sec 14.1] — by-ear verdicts override. Control law: `pregain =
  1 + 7 drive^2` against `X_ref = 8` (the exhibits' own 1/8 headroom
  convention), makeup `X_ref/pregain`, so small-signal through-gain is
  exactly 1 — the knob adds saturation, never volume. `drive = 0` is an
  exact bypass (scanner-OFF discipline): tick returns its input
  bit-identically and touches no state.
- Chain order: swell (inside the organ) -> drive, per design.md —
  closing the pedal also cleans the stage up; audible mid-passage in the
  exhibit renders.
- `driver/main.c` — the driver now holds a `tw_instrument`; CC85 ->
  drive, value/127.
- `test/test.c` — kernel oddness (bit-exact), boundedness and
  monotonicity (to f32 rounding; the clamp region pins +-1 exactly),
  tanh proximity, unit slope, tangent clamp join; follower attack and
  release taus, settled-target snap, exact-zero snap after silence, the
  negative DC image under a loud tone; H2 bloom vs the bare shaper's
  H2 = none (the sec 14 odd/even proxy); highpass -3 dB at 10 Hz,
  passband transparency, DC rejection; instrument drive-0 bit-identity
  to the bare organ, silence at full drive, mid-note knob-to-0 rejoin,
  and two-run FNV determinism of a knob-moving script.

## Test result

    7710 checks, 0 failures

(M4 baseline was 7671.) All pre-M5 signatures verified unchanged against
the recorded evidence: exhibit_phase `b71cbb09b1ecd064` /
`012442c11623cab8` / `96b17679450dec1b` (m1), exhibit_contacts
`3f25ffe656644fd6` (m3), exhibit_taper `0565b81fd82c84a7` (m3),
exhibit_percussion `69ae12dcd88cd2ea` (m3), exhibit_scanner
`079088b2a0394053` (m4) — drive 0 is bit-identical to the pre-M5
instrument, by construction and by measurement.

## Exhibit result

    saturator kernel (sec 14.1 [derived]):
      worst deviation vs true tanh on |x| <= 3: 0.0235 (~x = 1.5)
    bias-excursion follower:
      attack: 240 samples = 5.00 ms (pinned 5 ms)
      release: 2400 samples = 50.00 ms (pinned 50 ms)
    coupling-cap highpass:
      -3 dB crossing: 10.5 Hz (pinned 10 Hz; 0.5 Hz scan)
      |H(200 Hz)|: 0.9981 (passband transparency)
    even/odd proxy (220 Hz, amp 2, drive 0.8):
      stateful stage H2/H1: -13.3 dB (the bias bloom)
      bare shaper   H2/H1: -167.1 dB (odd symmetry: none)
      bare shaper   H3/H1: -19.8 dB (odd content present)
    drive-0 identity: FNV64 c5c2f6ce4161ca74 == organ c5c2f6ce4161ca74
    scripted determinism (driven passage): FNV64 bf83b19c31cd7b74
      (two runs identical)

The odd/even proxy is the section 14 acceptance in one row: an odd
memoryless shaper cannot make even harmonics at all (-167 dB is DFT
noise), while the bias-shifted stage puts H2 at -13 dB — and because the
shift rides the envelope follower, that H2 *moves* with playing level
instead of being a frozen recipe. The -3 dB crossing reads 10.5 Hz
because the matched-Z pole sits a hair under analog 0.707 at exactly
10 Hz and the scan reports the first half-step above the crossing.

## Bias-excursion A/B

    build/m5_dry.wav          dry passage (16'+5-1/3'+8'+4', low C-E-G,
                              percussion 2nd/fast/normal; swell closes
                              to 0.3 at 3 s, reopens at 5 s)
    build/m5_drive_bias.wav   the same passage through the stateful
                              stage at drive 0.7
    build/m5_drive_naive.wav  the wrong model: the same kernel, same
                              pregain/makeup, memoryless — no follower,
                              no coupling cap

Same kernel, same drive setting — the only difference is the state. The
naive render's distortion is static; the bias render blooms on the
percussive attack, breathes through the coupling cap on level changes,
and cleans up when the swell closes mid-passage (the design.md ordering
argument, audible). By-ear verdicts (attack/release, bias depth, drive
taper, any level trim) stay open in the section 16 register; a
wave-digital triode stage is the named upgrade if the ear demands more.
