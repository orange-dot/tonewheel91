/* Mamut Analog voice controls: patch, identity and control targets, note
 * events and the ma_synth_set_* API. Cold code; the per-sample control
 * smoothing stays with the tick in ma_voice.c. */
#include "ma_voice_impl.h"

static uint32_t glide_frames(const ma_synth *s) {
    float frames = s->glide.seconds * s->sample_rate_hz + 0.5f;
    if (frames < 1.0f) return 1;
    return (uint32_t)frames;
}

static void glide_to_note(ma_synth *s, uint8_t note) {
    float target = note;
    if (!s->glide.initialized || !s->glide.enabled
        || s->glide.seconds == 0.0f) {
        s->glide.current_note = target;
        s->glide.target_note = target;
        s->glide.step = 0.0f;
        s->glide.remaining = 0;
    } else {
        s->glide.target_note = target;
        s->glide.remaining = glide_frames(s);
        s->glide.step = (target - s->glide.current_note)
                      / (float)s->glide.remaining;
    }
    s->glide.initialized = true;
}

static ma_vco_smoothers initialized_vco_smoothers(ma_vco_controls value) {
    return (ma_vco_smoothers){
        .saw_level = initialized_smoother(value.saw_level),
        .pulse_level = initialized_smoother(value.pulse_level),
        .triangle_level = initialized_smoother(value.triangle_level),
        .sine_level = initialized_smoother(value.sine_level),
        .pulse_width = initialized_smoother(value.pulse_width),
    };
}

static void set_vco_targets(ma_vco_smoothers *smoothers,
                            ma_vco_controls value, float sample_rate_hz) {
    set_smoother_target(&smoothers->saw_level, value.saw_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->pulse_level, value.pulse_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->triangle_level, value.triangle_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->sine_level, value.sine_level,
                        sample_rate_hz);
    set_smoother_target(&smoothers->pulse_width, value.pulse_width,
                        sample_rate_hz);
}

static uint32_t mozaik_phason_q32(float phason) {
    phason = finite_or(phason, 0.0f);
    if (phason < -2147483520.0f || phason > 2147483520.0f) phason = 0.0f;
    phason -= (float)(int32_t)phason;
    if (phason < 0.0f) phason += 1.0f;
    return (uint32_t)(uint64_t)(phason * Q32_ONE_F);
}

static float late_macro(float value, float threshold) {
    if (value <= threshold) return 0.0f;
    return clamp_signal((value - threshold) / (1.0f - threshold),
                        0.0f, 1.0f);
}

static ma_identity resolve_identity(float gravitacija, float bloom,
                                    float heat, float ruin, float swarm) {
    float late_gravitacija = late_macro(gravitacija, 0.68f);
    float extreme_swarm = late_macro(swarm, 0.82f);
    ma_identity identity = {
        .gravitacija = gravitacija,
        .bloom = bloom,
        .heat = heat,
        .ruin = ruin,
        .swarm = swarm,
    };
    identity.horizont_open = clamp_signal(
        0.70f * bloom + 0.25f * (1.0f - gravitacija)
        - 0.10f * ruin - 0.05f * heat, 0.0f, 1.0f);
    identity.horizont_air = clamp_signal(
        0.78f * bloom + 0.08f * (1.0f - heat)
        - 0.18f * late_gravitacija, 0.0f, 1.0f);
    identity.horizont_span = clamp_signal(
        0.58f * bloom + 0.32f * swarm - 0.16f * gravitacija,
        0.0f, 1.0f);
    identity.pec_mass = clamp_signal(
        0.60f * heat + 0.25f * gravitacija + 0.05f * swarm
        - 0.05f * bloom, 0.0f, 1.0f);
    identity.pec_heat = clamp_signal(
        0.80f * heat + 0.12f * gravitacija, 0.0f, 1.0f);
    identity.pec_pressure = clamp_signal(
        0.58f * gravitacija + 0.24f * heat + 0.08f * ruin,
        0.0f, 1.0f);
    identity.baklja_ready = clamp_signal(
        0.56f * ruin + 0.28f * gravitacija + 0.08f * extreme_swarm,
        0.0f, 1.0f);
    identity.baklja_edge = clamp_signal(
        0.68f * ruin + 0.40f * late_gravitacija, 0.0f, 1.0f);
    identity.baklja_sync_bias = clamp_signal(
        0.72f * ruin + 0.06f * heat, 0.0f, 1.0f);
    identity.grav_pull = gravitacija;
    identity.mass = clamp_signal(
        0.70f * identity.pec_mass + 0.20f * identity.pec_heat
        + 0.10f * identity.grav_pull, 0.0f, 1.0f);
    identity.strain = clamp_signal(
        0.40f * identity.baklja_edge + 0.38f * identity.pec_pressure
        + 0.22f * identity.grav_pull, 0.0f, 1.0f);
    identity.headroom = clamp_signal(
        0.78f + 0.18f * identity.horizont_air + 0.08f * bloom
        - 0.46f * identity.grav_pull - 0.14f * heat
        - 0.08f * identity.baklja_ready, 0.0f, 1.0f);
    identity.body_focus = clamp_signal(
        0.50f * identity.pec_mass + 0.28f * identity.grav_pull
        - 0.18f * bloom, 0.0f, 1.0f);
    identity.rupture_threshold = clamp_signal(
        0.78f - 0.22f * identity.baklja_ready
        - 0.16f * identity.grav_pull, 0.0f, 1.0f);
    identity.rupture_response = clamp_signal(
        0.55f * identity.baklja_edge + 0.25f * identity.strain
        + 0.20f * ruin, 0.0f, 1.0f);
    identity.spatial_dispersion = clamp_signal(
        0.55f * identity.horizont_span + 0.33f * swarm
        - 0.10f * identity.grav_pull, 0.0f, 1.0f);
    return identity;
}

