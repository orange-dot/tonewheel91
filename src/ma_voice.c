/* Mamut Analog one-voice core. MA1 begins with the pinned pitch/control
 * spine; render state lands behind this concrete public object. */
#include "ma_internal.h"
#include "ma_raster_table.h"
#include "ma_voice_impl.h"

static float filter_prewarp(float cutoff_hz, float sample_rate_hz);

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
    float fundamental = tw_sin_turns(phase);
    float warm = tw_sat(DRIVE * fundamental) / tw_sat(DRIVE);
    float second = tw_sin_turns(wrap_phase(2.0f * phase + 0.07f));
    return PEAK_NORMALIZATION * (warm + SECOND_LEVEL * second);
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
    float sample = controls.saw_level * saw + controls.pulse_level * pulse
                 + controls.triangle_level * triangle;
    if (controls.sine_level > 0.0f)
        sample += controls.sine_level * mamut_sine(phase);
    return sample / divisor;
}

enum { WAVE_SAW = 1, WAVE_PULSE = 2, WAVE_TRIANGLE = 4,
       WAVE_ALL = WAVE_SAW | WAVE_PULSE | WAVE_TRIANGLE };

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
    float sample = controls.saw_level * saw_sample(phase, blep)
                 + controls.pulse_level * pulse_sample_with_blep(
                     phase, controls.pulse_width, step, blep)
                 + controls.triangle_level * triangle;
    if (controls.sine_level > 0.0f)
        sample += controls.sine_level * mamut_sine(phase);
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
    float sample = controls.saw_level * saw + controls.pulse_level * pulse
                 + controls.triangle_level * triangle;
    if (controls.sine_level > 0.0f)
        sample += controls.sine_level * mamut_sine(phase);
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
        oscillator->triangle
            + pulse_sample(phase_turns(oscillator->phase_q48), 0.5f, step)
                * 4.0f * step,
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

static float interval_ratio(int interval) {
    if (interval < -24) interval = -24;
    if (interval > 24) interval = 24;
    int octave = interval / 12;
    int semitone = interval % 12;
    if (semitone < 0) {
        semitone += 12;
        octave--;
    }
    float ratio = SEMITONE_RATIO[semitone];
    while (octave < 0) {
        ratio *= 0.5f;
        octave++;
    }
    while (octave > 0) {
        ratio *= 2.0f;
        octave--;
    }
    return ratio;
}

static float note_position_hz(float note) {
    note = clamp_signal(note, 0.0f, 127.0f);
    uint8_t floor_note = (uint8_t)note;
    float fraction = note - floor_note;
    return NOTE_HZ[floor_note] * octave_ratio_small(fraction / 12.0f);
}

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

static ma_smoother initialized_smoother(float value) {
    return (ma_smoother){ .current = value, .target = value };
}

static void set_smoother_target(ma_smoother *smoother, float target,
                                float sample_rate_hz) {
    if (target == smoother->target) return;
    smoother->target = target;
    smoother->remaining = smoothing_frames(sample_rate_hz);
    smoother->step = (target - smoother->current)
                   / (float)smoother->remaining;
}

static void set_bypass_smoother_target(ma_smoother *smoother, float target,
                                       float sample_rate_hz) {
    if (target == 0.0f) {
        *smoother = initialized_smoother(0.0f);
        return;
    }
    set_smoother_target(smoother, target, sample_rate_hz);
}

