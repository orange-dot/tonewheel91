/* Engine setup, mailboxes (UI side) and the ALSA thread. */
#define _GNU_SOURCE
#include "pb_engine.h"

#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "../../driver/live_io.h"
#include "pb_host.h"

enum { DIRTY = 4u };

static void triple_init(pb_triple *t) {
    t->back = 0;
    atomic_init(&t->state, 1u);
    t->front = 2;
}

/* UI side: the value in slot `back` becomes the newest. */
static void publish(pb_triple *t) {
    uint32_t old = atomic_exchange_explicit(&t->state, t->back | DIRTY, memory_order_acq_rel);
    t->back = old & 3u;
}

pb_pool *pb_pool_build(unsigned rate, const ma_patch *patch, const pb_voice *v) {
    if (!pb_voice_valid(v)) return nullptr;
    pb_pool *p = malloc(sizeof *p);
    if (!p) return nullptr;
    ma_voice_pool_config config = ma_voice_pool_default_config;
    config.limit = v->limit;
    config.unison_count = v->unison;
    config.detune_cents = v->detune_cents;
    config.session_seed = v->seed;
    if (!ma_voice_pool_init(&p->pool, (float)rate, &config, patch)) {
        free(p);
        return nullptr;
    }
    ma_voice_pool_set_character(&p->pool, v->character);
    ma_voice_pool_set_glide(&p->pool, v->glide_s > 0, v->glide_s);
    ma_voice_pool_set_lfo(&p->pool, v->lfo_depth, v->lfo_rate_hz);
    p->voice = *v;
    return p;
}

pb_engine *pb_engine_create(unsigned rate, const ma_patch patch[PB_CHANNELS], const pb_voice voice[PB_CHANNELS],
                            const pb_sequence *sequence) {
    pb_engine *e = pb_sequence_valid(sequence) ? calloc(1, sizeof *e) : nullptr;
    if (!e) return nullptr;
    e->rate = rate;
    e->options.audio_cpu = -1;
    e->options.worker_cpu = -1;
    e->sequence = *sequence;
    triple_init(&e->sequence_box);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_engine_channel *ch = &e->channel[c];
        ch->pool = pb_pool_build(rate, &patch[c], &voice[c]);
        if (!ch->pool) {
            pb_engine_destroy(e);
            return nullptr;
        }
        ch->voice = voice[c];
        ch->gain = voice[c].gain;
        triple_init(&ch->patch_box);
        triple_init(&ch->voice_box);
        triple_init(&ch->rack_box);
        ma_rack_init(&ch->rack, (float)rate);
        ma_rack_set_tempo(&ch->rack, (float)sequence->channel[0].bpm * .01f);
        ma_voice_pool_attach_rack(&ch->pool->pool, &ch->rack);
        /* The voice's chorus and reverb are in place from the first frame. */
        ma_rack_spec chain;
        (void)pb_rt_chain(&ch->rack_spec, &ch->voice, &chain);
        if (!ma_rack_spec_empty(&chain) && ma_rack_apply(&ch->rack, &chain)) ma_rack_settle(&ch->rack);
    }
    return e;
}

static void lock_pool(pb_engine *e, pb_pool *p) {
    if (e->options.lock_memory && p && !pb_lock(p, sizeof *p, &e->locked_bytes)) e->lock_failed = true;
}

static void free_pool(pb_pool *p) {
    if (!p) return;
    pb_unlock(p, sizeof *p);
    free(p);
}

void pb_engine_collect(pb_engine *e) {
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        free_pool(atomic_exchange_explicit(&e->channel[c].retired_pool, nullptr, memory_order_acq_rel));
}

void pb_engine_destroy(pb_engine *e) {
    if (!e) return;
    pb_engine_stop(e);
    pb_engine_collect(e);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        free_pool(atomic_exchange(&e->channel[c].next_pool, nullptr));
        free_pool(e->channel[c].pool);
    }
    pb_unlock(e, sizeof *e);
    free(e);
}

