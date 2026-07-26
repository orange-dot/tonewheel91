# EP1 evidence — the ep73 struck-voice bank

Date: 2026-07-26. Host: i7-4600U @ 2.10 GHz, Fedora, GCC 16.1.1.
Flags: `-std=c23 -O2 -Wall -Wextra -Wpedantic -ffp-contract=off`; core TU
additionally `-ffreestanding`. Commands: `make test`, `make exhibit`.

## Changes

- `src/epiano.h`, `src/ep_voice.c` — the struck-voice bank: 73 keys x 3
  clamped-free tine modes in mode-major SoA banks, equal-temperament note
  map, the Nyquist silence rule, strike laws for level and spectrum, a
  per-voice pickup nonlinearity, a passive sum, and both candidate bank
  layouts. Freestanding: no OS, no libm, no allocation. `epiano.h` includes
  `tonewheel.h` for `tw_sin_turns`, `tw_fabsf` and `tw_fnv1a64`; no organ
  translation unit was touched.
- `driver/exhibit_ep_voice.c` — the founding exhibit.
- `test/test.c` — a new EP section; the organ's pinned signature suite runs
  unchanged in the same binary.
- `Makefile` — `EP_OBJS`, the new exhibit target, and both wired into
  `all` / `test` / `exhibit`.

The doctrine carried over whole: f32, fixed state in caller-provided
structs, epsilon-snap smoothers, sanitizing setters with assuming tick
paths, `[[nodiscard]]` on pure lookups, no allocation and no `-ffast-math`.

## Test result

    9286 checks, 0 failures

Up from 9242. The 9242 organ checks — the pinned M1-M7 render signatures
among them — pass unchanged in the same run, which is what makes the
bit-stability guarantee asserted rather than promised.

The new checks cover: the equal-temperament map against a libm oracle and
the three sourced anchors (E1, A440, E7); the hammer-hardness zone edges;
out-of-compass counting and silence; the mode ratios against the
clamped-free eigenvalues; the Nyquist gate over the whole bank at 48 and
44.1 kHz, including that a muted mode takes no energy however hard it is
struck; the t60 table against its oracle and against the founding patent's
two dwell anchors; the *rendered* decay against the pinned t60 within 1 %;
the epsilon snap reaching exact zero and exact-zero output afterwards; the
mode-weight law against an independent oracle, its normalisation at mode 1,
its monotonicity in velocity, and its clamping; the linear velocity-to-level
law; the phase rule at a strike; the pickup kernel's monotonicity and its
closed-form harmonic ratios within 0.1 dB; the DC residual under 2 % of rms
while a note decays; bit-identity of the two bank layouts over a script;
two-run render determinism; panic to exact silence; and the hostile
sample-rate fallback.

## Exhibit result (verbatim)

    ep73 EP1 exhibit: the struck voice (docs/ep-constants.md)

    A. velocity ladder, MIDI 64 (E4, 329.63 Hz), hammer zone 2,
       42.7 ms window from the strike

       vel    peak     H2/H1 dB           H3/H1 dB      clang/H1 dB
                     meas   pinned      meas   pinned    meas  pinned
         1   0.009   -53.42  -53.29    -102.76 -110.10  -18.55 -17.87
         8   0.079   -35.39  -35.23     -74.10  -73.98  -12.86 -12.19
        24   0.268   -25.89  -25.73     -55.23  -54.94  -10.56  -9.92
        48   0.620   -20.04  -19.85     -43.37  -43.04   -9.32  -8.80
        72   1.070   -16.80  -16.56     -36.61  -36.22   -8.60  -8.25
       100   1.748   -14.40  -14.07     -31.36  -30.88   -7.95  -7.85
       127   2.591   -12.88  -12.44     -27.77  -27.17   -7.41  -7.59

       second-harmonic swing pp -> ff: 40.5 dB, monotone: yes

    B. free-decay conformance, f1 band of the render

       MIDI   f1 Hz     t60 pinned   t60 measured   error
         28     41.20       17.00 s        16.83 s    -1.0 %
         52    164.81       10.49 s        10.40 s    -0.9 %
         76    659.26        6.48 s         6.43 s    -0.8 %
        100   2637.02        4.00 s         3.97 s    -0.7 %

    C. bank layout, decision D4

       four-second script, FNV64: always-advance f4da4ad8873230df
                                  active-gated   f4da4ad8873230df  identical

       voices   always-advance   active-gated    one 48k core
                ns/sample        ns/sample       adv / gated
           1         2175               22       10.4% /  0.1%
           3         2109               71       10.1% /  0.3%
           6         2092              151       10.0% /  0.7%
          12         2092              308       10.0% /  1.5%
          24         2171              659       10.4% /  3.2%
          73         2188             2130       10.5% / 10.2%

       FNV64 E4 v100 run1 98663d5ce11165d4 run2 98663d5ce11165d4  identical

      exhibit verdict: PASS