static float next_smoother(ma_smoother *smoother) {
    if (smoother->remaining == 0) return smoother->current;
    smoother->current += smoother->step;
    smoother->remaining--;
    if (smoother->remaining == 0) smoother->current = smoother->target;
    return smoother->current;
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

static ma_vco_controls next_vco_controls(ma_vco_smoothers *smoothers) {
    return (ma_vco_controls){
        .saw_level = next_smoother(&smoothers->saw_level),
        .pulse_level = next_smoother(&smoothers->pulse_level),
        .triangle_level = next_smoother(&smoothers->triangle_level),
        .sine_level = next_smoother(&smoothers->sine_level),
        .pulse_width = next_smoother(&smoothers->pulse_width),
    };
}

static void set_mozaik_guard_target(ma_mozaik *mozaik, float target,
                                    float sample_rate_hz) {
    if (target == mozaik->guard_target) return;
    mozaik->guard_target = target;
    mozaik->guard_frames = smoothing_frames(sample_rate_hz);
    mozaik->guard_step = (target - mozaik->guard_gain)
                       / (float)mozaik->guard_frames;
}

static float next_mozaik_guard_gain(ma_mozaik *mozaik) {
    if (mozaik->guard_frames == 0) return mozaik->guard_gain;
    mozaik->guard_gain += mozaik->guard_step;
    mozaik->guard_frames--;
    if (mozaik->guard_frames == 0)
        mozaik->guard_gain = mozaik->guard_target;
    return mozaik->guard_gain;
}

static void set_guard_target(ma_oscillator *oscillator, float target,
                             float sample_rate_hz) {
    if (target == oscillator->guard_target) return;
    oscillator->guard_target = target;
    oscillator->guard_frames = smoothing_frames(sample_rate_hz);
    oscillator->guard_step = (target - oscillator->guard_gain)
                           / (float)oscillator->guard_frames;
}

static float next_guard_gain(ma_oscillator *oscillator) {
    if (oscillator->guard_frames == 0) return oscillator->guard_gain;
    oscillator->guard_gain += oscillator->guard_step;
    oscillator->guard_frames--;
    if (oscillator->guard_frames == 0)
        oscillator->guard_gain = oscillator->guard_target;
    return oscillator->guard_gain;
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

static uint32_t mozaik_slope_q32(float control) {
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
        .mozaik_slope_q32 = mozaik_slope_q32(s->mozaik_slope),
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
    c->mozaik_slope_q32 = mozaik_slope_q32(slope);
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

static void shift_mozaik_phason(ma_mozaik *mozaik, uint32_t delta_q32) {
    uint32_t base = mozaik->has_pending_phason
                  ? mozaik->pending_phason_q32
                  : mozaik->applied_phason_q32;
    mozaik->pending_phason_q32 = base + delta_q32;
    mozaik->has_pending_phason = true;
}

static void start_mozaik_tile(ma_synth *s,
                              const ma_render_controls *controls,
                              float f0_hz, float render_rate_hz) {
    ma_mozaik *mozaik = &s->mozaik;
    float leftover = mozaik->tile_pos > mozaik->tile_len
                   ? mozaik->tile_pos - mozaik->tile_len : 0.0f;
    if (mozaik->has_pending_phason) {
        mozaik->frac_q32 += mozaik->pending_phason_q32
                          - mozaik->applied_phason_q32;
        mozaik->applied_phason_q32 = mozaik->pending_phason_q32;
        mozaik->has_pending_phason = false;
    }
    uint32_t previous = mozaik->frac_q32;
    mozaik->frac_q32 += controls->mozaik_slope_q32;
    bool is_long = mozaik->frac_q32 < previous;
    float sigma = (float)controls->mozaik_slope_q32 / Q32_ONE_F;
    float mean_samples = render_rate_hz / (2.0f * f0_hz);
    float mean_factor = (1.0f - sigma)
                      + sigma * controls->mozaik_contrast;
    float kind_factor = is_long ? controls->mozaik_contrast : 1.0f;
    mozaik->tile_len = mean_samples * kind_factor / mean_factor;
    if (mozaik->tile_len < 4.0f) mozaik->tile_len = 4.0f;
    mozaik->tile_pos = leftover;
    mozaik->tile_sign = is_long ? 1.0f : -1.0f;
    mozaik->tiles_emitted++;
}

static float render_mozaik(ma_synth *s,
                           const ma_render_controls *controls,
                           float f0_hz, float render_rate_hz) {
    float upper_hz = 0.125f * s->sample_rate_hz;
    if (upper_hz > 8000.0f) upper_hz = 8000.0f;
    set_mozaik_guard_target(&s->mozaik,
                            f0_hz >= 20.0f && f0_hz <= upper_hz ? 1.0f : 0.0f,
                            render_rate_hz);
    uint32_t drift_q32 = (uint32_t)(0.5f * controls->mozaik_drift
                                   * controls->mozaik_drift * Q32_ONE_F
                                   / render_rate_hz + 0.5f);
    if (drift_q32) shift_mozaik_phason(&s->mozaik, drift_q32);
    if (s->mozaik.tile_pos >= s->mozaik.tile_len)
        start_mozaik_tile(s, controls, f0_hz, render_rate_hz);
    float unit_pos = s->mozaik.tile_pos / s->mozaik.tile_len;
    float hann = 0.5f * (1.0f - tw_sin_turns(wrap_phase(unit_pos + 0.25f)));
    s->mozaik.tile_pos += 1.0f;
    return s->mozaik.tile_sign * hann;
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
    };
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        result.macro[macro] = clamp_control(source->macro[macro],
                                            0.0f, 1.0f, 0.0f);
    return result;
}

static void start_envelope_stage(ma_envelope *envelope,
                                 ma_envelope_stage stage, float target) {
    float error = 0.001f * tw_fabsf(target - envelope->level);
    envelope->completion_error = error > 1.0e-9f ? error : 1.0e-9f;
    envelope->stage = stage;
}

#if !defined(MA_SOURCE_EVIDENCE)
static float envelope_coefficient(float time_ms, float sample_rate_hz) {
    float x = LN1000 / (0.001f * time_ms * sample_rate_hz);
    return tw_one_pole_coeff(x);
}

static float render_envelope(ma_envelope *envelope, ma_adsr controls,
                             float sample_rate_hz) {
    float target = 0.0f;
    float time_ms = controls.release_ms;
    switch (envelope->stage) {
    case MA_ENVELOPE_IDLE:
        envelope->level = 0.0f;
        return 0.0f;
    case MA_ENVELOPE_ATTACK:
        target = 1.0f;
        time_ms = controls.attack_ms;
        break;
    case MA_ENVELOPE_DECAY:
        target = controls.sustain;
        time_ms = controls.decay_ms;
        break;
    case MA_ENVELOPE_SUSTAIN:
        envelope->level = controls.sustain;
        return envelope->level;
    case MA_ENVELOPE_RELEASE:
        break;
    }

    float coefficient = envelope_coefficient(time_ms, sample_rate_hz);
    envelope->level += coefficient * (target - envelope->level);
    if (tw_fabsf(target - envelope->level) > envelope->completion_error)
        return envelope->level;

    envelope->level = target;
    switch (envelope->stage) {
    case MA_ENVELOPE_ATTACK:
        start_envelope_stage(envelope, MA_ENVELOPE_DECAY, controls.sustain);
        break;
    case MA_ENVELOPE_DECAY:
        envelope->completion_error = 0.0f;
        envelope->stage = MA_ENVELOPE_SUSTAIN;
        break;
    case MA_ENVELOPE_RELEASE:
        envelope->completion_error = 0.0f;
        envelope->stage = MA_ENVELOPE_IDLE;
        break;
    case MA_ENVELOPE_IDLE:
    case MA_ENVELOPE_SUSTAIN:
        break;
    }
    return envelope->level;
}

static float filter_envelope_amount(const ma_synth *s,
                                    const ma_render_controls *controls) {
    return clamp_signal(controls->filter_env_amount
                        + 0.25f * ma_velocity_filter(s->velocity),
                        0.0f, 1.0f);
}

static float effective_filter_cutoff(const ma_synth *s,
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
        .mozaik_slope_q32 = mozaik_slope_q32(value.mozaik_slope),
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

static ma_source_frame prepare_source_frame_guarded(
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
[[maybe_unused]] static ma_source_frame prepare_source_frame(const ma_synth *s,
                                            const ma_render_controls *controls,
                                            float base_hz,
                                            float filter_modulation) {
    return prepare_source_frame_guarded(s, controls, base_hz, filter_modulation,
                                        s->oscillator1.guard_gain,
                                        s->oscillator2.guard_gain);
}

static float render_source_substep(ma_synth *s,
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
    set_guard_target(&s->oscillator1, vco1_hz < guard_hz ? 1.0f : 0.0f,
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
            s->oscillator2.triangle
                + pulse_sample(sync_reset_phase, 0.5f,
                               vco2_step * remaining)
                    * 4.0f * vco2_step * remaining,
            -1.25f, 1.25f);
        s->oscillator2.triangle_initialized = true;
    } else {
        advance_phase(&s->oscillator2, vco2_step_q48);
    }

    return (guard1 * vco1 + guard2 * controls->vco2_level * vco2
            + controls->noise_level * noise) / source->mixer_divisor;
}

static void push_oversample_history(float history[31], uint8_t *position,
                                    float sample) {
    history[*position] = sample;
    (*position)++;
    if (*position == 31) *position = 0;
}

static float decimate_oversample(const float history[31], uint8_t position) {
    float output = 0.0f;
    int index = position;
    for (int tap = 0; tap < 31; tap++) {
        if (--index < 0) index = 30;
        output += OSC_HALFBAND_31[tap] * history[index];
    }
    return output;
}

static uint8_t raster_mip(float phase_step, float warp, float *blend) {
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

static float render_raster(ma_synth *s, const ma_render_controls *controls,
                           float frequency_hz, float render_rate_hz) {
    float step = phase_step(frequency_hz, render_rate_hz);
    uint64_t step_q48 = phase_step_q48(step);
    float phase = phase_turns(s->raster.phase_q48);
    float warped = phase
                 + 0.12f * controls->raster_warp * tw_sin_turns(phase);
    if (warped < 0.0f) warped += 1.0f;
    if (warped >= 1.0f) warped -= 1.0f;

    float family_position = controls->raster_position
                          * (float)(MA_RASTER_FAMILIES - 1);
    uint8_t family = (uint8_t)family_position;
    float morph = family_position - (float)family;
    uint8_t next_family = family;
    if (family + 1 < MA_RASTER_FAMILIES) next_family++;
    float mip_blend = 0.0f;
    s->raster.mip = raster_mip(step, controls->raster_warp, &mip_blend);
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
    float radius2 = state.hopf_re * state.hopf_re
                  + state.hopf_im * state.hopf_im;
    float radial = parameters.hopf_mu
                 - parameters.hopf_nonlinearity * radius2;
    float duffing_omega = omega * parameters.duffing_frequency_ratio;
    float square = state.duffing_x * state.duffing_x;
    float edge_damping = (parameters.duffing_edge
                          - parameters.duffing_damping) * duffing_omega;
    float nonlinear_damping = parameters.duffing_cubic_damping
                            * duffing_omega * square;
    float cubic = parameters.duffing_cubic_stiffness * duffing_omega
                * square * state.duffing_x;
    float drive = parameters.duffing_drive * state.hopf_re + external_drive;
    return (ma_bcs_vector){
        .hopf_re = radial * state.hopf_re - omega * state.hopf_im
                 + parameters.duffing_feedback * omega * state.duffing_x,
        .hopf_im = omega * state.hopf_re + radial * state.hopf_im,
        .duffing_x = duffing_omega * state.duffing_v,
        .duffing_v = (edge_damping - nonlinear_damping) * state.duffing_v
                   - duffing_omega * state.duffing_x - cubic
                   + duffing_omega * drive,
    };
}

static ma_bcs_vector bcs_advance(ma_bcs_vector state,
                                 ma_bcs_vector derivative, float scale) {
    return (ma_bcs_vector){
        .hopf_re = state.hopf_re + scale * derivative.hopf_re,
        .hopf_im = state.hopf_im + scale * derivative.hopf_im,
        .duffing_x = state.duffing_x + scale * derivative.duffing_x,
        .duffing_v = state.duffing_v + scale * derivative.duffing_v,
    };
}

static void reset_bcs(ma_bcs *bcs) {
    bcs->hopf_re = 0.025f;
    bcs->hopf_im = 0.0f;
    bcs->duffing_x = 0.001f;
    bcs->duffing_v = 0.0f;
    bcs->output = 0.0f;
    bcs->reset_count++;
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

static float render_bcs(ma_synth *s, float frequency_hz, float amount,
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
        state.hopf_re += (step / 6.0f)
                       * (k1.hopf_re + 2.0f * k2.hopf_re
                          + 2.0f * k3.hopf_re + k4.hopf_re);
        state.hopf_im += (step / 6.0f)
                       * (k1.hopf_im + 2.0f * k2.hopf_im
                          + 2.0f * k3.hopf_im + k4.hopf_im);
        state.duffing_x += (step / 6.0f)
                         * (k1.duffing_x + 2.0f * k2.duffing_x
                            + 2.0f * k3.duffing_x + k4.duffing_x);
        state.duffing_v += (step / 6.0f)
                         * (k1.duffing_v + 2.0f * k2.duffing_v
                            + 2.0f * k3.duffing_v + k4.duffing_v);
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
    s->bcs.output = tw_sat((parameters.hopf_mix * state.hopf_re
                            + parameters.duffing_mix * state.duffing_x)
                           * parameters.output_gain);
    return s->bcs.output;
}

static float filter_prewarp(float cutoff_hz, float sample_rate_hz) {
    float x = cutoff_hz / (2.0f * sample_rate_hz);
    float z = 2.0f * x * (1.0f / 0.42f) - 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    for (int k = 6; k >= 1; k--) {
        float b = 2.0f * z * b1 - b2 + PREWARP_CHEB[k];
        b2 = b1;
        b1 = b;
    }
    return x * (z * b1 - b2 + PREWARP_CHEB[0]);
}

static float mix_source_2x(const ma_render_controls *controls, float analog,
                           float guard1, float guard2,
                           float mozaik_gain, float mozaik, float raster) {
    if (controls->raster_mix == 0.0f) {
        if (!(controls->mozaik_mix > 0.0f && mozaik_gain > 0.0f))
            return analog;
        float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
        float vco2_weight = guard2 > 0.0f ? controls->vco2_level : 0.0f;
        float analog_weight = vco1_weight + vco2_weight
                            + controls->noise_level;
        if (analog_weight < 1.0f) analog_weight = 1.0f;
        return (analog_weight * analog
                + controls->mozaik_mix * mozaik_gain * mozaik)
             / (analog_weight + controls->mozaik_mix);
    }

    float vco1_weight = guard1 > 0.0f ? 1.0f : 0.0f;
    float vco2_weight = guard2 > 0.0f ? controls->vco2_level : 0.0f;
    float analog_weight = vco1_weight + vco2_weight
                        + controls->noise_level;
    if (analog_weight < 1.0f) analog_weight = 1.0f;
    float weighted = analog_weight * analog
                   + controls->raster_mix * raster;
    float weight = analog_weight + controls->raster_mix;
    if (controls->mozaik_mix > 0.0f && mozaik_gain > 0.0f) {
        weighted += controls->mozaik_mix * mozaik_gain * mozaik;
        weight += controls->mozaik_mix;
    }
    return weighted / weight;
}

#if !defined(MA_SOURCE_EVIDENCE)
static float apply_mixer_pressure(float source, float pressure) {
    if (pressure == 0.0f) return source;
    float gain = 1.0f + 5.0f * pressure;
    float shaped = tw_sat(gain * source) / tw_sat(gain);
    return source + pressure * (shaped - source);
}

static void reset_ladder(ma_ladder *ladder) {
    for (int stage = 0; stage < 4; stage++) ladder->state[stage] = 0.0f;
    ladder->y4_guess = 0.0f;
    ladder->reset_count++;
}

static float render_ladder_2x(ma_synth *s,
                              const ma_render_controls *controls,
                              float input, float filter_g) {
    ma_ladder *ladder = &s->ladder;
    float y[4] = { 0 };
    float v[4] = { 0 };
    float next_state[4] = { 0 };
    float y4_guess = ladder->y4_guess;
    float input_gain = 1.0f + 3.0f * controls->filter_drive
                     * controls->filter_drive;
    float feedback = 4.65f * controls->filter_resonance;
    for (int iteration = 0; iteration < 2; iteration++) {
        float u = tw_sat(input_gain * input - feedback * y4_guess);
        for (int stage = 0; stage < 4; stage++) {
            v[stage] = (tw_sat(u) - ladder->state[stage]) * filter_g;
            y[stage] = v[stage] + ladder->state[stage];
            u = y[stage];
        }
        y4_guess = y[3];
    }
    bool finite = y4_guess >= -FLT_MAX && y4_guess <= FLT_MAX;
    for (int stage = 0; stage < 4; stage++) {
        next_state[stage] = y[stage] + v[stage];
        finite = finite
              && y[stage] >= -FLT_MAX && y[stage] <= FLT_MAX
              && v[stage] >= -FLT_MAX && v[stage] <= FLT_MAX
              && next_state[stage] >= -FLT_MAX
              && next_state[stage] <= FLT_MAX;
    }
    if (!finite) {
        reset_ladder(ladder);
        return 0.0f;
    }
    for (int stage = 0; stage < 4; stage++)
        ladder->state[stage] = next_state[stage];
    ladder->y4_guess = y4_guess;
    return y[3];
}

static void push_filter_history(ma_ladder *ladder, float sample) {
    ladder->output_history[ladder->output_pos] = sample;
    ladder->output_pos++;
    if (ladder->output_pos == 7) ladder->output_pos = 0;
}

static float decimate_filter(const ma_ladder *ladder) {
    float output = 0.0f;
    int index = ladder->output_pos;
    for (int tap = 0; tap < 7; tap++) {
        if (--index < 0) index = 6;
        output += FILTER_HALFBAND_7[tap] * ladder->output_history[index];
    }
    return output;
}

static ma_frame render_output(ma_synth *s,
                              const ma_render_controls *controls,
                              float mono) {
    return ma_output_render(&s->output, mono, 0.0f, controls->body_drive,
                             controls->body_load_ratio, s->master_level);
}
#endif

#if !defined(MA_SOURCE_EVIDENCE)
static ma_adsr character_adsr(ma_adsr authored, ma_envelope_time_bias bias,
                              float amount) {
    if (amount > 0.0f) {
        authored.attack_ms *= 1.0f + amount * bias.attack;
        authored.decay_ms *= 1.0f + amount * bias.decay;
        authored.release_ms *= 1.0f + amount * bias.release;
    }
    return authored;
}
#endif

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
        float bcs = render_bcs(s, base_hz, controls.bcs_amount,
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
    float envelope_amount = filter_envelope_amount(s, &controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = effective_filter_cutoff(
        s, &controls, envelope_amount, note_position);
    float filter_g = filter_prewarp(s->filter_cutoff_effective_hz,
                                    s->sample_rate_hz);
#else
    float filter_modulation = 0.0f;
#endif
    float guard1 = s->oscillator1.guard_gain;
    float guard2 = s->oscillator2.guard_gain;
    float noise = next_noise(&s->noise_state);
    ma_source_frame source_frame = prepare_source_frame(
        s, &controls, base_hz, filter_modulation);
    set_guard_target(&s->oscillator2,
        source_frame.vco2_hz < 0.45f * s->sample_rate_hz ? 1.0f : 0.0f,
        s->sample_rate_hz);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        for (int at_4x = 0; at_4x < 2; at_4x++) {
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, &controls, &source_frame,
                                      base_hz, noise, guard1, guard2));
            push_oversample_history(
                s->oversample_history[0], &s->oversample_history_pos[0],
                render_source_substep(s, &controls, &source_frame,
                                      base_hz, noise, guard1, guard2));
            float sample_4x = decimate_oversample(
                s->oversample_history[0], s->oversample_history_pos[0]);
            push_oversample_history(
                s->oversample_history[1], &s->oversample_history_pos[1],
                sample_4x);
        }
        float sample_2x = decimate_oversample(
            s->oversample_history[1], s->oversample_history_pos[1]);
#if defined(MA_SOURCE_EVIDENCE)
        push_oversample_history(
            s->oversample_history[2], &s->oversample_history_pos[2],
            sample_2x);
#else
        float mozaik_gain = s->mozaik.guard_gain;
        float mozaik = render_mozaik(s, &controls, base_hz,
                                     2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
        float raster = controls.raster_mix > 0.0f
                     ? render_raster(s, &controls, base_hz,
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
    (void)next_guard_gain(&s->oscillator1);
    (void)next_guard_gain(&s->oscillator2);

#if defined(MA_SOURCE_EVIDENCE)
    float level = s->note_active ? ma_velocity_level(s->velocity) : 0.0f;
    float analog = decimate_oversample(
        s->oversample_history[2], s->oversample_history_pos[2]);
    float mozaik_gain = s->mozaik.guard_gain;
    float mozaik = render_mozaik(s, &controls, base_hz,
                                 s->sample_rate_hz);
    (void)next_mozaik_guard_gain(&s->mozaik);
    float raster = controls.raster_mix > 0.0f
                 ? render_raster(s, &controls, base_hz, s->sample_rate_hz)
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
    return render_output(s, &controls, output);
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
static void voice_front_a(ma_synth *restrict s, float detune_cents,
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

/* Front end, part B: the BCS output's effect on the controls (when BCS ran),
 * envelopes, prewarp, noise, the source frame, mozaik and raster. */
static void voice_front_b(ma_synth *restrict s, float bcs, float guard1,
                          float guard2, ma_voice_front *restrict f) {
    ma_render_controls *controls = &f->controls;
    float character = f->character;
    float note_position = f->note_position;
    float base_hz = f->base_hz;
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
        s->character.filter_time_bias, character);
    ma_adsr amp_adsr = character_adsr(s->amp_adsr,
        s->character.amp_time_bias, character);
    (void)render_envelope(&s->filter_envelope, filter_adsr,
                          s->sample_rate_hz);
    (void)render_envelope(&s->amp_envelope, amp_adsr,
                          s->sample_rate_hz);
    float envelope_amount = filter_envelope_amount(s, controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = effective_filter_cutoff(
        s, controls, envelope_amount, note_position);
    f->filter_g = filter_prewarp(s->filter_cutoff_effective_hz,
                                 s->sample_rate_hz);
    f->guard1 = guard1;
    f->guard2 = guard2;
    f->noise = next_noise(&s->noise_state);
    f->source = prepare_source_frame_guarded(s, controls, base_hz,
                                             filter_modulation,
                                             guard1, guard2);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        f->mozaik_gain[at_2x] = s->mozaik.guard_gain;
        f->mozaik[at_2x] = render_mozaik(s, controls, base_hz,
                                         2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
        f->raster[at_2x] = controls->raster_mix > 0.0f
                         ? render_raster(s, controls, base_hz,
                                         2.0f * s->sample_rate_hz)
                         : 0.0f;
    }
}

/* The scalar front end: part A, BCS when enabled, part B. */
static void voice_front_end(ma_synth *restrict s, float detune_cents,
                            float ladder_y4, float guard1, float guard2,
                            ma_voice_front *restrict f) {
    voice_front_a(s, detune_cents, f);
    float bcs = 0.0f;
    if (f->controls.bcs_amount > 0.0f)
        bcs = render_bcs(s, f->base_hz, f->controls.bcs_amount,
                         f->controls.bcs_regime, ladder_y4);
    voice_front_b(s, bcs, guard1, guard2, f);
}

/* The VCO2 guard target, eight 8x source substeps, and both guard steps. */
static void voice_oscillator_block(ma_synth *s, const ma_voice_front *f,
                                   float source[8]) {
    set_guard_target(&s->oscillator2,
        f->source.vco2_hz < 0.45f * s->sample_rate_hz ? 1.0f : 0.0f,
        s->sample_rate_hz);
    for (int substep = 0; substep < 8; substep++)
        source[substep] = render_source_substep(s, &f->controls, &f->source,
                                                f->base_hz, f->noise,
                                                f->guard1, f->guard2);
    (void)next_guard_gain(&s->oscillator1);
    (void)next_guard_gain(&s->oscillator2);
}

/* 8x -> 4x -> 2x, in the former push/decimate order. */
static void voice_decimate(ma_synth *s, const float source[8],
                           float at_2x[2]) {
    int substep = 0;
    for (int step_2x = 0; step_2x < 2; step_2x++) {
        for (int step_4x = 0; step_4x < 2; step_4x++) {
            push_oversample_history(s->oversample_history[0],
                                    &s->oversample_history_pos[0],
                                    source[substep++]);
            push_oversample_history(s->oversample_history[0],
                                    &s->oversample_history_pos[0],
                                    source[substep++]);
            float sample_4x = decimate_oversample(
                s->oversample_history[0], s->oversample_history_pos[0]);
            push_oversample_history(s->oversample_history[1],
                                    &s->oversample_history_pos[1],
                                    sample_4x);
        }
        at_2x[step_2x] = decimate_oversample(s->oversample_history[1],
                                             s->oversample_history_pos[1]);
    }
}

/* Two 2x mix/pressure/ladder steps and the 7-tap output decimator. */
static float voice_filter(ma_synth *s, const ma_voice_front *f,
                          const float at_2x[2]) {
    for (int step = 0; step < 2; step++) {
        float source = mix_source_2x(&f->controls, at_2x[step], f->guard1,
                                     f->guard2, f->mozaik_gain[step],
                                     f->mozaik[step], f->raster[step]);
        float pressured = apply_mixer_pressure(source,
                                                f->controls.mixer_pressure);
        s->ladder.pressure_input = source;
        s->ladder.pressure_output = pressured;
        push_filter_history(&s->ladder,
                            render_ladder_2x(s, &f->controls, pressured,
                                             f->filter_g));
    }
    return decimate_filter(&s->ladder);
}

static float voice_back_end(const ma_synth *s, const ma_voice_front *f,
                            float filtered) {
    float velocity_gain = 0.25f + 0.75f * ma_velocity_level(s->velocity);
    float output = filtered * s->amp_envelope.level * velocity_gain;
    if (f->controls.poly_pressure > 0.0f)
        output *= 1.0f + 0.10f * f->controls.poly_pressure;
    if (f->character > 0.0f)
        output *= 1.0f + f->character * s->character.vca_bias;
    return output;
}

static ma_frame synth_tick(ma_synth *s, bool raw, float detune_cents) {
    ma_voice_front f;
    float source[8], at_2x[2];
    voice_front_end(s, detune_cents, s->ladder.y4_guess,
                    s->oscillator1.guard_gain, s->oscillator2.guard_gain, &f);
    voice_oscillator_block(s, &f, source);
    voice_decimate(s, source, at_2x);
    float output = voice_back_end(s, &f, voice_filter(s, &f, at_2x));
    if (raw) return (ma_frame){ .left = output, .right = output };
    return render_output(s, &f.controls, output);
}
#endif

/* ---------------------------------------------------------------------------
 * Vector lanes (2026-11-06): eight pool voices per AVX2 vector, bit-exact.
 * The scalar front and back ends still run per voice. Four stages run eight
 * lanes wide: the oscillator block, the 8x->4x->2x decimators, the 2x
 * mix/pressure/ladder steps, and the 7-tap output decimator. Every lane
 * performs its voice's scalar float operations in the scalar order, with no
 * FMA and an exact replacement for each integer/float conversion AVX2 lacks
 * (see docs/ma3-simd-osc-spike-20261106.md). Only these functions are
 * target("avx2"); the pool calls them after runtime detection.
 * ------------------------------------------------------------------------ */
#if !defined(MA_SOURCE_EVIDENCE) && defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>

bool ma_lanes_supported(void) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
    if (!(c & bit_OSXSAVE) || !(c & bit_AVX)) return false;
    unsigned xcr0 = 0, xcr0_high = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xcr0_high) : "c"(0));
    (void)xcr0_high;
    if ((xcr0 & 6u) != 6u) return false;          /* OS saves XMM and YMM */
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
    return (b & bit_AVX2) != 0;
}

void ma_lanes_begin(ma_voice_lanes *b) {
    *b = (ma_voice_lanes){};
}

void ma_lanes_gather(ma_voice_lanes *b, unsigned lane, const ma_synth *s) {
    const ma_oscillator *osc[2] = { &s->oscillator1, &s->oscillator2 };
    for (int k = 0; k < 2; k++) {
        b->phase_q48[k][lane] = osc[k]->phase_q48;
        b->triangle[k][lane] = osc[k]->triangle;
        b->sync_residual[k][lane] = osc[k]->sync_residual;
        b->guard_gain[k][lane] = osc[k]->guard_gain;
        b->guard_target[k][lane] = osc[k]->guard_target;
        b->guard_step[k][lane] = osc[k]->guard_step;
        b->guard_frames[k][lane] = osc[k]->guard_frames;
        b->triangle_initialized[k][lane] = osc[k]->triangle_initialized ? -1 : 0;
        uint8_t own = s->oversample_history_pos[k];
        b->own_history_pos[k][lane] = own;
        for (int j = 0; j < 31; j++)
            b->history[k][j][lane] = s->oversample_history[k][(own + j) % 31];
    }
    uint8_t own = s->ladder.output_pos;
    b->own_output_pos[lane] = own;
    for (int j = 0; j < 7; j++)
        b->output_history[j][lane] = s->ladder.output_history[(own + j) % 7];
    for (int k = 0; k < 4; k++) b->ladder_state[k][lane] = s->ladder.state[k];
    b->y4_guess[lane] = s->ladder.y4_guess;
    b->pressure_input[lane] = s->ladder.pressure_input;
    b->pressure_output[lane] = s->ladder.pressure_output;
    b->reset_count[lane] = s->ladder.reset_count;
    b->active |= (uint8_t)(1u << lane);
}

void ma_lanes_scatter(const ma_voice_lanes *b, unsigned lane, ma_synth *s) {
    ma_oscillator *osc[2] = { &s->oscillator1, &s->oscillator2 };
    for (int k = 0; k < 2; k++) {
        osc[k]->phase_q48 = b->phase_q48[k][lane];
        osc[k]->triangle = b->triangle[k][lane];
        osc[k]->sync_residual = b->sync_residual[k][lane];
        osc[k]->guard_gain = b->guard_gain[k][lane];
        osc[k]->guard_target = b->guard_target[k][lane];
        osc[k]->guard_step = b->guard_step[k][lane];
        osc[k]->guard_frames = (uint16_t)b->guard_frames[k][lane];
        osc[k]->triangle_initialized = b->triangle_initialized[k][lane] != 0;
        uint8_t own = b->own_history_pos[k][lane];
        for (int j = 0; j < 31; j++)
            s->oversample_history[k][(own + j) % 31] = b->history[k][j][lane];
        s->oversample_history_pos[k] = (uint8_t)((own + b->history_pos[k]) % 31);
    }
    uint8_t own = b->own_output_pos[lane];
    for (int j = 0; j < 7; j++)
        s->ladder.output_history[(own + j) % 7] = b->output_history[j][lane];
    s->ladder.output_pos = (uint8_t)((own + b->output_pos) % 7);
    for (int k = 0; k < 4; k++) s->ladder.state[k] = b->ladder_state[k][lane];
    s->ladder.y4_guess = b->y4_guess[lane];
    s->ladder.pressure_input = b->pressure_input[lane];
    s->ladder.pressure_output = b->pressure_output[lane];
    s->ladder.reset_count = b->reset_count[lane];
}

#define ML_TARGET __attribute__((target("avx2")))
#define ML_F(x) _mm256_set1_ps(x)
typedef __m256 ml_f;
typedef __m256i ml_i;
typedef struct { ml_i lo, hi; } ml_u64;          /* lanes 0-3, 4-7 */

/* Per-sample lane inputs: the front end's products for each lane. */
typedef struct {
    alignas(32) float c1[5][MA_LANES], c2[5][MA_LANES];   /* saw pulse tri sine pw */
    alignas(32) float div1[MA_LANES], div2[MA_LANES], mixdiv[MA_LANES], rate[MA_LANES];
    alignas(32) float v1hz[MA_LANES], v1step[MA_LANES], v2hz[MA_LANES], v2step[MA_LANES];
    alignas(32) float envr[MA_LANES], sync[MA_LANES], base[MA_LANES], noise[MA_LANES];
    alignas(32) float g1[MA_LANES], g2[MA_LANES], crossmod[MA_LANES], v2level[MA_LANES];
    alignas(32) float noiselevel[MA_LANES], guard_hz[MA_LANES], guard_frames_f[MA_LANES];
    alignas(32) float raster_mix[MA_LANES], mozaik_mix[MA_LANES], pressure[MA_LANES];
    alignas(32) float drive[MA_LANES], resonance[MA_LANES], filter_g[MA_LANES];
    alignas(32) float mozaik_gain[2][MA_LANES], mozaik[2][MA_LANES], raster[2][MA_LANES];
    alignas(32) uint64_t v1q[MA_LANES], v2q[MA_LANES];
    alignas(32) int32_t act1[MA_LANES], act2[MA_LANES], guard_frames_i[MA_LANES];
} ml_in;

static void ml_benign(ml_in *in) {
    *in = (ml_in){};
    for (int l = 0; l < MA_LANES; l++) {
        in->div1[l] = in->div2[l] = in->mixdiv[l] = in->rate[l] = 1.0f;
        in->guard_frames_f[l] = 1.0f;
        in->guard_frames_i[l] = 1;
    }
}

/* Kept for white-box probes; the lane tick writes lane inputs directly. */
[[maybe_unused]] static void ml_fill(ml_in *in, unsigned l, const ma_voice_front *f,
                    const ma_synth *s) {
    const ma_vco_controls *v[2] = { &f->source.vco1, &f->source.vco2 };
    float (*c[2])[MA_LANES] = { in->c1, in->c2 };
    for (int k = 0; k < 2; k++) {
        c[k][0][l] = v[k]->saw_level;
        c[k][1][l] = v[k]->pulse_level;
        c[k][2][l] = v[k]->triangle_level;
        c[k][3][l] = v[k]->sine_level;
        c[k][4][l] = v[k]->pulse_width;
    }
    in->div1[l] = f->source.vco1_divisor;
    in->div2[l] = f->source.vco2_divisor;
    in->mixdiv[l] = f->source.mixer_divisor;
    in->rate[l] = f->source.rate_hz;
    in->v1hz[l] = f->source.vco1_hz;
    in->v1step[l] = f->source.vco1_step;
    in->v1q[l] = f->source.vco1_step_q48;
    in->v2hz[l] = f->source.vco2_hz;
    in->v2step[l] = f->source.vco2_step;
    in->v2q[l] = f->source.vco2_step_q48;
    in->envr[l] = f->source.envelope_ratio;
    in->sync[l] = f->source.sync;
    in->act1[l] = (int32_t)f->source.vco1_active;
    in->act2[l] = (int32_t)f->source.vco2_active;
    in->base[l] = f->base_hz;
    in->noise[l] = f->noise;
    in->g1[l] = f->guard1;
    in->g2[l] = f->guard2;
    in->crossmod[l] = f->controls.crossmod_amount;
    in->v2level[l] = f->controls.vco2_level;
    in->noiselevel[l] = f->controls.noise_level;
    in->guard_hz[l] = 0.45f * s->sample_rate_hz;
    uint16_t frames = smoothing_frames(s->sample_rate_hz);
    in->guard_frames_i[l] = frames;
    in->guard_frames_f[l] = (float)frames;
    in->raster_mix[l] = f->controls.raster_mix;
    in->mozaik_mix[l] = f->controls.mozaik_mix;
    in->pressure[l] = f->controls.mixer_pressure;
    in->drive[l] = f->controls.filter_drive;
    in->resonance[l] = f->controls.filter_resonance;
    in->filter_g[l] = f->filter_g;
    for (int k = 0; k < 2; k++) {
        in->mozaik_gain[k][l] = f->mozaik_gain[k];
        in->mozaik[k][l] = f->mozaik[k];
        in->raster[k][l] = f->raster[k];
    }
}

/* ---- lane primitives ---- */
ML_TARGET static inline ml_f ml_ld(const float *a) { return _mm256_loadu_ps(a); }
ML_TARGET static inline bool ml_any(ml_f m) { return !_mm256_testz_ps(m, m); }
ML_TARGET static inline ml_f ml_sel(ml_f a, ml_f b, ml_f m) { return _mm256_blendv_ps(a, b, m); }
ML_TARGET static inline ml_f ml_lt(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
ML_TARGET static inline ml_f ml_gt(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_GT_OQ); }
ML_TARGET static inline ml_f ml_ge(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_GE_OQ); }
ML_TARGET static inline ml_f ml_le(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_LE_OQ); }
ML_TARGET static inline ml_f ml_add(ml_f a, ml_f b) { return _mm256_add_ps(a, b); }
ML_TARGET static inline ml_f ml_sub(ml_f a, ml_f b) { return _mm256_sub_ps(a, b); }
ML_TARGET static inline ml_f ml_mul(ml_f a, ml_f b) { return _mm256_mul_ps(a, b); }
ML_TARGET static inline ml_f ml_div(ml_f a, ml_f b) { return _mm256_div_ps(a, b); }
ML_TARGET static inline ml_f ml_and(ml_f a, ml_f b) { return _mm256_and_ps(a, b); }

ML_TARGET static inline ml_u64 ml_u64_set(uint64_t x) {
    return (ml_u64){ _mm256_set1_epi64x((long long)x), _mm256_set1_epi64x((long long)x) };
}
ML_TARGET static inline ml_u64 ml_u64_ld(const uint64_t a[MA_LANES]) {
    return (ml_u64){ _mm256_loadu_si256((const ml_i *)a), _mm256_loadu_si256((const ml_i *)(a + 4)) };
}
ML_TARGET static inline void ml_u64_st(uint64_t a[MA_LANES], ml_u64 v) {
    _mm256_storeu_si256((ml_i *)a, v.lo);
    _mm256_storeu_si256((ml_i *)(a + 4), v.hi);
}
ML_TARGET static inline ml_u64 ml_u64_add(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_add_epi64(a.lo, b.lo), _mm256_add_epi64(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_sub(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_sub_epi64(a.lo, b.lo), _mm256_sub_epi64(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_and(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_and_si256(a.lo, b.lo), _mm256_and_si256(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_srl(ml_u64 a, int n) {
    return (ml_u64){ _mm256_srli_epi64(a.lo, n), _mm256_srli_epi64(a.hi, n) };
}
/* low 32 bits of each 64-bit lane, as 8 x i32 in lane order */
ML_TARGET static inline ml_i ml_u64_low32(ml_u64 v) {
    const ml_i idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    return _mm256_permute2x128_si256(_mm256_permutevar8x32_epi32(v.lo, idx),
                                     _mm256_permutevar8x32_epi32(v.hi, idx), 0x20);
}
ML_TARGET static inline ml_u64 ml_mask64(ml_f m) {
    ml_i i = _mm256_castps_si256(m);
    return (ml_u64){ _mm256_cvtepi32_epi64(_mm256_castsi256_si128(i)),
                     _mm256_cvtepi32_epi64(_mm256_extracti128_si256(i, 1)) };
}
ML_TARGET static inline ml_u64 ml_u64_sel(ml_u64 a, ml_u64 b, ml_f m) {
    ml_u64 m64 = ml_mask64(m);
    return (ml_u64){ _mm256_blendv_epi8(a.lo, b.lo, m64.lo), _mm256_blendv_epi8(a.hi, b.hi, m64.hi) };
}
ML_TARGET static inline ml_f ml_u64_ge(ml_u64 a, uint64_t bound) {   /* both < 2^62 */
    ml_u64 b = ml_u64_set(bound - 1);
    return _mm256_castsi256_ps(ml_u64_low32((ml_u64){
        _mm256_cmpgt_epi64(a.lo, b.lo), _mm256_cmpgt_epi64(a.hi, b.hi) }));
}
/* (float)(uint32_t)u exactly: two exact 16-bit halves, one rounding */
ML_TARGET static inline ml_f ml_u32_to_f(ml_i u) {
    ml_f hi = _mm256_cvtepi32_ps(_mm256_srli_epi32(u, 16));
    ml_f lo = _mm256_cvtepi32_ps(_mm256_and_si256(u, _mm256_set1_epi32(0xffff)));
    return ml_add(ml_mul(hi, ML_F(65536.0f)), lo);
}
ML_TARGET static inline ml_f ml_turns(ml_u64 q) {          /* phase_turns */
    return ml_mul(ml_u32_to_f(ml_u64_low32(ml_u64_srl(q, 16))), ML_F(0x1p-32f));
}
/* (float)x exactly for x <= 2^48: two exact 24-bit halves, one rounding */
ML_TARGET static inline ml_f ml_u48_to_f(ml_u64 x) {
    ml_f hi = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_srl(x, 24)));
    ml_f lo = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_and(x, ml_u64_set(0xffffffu))));
    return ml_add(ml_mul(hi, ML_F(16777216.0f)), lo);
}
/* (uint64_t)f for +-0 <= f < 2^63: truncated mantissa * 2^(e - 150) */
ML_TARGET static inline ml_i ml_f_to_u64_half(ml_i bits) {
    ml_i e = _mm256_and_si256(_mm256_srli_epi64(bits, 23), _mm256_set1_epi64x(0xff));
    ml_i m = _mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi64x(0x7fffff)),
                             _mm256_set1_epi64x(0x800000));
    return _mm256_or_si256(
        _mm256_sllv_epi64(m, _mm256_sub_epi64(e, _mm256_set1_epi64x(150))),
        _mm256_srlv_epi64(m, _mm256_sub_epi64(_mm256_set1_epi64x(150), e)));
}
ML_TARGET static inline ml_u64 ml_f_to_u64(ml_f f) {
    ml_i bits = _mm256_castps_si256(f);
    return (ml_u64){
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_castsi256_si128(bits))),
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_extracti128_si256(bits, 1))) };
}