bool pb_engine_send(pb_engine *e, pb_event ev) {
    uint32_t head = atomic_load_explicit(&e->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&e->tail, memory_order_acquire);
    if (head - tail == PB_RING) return false;
    e->ring[head % PB_RING] = ev;
    atomic_store_explicit(&e->head, head + 1, memory_order_release);
    return true;
}

void pb_engine_set_patch(pb_engine *e, unsigned c, const ma_patch *patch) {
    pb_engine_channel *ch = &e->channel[c];
    ch->patch_slot[ch->patch_box.back] = *patch;
    publish(&ch->patch_box);
}

void pb_engine_set_rack(pb_engine *e, unsigned c, const ma_rack_spec *rack) {
    pb_engine_channel *ch = &e->channel[c];
    ch->rack_slot[ch->rack_box.back] = *rack;
    publish(&ch->rack_box);
}

bool pb_engine_set_rack_now(pb_engine *e, unsigned c, const ma_rack_spec *rack) {
    pb_engine_channel *ch = &e->channel[c];
    if (!ma_rack_spec_valid(rack)) return false;
    ch->rack_spec = *rack;
    ma_rack_spec chain;
    (void)pb_rt_chain(&ch->rack_spec, &ch->voice, &chain);
    if (!ma_rack_apply(&ch->rack, &chain)) return false;
    ma_rack_settle(&ch->rack);
    return true;
}

void pb_engine_set_sequence(pb_engine *e, const pb_sequence *sequence) {
    if (!pb_sequence_valid(sequence)) return;
    e->sequence_slot[e->sequence_box.back] = *sequence;
    publish(&e->sequence_box);
}

void pb_engine_focus(pb_engine *e, unsigned c) {
    atomic_store_explicit(&e->focus, (uint8_t)(c < PB_CHANNELS ? c : 0), memory_order_relaxed);
}

/* The UI's view of the voice the channel's newest pool was built with. */
static pb_voice published_pool_voice(pb_engine_channel *ch) {
    pb_pool *next = atomic_load_explicit(&ch->next_pool, memory_order_acquire);
    return next ? next->voice : ch->voice;
}

bool pb_engine_set_voice(pb_engine *e, unsigned c, const pb_voice *voice, const ma_patch *patch) {
    if (!pb_voice_valid(voice)) return false;
    pb_engine_channel *ch = &e->channel[c];
    pb_engine_collect(e);
    pb_voice built = published_pool_voice(ch);
    if (pb_voice_needs_pool(&built, voice)) {
        if (atomic_load(&ch->next_pool) || atomic_load(&ch->retired_pool)) return false;
        pb_pool *p = pb_pool_build(e->rate, patch, voice);
        if (!p) return false;
        lock_pool(e, p);
        atomic_store_explicit(&ch->next_pool, p, memory_order_release);
    }
    ch->voice_slot[ch->voice_box.back] = *voice;
    publish(&ch->voice_box);
    return true;
}

pb_meters pb_engine_read(pb_engine *e) {
    pb_meters copy;
    for (;;) {
        uint32_t before = atomic_load_explicit(&e->meter_sequence, memory_order_acquire);
        if (before & 1u) continue;
        atomic_thread_fence(memory_order_acquire);
        memcpy(&copy, &e->meters, sizeof copy);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&e->meter_sequence, memory_order_acquire) == before) return copy;
    }
}

uint64_t pb_engine_scope(pb_engine *e, float *dst, size_t count) {
    uint64_t end = atomic_load_explicit(&e->scope_write, memory_order_acquire);
    if (count > PB_SCOPE / 2) count = PB_SCOPE / 2;
    for (size_t i = 0; i < count; i++) {
        uint64_t at = end - count + i;
        bool valid = end >= count - i && at < end;
        dst[2 * i] = valid ? e->scope[at % PB_SCOPE][0] : 0;
        dst[2 * i + 1] = valid ? e->scope[at % PB_SCOPE][1] : 0;
    }
    return end;
}

/* ---- the voice-bank worker (live-path step 7) ----------------------------- */

static void futex_wait(_Atomic uint32_t *word, uint32_t value, long timeout_ms) {
    struct timespec t = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000 };
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAIT_PRIVATE, value, timeout_ms > 0 ? &t : nullptr, nullptr, 0);
}
static void futex_wake(_Atomic uint32_t *word) {
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}

