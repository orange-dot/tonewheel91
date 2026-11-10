/* Mamut Analog one-voice core. MA1 begins with the pinned pitch/control
 * spine; render state lands behind this concrete public object. */
#include "ma_internal.h"
#include "ma_voice_impl.h"

static constexpr float NOTE_HZ[128] = {
    8.175799370e+00f, 8.661956787e+00f, 9.177023888e+00f, 9.722718239e+00f, 1.030086136e+01f, 1.091338253e+01f, 1.156232548e+01f, 1.224985695e+01f,
    1.297827148e+01f, 1.375000000e+01f, 1.456761742e+01f, 1.543385315e+01f, 1.635159874e+01f, 1.732391357e+01f, 1.835404778e+01f, 1.944543648e+01f,
    2.060172272e+01f, 2.182676506e+01f, 2.312465096e+01f, 2.449971390e+01f, 2.595654297e+01f, 2.750000000e+01f, 2.913523483e+01f, 3.086770630e+01f,
    3.270319748e+01f, 3.464782715e+01f, 3.670809555e+01f, 3.889087296e+01f, 4.120344543e+01f, 4.365353012e+01f, 4.624930191e+01f, 4.899942780e+01f,
    5.191308594e+01f, 5.500000000e+01f, 5.827046967e+01f, 6.173541260e+01f, 6.540639496e+01f, 6.929565430e+01f, 7.341619110e+01f, 7.778174591e+01f,
    8.240689087e+01f, 8.730706024e+01f, 9.249860382e+01f, 9.799885559e+01f, 1.038261719e+02f, 1.100000000e+02f, 1.165409393e+02f, 1.234708252e+02f,
    1.308127899e+02f, 1.385913086e+02f, 1.468323822e+02f, 1.555634918e+02f, 1.648137817e+02f, 1.746141205e+02f, 1.849972076e+02f, 1.959977112e+02f,
    2.076523438e+02f, 2.200000000e+02f, 2.330818787e+02f, 2.469416504e+02f, 2.616255798e+02f, 2.771826172e+02f, 2.936647644e+02f, 3.111269836e+02f,
    3.296275635e+02f, 3.492282410e+02f, 3.699944153e+02f, 3.919954224e+02f, 4.153046875e+02f, 4.400000000e+02f, 4.661637573e+02f, 4.938833008e+02f,
    5.232511597e+02f, 5.543652344e+02f, 5.873295288e+02f, 6.222539673e+02f, 6.592551270e+02f, 6.984564819e+02f, 7.399888306e+02f, 7.839908447e+02f,
    8.306093750e+02f, 8.800000000e+02f, 9.323275146e+02f, 9.877666016e+02f, 1.046502319e+03f, 1.108730469e+03f, 1.174659058e+03f, 1.244507935e+03f,
    1.318510254e+03f, 1.396912964e+03f, 1.479977661e+03f, 1.567981689e+03f, 1.661218750e+03f, 1.760000000e+03f, 1.864655029e+03f, 1.975533203e+03f,
    2.093004639e+03f, 2.217460938e+03f, 2.349318115e+03f, 2.489015869e+03f, 2.637020508e+03f, 2.793825928e+03f, 2.959955322e+03f, 3.135963379e+03f,
    3.322437500e+03f, 3.520000000e+03f, 3.729310059e+03f, 3.951066406e+03f, 4.186009277e+03f, 4.434921875e+03f, 4.698636230e+03f, 4.978031738e+03f,
    5.274041016e+03f, 5.587651855e+03f, 5.919910645e+03f, 6.271926758e+03f, 6.644875000e+03f, 7.040000000e+03f, 7.458620117e+03f, 7.902132812e+03f,
    8.372018555e+03f, 8.869843750e+03f, 9.397272461e+03f, 9.956063477e+03f, 1.054808203e+04f, 1.117530371e+04f, 1.183982129e+04f, 1.254385352e+04f,
};