/* ---- the scalar helpers, lane by lane ---- */
ML_TARGET static inline ml_f ml_clamp(ml_f v, float lo, float hi) {
    return ml_sel(ml_sel(v, ML_F(hi), ml_gt(v, ML_F(hi))), ML_F(lo), ml_lt(v, ML_F(lo)));
}
ML_TARGET static inline ml_f ml_abs(ml_f x) {
    return ml_and(x, _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)));
}
ML_TARGET static inline ml_f ml_smooth5(ml_f x) {
    return ml_mul(ml_mul(ml_mul(x, x), x),
                  ml_add(ML_F(10.0f), ml_mul(x, ml_add(ML_F(-15.0f), ml_mul(ML_F(6.0f), x)))));
}
ML_TARGET static inline ml_f ml_wrap(ml_f p) {
    p = ml_sel(p, ml_sub(p, ML_F(1.0f)), ml_ge(p, ML_F(1.0f)));
    return ml_sel(p, ml_add(p, ML_F(1.0f)), ml_lt(p, ML_F(0.0f)));
}
ML_TARGET static inline ml_f ml_poly_blep(ml_f phase, ml_f step) {
    ml_f valid = ml_gt(step, ML_F(FLT_EPSILON));
    ml_f edge = ml_mul(ML_F(8.0f), step);
    edge = ml_sel(edge, ML_F(0.49f), ml_gt(edge, ML_F(0.49f)));
    ml_f low = ml_and(ml_lt(phase, edge), valid);
    ml_f high = _mm256_andnot_ps(low, ml_and(ml_gt(phase, ml_sub(ML_F(1.0f), edge)), valid));
    if (!ml_any(_mm256_or_ps(low, high))) return _mm256_setzero_ps();
    ml_f xl = ml_mul(ML_F(0.5f), ml_add(ml_div(phase, edge), ML_F(1.0f)));
    ml_f xh = ml_mul(ML_F(0.5f), ml_add(ml_div(ml_sub(phase, ML_F(1.0f)), edge), ML_F(1.0f)));
    return _mm256_or_ps(ml_and(low, ml_mul(ML_F(2.0f), ml_sub(ml_smooth5(xl), ML_F(1.0f)))),
                        ml_and(high, ml_mul(ML_F(2.0f), ml_smooth5(xh))));
}
ML_TARGET static inline ml_f ml_saw(ml_f phase, ml_f blep) {
    return ml_clamp(ml_sub(ml_sub(ml_mul(ML_F(2.0f), phase), ML_F(1.0f)), blep), -1.25f, 1.25f);
}
ML_TARGET static inline ml_f ml_pulse_with_blep(ml_f phase, ml_f width, ml_f step, ml_f blep) {
    ml_f sample = ml_sel(ML_F(-1.0f), ML_F(1.0f), ml_lt(phase, width));
    sample = ml_add(sample, blep);
    sample = ml_sub(sample, ml_poly_blep(ml_wrap(ml_sub(phase, width)), step));
    return ml_clamp(sample, -1.25f, 1.25f);
}
ML_TARGET static inline ml_f ml_pulse(ml_f phase, ml_f width, ml_f step) {
    return ml_pulse_with_blep(phase, width, step, ml_poly_blep(phase, step));
}
ML_TARGET static inline ml_f ml_raw_triangle(ml_f p) {
    return ml_sub(ML_F(1.0f), ml_mul(ML_F(4.0f), ml_abs(ml_sub(p, ML_F(0.5f)))));
}
ML_TARGET static inline ml_f ml_sin_turns(ml_f p) {
    ml_f t = ml_sel(ml_sel(ml_sub(p, ML_F(1.0f)), ml_sub(ML_F(0.5f), p), ml_lt(p, ML_F(0.75f))),
                    p, ml_lt(p, ML_F(0.25f)));
    ml_f q = ml_mul(t, t);
    return ml_mul(t, ml_add(ML_F(6.28318531f), ml_mul(q, ml_add(ML_F(-41.3417022f),
        ml_mul(q, ml_add(ML_F(81.6052493f), ml_mul(q, ml_add(ML_F(-76.7058597f),
        ml_mul(q, ML_F(42.0586939f))))))))));
}
ML_TARGET static inline ml_f ml_sat(ml_f x) {               /* tw_sat */
    x = ml_sel(ml_sel(x, ML_F(-3.0f), ml_lt(x, ML_F(-3.0f))), ML_F(3.0f), ml_gt(x, ML_F(3.0f)));
    return ml_div(ml_mul(x, ml_add(ML_F(27.0f), ml_mul(x, x))),
                  ml_add(ML_F(27.0f), ml_mul(ml_mul(ML_F(9.0f), x), x)));
}
ML_TARGET static inline ml_f ml_mamut_sine(ml_f phase) {
    ml_f warm = ml_div(ml_sat(ml_mul(ML_F(1.45f), ml_sin_turns(phase))), ML_F(tw_sat(1.45f)));
    ml_f second = ml_sin_turns(ml_wrap(ml_add(ml_mul(ML_F(2.0f), phase), ML_F(0.07f))));
    return ml_mul(ML_F(0.95591217f), ml_add(warm, ml_mul(ML_F(0.07f), second)));
}

