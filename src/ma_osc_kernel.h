/* Mamut Analog oscillators, written once for both lane widths: the BLEP
 * waveforms, the triangle integrator, the guards, sync and cross-mod, the
 * 8x source block of one frame and its decimation to 2x. ma_osc.c
 * instantiates it for one voice (ML_W 1), ma_lanes.c for eight pool voices
 * per AVX2 vector (ML_W 8).
 * Contract: ma-constants.md 3.
 *
 * Where a lane may take either side of a choice, both sides are computed and
 * ml_if picks one; that is what lets eight voices share one instruction
 * stream. A branch on ml_any stays a branch at width one. Include after
 * ma_voice_impl.h and ma_lane_ops.h. */
#ifndef MA_OSC_KERNEL_H
#define MA_OSC_KERNEL_H

/* ---- waveforms ---- */

/* The width of the smoothing zone around an edge. */
static inline ml_f ml_edge(ml_f step) {
    ml_f edge = 8.0f * step;
    return ml_if(edge > 0.49f, 0.49f, edge);
}

static inline ml_f ml_poly_blep(ml_f phase, ml_f step) {
    ml_m valid = step > FLT_EPSILON;
    ml_f edge = ml_edge(step);
    ml_m low = (phase < edge) & valid;
    ml_m high = (phase > 1.0f - edge) & valid;
    if (!ml_any(low | high)) return ML(0.0f);
    ml_f x_low = 0.5f * (phase / edge + 1.0f);
    ml_f x_high = 0.5f * ((phase - 1.0f) / edge + 1.0f);
    return ml_if(low, 2.0f * (ml_smooth5(x_low) - 1.0f),
                 ml_keep(high, 2.0f * ml_smooth5(x_high)));
}

/* The distance behind an edge, for an oscillator that was sync-reset less
 * than half a turn ago. A reset moves the phase but not the edges already
 * passed: that distance is the phase when the crossing came after the reset
 * (the phase is less than `since`, the run since the reset), and
 * `since + carry` otherwise, `carry` being the distance behind the edge at
 * the reset. */
static inline ml_f ml_behind_edge(ml_f phase, ml_f since, ml_f carry) {
    return ml_if(phase < since, phase, since + carry);
}

/* poly_blep with the half that follows an edge on the distance behind it;
 * the half that precedes an edge still runs on the phase. */
static inline ml_f ml_poly_blep_since(ml_f phase, ml_f step, ml_f since, ml_f carry) {
    ml_m valid = step > FLT_EPSILON;
    ml_f edge = ml_edge(step);
    ml_f behind = ml_behind_edge(phase, since, carry);
    ml_m low = (behind < edge) & valid;
    ml_m high = (phase > 1.0f - edge) & valid;
    if (!ml_any(low | high)) return ML(0.0f);
    ml_f x_low = 0.5f * (behind / edge + 1.0f);
    ml_f x_high = 0.5f * ((phase - 1.0f) / edge + 1.0f);
    return ml_keep(low, 2.0f * (ml_smooth5(x_low) - 1.0f))
         + ml_keep(high, 2.0f * ml_smooth5(x_high));
}

static inline ml_f ml_saw(ml_f phase, ml_f blep) {
    return ml_clamp(2.0f * phase - 1.0f - blep, -1.25f, 1.25f);
}

static inline ml_f ml_pulse_with_blep(ml_f phase, ml_f width, ml_f step, ml_f blep) {
    ml_f sample = ml_if(phase < width, 1.0f, -1.0f);
    sample = sample + blep;
    sample = sample - ml_poly_blep(ml_wrap(phase - width), step);
    return ml_clamp(sample, -1.25f, 1.25f);
}

static inline ml_f ml_pulse(ml_f phase, ml_f width, ml_f step) {
    return ml_pulse_with_blep(phase, width, step, ml_poly_blep(phase, step));
}

static inline ml_f ml_pulse_since(ml_f phase, ml_f width, ml_f step, ml_f since,
                                  ml_f wrap_carry, ml_f width_carry) {
    ml_f sample = ml_if(phase < width, 1.0f, -1.0f);
    sample = sample + ml_poly_blep_since(phase, step, since, wrap_carry);
    sample = sample - ml_poly_blep_since(ml_wrap(phase - width), step, since, width_carry);
    return ml_clamp(sample, -1.25f, 1.25f);
}

/* The Mamut sine: a driven sine, normalized by the drive's own peak
 * (ml_sat(1.45) = 0x1.d67b52p-1), with a little second harmonic. */
static inline ml_f ml_mamut_sine(ml_f phase) {
    ml_f warm = ml_sat(1.45f * ml_sin_turns(phase)) / 0x1.d67b52p-1f;
    ml_f second = ml_sin_turns(ml_wrap(2.0f * phase + 0.07f));
    return 0.95591217f * ml_fmadd(0.07f, second, warm);
}

/* VCO-2's saw bow (docs/ma-vco-plan.md, V2): the ramp runs a little ahead of
 * a straight line in its middle, a parabola that is zero at both ends, with
 * its mean taken out. `bow` is the parabola's height; zero adds nothing. */
static inline ml_f ml_saw_bow(ml_f saw, ml_f phase, ml_f bow) {
    ml_f arch = ml_fmadd(4.0f * phase, 1.0f - phase, -2.0f / 3.0f);
    return ml_fmadd(bow, arch, saw);
}

