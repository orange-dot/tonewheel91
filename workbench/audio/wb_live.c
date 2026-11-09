/* Live engine setup and the ALSA thread.  Everything that allocates, loads
 * files or talks to ALSA lives here; the per-sample work is wb_live_rt.c. */
#define _GNU_SOURCE
#include "wb_live.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "../../driver/live_io.h"
#include "../../driver/ma_patch_file.h"
#include "../desk/wb_patch.h"

wb_live *wb_live_create(unsigned rate) {
    wb_live *live = calloc(1, sizeof *live);
    if (!live) return nullptr;
    live->rate = rate;
    for (unsigned p = 0; p < WB_PARTS; p++) live->part_slot[p] = -1;
    return live;
}

void wb_live_clear_slot(wb_live *live, unsigned slot) {
    if (slot >= WB_LIVE_SLOTS) return;
    wb_live_slot *x = &live->slot[slot];
    free(x->pool);
    wb_engine_close(&x->engine);
    *x = (wb_live_slot){ 0 };
}

void wb_live_destroy(wb_live *live) {
    if (!live) return;
    wb_live_stop(live);
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) wb_live_clear_slot(live, s);
    free(live);
}

/* The part setup of the desk renderer: the same pool configuration, seed,
 * character and send effects, so a live note sounds as it renders. */
bool wb_live_load_part(wb_live *live, unsigned slot, const wb_session *s, const char *dir, unsigned index,
                       wb_error *error) {
    if (slot >= WB_LIVE_SLOTS || index >= s->part_count || wb_live_running(live)) {
        if (error) snprintf(error->message, sizeof error->message, "cannot load a part while audio runs");
        return false;
    }
    wb_live_clear_slot(live, slot);
    const wb_part *p = &s->part[index];
    wb_live_slot *x = &live->slot[slot];
    char path[4400];
    snprintf(path, sizeof path, "%s/%s", dir, p->patch);
    x->kind = p->engine;
    x->gain = p->gain;
    x->pressure = p->pressure;
    if (p->engine == WB_PART_ORGAN) {
        wb_twreg reg;
        if (!wb_twreg_load(path, &reg, error) || !wb_engine_open_organ(&x->engine, live->rate, &reg)) return false;
    } else if (p->engine == WB_PART_EP73) {
        wb_epset set;
        if (!wb_epset_load(path, &set, error) || !wb_engine_open_ep(&x->engine, live->rate, &set)) return false;
    } else {
        ma_patch_document doc;
        if (!wb_patch_load(dir, p->patch, &doc, error)) return false;
        ma_voice_pool_config config = ma_voice_pool_default_config;
        config.limit = (uint8_t)p->limit;
        config.unison_count = (uint8_t)p->unison;
        config.detune_cents = p->detune_cents;
        config.session_seed = p->seed_from_name ? config.session_seed ^ tw_fnv1a64(p->id, strlen(p->id), 0) : p->seed;
        x->pool = malloc(sizeof *x->pool);
        if (!x->pool || !ma_voice_pool_init(x->pool, (float)live->rate, &config, &doc.value)) {
            free(x->pool);
            x->pool = nullptr;
            if (error) snprintf(error->message, sizeof error->message, "part '%s': pool init failed", p->id);
            return false;
        }
        ma_voice_pool_set_character(x->pool, p->character);
        ma_effects_set_chorus(&x->pool->effects, (ma_chorus_controls){ p->chorus > 0, p->chorus, p->chorus_depth, p->chorus_rate_hz });
        ma_effects_set_reverb(&x->pool->effects, (ma_reverb_controls){ p->reverb > 0, p->reverb, p->reverb_size, p->reverb_damping });
    }
    x->active = true;
    return true;
}

void wb_live_set_take(wb_live *live, unsigned index, const float *samples, uint64_t frames, float gain) {
    if (index >= 2 || wb_live_running(live)) return;
    live->take[index] = samples;
    live->take_frames[index] = samples ? frames : 0;
    live->take_gain[index] = gain;
    if (!samples && live->take_selected == index) live->take_selected = 0;
}

void wb_live_set_score(wb_live *live, const wb_note_event *events, size_t count, const int8_t part_slot[WB_PARTS]) {
    if (wb_live_running(live)) return;
    live->score = events;
    live->score_count = events ? count : 0;
    live->score_next = 0;
    for (unsigned p = 0; p < WB_PARTS; p++) live->part_slot[p] = part_slot ? part_slot[p] : -1;
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static void *audio_main(void *arg) {
    wb_live *live = arg;
    live_pcm *pcm = live->pcm;
    /* SCHED_FIFO when the host allows it, else a raised nice level. */
    struct sched_param param = { .sched_priority = 70 };
    int scheduling = 2;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)) {
        if (live->raise_priority && live->raise_priority()) scheduling = 3;
        else scheduling = setpriority(PRIO_PROCESS, 0, -10) == 0 ? 1 : 0;
    }
    atomic_store(&live->scheduling, scheduling);
    unsigned long xruns = 0;
    float period_ms = (float)(1000.0 * (double)pcm->period / live->rate);
    while (atomic_load_explicit(&live->running, memory_order_acquire)) {
        double t0 = now_ms();
        wb_live_process(live, pcm->stereo, pcm->period);
        double t1 = now_ms();
        wb_live_publish(live, (float)(t1 - t0), period_ms, xruns);
        if (live_pcm_write(pcm, &xruns) < 0) break;
    }
    atomic_store_explicit(&live->running, false, memory_order_release);
    return nullptr;
}

bool wb_live_start(wb_live *live, const char *device, unsigned period, unsigned periods) {
    if (wb_live_running(live)) return true;
    live_pcm *pcm = calloc(1, sizeof *pcm);
    pthread_t *thread = calloc(1, sizeof *thread);
    if (!pcm || !thread) {
        free(pcm);
        free(thread);
        snprintf(live->error, sizeof live->error, "out of memory");
        return false;
    }
    snprintf(live->device, sizeof live->device, "%s", device);
    if (live_pcm_open(pcm, device, live->rate, period, periods) < 0) {
        snprintf(live->error, sizeof live->error, "cannot open ALSA device '%s'", device);
        free(pcm);
        free(thread);
        return false;
    }
    live->pcm = pcm;
    live->period = (unsigned)pcm->period;
    live->thread = thread;
    atomic_store(&live->running, true);
    if (pthread_create(thread, nullptr, audio_main, live)) {
        atomic_store(&live->running, false);
        live_pcm_close(pcm, false);
        free(pcm);
        free(thread);
        live->pcm = live->thread = nullptr;
        snprintf(live->error, sizeof live->error, "cannot start the audio thread");
        return false;
    }
    return true;
}

void wb_live_stop(wb_live *live) {
    if (!live->thread) return;
    atomic_store(&live->running, false);
    pthread_join(*(pthread_t *)live->thread, nullptr);
    live_pcm_close(live->pcm, false);
    free(live->pcm);
    free(live->thread);
    live->pcm = live->thread = nullptr;
}

bool wb_live_running(const wb_live *live) { return live->thread != nullptr; }