typedef struct { ml_f saw, pulse, tri, sine, pw; } ml_vco;
ML_TARGET static inline ml_vco ml_vco_ld(const float c[5][MA_LANES]) {
    return (ml_vco){ ml_ld(c[0]), ml_ld(c[1]), ml_ld(c[2]), ml_ld(c[3]), ml_ld(c[4]) };
}
ML_TARGET static inline ml_f ml_raw_wave_mix(ml_f phase, const ml_vco *c, ml_f divisor) {
    ml_f saw = ml_sub(ml_mul(ML_F(2.0f), phase), ML_F(1.0f));
    ml_f pulse = ml_sel(ML_F(-1.0f), ML_F(1.0f), ml_lt(phase, c->pw));
    ml_f sample = ml_add(ml_add(ml_mul(c->saw, saw), ml_mul(c->pulse, pulse)),
                         ml_mul(c->tri, ml_raw_triangle(phase)));
    ml_f sine = ml_gt(c->sine, _mm256_setzero_ps());
    if (ml_any(sine))
        sample = ml_sel(sample, ml_add(sample, ml_mul(c->sine, ml_mamut_sine(phase))), sine);
    return ml_div(sample, divisor);
}

typedef struct {
    ml_u64 phase;
    ml_f triangle, residual, gain, target, step, init;
    ml_i frames;
} ml_osc;
ML_TARGET static inline ml_osc ml_osc_ld(const ma_voice_lanes *b, int k) {
    return (ml_osc){ ml_u64_ld(b->phase_q48[k]), ml_ld(b->triangle[k]), ml_ld(b->sync_residual[k]),
                     ml_ld(b->guard_gain[k]), ml_ld(b->guard_target[k]), ml_ld(b->guard_step[k]),
                     _mm256_castsi256_ps(_mm256_loadu_si256((const ml_i *)b->triangle_initialized[k])),
                     _mm256_loadu_si256((const ml_i *)b->guard_frames[k]) };
}
ML_TARGET static inline void ml_osc_st(ma_voice_lanes *b, int k, const ml_osc *o) {
    ml_u64_st(b->phase_q48[k], o->phase);
    _mm256_storeu_ps(b->triangle[k], o->triangle);
    _mm256_storeu_ps(b->sync_residual[k], o->residual);
    _mm256_storeu_ps(b->guard_gain[k], o->gain);
    _mm256_storeu_ps(b->guard_target[k], o->target);
    _mm256_storeu_ps(b->guard_step[k], o->step);
    _mm256_storeu_si256((ml_i *)b->triangle_initialized[k], _mm256_castps_si256(o->init));
    _mm256_storeu_si256((ml_i *)b->guard_frames[k], o->frames);
}

