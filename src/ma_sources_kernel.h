/* Mamut Analog digital and nonlinear sources, written once for both lane
 * widths: the raster wavetable (MA2 DIG) and the BCS oscillator (a
 * Hopf/Duffing pair under RK4). ma_sources.c instantiates it for one voice
 * (ML_W 1), ma_lanes.c for eight pool voices per AVX2 vector (ML_W 8).
 * Include after ma_raster_table.h, ma_voice_impl.h and ma_lane_ops.h. */
#ifndef MA_SOURCES_KERNEL_H
#define MA_SOURCES_KERNEL_H

/* ---- raster ---- */

/* The finest mip whose harmonics stay under 0.45 of the render rate, and
 * how far toward the next coarser mip to blend. The limits halve from mip
 * to mip, so the passing mips are a prefix and counting them is the walk. */
static inline ml_int ml_raster_mip(ml_f step, ml_f warp, ml_f *blend) {
    ml_f effective_step = step * (1.0f + 0.8f * warp);
    ml_int mip = ML_INT(0);
    for (int k = 0; k + 1 < MA_RASTER_MIPS; k++)
        mip = ml_ifi(effective_step * (float)(64 >> k) > 0.45f, mip + 1, mip);
    ml_f load = effective_step * ml_int_to_f(ML_INT(64) >> mip);
    *blend = ml_if(mip == MA_RASTER_MIPS - 1, 0.0f,
                   ml_clamp((load - 0.225f) * (1.0f / 0.225f), 0.0f, 1.0f));
    return mip;
}

typedef struct {
    ml_f base_hz, render_rate_hz, position, warp;
} ml_raster_in;

/* What stays fixed over a frame's raster steps. Rows index MA_RASTER_TABLE
 * as (family * MA_RASTER_MIPS + mip) * MA_RASTER_SAMPLES. */
typedef struct {
    ml_u64 step_q48;
    ml_int mip;
    ml_f warp, morph, blend;
    ml_int fine[2], coarse[2];          /* this family and the next */
} ml_raster_plan;

static inline ml_raster_plan ml_raster_plan_for(const ml_raster_in *in) {
    ml_f step = ml_phase_step(in->base_hz, in->render_rate_hz);
    ml_raster_plan p = {
        .step_q48 = ml_f_to_u64(step * 0x1p48f),
        .warp = in->warp,
    };
    p.mip = ml_raster_mip(step, in->warp, &p.blend);
    ml_f family_position = in->position * (float)(MA_RASTER_FAMILIES - 1);
    ml_int family = ml_trunc(family_position);
    p.morph = family_position - ml_int_to_f(family);
    ml_int next_family = ml_ifi(family + 1 < MA_RASTER_FAMILIES, family + 1, family);
    ml_int coarse_mip = ml_ifi(p.mip + 1 < MA_RASTER_MIPS, p.mip + 1, p.mip);
    p.fine[0] = (family * MA_RASTER_MIPS + p.mip) * MA_RASTER_SAMPLES;
    p.fine[1] = (next_family * MA_RASTER_MIPS + p.mip) * MA_RASTER_SAMPLES;
    p.coarse[0] = (family * MA_RASTER_MIPS + coarse_mip) * MA_RASTER_SAMPLES;
    p.coarse[1] = (next_family * MA_RASTER_MIPS + coarse_mip) * MA_RASTER_SAMPLES;
    return p;
}

/* One row, read at a table phase and interpolated. */
static inline ml_f ml_raster_read(ml_int row, ml_int index, ml_f fraction) {
    ml_int a = ML_INT(0), b = ML_INT(0);
    ml_i16_pair(&MA_RASTER_TABLE[0][0][0], row, index, MA_RASTER_SAMPLES, &a, &b);
    ml_f first = ml_int_to_f(a) * (1.0f / 32768.0f);
    ml_f second = ml_int_to_f(b) * (1.0f / 32768.0f);
    return first + fraction * (second - first);
}

/* Two families at one mip, morphed. */
static inline ml_f ml_raster_morph(const ml_int rows[2], ml_int index, ml_f fraction,
                                   ml_f morph) {
    ml_f first = ml_raster_read(rows[0], index, fraction);
    ml_f second = ml_raster_read(rows[1], index, fraction);
    return first + morph * (second - first);
}

/* One raster sample at the render rate, and the phase step after it. The
 * warp bends the read phase by a sine of itself. */