/* ---- triangle ---- */

static inline ml_f ml_raw_triangle(ml_f phase) {
    return 1.0f - 4.0f * ml_abs(phase - 0.5f);
}

/* B1's triangle (V2): the rise takes `rise` of the turn, the fall the rest.
 * At rise 0.5 this is the old triangle, bit for bit. */
static inline ml_f ml_raw_triangle_rise(ml_f phase, ml_f rise) {
    ml_f up = ml_fmadd(2.0f / rise, phase, -1.0f);
    ml_f down = ml_fnmadd(2.0f / (1.0f - rise), phase - rise, 1.0f);
    return ml_if(rise == 0.5f, ml_raw_triangle(phase), ml_if(phase < rise, up, down));
}

/* The circuit-derived shape of one VCO for this frame (docs/ma-vco-plan.md,
 * V2 and V3). The integrator takes `c + h * pulse`, pulse being the smoothed
 * pulse of width `rise`: +1/(2 rise) on the rise and -1/(2 (1 - rise)) on the
 * fall, so both ramps span two. `swing` is the triangle's size relative to
 * its size at the trim note, as the shapers see it. Rise 0.5, h 1, c 0 and
 * swing 1 are the old oscillator. */
typedef struct { ml_f rise, h, c, swing, skew, swell, view; } ml_tri;

/* From the departures the front end hands over: skew = rise - 0.5 and
 * swell = swing - 1, so a zeroed frame is the old oscillator. */
static inline ml_tri ml_tri_of(ml_f skew, ml_f swell) {
    ml_f rise = 0.5f + skew;
    ml_f q = 4.0f * rise * (1.0f - rise);
    ml_f swing = 1.0f + swell;
    return (ml_tri){ rise, 1.0f / q, (1.0f - 2.0f * rise) / q, swing, skew, swell, swing };
}

/* What the shapers multiply the triangle state by. The state's straight
 * ramps run between +-gain (the integrator's edge compensation) and its
 * corners are rounded inside that; the circuit's ramps run between its
 * vertices. So the shapers see the state over gain: ramps between +-1 at the
 * trim note, as the trim left them. */
static inline ml_tri ml_tri_view(ml_tri t, ml_f gain) {
    t.view = t.swing / gain;
    return t;
}

/* The integrator's input for a smoothed pulse of width `rise`. */
static inline ml_f ml_tri_input(const ml_tri *t, ml_f pulse) {
    ml_m skewed = t->c != 0.0f;
    if (!ml_any(skewed)) return pulse;
    return ml_if(skewed, ml_fmadd(t->h, pulse, t->c), pulse);
}

/* The two smoothed edges of a half period take 1.25 * edge of the 50%
 * pulse's area, so the integrator runs 1 / (1 - 1.25 * edge) faster
 * (ma-constants.md 3.1). With unequal ramps each edge is h times as tall and
 * takes h times the area. The gain for a step, and the increment for a unit
 * pulse. */
static inline ml_f ml_triangle_gain(ml_f step, ml_f h) {
    return 1.0f / ml_fnmadd(TRIANGLE_EDGE_LOSS * h, ml_edge(step), 1.0f);
}
static inline ml_f ml_triangle_step(ml_f step, ml_f gain) {
    return 4.0f * step * gain;
}

/* Where the integrator's steady trajectory is at a phase, outside the edges.
 * The state at a substep holds the pulse summed up to that substep's start,
 * which trails the triangle by half a step. */
static inline ml_f ml_triangle_seed(ml_f phase, ml_f step, ml_f gain, ml_f rise) {
    return ml_clamp(gain * ml_raw_triangle_rise(phase - 0.5f * step, rise), -1.0f, 1.0f);
}

/* ---- the triangle core's shapers (V3, driver/spice/vco/shapers.cir) ----
 * Both read the triangle the shaper sees, `swing * triangle`. */

/* The degenerated long-tailed pair, trimmed on a distortion meter at A4:
 * its static curve, normalized to a peak of one at the trim swing. */
static inline ml_f ml_shaper_sine(ml_f x) {
    x = ml_clamp(x, -1.1f, 1.1f);
    ml_f s = x * x;
    ml_f odd = ML(SHAPER_SINE_ODD[SHAPER_SINE_TERMS - 1]);
    for (int i = SHAPER_SINE_TERMS - 2; i >= 0; i--) odd = ml_fmadd(odd, s, SHAPER_SINE_ODD[i]);
    ml_f even = ML(SHAPER_SINE_EVEN[SHAPER_SINE_TERMS - 1]);
    for (int i = SHAPER_SINE_TERMS - 2; i >= 0; i--) even = ml_fmadd(even, s, SHAPER_SINE_EVEN[i]);
    return ml_fmadd(x, odd, even);
}

/* The switched pair: on the rise it follows the triangle, on the fall its
 * mirror image, from a reference trimmed to the top vertex at A4. `square`
 * is the smoothed pulse of width `rise` (+1 on the rise), whose edges
 * smooth both of the saw's steps. */
static inline ml_f ml_shaper_saw(ml_f x, ml_f square) {
    return 0.5f * square * (x - 1.0f);
}

/* ---- oscillator state and the frame's inputs ---- */