ML_TARGET static inline ml_f ml_preview(const ml_osc *o, const ml_vco *c, ml_f step,
                                        ml_f divisor, const int32_t active[MA_LANES]) {
    ml_i act = _mm256_loadu_si256((const ml_i *)active);
    ml_f phase = ml_turns(o->phase);
    ml_f blep = ml_poly_blep(phase, step);
    ml_f saw_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_SAW)), _mm256_set1_epi32(WAVE_SAW)));
    ml_f pulse_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_PULSE)), _mm256_set1_epi32(WAVE_PULSE)));
    ml_f tri_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_TRIANGLE)), _mm256_set1_epi32(WAVE_TRIANGLE)));
    ml_f saw = ml_and(saw_on, ml_saw(phase, blep));
    ml_f pulse = ml_and(pulse_on, ml_pulse_with_blep(phase, c->pw, step, blep));
    ml_f tri = ml_and(tri_on, ml_sel(ml_raw_triangle(phase),
                                     ml_clamp(o->triangle, -1.0f, 1.0f), o->init));
    ml_f sample = ml_add(ml_add(ml_mul(c->saw, saw), ml_mul(c->pulse, pulse)), ml_mul(c->tri, tri));
    ml_f sine = ml_gt(c->sine, _mm256_setzero_ps());
    if (ml_any(sine))
        sample = ml_sel(sample, ml_add(sample, ml_mul(c->sine, ml_mamut_sine(phase))), sine);
    return ml_div(sample, divisor);
}

ML_TARGET static inline ml_f ml_render_oscillator(ml_osc *o, ml_f preview, ml_f step,
                                                  ml_f sync_correction) {
    ml_f phase = ml_turns(o->phase);
    const ml_f all = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    if (!_mm256_testc_ps(o->init, all)) {
        o->triangle = ml_sel(ml_raw_triangle(phase), o->triangle, o->init);
        o->init = all;
    }
    ml_f sample = ml_add(ml_add(preview, o->residual), sync_correction);
    o->residual = _mm256_setzero_ps();
    o->triangle = ml_clamp(ml_add(o->triangle, ml_mul(ml_mul(ml_pulse(phase, ML_F(0.5f), step),
                                                              ML_F(4.0f)), step)), -1.25f, 1.25f);
    return ml_clamp(sample, -1.25f, 1.25f);
}

ML_TARGET static inline void ml_set_guard_target(ml_osc *o, ml_f target, const ml_in *in) {
    ml_f changed = _mm256_cmp_ps(target, o->target, _CMP_NEQ_UQ);
    if (!ml_any(changed)) return;
    o->target = ml_sel(o->target, target, changed);
    o->frames = _mm256_blendv_epi8(o->frames, _mm256_loadu_si256((const ml_i *)in->guard_frames_i),
                                   _mm256_castps_si256(changed));
    o->step = ml_sel(o->step, ml_div(ml_sub(target, o->gain), ml_ld(in->guard_frames_f)), changed);
}
ML_TARGET static inline void ml_next_guard_gain(ml_osc *o) {
    ml_i zero = _mm256_setzero_si256();
    ml_i active = _mm256_xor_si256(_mm256_cmpeq_epi32(o->frames, zero), _mm256_set1_epi32(-1));
    ml_f act = _mm256_castsi256_ps(active);
    o->gain = ml_sel(o->gain, ml_add(o->gain, o->step), act);
    o->frames = _mm256_sub_epi32(o->frames, _mm256_and_si256(active, _mm256_set1_epi32(1)));
    ml_f done = ml_and(act, _mm256_castsi256_ps(_mm256_cmpeq_epi32(o->frames, zero)));
    o->gain = ml_sel(o->gain, o->target, done);
}

/* render_source_substep, eight lanes */
ML_TARGET static inline ml_f ml_substep(ml_osc *o1, ml_osc *o2, const ml_in *in,
                                        const ml_vco *c1, const ml_vco *c2, ml_f guard_hz) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    const ml_u64 mask48 = ml_u64_set(PHASE_MASK_Q48);
    ml_f div2 = ml_ld(in->div2), v2step = ml_ld(in->v2step), sync = ml_ld(in->sync);
    ml_u64 v2q = ml_u64_ld(in->v2q);
    ml_f v2preview = ml_preview(o2, c2, v2step, div2, in->act2);

    ml_f v1hz = ml_ld(in->v1hz), v1step = ml_ld(in->v1step);
    ml_u64 v1q = ml_u64_ld(in->v1q);
    ml_f crossmod = ml_ld(in->crossmod);
    ml_f cm = _mm256_cmp_ps(crossmod, zero, _CMP_NEQ_UQ);
    if (ml_any(cm)) {
        ml_f ratio = ml_clamp(ml_add(one, ml_mul(ml_mul(v2preview, crossmod), ML_F(0.25f))),
                              0.25f, 4.0f);
        ml_f hz = ml_mul(ml_mul(ml_ld(in->base), ratio), ml_ld(in->envr));
        ml_f step = ml_clamp(ml_div(hz, ml_ld(in->rate)), 0.0f, 0.45f);
        ml_u64 q = ml_f_to_u64(ml_mul(step, ML_F(0x1p48f)));
        v1hz = ml_sel(v1hz, hz, cm);
        v1q = ml_u64_sel(v1q, q, cm);
        v1step = ml_sel(v1step, ml_turns(q), cm);
    }
    ml_set_guard_target(o1, ml_sel(zero, one, ml_lt(v1hz, guard_hz)), in);

    ml_u64 sum1 = ml_u64_add(o1->phase, v1q);
    ml_f wrap = ml_u64_ge(sum1, PHASE_ONE_Q48);
    ml_f sync_on = ml_gt(sync, zero);
    ml_f event = ml_and(ml_and(wrap, sync_on), ml_gt(v1step, ML_F(FLT_EPSILON)));
    ml_f event_fraction = one, sync_now = zero, sync_next = zero;
    ml_u64 reset_q = ml_u64_set(0);
    if (ml_any(event)) {
        ml_f fraction = ml_clamp(ml_div(ml_u48_to_f(ml_u64_sub(ml_u64_set(PHASE_ONE_Q48), o1->phase)),
                                        ml_u48_to_f(v1q)), 0.0f, 1.0f);
        ml_u64 advance = ml_f_to_u64(ml_mul(ml_u48_to_f(v2q), fraction));
        ml_u64 event_q = ml_u64_and(ml_u64_add(o2->phase, advance), mask48);
        ml_u64 reset = ml_u64_and(ml_f_to_u64(ml_mul(ml_u48_to_f(event_q), ml_sub(one, sync))), mask48);
        ml_f jump = ml_sub(ml_raw_wave_mix(ml_turns(reset), c2, div2),
                           ml_raw_wave_mix(ml_turns(event_q), c2, div2));
        ml_f before = ml_mul(ML_F(0.5f), ml_sub(one, fraction));
        ml_f after = ml_sub(one, ml_mul(ML_F(0.5f), fraction));
        event_fraction = ml_sel(one, fraction, event);
        sync_now = ml_and(event, ml_mul(jump, ml_smooth5(before)));
        sync_next = ml_and(event, ml_mul(jump, ml_sub(ml_smooth5(after), one)));
        reset_q = ml_u64_sel(reset_q, reset, event);
    }

    ml_f v1preview = ml_preview(o1, c1, v1step, ml_ld(in->div1), in->act1);
    ml_f vco1 = ml_render_oscillator(o1, v1preview, v1step, zero);
    ml_f vco2 = ml_render_oscillator(o2, v2preview, v2step, sync_now);
    o2->residual = ml_add(o2->residual, sync_next);
    o1->phase = ml_u64_and(sum1, mask48);

    ml_u64 advanced = ml_u64_and(ml_u64_add(o2->phase, v2q), mask48);
    ml_f restart = ml_and(wrap, sync_on);
    if (ml_any(restart)) {
        ml_f remaining = ml_sub(one, event_fraction);
        ml_u64 rest = ml_f_to_u64(ml_mul(ml_u48_to_f(v2q), remaining));
        ml_u64 restarted = ml_u64_and(ml_u64_add(reset_q, rest), mask48);
        ml_f reset_phase = ml_turns(reset_q);
        ml_f tri = ml_clamp(ml_add(ml_raw_triangle(reset_phase),
            ml_mul(ml_mul(ml_mul(ml_pulse(reset_phase, ML_F(0.5f), ml_mul(v2step, remaining)),
                                 ML_F(4.0f)), v2step), remaining)), -1.25f, 1.25f);
        o2->phase = ml_u64_sel(advanced, restarted, restart);
        o2->triangle = ml_sel(o2->triangle, tri, restart);
        o2->init = _mm256_or_ps(o2->init, restart);
    } else {
        o2->phase = advanced;
    }
    ml_f mix = ml_add(ml_add(ml_mul(ml_ld(in->g1), vco1),
                             ml_mul(ml_mul(ml_ld(in->g2), ml_ld(in->v2level)), vco2)),
                      ml_mul(ml_ld(in->noiselevel), ml_ld(in->noise)));
    return ml_div(mix, ml_ld(in->mixdiv));
}

ML_TARGET static inline void ml_push31(float history[31][MA_LANES], uint8_t *position, ml_f x) {
    _mm256_storeu_ps(history[*position], x);
    if (++*position == 31) *position = 0;
}
ML_TARGET static inline ml_f ml_decimate31(const float history[31][MA_LANES], uint8_t position) {
    ml_f output = _mm256_setzero_ps();
    int index = position;
    for (int tap = 0; tap < 31; tap++) {
        if (--index < 0) index = 30;
        output = ml_add(output, ml_mul(ML_F(OSC_HALFBAND_31[tap]), ml_ld(history[index])));
    }
    return output;
}
ML_TARGET static inline void ml_push7(float history[7][MA_LANES], uint8_t *position, ml_f x) {
    _mm256_storeu_ps(history[*position], x);
    if (++*position == 7) *position = 0;
}
ML_TARGET static inline ml_f ml_decimate7(const float history[7][MA_LANES], uint8_t position) {
    ml_f output = _mm256_setzero_ps();
    int index = position;
    for (int tap = 0; tap < 7; tap++) {
        if (--index < 0) index = 6;
        output = ml_add(output, ml_mul(ML_F(FILTER_HALFBAND_7[tap]), ml_ld(history[index])));
    }
    return output;
}

/* mix_source_2x, eight lanes */
ML_TARGET static inline ml_f ml_mix(const ml_in *in, ml_f analog, int step) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    ml_f raster_mix = ml_ld(in->raster_mix), mozaik_mix = ml_ld(in->mozaik_mix);
    ml_f mozaik_gain = ml_ld(in->mozaik_gain[step]);
    ml_f vco1_weight = ml_sel(zero, one, ml_gt(ml_ld(in->g1), zero));
    ml_f vco2_weight = ml_sel(zero, ml_ld(in->v2level), ml_gt(ml_ld(in->g2), zero));
    ml_f weight_a = ml_add(ml_add(vco1_weight, vco2_weight), ml_ld(in->noiselevel));
    weight_a = ml_sel(weight_a, one, ml_lt(weight_a, one));
    ml_f mozaik_on = ml_and(ml_gt(mozaik_mix, zero), ml_gt(mozaik_gain, zero));
    ml_f mozaik = ml_mul(ml_mul(mozaik_mix, mozaik_gain), ml_ld(in->mozaik[step]));
    ml_f no_raster = ml_sel(analog, ml_div(ml_add(ml_mul(weight_a, analog), mozaik),
                                           ml_add(weight_a, mozaik_mix)), mozaik_on);
    ml_f weighted = ml_add(ml_mul(weight_a, analog), ml_mul(raster_mix, ml_ld(in->raster[step])));
    ml_f weight = ml_add(weight_a, raster_mix);
    weighted = ml_sel(weighted, ml_add(weighted, mozaik), mozaik_on);
    weight = ml_sel(weight, ml_add(weight, mozaik_mix), mozaik_on);
    return ml_sel(ml_div(weighted, weight), no_raster,
                  _mm256_cmp_ps(raster_mix, zero, _CMP_EQ_OQ));
}

/* apply_mixer_pressure, eight lanes */
ML_TARGET static inline ml_f ml_pressure(ml_f source, ml_f pressure) {
    ml_f off = _mm256_cmp_ps(pressure, _mm256_setzero_ps(), _CMP_EQ_OQ);
    if (_mm256_testc_ps(off, _mm256_castsi256_ps(_mm256_set1_epi32(-1)))) return source;
    ml_f gain = ml_add(ML_F(1.0f), ml_mul(ML_F(5.0f), pressure));
    ml_f shaped = ml_div(ml_sat(ml_mul(gain, source)), ml_sat(gain));
    return ml_sel(ml_add(source, ml_mul(pressure, ml_sub(shaped, source))), source, off);
}

ML_TARGET static inline ml_f ml_in_range(ml_f a) {
    return ml_and(ml_ge(a, ML_F(-FLT_MAX)), ml_le(a, ML_F(FLT_MAX)));
}
/* render_ladder_2x, eight lanes */
ML_TARGET static inline ml_f ml_ladder(ma_voice_lanes *b, ml_f state[4], ml_f *y4_guess,
                                       ml_f input, ml_f g, ml_f drive, ml_f resonance) {
    ml_f input_gain = ml_add(ML_F(1.0f), ml_mul(ml_mul(ML_F(3.0f), drive), drive));
    ml_f feedback = ml_mul(ML_F(4.65f), resonance);
    ml_f y[4], v[4], next[4], y4 = *y4_guess;
    for (int iteration = 0; iteration < 2; iteration++) {
        ml_f u = ml_sat(ml_sub(ml_mul(input_gain, input), ml_mul(feedback, y4)));
        for (int stage = 0; stage < 4; stage++) {
            v[stage] = ml_mul(ml_sub(ml_sat(u), state[stage]), g);
            y[stage] = ml_add(v[stage], state[stage]);
            u = y[stage];
        }
        y4 = y[3];
    }
    ml_f finite = ml_in_range(y4);
    for (int stage = 0; stage < 4; stage++) {
        next[stage] = ml_add(y[stage], v[stage]);
        finite = ml_and(finite, ml_and(ml_in_range(y[stage]),
                                       ml_and(ml_in_range(v[stage]), ml_in_range(next[stage]))));
    }
    const ml_f zero = _mm256_setzero_ps();
    for (int stage = 0; stage < 4; stage++) state[stage] = ml_sel(zero, next[stage], finite);
    *y4_guess = ml_sel(zero, y4, finite);
    unsigned reset = ~(unsigned)_mm256_movemask_ps(finite) & 0xffu;
    while (reset) {
        b->reset_count[__builtin_ctz(reset)]++;
        reset &= reset - 1;
    }
    return ml_sel(zero, y[3], finite);
}

