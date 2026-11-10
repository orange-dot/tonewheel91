/* Mamut Analog oscillators: the BLEP waveforms, the 8x source frame, one
 * oversampled source substep (both VCOs, sync, cross-mod, noise) and the
 * 31-tap halfband that takes the source from 8x to 2x in two stages. */
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

/* poly_blep for an oscillator that was sync-reset less than half a turn ago.
 * A reset moves the phase but not the edges already passed: the half that
 * follows an edge runs on the distance travelled since that edge was really
 * crossed. That is the phase when the crossing came after the reset (the
 * phase is less than `since`, the run since the reset), and `since + carry`
 * otherwise, `carry` being the distance behind the edge at the reset. The
 * half that precedes an edge still runs on the phase. */
static float poly_blep_since(float phase, float step, float since,
                             float carry) {
    if (!(step > FLT_EPSILON)) return 0.0f;
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    float behind = phase < since ? phase : since + carry;
    float blep = 0.0f;
    if (behind < edge)
        blep = 2.0f * (smoothstep5(0.5f * (behind / edge + 1.0f)) - 1.0f);
    if (phase > 1.0f - edge)
        blep += 2.0f * smoothstep5(0.5f * ((phase - 1.0f) / edge + 1.0f));
    return blep;
}

/* The distance behind an edge that poly_blep_since works with. */
static float behind_edge(float phase, float since, float carry) {
    return phase < since ? phase : since + carry;
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

static float pulse_sample_since(float phase, float width, float step,
                                float since, float wrap_carry,
                                float width_carry) {
    float sample = phase < width ? 1.0f : -1.0f;
    sample += poly_blep_since(phase, step, since, wrap_carry);
    sample -= poly_blep_since(wrap_phase(phase - width), step, since,
                              width_carry);
    return clamp_signal(sample, -1.25f, 1.25f);
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

/* The smoothed waveform mix at a phase, for a given triangle state, run
 * since the last sync reset and carried edges. Inactive shapes contribute an
 * exact zero, as in preview_sparse_oscillator. */
static float wave_mix_since(float phase, ma_vco_controls controls, float step,
                            float divisor, unsigned active, float since,
                            float wrap_carry, float width_carry,
                            float triangle) {
    float saw = 0.0f, pulse = 0.0f, tri = 0.0f;
    float blep = active & (WAVE_SAW | WAVE_PULSE)
               ? poly_blep_since(phase, step, since, wrap_carry) : 0.0f;
    if (active & WAVE_SAW) saw = saw_sample(phase, blep);
    if (active & WAVE_PULSE) {
        pulse = (phase < controls.pulse_width ? 1.0f : -1.0f) + blep;
        pulse -= poly_blep_since(wrap_phase(phase - controls.pulse_width),
                                 step, since, width_carry);
        pulse = clamp_signal(pulse, -1.25f, 1.25f);
    }
    if (active & WAVE_TRIANGLE) tri = clamp_signal(triangle, -1.0f, 1.0f);
    float sample = ma_fmadd(controls.triangle_level, tri,
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
    float triangle = clamp_signal(oscillator->triangle, -1.0f, 1.0f);
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
        triangle = clamp_signal(oscillator->triangle, -1.0f, 1.0f);
    /* Only preview work is optional. render_oscillator still advances its
     * triangle integrator even when the triangle contribution is zero. */
    float sample = ma_fmadd(controls.triangle_level, triangle,
                            ma_fmadd(controls.pulse_level, pulse, controls.saw_level * saw));
    if (controls.sine_level > 0.0f)
        sample = ma_fmadd(controls.sine_level, mamut_sine(phase), sample);
    return sample / divisor;
}

/* `square` is the smoothed 50% pulse at the current phase; the triangle
 * integrates it at the edge-compensated rate. */
static inline float integrate_oscillator(ma_oscillator *oscillator,
                                         float preview, float square,
                                         float triangle_step,
                                         float sync_correction) {
    float sample = preview + oscillator->sync_residual + sync_correction;
    oscillator->sync_residual = 0.0f;
    oscillator->triangle = clamp_signal(
        ma_fmadd(square, triangle_step, oscillator->triangle),
        -1.25f, 1.25f);
    return clamp_signal(sample, -1.25f, 1.25f);
}

static float render_oscillator(ma_oscillator *oscillator, float preview,
                               float step, float triangle_step,
                               float sync_correction) {
    float square = pulse_sample(phase_turns(oscillator->phase_q48), 0.5f, step);
    return integrate_oscillator(oscillator, preview, square, triangle_step,
                                sync_correction);
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
    /* Under cross-mod the triangle keeps the gain of the unmodulated step. */
    float vco1_hz = base_hz;
#if !defined(MA_SOURCE_EVIDENCE)
    vco1_hz *= source.envelope_ratio;
#endif
    uint64_t vco1_step_q48 = phase_step_q48(phase_step(vco1_hz, source.rate_hz));
    float vco1_step = phase_turns(vco1_step_q48);
    source.vco1_seed_step = vco1_step;
    source.vco1_triangle_gain = triangle_gain_for(vco1_step);
    if (controls->crossmod_amount == 0.0f) {
        source.vco1_hz = vco1_hz;
        source.vco1_step_q48 = vco1_step_q48;
        source.vco1_step = vco1_step;
        source.vco1_triangle_step = triangle_step_for(vco1_step,
                                                      source.vco1_triangle_gain);
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
    source.vco2_triangle_gain = triangle_gain_for(source.vco2_step);
    source.vco2_triangle_step = triangle_step_for(source.vco2_step,
                                              source.vco2_triangle_gain);
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

void ma_voice_seed_triangles(ma_synth *s, const ma_source_frame *source) {
    ma_oscillator *oscillator[2] = { &s->oscillator1, &s->oscillator2 };
    const float step[2] = { source->vco1_seed_step, source->vco2_step };
    const float gain[2] = { source->vco1_triangle_gain,
                            source->vco2_triangle_gain };
    for (int k = 0; k < 2; k++) {
        if (oscillator[k]->triangle_initialized) continue;
        oscillator[k]->triangle = triangle_seed(
            phase_turns(oscillator[k]->phase_q48), step[k], gain[k]);
        oscillator[k]->triangle_initialized = true;
    }
}

/* The three sync paths below stay out of line. They run for two edge zones
 * after a reset and at the reset itself, and inlined they slow the ordinary
 * substep. */

/* VCO2's preview and its 50% pulse while a sync reset is recent: its edges
 * run on their own distances (ma-constants.md 3.2). */
typedef struct {
    float preview;
    float square;
} held_vco;

[[gnu::noinline, gnu::cold]]
static held_vco held_preview(const ma_oscillator *oscillator,
                             const ma_source_frame *source) {
    const float *carry = oscillator->sync_carry;      /* wrap, width, half */
    float since = phase_turns(oscillator->sync_run_q48 - 1);
    float phase = phase_turns(oscillator->phase_q48);
    float step = source->vco2_step;
    return (held_vco){
        .preview = wave_mix_since(phase, source->vco2, step,
                                  source->vco2_divisor, source->vco2_active,
                                  since, carry[0], carry[1],
                                  oscillator->triangle),
        .square = pulse_sample_since(phase, 0.5f, step, since,
                                     carry[0], carry[2]),
    };
}

/* render_oscillator for a held VCO2, whose 50% pulse held_preview made. */
[[gnu::noinline, gnu::cold]]
static float render_held(ma_oscillator *oscillator,
                         const ma_source_frame *source, held_vco held,
                         float sync_correction) {
    return integrate_oscillator(oscillator, held.preview, held.square,
                                source->vco2_triangle_step, sync_correction);
}

/* The step VCO2's output takes at a sync reset. Edges crossed before the
 * event keep their distances across the reset, and a reset from just before
 * the wrap to just after it takes that wrap. The step then runs from the
 * smoothed waveform at the event to the one at the reset phase, where the
 * phase has not run yet. `square` is the held 50% pulse; with no reset
 * recent it is the ordinary one. Leaves the carried distances in the
 * oscillator. */
[[gnu::noinline, gnu::cold]]
static float sync_jump(ma_oscillator *oscillator, const ma_source_frame *source,
                       uint64_t event_advance, uint64_t event_phase_q48,
                       uint64_t reset_phase_q48, float event_fraction,
                       float square) {
    float *carry = oscillator->sync_carry;
    uint64_t run = oscillator->sync_run_q48;
    float step = source->vco2_step;
    if (!run)
        square = pulse_sample(phase_turns(oscillator->phase_q48), 0.5f, step);
    float event_phase = phase_turns(event_phase_q48);
    float reset_phase = phase_turns(reset_phase_q48);
    float event_since = run ? phase_turns(run - 1 + event_advance) : 1.0f;
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    float behind[3] = {
        behind_edge(event_phase, event_since, carry[0]),
        behind_edge(wrap_phase(event_phase - source->vco2.pulse_width),
                    event_since, carry[1]),
        behind_edge(wrap_phase(event_phase - 0.5f), event_since, carry[2]),
    };
    if (event_phase > 1.0f - edge && reset_phase < edge)
        behind[0] = reset_phase;
    float event_triangle = clamp_signal(
        ma_fmadd(square, source->vco2_triangle_step * event_fraction,
                 oscillator->triangle),
        -1.25f, 1.25f);
    float jump = wave_mix_since(reset_phase, source->vco2, step,
                                source->vco2_divisor, source->vco2_active,
                                0.0f, behind[0], behind[1],
                                triangle_seed(reset_phase, step,
                                              source->vco2_triangle_gain))
               - wave_mix_since(event_phase, source->vco2, step,
                                source->vco2_divisor, source->vco2_active,
                                event_since, carry[0], carry[1],
                                event_triangle);
    for (int k = 0; k < 3; k++) carry[k] = behind[k];
    return jump;
}

/* Moves VCO2 to its reset phase and through the rest of the substep. The
 * reset needs tracking only if it left an edge's smoothing running or landed
 * inside a zone that follows an edge; otherwise the phase alone is right. */
[[gnu::noinline, gnu::cold]]
static void sync_restart(ma_oscillator *oscillator,
                         const ma_source_frame *source,
                         uint64_t reset_phase_q48, float event_fraction,
                         bool event) {
    float *carry = oscillator->sync_carry;
    if (!event)
        for (int k = 0; k < 3; k++) carry[k] = 1.0f;
    float step = source->vco2_step;
    float remaining = 1.0f - event_fraction;
    uint64_t remaining_advance = (uint64_t)((float)source->vco2_step_q48
                                            * remaining);
    oscillator->phase_q48 = (reset_phase_q48 + remaining_advance)
                          & PHASE_MASK_Q48;
    float reset_phase = phase_turns(reset_phase_q48);
    oscillator->triangle = clamp_signal(
        ma_fmadd(pulse_sample_since(reset_phase, 0.5f, step, 0.0f,
                                    carry[0], carry[2]),
                 source->vco2_triangle_step * remaining,
                 triangle_seed(reset_phase, step, source->vco2_triangle_gain)),
        -1.25f, 1.25f);
    oscillator->triangle_initialized = true;
    float edge = 8.0f * step;
    if (edge > 0.49f) edge = 0.49f;
    bool track = carry[0] < edge || carry[1] < edge || carry[2] < edge
              || reset_phase < edge
              || wrap_phase(reset_phase - source->vco2.pulse_width) < edge
              || wrap_phase(reset_phase - 0.5f) < edge;
    oscillator->sync_run_q48 = track ? 1 + remaining_advance : 0;
}

/* One substep. `held` is whether VCO2 was sync-reset within its last edge
 * zone; it is a constant in each of the two instances below, so the ordinary
 * one carries none of the held code. */
[[gnu::always_inline]]
static inline float source_substep(ma_synth *s,
                                   const ma_render_controls *controls,
                                   const ma_source_frame *source,
                                   float base_hz, float noise,
                                   float guard1, float guard2, bool is_held) {
    ma_vco_controls vco1_controls = source->vco1;
    ma_vco_controls vco2_controls = source->vco2;
    float vco2_step = source->vco2_step;
    uint64_t vco2_step_q48 = source->vco2_step_q48;
    held_vco held = { 0 };
    float vco2_preview;
    if (is_held) {
        held = held_preview(&s->oscillator2, source);
        vco2_preview = held.preview;
    } else {
        vco2_preview = source->vco2_active == WAVE_ALL
            ? preview_oscillator(&s->oscillator2, vco2_controls, vco2_step,
                                 source->vco2_divisor)
            : preview_sparse_oscillator(&s->oscillator2, vco2_controls,
                                         vco2_step, source->vco2_divisor,
                                         source->vco2_active);
    }
    float vco1_hz = source->vco1_hz;
    float vco1_step = source->vco1_step;
    uint64_t vco1_step_q48 = source->vco1_step_q48;
    float vco1_triangle_step = source->vco1_triangle_step;
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
        vco1_triangle_step = triangle_step_for(vco1_step,
                                               source->vco1_triangle_gain);
    }

    float guard_hz = 0.45f * s->sample_rate_hz;
    ma_voice_set_guard_target(&s->oscillator1, vco1_hz < guard_hz ? 1.0f : 0.0f,
                              s->sample_rate_hz);
    bool master_wrap = phase_will_wrap(&s->oscillator1, vco1_step_q48);
    bool reset = master_wrap && source->sync > 0.0f;
    bool event = reset && vco1_step > FLT_EPSILON;
    float sync_now = 0.0f;
    float sync_next = 0.0f;
    float event_fraction = 1.0f;
    uint64_t sync_reset_phase_q48 = 0;
    if (event) {
        uint64_t until_wrap = PHASE_ONE_Q48 - s->oscillator1.phase_q48;
        event_fraction = (float)until_wrap / (float)vco1_step_q48;
        event_fraction = clamp_signal(event_fraction, 0.0f, 1.0f);
        uint64_t event_advance = (uint64_t)((float)vco2_step_q48
                                            * event_fraction);
        uint64_t sync_event_phase_q48 =
            (s->oscillator2.phase_q48 + event_advance) & PHASE_MASK_Q48;
        sync_reset_phase_q48 =
            (uint64_t)((float)sync_event_phase_q48 * (1.0f - source->sync))
            & PHASE_MASK_Q48;
        float jump = sync_jump(&s->oscillator2, source, event_advance,
                               sync_event_phase_q48, sync_reset_phase_q48,
                               event_fraction, held.square);
        float before = 0.5f * (1.0f - event_fraction);
        float after = 1.0f - 0.5f * event_fraction;
        sync_now = jump * smoothstep5(before);
        sync_next = jump * (smoothstep5(after) - 1.0f);
    }

    float vco1_preview = source->vco1_active == WAVE_ALL
        ? preview_oscillator(&s->oscillator1, vco1_controls, vco1_step,
                             source->vco1_divisor)
        : preview_sparse_oscillator(&s->oscillator1, vco1_controls,
                                     vco1_step, source->vco1_divisor,
                                     source->vco1_active);
    float vco1 = render_oscillator(&s->oscillator1, vco1_preview, vco1_step,
                                   vco1_triangle_step, 0.0f);
    /* Cross-mod already read VCO2 at this phase, before sync correction. */
    float vco2 = is_held
        ? render_held(&s->oscillator2, source, held, sync_now)
        : render_oscillator(&s->oscillator2, vco2_preview, vco2_step,
                            source->vco2_triangle_step, sync_now);
    s->oscillator2.sync_residual += sync_next;
    advance_phase(&s->oscillator1, vco1_step_q48);
    if (reset) {
        sync_restart(&s->oscillator2, source, sync_reset_phase_q48,
                     event_fraction, event);
    } else {
        advance_phase(&s->oscillator2, vco2_step_q48);
        if (is_held) {
            uint64_t run = s->oscillator2.sync_run_q48 + vco2_step_q48;
            s->oscillator2.sync_run_q48 = run > sync_run_limit(vco2_step_q48)
                                        ? 0 : run;
        }
    }

    return (guard1 * vco1 + guard2 * controls->vco2_level * vco2
            + controls->noise_level * noise) / source->mixer_divisor;
}

[[gnu::noinline, gnu::cold]]
static float held_substep(ma_synth *s, const ma_render_controls *controls,
                          const ma_source_frame *source, float base_hz,
                          float noise, float guard1, float guard2) {
    return source_substep(s, controls, source, base_hz, noise, guard1, guard2,
                          true);
}

float ma_voice_render_source_substep(ma_synth *s,
                                     const ma_render_controls *controls,
                                     const ma_source_frame *source,
                                     float base_hz, float noise,
                                     float guard1, float guard2) {
    if (s->oscillator2.sync_run_q48)
        return held_substep(s, controls, source, base_hz, noise, guard1,
                            guard2);
    return source_substep(s, controls, source, base_hz, noise, guard1, guard2,
                          false);
}

void ma_voice_push_oversample_history(float history[31], uint8_t *position,
                                      float sample) {
    history[*position] = sample;
    (*position)++;
    if (*position == 31) *position = 0;
}

/* Tap t reads history[position - 1 - t] (mod 31). Each nonzero pair is
 * added first, h[t] (x[t] + x[30 - t]) is fused into the sum, and the centre
 * tap comes last. */
float ma_voice_decimate_oversample(const float history[31], uint8_t position) {
    float output = 0.0f;
    for (int k = 0; k < OSC_HALFBAND_31_PAIRS; k++) {
        int tap = OSC_HALFBAND_31_PAIR[k];
        int near = position - 1 - tap, far = position - 31 + tap;
        if (near < 0) near += 31;
        if (far < 0) far += 31;
        output = ma_fmadd(OSC_HALFBAND_31[tap], history[near] + history[far], output);
    }
    int centre = position - 1 - OSC_HALFBAND_31_CENTRE;
    if (centre < 0) centre += 31;
    return ma_fmadd(OSC_HALFBAND_31[OSC_HALFBAND_31_CENTRE], history[centre], output);
}