/* ma_oscillator, lane by lane */
typedef struct {
    ml_u64 phase_q48;
    ml_f triangle;
    ml_f sync_residual;
    ml_f guard_gain;
    ml_f guard_target;
    ml_f guard_step;
    ml_int guard_frames;
    ml_m triangle_initialized;
    ml_f rise;                  /* the rise the triangle last ran on; 0: none yet */
} ml_osc;

/* Both VCOs. Only VCO2 is a sync slave, so its run and carried edges (wrap,
 * width, half) live here. */
typedef struct {
    ml_osc vco1, vco2;
    ml_u64 sync_run_q48;
    ml_f sync_carry[3];
} ml_osc_pair;

/* ma_vco_controls, and which of its shapes sound */
typedef struct {
    ml_f saw_level, pulse_level, triangle_level, sine_level, pulse_width;
    ml_f tri_sine_level, tri_saw_level;
} ml_vco;
typedef struct { ml_m saw, pulse, triangle, tri_sine, tri_saw; } ml_shapes;

/* ma_source_frame, held over the frame's eight substeps, with the inputs the
 * substeps take from the front end and the controls */
typedef struct {
    ml_vco vco1, vco2;
    ml_shapes vco1_on, vco2_on;
    ml_f vco1_divisor, vco2_divisor, mixer_divisor;
    ml_f rate_hz;
    ml_f vco1_hz, vco1_step, vco1_seed_step;
    ml_f vco1_triangle_gain, vco1_triangle_step;
    ml_u64 vco1_step_q48;
    ml_f vco2_hz, vco2_step;
    ml_f vco2_triangle_gain, vco2_triangle_step;
    ml_u64 vco2_step_q48;
    ml_tri vco1_tri, vco2_tri;
    ml_f saw_bow;
    ml_f envelope_ratio, sync;
    ml_f base_hz, noise, guard1, guard2;
    ml_f crossmod_amount, vco2_level, noise_level;
    ml_f guard_hz;                      /* 0.45 fs */
    ml_int guard_frames;                /* smoothing_frames(fs) */
    ml_f guard_frames_f;
} ml_source;

/* ---- the frame's sources ---- */

/* What prepare_source_frame reads from the front end and the controls. */
typedef struct {
    ml_vco vco1, vco2;
    ml_f sample_rate_hz;
    ml_f base_hz, interval_ratio, vco2_fine_cents;
    ml_f character, vco2_cents;         /* card character and its VCO2 bias */
    ml_f sync_amount, sync_softness, crossmod_amount;
    ml_f vco2_level, noise_level, guard1, guard2;
    ml_f vco1_skew, vco2_skew, vco1_swell, vco2_swell, saw_bow;   /* V2, V3 */
} ml_source_in;

static inline ml_shapes ml_shapes_on(const ml_vco *c) {
    return (ml_shapes){ c->saw_level != 0.0f, c->pulse_level != 0.0f,
                        c->triangle_level != 0.0f, c->tri_sine_level != 0.0f,
                        c->tri_saw_level != 0.0f };
}

/* The shapers' levels join the sum after the older four, so a patch without
 * them divides by the same bits. */
static inline ml_f ml_waveform_divisor(const ml_vco *c) {
    ml_f sum = c->saw_level + c->pulse_level + c->triangle_level + c->sine_level;
    sum = sum + c->tri_sine_level + c->tri_saw_level;
    return ml_if(sum > 1.0f, sum, 1.0f);
}

/* The destinations held over the frame's eight substeps. The filter
 * envelope's modulation widens VCO1's pulse, narrows VCO2's and bends VCO1
 * up. Under cross-mod VCO1's pitch is left to the substeps; its triangle
 * still keeps the gain of the unmodulated step. */