static inline ml_f ml_raster_step(const ml_raster_plan *p, ml_u64 *phase_q48) {
    ml_f phase = ml_turns(*phase_q48);
    ml_f warped = ml_fmadd(0.12f * p->warp, ml_sin_turns(phase), phase);
    warped = ml_if(warped < 0.0f, warped + 1.0f, warped);
    warped = ml_if(warped >= 1.0f, warped - 1.0f, warped);
    ml_f table_phase = warped * (float)MA_RASTER_SAMPLES;
    ml_int whole = ml_trunc(table_phase);
    ml_int index = whole & (MA_RASTER_SAMPLES - 1);
    ml_f fraction = table_phase - ml_int_to_f(whole);
    ml_f sample = ml_raster_morph(p->fine, index, fraction, p->morph);
    ml_m blend = p->blend > 0.0f;
    if (ml_any(blend)) {
        ml_f coarse = ml_raster_morph(p->coarse, index, fraction, p->morph);
        sample = ml_if(blend, sample + p->blend * (coarse - sample), sample);
    }
    *phase_q48 = ml_u64_and(ml_u64_add(*phase_q48, p->step_q48), ML_U64(PHASE_MASK_Q48));
    return sample;
}

/* ---- BCS ---- */

typedef struct { ml_f hopf_re, hopf_im, duffing_x, duffing_v; } ml_bcs_vector;

typedef struct {
    ml_f hopf_mu, hopf_nonlinearity, duffing_frequency_ratio, duffing_damping,
         duffing_edge, duffing_cubic_stiffness, duffing_cubic_damping,
         duffing_drive, duffing_feedback, hopf_mix, duffing_mix, output_gain;
} ml_bcs_parameters;

typedef struct {
    ml_f regime, amount, base_hz, sample_rate_hz;
    ml_f feedback;                      /* the ladder's last y4 */
} ml_bcs_in;

typedef struct {
    ml_bcs_vector state;
    ml_f peak, output;
    ml_m safe;
} ml_bcs_out;

/* The parameters between the two landmarks around the regime. */
static inline ml_bcs_parameters ml_bcs_parameters_at(ml_f regime) {
    ml_f position = regime * 3.0f;
    ml_int landmark = ml_trunc(position);
    landmark = ml_ifi(landmark > 2, ML_INT(2), landmark);
    ml_f fraction = position - ml_int_to_f(landmark);
    ml_bcs_parameters p = {};
#define BCS_LERP(m) do { \
        const float table[4] = { BCS_LANDMARK[0].m, BCS_LANDMARK[1].m, \
                                 BCS_LANDMARK[2].m, BCS_LANDMARK[3].m }; \
        ml_f first = ml_pick4(table, landmark); \
        p.m = first + fraction * (ml_pick4(table, landmark + 1) - first); \
    } while (0)
    BCS_LERP(hopf_mu);
    BCS_LERP(hopf_nonlinearity);
    BCS_LERP(duffing_frequency_ratio);
    BCS_LERP(duffing_damping);
    BCS_LERP(duffing_edge);
    BCS_LERP(duffing_cubic_stiffness);
    BCS_LERP(duffing_cubic_damping);
    BCS_LERP(duffing_drive);
    BCS_LERP(duffing_feedback);
    BCS_LERP(hopf_mix);
    BCS_LERP(duffing_mix);
    BCS_LERP(output_gain);
#undef BCS_LERP
    return p;
}

static inline ml_bcs_vector ml_bcs_derivative(ml_bcs_vector y, const ml_bcs_parameters *q,
                                              ml_f omega, ml_f external_drive) {
    ml_f radius2 = ml_fmadd(y.hopf_re, y.hopf_re, y.hopf_im * y.hopf_im);
    ml_f radial = ml_fnmadd(q->hopf_nonlinearity, radius2, q->hopf_mu);
    ml_f duffing_omega = omega * q->duffing_frequency_ratio;
    ml_f square = y.duffing_x * y.duffing_x;
    ml_f edge_damping = (q->duffing_edge - q->duffing_damping) * duffing_omega;
    ml_f nonlinear_damping = q->duffing_cubic_damping * duffing_omega * square;
    ml_f cubic = q->duffing_cubic_stiffness * duffing_omega * square * y.duffing_x;
    ml_f drive = ml_fmadd(q->duffing_drive, y.hopf_re, external_drive);
    ml_f spring = ml_fnmadd(duffing_omega, y.duffing_x,
                            (edge_damping - nonlinear_damping) * y.duffing_v);
    return (ml_bcs_vector){
        .hopf_re = ml_fmadd(q->duffing_feedback * omega, y.duffing_x,
                            ml_fnmadd(omega, y.hopf_im, radial * y.hopf_re)),
        .hopf_im = ml_fmadd(omega, y.hopf_re, radial * y.hopf_im),
        .duffing_x = duffing_omega * y.duffing_v,
        .duffing_v = ml_fmadd(duffing_omega, drive, spring - cubic),
    };
}