/* The executor (on the audio thread): part 1 to the worker, part 0 here,
 * then wait. The wait spins a little first (the parts take about the same
 * time), then sleeps on the futex. */
static void worker_run(void *context, void (*job)(void *, unsigned), void *arg, unsigned parts) {
    pb_engine *e = context;
    pb_worker *w = &e->worker;
    if (parts < 2) {
        job(arg, 0);
        return;
    }
    w->job = job;
    w->arg = arg;
    uint32_t g = atomic_load_explicit(&w->go, memory_order_relaxed) + 1;
    atomic_store_explicit(&w->go, g, memory_order_release);
    futex_wake(&w->go);
    job(arg, 0);
    for (unsigned spin = 0;; spin++) {
        uint32_t d = atomic_load_explicit(&w->done, memory_order_acquire);
        if (d == g) break;
        if (spin < 4000) __builtin_ia32_pause();
        else futex_wait(&w->done, d, 5);
    }
    w->runs++;
}

static double mono_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

/* Sleep until absolute monotonic time `ms`. */
static void sleep_until_ms(double ms) {
    struct timespec t = { (time_t)(ms / 1000), (long)((ms - (double)(time_t)(ms / 1000) * 1000) * 1e6) };
    if (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; }
    (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr);
}

/* The worker waits for the next run in three stages. The runs come once per
 * period, so after one it sleeps until the next is due less the lead time,
 * then spins, so its core is awake (out of a deep C-state) when the run
 * arrives; if nothing comes within half a period more, it sleeps on the
 * futex. With no lead time it only sleeps on the futex. */
static void *worker_main(void *arg) {
    pb_engine *e = arg;
    pb_worker *w = &e->worker;
    double period_ms = e->period ? 1000.0 * e->period / e->rate : 2.667, last_go = 0;
    double lead_ms = e->options.worker_lead_us / 1000.0;
    if (e->options.worker_cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(e->options.worker_cpu, &set);
        atomic_store(&w->pinned, pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0);
    }
    struct sched_param param = { .sched_priority = 70 };
    int priority = 70;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)) {
        priority = 0;
        if (!(e->raise_priority && e->raise_priority(&priority))) priority = 0;
    }
    atomic_store(&w->priority, priority);
    /* Generation 0 is the start (worker_start set it before this thread
     * existed). Reading go here instead would lose a run published while
     * the setup above (RealtimeKit over the bus) was still going. */
    uint32_t seen = 0;
    atomic_store_explicit(&w->running, true, memory_order_release);
    while (!atomic_load_explicit(&w->quit, memory_order_acquire)) {
        uint32_t g = atomic_load_explicit(&w->go, memory_order_acquire);
        if (g == seen) {
            if (lead_ms > 0 && last_go > 0) {
                double due = last_go + period_ms;
                if (mono_ms() < due - lead_ms) sleep_until_ms(due - lead_ms);
                double give_up = due + period_ms / 2;
                while ((g = atomic_load_explicit(&w->go, memory_order_acquire)) == seen && mono_ms() < give_up
                       && !atomic_load_explicit(&w->quit, memory_order_relaxed))
                    __builtin_ia32_pause();
                if (g != seen) goto work;
                last_go = 0;   /* missed the beat: fall back to the futex until the next run */
            }
            futex_wait(&w->go, seen, 100);   /* the timeout only lets it see quit */
            continue;
        }
    work:
        last_go = mono_ms();
        seen = g;
        w->job(w->arg, 1);
        atomic_store_explicit(&w->done, g, memory_order_release);
        futex_wake(&w->done);
    }
    atomic_store(&w->running, false);
    return nullptr;
}