static inline void ml_prepare_source(const ml_source_in *x, ml_f modulation, ml_source *src) {
    src->vco1 = x->vco1;
    src->vco2 = x->vco2;
    src->vco1_on = ml_shapes_on(&x->vco1);
    src->vco2_on = ml_shapes_on(&x->vco2);
    src->vco1_divisor = ml_waveform_divisor(&x->vco1);
    src->vco2_divisor = ml_waveform_divisor(&x->vco2);
    src->rate_hz = 8.0f * x->sample_rate_hz;
    src->sync = x->sync_amount * ml_clamp(1.0f - 0.75f * x->sync_softness, 0.0f, 1.0f);
    src->vco1.pulse_width = ml_clamp(x->vco1.pulse_width + 0.120f * modulation, 0.05f, 0.95f);
    src->vco2.pulse_width = ml_clamp(x->vco2.pulse_width - 0.080f * modulation, 0.05f, 0.95f);
    src->envelope_ratio = ml_octave_ratio_small(0.125f * modulation);

    ml_f vco1_weight = ml_if(x->guard1 > 0.0f, 1.0f, 0.0f);
    ml_f vco2_weight = ml_if(x->guard2 > 0.0f, x->vco2_level, 0.0f);
    ml_f mixer_divisor = vco1_weight + vco2_weight + x->noise_level;
    src->mixer_divisor = ml_if(mixer_divisor < 1.0f, 1.0f, mixer_divisor);

    ml_m crossmod_off = x->crossmod_amount == 0.0f;
    ml_f vco1_hz = x->base_hz * src->envelope_ratio;
    ml_u64 vco1_step_q48 = ml_f_to_u64(ml_phase_step(vco1_hz, src->rate_hz) * 0x1p48f);
    ml_f vco1_step = ml_turns(vco1_step_q48);
    src->vco1_tri = ml_tri_of(x->vco1_skew, x->vco1_swell);
    src->vco2_tri = ml_tri_of(x->vco2_skew, x->vco2_swell);
    src->saw_bow = x->saw_bow;
    src->vco1_seed_step = vco1_step;
    src->vco1_triangle_gain = ml_triangle_gain(vco1_step, src->vco1_tri.h);
    src->vco1_hz = ml_keep(crossmod_off, vco1_hz);
    src->vco1_step = ml_keep(crossmod_off, vco1_step);
    src->vco1_step_q48 = ml_u64_keep(crossmod_off, vco1_step_q48);
    src->vco1_triangle_step = ml_keep(crossmod_off,
                                      ml_triangle_step(vco1_step, src->vco1_triangle_gain));

    ml_f vco2_hz = x->base_hz * x->interval_ratio
                 * ml_octave_ratio_small(x->vco2_fine_cents / 1200.0f);
    ml_m character = x->character > 0.0f;
    if (ml_any(character))
        vco2_hz = ml_if(character,
                        vco2_hz * ml_octave_ratio_small(x->character * x->vco2_cents / 1200.0f),
                        vco2_hz);
    src->vco2_hz = vco2_hz;
    src->vco2_step_q48 = ml_f_to_u64(ml_phase_step(vco2_hz, src->rate_hz) * 0x1p48f);
    src->vco2_step = ml_turns(src->vco2_step_q48);
    src->vco2_triangle_gain = ml_triangle_gain(src->vco2_step, src->vco2_tri.h);
    src->vco1_tri = ml_tri_view(src->vco1_tri, src->vco1_triangle_gain);
    src->vco2_tri = ml_tri_view(src->vco2_tri, src->vco2_triangle_gain);
    src->vco2_triangle_step = ml_triangle_step(src->vco2_step, src->vco2_triangle_gain);
}

/* ---- one oscillator ---- */

/* The circuit terms of a mix (V2, V3): the saw's bow, and the two shapers
 * on the triangle. Lanes without them keep `sample` bit for bit. `square` is
 * the smoothed pulse of width `rise` at this phase. */
[[gnu::always_inline]]
static inline ml_f ml_circuit_mix(ml_f sample, ml_f phase, const ml_vco *c, ml_shapes on,
                                  ml_f triangle, ml_f square, const ml_tri *t, ml_f bow) {
    ml_m bowed = on.saw & (bow != 0.0f);
    if (ml_any(bowed)) {
        ml_f arch = ml_fmadd(4.0f * phase, 1.0f - phase, -2.0f / 3.0f);
        sample = ml_if(bowed, ml_fmadd(c->saw_level * bow, arch, sample), sample);
    }
    ml_m shaped = on.tri_sine | on.tri_saw;
    if (ml_any(shaped)) {
        ml_f x = t->view * ml_clamp(triangle, -1.25f, 1.25f);
        ml_f sine = ml_keep(on.tri_sine, ml_shaper_sine(x));
        ml_f saw = ml_keep(on.tri_saw, ml_shaper_saw(x, square));
        ml_f added = ml_fmadd(c->tri_saw_level, saw, ml_fmadd(c->tri_sine_level, sine, sample));
        sample = ml_if(shaped, added, sample);
    }
    return sample;
}

/* The smoothed waveform mix at a phase, for a given triangle state, run
 * since the last sync reset and carried edges. Silent shapes contribute an
 * exact zero. */
static inline ml_f ml_wave_mix_since(ml_f phase, const ml_vco *c, ml_shapes on, ml_f step,
                                     ml_f divisor, ml_f since, ml_f wrap_carry,
                                     ml_f width_carry, ml_f triangle, ml_f square,
                                     const ml_tri *t, ml_f bow) {
    ml_f blep = ml_poly_blep_since(phase, step, since, wrap_carry);
    ml_f saw = ml_keep(on.saw, ml_saw(phase, blep));
    ml_f pulse = ml_if(phase < c->pulse_width, 1.0f, -1.0f) + blep;
    pulse = pulse - ml_poly_blep_since(ml_wrap(phase - c->pulse_width), step, since,
                                       width_carry);
    pulse = ml_keep(on.pulse, ml_clamp(pulse, -1.25f, 1.25f));
    ml_f tri = ml_keep(on.triangle, ml_clamp(triangle, -1.0f, 1.0f));
    ml_f sample = ml_fmadd(c->triangle_level, tri,
                           ml_fmadd(c->pulse_level, pulse, c->saw_level * saw));
    ml_m sine = c->sine_level > 0.0f;
    if (ml_any(sine))
        sample = ml_if(sine, ml_fmadd(c->sine_level, ml_mamut_sine(phase), sample), sample);
    sample = ml_circuit_mix(sample, phase, c, on, triangle, square, t, bow);
    return sample / divisor;
}

/* The waveform mix at the oscillator's phase, before this substep runs.
 * Saw and pulse share the wrap edge; the pulse's width edge is its own.
 * Always inlined: it runs twice per substep, and once the lanes file grew
 * past GCC's inlining budget an out-of-line call cost the lanes 7 % more
 * instructions (ymm state spilled around every call). */