static constexpr float VELOCITY_LEVEL[128] = {
    0.000000000e+00f, 2.285772935e-02f, 3.924971446e-02f, 5.385024473e-02f, 6.739689410e-02f, 8.021021634e-02f, 9.246791899e-02f, 1.042820513e-01f,
    1.157292873e-01f, 1.268651187e-01f, 1.377314478e-01f, 1.483608782e-01f, 1.587795168e-01f, 1.690086424e-01f, 1.790659279e-01f, 1.889662594e-01f,
    1.987223327e-01f, 2.083450854e-01f, 2.178440243e-01f, 2.272275090e-01f, 2.365029156e-01f, 2.456768006e-01f, 2.547550499e-01f, 2.637429237e-01f,
    2.726452053e-01f, 2.814662457e-01f, 2.902099490e-01f, 2.988799810e-01f, 3.074796498e-01f, 3.160119653e-01f, 3.244798183e-01f, 3.328857720e-01f,
    3.412322700e-01f, 3.495215476e-01f, 3.577557802e-01f, 3.659368753e-01f, 3.740667105e-01f, 3.821469843e-01f, 3.901793659e-01f, 3.981653750e-01f,
    4.061064422e-01f, 4.140039682e-01f, 4.218592048e-01f, 4.296734333e-01f, 4.374477565e-01f, 4.451833069e-01f, 4.528810978e-01f, 4.605422020e-01f,
    4.681675136e-01f, 4.757579267e-01f, 4.833143651e-01f, 4.908376038e-01f, 4.983284771e-01f, 5.057877302e-01f, 5.132160783e-01f, 5.206142068e-01f,
    5.279827714e-01f, 5.353224874e-01f, 5.426338911e-01f, 5.499176383e-01f, 5.571743250e-01f, 5.644043684e-01f, 5.716084242e-01f, 5.787869692e-01f,
    5.859404206e-01f, 5.930693746e-01f, 6.001742482e-01f, 6.072554588e-01f, 6.143134832e-01f, 6.213486791e-01f, 6.283615232e-01f, 6.353523135e-01f,
    6.423214674e-01f, 6.492694020e-01f, 6.561964154e-01f, 6.631028056e-01f, 6.699890494e-01f, 6.768553257e-01f, 6.837020516e-01f, 6.905294657e-01f,
    6.973379254e-01f, 7.041276693e-01f, 7.108989954e-01f, 7.176522017e-01f, 7.243874669e-01f, 7.311051488e-01f, 7.378054857e-01f, 7.444887161e-01f,
    7.511550188e-01f, 7.578046918e-01f, 7.644379735e-01f, 7.710550427e-01f, 7.776561379e-01f, 7.842414379e-01f, 7.908112407e-01f, 7.973656058e-01f,
    8.039048910e-01f, 8.104291558e-01f, 8.169386387e-01f, 8.234335184e-01f, 8.299140334e-01f, 8.363802433e-01f, 8.428324461e-01f, 8.492707014e-01f,
    8.556951880e-01f, 8.621061444e-01f, 8.685036898e-01f, 8.748879433e-01f, 8.812591434e-01f, 8.876172900e-01f, 8.939626813e-01f, 9.002953768e-01f,
    9.066155553e-01f, 9.129232764e-01f, 9.192187786e-01f, 9.255021214e-01f, 9.317734241e-01f, 9.380329251e-01f, 9.442805648e-01f, 9.505166411e-01f,
    9.567412138e-01f, 9.629543424e-01f, 9.691561460e-01f, 9.753468633e-01f, 9.815264344e-01f, 9.876950979e-01f, 9.938529134e-01f, 1.000000000e+00f,
};

