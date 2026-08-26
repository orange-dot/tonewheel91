#include "wav.h"

#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static bool wav_layout(size_t frames, unsigned rate_hz, unsigned channels,
                       size_t *sample_count, uint32_t *data_bytes) {
    if (!rate_hz || !channels
        || channels > UINT16_MAX || channels > UINT16_MAX / 4u
        || frames > UINT32_MAX)
        return false;
    if (rate_hz > UINT32_MAX / channels / 4u
        || frames > SIZE_MAX / channels)
        return false;
    *sample_count = frames * channels;
    if (*sample_count > (UINT32_MAX - 48u) / 4u) return false;
    *data_bytes = (uint32_t)(*sample_count * 4u);
    return true;
}

static void wav_writer_clear(wav_f32_writer *writer) {
    *writer = (wav_f32_writer){ 0 };
}

void wav_f32_abort(wav_f32_writer *writer) {
    if (!writer) return;
    if (writer->file) (void)fclose(writer->file);
    if (writer->path) (void)remove(writer->path);
    free(writer->path);
    wav_writer_clear(writer);
}

int wav_f32_open(wav_f32_writer *writer, const char *path, size_t frames,
                 unsigned rate_hz, unsigned channels) {
    size_t sample_count = 0;
    uint32_t data_bytes = 0;
    if (!writer || !path || !*path
        || !wav_layout(frames, rate_hz, channels, &sample_count, &data_bytes))
        return -1;
    (void)sample_count;
    wav_writer_clear(writer);
    unsigned char h[56];
    memcpy(h + 0, "RIFF", 4);
    put_u32(h + 4, 48 + data_bytes);
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
    size_t path_size = strlen(path) + 1u;
    writer->path = malloc(path_size);
    if (!writer->path) return -1;
    memcpy(writer->path, path, path_size);
    writer->file = fopen(path, "wb");
    writer->expected_frames = frames;
    writer->channels = channels;
    if (!writer->file) {
        free(writer->path);
        wav_writer_clear(writer);
        return -1;
    }
    if (fwrite(h, 1, sizeof h, writer->file) != sizeof h) {
        wav_f32_abort(writer);
        return -1;
    }
    return 0;
}

int wav_f32_write(wav_f32_writer *writer, const float *samples, size_t frames) {
    if (!writer || !writer->file || (!samples && frames) || writer->failed
        || frames > writer->expected_frames - writer->written_frames) {
        if (writer && writer->file) writer->failed = true;
        return -1;
    }
    if (frames > SIZE_MAX / writer->channels) {
        writer->failed = true;
        return -1;
    }
    size_t count = frames * writer->channels;
    if (count && fwrite(samples, sizeof *samples, count, writer->file) != count) {
        writer->failed = true;
        return -1;
    }
    writer->written_frames += frames;
    return 0;
}

int wav_f32_close(wav_f32_writer *writer) {
    if (!writer || !writer->file) return -1;
    bool complete = !writer->failed
                 && writer->written_frames == writer->expected_frames;
    bool closed = fclose(writer->file) == 0;
    writer->file = 0;
    if (!complete || !closed) {
        if (writer->path) (void)remove(writer->path);
        free(writer->path);
        wav_writer_clear(writer);
        return -1;
    }
    free(writer->path);
    wav_writer_clear(writer);
    return 0;
}

int wav_write_f32(const char *path, const float *samples, size_t frames,
                  unsigned rate_hz, unsigned channels) {
    if (!path || (!samples && frames)) return -1;
    wav_f32_writer writer = { 0 };
    if (wav_f32_open(&writer, path, frames, rate_hz, channels) < 0) return -1;
    if (wav_f32_write(&writer, samples, frames) < 0
        || wav_f32_close(&writer) < 0) {
        wav_f32_abort(&writer);
        return -1;
    }
    return 0;
}
