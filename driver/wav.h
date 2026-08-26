/* Minimal RIFF writer, float32 PCM. Hosted code (driver layer). */
#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    FILE *file;
    char *path;
    size_t expected_frames;
    size_t written_frames;
    unsigned channels;
    bool failed;
} wav_f32_writer;

/* Zero-initialize before open. Opens a float32 RIFF file whose final size is
 * known in advance. */
int wav_f32_open(wav_f32_writer *writer, const char *path, size_t frames,
                 unsigned rate_hz, unsigned channels);
int wav_f32_write(wav_f32_writer *writer, const float *samples, size_t frames);
/* Close succeeds only after exactly the declared number of frames. On any
 * failure, the incomplete pathname is removed. */
int wav_f32_close(wav_f32_writer *writer);
void wav_f32_abort(wav_f32_writer *writer);

/* Interleaved f32 frames; returns 0 on success, -1 on invalid input or an
 * I/O failure. A null sample pointer is valid only for zero frames. */
int wav_write_f32(const char *path, const float *samples, size_t frames,
                  unsigned rate_hz, unsigned channels);

#endif
