/* Minimal RIFF writer, float32 PCM. Hosted code (driver layer). */
#ifndef WAV_H
#define WAV_H

/* Interleaved f32 frames; returns 0 on success, -1 on I/O failure. */
int wav_write_f32(const char *path, const float *samples, long count,
                  int rate_hz, int channels);

#endif
