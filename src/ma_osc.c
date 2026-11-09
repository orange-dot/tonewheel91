/* Mamut Analog oscillators: the BLEP waveforms, the 8x source frame and
 * one oversampled source substep (both VCOs, sync, cross-mod, noise). */
#include "ma_internal.h"
#include "ma_voice_impl.h"

static float poly_blep(float phase, float step) {
    if (!(step > FLT_EPSILON)) return 0.0f;
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    if (phase < edge) {
        float x = 0.5f * (phase / edge + 1.0f);
        return 2.0f * (smoothstep5(x) - 1.0f);
    }
    if (phase > 1.0f - edge) {
        float x = 0.5f * ((phase - 1.0f) / edge + 1.0f);
        return 2.0f * smoothstep5(x);
    }
    return 0.0f;
}

static float saw_sample(float phase, float blep) {
    return clamp_signal(2.0f * phase - 1.0f - blep,
                        -1.25f, 1.25f);
}

static float pulse_sample_with_blep(float phase, float width, float step,
                                     float blep) {
    float sample = phase < width ? 1.0f : -1.0f;
    sample += blep;
    sample -= poly_blep(wrap_phase(phase - width), step);
    return clamp_signal(sample, -1.25f, 1.25f);
}

static float pulse_sample(float phase, float width, float step) {
    return pulse_sample_with_blep(phase, width, step, poly_blep(phase, step));
}

static float mamut_sine(float phase) {
    static constexpr float DRIVE = 1.45f;
    static constexpr float SECOND_LEVEL = 0.07f;
    static constexpr float PEAK_NORMALIZATION = 0.95591217f;
    float fundamental = ma_sin_turns(phase);
    float warm = ma_sat(DRIVE * fundamental) / ma_sat(DRIVE);
    float second = ma_sin_turns(wrap_phase(2.0f * phase + 0.07f));
    return PEAK_NORMALIZATION * ma_fmadd(SECOND_LEVEL, second, warm);
}

static float waveform_divisor(ma_vco_controls controls) {
    float sum = controls.saw_level + controls.pulse_level
              + controls.triangle_level + controls.sine_level;
    return sum > 1.0f ? sum : 1.0f;
}

static float raw_wave_mix(float phase, ma_vco_controls controls,
                           float divisor) {
    float saw = 2.0f * phase - 1.0f;
    float pulse = phase < controls.pulse_width ? 1.0f : -1.0f;
    float triangle = raw_triangle(phase);
    float sample = ma_fmadd(controls.triangle_level, triangle,
                            ma_fmadd(controls.pulse_level, pulse, controls.saw_level * saw));
    if (controls.sine_level > 0.0f)
        sample = ma_fmadd(controls.sine_level, mamut_sine(phase), sample);
    return sample / divisor;
}

static unsigned active_waveforms(ma_vco_controls controls) {
    return (controls.saw_level != 0.0f ? WAVE_SAW : 0)
         | (controls.pulse_level != 0.0f ? WAVE_PULSE : 0)
         | (controls.triangle_level != 0.0f ? WAVE_TRIANGLE : 0);
}

static float preview_oscillator(const ma_oscillator *oscillator,
                                ma_vco_controls controls, float step,
                                float divisor) {
    float phase = phase_turns(oscillator->phase_q48);
    float blep = poly_blep(phase, step);
    float triangle = oscillator->triangle_initialized
                   ? clamp_signal(oscillator->triangle, -1.0f, 1.0f)
                   : raw_triangle(phase);
    float sample = ma_fmadd(controls.triangle_level, triangle,
                            ma_fmadd(controls.pulse_level,
                                     pulse_sample_with_blep(phase, controls.pulse_width,
                                                            step, blep),
                                     controls.saw_level * saw_sample(phase, blep)));
    if (controls.sine_level > 0.0f)
        sample = ma_fmadd(controls.sine_level, mamut_sine(phase), sample);
    return sample / divisor;
}

static float preview_sparse_oscillator(const ma_oscillator *oscillator,
                                ma_vco_controls controls, float step,
                                float divisor, unsigned active) {
    float phase = phase_turns(oscillator->phase_q48);
    float saw = 0.0f, pulse = 0.0f, triangle = 0.0f;
    /* Saw and pulse share this edge; pulse's width-shifted edge is distinct. */
    float blep = active & (WAVE_SAW | WAVE_PULSE)
               ? poly_blep(phase, step) : 0.0f;
    if (active & WAVE_SAW) saw = saw_sample(phase, blep);
    if (active & WAVE_PULSE)
        pulse = pulse_sample_with_blep(phase, controls.pulse_width, step, blep);
    if (active & WAVE_TRIANGLE)
        triangle = oscillator->triangle_initialized
                 ? clamp_signal(oscillator->triangle, -1.0f, 1.0f)
                 : raw_triangle(phase);
    /* Only preview work is optional. render_oscillator still advances its
     * triangle integrator even when the triangle contribution is zero. */
    float sample = ma_fmadd(controls.triangle_level, triangle,
                            ma_fmadd(controls.pulse_level, pulse, controls.saw_level * saw));
    if (controls.sine_level > 0.0f)
        sample = ma_fmadd(controls.sine_level, mamut_sine(phase), sample);
    return sample / divisor;
}

