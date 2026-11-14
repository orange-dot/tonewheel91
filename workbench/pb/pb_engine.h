/* Patchbench audio engine (docs/patchbench-plan.md §5).
 *
 * The engine plays PB_CHANNELS channels.  A channel is a voice pool with its
 * own patch, voice setup and insert rack, fed by its own pattern of the
 * sequencer; the output is the sum of the channels, each at its gain.
 *
 * The UI publishes whole values through latest-wins mailboxes (triple
 * buffers): per channel the audible patch, the voice setup and the rack,
 * and one sequence for all channels.  The audio thread adopts the newest of
 * each at the start of a period.  Notes, panic and transport are events on
 * an SPSC ring.  A pool change (limit, unison, detune, seed) is a pool the
 * UI builds and publishes as a pointer; the audio thread swaps at a period
 * boundary and hands the old one back for the UI to free.  ALSA keeps
 * running throughout.
 *
 * The real-time half (pb_rt.c) allocates nothing, takes no locks and makes
 * no system calls; `make check-patchbench-symbols` audits it. */
#ifndef PB_ENGINE_H
#define PB_ENGINE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../src/mamutanalog.h"
#include "pb_seq.h"
#include "pb_state.h"

enum { PB_RING = 1024, PB_SCOPE = 16384, PB_BLOCK_MAX = 4096, PB_HIST_BINS = 1000 };

/* Live-path options, set before pb_engine_start (2026-11-11). */
typedef struct {
    bool lock_memory;     /* mlock the engine, its pools and PCM buffers; prefault the stack */
    int audio_cpu;        /* pin the audio thread to this CPU, -1 none */
    bool worker;          /* a second real-time thread renders half the voice banks (step 7) */
    int worker_cpu;       /* pin it here, -1 none */
    unsigned worker_lead_us; /* wake this long before the next period is due and spin; 0 = futex only */
} pb_engine_options;

/* The voice-bank worker (live-path step 7): woken by a futex once per
 * render run, it computes part 1 while the audio thread computes part 0. */
typedef struct {
    void *thread;
    _Atomic uint32_t go, done;    /* generations, also the futex words */
    _Atomic bool quit;
    void (*job)(void *, unsigned);
    void *arg;
    _Atomic int priority;         /* granted, 0 none */
    _Atomic bool running, pinned;
    uint64_t runs;
} pb_worker;

/* Per-period timing written by the audio thread, read after a run (or
 * approximately while it runs). Bins are 10 us wide, the last one open. */
typedef struct {
    uint32_t dsp[PB_HIST_BINS];   /* pb_rt_process time */
    uint32_t wake[PB_HIST_BINS];  /* start of one period to the next */
    float dsp_max_ms, wake_max_ms;
    uint64_t periods, migrations;
    int cpu;                      /* where the last period ran */
} pb_timing;

typedef enum : uint8_t {
    PB_EV_NOTE_ON, PB_EV_NOTE_OFF, PB_EV_PANIC, PB_EV_SEQ_PLAY, PB_EV_SEQ_STOP, PB_EV_RESET_SAFETY,
} pb_event_kind;

typedef struct {
    pb_event_kind kind;
    uint8_t note, velocity;
    uint8_t channel;              /* notes only */
} pb_event;

/* Triple buffer: three slots, the UI owns `back`, the audio thread `front`,
 * the third is in the atomic `state` with a dirty bit. */
typedef struct {
    _Atomic uint32_t state;
    uint32_t back, front;
} pb_triple;

/* A pool and the voice setup it was built with, allocated together. */
typedef struct {
    ma_voice_pool pool;
    pb_voice voice;
} pb_pool;

typedef struct {
    float peak_left, peak_right, rms_left, rms_right;
    float budget_ms, budget_peak_ms, period_ms;
    uint64_t xruns, periods, frames;
    unsigned active_voices, peak_voices;      /* all channels */
    uint64_t steals;
    unsigned channel_voices[PB_CHANNELS];
    float channel_peak[PB_CHANNELS];          /* after the channel's gain */
    unsigned seq_step[PB_CHANNELS];
    bool seq_playing, muted;
    /* The focus channel (pb_engine_focus).  Its newest voice: the note and
     * start frame (scope pitch lock), the envelope, BCS and Glas counters. */
    uint8_t focus;
    bool newest_valid;
    uint8_t newest_note;
    uint64_t newest_frame;
    ma_envelope_stage amp_stage;
    float amp_level, cutoff_hz;
    uint64_t bcs_resets;
    float bcs_maximum;
    uint64_t glas_captures, glas_releases, glas_stolen;
    /* The focus channel's pool output diagnostics. */
    uint64_t knee_hits, sanitizations;
    float pre_peak, post_peak;
    uint64_t pool_swaps;
    /* The focus channel's insert rack (RK1). */
    uint64_t rack_applies, rack_sanitizations;
    bool rack_moving;
    uint64_t overload_panics;
    bool demoted;
    uint64_t worker_runs;
} pb_meters;

typedef struct {
    /* Audio-side state. */
    pb_pool *pool;
    pb_voice voice;                 /* applied */
    float gain;
    pb_seq_run seq;
    uint8_t newest_note;
    uint64_t newest_frame;
    bool newest_valid;
    float last_peak;
    ma_frame block[PB_BLOCK_MAX];
    /* The insert rack's state lives with the engine, not the pool, so a
     * pool swap keeps its tails (RKD4). Audio thread only after start. */
    ma_rack rack;
    /* Mailboxes. */
    ma_patch patch_slot[3];
    pb_triple patch_box;
    pb_voice voice_slot[3];
    pb_triple voice_box;
    ma_rack_spec rack_slot[3];
    pb_triple rack_box;
    _Atomic(pb_pool *) next_pool;
    _Atomic(pb_pool *) retired_pool;
} pb_engine_channel;

