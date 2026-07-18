# M3 evidence, part 1 — taper wired in, robbing on real wires

Date: 2026-07-17. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Commands: `make test`, `make exhibit`, one clean-build run of each binary;
all numbers below are from that run. Percussion — the other M3 item — is
not in this change.

## Changes

Level and robbing are the same physical network: the taper wire sets each
key's level and decides how hard it robs when keys collide on a wheel, so
both landed as one change (constants.md 6.1).

- `src/organ.c` — the section 6.1 taper: six wire classes (+7, +3.5, 0,
  -3.5, -7, -10 dB / 10, 15, 24, 34, 50, 100 ohm), run-length tables per
  (key, bus), gains precomputed (the core has no libm, no pow()). The dB
  column is the level authority, the ohm column the robbing authority.
- The robbing law replaced: per (wheel, bus) the closed contacts merge as
  `Rpar = 1/sum(1/Rw_i)`, `merge_ratio = [1/(R0+Rpar)] / sum 1/(R0+Rw_i)`
  (R0 = 5 ohm), `contribution = merge_ratio x sum(g_i)`. One contact
  passes its taper gain exactly; equal wires collapse to the old
  a(k) = 4k/(k+3), now the special case rather than the law.
- The structural fix that unblocked it: `count[wheel][bus]` (how many
  contacts, fine only while taper was flat) gave way to per-(bus, wheel)
  lists of the keys that can tap a wheel, derived once at init from the
  section 4 foldback rule. `refold_wheel()` walks the list and tests
  `contact[key][bus]`, so colliding *unequal* wires are represented.
  Still freestanding, still allocation-free.
- `driver/exhibit_taper.c` — the M3 exhibit: measures every collision
  site through the public API and renders a 16' chromatic sweep.
- `test/test.c` — tap-list census (53 collision sites, 7 three-key, every
  (key, bus) exactly once), a double-precision oracle for the merge law
  including its a(k) collapse, taper spot checks across run boundaries,
  and the rewritten 16'/wheel-13 collision assert (see below).

## Test result

    7213 checks, 0 failures

(M2 baseline was 6092.) The old `a(2) = 1.6` assert on the 16'/wheel-13
collision is gone deliberately: keys 1 and 13 carry 100- and 50-ohm wires,
so the pair merges to merge_ratio 0.9416 — the a(2)-equivalent is ~1.88 —
but taper puts the absolute contribution at 0.9416 x (0.3162 + 0.4467)
= ~0.7183, and that is what the test now asserts against the oracle.