static void resolve_effective_identity(ma_synth *s) {
    float gravitacija = clamp_signal(
        s->macro[MA_MACRO_GRAVITACIJA] + 0.45f * s->channel_pressure,
        0.0f, 1.0f);
    float bloom = clamp_signal(
        s->macro[MA_MACRO_BLOOM] + 0.35f * s->mod_wheel,
        0.0f, 1.0f);
    float heat = s->macro[MA_MACRO_HEAT];
    float ruin = clamp_signal(
        s->macro[MA_MACRO_RUIN] + 0.35f * s->channel_pressure,
        0.0f, 1.0f);
    float swarm = clamp_signal(
        s->macro[MA_MACRO_SWARM] + 0.50f * s->mod_wheel,
        0.0f, 1.0f);
    s->identity = resolve_identity(gravitacija, bloom, heat, ruin, swarm);
}

static ma_render_controls target_render_controls(const ma_synth *s) {
    ma_identity const *identity = &s->identity;
    ma_identity const *zero = &s->identity_zero;
    float cutoff_ratio = 1.0f
                       + (0.62f * identity->bloom
                          + 0.18f * (identity->horizont_air
                                     - zero->horizont_air)
                          - 0.40f * identity->gravitacija) / 0.9144f;
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    float resonance_delta = 0.14f * identity->baklja_edge
                          + 0.06f * identity->ruin
                          - 0.04f * identity->mass;
    float filter_drive_delta = 0.18f * identity->pec_heat
                             + 0.10f * identity->strain;
    float filter_env_delta = 0.12f * (identity->horizont_open
                                     - zero->horizont_open);
    float keytrack_ratio = 1.0f - 0.12f * identity->mass / 0.92f;
    float sync_delta = 0.30f * identity->baklja_sync_bias;
    float crossmod_delta = 0.24f * identity->baklja_ready;
    float width_delta = 0.18f * identity->horizont_span
                      - 0.20f * identity->body_focus;
    float crossfeed_delta = 0.28f * identity->body_focus
                          + 0.10f * identity->grav_pull
                          - 0.10f * identity->spatial_dispersion;
    float body_drive_delta = 0.25f * identity->body_focus;
    float body_load_ratio = 1.0f + 0.14f * identity->mass
                          + 0.08f * identity->strain
                          + 0.25f * (zero->headroom - identity->headroom);
    return (ma_render_controls){
        .vco1 = s->vco1,
        .vco2 = s->vco2,
        .vco2_level = s->vco2_level,
        .vco2_fine_cents = s->vco2_fine_cents,
        .sync_amount = clamp_signal(s->sync_amount + sync_delta,
                                    0.0f, 1.0f),
        .sync_softness = s->sync_softness,
        .crossmod_amount = clamp_signal(s->crossmod_amount + crossmod_delta,
                                        0.0f, 1.0f),
        .noise_level = s->noise_level,
        .raster_mix = s->raster_mix,
        .raster_position = s->raster_position,
        .raster_warp = s->raster_warp,
        .bcs_amount = s->bcs_amount,
        .bcs_regime = s->bcs_regime,
        .mozaik_mix = clamp_signal(s->mozaik_mix
                                   + 0.10f * identity->bloom, 0.0f, 1.0f),
        .mozaik_slope_q32 = ma_voice_mozaik_slope_q32(s->mozaik_slope),
        .mozaik_contrast = 1.0f + 1.2f * clamp_signal(
            s->mozaik_contrast_control + 0.20f * identity->heat,
            0.0f, 1.0f),
        .mozaik_drift = clamp_signal(s->mozaik_drift
                                     + 0.35f * identity->swarm,
                                     0.0f, 1.0f),
        .mixer_pressure = s->mixer_pressure,
        .filter_cutoff_hz = clamp_signal(s->filter_cutoff_hz * cutoff_ratio,
                                         20.0f, cutoff_max),
        .filter_resonance = clamp_signal(s->filter_resonance
                                         + resonance_delta, 0.0f, 1.0f),
        .filter_drive = clamp_signal(s->filter_drive + filter_drive_delta,
                                     0.0f, 1.0f),
        .filter_env_amount = clamp_signal(s->filter_env_amount
                                          + filter_env_delta, 0.0f, 1.0f),
        .filter_keytrack = clamp_signal(s->filter_keytrack * keytrack_ratio,
                                        0.0f, 1.0f),
        .pitch_bend_semitones = s->pitch_bend_semitones,
        .poly_pressure = s->poly_pressure,
        .lfo_depth = s->lfo.depth,
        .lfo_rate_hz = s->lfo.rate_hz,
        .body_drive = clamp_signal(s->body_drive + body_drive_delta,
                                   0.0f, 1.0f),
        .body_load_ratio = clamp_signal(body_load_ratio, 0.75f, 1.50f),
        .width = clamp_signal(s->width + width_delta, 0.0f, 1.0f),
        .crossfeed = clamp_signal(s->crossfeed + crossfeed_delta,
                                  0.0f, 1.0f),
    };
}

