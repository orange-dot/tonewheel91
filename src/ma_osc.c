/* Mamut Analog oscillators for one voice: the 8x source frame, the
 * oscillator kernel (ma_osc_kernel.h) at width one, and the 31-tap halfband
 * that takes the source from 8x to 2x in two stages. */
#include "ma_internal.h"
#include "ma_voice_impl.h"
#define ML_W 1
#include "ma_lane_ops.h"
#include "ma_osc_kernel.h"

float ma_octave_ratio_small(float octaves) {
    return ml_octave_ratio_small(octaves);
}

static ml_vco vco_of(ma_vco_controls c) {
    return (ml_vco){ c.saw_level, c.pulse_level, c.triangle_level, c.sine_level,
                     c.pulse_width };
}

static ma_vco_controls controls_of(ml_vco c) {
    return (ma_vco_controls){ c.saw_level, c.pulse_level, c.triangle_level,
                              c.sine_level, c.pulse_width };
}

static unsigned active_of(ml_shapes on) {
    return (on.saw ? WAVE_SAW : 0u) | (on.pulse ? WAVE_PULSE : 0u)
         | (on.triangle ? WAVE_TRIANGLE : 0u);
}

ma_source_frame ma_voice_prepare_source_frame_guarded(
    const ma_synth *s, const ma_render_controls *controls, float base_hz,
    float filter_modulation, float guard1_gain, float guard2_gain) {
    const ml_source_in x = {
        .vco1 = vco_of(controls->vco1),
        .vco2 = vco_of(controls->vco2),
        .sample_rate_hz = s->sample_rate_hz,
        .base_hz = base_hz,
        .interval_ratio = interval_ratio(s->vco2_interval),
        .vco2_fine_cents = controls->vco2_fine_cents,
        .character = s->character.smoother.current,
        .vco2_cents = s->character.vco2_cents,
        .sync_amount = controls->sync_amount,
        .sync_softness = controls->sync_softness,
        .crossmod_amount = controls->crossmod_amount,
        .vco2_level = controls->vco2_level,
        .noise_level = controls->noise_level,
        .guard1 = guard1_gain,
        .guard2 = guard2_gain,
    };
    ml_source src = {};
    ml_prepare_source(&x, filter_modulation, &src);
    return (ma_source_frame){
        .vco1 = controls_of(src.vco1),
        .vco2 = controls_of(src.vco2),
        .vco1_active = active_of(src.vco1_on),
        .vco2_active = active_of(src.vco2_on),
        .vco1_divisor = src.vco1_divisor,
        .vco2_divisor = src.vco2_divisor,
        .mixer_divisor = src.mixer_divisor,
        .rate_hz = src.rate_hz,
        .vco1_hz = src.vco1_hz,
        .vco1_step = src.vco1_step,
        .vco1_step_q48 = src.vco1_step_q48,
        .vco1_seed_step = src.vco1_seed_step,
        .vco1_triangle_gain = src.vco1_triangle_gain,
        .vco1_triangle_step = src.vco1_triangle_step,
        .vco2_hz = src.vco2_hz,
        .vco2_step = src.vco2_step,
        .vco2_step_q48 = src.vco2_step_q48,
        .vco2_triangle_gain = src.vco2_triangle_gain,
        .vco2_triangle_step = src.vco2_triangle_step,
        .envelope_ratio = src.envelope_ratio,
        .sync = src.sync,
    };
}

/* Used by the MA_SOURCE_EVIDENCE tick and by white-box spikes. */
ma_source_frame ma_voice_prepare_source_frame(const ma_synth *s,
                                              const ma_render_controls *controls,
                                              float base_hz,
                                              float filter_modulation) {
    return ma_voice_prepare_source_frame_guarded(s, controls, base_hz, filter_modulation,
                                                 s->oscillator1.guard_gain,
                                                 s->oscillator2.guard_gain);
}

/* ---- the kernel at width one, on the voice's own oscillator state ---- */

static ml_osc osc_load(const ma_oscillator *o) {
    return (ml_osc){
        .phase_q48 = o->phase_q48,
        .triangle = o->triangle,
        .sync_residual = o->sync_residual,
        .guard_gain = o->guard_gain,
        .guard_target = o->guard_target,
        .guard_step = o->guard_step,
        .guard_frames = o->guard_frames,
        .triangle_initialized = o->triangle_initialized,
    };
}

static void osc_store(ma_oscillator *o, const ml_osc *x) {
    o->phase_q48 = x->phase_q48;
    o->triangle = x->triangle;
    o->sync_residual = x->sync_residual;
    o->guard_gain = x->guard_gain;
    o->guard_target = x->guard_target;
    o->guard_step = x->guard_step;
    o->guard_frames = (uint16_t)x->guard_frames;
    o->triangle_initialized = x->triangle_initialized;
}

