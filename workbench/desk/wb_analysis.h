/* Analysis of rendered audio (docs/workbench-plan.md §7) and the listening
 * copy: a constant-gain copy of the mix whose peak sits just below -3 dBFS,
 * computed as driver/prepare_hurt_listening.py does.  No compression, no
 * gain riding. */
#ifndef WB_ANALYSIS_H
#define WB_ANALYSIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wb_session.h"

/* An interleaved stereo f32 WAV in memory. */
typedef struct {
    float *sample;
    uint64_t frames;
    unsigned rate, channels;
} wb_audio;

bool wb_audio_load(const char *path, wb_audio *audio, wb_error *error);
void wb_audio_free(wb_audio *audio);

typedef struct {
    uint64_t frames;
    double peak, rms, mono_rms, correlation;   /* linear */
    double peak_dbfs, rms_dbfs, crest_db, mono_loss_db;
    double last_100ms_peak;
} wb_measure;

void wb_measure_audio(const float *sample, uint64_t frames, unsigned rate, wb_measure *m);

/* Write <out> as mix * g with g = f32(target / peak), target the largest f32
 * below 10^(-3/20).  Returns the gain and the SHA-256 of the file. */
bool wb_listening_copy(const wb_audio *mix, const char *out, double *gain, char sha256[65],
                       wb_error *error);

/* RMS-matched constant gains for an A/B of two takes (the --reference
 * method): both play at the lower of the two listening-copy RMS levels. */
bool wb_ab_gains(const wb_audio *a, const wb_audio *b, double *gain_a, double *gain_b);

/* Short-time RMS in dBFS over windows of `window` frames. */
size_t wb_loudness_lane(const wb_audio *audio, uint64_t window, float *out, size_t max);

#endif