static void initialize_control_smoothers(ma_synth *s) {
    ma_render_controls target = target_render_controls(s);
    s->smoothers = (ma_control_smoothers){
        .vco1 = initialized_vco_smoothers(target.vco1),
        .vco2 = initialized_vco_smoothers(target.vco2),
        .vco2_level = initialized_smoother(target.vco2_level),
        .vco2_fine_cents = initialized_smoother(target.vco2_fine_cents),
        .sync_amount = initialized_smoother(target.sync_amount),
        .sync_softness = initialized_smoother(target.sync_softness),
        .crossmod_amount = initialized_smoother(target.crossmod_amount),
        .noise_level = initialized_smoother(target.noise_level),
        .raster_mix = initialized_smoother(target.raster_mix),
        .raster_position = initialized_smoother(target.raster_position),
        .raster_warp = initialized_smoother(target.raster_warp),
        .bcs_amount = initialized_smoother(target.bcs_amount),
        .bcs_regime = initialized_smoother(target.bcs_regime),
        .mozaik_mix = initialized_smoother(target.mozaik_mix),
        .mozaik_slope = initialized_smoother(s->mozaik_slope),
        .mozaik_contrast = initialized_smoother(target.mozaik_contrast),
        .mozaik_drift = initialized_smoother(target.mozaik_drift),
        .mixer_pressure = initialized_smoother(target.mixer_pressure),
        .filter_cutoff_hz = initialized_smoother(target.filter_cutoff_hz),
        .filter_resonance = initialized_smoother(target.filter_resonance),
        .filter_drive = initialized_smoother(target.filter_drive),
        .filter_env_amount = initialized_smoother(target.filter_env_amount),
        .filter_keytrack = initialized_smoother(target.filter_keytrack),
        .pitch_bend_semitones = initialized_smoother(
            target.pitch_bend_semitones),
        .poly_pressure = initialized_smoother(target.poly_pressure),
        .lfo_depth = initialized_smoother(target.lfo_depth),
        .lfo_rate_hz = initialized_smoother(target.lfo_rate_hz),
        .body_drive = initialized_smoother(target.body_drive),
        .body_load_ratio = initialized_smoother(target.body_load_ratio),
        .width = initialized_smoother(target.width),
        .crossfeed = initialized_smoother(target.crossfeed),
        .spatial_dispersion = initialized_smoother(s->identity.spatial_dispersion),
    };
}

