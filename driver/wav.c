#include "wav.h"

#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static_assert(CHAR_BIT == 8);
static_assert(sizeof(float) == 4);
static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24);
static_assert(FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128);

static void put_u16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)(v >> 8);
}

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)(v >> 8 & 0xff);
    p[2] = (unsigned char)(v >> 16 & 0xff);
    p[3] = (unsigned char)(v >> 24 & 0xff);
}

int wav_write_f32(const char *path, const float *samples, size_t frames,
                  unsigned rate_hz, unsigned channels) {
    if (!path || (!samples && frames) || !rate_hz || !channels
        || channels > UINT16_MAX || channels > UINT16_MAX / 4u
        || frames > UINT32_MAX)
        return -1;
    if (rate_hz > UINT32_MAX / channels / 4u) return -1;
    if (frames > SIZE_MAX / channels) return -1;
    size_t sample_count = frames * channels;
    if (sample_count > (UINT32_MAX - 48u) / 4u)
        return -1;
    uint32_t data_bytes = (uint32_t)(sample_count * 4u);
    unsigned char h[56];
    memcpy(h + 0, "RIFF", 4);
    put_u32(h + 4, 48 + data_bytes); /* WAVE + fmt(24) + fact(12) + data hdr(8) */
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    put_u32(h + 16, 16);
    put_u16(h + 20, 3); /* IEEE float */
    put_u16(h + 22, (uint16_t)channels);
    put_u32(h + 24, rate_hz);
    put_u32(h + 28, rate_hz * channels * 4u);
    put_u16(h + 32, (uint16_t)(channels * 4));
    put_u16(h + 34, 32);
    memcpy(h + 36, "fact", 4);
    put_u32(h + 40, 4);
    put_u32(h + 44, (uint32_t)frames);
    memcpy(h + 48, "data", 4);
    put_u32(h + 52, data_bytes);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    if (ok && sample_count)
        ok = fwrite(samples, sizeof *samples, sample_count, f) == sample_count;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}
