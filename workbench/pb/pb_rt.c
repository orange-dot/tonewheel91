/* The audio thread's half of the Patchbench engine.  No allocation, no
 * locks, no system calls, no logging (make check-patchbench-symbols). */
#include "pb_engine.h"

#include <math.h>

enum { DIRTY = 4u };

/* Audio side of a triple buffer: true when a newer value was published. */
static bool take(pb_triple *t) {
    if (!(atomic_load_explicit(&t->state, memory_order_relaxed) & DIRTY)) return false;
    uint32_t old = atomic_exchange_explicit(&t->state, t->front, memory_order_acq_rel);
    t->front = old & 3u;
    return true;
}

static void note_on(pb_engine *e, unsigned c, uint8_t note, uint8_t velocity, uint64_t frame) {
    pb_engine_channel *ch = &e->channel[c];
    if (ma_voice_pool_note_on(&ch->pool->pool, 0, note, velocity)) {
        ch->newest_note = note;
        ch->newest_frame = frame;
        ch->newest_valid = true;
    }
}

static void apply_voice(pb_engine_channel *ch, const pb_voice *v) {
    pb_voice *a = &ch->voice;
    ma_voice_pool *p = &ch->pool->pool;
    if (v->character != a->character) ma_voice_pool_set_character(p, v->character);
    if (v->glide_s != a->glide_s) ma_voice_pool_set_glide(p, v->glide_s > 0, v->glide_s);
    if (v->lfo_depth != a->lfo_depth || v->lfo_rate_hz != a->lfo_rate_hz)
        ma_voice_pool_set_lfo(p, v->lfo_depth, v->lfo_rate_hz);
    if (v->chorus_mix != a->chorus_mix || v->chorus_depth != a->chorus_depth || v->chorus_rate_hz != a->chorus_rate_hz)
        ma_effects_set_chorus(&p->effects, (ma_chorus_controls){ v->chorus_mix > 0, v->chorus_mix, v->chorus_depth,
                                                                 v->chorus_rate_hz });
    if (v->reverb_mix != a->reverb_mix || v->reverb_size != a->reverb_size || v->reverb_damping != a->reverb_damping)
        ma_effects_set_reverb(&p->effects, (ma_reverb_controls){ v->reverb_mix > 0, v->reverb_mix, v->reverb_size,
                                                                 v->reverb_damping });
    /* Pool fields belong to the pool that is playing. */
    pb_voice keep = *a;
    *a = *v;
    a->limit = keep.limit;
    a->unison = keep.unison;
    a->detune_cents = keep.detune_cents;
    a->seed = keep.seed;
    ch->gain = v->gain;
}

/* A muted channel starts no notes; its note-offs still go out, so a note
 * that sounded when the mute came ends at its gate. */
static void seq_events(pb_engine *e, unsigned c, const pb_seq_event *ev, size_t n, uint64_t frame) {
    for (size_t i = 0; i < n; i++)
        if (!ev[i].on) (void)ma_voice_pool_note_off(&e->channel[c].pool->pool, 0, ev[i].note, 0);
        else if (!e->sequence.mute[c]) note_on(e, c, ev[i].note, ev[i].velocity, frame);
}

/* The channels start and stop together: channel 0 tells for all. */
static bool playing(const pb_engine *e) { return e->channel[0].seq.playing; }

static void stop_sequence(pb_engine *e) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_seq_event ev[PB_SEQ_HELD];
        size_t n = pb_seq_stop(&e->channel[c].seq, ev, PB_SEQ_HELD);
        seq_events(e, c, ev, n, e->frames);
    }
}