static void update_control_targets(ma_synth *s) {
    ma_render_controls target = target_render_controls(s);
    set_vco_targets(&s->smoothers.vco1, target.vco1, s->sample_rate_hz);
    set_vco_targets(&s->smoothers.vco2, target.vco2, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.vco2_level, target.vco2_level,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.vco2_fine_cents,
                        target.vco2_fine_cents, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.sync_amount, target.sync_amount,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.sync_softness, target.sync_softness,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.crossmod_amount,
                        target.crossmod_amount, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.noise_level, target.noise_level,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.raster_mix, target.raster_mix,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.raster_position,
                        target.raster_position, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.raster_warp, target.raster_warp,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.bcs_amount, target.bcs_amount,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.bcs_regime, target.bcs_regime,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.mozaik_mix, target.mozaik_mix,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_slope, s->mozaik_slope,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_contrast,
                        target.mozaik_contrast, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.mozaik_drift, target.mozaik_drift,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.mixer_pressure,
                               target.mixer_pressure, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_cutoff_hz,
                        target.filter_cutoff_hz, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_resonance,
                        target.filter_resonance, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_drive, target.filter_drive,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_env_amount,
                        target.filter_env_amount, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.filter_keytrack,
                        target.filter_keytrack, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.pitch_bend_semitones,
                        target.pitch_bend_semitones, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.poly_pressure, target.poly_pressure,
                        s->sample_rate_hz);
    set_bypass_smoother_target(&s->smoothers.lfo_depth, target.lfo_depth,
                               s->sample_rate_hz);
    set_smoother_target(&s->smoothers.lfo_rate_hz, target.lfo_rate_hz,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.body_drive, target.body_drive,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.body_load_ratio,
                        target.body_load_ratio, s->sample_rate_hz);
    set_smoother_target(&s->smoothers.width, target.width,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.crossfeed, target.crossfeed,
                        s->sample_rate_hz);
    set_smoother_target(&s->smoothers.spatial_dispersion,
                        s->identity.spatial_dispersion, s->sample_rate_hz);
}

static uint32_t effective_phason_q32(const ma_synth *s) {
    uint32_t identity_delta = (uint32_t)(uint64_t)(
        0.15f * s->identity.ruin * Q32_ONE_F);
    return s->mozaik_phason_q32 + identity_delta;
}

static void queue_effective_phason(ma_synth *s) {
    uint32_t target = effective_phason_q32(s);
    uint32_t current_target = s->mozaik.has_pending_phason
                            ? s->mozaik.pending_phason_q32
                            : s->mozaik.applied_phason_q32;
    if (target == current_target) return;
    s->mozaik.pending_phason_q32 = target;
    s->mozaik.has_pending_phason = true;
}

static void reset_mozaik(ma_mozaik *mozaik, uint32_t phason_q32) {
    *mozaik = (ma_mozaik){
        .frac_q32 = phason_q32,
        .applied_phason_q32 = phason_q32,
        .tile_sign = 1.0f,
        .guard_gain = 1.0f,
        .guard_target = 1.0f,
    };
}

static ma_vco_controls sanitize_vco(ma_vco_controls controls,
                                    ma_vco_controls fallback) {
    return (ma_vco_controls){
        .saw_level = clamp_control(controls.saw_level, 0.0f, 1.0f,
                                   fallback.saw_level),
        .pulse_level = clamp_control(controls.pulse_level, 0.0f, 1.0f,
                                     fallback.pulse_level),
        .triangle_level = clamp_control(controls.triangle_level, 0.0f, 1.0f,
                                        fallback.triangle_level),
        .sine_level = clamp_control(controls.sine_level, 0.0f, 1.0f,
                                    fallback.sine_level),
        .pulse_width = clamp_control(controls.pulse_width, 0.05f, 0.95f,
                                     fallback.pulse_width),
    };
}

static ma_adsr sanitize_adsr(ma_adsr value, ma_adsr fallback) {
    return (ma_adsr){
        .attack_ms = clamp_control(value.attack_ms, 1.0f, 20000.0f,
                                   fallback.attack_ms),
        .decay_ms = clamp_control(value.decay_ms, 1.0f, 20000.0f,
                                  fallback.decay_ms),
        .sustain = clamp_control(value.sustain, 0.0f, 1.0f,
                                 fallback.sustain),
        .release_ms = clamp_control(value.release_ms, 1.0f, 20000.0f,
                                    fallback.release_ms),
    };
}