[[gnu::always_inline]]
static inline ml_f ml_preview(const ml_osc *o, const ml_vco *c, ml_shapes on, ml_f step,
                              ml_f divisor, ml_f square, const ml_tri *t, ml_f bow) {
    ml_f phase = ml_turns(o->phase_q48);
    ml_f blep = ml_poly_blep(phase, step);
    ml_f saw = ml_keep(on.saw, ml_saw(phase, blep));
    ml_f pulse = ml_keep(on.pulse, ml_pulse_with_blep(phase, c->pulse_width, step, blep));
    ml_f tri = ml_keep(on.triangle, ml_clamp(o->triangle, -1.0f, 1.0f));
    ml_f sample = ml_fmadd(c->triangle_level, tri,
                           ml_fmadd(c->pulse_level, pulse, c->saw_level * saw));
    ml_m sine = c->sine_level > 0.0f;
    if (ml_any(sine))
        sample = ml_if(sine, ml_fmadd(c->sine_level, ml_mamut_sine(phase), sample), sample);
    sample = ml_circuit_mix(sample, phase, c, on, o->triangle, square, t, bow);
    return sample / divisor;
}

/* A voice's triangle starts on its steady trajectory: once, before the
 * substeps of the frame in which it first sounds. A triangle already
 * running moves with its rise (V2): when the rise changes, the state takes
 * the difference between the two steady trajectories at its phase, so the
 * ramps stay centred. An integrator has no other way back, and a new rise on
 * every note would otherwise walk it off centre. The first rise a triangle
 * runs on is recorded without a move. */
static inline void ml_seed_triangle(ml_osc *o, ml_f step, ml_f gain, ml_f rise) {
    ml_m moved = o->triangle_initialized & (o->rise != rise) & (o->rise > 0.0f);
    if (ml_any(moved)) {
        ml_f at = ml_turns(o->phase_q48) - 0.5f * step;
        ml_f shift = gain * (ml_raw_triangle_rise(at, rise) - ml_raw_triangle_rise(at, o->rise));
        o->triangle = ml_if(moved, ml_clamp(o->triangle + shift, -1.25f, 1.25f), o->triangle);
    }
    o->rise = rise;
    /* A skewed triangle adds +a on the rise and -b on the fall; in f32 their
     * rounding no longer cancels and the state walks (about 0.005 a second
     * at 27.5 Hz). Once a frame, away from both edges, where the steady
     * trajectory is exact, the state is pulled a 64th of the way back to it:
     * an offset dies within a few ms, the shape is untouched. Equal ramps
     * keep the old integrator. */
    ml_m skewed = o->triangle_initialized & (rise != 0.5f);
    if (ml_any(skewed)) {
        ml_f at = ml_turns(o->phase_q48) - 0.5f * step;
        ml_f edge = ml_edge(step);
        ml_m away = ((at > edge) & (at < rise - edge)) | ((at > rise + edge) & (at < 1.0f - edge));
        ml_f target = gain * ml_raw_triangle_rise(at, rise);
        o->triangle = ml_if(skewed & away,
                            ml_fmadd(TRIANGLE_PULL, target - o->triangle, o->triangle), o->triangle);
    }
    if (ml_all(o->triangle_initialized)) return;
    o->triangle = ml_if(o->triangle_initialized, o->triangle,
                        ml_triangle_seed(ml_turns(o->phase_q48), step, gain, rise));
    o->triangle_initialized = ML_TRUE;
}

/* The oscillator's output for this substep. `square` is the smoothed pulse
 * of width `rise` at the current phase (50 % for the old triangle); the
 * triangle integrates c + h * square at the edge-compensated rate. Even a
 * silent triangle keeps integrating. */
static inline ml_f ml_render_oscillator(ml_osc *o, ml_f preview, ml_f square, const ml_tri *t,
                                        ml_f triangle_step, ml_f sync_correction) {
    ml_f sample = preview + o->sync_residual + sync_correction;
    o->sync_residual = ML(0.0f);
    o->triangle = ml_clamp(ml_fmadd(ml_tri_input(t, square), triangle_step, o->triangle),
                           -1.25f, 1.25f);
    return ml_clamp(sample, -1.25f, 1.25f);
}

/* The anti-alias guard fades an oscillator out above 0.45 fs, over the
 * smoothing time, and back in below it. */
static inline void ml_set_guard_target(ml_osc *o, ml_f target, const ml_source *in) {
    ml_m changed = target != o->guard_target;
    if (!ml_any(changed)) return;
    o->guard_target = ml_if(changed, target, o->guard_target);
    o->guard_frames = ml_ifi(changed, in->guard_frames, o->guard_frames);
    o->guard_step = ml_if(changed, (target - o->guard_gain) / in->guard_frames_f,
                          o->guard_step);
}
static inline void ml_next_guard_gain(ml_osc *o) {
    ml_m running = o->guard_frames != 0;
    o->guard_gain = ml_if(running, o->guard_gain + o->guard_step, o->guard_gain);
    o->guard_frames = ml_ifi(running, o->guard_frames - 1, o->guard_frames);
    ml_m done = running & (o->guard_frames == 0);
    o->guard_gain = ml_if(done, o->guard_target, o->guard_gain);
}

