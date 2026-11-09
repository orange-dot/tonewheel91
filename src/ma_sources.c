/* Mamut Analog digital and nonlinear sources: the raster wavetable (MA2
 * DIG) and the BCS oscillator (a Hopf/Duffing pair under RK4). */
#include "ma_internal.h"
#include "ma_raster_table.h"
#include "ma_voice_impl.h"

uint8_t ma_voice_raster_mip(float phase_step, float warp, float *blend) {
    static constexpr uint8_t harmonic_limit[MA_RASTER_MIPS] = {
        64, 32, 16, 8, 4, 2, 1,
    };
    float effective_step = phase_step * (1.0f + 0.8f * warp);
    uint8_t mip = 0;
    while (mip + 1 < MA_RASTER_MIPS
           && effective_step * harmonic_limit[mip] > 0.45f)
        mip++;
    float load = effective_step * harmonic_limit[mip];
    *blend = mip + 1 < MA_RASTER_MIPS
           ? clamp_signal((load - 0.225f) * (1.0f / 0.225f), 0.0f, 1.0f)
           : 0.0f;
    return mip;
}

static float raster_table_sample(uint8_t family, uint8_t mip, float phase) {
    float table_phase = phase * (float)MA_RASTER_SAMPLES;
    uint16_t index = (uint16_t)table_phase & (MA_RASTER_SAMPLES - 1);
    uint16_t next = (index + 1) & (MA_RASTER_SAMPLES - 1);
    float fraction = table_phase - (float)(uint16_t)table_phase;
    float first = (float)MA_RASTER_TABLE[family][mip][index]
                * (1.0f / 32768.0f);
    float second = (float)MA_RASTER_TABLE[family][mip][next]
                 * (1.0f / 32768.0f);
    return first + fraction * (second - first);
}

float ma_voice_render_raster(ma_synth *s, const ma_render_controls *controls,
                             float frequency_hz, float render_rate_hz) {
    float step = phase_step(frequency_hz, render_rate_hz);
    uint64_t step_q48 = phase_step_q48(step);
    float phase = phase_turns(s->raster.phase_q48);
    float warped = ma_fmadd(0.12f * controls->raster_warp, ma_sin_turns(phase), phase);
    if (warped < 0.0f) warped += 1.0f;
    if (warped >= 1.0f) warped -= 1.0f;

    float family_position = controls->raster_position
                          * (float)(MA_RASTER_FAMILIES - 1);
    uint8_t family = (uint8_t)family_position;
    float morph = family_position - (float)family;
    uint8_t next_family = family;
    if (family + 1 < MA_RASTER_FAMILIES) next_family++;
    float mip_blend = 0.0f;
    s->raster.mip = ma_voice_raster_mip(step, controls->raster_warp, &mip_blend);
    float first = raster_table_sample(family, s->raster.mip, warped);
    float second = raster_table_sample(next_family, s->raster.mip, warped);
    float sample = first + morph * (second - first);
    if (mip_blend > 0.0f) {
        uint8_t coarse_mip = s->raster.mip + 1;
        first = raster_table_sample(family, coarse_mip, warped);
        second = raster_table_sample(next_family, coarse_mip, warped);
        float coarse = first + morph * (second - first);
        sample += mip_blend * (coarse - sample);
    }
    s->raster.phase_q48 = (s->raster.phase_q48 + step_q48)
                        & PHASE_MASK_Q48;
    return sample;
}

static float bcs_lerp(float first, float second, float fraction) {
    return first + fraction * (second - first);
}

static ma_bcs_parameters bcs_parameters(float regime) {
    float position = regime * 3.0f;
    int landmark = (int)position;
    if (landmark > 2) landmark = 2;
    float fraction = position - (float)landmark;
    ma_bcs_parameters const *first = &BCS_LANDMARK[landmark];
    ma_bcs_parameters const *second = &BCS_LANDMARK[landmark + 1];
#define BCS_INTERPOLATE(member) \
    .member = bcs_lerp(first->member, second->member, fraction)
    return (ma_bcs_parameters){
        BCS_INTERPOLATE(hopf_mu),
        BCS_INTERPOLATE(hopf_nonlinearity),
        BCS_INTERPOLATE(duffing_frequency_ratio),
        BCS_INTERPOLATE(duffing_damping),
        BCS_INTERPOLATE(duffing_edge),
        BCS_INTERPOLATE(duffing_cubic_stiffness),
        BCS_INTERPOLATE(duffing_cubic_damping),
        BCS_INTERPOLATE(duffing_drive),
        BCS_INTERPOLATE(duffing_feedback),
        BCS_INTERPOLATE(hopf_mix),
        BCS_INTERPOLATE(duffing_mix),
        BCS_INTERPOLATE(output_gain),
    };
#undef BCS_INTERPOLATE
}