static void adopt(pb_engine *e) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_engine_channel *ch = &e->channel[c];
        pb_pool *next = atomic_exchange_explicit(&ch->next_pool, nullptr, memory_order_acq_rel);
        if (next) {
            atomic_store_explicit(&ch->retired_pool, ch->pool, memory_order_release);
            ch->pool = next;
            ma_voice_pool_attach_rack(&ch->pool->pool, &ch->rack);
            ch->voice = next->voice;
            ch->gain = next->voice.gain;
            e->pool_swaps++;
            ch->newest_valid = false;
            /* Sounding notes end at the swap; the sequencer forgets them. */
            ch->seq.held_count = 0;
        }
        if (take(&ch->voice_box)) apply_voice(ch, &ch->voice_slot[ch->voice_box.front]);
        if (take(&ch->patch_box)) ma_voice_pool_set_patch(&ch->pool->pool, &ch->patch_slot[ch->patch_box.front]);
        if (take(&ch->rack_box)) (void)ma_rack_apply(&ch->rack, &ch->rack_slot[ch->rack_box.front]);
    }
    if (take(&e->sequence_box)) {
        e->pending = e->sequence_slot[e->sequence_box.front];
        e->has_pending = true;
    }
    if (e->has_pending && !playing(e)) {
        e->sequence = e->pending;
        e->has_pending = false;
    }
}

static void panic(pb_engine *e) {
    if (playing(e)) stop_sequence(e);
    for (unsigned c = 0; c < PB_CHANNELS; c++) ma_voice_pool_panic(&e->channel[c].pool->pool);
}

static void events(pb_engine *e) {
    uint32_t tail = atomic_load_explicit(&e->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&e->head, memory_order_acquire);
    for (; tail != head; tail++) {
        const pb_event *c = &e->ring[tail % PB_RING];
        unsigned channel = c->channel < PB_CHANNELS ? c->channel : 0;
        switch (c->kind) {
        case PB_EV_NOTE_ON: note_on(e, channel, c->note, c->velocity, e->frames); break;
        case PB_EV_NOTE_OFF: (void)ma_voice_pool_note_off(&e->channel[channel].pool->pool, 0, c->note, 0); break;
        case PB_EV_PANIC: panic(e); break;
        case PB_EV_SEQ_PLAY:
            if (playing(e)) stop_sequence(e);
            if (e->has_pending) {
                e->sequence = e->pending;
                e->has_pending = false;
            }
            for (unsigned k = 0; k < PB_CHANNELS; k++) pb_seq_start(&e->channel[k].seq, &e->sequence.channel[k], e->rate);
            break;
        case PB_EV_SEQ_STOP: if (playing(e)) stop_sequence(e); break;
        case PB_EV_RESET_SAFETY: e->muted = false; e->over_run = 0; break;
        }
    }
    atomic_store_explicit(&e->tail, tail, memory_order_release);
}

/* One part's channels over the piece under render. */
static void render_part(void *arg, unsigned part) {
    pb_engine *e = arg;
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (e->run_part[c] == part)
            ma_voice_pool_render(&e->channel[c].pool->pool, e->channel[c].block + e->run_at, e->run_frames);
}

/* Render [at, at + n) of every channel's block.  When the worker is up and
 * two or more channels have voices, the channels go to the two threads,
 * each to the one with fewer voices so far; with one busy channel its voice
 * banks do (ma_voice_pool_render_parallel).  A short piece (the sequencer
 * may split a period at an event) stays on this thread: waking the worker
 * would cost more than it saves.  Every way gives the same bits: the
 * channels share no state. */
enum { PB_PARALLEL_MIN = 32 };
static void render(pb_engine *e, size_t at, size_t n) {
    if (!n) return;
    bool wide = e->parallel && n >= PB_PARALLEL_MIN;
    unsigned load[2] = { 0, 0 }, busy = 0;
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        unsigned voices = e->channel[c].pool->pool.diagnostics.active_slots;
        unsigned part = voices && load[1] < load[0];
        e->run_part[c] = (uint8_t)part;
        load[part] += voices;
        busy += voices != 0;
    }
    if (wide && busy > 1) {
        e->run_at = at;
        e->run_frames = n;
        e->executor.run(e->executor.context, render_part, e, 2);
        return;
    }
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        ma_voice_pool_render_parallel(&e->channel[c].pool->pool, e->channel[c].block + at, n,
                                      wide ? &e->executor : nullptr);
}