static constexpr float VELOCITY_FILTER[128] = {
    0.000000000e+00f, 5.344314035e-03f, 1.129807346e-02f, 1.750583947e-02f, 2.388453484e-02f, 3.039342165e-02f, 3.700797632e-02f, 4.371171817e-02f,
    5.049276724e-02f, 5.734213814e-02f, 6.425278634e-02f, 7.121903449e-02f, 7.823619992e-02f, 8.530034870e-02f, 9.240814298e-02f, 9.955671430e-02f,
    1.067435294e-01f, 1.139663979e-01f, 1.212233528e-01f, 1.285126507e-01f, 1.358327121e-01f, 1.431821287e-01f, 1.505596042e-01f, 1.579639763e-01f,
    1.653941423e-01f, 1.728491336e-01f, 1.803280115e-01f, 1.878299564e-01f, 1.953541487e-01f, 2.028998882e-01f, 2.104664743e-01f, 2.180532664e-01f,
    2.256596684e-01f, 2.332851142e-01f, 2.409290671e-01f, 2.485910356e-01f, 2.562705278e-01f, 2.639671266e-01f, 2.716803849e-01f, 2.794098854e-01f,
    2.871552706e-01f, 2.949161530e-01f, 3.026921749e-01f, 3.104830682e-01f, 3.182884455e-01f, 3.261080384e-01f, 3.339415491e-01f, 3.417886794e-01f,
    3.496491909e-01f, 3.575228155e-01f, 3.654092848e-01f, 3.733084202e-01f, 3.812199235e-01f, 3.891436160e-01f, 3.970792890e-01f, 4.050267339e-01f,
    4.129857421e-01f, 4.209561050e-01f, 4.289377034e-01f, 4.369302988e-01f, 4.449337423e-01f, 4.529478550e-01f, 4.609724879e-01f, 4.690074921e-01f,
    4.770526886e-01f, 4.851079583e-01f, 4.931731522e-01f, 5.012481213e-01f, 5.093327761e-01f, 5.174269080e-01f, 5.255303979e-01f, 5.336432457e-01f,
    5.417651534e-01f, 5.498961210e-01f, 5.580360293e-01f, 5.661846995e-01f, 5.743421316e-01f, 5.825080872e-01f, 5.906825662e-01f, 5.988654494e-01f,
    6.070565581e-01f, 6.152558923e-01f, 6.234633923e-01f, 6.316788197e-01f, 6.399022341e-01f, 6.481334567e-01f, 6.563723683e-01f, 6.646190286e-01f,
    6.728732586e-01f, 6.811349988e-01f, 6.894041300e-01f, 6.976806521e-01f, 7.059644461e-01f, 7.142554522e-01f, 7.225536108e-01f, 7.308588028e-01f,
    7.391709685e-01f, 7.474901080e-01f, 7.558161020e-01f, 7.641488910e-01f, 7.724884152e-01f, 7.808346152e-01f, 7.891874313e-01f, 7.975468040e-01f,
    8.059126735e-01f, 8.142849803e-01f, 8.226636648e-01f, 8.310486674e-01f, 8.394399285e-01f, 8.478374481e-01f, 8.562411070e-01f, 8.646509051e-01f,
    8.730667233e-01f, 8.814886212e-01f, 8.899164200e-01f, 8.983501792e-01f, 9.067897797e-01f, 9.152352214e-01f, 9.236863852e-01f, 9.321433306e-01f,
    9.406059384e-01f, 9.490742087e-01f, 9.575480819e-01f, 9.660274982e-01f, 9.745124578e-01f, 9.830029011e-01f, 9.914987683e-01f, 1.000000000e+00f,
};

static float note_position_hz(float note) {
    note = clamp_signal(note, 0.0f, 127.0f);
    uint8_t floor_note = (uint8_t)note;
    float fraction = note - floor_note;
    return NOTE_HZ[floor_note] * octave_ratio_small(fraction / 12.0f);
}

static float next_glide_note(ma_synth *s) {
    float note = s->glide.current_note;
    if (s->glide.remaining) {
        s->glide.current_note += s->glide.step;
        s->glide.remaining--;
        if (!s->glide.remaining)
            s->glide.current_note = s->glide.target_note;
    }
    return note;
}

static float next_lfo(ma_synth *s, const ma_render_controls *controls) {
    if (controls->lfo_depth == 0.0f) return 0.0f;
    float value = controls->lfo_depth * tw_sin_turns(s->lfo.phase);
    s->lfo.phase += controls->lfo_rate_hz / s->sample_rate_hz;
    if (s->lfo.phase >= 1.0f) s->lfo.phase -= 1.0f;
    return value;
}

static float next_smoother(ma_smoother *smoother) {
    if (smoother->remaining == 0) return smoother->current;
    smoother->current += smoother->step;
    smoother->remaining--;
    if (smoother->remaining == 0) smoother->current = smoother->target;
    return smoother->current;
}

static ma_vco_controls next_vco_controls(ma_vco_smoothers *smoothers) {
    return (ma_vco_controls){
        .saw_level = next_smoother(&smoothers->saw_level),
        .pulse_level = next_smoother(&smoothers->pulse_level),
        .triangle_level = next_smoother(&smoothers->triangle_level),
        .sine_level = next_smoother(&smoothers->sine_level),
        .pulse_width = next_smoother(&smoothers->pulse_width),
    };
}