static ma_bcs_vector bcs_derivative(ma_bcs_vector state,
                                    ma_bcs_parameters parameters,
                                    float omega, float external_drive) {
    float radius2 = ma_fmadd(state.hopf_re, state.hopf_re,
                             state.hopf_im * state.hopf_im);
    float radial = ma_fnmadd(parameters.hopf_nonlinearity, radius2,
                             parameters.hopf_mu);
    float duffing_omega = omega * parameters.duffing_frequency_ratio;
    float square = state.duffing_x * state.duffing_x;
    float edge_damping = (parameters.duffing_edge
                          - parameters.duffing_damping) * duffing_omega;
    float nonlinear_damping = parameters.duffing_cubic_damping
                            * duffing_omega * square;
    float cubic = parameters.duffing_cubic_stiffness * duffing_omega
                * square * state.duffing_x;
    float drive = ma_fmadd(parameters.duffing_drive, state.hopf_re, external_drive);
    float spring = ma_fnmadd(duffing_omega, state.duffing_x,
                             (edge_damping - nonlinear_damping) * state.duffing_v);
    return (ma_bcs_vector){
        .hopf_re = ma_fmadd(parameters.duffing_feedback * omega, state.duffing_x,
                            ma_fnmadd(omega, state.hopf_im, radial * state.hopf_re)),
        .hopf_im = ma_fmadd(omega, state.hopf_re, radial * state.hopf_im),
        .duffing_x = duffing_omega * state.duffing_v,
        .duffing_v = ma_fmadd(duffing_omega, drive, spring - cubic),
    };
}

static ma_bcs_vector bcs_advance(ma_bcs_vector state,
                                 ma_bcs_vector derivative, float scale) {
    return (ma_bcs_vector){
        .hopf_re = ma_fmadd(scale, derivative.hopf_re, state.hopf_re),
        .hopf_im = ma_fmadd(scale, derivative.hopf_im, state.hopf_im),
        .duffing_x = ma_fmadd(scale, derivative.duffing_x, state.duffing_x),
        .duffing_v = ma_fmadd(scale, derivative.duffing_v, state.duffing_v),
    };
}

static bool bcs_state_is_safe(ma_bcs_vector state, float *maximum) {
    float values[4] = {
        state.hopf_re, state.hopf_im, state.duffing_x, state.duffing_v,
    };
    float peak = 0.0f;
    for (int i = 0; i < 4; i++) {
        if (!(values[i] >= -FLT_MAX && values[i] <= FLT_MAX)
            || tw_fabsf(values[i]) > BCS_STATE_CEILING)
            return false;
        if (tw_fabsf(values[i]) > peak) peak = tw_fabsf(values[i]);
    }
    *maximum = peak;
    return true;
}

float ma_voice_render_bcs(ma_synth *s, float frequency_hz, float amount,
                          float regime, float feedback_sample) {
    ma_bcs_parameters parameters = bcs_parameters(regime);
    float upper_hz = 0.125f * s->sample_rate_hz;
    if (upper_hz > 8000.0f) upper_hz = 8000.0f;
    frequency_hz = clamp_signal(frequency_hz, 20.0f, upper_hz);
    float omega = BCS_TAU * frequency_hz;
    float step = 1.0f / (s->sample_rate_hz * BCS_INTEGRATION_STEPS);
    float external_drive = 0.018f * amount
                         * clamp_signal(feedback_sample, -1.0f, 1.0f);
    ma_bcs_vector state = {
        s->bcs.hopf_re, s->bcs.hopf_im, s->bcs.duffing_x, s->bcs.duffing_v,
    };
    for (int substep = 0; substep < BCS_INTEGRATION_STEPS; substep++) {
        ma_bcs_vector k1 = bcs_derivative(state, parameters, omega,
                                          external_drive);
        ma_bcs_vector k2 = bcs_derivative(
            bcs_advance(state, k1, 0.5f * step), parameters, omega,
            external_drive);
        ma_bcs_vector k3 = bcs_derivative(
            bcs_advance(state, k2, 0.5f * step), parameters, omega,
            external_drive);
        ma_bcs_vector k4 = bcs_derivative(
            bcs_advance(state, k3, step), parameters, omega,
            external_drive);
        float sixth = step / 6.0f;
        state.hopf_re = ma_fmadd(sixth, k1.hopf_re + 2.0f * k2.hopf_re
                                 + 2.0f * k3.hopf_re + k4.hopf_re, state.hopf_re);
        state.hopf_im = ma_fmadd(sixth, k1.hopf_im + 2.0f * k2.hopf_im
                                 + 2.0f * k3.hopf_im + k4.hopf_im, state.hopf_im);
        state.duffing_x = ma_fmadd(sixth, k1.duffing_x + 2.0f * k2.duffing_x
                                   + 2.0f * k3.duffing_x + k4.duffing_x, state.duffing_x);
        state.duffing_v = ma_fmadd(sixth, k1.duffing_v + 2.0f * k2.duffing_v
                                   + 2.0f * k3.duffing_v + k4.duffing_v, state.duffing_v);
    }
    float maximum = 0.0f;
    if (!bcs_state_is_safe(state, &maximum)) {
        reset_bcs(&s->bcs);
        return 0.0f;
    }
    s->bcs.hopf_re = state.hopf_re;
    s->bcs.hopf_im = state.hopf_im;
    s->bcs.duffing_x = state.duffing_x;
    s->bcs.duffing_v = state.duffing_v;
    if (maximum > s->bcs.maximum_state) s->bcs.maximum_state = maximum;
    s->bcs.output = ma_sat(ma_fmadd(parameters.hopf_mix, state.hopf_re,
                                    parameters.duffing_mix * state.duffing_x)
                           * parameters.output_gain);
    return s->bcs.output;
}
