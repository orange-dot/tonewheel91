#ifndef MA_ARCHITECTURE_RENDER_H
#define MA_ARCHITECTURE_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "ma_architecture_score.h"

enum {
    MA_ARCH_DEFAULT_RATE = 48000,
    MA_ARCH_BLOCK_FRAMES = 4096,
    MA_ARCH_MAX_SECONDS = 960,
};

typedef struct {
    uint64_t hash;
    uint64_t frames;
    double sum_squares;
    float peak;
    unsigned notes;
    unsigned peak_voices;
    unsigned nonfinite;
    unsigned clipped;
} ma_arch_render_metrics;

typedef struct {
    ma_arch_render_metrics first_pass;
    ma_arch_render_metrics second_pass;
} ma_arch_render_result;

bool ma_arch_duration_frames(double seconds, unsigned rate_hz,
                             uint64_t *frames);
int ma_arch_render_file(const ma_arch_score *score, const char *path,
                        double seconds, unsigned rate_hz,
                        ma_arch_render_result *result, const char **reason);

#endif