static float next_character(ma_synth *s) {
    ma_character *c = &s->character;
    if (!c->assigned) return 0.0f;
    set_bypass_smoother_target(&c->smoother,
        clamp_control(c->amount, 0.0f, 1.0f, 0.0f), s->sample_rate_hz);
    c->walk_cents = clamp_signal(c->walk_cents + c->walk_step, -3.0f, 3.0f);
    if (++c->walk_phase == 32) {
        c->walk_phase = 0;
        c->walk_cents = c->walk_target;
        c->walk_target = clamp_signal(
            c->walk_target + .03f * next_noise(&c->walk_state), -3.0f, 3.0f);
        c->walk_step = (c->walk_target - c->walk_cents) * (1.0f / 32.0f);
    }
    return next_smoother(&c->smoother);
}

uint32_t ma_voice_mozaik_slope_q32(float control) {
    float sigma = 0.45f + 0.30f * control;
    float best_distance = FLT_MAX;
    uint32_t snapped = 0;
    for (size_t i = 0; i < sizeof MOZAIK_DETENT / sizeof *MOZAIK_DETENT; i++) {
        float distance = tw_fabsf(sigma - MOZAIK_DETENT[i].sigma);
        if (distance <= 0.004f && distance < best_distance) {
            best_distance = distance;
            snapped = MOZAIK_DETENT[i].q32;
        }
    }
    if (snapped) return snapped;
    uint64_t q32 = (uint64_t)(sigma * Q32_ONE_F + 0.5f);
    if (q32 < MOZAIK_SLOPE_MIN_Q32) return MOZAIK_SLOPE_MIN_Q32;
    if (q32 > MOZAIK_SLOPE_MAX_Q32) return MOZAIK_SLOPE_MAX_Q32;
    return (uint32_t)q32;
}

/* Fills in place, field by field: the fields read distinct smoothers, so
 * order does not matter (a compound literal's initializers are
 * indeterminately sequenced anyway), and no struct copy reaches memcpy. */
static void fill_render_controls(ma_synth *s, ma_render_controls *c) {
    (void)next_smoother(&s->smoothers.spatial_dispersion);
    float slope = next_smoother(&s->smoothers.mozaik_slope);
    c->vco1 = next_vco_controls(&s->smoothers.vco1);
    c->vco2 = next_vco_controls(&s->smoothers.vco2);
    c->vco2_level = next_smoother(&s->smoothers.vco2_level);
    c->vco2_fine_cents = next_smoother(&s->smoothers.vco2_fine_cents);
    c->sync_amount = next_smoother(&s->smoothers.sync_amount);
    c->sync_softness = next_smoother(&s->smoothers.sync_softness);
    c->crossmod_amount = next_smoother(&s->smoothers.crossmod_amount);
    c->noise_level = next_smoother(&s->smoothers.noise_level);
    c->raster_mix = next_smoother(&s->smoothers.raster_mix);
    c->raster_position = next_smoother(&s->smoothers.raster_position);
    c->raster_warp = next_smoother(&s->smoothers.raster_warp);
    c->bcs_amount = next_smoother(&s->smoothers.bcs_amount);
    c->bcs_regime = next_smoother(&s->smoothers.bcs_regime);
    c->mozaik_mix = next_smoother(&s->smoothers.mozaik_mix);
    c->mozaik_slope_q32 = ma_voice_mozaik_slope_q32(slope);
    c->mozaik_contrast = next_smoother(&s->smoothers.mozaik_contrast);
    c->mozaik_drift = next_smoother(&s->smoothers.mozaik_drift);
    c->mixer_pressure = next_smoother(&s->smoothers.mixer_pressure);
    c->filter_cutoff_hz = next_smoother(&s->smoothers.filter_cutoff_hz);
    c->filter_resonance = next_smoother(&s->smoothers.filter_resonance);
    c->filter_drive = next_smoother(&s->smoothers.filter_drive);
    c->filter_env_amount = next_smoother( &s->smoothers.filter_env_amount);
    c->filter_keytrack = next_smoother(&s->smoothers.filter_keytrack);
    c->pitch_bend_semitones = next_smoother( &s->smoothers.pitch_bend_semitones);
    c->poly_pressure = next_smoother(&s->smoothers.poly_pressure);
    c->lfo_depth = next_smoother(&s->smoothers.lfo_depth);
    c->lfo_rate_hz = next_smoother(&s->smoothers.lfo_rate_hz);
    c->body_drive = next_smoother(&s->smoothers.body_drive);
    c->body_load_ratio = next_smoother(&s->smoothers.body_load_ratio);
    c->width = next_smoother(&s->smoothers.width);
    c->crossfeed = next_smoother(&s->smoothers.crossfeed);
}