static float render_oscillator(ma_oscillator *oscillator,
                               float preview, float step,
                               float sync_correction) {
    if (!oscillator->triangle_initialized) {
        oscillator->triangle = raw_triangle(
            phase_turns(oscillator->phase_q48));
        oscillator->triangle_initialized = true;
    }
    float sample = preview + oscillator->sync_residual + sync_correction;
    oscillator->sync_residual = 0.0f;
    oscillator->triangle = clamp_signal(
        ma_fmadd(pulse_sample(phase_turns(oscillator->phase_q48), 0.5f, step) * 4.0f,
                 step, oscillator->triangle),
        -1.25f, 1.25f);
    return clamp_signal(sample, -1.25f, 1.25f);
}

static bool phase_will_wrap(const ma_oscillator *oscillator,
                            uint64_t step_q48) {
    return oscillator->phase_q48 + step_q48 >= PHASE_ONE_Q48;
}

static void advance_phase(ma_oscillator *oscillator, uint64_t step_q48) {
    oscillator->phase_q48 = (oscillator->phase_q48 + step_q48)
                          & PHASE_MASK_Q48;
}

void ma_voice_set_guard_target(ma_oscillator *oscillator, float target,
                               float sample_rate_hz) {
    if (target == oscillator->guard_target) return;
    oscillator->guard_target = target;
    oscillator->guard_frames = smoothing_frames(sample_rate_hz);
    oscillator->guard_step = (target - oscillator->guard_gain)
                           / (float)oscillator->guard_frames;
}

float ma_voice_next_guard_gain(ma_oscillator *oscillator) {
    if (oscillator->guard_frames == 0) return oscillator->guard_gain;
    oscillator->guard_gain += oscillator->guard_step;
    oscillator->guard_frames--;
    if (oscillator->guard_frames == 0)
        oscillator->guard_gain = oscillator->guard_target;
    return oscillator->guard_gain;
}