static ma_patch sanitize_patch(const ma_patch *value, float sample_rate_hz) {
    const ma_patch *source = value ? value : &ma_patch_tepih;
    float cutoff_max = 0.42f * sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    ma_patch result = {
        .vco1 = sanitize_vco(source->vco1, ma_patch_tepih.vco1),
        .vco2 = sanitize_vco(source->vco2, ma_patch_tepih.vco2),
        .vco2_level = clamp_control(source->vco2_level, 0.0f, 1.0f,
                                    ma_patch_tepih.vco2_level),
        .vco2_interval = source->vco2_interval < -24 ? -24
                       : source->vco2_interval > 24 ? 24
                       : source->vco2_interval,
        .vco2_fine_cents = clamp_control(
            source->vco2_fine_cents, -50.0f, 50.0f,
            ma_patch_tepih.vco2_fine_cents),
        .sync_amount = clamp_control(source->sync_amount, 0.0f, 1.0f, 0.0f),
        .sync_softness = clamp_control(source->sync_softness,
                                       0.0f, 1.0f, 0.0f),
        .crossmod_amount = clamp_control(source->crossmod_amount,
                                         0.0f, 1.0f, 0.0f),
        .noise_level = clamp_control(source->noise_level, 0.0f, 1.0f, 0.02f),
        .raster_mix = clamp_control(source->raster_mix, 0.0f, 1.0f, 0.0f),
        .raster_position = clamp_control(source->raster_position,
                                         0.0f, 1.0f, 0.0f),
        .raster_warp = clamp_control(source->raster_warp,
                                     0.0f, 1.0f, 0.0f),
        .bcs_amount = clamp_control(source->bcs_amount, 0.0f, 1.0f, 0.0f),
        .bcs_regime = clamp_control(source->bcs_regime, 0.0f, 1.0f, 0.0f),
        .mozaik_mix = clamp_control(source->mozaik_mix, 0.0f, 1.0f, 0.20f),
        .mozaik_slope = clamp_control(source->mozaik_slope,
                                      0.0f, 1.0f, 0.5601133f),
        .mozaik_contrast = clamp_control(source->mozaik_contrast,
                                         0.0f, 1.0f, 0.5150284f),
        .mozaik_phason = finite_or(source->mozaik_phason, 0.0f),
        .mozaik_drift = clamp_control(source->mozaik_drift,
                                      0.0f, 1.0f, 0.05f),
        .mixer_pressure = clamp_control(source->mixer_pressure,
                                        0.0f, 1.0f, 0.15f),
        .filter_cutoff_hz = clamp_control(source->filter_cutoff_hz,
                                          20.0f, cutoff_max, 900.0f),
        .filter_resonance = clamp_control(source->filter_resonance,
                                           0.0f, 1.0f, 0.18f),
        .filter_drive = clamp_control(source->filter_drive,
                                      0.0f, 1.0f, 0.12f),
        .filter_env_amount = clamp_control(source->filter_env_amount,
                                            0.0f, 1.0f, 0.30f),
        .filter_keytrack = clamp_control(source->filter_keytrack,
                                          0.0f, 1.0f, 0.45f),
        .amp_adsr = sanitize_adsr(source->amp_adsr, AMP_ADSR_DEFAULT),
        .filter_adsr = sanitize_adsr(source->filter_adsr,
                                     FILTER_ADSR_DEFAULT),
        .body_drive = clamp_control(source->body_drive, 0.0f, 1.0f, 0.10f),
        .width = clamp_control(source->width, 0.0f, 1.0f, 0.70f),
        .crossfeed = clamp_control(source->crossfeed, 0.0f, 1.0f, 0.0f),
        .master_level = clamp_control(source->master_level,
                                      0.0f, 1.0f, 0.18f),
        .formant_mix = clamp_control(source->formant_mix, 0.0f, 1.0f, 0.0f),
        .formant_vowel = clamp_control(source->formant_vowel,
                                       0.0f, 1.0f, 0.25f),
        .formant_shift = clamp_control(source->formant_shift,
                                       -1.0f, 1.0f, 0.0f),
        .formant_orbita = clamp_control(source->formant_orbita,
                                        0.0f, 1.0f, 0.0f),
        .grain_mix = clamp_control(source->grain_mix, 0.0f, 1.0f, 0.0f),
    };
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        result.macro[macro] = clamp_control(source->macro[macro],
                                            0.0f, 1.0f, 0.0f);
    return result;
}