static ma_render_controls next_render_controls(ma_synth *s) {
    ma_render_controls c;
    fill_render_controls(s, &c);
    return c;
}

#if !defined(MA_SOURCE_EVIDENCE)
float ma_voice_filter_envelope_amount(const ma_synth *s,
                                      const ma_render_controls *controls) {
    return clamp_signal(controls->filter_env_amount
                        + 0.25f * ma_velocity_filter(s->velocity),
                        0.0f, 1.0f);
}

float ma_voice_effective_filter_cutoff(const ma_synth *s,
                                       const ma_render_controls *controls,
                                       float envelope_amount,
                                       float note_position) {
    float envelope = 1.0f + 0.78f * s->filter_envelope.level
                                  * envelope_amount;
    float keytrack = 1.0f
                   + (note_position - 60.0f) * (1.0f / 48.0f)
                   * controls->filter_keytrack * 0.42f;
    keytrack = clamp_signal(keytrack, 0.55f, 1.35f);
    float cutoff = controls->filter_cutoff_hz * envelope * keytrack;
    if (controls->poly_pressure > 0.0f)
        cutoff *= octave_ratio_small(0.25f * controls->poly_pressure);
    if (s->character.smoother.current > 0.0f)
        cutoff *= octave_ratio_small(s->character.smoother.current
                                     * s->character.cutoff_octaves);
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    return clamp_signal(cutoff, 20.0f, cutoff_max);
}
#endif

float ma_note_frequency_hz(uint8_t note) {
    return note < 128 ? NOTE_HZ[note] : 0.0f;
}

float ma_velocity_level(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_LEVEL[velocity] : 0.0f;
}

float ma_velocity_filter(uint8_t velocity) {
    return velocity < 128 ? VELOCITY_FILTER[velocity] : 0.0f;
}

float ma_safety_curve(float sample) {
    if (!(sample >= -FLT_MAX && sample <= FLT_MAX)) return 0.0f;
    float magnitude = tw_fabsf(sample);
    if (magnitude <= SAFETY_KNEE) return sample;
    float t = (magnitude - SAFETY_KNEE) / SAFETY_RANGE;
    t = clamp_signal(t, 0.0f, 1.0f);
    float limited = SAFETY_KNEE
                  + SAFETY_RANGE * (t + t * t - t * t * t);
    return sample < 0.0f ? -limited : limited;
}