static inline ml_bcs_vector ml_bcs_advance(ml_bcs_vector y, ml_bcs_vector d, ml_f scale) {
    return (ml_bcs_vector){
        .hopf_re = ml_fmadd(scale, d.hopf_re, y.hopf_re),
        .hopf_im = ml_fmadd(scale, d.hopf_im, y.hopf_im),
        .duffing_x = ml_fmadd(scale, d.duffing_x, y.duffing_x),
        .duffing_v = ml_fmadd(scale, d.duffing_v, y.duffing_v),
    };
}

/* One output sample: BCS_INTEGRATION_STEPS RK4 steps from `y`. The state is
 * safe when every value is finite and within BCS_STATE_CEILING; `peak` is
 * the largest magnitude. */
static inline ml_bcs_out ml_bcs_sample(const ml_bcs_in *in, ml_bcs_vector y) {
    ml_bcs_parameters p = ml_bcs_parameters_at(in->regime);
    ml_f upper_hz = 0.125f * in->sample_rate_hz;
    upper_hz = ml_if(upper_hz > 8000.0f, 8000.0f, upper_hz);
    ml_f omega = BCS_TAU * ml_clamp(in->base_hz, 20.0f, upper_hz);
    ml_f h = 1.0f / (in->sample_rate_hz * (float)BCS_INTEGRATION_STEPS);
    ml_f external_drive = 0.018f * in->amount * ml_clamp(in->feedback, -1.0f, 1.0f);
    for (int substep = 0; substep < BCS_INTEGRATION_STEPS; substep++) {
        ml_bcs_vector k1 = ml_bcs_derivative(y, &p, omega, external_drive);
        ml_bcs_vector k2 = ml_bcs_derivative(ml_bcs_advance(y, k1, 0.5f * h), &p, omega,
                                             external_drive);
        ml_bcs_vector k3 = ml_bcs_derivative(ml_bcs_advance(y, k2, 0.5f * h), &p, omega,
                                             external_drive);
        ml_bcs_vector k4 = ml_bcs_derivative(ml_bcs_advance(y, k3, h), &p, omega,
                                             external_drive);
        ml_f sixth = h / 6.0f;
        y.hopf_re = ml_fmadd(sixth, k1.hopf_re + 2.0f * k2.hopf_re + 2.0f * k3.hopf_re
                                    + k4.hopf_re, y.hopf_re);
        y.hopf_im = ml_fmadd(sixth, k1.hopf_im + 2.0f * k2.hopf_im + 2.0f * k3.hopf_im
                                    + k4.hopf_im, y.hopf_im);
        y.duffing_x = ml_fmadd(sixth, k1.duffing_x + 2.0f * k2.duffing_x
                                      + 2.0f * k3.duffing_x + k4.duffing_x, y.duffing_x);
        y.duffing_v = ml_fmadd(sixth, k1.duffing_v + 2.0f * k2.duffing_v
                                      + 2.0f * k3.duffing_v + k4.duffing_v, y.duffing_v);
    }
    ml_bcs_out out = { .state = y, .peak = ML(0.0f), .safe = ML_TRUE };
    const ml_f values[4] = { y.hopf_re, y.hopf_im, y.duffing_x, y.duffing_v };
    for (int k = 0; k < 4; k++) {
        ml_f magnitude = ml_abs(values[k]);
        ml_m finite = (values[k] >= -FLT_MAX) & (values[k] <= FLT_MAX);
        out.safe = out.safe & finite & (magnitude <= BCS_STATE_CEILING);
        out.peak = ml_if(magnitude > out.peak, magnitude, out.peak);
    }
    out.output = ml_sat(ml_fmadd(p.hopf_mix, y.hopf_re, p.duffing_mix * y.duffing_x)
                        * p.output_gain);
    return out;
}

#endif
