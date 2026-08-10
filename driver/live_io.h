#ifndef LIVE_IO_H
#define LIVE_IO_H

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { LIVE_PERIOD_MAX = 1048576 };

typedef struct {
    snd_pcm_t *handle;
    float *stereo;
    int32_t *output;
    snd_pcm_uframes_t period, buffer;
    unsigned rate;
} live_pcm;

int live_pcm_open(live_pcm *pcm, const char *device, unsigned rate,
                  snd_pcm_uframes_t requested_period, unsigned periods);
bool live_pcm_buffer_layout(snd_pcm_uframes_t period, size_t *samples,
                            size_t *float_bytes, size_t *output_bytes);
int live_pcm_write(live_pcm *pcm, unsigned long *xruns);
void live_pcm_close(live_pcm *pcm, bool drain);

#endif
