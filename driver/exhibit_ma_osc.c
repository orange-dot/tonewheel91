/* Hosted MA1-2 alias referee. The core remains freestanding; this program
 * supplies libm, a Blackman-Harris window and an FFT around its public API. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/mamutanalog.h"

enum { FFT_N = 65536, WARMUP = 4096, MASK_RADIUS = 10 };

static constexpr double PI = 3.14159265358979323846264338327950288;
typedef struct {
    double real;
    double imag;
} complex_sample;

typedef struct {
    double alias;
    double total;
} spectral_energy;

typedef enum {
    CASE_ORDINARY,
    CASE_SYNC,
    CASE_CROSSMOD,
} case_kind;

typedef enum {
    SHAPE_SAW,
    SHAPE_PULSE,
    SHAPE_SINE,
} shape_kind;

typedef struct {
    const char *name;
    case_kind kind;
    shape_kind shape;
    uint8_t note;
    int interval;
    float pulse_width;
    float amount;
    float sample_rate_hz;
} alias_case;

static float bounded(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static float wrap(float phase) {
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static float raw_wave(shape_kind shape, float phase, float width) {
    if (shape == SHAPE_SINE) return (float)sin(2.0 * PI * phase);
    if (shape == SHAPE_PULSE) return phase < width ? 1.0f : -1.0f;
    return 2.0f * phase - 1.0f;
}

static float interval_ratio(int interval) {
    static constexpr float semitone[12] = {
        1.000000000e+00f, 1.059463143e+00f, 1.122462034e+00f,
        1.189207077e+00f, 1.259921074e+00f, 1.334839821e+00f,
        1.414213538e+00f, 1.498307109e+00f, 1.587401032e+00f,
        1.681792855e+00f, 1.781797409e+00f, 1.887748599e+00f,
    };
    int octave = interval / 12;
    int remainder = interval % 12;
    if (remainder < 0) {
        remainder += 12;
        octave--;
    }
    float ratio = semitone[remainder];
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

static ma_vco_controls controls(shape_kind shape, float pulse_width) {
    return (ma_vco_controls){
        .saw_level = shape == SHAPE_SAW ? 1.0f : 0.0f,
        .pulse_level = shape == SHAPE_PULSE ? 1.0f : 0.0f,
        .pulse_width = pulse_width,
    };
}

static void configure_core(ma_synth *synth, const alias_case *test) {
    ma_synth_init(synth, test->sample_rate_hz);
    ma_synth_set_mozaik(synth, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_vco_controls silent = { .pulse_width = 0.5f };
    ma_synth_set_vco1_sine(synth, 0.0f);
    if (test->kind == CASE_SYNC) {
        ma_synth_set_vco1(synth, silent);
        ma_synth_set_vco2(synth, controls(test->shape, test->pulse_width),
                          1.0f, test->interval, 0.0f);
        ma_synth_set_oscillator_modulation(synth, test->amount, 0.0f,
                                           0.0f, 0.0f);
    } else {
        ma_synth_set_vco1(synth, controls(test->shape, test->pulse_width));
        ma_synth_set_vco1_sine(synth,
                               test->shape == SHAPE_SINE ? 1.0f : 0.0f);
        ma_synth_set_vco2(synth, controls(SHAPE_SAW, 0.5f), 0.0f,
                          test->interval, 0.0f);
        ma_synth_set_oscillator_modulation(
            synth, 0.0f, 0.0f,
            test->kind == CASE_CROSSMOD ? test->amount : 0.0f, 0.0f);
    }
    ma_synth_note_on(synth, 0, test->note, 127);
}

static void render_core(float output[FFT_N], const alias_case *test) {
    ma_synth synth;
    configure_core(&synth, test);
    for (int i = 0; i < WARMUP; i++) (void)ma_synth_tick(&synth);
    for (int i = 0; i < FFT_N; i++) output[i] = ma_synth_tick(&synth).left;
}

static void render_naive(float output[FFT_N], const alias_case *test) {
    float base_hz = ma_note_frequency_hz(test->note);
    float master_step = bounded(base_hz / test->sample_rate_hz, 0.0f, 0.45f);
    float slave_step = bounded(base_hz * interval_ratio(test->interval)
                               / test->sample_rate_hz, 0.0f, 0.45f);
    float master_phase = 0.0f, slave_phase = 0.0f;
    for (int frame = -WARMUP; frame < FFT_N; frame++) {
        float sample;
        float effective_master_step = master_step;
        if (test->kind == CASE_CROSSMOD) {
            float modulator = raw_wave(SHAPE_SAW, slave_phase, 0.5f);
            float ratio = bounded(1.0f + modulator * test->amount * 0.25f,
                                  0.25f, 4.0f);
            effective_master_step = bounded(base_hz * ratio
                                            / test->sample_rate_hz,
                                            0.0f, 0.45f);
        }

        if (test->kind == CASE_SYNC)
            sample = 0.5f * raw_wave(test->shape, slave_phase,
                                     test->pulse_width);
        else
            sample = raw_wave(test->shape, master_phase, test->pulse_width);
        if (frame >= 0) output[frame] = sample;

        bool wrapped = master_phase + effective_master_step >= 1.0f;
        if (test->kind == CASE_SYNC && wrapped && test->amount > 0.0f) {
            float fraction = (1.0f - master_phase) / effective_master_step;
            fraction = bounded(fraction, 0.0f, 1.0f);
            float event_phase = wrap(slave_phase + slave_step * fraction);
            float reset_phase = event_phase * (1.0f - test->amount);
            slave_phase = wrap(reset_phase + slave_step * (1.0f - fraction));
        } else {
            slave_phase = wrap(slave_phase + slave_step);
        }
        master_phase = wrap(master_phase + effective_master_step);
    }
}

static void fft(complex_sample value[FFT_N]) {
    for (unsigned i = 1, j = 0; i < FFT_N; i++) {
        unsigned bit = FFT_N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            complex_sample temporary = value[i];
            value[i] = value[j];
            value[j] = temporary;
        }
    }
    for (unsigned width = 2; width <= FFT_N; width <<= 1) {
        double angle = -2.0 * PI / (double)width;
        double step_real = cos(angle), step_imag = sin(angle);
        for (unsigned start = 0; start < FFT_N; start += width) {
            double weight_real = 1.0, weight_imag = 0.0;
            for (unsigned offset = 0; offset < width / 2; offset++) {
                complex_sample even = value[start + offset];
                complex_sample odd = value[start + offset + width / 2];
                double odd_real = odd.real * weight_real
                                - odd.imag * weight_imag;
                double odd_imag = odd.real * weight_imag
                                + odd.imag * weight_real;
                value[start + offset] = (complex_sample){
                    .real = even.real + odd_real,
                    .imag = even.imag + odd_imag,
                };
                value[start + offset + width / 2] = (complex_sample){
                    .real = even.real - odd_real,
                    .imag = even.imag - odd_imag,
                };
                double next_real = weight_real * step_real
                                 - weight_imag * step_imag;
                weight_imag = weight_real * step_imag
                            + weight_imag * step_real;
                weight_real = next_real;
            }
        }
    }
}

static spectral_energy measure_energy(const float input[FFT_N],
                                      double fundamental_hz,
                                      double sample_rate_hz,
                                      bool fundamental_only) {
    static complex_sample spectrum[FFT_N];
    static bool legal[FFT_N / 2 + 1];
    memset(legal, 0, sizeof legal);
    for (int i = 0; i < FFT_N; i++) {
        double phase = 2.0 * PI * i / (FFT_N - 1.0);
        double window = 0.35875 - 0.48829 * cos(phase)
                      + 0.14128 * cos(2.0 * phase)
                      - 0.01168 * cos(3.0 * phase);
        spectrum[i] = (complex_sample){ .real = input[i] * window };
    }
    fft(spectrum);

    for (int bin = 0; bin <= MASK_RADIUS; bin++) legal[bin] = true;
    for (int harmonic = 1;
         harmonic * fundamental_hz < 0.5 * sample_rate_hz
         && (!fundamental_only || harmonic == 1);
         harmonic++) {
        int center = (int)llround(harmonic * fundamental_hz * FFT_N
                                 / sample_rate_hz);
        int low = center - MASK_RADIUS;
        int high = center + MASK_RADIUS;
        if (low < 0) low = 0;
        if (high > FFT_N / 2) high = FFT_N / 2;
        for (int bin = low; bin <= high; bin++) legal[bin] = true;
    }

    spectral_energy energy = { 0 };
    for (int bin = 1; bin < FFT_N / 2; bin++) {
        double power = spectrum[bin].real * spectrum[bin].real
                     + spectrum[bin].imag * spectrum[bin].imag;
        energy.total += power;
        if (!legal[bin]) energy.alias += power;
    }
    return energy;
}

int main(void) {
    static const alias_case cases[] = {
        { "ordinary-saw-n96", CASE_ORDINARY, SHAPE_SAW,
          96, 0, .50f, 0.0f, 48000.0f },
        { "ordinary-saw-n120", CASE_ORDINARY, SHAPE_SAW,
          120, 0, .50f, 0.0f, 48000.0f },
        { "ordinary-pulse-n96", CASE_ORDINARY, SHAPE_PULSE,
          96, 0, .37f, 0.0f, 48000.0f },
        { "ordinary-pulse-n120", CASE_ORDINARY, SHAPE_PULSE,
          120, 0, .37f, 0.0f, 48000.0f },
        { "hard-sync-saw-n96", CASE_SYNC, SHAPE_SAW,
          96, 7, .50f, 1.0f, 48000.0f },
        { "hard-sync-pulse-n96", CASE_SYNC, SHAPE_PULSE,
          96, 7, .37f, 1.0f, 48000.0f },
        { "crossmod-low-n96", CASE_CROSSMOD, SHAPE_SAW,
          96, 12, .50f, .35f, 48000.0f },
        { "crossmod-high-n96", CASE_CROSSMOD, SHAPE_SAW,
          96, 12, .50f, 1.0f, 48000.0f },
        { "vco1-sine-44k1", CASE_ORDINARY, SHAPE_SINE,
          96, 0, .50f, 0.0f, 44100.0f },
        { "vco1-sine-48k", CASE_ORDINARY, SHAPE_SINE,
          96, 0, .50f, 0.0f, 48000.0f },
        { "vco1-sine-96k", CASE_ORDINARY, SHAPE_SINE,
          96, 0, .50f, 0.0f, 96000.0f },
        { "vco1-sine-192k", CASE_ORDINARY, SHAPE_SINE,
          96, 0, .50f, 0.0f, 192000.0f },
    };
    static float clean[FFT_N], naive[FFT_N];
    int failures = 0;

    puts("MA1-2 alias energy outside legal harmonic masks");
    puts("case                         clean dBc  naive dBc  reduction  verdict");
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        render_core(clean, &cases[i]);
        render_naive(naive, &cases[i]);
        spectral_energy clean_energy = measure_energy(
            clean, ma_note_frequency_hz(cases[i].note),
            cases[i].sample_rate_hz, cases[i].shape == SHAPE_SINE);
        spectral_energy naive_energy = measure_energy(
            naive, ma_note_frequency_hz(cases[i].note),
            cases[i].sample_rate_hz, cases[i].shape == SHAPE_SINE);
        double clean_dbc = 10.0 * log10(clean_energy.alias
                                        / clean_energy.total);
        double naive_dbc = 10.0 * log10(naive_energy.alias
                                        / naive_energy.total);
        double reduction_db = 10.0 * log10(clean_energy.alias
                                           / naive_energy.alias);
        bool pass = cases[i].shape == SHAPE_SINE
                  ? clean_dbc <= -80.0 : reduction_db <= -20.0;
        printf("%-28s %8.2f %9.2f %9.2f dB  %s\n", cases[i].name,
               clean_dbc, naive_dbc, reduction_db, pass ? "PASS" : "FAIL");
        failures += !pass;
    }
    return failures ? 1 : 0;
}