/* render_bcs, eight lanes: RK4 over four substeps per sample. */
/* Per-lane scalars straight into a vector register (no store/reload, so no
 * blocked store forwarding). `get(l)` is read only for lanes in `on`; other
 * lanes take `fallback`. */
#define ML_LANE(on, l, get, fallback) (((on) >> (l) & 1u) ? (get(l)) : (fallback))
#define ML_PICK_PS(on, get, fallback) _mm256_setr_ps( \
    ML_LANE(on, 0, get, fallback), ML_LANE(on, 1, get, fallback), \
    ML_LANE(on, 2, get, fallback), ML_LANE(on, 3, get, fallback), \
    ML_LANE(on, 4, get, fallback), ML_LANE(on, 5, get, fallback), \
    ML_LANE(on, 6, get, fallback), ML_LANE(on, 7, get, fallback))
#define ML_PICK_U64(on, get) ((ml_u64){ \
    _mm256_setr_epi64x((long long)ML_LANE(on, 0, get, 0u), (long long)ML_LANE(on, 1, get, 0u), \
                       (long long)ML_LANE(on, 2, get, 0u), (long long)ML_LANE(on, 3, get, 0u)), \
    _mm256_setr_epi64x((long long)ML_LANE(on, 4, get, 0u), (long long)ML_LANE(on, 5, get, 0u), \
                       (long long)ML_LANE(on, 6, get, 0u), (long long)ML_LANE(on, 7, get, 0u)) })
ML_TARGET static inline ml_f ml_lane_mask(unsigned on) {
    return _mm256_castsi256_ps(_mm256_cmpgt_epi32(
        _mm256_and_si256(_mm256_set1_epi32((int)on), _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128)),
        _mm256_setzero_si256()));
}

typedef struct { ml_f re, im, x, v; } ml_bcs_state;
typedef struct {
    ml_f hopf_mu, hopf_nonlinearity, duffing_frequency_ratio, duffing_damping,
         duffing_edge, duffing_cubic_stiffness, duffing_cubic_damping,
         duffing_drive, duffing_feedback, hopf_mix, duffing_mix, output_gain;
} ml_bcs_parameters;

ML_TARGET static inline ml_bcs_state ml_bcs_derivative(ml_bcs_state st,
    const ml_bcs_parameters *q, ml_f omega, ml_f external_drive) {
    ml_f radius2 = ml_add(ml_mul(st.re, st.re), ml_mul(st.im, st.im));
    ml_f radial = ml_sub(q->hopf_mu, ml_mul(q->hopf_nonlinearity, radius2));
    ml_f duffing_omega = ml_mul(omega, q->duffing_frequency_ratio);
    ml_f square = ml_mul(st.x, st.x);
    ml_f edge_damping = ml_mul(ml_sub(q->duffing_edge, q->duffing_damping), duffing_omega);
    ml_f nonlinear_damping = ml_mul(ml_mul(q->duffing_cubic_damping, duffing_omega), square);
    ml_f cubic = ml_mul(ml_mul(ml_mul(q->duffing_cubic_stiffness, duffing_omega), square), st.x);
    ml_f drive = ml_add(ml_mul(q->duffing_drive, st.re), external_drive);
    return (ml_bcs_state){
        .re = ml_add(ml_sub(ml_mul(radial, st.re), ml_mul(omega, st.im)),
                     ml_mul(ml_mul(q->duffing_feedback, omega), st.x)),
        .im = ml_add(ml_mul(omega, st.re), ml_mul(radial, st.im)),
        .x = ml_mul(duffing_omega, st.v),
        .v = ml_add(ml_sub(ml_sub(ml_mul(ml_sub(edge_damping, nonlinear_damping), st.v),
                                  ml_mul(duffing_omega, st.x)), cubic),
                    ml_mul(duffing_omega, drive)),
    };
}
ML_TARGET static inline ml_bcs_state ml_bcs_advance(ml_bcs_state st, ml_bcs_state d, ml_f scale) {
    return (ml_bcs_state){ ml_add(st.re, ml_mul(scale, d.re)), ml_add(st.im, ml_mul(scale, d.im)),
                           ml_add(st.x, ml_mul(scale, d.x)), ml_add(st.v, ml_mul(scale, d.v)) };
}
/* k1 + 2 k2 + 2 k3 + k4, left to right as in the scalar sum */
ML_TARGET static inline ml_f ml_rk4_sum(ml_f k1, ml_f k2, ml_f k3, ml_f k4) {
    return ml_add(ml_add(ml_add(k1, ml_mul(ML_F(2.0f), k2)), ml_mul(ML_F(2.0f), k3)), k4);
}

/* bcs_lerp(first->m, second->m, fraction) for landmark index lm, eight lanes:
 * the four landmark values sit in one constant vector, indexed per lane. */
ML_TARGET static inline ml_f ml_bcs_landmark_lerp(float l0, float l1, float l2, float l3,
                                                  ml_i lm, ml_f fraction) {
    const ml_f table = _mm256_setr_ps(l0, l1, l2, l3, l3, l3, l3, l3);
    ml_f first = _mm256_permutevar8x32_ps(table, lm);
    ml_f second = _mm256_permutevar8x32_ps(table, _mm256_add_epi32(lm, _mm256_set1_epi32(1)));
    return ml_add(first, ml_mul(fraction, ml_sub(second, first)));
}

/* BCS for the lanes in `on`. bcs_parameters, the clamps and the step run
 * eight lanes wide; state stays in each voice's ma_synth and is read into
 * registers directly. A lane whose state leaves the safe range takes the
 * scalar reset, exactly as render_bcs does. */
ML_TARGET static void ml_bcs(ma_synth *const voice[MA_LANES],
                             const ma_voice_front front[MA_LANES],
                             const float ladder_y4[MA_LANES], unsigned on,
                             float out[MA_LANES]) {
#define BCS_REGIME(l) front[l].controls.bcs_regime
#define BCS_AMOUNT(l) front[l].controls.bcs_amount
#define BCS_BASE(l) front[l].base_hz
#define BCS_RATE(l) voice[l]->sample_rate_hz
#define BCS_RE(l) voice[l]->bcs.hopf_re
#define BCS_IM(l) voice[l]->bcs.hopf_im
#define BCS_X(l) voice[l]->bcs.duffing_x
#define BCS_V(l) voice[l]->bcs.duffing_v
    const ml_f on_mask = ml_lane_mask(on);
    /* bcs_parameters */
    ml_f position = ml_mul(ML_PICK_PS(on, BCS_REGIME, 0.0f), ML_F(3.0f));
    ml_i landmark = _mm256_min_epi32(_mm256_cvttps_epi32(position), _mm256_set1_epi32(2));
    ml_f fraction = ml_sub(position, _mm256_cvtepi32_ps(landmark));
#define BCS_P(m) ml_bcs_landmark_lerp(BCS_LANDMARK[0].m, BCS_LANDMARK[1].m, BCS_LANDMARK[2].m, \
                                      BCS_LANDMARK[3].m, landmark, fraction)
    ml_bcs_parameters p = {
        BCS_P(hopf_mu), BCS_P(hopf_nonlinearity), BCS_P(duffing_frequency_ratio),
        BCS_P(duffing_damping), BCS_P(duffing_edge), BCS_P(duffing_cubic_stiffness),
        BCS_P(duffing_cubic_damping), BCS_P(duffing_drive), BCS_P(duffing_feedback),
        BCS_P(hopf_mix), BCS_P(duffing_mix), BCS_P(output_gain) };
#undef BCS_P
    ml_f rate = ML_PICK_PS(on, BCS_RATE, 48000.0f);
    ml_f up = ml_mul(ML_F(0.125f), rate);
    up = ml_sel(up, ML_F(8000.0f), ml_gt(up, ML_F(8000.0f)));
    ml_f f = ML_PICK_PS(on, BCS_BASE, 20.0f);
    f = ml_sel(ml_sel(f, up, ml_gt(f, up)), ML_F(20.0f), ml_lt(f, ML_F(20.0f)));
    ml_f omega = ml_mul(ML_F(BCS_TAU), f);
    ml_f h = ml_div(ML_F(1.0f), ml_mul(rate, ML_F((float)BCS_INTEGRATION_STEPS)));
    ml_f external_drive = ml_mul(ml_mul(ML_F(0.018f), ML_PICK_PS(on, BCS_AMOUNT, 0.0f)),
                                 ml_clamp(ml_and(on_mask, ml_ld(ladder_y4)), -1.0f, 1.0f));
    ml_bcs_state y = { ML_PICK_PS(on, BCS_RE, 0.0f), ML_PICK_PS(on, BCS_IM, 0.0f),
                       ML_PICK_PS(on, BCS_X, 0.0f), ML_PICK_PS(on, BCS_V, 0.0f) };
#undef BCS_REGIME
#undef BCS_AMOUNT
#undef BCS_BASE
#undef BCS_RATE
#undef BCS_RE
#undef BCS_IM
#undef BCS_X
#undef BCS_V
    for (int substep = 0; substep < BCS_INTEGRATION_STEPS; substep++) {
        ml_bcs_state k1 = ml_bcs_derivative(y, &p, omega, external_drive);
        ml_bcs_state k2 = ml_bcs_derivative(ml_bcs_advance(y, k1, ml_mul(ML_F(0.5f), h)),
                                            &p, omega, external_drive);
        ml_bcs_state k3 = ml_bcs_derivative(ml_bcs_advance(y, k2, ml_mul(ML_F(0.5f), h)),
                                            &p, omega, external_drive);
        ml_bcs_state k4 = ml_bcs_derivative(ml_bcs_advance(y, k3, h), &p, omega, external_drive);
        ml_f sixth = ml_div(h, ML_F(6.0f));
        y.re = ml_add(y.re, ml_mul(sixth, ml_rk4_sum(k1.re, k2.re, k3.re, k4.re)));
        y.im = ml_add(y.im, ml_mul(sixth, ml_rk4_sum(k1.im, k2.im, k3.im, k4.im)));
        y.x = ml_add(y.x, ml_mul(sixth, ml_rk4_sum(k1.x, k2.x, k3.x, k4.x)));
        y.v = ml_add(y.v, ml_mul(sixth, ml_rk4_sum(k1.v, k2.v, k3.v, k4.v)));
    }
    /* bcs_state_is_safe: every value finite and within the ceiling; the
     * peak is the running maximum of |value| in hopf_re, hopf_im, x, v order */
    const ml_f ceiling = ML_F(BCS_STATE_CEILING);
    ml_f safe = _mm256_castsi256_ps(_mm256_set1_epi32(-1)), peak = _mm256_setzero_ps();
    const ml_f values[4] = { y.re, y.im, y.x, y.v };
    for (int k = 0; k < 4; k++) {
        ml_f magnitude = ml_abs(values[k]);
        safe = ml_and(safe, ml_and(ml_in_range(values[k]), _mm256_cmp_ps(magnitude, ceiling, _CMP_LE_OQ)));
        peak = ml_sel(peak, magnitude, ml_gt(magnitude, peak));
    }
    ml_f output = ml_sat(ml_mul(ml_add(ml_mul(p.hopf_mix, y.re), ml_mul(p.duffing_mix, y.x)),
                                p.output_gain));
    float re[MA_LANES], im[MA_LANES], x[MA_LANES], v[MA_LANES], pk[MA_LANES], o[MA_LANES];
    _mm256_storeu_ps(re, y.re); _mm256_storeu_ps(im, y.im);
    _mm256_storeu_ps(x, y.x); _mm256_storeu_ps(v, y.v);
    _mm256_storeu_ps(pk, peak); _mm256_storeu_ps(o, output);
    unsigned safe_bits = (unsigned)_mm256_movemask_ps(safe);
    for (unsigned l = 0; l < MA_LANES; l++) {
        if (!(on >> l & 1u)) continue;
        ma_bcs *bcs = &voice[l]->bcs;
        if (!(safe_bits >> l & 1u)) {
            reset_bcs(bcs);
            out[l] = 0.0f;
            continue;
        }
        bcs->hopf_re = re[l]; bcs->hopf_im = im[l];
        bcs->duffing_x = x[l]; bcs->duffing_v = v[l];
        if (pk[l] > bcs->maximum_state) bcs->maximum_state = pk[l];
        bcs->output = o[l];
        out[l] = o[l];
    }
}

/* Front end, part B, for a lane: the scalar parts of voice_front_b (BCS
 * effect, envelopes, noise, mozaik) and the plain lane inputs, written
 * directly. Raster follows eight lanes wide in ml_raster; it reads only its
 * own state, base_hz and the raster controls, none of which this touches,
 * so moving it after mozaik changes nothing. Cutoff, prewarp and the source
 * frame follow in ml_front_math; they read only the envelope level, the
 * controls and the guards. */