static bool worker_start(pb_engine *e) {
    pb_worker *w = &e->worker;
    w->thread = calloc(1, sizeof(pthread_t));
    if (!w->thread) return false;
    atomic_store(&w->quit, false);
    atomic_store(&w->go, 0);
    atomic_store(&w->done, 0);
    atomic_store(&w->running, false);
    if (pthread_create(w->thread, nullptr, worker_main, e)) {
        free(w->thread);
        w->thread = nullptr;
        return false;
    }
    /* Start the audio only once the worker is set up (pinned, real time):
     * the first runs must not wait on its setup. */
    for (int waited = 0; !atomic_load_explicit(&w->running, memory_order_acquire) && waited < 2000; waited++) {
        struct timespec t = { 0, 1000000 };
        nanosleep(&t, nullptr);
    }
    e->executor = (ma_pool_executor){ worker_run, e, 2 };
    return true;
}

static void worker_stop(pb_engine *e) {
    pb_worker *w = &e->worker;
    if (!w->thread) return;
    atomic_store_explicit(&w->quit, true, memory_order_release);
    atomic_fetch_add(&w->go, 0);
    futex_wake(&w->go);
    pthread_join(*(pthread_t *)w->thread, nullptr);
    free(w->thread);
    w->thread = nullptr;
}

/* ---- thread --------------------------------------------------------------- */

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static void record(uint32_t *hist, float *max, double ms) {
    int bin = (int)(ms * 100);
    hist[bin < 0 ? 0 : bin >= PB_HIST_BINS ? PB_HIST_BINS - 1 : bin]++;
    if (ms > *max) *max = (float)ms;
}

/* SIGXCPU: the audio thread used its real-time CPU allowance without
 * blocking (RLIMIT_RTTIME soft limit, see pb_rtkit_realtime). Leave real time
 * instead of meeting the hard limit's SIGKILL. sched_setscheduler is a plain
 * system call, safe in a handler. One engine runs audio at a time. */
static pb_engine *_Atomic overloaded_engine;

static void on_sigxcpu(int signal) {
    (void)signal;
    pb_engine *e = atomic_load(&overloaded_engine);
    if (!e) return;
    /* RealtimeKit sets SCHED_RESET_ON_FORK, which an unprivileged thread may
     * not clear: keep it, or the call fails with EPERM. */
    struct sched_param normal = { 0 };
    pid_t tid = atomic_load(&e->audio_tid);
    if (sched_setscheduler(tid, SCHED_OTHER | SCHED_RESET_ON_FORK, &normal) == 0
        || sched_setscheduler(tid, SCHED_OTHER, &normal) == 0)
        atomic_store(&e->demoted, true);
}

static void *audio_main(void *arg) {
    pb_engine *e = arg;
    live_pcm *pcm = e->pcm;
    atomic_store(&e->audio_tid, (int)gettid());
    atomic_store(&overloaded_engine, e);
    struct sigaction action = { .sa_handler = on_sigxcpu };
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGXCPU, &action, nullptr);
    if (e->options.audio_cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(e->options.audio_cpu, &set);
        atomic_store(&e->pinned, pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0);
    }
    if (e->options.lock_memory) {
        /* Touch and lock a quarter megabyte of this thread's stack now, so
         * the loop below never takes a page fault on it. */
        volatile unsigned char stack[256 * 1024];
        for (size_t i = 0; i < sizeof stack; i += 4096) stack[i] = 0;
        if (!pb_lock((const void *)stack, sizeof stack, nullptr)) e->lock_failed = true;
    }
    struct sched_param param = { .sched_priority = 70 };
    int scheduling = 2, priority = 70;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)) {
        priority = 0;
        if (e->raise_priority && e->raise_priority(&priority)) scheduling = 3;
        else scheduling = setpriority(PRIO_PROCESS, 0, -10) == 0 ? 1 : 0;
    }
    atomic_store(&e->scheduling, scheduling);
    atomic_store(&e->priority, scheduling >= 2 ? priority : 0);
    unsigned long xruns = 0;
    float period_ms = (float)(1000.0 * (double)pcm->period / e->rate);
    pb_timing *timing = &e->timing;
    *timing = (pb_timing){ .cpu = -1 };
    double last = 0;
    while (atomic_load_explicit(&e->running, memory_order_acquire)) {
        double t0 = now_ms();
        pb_rt_process(e, pcm->stereo, pcm->period);
        double t1 = now_ms();
        pb_rt_publish(e, (float)(t1 - t0), period_ms, xruns);
        record(timing->dsp, &timing->dsp_max_ms, t1 - t0);
        /* Falling behind for several periods: shed every voice now rather
         * than run the buffer dry and, at real time, hit the CPU limit. */
        e->over_budget = t1 - t0 > period_ms ? e->over_budget + 1 : 0;
        if (e->over_budget >= PB_OVERLOAD_PERIODS) {
            pb_rt_overload(e);
            e->over_budget = 0;
        }
        if (last > 0) record(timing->wake, &timing->wake_max_ms, t0 - last);
        last = t0;
        int cpu = sched_getcpu();
        if (timing->cpu >= 0 && cpu != timing->cpu) timing->migrations++;
        timing->cpu = cpu;
        timing->periods++;
        double tw = now_ms();
        if (live_pcm_write(pcm, &xruns) < 0) break;
        /* A write that did not block means the buffer is draining: the
         * thread has not slept, and real-time CPU time accumulates towards
         * RLIMIT_RTTIME. A long run of them is an overload too. */
        e->not_blocked = now_ms() - tw < .1 ? e->not_blocked + 1 : 0;
        if (e->not_blocked >= PB_NOT_BLOCKED_PERIODS) {
            pb_rt_overload(e);
            e->not_blocked = 0;
        }
    }
    atomic_store_explicit(&e->running, false, memory_order_release);
    return nullptr;
}