#if defined(MA_SOURCE_EVIDENCE)
static ma_frame synth_tick(ma_synth *s, bool raw, float detune_cents) {
    ma_render_controls controls = next_render_controls(s);
    float character = next_character(s);
    float note_position = next_glide_note(s);
    float pitch_delta = controls.pitch_bend_semitones + next_lfo(s, &controls);
    float base_hz = note_position_hz(note_position);
    if (detune_cents != 0.0f)
        base_hz *= octave_ratio_small(detune_cents / 1200.0f);
    if (pitch_delta != 0.0f)
        base_hz *= octave_ratio_small(pitch_delta / 12.0f);
    if (character > 0.0f)
        base_hz *= octave_ratio_small(character
            * (s->character.vco1_cents + s->character.walk_cents) / 1200.0f);
    if (controls.bcs_amount > 0.0f) {
        float bcs = ma_voice_render_bcs(s, base_hz, controls.bcs_amount,
                                        controls.bcs_regime, s->ladder.y4_guess);
        float crossmod_depth = 0.10f + 0.20f * controls.bcs_regime;
        float resonance_depth = 0.08f + 0.08f * controls.bcs_regime;
        controls.crossmod_amount = clamp_signal(
            controls.crossmod_amount
                + controls.bcs_amount * crossmod_depth * bcs,
            0.0f, 1.0f);
        controls.filter_resonance = clamp_signal(
            controls.filter_resonance
                + controls.bcs_amount * resonance_depth * bcs,
            0.0f, 1.0f);
    }
#if !defined(MA_SOURCE_EVIDENCE)
    ma_adsr filter_adsr = character_adsr(s->filter_adsr,
        s->character.filter_time_bias, character);
    ma_adsr amp_adsr = character_adsr(s->amp_adsr,
        s->character.amp_time_bias, character);
    (void)render_envelope(&s->filter_envelope, filter_adsr,
                          s->sample_rate_hz);
    (void)render_envelope(&s->amp_envelope, amp_adsr,
                          s->sample_rate_hz);
    float envelope_amount = ma_voice_filter_envelope_amount(s, &controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = ma_voice_effective_filter_cutoff(
        s, &controls, envelope_amount, note_position);
    float filter_g = filter_prewarp(s->filter_cutoff_effective_hz,
                                    s->sample_rate_hz);
#else
    float filter_modulation = 0.0f;
#endif
    float guard1 = s->oscillator1.guard_gain;
    float guard2 = s->oscillator2.guard_gain;
    float noise = next_noise(&s->noise_state);
    ma_source_frame source_frame = ma_voice_prepare_source_frame(
        s, &controls, base_hz, filter_modulation);
    ma_voice_set_guard_target(&s->oscillator2,
        source_frame.vco2_hz < 0.45f * s->sample_rate_hz ? 1.0f : 0.0f,
        s->sample_rate_hz);
    ma_voice_seed_triangles(s, &source_frame);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        for (int at_4x = 0; at_4x < 2; at_4x++) {
            ma_voice_push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                ma_voice_render_source_substep(s, &controls, &source_frame,
                                               base_hz, noise, guard1, guard2));
            ma_voice_push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                ma_voice_render_source_substep(s, &controls, &source_frame,
                                               base_hz, noise, guard1, guard2));
            float sample_4x = ma_voice_decimate_oversample(
                s->oversample_history[0], s->oversample_history_pos[0]);
            ma_voice_push_oversample_history(
                s->oversample_history[1], &s->oversample_history_pos[1],
                sample_4x);
        }
        float sample_2x = ma_voice_decimate_oversample(
            s->oversample_history[1], s->oversample_history_pos[1]);
#if defined(MA_SOURCE_EVIDENCE)
        ma_voice_push_oversample_history(
            s->oversample_history[2], &s->oversample_history_pos[2],
            sample_2x);
#else
        float mozaik_gain = s->mozaik.guard_gain;
        float mozaik = render_mozaik(s, &controls, base_hz,
                                     2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
        float raster = controls.raster_mix > 0.0f
                     ? ma_voice_render_raster(s, &controls, base_hz,
                                              2.0f * s->sample_rate_hz)
                     : 0.0f;
        float source = mix_source_2x(&controls, sample_2x, guard1, guard2,
                                     mozaik_gain, mozaik, raster);
        float pressured = apply_mixer_pressure(source,
                                                controls.mixer_pressure);
        s->ladder.pressure_input = source;
        s->ladder.pressure_output = pressured;
        push_filter_history(&s->ladder,
                            render_ladder_2x(s, &controls, pressured,
                                             filter_g));
#endif
    }
    (void)ma_voice_next_guard_gain(&s->oscillator1);
    (void)ma_voice_next_guard_gain(&s->oscillator2);

#if defined(MA_SOURCE_EVIDENCE)
    float level = s->note_active ? ma_velocity_level(s->velocity) : 0.0f;
    float analog = ma_voice_decimate_oversample(
        s->oversample_history[2], s->oversample_history_pos[2]);
    float mozaik_gain = s->mozaik.guard_gain;
    float mozaik = render_mozaik(s, &controls, base_hz,
                                 s->sample_rate_hz);
    (void)next_mozaik_guard_gain(&s->mozaik);
    float raster = controls.raster_mix > 0.0f
                 ? ma_voice_render_raster(s, &controls, base_hz, s->sample_rate_hz)
                 : 0.0f;
    float output = mix_source_2x(&controls, analog, guard1, guard2,
                                 mozaik_gain, mozaik, raster) * level;
#else
    float velocity_gain = 0.25f + 0.75f * ma_velocity_level(s->velocity);
    float output = decimate_filter(&s->ladder)
                 * s->amp_envelope.level * velocity_gain;
#endif
    if (controls.poly_pressure > 0.0f)
        output *= 1.0f + 0.10f * controls.poly_pressure;
    if (character > 0.0f)
        output *= 1.0f + character * s->character.vca_bias;
#if defined(MA_SOURCE_EVIDENCE)
    (void)raw;
    return (ma_frame){ .left = output, .right = output };