/* ---- sync ----
 * These paths stay out of line. They run for one edge zone after a reset and
 * at the reset itself, and inlined they slow the ordinary substep. */

/* A reset is tracked for one edge zone (eight steps), never past half a turn. */
static inline ml_u64 ml_sync_run_limit(ml_u64 step_q48) {
    ml_u64 limit = ml_u64_shl(step_q48, 3);
    return ml_u64_if(ml_u64_gt(limit, ML_U64(SYNC_RUN_LIMIT_Q48)),
                     ML_U64(SYNC_RUN_LIMIT_Q48), limit);
}

/* VCO2's preview and its triangle pulse while a sync reset is recent: its edges
 * run on their own distances (ma-constants.md 3.2). A voice with no recent
 * reset gets since = 1, which is the ordinary preview and pulse. */
[[gnu::noinline, gnu::cold]]
static ml_f ml_held_preview(const ml_osc_pair *v, const ml_source *in, ml_f *square) {
    const ml_f *carry = v->sync_carry;
    ml_m held = ml_u64_nonzero(v->sync_run_q48);
    ml_f since = ml_if(held, ml_turns(ml_u64_sub(v->sync_run_q48, ML_U64(1))), 1.0f);
    ml_f phase = ml_turns(v->vco2.phase_q48);
    ml_f step = in->vco2_step;
    *square = ml_pulse_since(phase, in->vco2_tri.rise, step, since, carry[0], carry[2]);
    return ml_wave_mix_since(phase, &in->vco2, in->vco2_on, step, in->vco2_divisor, since,
                             carry[0], carry[1], v->vco2.triangle, *square, &in->vco2_tri,
                             in->saw_bow);
}

/* The step VCO2's output takes at a sync reset. Edges crossed before the
 * event keep their distances across the reset, and a reset from just before
 * the wrap to just after it takes that wrap. The step then runs from the
 * smoothed waveform at the event to the one at the reset phase, where the
 * phase has not run yet. `square` is VCO2's triangle pulse this substep. Leaves
 * the carried distances of the `event` voices. */
[[gnu::noinline, gnu::cold]]
static ml_f ml_sync_jump(ml_osc_pair *v, const ml_source *in, ml_u64 event_advance,
                         ml_u64 event_phase_q48, ml_u64 reset_phase_q48,
                         ml_f event_fraction, ml_f square, ml_m event) {
    ml_f *carry = v->sync_carry;
    ml_m held = ml_u64_nonzero(v->sync_run_q48);
    ml_f step = in->vco2_step;
    ml_f event_phase = ml_turns(event_phase_q48);
    ml_f reset_phase = ml_turns(reset_phase_q48);
    ml_f event_since = ml_if(held, ml_turns(ml_u64_add(ml_u64_sub(v->sync_run_q48, ML_U64(1)),
                                                       event_advance)), 1.0f);
    ml_f edge = ml_edge(step);
    ml_f behind[3] = {
        ml_behind_edge(event_phase, event_since, carry[0]),
        ml_behind_edge(ml_wrap(event_phase - in->vco2.pulse_width), event_since, carry[1]),
        ml_behind_edge(ml_wrap(event_phase - in->vco2_tri.rise), event_since, carry[2]),
    };
    behind[0] = ml_if((event_phase > 1.0f - edge) & (reset_phase < edge),
                      reset_phase, behind[0]);
    const ml_tri *t = &in->vco2_tri;
    ml_f event_triangle = ml_clamp(ml_fmadd(ml_tri_input(t, square),
                                            in->vco2_triangle_step * event_fraction,
                                            v->vco2.triangle), -1.25f, 1.25f);
    /* The pulses the triangle shapers read at either end; nothing else uses
     * them. */
    ml_f reset_square = ML(0.0f), event_square = ML(0.0f);
    if (ml_any(in->vco2_on.tri_saw)) {
        reset_square = ml_pulse_since(reset_phase, t->rise, step, ML(0.0f), behind[0],
                                      behind[2]);
        event_square = ml_pulse_since(event_phase, t->rise, step, event_since, carry[0],
                                      carry[2]);
    }
    ml_f jump = ml_wave_mix_since(reset_phase, &in->vco2, in->vco2_on, step, in->vco2_divisor,
                                  ML(0.0f), behind[0], behind[1],
                                  ml_triangle_seed(reset_phase, step, in->vco2_triangle_gain,
                                                   t->rise),
                                  reset_square, t, in->saw_bow)
              - ml_wave_mix_since(event_phase, &in->vco2, in->vco2_on, step, in->vco2_divisor,
                                  event_since, carry[0], carry[1], event_triangle,
                                  event_square, t, in->saw_bow);
    for (int k = 0; k < 3; k++) carry[k] = ml_if(event, behind[k], carry[k]);
    return jump;
}

/* A held VCO2 runs on, and stops being held one edge zone past its reset. */
[[gnu::noinline, gnu::cold]]
static void ml_sync_run_on(ml_osc_pair *v, const ml_source *in) {
    ml_m held = ml_u64_nonzero(v->sync_run_q48);
    ml_u64 run = ml_u64_add(v->sync_run_q48, in->vco2_step_q48);
    ml_m keep = held & ml_not(ml_u64_gt(run, ml_sync_run_limit(in->vco2_step_q48)));
    v->sync_run_q48 = ml_u64_keep(keep, run);
}