bool pb_engine_start(pb_engine *e, const char *device, unsigned period, unsigned periods) {
    if (pb_engine_running(e)) return true;
    live_pcm *pcm = calloc(1, sizeof *pcm);
    pthread_t *thread = calloc(1, sizeof *thread);
    if (!pcm || !thread) goto fail;
    if (live_pcm_open(pcm, device, e->rate, period, periods) < 0) {
        snprintf(e->error, sizeof e->error, "cannot open ALSA device '%s'", device);
        free(pcm);
        free(thread);
        return false;
    }
    if (pcm->period > PB_BLOCK_MAX) {
        snprintf(e->error, sizeof e->error, "period of %lu frames exceeds %d", (unsigned long)pcm->period,
                 PB_BLOCK_MAX);
        live_pcm_close(pcm, false);
        goto fail;
    }
    e->pcm = pcm;
    e->period = (unsigned)pcm->period;
    if (e->options.lock_memory) {
        size_t samples = 0, floats = 0, output = 0;
        bool ok = pb_lock(e, sizeof *e, &e->locked_bytes) && pb_lock(pcm, sizeof *pcm, &e->locked_bytes)
               && live_pcm_buffer_layout(pcm->period, &samples, &floats, &output)
               && pb_lock(pcm->stereo, floats, &e->locked_bytes) && pb_lock(pcm->output, output, &e->locked_bytes);
        for (unsigned c = 0; c < PB_CHANNELS; c++)
            ok = pb_lock(e->channel[c].pool, sizeof *e->channel[c].pool, &e->locked_bytes) && ok;
        if (!ok) e->lock_failed = true;
    }
    e->thread = thread;
    /* The worker takes channels, or voice banks of the vector engine. */
    e->parallel = e->options.worker && worker_start(e);
    atomic_store(&e->running, true);
    if (pthread_create(thread, nullptr, audio_main, e)) {
        e->parallel = false;
        worker_stop(e);
        atomic_store(&e->running, false);
        live_pcm_close(pcm, false);
        e->pcm = e->thread = nullptr;
        snprintf(e->error, sizeof e->error, "cannot start the audio thread");
        goto fail;
    }
    return true;
fail:
    free(pcm);
    free(thread);
    if (!e->error[0]) snprintf(e->error, sizeof e->error, "out of memory");
    return false;
}

void pb_engine_stop(pb_engine *e) {
    if (!e->thread) return;
    atomic_store(&e->running, false);
    pthread_join(*(pthread_t *)e->thread, nullptr);
    e->parallel = false;
    worker_stop(e);
    live_pcm_close(e->pcm, false);
    free(e->pcm);
    free(e->thread);
    e->pcm = e->thread = nullptr;
}

bool pb_engine_running(const pb_engine *e) { return e->thread != nullptr; }