void ma_synth_init_patch(ma_synth *s, float sample_rate_hz,
                         const ma_patch *patch) {
    sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    ma_patch value = sanitize_patch(patch, sample_rate_hz);
    *s = (ma_synth){
        .sample_rate_hz = sample_rate_hz,
        .vco1 = value.vco1,
        .vco2 = value.vco2,
        .vco2_level = value.vco2_level,
        .vco2_interval = value.vco2_interval,
        .vco2_fine_cents = value.vco2_fine_cents,
        .sync_amount = value.sync_amount,
        .sync_softness = value.sync_softness,
        .crossmod_amount = value.crossmod_amount,
        .noise_level = value.noise_level,
        .raster_mix = value.raster_mix,
        .raster_position = value.raster_position,
        .raster_warp = value.raster_warp,
        .bcs_amount = value.bcs_amount,
        .bcs_regime = value.bcs_regime,
        .mozaik_mix = value.mozaik_mix,
        .mozaik_slope = value.mozaik_slope,
        .mozaik_contrast_control = value.mozaik_contrast,
        .mozaik_contrast = 1.0f + 1.2f * value.mozaik_contrast,
        .mozaik_slope_q32 = ma_voice_mozaik_slope_q32(value.mozaik_slope),
        .mozaik_phason_q32 = mozaik_phason_q32(value.mozaik_phason),
        .mozaik_drift = value.mozaik_drift,
        .mixer_pressure = value.mixer_pressure,
        .filter_cutoff_hz = value.filter_cutoff_hz,
        .filter_cutoff_effective_hz = value.filter_cutoff_hz,
        .filter_resonance = value.filter_resonance,
        .filter_drive = value.filter_drive,
        .filter_env_amount = value.filter_env_amount,
        .filter_keytrack = value.filter_keytrack,
        .amp_adsr = value.amp_adsr,
        .filter_adsr = value.filter_adsr,
        .pitch_bend_semitones = 0.0f,
        .glide = {
            .current_note = 69.0f,
            .target_note = 69.0f,
        },
        .lfo = { .rate_hz = LFO_RATE_DEFAULT_HZ },
        .channel_pressure = 0.0f,
        .poly_pressure = 0.0f,
        .mod_wheel = 0.0f,
        .body_drive = value.body_drive,
        .body_load_ratio = 1.0f,
        .width = value.width,
        .crossfeed = value.crossfeed,
        .master_level = value.master_level,
        .noise_state = NOISE_SEED,
        .oscillator1 = {
            .phase_q48 = 0,
            .triangle = -1.0f,
            .guard_gain = 1.0f,
            .guard_target = 1.0f,
            .triangle_initialized = true,
        },
        .oscillator2 = {
            .phase_q48 = 0,
            .triangle = -1.0f,
            .guard_gain = 1.0f,
            .guard_target = 1.0f,
            .triangle_initialized = true,
        },
        .bcs = {
            .hopf_re = 0.025f,
            .duffing_x = 0.001f,
        },
        .note = 69,
        .velocity = 0,
        .channel = 0,
        .note_active = false,
    };
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        s->macro[macro] = value.macro[macro];
    s->identity_zero = resolve_identity(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    resolve_effective_identity(s);
    initialize_control_smoothers(s);
    ma_glas_init(&s->glas, value.formant_mix, value.formant_vowel,
                 value.formant_shift, value.formant_orbita, value.grain_mix);
    reset_mozaik(&s->mozaik, effective_phason_q32(s));
    s->filter_g = filter_prewarp(s->filter_cutoff_hz, s->sample_rate_hz);
    tw_drive_init(&s->output.body, s->sample_rate_hz);
    s->output.dc_coefficient = tw_one_pole_coeff(
        DC_BLOCK_10_HZ / s->sample_rate_hz);
}

void ma_synth_init(ma_synth *s, float sample_rate_hz) {
    ma_synth_init_patch(s, sample_rate_hz, &ma_patch_tepih);
}

void ma_synth_apply_patch(ma_synth *s, const ma_patch *patch) {
    ma_character character = s->character;
    ma_synth_init_patch(s, s->sample_rate_hz, patch);
    s->character = character;
}

void ma_synth_note_on(ma_synth *s, uint8_t channel, uint8_t note,
                      uint8_t velocity) {
    if (channel >= 16 || note >= 128) return;
    if (velocity == 0) {
        ma_synth_note_off(s, channel, note, 0);
        return;
    }
    s->channel = channel;
    s->note = note;
    glide_to_note(s, note);
    s->velocity = velocity < 128 ? velocity : 127;
    s->note_active = true;
    s->poly_pressure = 0.0f;
    s->smoothers.poly_pressure = initialized_smoother(0.0f);
    start_envelope_stage(&s->amp_envelope, MA_ENVELOPE_ATTACK, 1.0f);
    start_envelope_stage(&s->filter_envelope, MA_ENVELOPE_ATTACK, 1.0f);
    reset_mozaik(&s->mozaik, effective_phason_q32(s));
    s->glas.retrigger = true;
}

void ma_synth_note_off(ma_synth *s, uint8_t channel, uint8_t note,
                       uint8_t release_velocity) {
    if (channel >= 16 || note >= 128) return;
    (void)release_velocity;
    s->ignored_release_velocities++;
    if (s->note_active && s->channel == channel && s->note == note) {
        s->note_active = false;
        if (s->amp_envelope.stage != MA_ENVELOPE_IDLE)
            start_envelope_stage(&s->amp_envelope, MA_ENVELOPE_RELEASE, 0.0f);
        if (s->filter_envelope.stage != MA_ENVELOPE_IDLE)
            start_envelope_stage(&s->filter_envelope,
                                 MA_ENVELOPE_RELEASE, 0.0f);
    }
}

void ma_synth_set_vco1(ma_synth *s, ma_vco_controls controls) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.70f, .pulse_level = 0.25f,
        .triangle_level = 0.15f, .sine_level = 0.20f,
        .pulse_width = 0.50f,
    };
    s->vco1 = sanitize_vco(controls, fallback);
    update_control_targets(s);
}