#else
    if (raw) return (ma_frame){ .left = output, .right = output };
    return ma_voice_render_output(s, &controls, output);
#endif
}
#else
/* Staged voice tick (2026-11-06). Every voice performs the same float
 * operations, in the same order, as the former single-pass tick. Only the
 * interleaving of stages with disjoint state moved: all eight oscillator
 * substeps (oscillators, guards) run before the decimators, and mozaik and
 * raster (their own state) run before the 2x mix and ladder. Grouping the
 * stages this way lets several voices run each stage together. */
/* Controls, pitch, BCS, envelopes, filter prewarp, noise, the source frame,
 * and both 2x mozaik/raster steps. The ladder's previous y4 and the guard
 * gains are inputs because a lane bank may hold them. */
/* Front end, part A: controls, character, glide, LFO and pitch. BCS comes
 * next and reads only its own state, so a lane bank may run it for all
 * voices between part A and part B. */
void ma_voice_front_a(ma_synth *restrict s, float detune_cents,
                      ma_voice_front *restrict f) {
    /* Written in place (f never aliases s): a freestanding core must not
     * emit memcpy. */
    ma_render_controls *controls = &f->controls;
    fill_render_controls(s, controls);
    float character = next_character(s);
    float note_position = next_glide_note(s);
    float pitch_delta = controls->pitch_bend_semitones + next_lfo(s, controls);
    float base_hz = note_position_hz(note_position);
    if (detune_cents != 0.0f)
        base_hz *= octave_ratio_small(detune_cents / 1200.0f);
    if (pitch_delta != 0.0f)
        base_hz *= octave_ratio_small(pitch_delta / 12.0f);
    if (character > 0.0f)
        base_hz *= octave_ratio_small(character
            * (s->character.vco1_cents + s->character.walk_cents) / 1200.0f);
    f->character = character;
    f->note_position = note_position;
    f->base_hz = base_hz;
}

/* Front end, part B, shared by the scalar tick and the lanes: the BCS
 * output's effect on the controls (when BCS ran), envelopes, guards, noise
 * and both mozaik steps. What follows reads only the envelope level, the
 * controls, the guards and its own state, never the mozaik state. */
void ma_voice_front_b_shared(ma_synth *restrict s, float bcs, float guard1,
                             float guard2, ma_voice_front *restrict f) {
    ma_render_controls *controls = &f->controls;
    if (controls->bcs_amount > 0.0f) {
        float crossmod_depth = 0.10f + 0.20f * controls->bcs_regime;
        float resonance_depth = 0.08f + 0.08f * controls->bcs_regime;
        controls->crossmod_amount = clamp_signal(
            controls->crossmod_amount
                + controls->bcs_amount * crossmod_depth * bcs,
            0.0f, 1.0f);
        controls->filter_resonance = clamp_signal(
            controls->filter_resonance
                + controls->bcs_amount * resonance_depth * bcs,
            0.0f, 1.0f);
    }
    ma_adsr filter_adsr = character_adsr(s->filter_adsr,
        s->character.filter_time_bias, f->character);
    ma_adsr amp_adsr = character_adsr(s->amp_adsr,
        s->character.amp_time_bias, f->character);
    (void)render_envelope(&s->filter_envelope, filter_adsr, s->sample_rate_hz);
    (void)render_envelope(&s->amp_envelope, amp_adsr, s->sample_rate_hz);
    f->guard1 = guard1;
    f->guard2 = guard2;
    f->noise = next_noise(&s->noise_state);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        f->mozaik_gain[at_2x] = s->mozaik.guard_gain;
        f->mozaik[at_2x] = render_mozaik(s, controls, f->base_hz,
                                         2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
    }
}

/* Front end, part B, scalar: the shared part, then cutoff, prewarp, the
 * source frame and raster (the lanes run these eight wide). */