typedef struct pb_engine {
    unsigned rate;
    pb_engine_channel channel[PB_CHANNELS];
    /* Audio-side state. */
    pb_sequence sequence, pending;
    bool has_pending;
    uint64_t frames;                /* rendered since create */
    unsigned over_run;
    bool muted;
    uint64_t pool_swaps;
    float last_peak[2], last_rms[2];
    /* The piece of a period under render, and which part (thread) renders
     * each channel when the channels are spread over the worker. */
    size_t run_at, run_frames;
    uint8_t run_part[PB_CHANNELS];
    /* The sequence mailbox. */
    pb_sequence sequence_slot[3];
    pb_triple sequence_box;
    _Atomic uint8_t focus;          /* the channel the meters describe */
    /* Events. */
    _Atomic uint32_t head, tail;
    pb_event ring[PB_RING];
    /* Scope: the output frames, newest at scope_write - 1. */
    float scope[PB_SCOPE][2];
    _Atomic uint64_t scope_write;
    /* Meters under a sequence counter. */
    _Atomic uint32_t meter_sequence;
    pb_meters meters;
    /* Thread. */
    _Atomic bool running;
    _Atomic int scheduling;         /* 3 RealtimeKit, 2 SCHED_FIFO, 1 nice, 0 normal */
    _Atomic int priority;           /* the real-time priority granted, 0 none */
    _Atomic int audio_tid;          /* the audio thread, for the SIGXCPU handler */
    _Atomic bool demoted;           /* SIGXCPU: real time given up under overload */
    uint64_t overload_panics;       /* periods over budget in a row -> panic */
    unsigned over_budget;           /* consecutive periods with DSP > period */
    unsigned not_blocked;           /* consecutive writes that returned at once */
    _Atomic bool pinned;            /* the audio thread runs where options.audio_cpu says */
    pb_engine_options options;
    size_t locked_bytes;            /* mlocked by pb_engine_start and later pools */
    bool lock_failed;
    pb_timing timing;
    pb_worker worker;
    ma_pool_executor executor;
    bool parallel;                  /* the worker is up: renders may use it */
    void *thread, *pcm;
    unsigned period;
    bool (*raise_priority)(int *priority);   /* called on the audio thread */
    char error[160];
} pb_engine;

/* ---- real time (pb_rt.c) -------------------------------------------------- */

/* Adopt mailboxes and events, then render `frames` (≤ PB_BLOCK_MAX)
 * interleaved stereo frames. */
void pb_rt_process(pb_engine *e, float *out, size_t frames);
void pb_rt_publish(pb_engine *e, float budget_ms, float period_ms, uint64_t xruns);
/* CPU overload: DSP took longer than the period PB_OVERLOAD_PERIODS times in
 * a row. Stops every voice and tail at once (a pool reset, not a panic,
 * whose releases would keep the load), so the next periods fit again. */
enum { PB_OVERLOAD_PERIODS = 4 };
/* ... or the ALSA write did not block this many periods in a row (about
 * 107 ms at 128 frames, under RealtimeKit's 150 ms soft limit). */
enum { PB_NOT_BLOCKED_PERIODS = 40 };
void pb_rt_overload(pb_engine *e);

/* ---- UI side (pb_engine.c) ------------------------------------------------ */

/* A pool with the patch and voice setup applied; null on failure. */
pb_pool *pb_pool_build(unsigned rate, const ma_patch *patch, const pb_voice *voice);
/* `patch` and `voice` hold one entry per channel. */
pb_engine *pb_engine_create(unsigned rate, const ma_patch patch[PB_CHANNELS], const pb_voice voice[PB_CHANNELS],
                            const pb_sequence *sequence);
void pb_engine_destroy(pb_engine *e);
bool pb_engine_send(pb_engine *e, pb_event ev);
void pb_engine_set_patch(pb_engine *e, unsigned channel, const ma_patch *patch);
/* Adopted while stopped, else at the next step boundary (all channels). */
void pb_engine_set_sequence(pb_engine *e, const pb_sequence *sequence);
/* Publish a channel's audible insert rack; the audio thread applies it with
 * its fades.  _now applies and settles it at once and is only for an engine
 * whose audio thread is not running (bounce, tests). */
void pb_engine_set_rack(pb_engine *e, unsigned channel, const ma_rack_spec *rack);
bool pb_engine_set_rack_now(pb_engine *e, unsigned channel, const ma_rack_spec *rack);
/* Live fields apply at the next period; pool fields build a new pool from
 * `patch` (the channel's audible patch) and publish it; the audio thread
 * swaps at the next period (or the first one after a start).  False while an
 * earlier swap of that channel is still in flight: call again later. */
bool pb_engine_set_voice(pb_engine *e, unsigned channel, const pb_voice *voice, const ma_patch *patch);
/* The channel whose newest voice, pool and rack the meters report. */
void pb_engine_focus(pb_engine *e, unsigned channel);
/* Free the pools the audio thread handed back. */
void pb_engine_collect(pb_engine *e);
pb_meters pb_engine_read(pb_engine *e);
/* The newest `count` scope frames (interleaved) and the absolute frame
 * index one past the last of them. */
uint64_t pb_engine_scope(pb_engine *e, float *dst, size_t count);

bool pb_engine_start(pb_engine *e, const char *device, unsigned period, unsigned periods);
void pb_engine_stop(pb_engine *e);
bool pb_engine_running(const pb_engine *e);

#endif