/* Frames until the next event of any channel, at most `limit`. */
static uint32_t until(const pb_engine *e, uint32_t limit) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) limit = pb_seq_until(&e->channel[c].seq, limit);
    return limit;
}

void pb_rt_process(pb_engine *e, float *out, size_t frames) {
    if (frames > PB_BLOCK_MAX) frames = PB_BLOCK_MAX;
    adopt(e);
    events(e);
    size_t at = 0;
    while (at < frames) {
        uint32_t left = (uint32_t)(frames - at);
        uint32_t n = until(e, left);
        if (!n) {
            /* One tempo: when channel 0 is at a step boundary, all are. */
            if (e->has_pending && pb_seq_at_boundary(&e->channel[0].seq)) {
                for (unsigned c = 0; c < PB_CHANNELS; c++) pb_seq_adopt(&e->channel[c].seq, &e->pending.channel[c]);
                e->sequence = e->pending;
                e->has_pending = false;
            }
            for (unsigned c = 0; c < PB_CHANNELS; c++) {
                pb_seq_event ev[PB_SEQ_EVENTS];
                size_t count = pb_seq_fire(&e->channel[c].seq, &e->sequence.channel[c], ev, PB_SEQ_EVENTS);
                seq_events(e, c, ev, count, e->frames + at);
            }
            n = until(e, left);
            if (!n) n = 1;   /* nothing more is due at this frame */
        }
        render(e, at, n);
        for (unsigned c = 0; c < PB_CHANNELS; c++) pb_seq_skip(&e->channel[c].seq, n);
        at += n;
    }

    float peak[2] = { 0, 0 }, channel_peak[PB_CHANNELS] = { 0 };
    double square[2] = { 0, 0 };
    uint64_t write = atomic_load_explicit(&e->scope_write, memory_order_relaxed);
    for (size_t i = 0; i < frames; i++) {
        /* The mix: the channels in order, each at its gain. */
        float l = 0, r = 0;
        for (unsigned c = 0; c < PB_CHANNELS; c++) {
            const pb_engine_channel *ch = &e->channel[c];
            float cl = ch->block[i].left * ch->gain, cr = ch->block[i].right * ch->gain;
            l = c ? l + cl : cl;
            r = c ? r + cr : cr;
            if (fabsf(cl) > channel_peak[c]) channel_peak[c] = fabsf(cl);
            if (fabsf(cr) > channel_peak[c]) channel_peak[c] = fabsf(cr);
        }
        bool finite = isfinite(l) && isfinite(r);
        bool over = fabsf(l) > 1 || fabsf(r) > 1;
        e->over_run = over ? e->over_run + 1 : 0;
        if (!e->muted && (!finite || e->over_run > e->rate / 100)) {
            e->muted = true;
            panic(e);
        }
        if (e->muted || !finite) l = r = 0;
        out[2 * i] = l;
        out[2 * i + 1] = r;
        e->scope[write % PB_SCOPE][0] = l;
        e->scope[write % PB_SCOPE][1] = r;
        write++;
        if (fabsf(l) > peak[0]) peak[0] = fabsf(l);
        if (fabsf(r) > peak[1]) peak[1] = fabsf(r);
        square[0] += (double)l * l;
        square[1] += (double)r * r;
    }
    atomic_store_explicit(&e->scope_write, write, memory_order_release);
    e->frames += frames;
    for (unsigned c = 0; c < 2; c++) {
        e->last_peak[c] = peak[c];
        e->last_rms[c] = frames ? (float)sqrt(square[c] / (double)frames) : 0;
    }
    for (unsigned c = 0; c < PB_CHANNELS; c++) e->channel[c].last_peak = channel_peak[c];
}