static void voice_front_b_lane(ma_synth *restrict s, float bcs, float guard1,
                               float guard2, ma_voice_front *restrict f,
                               ml_in *in, unsigned l) {
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
    in->base[l] = f->base_hz;
    in->noise[l] = f->noise;
    in->g1[l] = guard1;
    in->g2[l] = guard2;
    in->crossmod[l] = controls->crossmod_amount;
    in->v2level[l] = controls->vco2_level;
    in->noiselevel[l] = controls->noise_level;
    in->guard_hz[l] = 0.45f * s->sample_rate_hz;
    uint16_t frames = smoothing_frames(s->sample_rate_hz);
    in->guard_frames_i[l] = frames;
    in->guard_frames_f[l] = (float)frames;
    in->raster_mix[l] = controls->raster_mix;
    in->mozaik_mix[l] = controls->mozaik_mix;
    in->pressure[l] = controls->mixer_pressure;
    in->drive[l] = controls->filter_drive;
    in->resonance[l] = controls->filter_resonance;
    for (int k = 0; k < 2; k++) {
        in->mozaik_gain[k][l] = f->mozaik_gain[k];
        in->mozaik[k][l] = f->mozaik[k];
    }
}

/* Front-end math for eight voices, written straight into the lane inputs:
 * filter_envelope_amount, effective_filter_cutoff, filter_prewarp and
 * prepare_source_frame_guarded, each lane in its scalar operation order.
 * Per-lane scalar inputs are gathered by the caller (ml_front_in). */
typedef struct {
    float env_level[MA_LANES], env_amount[MA_LANES], velocity_filter[MA_LANES];
    float note[MA_LANES], keytrack[MA_LANES], cutoff_hz[MA_LANES], poly[MA_LANES];
    float character[MA_LANES], cutoff_octaves[MA_LANES], vco2_cents[MA_LANES];
    float cutoff_max[MA_LANES], rate[MA_LANES];
    float vco[2][5][MA_LANES];                       /* saw pulse tri sine pw */
    float sync_amount[MA_LANES], sync_softness[MA_LANES], v2level[MA_LANES];
    float noise_level[MA_LANES], crossmod[MA_LANES], fine_cents[MA_LANES];
    float base[MA_LANES], interval[MA_LANES], g1[MA_LANES], g2[MA_LANES];
} ml_front_in;

static void ml_front_benign(ml_front_in *x) {
    *x = (ml_front_in){};
    for (int l = 0; l < MA_LANES; l++) {
        x->cutoff_hz[l] = 1000.0f; x->cutoff_max[l] = 20000.0f;
        x->rate[l] = 48000.0f; x->interval[l] = 1.0f;
    }
}

static void ml_front_gather(ml_front_in *x, unsigned l, const ma_synth *s,
                            const ma_voice_front *f, float guard1, float guard2) {
    const ma_render_controls *c = &f->controls;
    x->env_level[l] = s->filter_envelope.level;
    x->env_amount[l] = c->filter_env_amount;
    x->velocity_filter[l] = ma_velocity_filter(s->velocity);
    x->note[l] = f->note_position;
    x->keytrack[l] = c->filter_keytrack;
    x->cutoff_hz[l] = c->filter_cutoff_hz;
    x->poly[l] = c->poly_pressure;
    x->character[l] = s->character.smoother.current;
    x->cutoff_octaves[l] = s->character.cutoff_octaves;
    x->vco2_cents[l] = s->character.vco2_cents;
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    x->cutoff_max[l] = cutoff_max;
    x->rate[l] = s->sample_rate_hz;
    const ma_vco_controls *v[2] = { &c->vco1, &c->vco2 };
    for (int k = 0; k < 2; k++) {
        x->vco[k][0][l] = v[k]->saw_level; x->vco[k][1][l] = v[k]->pulse_level;
        x->vco[k][2][l] = v[k]->triangle_level; x->vco[k][3][l] = v[k]->sine_level;
        x->vco[k][4][l] = v[k]->pulse_width;
    }
    x->sync_amount[l] = c->sync_amount; x->sync_softness[l] = c->sync_softness;
    x->v2level[l] = c->vco2_level; x->noise_level[l] = c->noise_level;
    x->crossmod[l] = c->crossmod_amount; x->fine_cents[l] = c->vco2_fine_cents;
    x->base[l] = f->base_hz;
    x->interval[l] = interval_ratio(s->vco2_interval);
    x->g1[l] = guard1; x->g2[l] = guard2;
}

ML_TARGET static inline ml_f ml_octave_ratio_small(ml_f octaves) {
    octaves = ml_clamp(octaves, -0.25f, 0.25f);
    ml_f value = ML_F(EXP2_SMALL[5]);
    for (int i = 4; i >= 0; i--) value = ml_add(ml_mul(value, octaves), ML_F(EXP2_SMALL[i]));
    return value;
}
ML_TARGET static inline ml_f ml_phase_step(ml_f hz, ml_f rate) {
    return ml_clamp(ml_div(hz, rate), 0.0f, 0.45f);
}
ML_TARGET static inline ml_i ml_active(const ml_f level[5]) {
    const ml_f zero = _mm256_setzero_ps();
    ml_i saw = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[0], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_SAW));
    ml_i pulse = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[1], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_PULSE));
    ml_i tri = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[2], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_TRIANGLE));
    return _mm256_or_si256(_mm256_or_si256(saw, pulse), tri);
}
ML_TARGET static inline ml_f ml_waveform_divisor(const ml_f level[5]) {
    ml_f sum = ml_add(ml_add(ml_add(level[0], level[1]), level[2]), level[3]);
    return ml_sel(ML_F(1.0f), sum, ml_gt(sum, ML_F(1.0f)));
}

/* Writes the source-frame fields and filter_g of `in`; cutoff_out gets
 * effective_filter_cutoff for each lane (the caller stores it per voice). */
ML_TARGET static void ml_front_math(const ml_front_in *x, ml_in *in, float cutoff_out[MA_LANES]) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    /* filter_envelope_amount, filter modulation */
    ml_f amount = ml_clamp(ml_add(ml_ld(x->env_amount), ml_mul(ML_F(0.25f), ml_ld(x->velocity_filter))), 0.0f, 1.0f);
    ml_f level = ml_ld(x->env_level);
    ml_f modulation = ml_mul(level, amount);
    /* effective_filter_cutoff */
    ml_f envelope = ml_add(one, ml_mul(ml_mul(ML_F(0.78f), level), amount));
    ml_f keytrack = ml_add(one, ml_mul(ml_mul(ml_mul(ml_sub(ml_ld(x->note), ML_F(60.0f)),
                                                     ML_F(1.0f / 48.0f)), ml_ld(x->keytrack)), ML_F(0.42f)));
    keytrack = ml_clamp(keytrack, 0.55f, 1.35f);
    ml_f cutoff = ml_mul(ml_mul(ml_ld(x->cutoff_hz), envelope), keytrack);
    ml_f poly = ml_ld(x->poly);
    ml_f poly_on = ml_gt(poly, zero);
    if (ml_any(poly_on))
        cutoff = ml_sel(cutoff, ml_mul(cutoff, ml_octave_ratio_small(ml_mul(ML_F(0.25f), poly))), poly_on);
    ml_f character = ml_ld(x->character);
    ml_f character_on = ml_gt(character, zero);
    if (ml_any(character_on))
        cutoff = ml_sel(cutoff, ml_mul(cutoff, ml_octave_ratio_small(ml_mul(character, ml_ld(x->cutoff_octaves)))),
                        character_on);
    ml_f cutoff_max = ml_ld(x->cutoff_max);
    cutoff = ml_sel(ml_sel(cutoff, cutoff_max, ml_gt(cutoff, cutoff_max)), ML_F(20.0f), ml_lt(cutoff, ML_F(20.0f)));
    _mm256_storeu_ps(cutoff_out, cutoff);
    /* filter_prewarp */
    ml_f fs = ml_ld(x->rate);
    ml_f xw = ml_div(cutoff, ml_mul(ML_F(2.0f), fs));
    ml_f z = ml_sub(ml_mul(ml_mul(ML_F(2.0f), xw), ML_F(1.0f / 0.42f)), one);
    ml_f b1 = zero, b2 = zero;
    for (int k = 6; k >= 1; k--) {
        ml_f bk = ml_add(ml_sub(ml_mul(ml_mul(ML_F(2.0f), z), b1), b2), ML_F(PREWARP_CHEB[k]));
        b2 = b1;
        b1 = bk;
    }
    _mm256_storeu_ps(in->filter_g, ml_mul(xw, ml_add(ml_sub(ml_mul(z, b1), b2), ML_F(PREWARP_CHEB[0]))));
    /* prepare_source_frame_guarded */
    ml_f v1[5], v2[5];
    for (int k = 0; k < 5; k++) { v1[k] = ml_ld(x->vco[0][k]); v2[k] = ml_ld(x->vco[1][k]); }
    _mm256_storeu_si256((ml_i *)in->act1, ml_active(v1));
    _mm256_storeu_si256((ml_i *)in->act2, ml_active(v2));
    _mm256_storeu_ps(in->div1, ml_waveform_divisor(v1));
    _mm256_storeu_ps(in->div2, ml_waveform_divisor(v2));
    ml_f rate = ml_mul(ML_F(8.0f), fs);
    _mm256_storeu_ps(in->rate, rate);
    _mm256_storeu_ps(in->sync, ml_mul(ml_ld(x->sync_amount),
        ml_clamp(ml_sub(one, ml_mul(ML_F(0.75f), ml_ld(x->sync_softness))), 0.0f, 1.0f)));
    v1[4] = ml_clamp(ml_add(v1[4], ml_mul(ML_F(0.120f), modulation)), 0.05f, 0.95f);
    v2[4] = ml_clamp(ml_sub(v2[4], ml_mul(ML_F(0.080f), modulation)), 0.05f, 0.95f);
    for (int k = 0; k < 5; k++) { _mm256_storeu_ps(in->c1[k], v1[k]); _mm256_storeu_ps(in->c2[k], v2[k]); }
    ml_f envelope_ratio = ml_octave_ratio_small(ml_mul(ML_F(0.125f), modulation));
    _mm256_storeu_ps(in->envr, envelope_ratio);
    ml_f vco1_weight = ml_sel(zero, one, ml_gt(ml_ld(x->g1), zero));
    ml_f vco2_weight = ml_sel(zero, ml_ld(x->v2level), ml_gt(ml_ld(x->g2), zero));
    ml_f mixer = ml_add(ml_add(vco1_weight, vco2_weight), ml_ld(x->noise_level));
    _mm256_storeu_ps(in->mixdiv, ml_sel(mixer, one, ml_lt(mixer, one)));
    ml_f base = ml_ld(x->base);
    ml_f no_crossmod = _mm256_cmp_ps(ml_ld(x->crossmod), zero, _CMP_EQ_OQ);
    ml_f v1hz = ml_mul(base, envelope_ratio);
    ml_u64 v1q = ml_f_to_u64(ml_mul(ml_phase_step(v1hz, rate), ML_F(0x1p48f)));
    ml_f v1step = ml_turns(v1q);
    _mm256_storeu_ps(in->v1hz, ml_and(no_crossmod, v1hz));
    _mm256_storeu_ps(in->v1step, ml_and(no_crossmod, v1step));
    ml_u64_st(in->v1q, ml_u64_sel(ml_u64_set(0), v1q, no_crossmod));
    ml_f fine = ml_octave_ratio_small(ml_div(ml_ld(x->fine_cents), ML_F(1200.0f)));
    ml_f v2hz = ml_mul(ml_mul(base, ml_ld(x->interval)), fine);
    if (ml_any(character_on))
        v2hz = ml_sel(v2hz, ml_mul(v2hz, ml_octave_ratio_small(
            ml_div(ml_mul(character, ml_ld(x->vco2_cents)), ML_F(1200.0f)))), character_on);
    _mm256_storeu_ps(in->v2hz, v2hz);
    ml_u64 v2q = ml_f_to_u64(ml_mul(ml_phase_step(v2hz, rate), ML_F(0x1p48f)));
    ml_u64_st(in->v2q, v2q);
    _mm256_storeu_ps(in->v2step, ml_turns(v2q));
}

/* Both table values raster_table_sample reads, eight lanes: one 32-bit
 * gather per row fetches the int16 pair (index, index + 1). Index 255 wraps
 * to 0 within its row, so it reads the pair at 254 and, when any lane needs
 * it, entry 0 of the row; no read leaves MA_RASTER_TABLE. */
ML_TARGET static inline ml_f ml_raster_row(ml_i row, ml_i word, ml_i last, bool any_last,
                                           ml_f fraction) {
    const int *table = (const int *)(const void *)&MA_RASTER_TABLE[0][0][0];
    ml_i pair = _mm256_i32gather_epi32(table, _mm256_add_epi32(row, word), 2);
    ml_i low = _mm256_srai_epi32(_mm256_slli_epi32(pair, 16), 16);
    ml_i high = _mm256_srai_epi32(pair, 16);
    ml_i a = _mm256_blendv_epi8(low, high, last), b = high;
    if (any_last) {
        ml_i start = _mm256_i32gather_epi32(table, row, 2);
        b = _mm256_blendv_epi8(high, _mm256_srai_epi32(_mm256_slli_epi32(start, 16), 16), last);
    }
    const ml_f scale = ML_F(1.0f / 32768.0f);
    ml_f first = ml_mul(_mm256_cvtepi32_ps(a), scale);
    ml_f second = ml_mul(_mm256_cvtepi32_ps(b), scale);
    return ml_add(first, ml_mul(fraction, ml_sub(second, first)));
}

