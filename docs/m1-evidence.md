# M1 evidence — generator core and the phase-coherence exhibit

Date: 2026-07-16. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Flags: `-std=c23 -O2 -Wall -Wextra -Wpedantic -ffp-contract=off`; core TU
additionally `-ffreestanding`. Commands: `make test`, `make exhibit`.

## Changes

- `src/tonewheel.h`, `src/generator.c` — gear table, wheel frequencies,
  foldback (`tw_wheel_index`), drawbar gain taps, and `tw_generator`:
  91 always-running phase accumulators with a repo-local sine kernel
  (odd Taylor to t^9, worst error < 5e-6), three gain banks with one-pole
  smoothing and a snap-to-target denormal guard. Freestanding: no OS, no
  libm, no allocation.
- `driver/wav.c` — minimal RIFF f32 writer. `driver/exhibit_phase.c` —
  the founding exhibit. `test/test.c` — table-driven checks.

## Test result

    1085 checks, 0 failures

Covers: frequency anchors (wheel 46 == 440.0 exactly; 13/73/85/91 within
1e-6 relative), whole-table deviation-from-ET (A wheels exact; worst in
1..84 is the G#-class at ~0.71 cents; worst in the top seven ~1.98 cents),
foldback totality and the per-drawbar span table from constants.md section
4, exhibit-pair collision (key 25 8' == key 37 16' == wheel 37), drawbar
tap values exact, sine-kernel error bound against libm, two-run bit
determinism (memcmp + FNV-64), hostile-target sanitize, smoothing
convergence, snap-to-zero after decay, and an end-to-end zero-crossing
count on wheel 46 (880/s).

Note: constants.md quotes [ED]'s "0.69 cents" for the worst G#-class
deviation; the exact computed value is 0.706 cents. [ED] rounded; the
test asserts the computed truth (0.68..0.73) and the class (G#).

## Exhibit result (verbatim)

    tonewheel91 M1 exhibit -- shared-wheel phase coherence
    render: 48000 Hz, 8 s, f32; wheel under test: 37 (261.5385 Hz)
    taps: key 25 drawbar 8' and key 37 drawbar 16' -> wheel 37, 37

      A(shared) envelope: min 0.35327  max 0.35383  depth 0.0 dB
        (fine 50 ms windows: 0.0 dB)
      B(detuned) envelope: min 0.03789  max 0.35189  depth 19.4 dB
        (fine 50 ms windows: 33.6 dB)

      B beat: predicted 0.4532 Hz, measured 0.4545 Hz

      FNV64 A run1 b71cbb09b1ecd064 run2 b71cbb09b1ecd064  identical
      FNV64 B      012442c11623cab8
      FNV64 chord  96b17679450dec1b

      cost: ~1.2-1.4 us/frame, ~6% of one core at 48000 Hz

      exhibit verdict: PASS

Reading: two held keys whose drawbar taps land on the same wheel are one
signal in the shared generator — the envelope is flat to 0.0 dB. The same
interval built as two independent oscillators detuned +/-1.5 cents (the
per-voice additive idiom) beats at the predicted 0.45 Hz with 33.6 dB
envelope swing. The shared generator renders the shared-wheel interval as
one signal; independent oscillators do not. The bonus chord render exercises a
real in-chord collision (C's 5-1/3' and G's 8' are both wheel 44).

WAVs (in `build/`, not committed): `m1_a_shared_generator.wav`,
`m1_b_detuned_pair.wav`, `m1_chord_888000000.wav`.

## Cost note

~1.2-1.4 us/frame measured (run-to-run turbo/thermal variance on this
laptop) is scalar, unvectorized code at -O2 — about 6% of one host core,
projected roughly 20-25% of one Cortex-A53 core at 48 kHz. Within budget
for the whole chain; if the SBC build ever wants headroom, the known
levers are a block-render API, separating the smoother/oscillator loops,
and -O3/NEON — deliberately not pulled at M1.

## Caveats

- No contact model yet: the exhibit folds gain targets itself. Click,
  bounce, taper, and the robbing merge law land at M2; bus summing here
  is plain linear addition of tap gains.
- Taper is flat (constants.md leaves it open for M2), so registration
  balance across the compass is not yet representative.
- Component-modeled tonewheel emulation is established practice
  (commercial emulations and published papers exist); M1's claim is a
  correct, sourced, deterministic generator core — not novel DSP.