ma_source_frame ma_voice_prepare_source_frame_guarded(
    const ma_synth *s, const ma_render_controls *controls, float base_hz,
    float filter_modulation, float guard1_gain, float guard2_gain) {
    ma_source_frame source = {
        .vco1 = controls->vco1,
        .vco2 = controls->vco2,
        .vco1_active = active_waveforms(controls->vco1),
        .vco2_active = active_waveforms(controls->vco2),
        .vco1_divisor = waveform_divisor(controls->vco1),
        .vco2_divisor = waveform_divisor(controls->vco2),
        .rate_hz = 8.0f * s->sample_rate_hz,
        .envelope_ratio = 1.0f,
        .sync = controls->sync_amount
              * clamp_signal(1.0f - 0.75f * controls->sync_softness,
                             0.0f, 1.0f),
    };
#if !defined(MA_SOURCE_EVIDENCE)
    source.vco1.pulse_width = clamp_signal(
        source.vco1.pulse_width + 0.120f * filter_modulation,
        0.05f, 0.95f);
    source.vco2.pulse_width = clamp_signal(
        source.vco2.pulse_width - 0.080f * filter_modulation,
        0.05f, 0.95f);
    source.envelope_ratio = octave_ratio_small(0.125f * filter_modulation);
#else
    (void)filter_modulation;
#endif
    float vco1_weight = guard1_gain > 0.0f ? 1.0f : 0.0f;
    float vco2_weight = guard2_gain > 0.0f ? controls->vco2_level : 0.0f;
    source.mixer_divisor = vco1_weight + vco2_weight + controls->noise_level;
    if (source.mixer_divisor < 1.0f) source.mixer_divisor = 1.0f;
    /* Cross-mod is the only source of pitch changes within these eight
     * substeps. Character, envelopes and LFO have already advanced. */
    if (controls->crossmod_amount == 0.0f) {
        source.vco1_hz = base_hz;
#if !defined(MA_SOURCE_EVIDENCE)
        source.vco1_hz *= source.envelope_ratio;
#endif
        source.vco1_step_q48 = phase_step_q48(
            phase_step(source.vco1_hz, source.rate_hz));
        source.vco1_step = phase_turns(source.vco1_step_q48);
    }
    float fine_ratio = octave_ratio_small(
        controls->vco2_fine_cents / 1200.0f);
    source.vco2_hz = base_hz * interval_ratio(s->vco2_interval) * fine_ratio;
    if (s->character.smoother.current > 0.0f)
        source.vco2_hz *= octave_ratio_small(s->character.smoother.current
                                            * s->character.vco2_cents / 1200.0f);
    float step = phase_step(source.vco2_hz, source.rate_hz);
    source.vco2_step_q48 = phase_step_q48(step);
    source.vco2_step = phase_turns(source.vco2_step_q48);
    return source;
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

float ma_voice_render_source_substep(ma_synth *s,
                                     const ma_render_controls *controls,
                                     const ma_source_frame *source,
                                     float base_hz, float noise,
                                     float guard1, float guard2) {
    ma_vco_controls vco1_controls = source->vco1;
    ma_vco_controls vco2_controls = source->vco2;
    float vco2_step = source->vco2_step;
    uint64_t vco2_step_q48 = source->vco2_step_q48;
    float vco2_preview = source->vco2_active == WAVE_ALL
        ? preview_oscillator(&s->oscillator2, vco2_controls,
                              vco2_step, source->vco2_divisor)
        : preview_sparse_oscillator(&s->oscillator2, vco2_controls,
                                     vco2_step, source->vco2_divisor,
                                     source->vco2_active);
    float vco1_hz = source->vco1_hz;
    float vco1_step = source->vco1_step;
    uint64_t vco1_step_q48 = source->vco1_step_q48;
    if (controls->crossmod_amount != 0.0f) {
        float cross_ratio = clamp_signal(
            1.0f + vco2_preview * controls->crossmod_amount * 0.25f,
            0.25f, 4.0f);
        vco1_hz = base_hz * cross_ratio;
#if !defined(MA_SOURCE_EVIDENCE)
        vco1_hz *= source->envelope_ratio;
#endif
        vco1_step_q48 = phase_step_q48(phase_step(vco1_hz, source->rate_hz));
        vco1_step = phase_turns(vco1_step_q48);
    }

    float guard_hz = 0.45f * s->sample_rate_hz;
    ma_voice_set_guard_target(&s->oscillator1, vco1_hz < guard_hz ? 1.0f : 0.0f,
                              s->sample_rate_hz);
    bool master_wrap = phase_will_wrap(&s->oscillator1, vco1_step_q48);
    float sync = source->sync;
    float sync_now = 0.0f;
    float sync_next = 0.0f;
    float event_fraction = 1.0f;
    uint64_t sync_reset_phase_q48 = 0;
    if (master_wrap && sync > 0.0f && vco1_step > FLT_EPSILON) {
        uint64_t until_wrap = PHASE_ONE_Q48 - s->oscillator1.phase_q48;
        event_fraction = (float)until_wrap / (float)vco1_step_q48;
        event_fraction = clamp_signal(event_fraction, 0.0f, 1.0f);
        uint64_t event_advance = (uint64_t)((float)vco2_step_q48
                                            * event_fraction);
        uint64_t sync_event_phase_q48 =
            (s->oscillator2.phase_q48 + event_advance) & PHASE_MASK_Q48;
        sync_reset_phase_q48 =
            (uint64_t)((float)sync_event_phase_q48 * (1.0f - sync))
            & PHASE_MASK_Q48;
        float sync_event_phase = phase_turns(sync_event_phase_q48);
        float sync_reset_phase = phase_turns(sync_reset_phase_q48);
        float jump = raw_wave_mix(sync_reset_phase, vco2_controls,
                                   source->vco2_divisor)
                   - raw_wave_mix(sync_event_phase, vco2_controls,
                                   source->vco2_divisor);
        float before = 0.5f * (1.0f - event_fraction);
        float after = 1.0f - 0.5f * event_fraction;
        sync_now = jump * smoothstep5(before);
        sync_next = jump * (smoothstep5(after) - 1.0f);
    }

    float vco1_preview = source->vco1_active == WAVE_ALL
        ? preview_oscillator(&s->oscillator1, vco1_controls,
                              vco1_step, source->vco1_divisor)
        : preview_sparse_oscillator(&s->oscillator1, vco1_controls,
                                     vco1_step, source->vco1_divisor,
                                     source->vco1_active);
    float vco1 = render_oscillator(&s->oscillator1, vco1_preview,
                                   vco1_step, 0.0f);
    /* Cross-mod already read VCO2 at this phase, before sync correction. */
    float vco2 = render_oscillator(&s->oscillator2, vco2_preview,
                                   vco2_step, sync_now);
    s->oscillator2.sync_residual += sync_next;
    advance_phase(&s->oscillator1, vco1_step_q48);
    if (master_wrap && sync > 0.0f) {
        float remaining = 1.0f - event_fraction;
        uint64_t remaining_advance = (uint64_t)((float)vco2_step_q48
                                                * remaining);
        s->oscillator2.phase_q48 =
            (sync_reset_phase_q48 + remaining_advance) & PHASE_MASK_Q48;
        float sync_reset_phase = phase_turns(sync_reset_phase_q48);
        s->oscillator2.triangle = raw_triangle(sync_reset_phase);
        s->oscillator2.triangle = clamp_signal(
            ma_fmadd(pulse_sample(sync_reset_phase, 0.5f, vco2_step * remaining)
                         * 4.0f * vco2_step,
                     remaining, s->oscillator2.triangle),
            -1.25f, 1.25f);
        s->oscillator2.triangle_initialized = true;
    } else {
        advance_phase(&s->oscillator2, vco2_step_q48);
    }

    return (guard1 * vco1 + guard2 * controls->vco2_level * vco2
            + controls->noise_level * noise) / source->mixer_divisor;
}