## Exhibit result

    robbing sweep over every same-(bus, wheel) collision site:
      sites: 53 (per bus 12 0 0 0 1 6 10 12 12), 7 of them three-key
      placeholder a(k) over-robbed by +0.97..+1.78 dB (mean +1.19)
      anchor 16'/wheel 13: solo 0.3162 + 0.4467, pair 0.7183
        (merge_ratio 0.9416, a(2)-equivalent 1.883)
    taper sweep (16' chromatic, 150 ms/key):
      FNV64 0565b81fd82c84a7 (two runs identical)

The sweep measures, through the public API only, exactly the correction
constants.md 6.1 derived on paper: +0.97..+1.78 dB (mean +1.19) across
all 53 sites.

**That figure is a ratio, not an audible lift.** The robbing at collision
sites is indeed less compressive
(0.667 -> 0.8185 on the 1' triple), but the same change applies taper,
which pads the upper-harmonic buses at the treble end by up to -7 dB, and
the collision sites all live there. A/B rendered against the retired M2
core (in git at 125678a), same script through both, one shared scale:

    16' chromatic walk, M3 vs M2, per key:
      key 1 -10.00 dB   key 11 -7.00   key 17 -3.50
      key 25 +0.00      key 37 +3.50   key 49/61 +7.00   (the 6.1 ladder, exactly)
    1' bus, three keys on wheel 80 (the strongest site):
      robbing ratio  M2 0.6667 (a(3)/3)   M3 0.8185 (network)  -> +1.78 dB
      absolute level M3 vs M2: -2.22 dB   (weaker robbing, but two -7 dB wires)
    full 888888888 registration, M3 vs M2:
      bottom chord  +1.61 dB      top chord  -0.24 dB      top 4-note stack  -0.01 dB

So taper **redistributes** rather than lifts: the compass tilts, the audible
gain lands in the bass, and the top is left about where it was. The
per-key ladder above is the cleanest confirmation that the core applies the
6.1 dB column exactly.

Updated M2 contacts exhibit (same law, RMS-level corroboration):

    click (2nd-difference peak, attack vs sustain):
      vel 127 (stagger ~0 ms):  +43.0 dB
      vel 25  (stagger ~12 ms): +44.1 dB
    merge law (16' bus, steady RMS ratios):
      same wheel pair / coherent sum: 0.9413 (law merge_ratio 0.9416,
                                              a(2)-equivalent 1.883)
      distinct pair / power sum:      1.0009 (independent sources = 1.000)
    scripted determinism: FNV64 3f25ffe656644fd6 (two runs identical)
    cost: 1144 ns/frame, 5.49% of one core at 48 kHz

The scripted-determinism signature changed from M2's 67159f6aecb0bc91 —
expected: the same script now renders through taper. Two-run identity is
the invariant, and it holds. Cost is unchanged (~5.5% of one core); the
merge math runs per contact event, not per sample.

WAVs (in `build/`): `m3_taper_sweep.wav` (the 16' chromatic walk — the
-10 dB foldback pad and the +7 dB treble lift are audible), plus the
re-rendered M2 set under the new law.

## Caveats

- **By-ear verification of taper and the new robbing stays open.** The
  numbers above check the model against its own pinned law; nobody has
  yet played it against reference recordings.
- Taper provenance is unchanged and remains the weak point: one secondary
  chart, corroborated in shape but not values (constants.md 6.1). Wiring
  it in does not upgrade it.
- ~~Percussion (the remaining M3 item)~~ — landed, part 2 below. Scanner,
  drive, rotary: not in yet.

# M3 evidence, part 2 — percussion (completes M3)

Date: 2026-07-17. Same host/toolchain as part 1. Commands: `make test`,
`make exhibit`, one clean-build run of each binary; all numbers below are
from that run.

## Changes

All eight M3-1..M3-8 slices from the backlog: percussion state + API
(`tw_organ_set_percussion`), tap/wheel selection (2nd = 4' tap, 3rd =
2-2/3' tap, pre-drawbar), the single-trigger + re-arm state machine,
the decay envelope, ninth-drawbar theft, NORMAL/SOFT levels, the MIDI/CC
map, and the truth-table tests. `src/tonewheel.h`/`src/organ.c` grew a
`tw_percussion` struct (four tablets + the armed/wheel trigger state);
`src/generator.c` grew a second, independent one-pole coefficient
(`perc_smooth`) so `perc_target` decays on its own schedule while
`keyed`'s click-tau smoothing is untouched, exactly as scoped.

- **Trigger model**: `perc_target` jumps to peak at trigger and decays on
  its own one-pole every sample (gain-gated, not branch-gated — it runs
  for all 91 wheels regardless of whether percussion is active); the
  existing click-tau gain-chases-target smoothing shapes the attack on
  top, unchanged for both banks.
- **Ninth-drawbar theft**: `refold_wheel` skips bus 8 (1') outright
  whenever percussion is on, unconditional on trigger/decay state, per
  constants.md 8's "our model mutes the 1' bus contribution."
- **NORMAL attenuation [derived]**: R50 (22 ohm, already pinned) folded as
  a signal-free shunt leg into the *same* section 6/6.1 merge network,
  giving one fixed ratio `R_SRC/(R_SRC+R50) = 5/27 ~= 0.8148` applied to
  whichever wheel percussion is currently tapping. **Caveat**: this is a
  simplification against the full per-taper-class merge-law
  recomputation, and the attenuation is tied to "most recently triggered
  wheel" rather than to decay-to-silence — a held key on that wheel stays
  slightly attenuated for longer than the real circuit would, since
  nothing here tracks the percussion envelope reaching audible zero. Not
  audible at FAST decay (well under a second); worth tightening if SLOW
  decay under a long-held chord reveals it by ear.
- **SOFT pad [decision]**: `PERC_SOFT_PAD = 0.5` on percussion's own peak
  only — **not** derived from the still-unresolved R46/R59/R51 divider
  (constants.md 8 continues to flag that topology as open).
- **Percussion joins the output pre-swell** (`tw_organ_tick` now returns
  `(f.keyed + f.percussion) * swell_gain`) — matches the design.md signal
  chain ("percussion join" before the not-yet-built scanner). Existing
  scripts that never trigger percussion are bit-identical (perc banks
  stay all-zero), so this is the "new phases default to bypass" rule
  holding for free, not by special-casing.

## Test result

    7238 checks, 0 failures

(M3 part-1 baseline was 7213; +25 new checks.) `test_percussion_trigger`
runs a table-driven key sequence (legato add/release, staccato retrigger,
percussion-off no-op, harmonic-switch wheel selection, hostile-argument
clamping); `test_percussion_decay` measures samples-to-1/e for FAST and
SLOW against the pinned 0.375 s / 1.551 s tau and their 4.133 ratio;
`test_percussion_levels` checks ninth-drawbar theft, the NORMAL
attenuation ratio, and the SOFT peak pad, each against its own oracle
constant mirrored from `src/organ.c`.

## Exhibit result

    single-trigger vs naive retrigger (registration silenced, pure percussion):
      legato three-note chord:   1 trigger(s), expected 1
      staccato three notes:      3 trigger(s), expected 3

    decay fast/slow (samples to 1/e of trigger peak):
      fast: 17999 samples (0.375 s, expected ~0.375 s)
      slow: 74448 samples (1.551 s, expected ~1.551 s)
      ratio slow/fast: 4.136, expected ~4.133

    scripted determinism: FNV64 69ae12dcd88cd2ea (two runs identical)

The single-trigger/naive-retrigger A/B needed no test-only backdoor: with
registration silenced (all digits 0) the render is pure percussion
through the public API, and legato vs staccato *is* the real single-
trigger + re-arm rule made audible — a legato chord is one hit no matter
how many notes join it; the same notes played staccato retrigger every
time, because each release re-arms. The measured decay lands within one
sample-window of the pinned RC tau in both speeds, and the ratio (4.136)
is inside 0.1% of the pinned 4.133.

WAVs (in `build/`): `m3_percussion_legato.wav`, `m3_percussion_staccato.wav`
(the A/B), `m3_percussion_decay_fast.wav`, `m3_percussion_decay_slow.wav`.

## MIDI/CC map (M3-6)

`driver/main.c`: CC80 on/off, CC81 2nd/3rd harmonic, CC82 fast/slow decay,
CC83 soft/normal volume; value >= 64 is each toggle's second-named
position. `tw91` builds against these (`make build/tw91`, this host).
**Manual CC trial against a real controller is not done** — no MIDI
hardware in hand; flagged open rather than claimed.

## Caveats

- NORMAL attenuation's "most recently triggered wheel" simplification
  (above) — a documented approximation, not a closed derivation.
- SOFT's percussion-peak pad and the R46/R59/R51 divider it would
  properly come from remain two separate open items; only the former is
  landed, as a placeholder.
- The ~34 ms re-arm RC is not modeled as a timer — constants.md 8 already
  calls it "essentially immediate on release" against realistic playing
  tempo and the much slower decay taus; the state machine re-arms the
  instant the last key releases. If by-ear testing ever disagrees, this
  is where to look.
- Percussion's absolute peak level (`PERC_PEAK_GAIN = 1.0`) is a
  [decision], not sourced — the manual gives no absolute gain figure for
  the borrowed-bus transformer path. Not verified against reference
  recordings.
- Scanner, drive, rotary: still not in.
