#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int wav_write_f32(const char *path, const float *samples, long count,
                  int rate_hz, int channels) {
    if (count < 0 || channels < 1 || rate_hz < 1) return -1;
    uint32_t data_bytes = (uint32_t)count * 4u;
    unsigned char h[58];
    memcpy(h + 0, "RIFF", 4);
    put_u32(h + 4, 48 + data_bytes); /* WAVE + fmt(24) + fact(12) + data hdr(8) */
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    put_u32(h + 16, 16);
    put_u16(h + 20, 3); /* IEEE float */
    put_u16(h + 22, (uint16_t)channels);
    put_u32(h + 24, (uint32_t)rate_hz);
    put_u32(h + 28, (uint32_t)rate_hz * (uint32_t)channels * 4u);
    put_u16(h + 32, (uint16_t)(channels * 4));
    put_u16(h + 34, 32);
    memcpy(h + 36, "fact", 4);
    put_u32(h + 40, 4);
    put_u32(h + 44, (uint32_t)(count / channels));
    memcpy(h + 48, "data", 4);
    put_u32(h + 52, data_bytes);
    /* h has 56 meaningful bytes; the array is padded for alignment. */

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(h, 1, 56, f) == 56
          && fwrite(samples, 4, (size_t)count, f) == (size_t)count;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}