## Reading

**A — the identity claim.** Velocity moves timbre, not only loudness. The
second harmonic the pickup makes out of the tine's excursion rises 40.5 dB
across the velocity range, monotonically, tracking the pinned closed form
of `ep-constants.md` section 6 to within 0.5 dB everywhere it matters. The
clang partial rises 11 dB over the same range as the hammer's contact
shortens. Both come from sourced mechanisms — an off-centre pickup edge
that the founding patent says exists precisely to set the
fundamental-to-overtone relationship, and a hammer-felt hardness ladder the
service manual tabulates — not from an envelope drawn to taste.

Measured ratios sit slightly below the pinned ones throughout, and the gap
widens for the third harmonic and the clang. That is the window, not the
model: 42.7 ms of a decaying note, in which the harmonics decay faster than
the fundamental carrying them. The one real outlier is H3 at velocity 1,
measured -102.8 dB against a predicted -110.1 dB — that is the f32 render's
own noise floor, not a model term.

The audible A/B is `ep1_a_soft_normalised.wav` against
`ep1_a_hard_normalised.wav`: the same note at velocity 8 and 127, each
scaled to the same peak, so the only difference left to hear is the bell
turning into a bark. `ep1_a_velocity_ladder.wav` is the same ladder played
straight, with the dynamic left in.

**B — the decay claim.** The two anchors are the founding patent's own
design targets: about 17 seconds of dwell at the bottom of the compass,
3 to 5 seconds at the top. Everything between them is the `f^-p`
interpolation those two numbers fix, with `p = 0.348`. The measurement is
taken from the rendered `f1` band rather than from the amplitude state, and
lands within 1 % at all four registers. The residual is systematic and
signed: the pickup's own level-dependent boost of the fundamental is still
decaying inside the first measurement window, which reads as a slightly
faster decay.

**C — decision D4, closed: active-gated.** The two layouts are
bit-identical over a four-second script, by construction rather than by
luck — a strike resets the phase of any mode standing at exactly zero
(`ep-constants.md` section 5.3), so a frozen silent voice and a running
silent voice can never differ at the output. That reduces the choice to
cost, and the cost table is one-sided: at the polyphony anyone actually
plays, six to twelve voices, active-gated costs 0.7-1.5 % of one host core
against a flat 10 %. At the quoted worst case — the full compass ringing,
pedal down under a glissando — the two are within this laptop's run-to-run
variance of each other. There is no case in which always-advance wins, so
the organ's "never branch-gated" rule, which exists to protect wheel phase
continuity, correctly does not extend to an instrument whose resonators are
silent until struck.

`ep_bank_tick` stays in the header as the constant-cost reference the
bit-identity test asserts against. If EP2 finds no use for it beyond that
test, it belongs in the test.

## Cost note

Neither layout vectorises at the project's `-O2`: the sine kernel's
quadrant-folding ternaries defeat if-conversion, in this bank exactly as in
the organ's 91-wheel loop, so both figures above are scalar and the
comparison is fair. `restrict` on the bank rows is in place and is not what
is blocking it. A branch-free sine kernel is the named, unscheduled lever;
it would speed up the organ and the piano together, which is an argument
for doing it once, later, on evidence — not inside EP1.

Projected on the SBC class of `design.md` (Cortex-A53 at 1.2 GHz), the
gated layout at twelve voices should stay in single-digit percent of one
core. That projection is not a measurement: no SBC is attached to this
host, and the second measurement point decision D4 nominally asks for is
recorded as open.

## Caveats

- **No dampers, no sustain pedal, no MIDI, no live binary.** A note here
  rings out freely and cannot be released. All of that is EP2, along with
  the restrike law (D5), for which EP1 runs the provisional
  `amplitude replaces, phase continues`.
- **The identity constants are drafts.** The mode weights, the corner
  ladder, the extra per-mode damping factors and the pickup alpha are
  tagged [FOLK] or [decision] in `ep-constants.md` section 12 and are EP3's
  to settle by ear against reference recordings. EP1 proves the mechanism
  and the plumbing, not the voicing.
- **Reference level.** The sum is passive and a single voice struck at
  velocity 127 peaks near 2.6, asymmetrically — the pickup expands one
  polarity. Whole-compass renders need the driver's gain flag, exactly as
  the organ does.
- **No inter-note coupling.** The service manual describes undamped tines
  ringing sympathetically with struck ones; `piano-backlog.md` excludes
  that from this line on purpose and it is the acoustic program's A5.
- Struck-voice modal synthesis is long-established practice. EP1's claim is
  a correct, sourced, deterministic bank with a measurable velocity-timbre
  law — not novel DSP.
