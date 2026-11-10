/* Mamut Analog digital and nonlinear sources for one voice: the sources
 * kernel (ma_sources_kernel.h) at width one. */
#include "ma_internal.h"
#include "ma_raster_table.h"
#include "ma_voice_impl.h"
#define ML_W 1
#include "ma_lane_ops.h"
#include "ma_sources_kernel.h"

uint8_t ma_voice_raster_mip(float phase_step, float warp, float *blend) {
    return (uint8_t)ml_raster_mip(phase_step, warp, blend);
}

float ma_voice_render_raster(ma_synth *s, const ma_render_controls *controls,
                             float frequency_hz, float render_rate_hz) {
    const ml_raster_in in = {
        .base_hz = frequency_hz,
        .render_rate_hz = render_rate_hz,
        .position = controls->raster_position,
        .warp = controls->raster_warp,
    };
    const ml_raster_plan plan = ml_raster_plan_for(&in);
    s->raster.mip = (uint8_t)plan.mip;
    return ml_raster_step(&plan, &s->raster.phase_q48);
}

float ma_voice_render_bcs(ma_synth *s, float frequency_hz, float amount,
                          float regime, float feedback_sample) {
    const ml_bcs_in in = {
        .regime = regime,
        .amount = amount,
        .base_hz = frequency_hz,
        .sample_rate_hz = s->sample_rate_hz,
        .feedback = feedback_sample,
    };
    ma_bcs *bcs = &s->bcs;
    const ml_bcs_out out = ml_bcs_sample(&in, (ml_bcs_vector){
        bcs->hopf_re, bcs->hopf_im, bcs->duffing_x, bcs->duffing_v });
    if (!out.safe) {
        reset_bcs(bcs);
        return 0.0f;
    }
    bcs->hopf_re = out.state.hopf_re;
    bcs->hopf_im = out.state.hopf_im;
    bcs->duffing_x = out.state.duffing_x;
    bcs->duffing_v = out.state.duffing_v;
    if (out.peak > bcs->maximum_state) bcs->maximum_state = out.peak;
    bcs->output = out.output;
    return out.output;
}