static ml_osc_pair pair_load(const ma_synth *s) {
    const ma_oscillator *vco2 = &s->oscillator2;
    return (ml_osc_pair){
        .vco1 = osc_load(&s->oscillator1),
        .vco2 = osc_load(vco2),
        .sync_run_q48 = vco2->sync_run_q48,
        .sync_carry = { vco2->sync_carry[0], vco2->sync_carry[1], vco2->sync_carry[2] },
    };
}

static void pair_store(ma_synth *s, const ml_osc_pair *v) {
    osc_store(&s->oscillator1, &v->vco1);
    osc_store(&s->oscillator2, &v->vco2);
    s->oscillator2.sync_run_q48 = v->sync_run_q48;
    for (int k = 0; k < 3; k++) s->oscillator2.sync_carry[k] = v->sync_carry[k];
}

static ml_source source_of(const ma_synth *s, const ma_render_controls *controls,
                           const ma_source_frame *f, float base_hz, float noise,
                           float guard1, float guard2) {
    uint16_t frames = smoothing_frames(s->sample_rate_hz);
    return (ml_source){
        .vco1 = vco_of(f->vco1),
        .vco2 = vco_of(f->vco2),
        .vco1_on = { (f->vco1_active & WAVE_SAW) != 0, (f->vco1_active & WAVE_PULSE) != 0,
                     (f->vco1_active & WAVE_TRIANGLE) != 0 },
        .vco2_on = { (f->vco2_active & WAVE_SAW) != 0, (f->vco2_active & WAVE_PULSE) != 0,
                     (f->vco2_active & WAVE_TRIANGLE) != 0 },
        .vco1_divisor = f->vco1_divisor,
        .vco2_divisor = f->vco2_divisor,
        .mixer_divisor = f->mixer_divisor,
        .rate_hz = f->rate_hz,
        .vco1_hz = f->vco1_hz,
        .vco1_step = f->vco1_step,
        .vco1_seed_step = f->vco1_seed_step,
        .vco1_triangle_gain = f->vco1_triangle_gain,
        .vco1_triangle_step = f->vco1_triangle_step,
        .vco1_step_q48 = f->vco1_step_q48,
        .vco2_hz = f->vco2_hz,
        .vco2_step = f->vco2_step,
        .vco2_triangle_gain = f->vco2_triangle_gain,
        .vco2_triangle_step = f->vco2_triangle_step,
        .vco2_step_q48 = f->vco2_step_q48,
        .envelope_ratio = f->envelope_ratio,
        .sync = f->sync,
        .base_hz = base_hz,
        .noise = noise,
        .guard1 = guard1,
        .guard2 = guard2,
        .crossmod_amount = controls->crossmod_amount,
        .vco2_level = controls->vco2_level,
        .noise_level = controls->noise_level,
        .guard_hz = 0.45f * s->sample_rate_hz,
        .guard_frames = frames,
        .guard_frames_f = (float)frames,
    };
}

void ma_voice_render_source_block(ma_synth *s, const ma_render_controls *controls,
                                  const ma_source_frame *source, float base_hz,
                                  float noise, float guard1, float guard2,
                                  float out[8]) {
    ml_osc_pair v = pair_load(s);
    ml_source in = source_of(s, controls, source, base_hz, noise, guard1, guard2);
    ml_source_block(&v, &in, out);
    pair_store(s, &v);
}

/* The block's parts, for white-box gates. */
void ma_voice_seed_triangles(ma_synth *s, const ma_source_frame *source) {
    ml_osc_pair v = pair_load(s);
    ml_seed_triangle(&v.vco1, source->vco1_seed_step, source->vco1_triangle_gain);
    ml_seed_triangle(&v.vco2, source->vco2_step, source->vco2_triangle_gain);
    pair_store(s, &v);
}

float ma_voice_render_source_substep(ma_synth *s,
                                     const ma_render_controls *controls,
                                     const ma_source_frame *source,
                                     float base_hz, float noise,
                                     float guard1, float guard2) {
    ml_osc_pair v = pair_load(s);
    ml_source in = source_of(s, controls, source, base_hz, noise, guard1, guard2);
    float y = ml_source_substep(&v, &in);
    pair_store(s, &v);
    return y;
}

void ma_voice_decimate_source(ma_synth *s, const float source[8], float at_2x[2]) {
    ml_decimate_source(s->oversample_history[0], s->oversample_history[1],
                       s->oversample_history_pos, source, at_2x);
}

void ma_voice_push_oversample_history(float history[31], uint8_t *position,
                                      float sample) {
    ml_push(history, 31, position, sample);
}

float ma_voice_decimate_oversample(const float history[31], uint8_t position) {
    return ml_halfband31(history, position);
}
