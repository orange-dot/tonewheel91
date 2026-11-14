#include "pb_fft.h"

#include <math.h>

static constexpr double PI = 3.14159265358979323846;

bool pb_fft(float *re, float *im, size_t n) {
    if (n < 2 || n > PB_FFT_MAX || (n & (n - 1))) return false;
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * PI / (double)len;
        for (size_t k = 0; k < len / 2; k++) {
            float wr = (float)cos(angle * (double)k), wi = (float)sin(angle * (double)k);
            for (size_t i = k; i < n; i += len) {
                size_t j = i + len / 2;
                float xr = re[j] * wr - im[j] * wi, xi = re[j] * wi + im[j] * wr;
                re[j] = re[i] - xr;
                im[j] = im[i] - xi;
                re[i] += xr;
                im[i] += xi;
            }
        }
    }
    return true;
}

bool pb_spectrum_db(const float *x, size_t n, float *db) {
    static thread_local float re[PB_FFT_MAX], im[PB_FFT_MAX];
    if (n < 2 || n > PB_FFT_MAX || (n & (n - 1))) return false;
    for (size_t i = 0; i < n; i++) {
        float w = .5f - .5f * (float)cos(2.0 * PI * (double)i / (double)n);
        re[i] = x[i] * w;
        im[i] = 0;
    }
    pb_fft(re, im, n);
    /* Hann's coherent gain is 1/2; a real sine splits over ± bins. */
    float scale = 4.0f / (float)n;
    for (size_t i = 0; i <= n / 2; i++) {
        float m = sqrtf(re[i] * re[i] + im[i] * im[i]) * scale;
        db[i] = m > 1e-9f ? 20.0f * log10f(m) : -180.0f;
    }
    return true;
}

void pb_classify(const float *db, size_t n, float rate, float f0, unsigned count, pb_harmonics *out) {
    *out = (pb_harmonics){ .inharmonic_db = -180 };
    if (count > 32) count = 32;
    float bin_hz = rate / (float)n;
    size_t bins = n / 2 + 1;
    for (unsigned h = 0; h < count; h++) {
        float at = (float)(h + 1) * f0 / bin_hz;
        out->harmonic_db[h] = -180;
        if (at >= (float)bins - 1) continue;
        out->count = h + 1;
        long c = lroundf(at);
        for (long b = c - 2; b <= c + 2; b++)
            if (b >= 0 && (size_t)b < bins && db[b] > out->harmonic_db[h]) out->harmonic_db[h] = db[b];
    }
    float spacing = f0 / bin_hz;
    for (size_t b = 3; b < bins; b++) {
        float near = roundf((float)b / spacing) * spacing;
        if (fabsf((float)b - near) <= 3.0f || (spacing < 7.0f)) continue;
        if (db[b] > out->inharmonic_db) {
            out->inharmonic_db = db[b];
            out->inharmonic_hz = (float)b * bin_hz;
        }
    }
}
