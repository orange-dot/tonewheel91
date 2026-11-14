/* Radix-2 FFT, Hann window and dB, for the spectrum and the probe's
 * harmonic readouts (docs/patchbench-plan.md §2.7). */
#ifndef PB_FFT_H
#define PB_FFT_H

#include <stdbool.h>
#include <stddef.h>

enum { PB_FFT_MAX = 8192 };

/* In-place complex FFT of n (a power of two ≤ PB_FFT_MAX) points. */
bool pb_fft(float *re, float *im, size_t n);
/* Hann-windowed magnitude spectrum of n real samples in dBFS (a full-scale
 * sine reads 0 dB): db[0 .. n/2]. */
bool pb_spectrum_db(const float *x, size_t n, float *db);
static inline float pb_bin_hz(size_t bin, size_t n, float rate) { return (float)bin * rate / (float)n; }

/* Classify a spectrum against a harmonic grid at n·f0: the level of the
 * first `count` harmonics (peak within ±2 bins) and the strongest bin
 * farther than 3 bins from every harmonic. */
typedef struct {
    float harmonic_db[32];
    unsigned count;
    float inharmonic_db, inharmonic_hz;
} pb_harmonics;
void pb_classify(const float *db, size_t n, float rate, float f0, unsigned count, pb_harmonics *out);

#endif
