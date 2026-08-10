/* Minimal RIFF writer, float32 PCM. Hosted code (driver layer). */
#ifndef WAV_H
#define WAV_H

#include <stddef.h>

/* Interleaved f32 frames; returns 0 on success, -1 on invalid input or an
 * I/O failure. A null sample pointer is valid only for zero frames. */
int wav_write_f32(const char *path, const float *samples, size_t frames,
                  unsigned rate_hz, unsigned channels);

#endif
