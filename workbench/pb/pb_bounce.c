#define _POSIX_C_SOURCE 200809L
#include "pb_bounce.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../driver/ma_patch_file.h"
#include "../../driver/wav.h"
#include "../desk/wb_hash.h"

uint64_t pb_sequence_frames(const pb_sequence *s, unsigned rate, unsigned loops) {
    pb_seq_run r;
    pb_seq_start(&r, &s->channel[0], rate);
    return pb_seq_step_frame(&r, (uint64_t)pb_sequence_steps(s) * loops);
}

bool pb_bounce_render(unsigned rate, const ma_patch patch[PB_CHANNELS], const ma_rack_spec *rack,
                      const pb_voice voice[PB_CHANNELS], const pb_sequence *sequence, unsigned loops, float tail_s,
                      float **pcm, size_t *frames) {
    enum { PERIOD = 128 };
    if (!pb_sequence_valid(sequence)) return false;
    uint64_t play = pb_sequence_frames(sequence, rate, loops);
    uint64_t tail = (uint64_t)(tail_s * (float)rate);
    size_t total = (size_t)((play + tail + PERIOD - 1) / PERIOD * PERIOD);
    float *out = malloc(total * 2 * sizeof *out);
    pb_engine *e = out ? pb_engine_create(rate, patch, voice, sequence) : nullptr;
    bool ok = e != nullptr;
    for (unsigned c = 0; ok && rack && c < PB_CHANNELS; c++) ok = pb_engine_set_rack_now(e, c, &rack[c]);
    if (!ok) {
        pb_engine_destroy(e);
        free(out);
        return false;
    }
    pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_PLAY });
    bool stopped = false;
    for (size_t at = 0; at < total; at += PERIOD) {
        if (!stopped && at >= play) {
            pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_STOP });
            stopped = true;
        }
        pb_rt_process(e, out + 2 * at, PERIOD);
    }
    pb_engine_destroy(e);
    *pcm = out;
    *frames = total;
    return true;
}

static bool write_patch(FILE *f, const void *data) { return ma_patch_write(f, data); }
static bool write_bench(FILE *f, const void *data) { return pb_bench_write(f, data); }

/* SHA-256 of what a writer produces. */
static bool text_sha(bool (*write)(FILE *, const void *), const void *data, char hex[WB_SHA256_HEX + 1]) {
    char *buffer = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&buffer, &size);
    if (!f) return false;
    bool ok = write(f, data);
    if (fclose(f)) ok = false;
    if (ok) wb_sha256_bytes_hex(buffer, size, hex);
    free(buffer);
    return ok;
}

bool pb_bounce_write(const char *wav_path, unsigned rate, const ma_patch patch[PB_CHANNELS],
                     const ma_rack_spec *rack, const pb_bench *bench, const float *pcm, size_t frames,
                     char *message, size_t size) {
    if (wav_write_f32(wav_path, pcm, frames, rate, 2)) {
        snprintf(message, size, "cannot write %s", wav_path);
        return false;
    }
    char patch_hex[PB_CHANNELS][WB_SHA256_HEX + 1], bench_hex[WB_SHA256_HEX + 1];
    bool hashed = text_sha(write_bench, bench, bench_hex);
    for (unsigned c = 0; hashed && c < PB_CHANNELS; c++) {
        ma_patch_document doc = { .name = "bounce", .value = patch[c] };
        if (rack) doc.rack = rack[c];
        hashed = text_sha(write_patch, &doc, patch_hex[c]);
    }
    if (!hashed) {
        snprintf(message, size, "cannot hash the patches or the bench");
        return false;
    }
    uint64_t fnv = tw_fnv1a64(pcm, frames * 2 * sizeof *pcm, 0);
    char manifest[PB_PATH_MAX + 16];
    snprintf(manifest, sizeof manifest, "%s.manifest", wav_path);
    FILE *f = fopen(manifest, "w");
    if (!f) {
        snprintf(message, size, "cannot write %s", manifest);
        return false;
    }
    fprintf(f, "patchbench_bounce=2\nwav=%s\nrate=%u\nframes=%zu\n", wav_path, rate, frames);
    for (unsigned c = 0; c < PB_CHANNELS; c++) fprintf(f, "channel%u_patch_sha256=%s\n", c + 1, patch_hex[c]);
    fprintf(f, "bench_sha256=%s\npcm_fnv64=%016llx\n", bench_hex, (unsigned long long)fnv);
    bool ok = !ferror(f);
    if (fclose(f)) ok = false;
    snprintf(message, size, "%s: %zu frames, PCM FNV-64 %016llx", wav_path, frames, (unsigned long long)fnv);
    return ok;
}
