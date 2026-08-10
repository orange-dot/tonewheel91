#include "live_io.h"

#include <alloca.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "host_parse.h"

static int pcm_error(const char *where, int error) {
    fprintf(stderr, "%s: %s\n", where, snd_strerror(error));
    return error;
}

bool live_pcm_buffer_layout(snd_pcm_uframes_t period, size_t *samples,
                            size_t *float_bytes, size_t *output_bytes) {
    if (!samples || !float_bytes || !output_bytes || !period
        || period > LIVE_PERIOD_MAX || (uintmax_t)period > SIZE_MAX)
        return false;
    return host_size_mul((size_t)period, 2, samples)
        && host_size_mul(*samples, sizeof(float), float_bytes)
        && host_size_mul(*samples, sizeof(int32_t), output_bytes);
}

int live_pcm_open(live_pcm *pcm, const char *device, unsigned rate,
                  snd_pcm_uframes_t requested_period, unsigned periods) {
    if (!pcm || !device || !rate || !requested_period || !periods)
        return -EINVAL;
    *pcm = (live_pcm){ 0 };

    int error = snd_pcm_open(&pcm->handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (error < 0) {
        pcm_error("pcm open", error);
        if (error == -EBUSY)
            fprintf(stderr, "hint: another client holds the device"
                            " (PipeWire?); release the card's node first\n");
        return error;
    }

    snd_pcm_hw_params_t *hardware;
    snd_pcm_hw_params_alloca(&hardware);
    if ((error = snd_pcm_hw_params_any(pcm->handle, hardware)) < 0
        || (error = snd_pcm_hw_params_set_access(
                pcm->handle, hardware, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0
        || (error = snd_pcm_hw_params_set_format(
                pcm->handle, hardware, SND_PCM_FORMAT_S32_LE)) < 0
        || (error = snd_pcm_hw_params_set_channels(pcm->handle, hardware, 2)) < 0
        || (error = snd_pcm_hw_params_set_rate(pcm->handle, hardware, rate, 0)) < 0) {
        pcm_error("pcm hardware parameters", error);
        goto failure;
    }

    int direction = 0;
    snd_pcm_uframes_t period = requested_period;
    if ((error = snd_pcm_hw_params_set_period_size_near(
             pcm->handle, hardware, &period, &direction)) < 0) {
        pcm_error("pcm period", error);
        goto failure;
    }
    if (period > (snd_pcm_uframes_t)-1 / periods) {
        error = -EOVERFLOW;
        pcm_error("pcm buffer size", error);
        goto failure;
    }
    snd_pcm_uframes_t buffer = period * periods;
    if ((error = snd_pcm_hw_params_set_buffer_size_near(
             pcm->handle, hardware, &buffer)) < 0
        || (error = snd_pcm_hw_params(pcm->handle, hardware)) < 0
        || (error = snd_pcm_hw_params_get_period_size(
                hardware, &period, &direction)) < 0
        || (error = snd_pcm_hw_params_get_buffer_size(hardware, &buffer)) < 0) {
        pcm_error("pcm hardware commit", error);
        goto failure;
    }
    if (!period || period > LIVE_PERIOD_MAX || (uintmax_t)period > SIZE_MAX
        || !buffer || buffer < period) {
        error = -EOVERFLOW;
        fprintf(stderr, "pcm geometry period=%lu buffer=%lu is unsupported\n",
                (unsigned long)period, (unsigned long)buffer);
        goto failure;
    }

    size_t samples = 0, float_bytes = 0, output_bytes = 0;
    if (!live_pcm_buffer_layout(period, &samples, &float_bytes, &output_bytes)) {
        error = -EOVERFLOW;
        goto failure;
    }
    pcm->stereo = malloc(float_bytes);
    pcm->output = malloc(output_bytes);
    if (!pcm->stereo || !pcm->output) {
        error = -ENOMEM;
        goto failure;
    }

    snd_pcm_sw_params_t *software;
    snd_pcm_sw_params_alloca(&software);
    if ((error = snd_pcm_sw_params_current(pcm->handle, software)) < 0
        || (error = snd_pcm_sw_params_set_start_threshold(
                pcm->handle, software, buffer)) < 0
        || (error = snd_pcm_sw_params_set_avail_min(
                pcm->handle, software, period)) < 0
        || (error = snd_pcm_sw_params(pcm->handle, software)) < 0) {
        pcm_error("pcm software parameters", error);
        goto failure;
    }

    pcm->period = period;
    pcm->buffer = buffer;
    pcm->rate = rate;
    printf("pcm: %s, S32_LE stereo %u Hz, period %lu, buffer %lu (%.1f ms)\n",
           device, rate, (unsigned long)period, (unsigned long)buffer,
           1000.0 * (double)buffer / rate);
    return 0;

failure:
    live_pcm_close(pcm, false);
    return error;
}

int live_pcm_write(live_pcm *pcm, unsigned long *xruns) {
    size_t samples = (size_t)pcm->period * 2;
    for (size_t i = 0; i < samples; i++) {
        float value = pcm->stereo[i];
        if (!(value >= -1.0f && value <= 1.0f))
            value = value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : 0.0f;
        pcm->output[i] = (int32_t)(value * 2147483392.0f);
    }

    snd_pcm_uframes_t done = 0;
    while (done < pcm->period) {
        snd_pcm_sframes_t written = snd_pcm_writei(
            pcm->handle, pcm->output + 2 * done, pcm->period - done);
        if (written < 0) {
            if (xruns) (*xruns)++;
            int recovered = snd_pcm_recover(pcm->handle, (int)written, 1);
            if (recovered < 0) return pcm_error("pcm write", recovered);
            continue;
        }
        if (!written) return pcm_error("pcm write", -EIO);
        done += (snd_pcm_uframes_t)written;
    }
    return 0;
}

void live_pcm_close(live_pcm *pcm, bool drain) {
    if (!pcm) return;
    if (pcm->handle) {
        if (drain) snd_pcm_drain(pcm->handle);
        snd_pcm_close(pcm->handle);
    }
    free(pcm->stereo);
    free(pcm->output);
    *pcm = (live_pcm){ 0 };
}