/* Moves the `restart` voices' VCO2 to its reset phase and through the rest
 * of the substep; the others take `advanced`. The reset needs tracking only
 * if it left an edge's smoothing running or landed inside a zone that
 * follows an edge; otherwise the phase alone is right. */
[[gnu::noinline, gnu::cold]]
static void ml_sync_restart(ml_osc_pair *v, const ml_source *in, ml_u64 reset_phase_q48,
                            ml_u64 advanced, ml_f event_fraction, ml_m event, ml_m restart) {
    ml_f *carry = v->sync_carry;
    ml_m fresh = restart & ml_not(event);        /* a reset with no event */
    for (int k = 0; k < 3; k++) carry[k] = ml_if(fresh, 1.0f, carry[k]);
    ml_f step = in->vco2_step;
    ml_f remaining = 1.0f - event_fraction;
    ml_u64 remaining_advance = ml_f_to_u64(ml_u48_to_f(in->vco2_step_q48) * remaining);
    ml_u64 restarted = ml_u64_and(ml_u64_add(reset_phase_q48, remaining_advance),
                                  ML_U64(PHASE_MASK_Q48));
    ml_f reset_phase = ml_turns(reset_phase_q48);
    const ml_tri *t = &in->vco2_tri;
    ml_f triangle = ml_clamp(
        ml_fmadd(ml_tri_input(t, ml_pulse_since(reset_phase, t->rise, step, ML(0.0f), carry[0],
                                                carry[2])),
                 in->vco2_triangle_step * remaining,
                 ml_triangle_seed(reset_phase, step, in->vco2_triangle_gain, t->rise)),
        -1.25f, 1.25f);
    v->vco2.phase_q48 = ml_u64_if(restart, restarted, advanced);
    v->vco2.triangle = ml_if(restart, triangle, v->vco2.triangle);
    v->vco2.triangle_initialized = v->vco2.triangle_initialized | restart;
    ml_f edge = ml_edge(step);
    ml_f past_width = ml_wrap(reset_phase - in->vco2.pulse_width);
    ml_f past_half = ml_wrap(reset_phase - t->rise);
    ml_m track = (carry[0] < edge) | (carry[1] < edge) | (carry[2] < edge)
               | (reset_phase < edge) | (past_width < edge) | (past_half < edge);
    ml_u64 tracked = ml_u64_keep(track, ml_u64_add(remaining_advance, ML_U64(1)));
    v->sync_run_q48 = ml_u64_if(restart, tracked, v->sync_run_q48);
}

/* ---- one 8x substep ---- */