void ma_synth_set_vco2(ma_synth *s, ma_vco_controls controls, float level,
                       int interval, float fine_cents) {
    static constexpr ma_vco_controls fallback = {
        .saw_level = 0.35f, .pulse_level = 0.20f,
        .triangle_level = 0.55f, .sine_level = 0.0f,
        .pulse_width = 0.50f,
    };
    s->vco2 = sanitize_vco(controls, fallback);
    s->vco2_level = clamp_control(level, 0.0f, 1.0f, 0.62f);
    s->vco2_interval = interval < -24 ? -24 : interval > 24 ? 24 : interval;
    s->vco2_fine_cents = clamp_control(fine_cents, -50.0f, 50.0f, 7.0f);
    update_control_targets(s);
}

void ma_synth_set_oscillator_modulation(ma_synth *s, float sync_amount,
                                        float sync_softness,
                                        float crossmod_amount,
                                        float noise_level) {
    s->sync_amount = clamp_control(sync_amount, 0.0f, 1.0f, 0.0f);
    s->sync_softness = clamp_control(sync_softness, 0.0f, 1.0f, 0.0f);
    s->crossmod_amount = clamp_control(crossmod_amount, 0.0f, 1.0f, 0.0f);
    s->noise_level = clamp_control(noise_level, 0.0f, 1.0f, 0.02f);
    update_control_targets(s);
}

void ma_synth_set_formant(ma_synth *s, float mix, float vowel, float shift,
                          float orbita) {
    ma_glas_set_formant(&s->glas, mix, vowel, shift, orbita, s->sample_rate_hz);
}

void ma_synth_set_grain(ma_synth *s, float mix) {
    ma_glas_set_grain(&s->glas, mix, s->sample_rate_hz);
}