static void voice_front_b(ma_synth *restrict s, float bcs, float guard1,
                          float guard2, ma_voice_front *restrict f) {
    ma_voice_front_b_shared(s, bcs, guard1, guard2, f);
    const ma_render_controls *controls = &f->controls;
    float envelope_amount = ma_voice_filter_envelope_amount(s, controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = ma_voice_effective_filter_cutoff(
        s, controls, envelope_amount, f->note_position);
    f->filter_g = filter_prewarp(s->filter_cutoff_effective_hz,
                                 s->sample_rate_hz);
    f->source = ma_voice_prepare_source_frame_guarded(s, controls, f->base_hz,
                                                      filter_modulation,
                                                      guard1, guard2);
    for (int at_2x = 0; at_2x < 2; at_2x++)
        f->raster[at_2x] = controls->raster_mix > 0.0f
                         ? ma_voice_render_raster(s, controls, f->base_hz,
                                                  2.0f * s->sample_rate_hz)
                         : 0.0f;
}

/* The scalar front end: part A, BCS when enabled, part B. */
static void voice_front_end(ma_synth *restrict s, float detune_cents,
                            float ladder_y4, float guard1, float guard2,
                            ma_voice_front *restrict f) {
    ma_voice_front_a(s, detune_cents, f);
    float bcs = 0.0f;
    if (f->controls.bcs_amount > 0.0f)
        bcs = ma_voice_render_bcs(s, f->base_hz, f->controls.bcs_amount,
                                  f->controls.bcs_regime, ladder_y4);
    voice_front_b(s, bcs, guard1, guard2, f);
}

/* The VCO2 guard target, eight 8x source substeps, and both guard steps. */
static void voice_oscillator_block(ma_synth *s, const ma_voice_front *f,
                                   float source[8]) {
    ma_voice_set_guard_target(&s->oscillator2,
        f->source.vco2_hz < 0.45f * s->sample_rate_hz ? 1.0f : 0.0f,
        s->sample_rate_hz);
    ma_voice_seed_triangles(s, &f->source);
    for (int substep = 0; substep < 8; substep++)
        source[substep] = ma_voice_render_source_substep(s, &f->controls, &f->source,
                                                         f->base_hz, f->noise,
                                                         f->guard1, f->guard2);
    (void)ma_voice_next_guard_gain(&s->oscillator1);
    (void)ma_voice_next_guard_gain(&s->oscillator2);
}

/* 8x -> 4x -> 2x, in the former push/decimate order. */
static void voice_decimate(ma_synth *s, const float source[8],
                           float at_2x[2]) {
    int substep = 0;
    for (int step_2x = 0; step_2x < 2; step_2x++) {
        for (int step_4x = 0; step_4x < 2; step_4x++) {
            ma_voice_push_oversample_history(s->oversample_history[0],
                                             &s->oversample_history_pos[0],
                                             source[substep++]);
            ma_voice_push_oversample_history(s->oversample_history[0],
                                             &s->oversample_history_pos[0],
                                             source[substep++]);
            float sample_4x = ma_voice_decimate_oversample(
                s->oversample_history[0], s->oversample_history_pos[0]);
            ma_voice_push_oversample_history(s->oversample_history[1],
                                             &s->oversample_history_pos[1],
                                             sample_4x);
        }
        at_2x[step_2x] = ma_voice_decimate_oversample(s->oversample_history[1],
                                                      s->oversample_history_pos[1]);
    }
}

static ma_frame synth_tick(ma_synth *s, bool raw, float detune_cents) {
    ma_voice_front f;
    float source[8], at_2x[2];
    voice_front_end(s, detune_cents, s->ladder.y4_guess,
                    s->oscillator1.guard_gain, s->oscillator2.guard_gain, &f);
    voice_oscillator_block(s, &f, source);
    voice_decimate(s, source, at_2x);
    bool glas = ma_glas_active(&s->glas);
    s->glas.frame_grain_mix = glas ? ma_glas_prepare(s, f.base_hz) : 0.0f;
    float filtered = ma_voice_filter(s, &f, at_2x);
    if (glas) filtered = ma_glas_formant(s, filtered);
    float output = voice_back_end(s, &f, filtered);
    if (raw) return (ma_frame){ .left = output, .right = output };
    return ma_voice_render_output(s, &f.controls, output);
}
#endif

ma_frame ma_synth_tick(ma_synth *s) {
    return synth_tick(s, false, 0.0f);
}

float ma_voice_tick_raw(ma_synth *s) {
    return synth_tick(s, true, 0.0f).left;
}

/* Pool helpers leave the legacy synth/card execution order unchanged. */
float ma_voice_tick_detuned(ma_synth *s, float cents) {
    return synth_tick(s, true, cents).left;
}
void ma_voice_tick_controls(ma_synth *s) {
    (void)next_render_controls(s);
    ma_glas_tick_controls(&s->glas);
}