/* Both VCOs, sync, cross-mod and noise, mixed. */
static inline ml_f ml_source_substep(ml_osc_pair *v, const ml_source *in) {
    ml_osc *vco1 = &v->vco1, *vco2 = &v->vco2;
    ml_f vco2_step = in->vco2_step;
    bool held = ml_u64_any(v->sync_run_q48);
    ml_f vco2_preview, vco2_square;
    if (held) {
        vco2_preview = ml_held_preview(v, in, &vco2_square);
    } else {
        vco2_square = ml_pulse(ml_turns(vco2->phase_q48), in->vco2_tri.rise, vco2_step);
        vco2_preview = ml_preview(vco2, &in->vco2, in->vco2_on, vco2_step, in->vco2_divisor,
                                  vco2_square, &in->vco2_tri, in->saw_bow);
    }

    /* Cross-mod is the only source of pitch changes within these eight
     * substeps. Under it the triangle keeps the gain of the unmodulated
     * step. */
    ml_f vco1_hz = in->vco1_hz;
    ml_f vco1_step = in->vco1_step;
    ml_u64 vco1_step_q48 = in->vco1_step_q48;
    ml_f vco1_triangle_step = in->vco1_triangle_step;
    ml_m crossmod = in->crossmod_amount != 0.0f;
    if (ml_any(crossmod)) {
        ml_f ratio = ml_clamp(1.0f + vco2_preview * in->crossmod_amount * 0.25f, 0.25f, 4.0f);
        ml_f hz = in->base_hz * ratio * in->envelope_ratio;
        ml_f step = ml_clamp(hz / in->rate_hz, 0.0f, 0.45f);          /* phase_step */
        ml_u64 step_q48 = ml_f_to_u64(step * 0x1p48f);                /* phase_step_q48 */
        ml_f turns = ml_turns(step_q48);
        vco1_hz = ml_if(crossmod, hz, vco1_hz);
        vco1_step_q48 = ml_u64_if(crossmod, step_q48, vco1_step_q48);
        vco1_step = ml_if(crossmod, turns, vco1_step);
        vco1_triangle_step = ml_if(crossmod, ml_triangle_step(turns, in->vco1_triangle_gain),
                                   vco1_triangle_step);
    }
    ml_set_guard_target(vco1, ml_if(vco1_hz < in->guard_hz, 1.0f, 0.0f), in);

    /* A master wrap resets a synced VCO2. The reset lands where VCO2 was at
     * the wrap, scaled toward zero by the sync amount, and its step is
     * spread over this substep and the next. */
    ml_u64 vco1_sum = ml_u64_add(vco1->phase_q48, vco1_step_q48);
    ml_m reset = ml_u64_ge(vco1_sum, PHASE_ONE_Q48) & (in->sync > 0.0f);
    ml_m event = reset & (vco1_step > FLT_EPSILON);
    ml_f event_fraction = ML(1.0f), sync_now = ML(0.0f), sync_next = ML(0.0f);
    ml_u64 reset_phase_q48 = ML_U64(0);
    if (ml_any(event)) {
        ml_f until_wrap = ml_u48_to_f(ml_u64_sub(ML_U64(PHASE_ONE_Q48), vco1->phase_q48));
        ml_f fraction = ml_clamp(until_wrap / ml_u48_to_f(vco1_step_q48), 0.0f, 1.0f);
        ml_u64 event_advance = ml_f_to_u64(ml_u48_to_f(in->vco2_step_q48) * fraction);
        ml_u64 event_phase_q48 = ml_u64_and(ml_u64_add(vco2->phase_q48, event_advance),
                                            ML_U64(PHASE_MASK_Q48));
        ml_u64 reset_q48 = ml_u64_and(
            ml_f_to_u64(ml_u48_to_f(event_phase_q48) * (1.0f - in->sync)),
            ML_U64(PHASE_MASK_Q48));
        ml_f jump = ml_sync_jump(v, in, event_advance, event_phase_q48, reset_q48,
                                 fraction, vco2_square, event);
        ml_f before = 0.5f * (1.0f - fraction);
        ml_f after = 1.0f - 0.5f * fraction;
        event_fraction = ml_if(event, fraction, 1.0f);
        sync_now = ml_keep(event, jump * ml_smooth5(before));
        sync_next = ml_keep(event, jump * (ml_smooth5(after) - 1.0f));
        reset_phase_q48 = ml_u64_if(event, reset_q48, reset_phase_q48);
    }

    ml_f vco1_square = ml_pulse(ml_turns(vco1->phase_q48), in->vco1_tri.rise, vco1_step);
    ml_f vco1_preview = ml_preview(vco1, &in->vco1, in->vco1_on, vco1_step, in->vco1_divisor,
                                   vco1_square, &in->vco1_tri, in->saw_bow);
    ml_f vco1_out = ml_render_oscillator(vco1, vco1_preview, vco1_square, &in->vco1_tri,
                                         vco1_triangle_step, ML(0.0f));
    /* Cross-mod already read VCO2 at this phase, before sync correction. */
    ml_f vco2_out = ml_render_oscillator(vco2, vco2_preview, vco2_square, &in->vco2_tri,
                                         in->vco2_triangle_step, sync_now);
    vco2->sync_residual = vco2->sync_residual + sync_next;
    vco1->phase_q48 = ml_u64_and(vco1_sum, ML_U64(PHASE_MASK_Q48));
    ml_u64 advanced = ml_u64_and(ml_u64_add(vco2->phase_q48, in->vco2_step_q48),
                                 ML_U64(PHASE_MASK_Q48));
    if (held) ml_sync_run_on(v, in);
    if (ml_any(reset))
        ml_sync_restart(v, in, reset_phase_q48, advanced, event_fraction, event, reset);
    else
        vco2->phase_q48 = advanced;

    return (in->guard1 * vco1_out + in->guard2 * in->vco2_level * vco2_out
            + in->noise_level * in->noise) / in->mixer_divisor;
}

/* ---- one frame ---- */

/* VCO2's guard target, the triangle seeds, eight 8x source substeps, and
 * both guard steps. */
static inline void ml_source_block(ml_osc_pair *v, const ml_source *in, ml_f source[8]) {
    ml_set_guard_target(&v->vco2, ml_if(in->vco2_hz < in->guard_hz, 1.0f, 0.0f), in);
    ml_seed_triangle(&v->vco1, in->vco1_seed_step, in->vco1_triangle_gain, in->vco1_tri.rise);
    ml_seed_triangle(&v->vco2, in->vco2_step, in->vco2_triangle_gain, in->vco2_tri.rise);
    for (int substep = 0; substep < 8; substep++)
        source[substep] = ml_source_substep(v, in);
    ml_next_guard_gain(&v->vco1);
    ml_next_guard_gain(&v->vco2);
}

static inline ml_f ml_halfband31(const float *ring, int position) {
    return ml_halfband(ring, 31, position, OSC_HALFBAND_31, OSC_HALFBAND_31_PAIR,
                       OSC_HALFBAND_31_PAIRS, OSC_HALFBAND_31_CENTRE);
}

/* 8x -> 4x -> 2x through the 31-tap halfband: ring8x holds the 8x source,
 * ring4x the 4x one. */
static inline void ml_decimate_source(float *ring8x, float *ring4x, uint8_t position[2],
                                      const ml_f source[8], ml_f at_2x[2]) {
    int substep = 0;
    for (int step_2x = 0; step_2x < 2; step_2x++) {
        for (int step_4x = 0; step_4x < 2; step_4x++) {
            ml_push(ring8x, 31, &position[0], source[substep++]);
            ml_push(ring8x, 31, &position[0], source[substep++]);
            ml_push(ring4x, 31, &position[1], ml_halfband31(ring8x, position[0]));
        }
        at_2x[step_2x] = ml_halfband31(ring4x, position[1]);
    }
}

#endif