/* render_raster for the lanes in `on`, both 2x steps of one output sample,
 * written to in->raster (0 for other lanes, as the scalar path gives).
 * Everything runs eight lanes wide: the phase step and q48, raster_mip (the
 * limits halve, so the scalar walk equals a count of passing limits), family
 * and morph, the phase warp, the table reads and interpolations, the mip
 * blend and the phase advance. Lanes outside `on` compute on benign inputs
 * (rate 1, everything else 0), are masked out and never written back. */
ML_TARGET static void ml_raster(ma_synth *const voice[MA_LANES],
                                const ma_voice_front front[MA_LANES],
                                unsigned on, ml_in *in) {
#define RASTER_BASE(l) front[l].base_hz
#define RASTER_RATE(l) voice[l]->sample_rate_hz
#define RASTER_POSITION(l) front[l].controls.raster_position
#define RASTER_WARP(l) front[l].controls.raster_warp
#define RASTER_PHASE(l) voice[l]->raster.phase_q48
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    const ml_f on_mask = ml_lane_mask(on);
    ml_f step = ml_phase_step(ML_PICK_PS(on, RASTER_BASE, 0.0f),
                              ml_mul(ML_F(2.0f), ML_PICK_PS(on, RASTER_RATE, 1.0f)));
    const ml_u64 vstep = ml_f_to_u64(ml_mul(step, ML_F(0x1p48f)));
    const ml_f warp = ML_PICK_PS(on, RASTER_WARP, 0.0f);
    /* raster_mip */
    ml_f effective_step = ml_mul(step, ml_add(one, ml_mul(ML_F(0.8f), warp)));
    ml_i mip = _mm256_setzero_si256();
    for (int k = 0; k + 1 < MA_RASTER_MIPS; k++)
        mip = _mm256_sub_epi32(mip, _mm256_castps_si256(
            ml_gt(ml_mul(effective_step, ML_F((float)(64 >> k))), ML_F(0.45f))));
    ml_f load = ml_mul(effective_step, _mm256_cvtepi32_ps(_mm256_srlv_epi32(_mm256_set1_epi32(64), mip)));
    ml_i top = _mm256_cmpeq_epi32(mip, _mm256_set1_epi32(MA_RASTER_MIPS - 1));
    ml_f blend = ml_sel(ml_clamp(ml_mul(ml_sub(load, ML_F(0.225f)), ML_F(1.0f / 0.225f)), 0.0f, 1.0f),
                        zero, _mm256_castsi256_ps(top));
    const ml_f blend_on = ml_gt(blend, zero);
    const bool any_blend = ml_any(blend_on);
    /* family, morph and the table rows (coarse row clamped where unused) */
    ml_f family_position = ml_mul(ML_PICK_PS(on, RASTER_POSITION, 0.0f),
                                  ML_F((float)(MA_RASTER_FAMILIES - 1)));
    ml_i family = _mm256_cvttps_epi32(family_position);
    const ml_f morph = ml_sub(family_position, _mm256_cvtepi32_ps(family));
    ml_i next_family = _mm256_min_epi32(_mm256_add_epi32(family, _mm256_set1_epi32(1)),
                                        _mm256_set1_epi32(MA_RASTER_FAMILIES - 1));
    ml_i coarse = _mm256_min_epi32(_mm256_add_epi32(mip, _mm256_set1_epi32(1)),
                                   _mm256_set1_epi32(MA_RASTER_MIPS - 1));
    const ml_i mips = _mm256_set1_epi32(MA_RASTER_MIPS);
#define RASTER_ROW(f, m) _mm256_slli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(f, mips), m), 8)
    const ml_i row_a = RASTER_ROW(family, mip), row_b = RASTER_ROW(next_family, mip);
    const ml_i coarse_a = RASTER_ROW(family, coarse), coarse_b = RASTER_ROW(next_family, coarse);
#undef RASTER_ROW
    ml_u64 vphase = ML_PICK_U64(on, RASTER_PHASE);
    const ml_u64 mask48 = ml_u64_set(PHASE_MASK_Q48);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        ml_f p = ml_turns(vphase);
        ml_f warped = ml_add(p, ml_mul(ml_mul(ML_F(0.12f), warp), ml_sin_turns(p)));
        warped = ml_sel(warped, ml_add(warped, one), ml_lt(warped, zero));
        warped = ml_sel(warped, ml_sub(warped, one), ml_ge(warped, one));
        ml_f table_phase = ml_mul(warped, ML_F((float)MA_RASTER_SAMPLES));
        ml_i whole = _mm256_cvttps_epi32(table_phase);   /* (uint16_t): 0 <= table_phase < 256 */
        ml_f fraction = ml_sub(table_phase, _mm256_cvtepi32_ps(whole));
        ml_i index = _mm256_and_si256(whole, _mm256_set1_epi32(MA_RASTER_SAMPLES - 1));
        ml_i last = _mm256_cmpeq_epi32(index, _mm256_set1_epi32(MA_RASTER_SAMPLES - 1));
        bool any_last = !_mm256_testz_si256(last, last);
        ml_i word = _mm256_min_epi32(index, _mm256_set1_epi32(MA_RASTER_SAMPLES - 2));
        ml_f first = ml_raster_row(row_a, word, last, any_last, fraction);
        ml_f second = ml_raster_row(row_b, word, last, any_last, fraction);
        ml_f sample = ml_add(first, ml_mul(morph, ml_sub(second, first)));
        if (any_blend) {
            first = ml_raster_row(coarse_a, word, last, any_last, fraction);
            second = ml_raster_row(coarse_b, word, last, any_last, fraction);
            ml_f coarse_sample = ml_add(first, ml_mul(morph, ml_sub(second, first)));
            sample = ml_sel(sample, ml_add(sample, ml_mul(blend, ml_sub(coarse_sample, sample))), blend_on);
        }
        _mm256_storeu_ps(in->raster[at_2x], ml_and(on_mask, sample));
        vphase = ml_u64_and(ml_u64_add(vphase, vstep), mask48);
    }
    alignas(32) uint64_t phase[MA_LANES];
    alignas(32) int32_t mip_out[MA_LANES];
    ml_u64_st(phase, vphase);
    _mm256_store_si256((ml_i *)mip_out, mip);
    for (unsigned l = 0; l < MA_LANES; l++)
        if (on >> l & 1u) {
            voice[l]->raster.phase_q48 = phase[l];
            voice[l]->raster.mip = (uint8_t)mip_out[l];
        }
#undef RASTER_BASE
#undef RASTER_RATE
#undef RASTER_POSITION
#undef RASTER_WARP
#undef RASTER_PHASE
}

/* Oscillator block, decimators, filter steps and output decimator. */
ML_TARGET static void ml_kernel(ma_voice_lanes *b, const ml_in *in, float filtered[MA_LANES]) {
    ml_osc o1 = ml_osc_ld(b, 0), o2 = ml_osc_ld(b, 1);
    ml_vco c1 = ml_vco_ld(in->c1), c2 = ml_vco_ld(in->c2);
    ml_f guard_hz = ml_ld(in->guard_hz);
    ml_set_guard_target(&o2, ml_sel(_mm256_setzero_ps(), ML_F(1.0f),
                                    ml_lt(ml_ld(in->v2hz), guard_hz)), in);
    ml_f source[8];
    for (int substep = 0; substep < 8; substep++)
        source[substep] = ml_substep(&o1, &o2, in, &c1, &c2, guard_hz);
    ml_next_guard_gain(&o1);
    ml_next_guard_gain(&o2);
    ml_osc_st(b, 0, &o1);
    ml_osc_st(b, 1, &o2);

    ml_f at_2x[2];
    int substep = 0;
    for (int step_2x = 0; step_2x < 2; step_2x++) {
        for (int step_4x = 0; step_4x < 2; step_4x++) {
            ml_push31(b->history[0], &b->history_pos[0], source[substep++]);
            ml_push31(b->history[0], &b->history_pos[0], source[substep++]);
            ml_push31(b->history[1], &b->history_pos[1],
                      ml_decimate31(b->history[0], b->history_pos[0]));
        }
        at_2x[step_2x] = ml_decimate31(b->history[1], b->history_pos[1]);
    }

    ml_f state[4] = { ml_ld(b->ladder_state[0]), ml_ld(b->ladder_state[1]),
                      ml_ld(b->ladder_state[2]), ml_ld(b->ladder_state[3]) };
    ml_f y4 = ml_ld(b->y4_guess);
    ml_f pressure = ml_ld(in->pressure), g = ml_ld(in->filter_g);
    ml_f drive = ml_ld(in->drive), resonance = ml_ld(in->resonance);
    for (int step = 0; step < 2; step++) {
        ml_f mixed = ml_mix(in, at_2x[step], step);
        ml_f pressured = ml_pressure(mixed, pressure);
        _mm256_storeu_ps(b->pressure_input, mixed);
        _mm256_storeu_ps(b->pressure_output, pressured);
        ml_push7(b->output_history, &b->output_pos,
                 ml_ladder(b, state, &y4, pressured, g, drive, resonance));
    }
    for (int stage = 0; stage < 4; stage++) _mm256_storeu_ps(b->ladder_state[stage], state[stage]);
    _mm256_storeu_ps(b->y4_guess, y4);
    _mm256_storeu_ps(filtered, ml_decimate7(b->output_history, b->output_pos));
}

void ma_lanes_tick(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                   const float detune_cents[MA_LANES], float out[MA_LANES]) {
    ma_voice_front front[MA_LANES];
    ml_in in;
    ml_benign(&in);
    unsigned bcs_on = 0;
    for (unsigned lane = 0; lane < MA_LANES; lane++) {
        if (!(b->active >> lane & 1u)) continue;
        voice_front_a(voice[lane], detune_cents[lane], &front[lane]);
        if (front[lane].controls.bcs_amount > 0.0f) bcs_on |= 1u << lane;
    }
    float bcs[MA_LANES] = {};
    if (bcs_on) ml_bcs(voice, front, b->y4_guess, bcs_on, bcs);
    ml_front_in fx;
    ml_front_benign(&fx);
    for (unsigned lane = 0; lane < MA_LANES; lane++) {
        if (!(b->active >> lane & 1u)) continue;
        voice_front_b_lane(voice[lane], bcs[lane], b->guard_gain[0][lane],
                           b->guard_gain[1][lane], &front[lane], &in, lane);
        ml_front_gather(&fx, lane, voice[lane], &front[lane],
                        b->guard_gain[0][lane], b->guard_gain[1][lane]);
    }
    unsigned raster_on = 0;
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        if ((b->active >> lane & 1u) && front[lane].controls.raster_mix > 0.0f)
            raster_on |= 1u << lane;
    if (raster_on) ml_raster(voice, front, raster_on, &in);
    float cutoff[MA_LANES];
    ml_front_math(&fx, &in, cutoff);
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        if (b->active >> lane & 1u) voice[lane]->filter_cutoff_effective_hz = cutoff[lane];
    float filtered[MA_LANES];
    ml_kernel(b, &in, filtered);
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        out[lane] = b->active >> lane & 1u
                  ? voice_back_end(voice[lane], &front[lane], filtered[lane]) : 0.0f;
}
#undef ML_PICK_U64
#undef ML_PICK_PS
#undef ML_LANE
#undef ML_TARGET
#undef ML_F
#else
bool ma_lanes_supported(void) { return false; }
void ma_lanes_begin(ma_voice_lanes *b) { b->active = 0; }
void ma_lanes_gather(ma_voice_lanes *b, unsigned lane, const ma_synth *s) {
    (void)b; (void)lane; (void)s;
}
void ma_lanes_scatter(const ma_voice_lanes *b, unsigned lane, ma_synth *s) {
    (void)b; (void)lane; (void)s;
}
void ma_lanes_tick(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                   const float detune_cents[MA_LANES], float out[MA_LANES]) {
    (void)b; (void)voice; (void)detune_cents;
    for (unsigned lane = 0; lane < MA_LANES; lane++) out[lane] = 0.0f;
}
#endif

ma_frame ma_synth_tick(ma_synth *s) {
    return synth_tick(s, false, 0.0f);
}

float ma_voice_tick_raw(ma_synth *s) {
    return synth_tick(s, true, 0.0f).left;
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
    s->mozaik_slope_q32 = mozaik_slope_q32(slope);
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

/* Pool helpers leave the legacy synth/card execution order unchanged. */
float ma_voice_tick_detuned(ma_synth *s, float cents) {
    return synth_tick(s, true, cents).left;
}
void ma_voice_tick_controls(ma_synth *s) {
    (void)next_render_controls(s);
}
void ma_voice_snap_controls(ma_synth *s) {
    initialize_control_smoothers(s);
}
void ma_voice_patch_controls(ma_synth *s, const ma_patch *patch) {
    ma_patch p = sanitize_patch(patch, s->sample_rate_hz);
    ma_synth_set_vco1(s, p.vco1);
    ma_synth_set_vco2(s, p.vco2, p.vco2_level, p.vco2_interval, p.vco2_fine_cents);
    ma_synth_set_oscillator_modulation(s, p.sync_amount, p.sync_softness,
                                      p.crossmod_amount, p.noise_level);
    ma_synth_set_raster(s, p.raster_mix, p.raster_position, p.raster_warp);
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