/* An emergency stop, not a musical panic: a panic lets every voice ring
 * through its release, which keeps the CPU overloaded. A reset clears
 * notes and tails at once and keeps patch, controls and configuration. */
void pb_rt_overload(pb_engine *e) {
    if (playing(e)) stop_sequence(e);
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        ma_voice_pool_reset(&e->channel[c].pool->pool);
        e->channel[c].newest_valid = false;
    }
    e->overload_panics++;
}

void pb_rt_publish(pb_engine *e, float budget_ms, float period_ms, uint64_t xruns) {
    uint32_t sequence = atomic_load_explicit(&e->meter_sequence, memory_order_relaxed);
    atomic_store_explicit(&e->meter_sequence, sequence + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    pb_meters *m = &e->meters;
    unsigned focus = atomic_load_explicit(&e->focus, memory_order_relaxed);
    if (focus >= PB_CHANNELS) focus = 0;
    const pb_engine_channel *ch = &e->channel[focus];
    const ma_voice_pool *p = &ch->pool->pool;
    m->peak_left = e->last_peak[0];
    m->peak_right = e->last_peak[1];
    m->rms_left = e->last_rms[0];
    m->rms_right = e->last_rms[1];
    m->budget_ms = budget_ms;
    if (budget_ms > m->budget_peak_ms) m->budget_peak_ms = budget_ms;
    m->period_ms = period_ms;
    m->xruns = xruns;
    m->periods++;
    m->frames = e->frames;
    m->active_voices = m->peak_voices = 0;
    m->steals = 0;
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        const ma_pool_diagnostics *d = &e->channel[c].pool->pool.diagnostics;
        m->channel_voices[c] = d->active_slots;
        m->channel_peak[c] = e->channel[c].last_peak;
        m->seq_step[c] = e->channel[c].seq.sounding;
        m->active_voices += d->active_slots;
        m->peak_voices += d->peak_slots;
        m->steals += d->steals_released + d->steals_sustained + d->steals_held;
    }
    m->seq_playing = playing(e);
    m->muted = e->muted;
    m->focus = (uint8_t)focus;
    m->newest_valid = ch->newest_valid;
    m->newest_note = ch->newest_note;
    m->newest_frame = ch->newest_frame;
    m->pool_swaps = e->pool_swaps;
    m->rack_applies = ch->rack.applies;
    m->rack_sanitizations = ch->rack.sanitizations;
    m->rack_moving = ma_rack_moving(&ch->rack);
    m->overload_panics = e->overload_panics;
    m->worker_runs = e->worker.runs;
    m->demoted = atomic_load_explicit(&e->demoted, memory_order_relaxed);
    m->knee_hits = p->output.diagnostics.knee_hit_count;
    m->sanitizations = p->output.diagnostics.sanitization_count;
    m->pre_peak = p->output.diagnostics.pre_peak;
    m->post_peak = p->output.diagnostics.post_peak;
    /* The newest awake slot (highest allocation id). */
    const ma_pool_slot *newest = nullptr;
    for (unsigned s = 0; s < p->config.limit; s++)
        if (p->slot[s].awake && (!newest || p->slot[s].allocation_id > newest->allocation_id)) newest = &p->slot[s];
    if (newest) {
        m->amp_stage = newest->voice.amp_envelope.stage;
        m->amp_level = newest->voice.amp_envelope.level;
        m->cutoff_hz = newest->voice.filter_cutoff_effective_hz;
        m->bcs_resets = newest->voice.bcs.reset_count;
        m->bcs_maximum = newest->voice.bcs.maximum_state;
        m->glas_captures = newest->voice.glas.captures;
        m->glas_releases = newest->voice.glas.releases;
        m->glas_stolen = newest->voice.glas.grains_stolen;
    } else {
        m->amp_stage = MA_ENVELOPE_IDLE;
        m->amp_level = 0;
    }
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&e->meter_sequence, sequence + 2, memory_order_release);
}