void ma_synth_set_raster(ma_synth *s, float mix, float position, float warp) {
    s->raster_mix = clamp_control(mix, 0.0f, 1.0f, 0.0f);
    s->raster_position = clamp_control(position, 0.0f, 1.0f, 0.0f);
    s->raster_warp = clamp_control(warp, 0.0f, 1.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_bcs(ma_synth *s, float amount, float regime) {
    s->bcs_amount = clamp_control(amount, 0.0f, 1.0f, 0.0f);
    s->bcs_regime = clamp_control(regime, 0.0f, 1.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_mozaik(ma_synth *s, float mix, float slope,
                         float contrast, float phason, float drift) {
    s->mozaik_mix = clamp_control(mix, 0.0f, 1.0f, 0.20f);
    slope = clamp_control(slope, 0.0f, 1.0f,
                          (MOZAIK_DETENT[0].sigma - 0.45f) / 0.30f);
    s->mozaik_slope = slope;
    s->mozaik_slope_q32 = ma_voice_mozaik_slope_q32(slope);
    contrast = clamp_control(contrast, 0.0f, 1.0f,
                             (MOZAIK_GOLDEN_CONTRAST - 1.0f) / 1.2f);
    s->mozaik_contrast_control = contrast;
    s->mozaik_contrast = 1.0f + 1.2f * contrast;
    s->mozaik_phason_q32 = mozaik_phason_q32(phason);
    s->mozaik_drift = clamp_control(drift, 0.0f, 1.0f, 0.05f);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_filter(ma_synth *s, float cutoff_hz, float resonance,
                         float drive, float mixer_pressure) {
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    s->filter_cutoff_hz = clamp_control(cutoff_hz, 20.0f, cutoff_max, 900.0f);
    s->filter_g = filter_prewarp(s->filter_cutoff_hz, s->sample_rate_hz);
    s->filter_cutoff_effective_hz = s->filter_cutoff_hz;
    s->filter_resonance = clamp_control(resonance, 0.0f, 1.0f, 0.18f);
    s->filter_drive = clamp_control(drive, 0.0f, 1.0f, 0.12f);
    s->mixer_pressure = clamp_control(mixer_pressure, 0.0f, 1.0f, 0.15f);
    update_control_targets(s);
}

void ma_synth_set_filter_modulation(ma_synth *s, float envelope_amount,
                                    float keytrack) {
    s->filter_env_amount = clamp_control(envelope_amount, 0.0f, 1.0f, 0.30f);
    s->filter_keytrack = clamp_control(keytrack, 0.0f, 1.0f, 0.45f);
    update_control_targets(s);
}

void ma_synth_set_amp_adsr(ma_synth *s, ma_adsr adsr) {
    s->amp_adsr = sanitize_adsr(adsr, AMP_ADSR_DEFAULT);
}

void ma_synth_set_filter_adsr(ma_synth *s, ma_adsr adsr) {
    s->filter_adsr = sanitize_adsr(adsr, FILTER_ADSR_DEFAULT);
}

void ma_synth_set_glide(ma_synth *s, bool enabled, float seconds) {
    s->glide.enabled = enabled;
    s->glide.seconds = clamp_control(seconds, 0.0f, 10.0f, 0.0f);
    if (!enabled || s->glide.seconds == 0.0f) {
        s->glide.current_note = s->glide.target_note;
        s->glide.step = 0.0f;
        s->glide.remaining = 0;
    } else if (s->glide.remaining) {
        s->glide.remaining = glide_frames(s);
        s->glide.step = (s->glide.target_note - s->glide.current_note)
                      / (float)s->glide.remaining;
    }
}

void ma_synth_set_lfo(ma_synth *s, float depth, float rate_hz) {
    s->lfo.depth = clamp_control(depth, 0.0f, 1.0f, 0.0f);
    s->lfo.rate_hz = clamp_control(rate_hz, 0.03f, 20.0f,
                                   LFO_RATE_DEFAULT_HZ);
    update_control_targets(s);
}

void ma_synth_set_macro(ma_synth *s, ma_macro_id macro, float value) {
    if ((unsigned)macro >= MA_MACRO_COUNT) return;
    s->macro[macro] = clamp_control(value, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_output(ma_synth *s, float body_drive, float width,
                         float crossfeed, float master_level) {
    s->body_drive = clamp_control(body_drive, 0.0f, 1.0f, 0.10f);
    s->width = clamp_control(width, 0.0f, 1.0f, 0.70f);
    s->crossfeed = clamp_control(crossfeed, 0.0f, 1.0f, 0.0f);
    s->master_level = clamp_control(master_level, 0.0f, 1.0f, 0.18f);
    update_control_targets(s);
}

void ma_synth_set_pitch_bend(ma_synth *s, float semitones) {
    s->pitch_bend_semitones = clamp_control(semitones, -2.0f, 2.0f, 0.0f);
    update_control_targets(s);
}

void ma_synth_set_channel_pressure(ma_synth *s, float pressure) {
    s->channel_pressure = clamp_control(pressure, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
    queue_effective_phason(s);
}

void ma_synth_set_poly_pressure(ma_synth *s, uint8_t channel, uint8_t note,
                                float pressure) {
    if (channel >= 16 || note >= 128 || s->channel != channel
        || s->note != note || s->amp_envelope.stage == MA_ENVELOPE_IDLE)
        return;
    s->poly_pressure = clamp_control(pressure, 0.0f, 1.0f, 0.0f);
    set_smoother_target(&s->smoothers.poly_pressure, s->poly_pressure,
                        s->sample_rate_hz);
}

void ma_synth_set_mod_wheel(ma_synth *s, float amount) {
    s->mod_wheel = clamp_control(amount, 0.0f, 1.0f, 0.0f);
    resolve_effective_identity(s);
    update_control_targets(s);
}

void ma_voice_snap_controls(ma_synth *s) {
    initialize_control_smoothers(s);
    ma_glas_snap_controls(&s->glas);
}

void ma_voice_patch_controls(ma_synth *s, const ma_patch *patch) {
    ma_patch p = sanitize_patch(patch, s->sample_rate_hz);
    ma_synth_set_vco1(s, p.vco1);
    ma_synth_set_vco2(s, p.vco2, p.vco2_level, p.vco2_interval, p.vco2_fine_cents);
    ma_synth_set_oscillator_modulation(s, p.sync_amount, p.sync_softness,
                                      p.crossmod_amount, p.noise_level);
    ma_synth_set_raster(s, p.raster_mix, p.raster_position, p.raster_warp);
    ma_synth_set_formant(s, p.formant_mix, p.formant_vowel, p.formant_shift,
                         p.formant_orbita);
    ma_synth_set_grain(s, p.grain_mix);
    ma_synth_set_bcs(s, p.bcs_amount, p.bcs_regime);
    ma_synth_set_mozaik(s, p.mozaik_mix, p.mozaik_slope, p.mozaik_contrast,
                       p.mozaik_phason, p.mozaik_drift);
    ma_synth_set_filter(s, p.filter_cutoff_hz, p.filter_resonance,
                       p.filter_drive, p.mixer_pressure);
    ma_synth_set_filter_modulation(s, p.filter_env_amount, p.filter_keytrack);
    ma_synth_set_amp_adsr(s, p.amp_adsr);
    ma_synth_set_filter_adsr(s, p.filter_adsr);
    for (unsigned i = 0; i < MA_MACRO_COUNT; i++)
        ma_synth_set_macro(s, (ma_macro_id)i, p.macro[i]);
    ma_synth_set_output(s, p.body_drive, p.width, p.crossfeed, p.master_level);
}
