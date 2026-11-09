/* The real-time half of the live engine: runs on the audio thread.  No
 * allocation, no locks, no system calls, no logging (audited by
 * `make check-workbench-symbols`). */
#include "wb_live.h"

#include <math.h>
#include <string.h>

bool wb_live_send(wb_live *live, wb_live_cmd cmd) {
    uint32_t head = atomic_load_explicit(&live->ring.head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&live->ring.tail, memory_order_acquire);
    if (head - tail == WB_LIVE_RING) return false;
    live->ring.cmd[head % WB_LIVE_RING] = cmd;
    atomic_store_explicit(&live->ring.head, head + 1, memory_order_release);
    return true;
}

static void panic(wb_live *live) {
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) {
        wb_live_slot *x = &live->slot[s];
        if (!x->active) continue;
        if (x->kind == WB_PART_MAMUT) ma_voice_pool_panic(x->pool);
        else
            for (uint8_t ch = 0; ch < 16; ch++) {
                /* All notes off, then sustain off, through the driver's map. */
                for (uint8_t n = 0; n < 128; n++) wb_engine_midi(&x->engine, (uint8_t)(0x80u | ch), n, 0);
                wb_engine_midi(&x->engine, (uint8_t)(0xb0u | ch), 64, 0);
            }
    }
}

static void apply(wb_live *live, const wb_live_cmd *c) {
    wb_live_slot *x = c->slot < WB_LIVE_SLOTS ? &live->slot[c->slot] : nullptr;
    switch (c->kind) {
    case WB_LIVE_NOTE_ON:
        if (!x || !x->active) return;
        if (x->kind == WB_PART_MAMUT) {
            if (ma_voice_pool_note_on(x->pool, 0, c->d0, c->d1))
                ma_voice_pool_set_poly_pressure(x->pool, 0, c->d0, x->pressure);
        } else wb_engine_midi(&x->engine, 0x90u, c->d0, c->d1);
        return;
    case WB_LIVE_NOTE_OFF:
        if (!x || !x->active) return;
        if (x->kind == WB_PART_MAMUT) (void)ma_voice_pool_note_off(x->pool, 0, c->d0, 0);
        else wb_engine_midi(&x->engine, 0x80u, c->d0, 0);
        return;
    case WB_LIVE_MIDI:
        /* A raw channel message (console controls) for organ and ep73. */
        if (x && x->active && x->kind != WB_PART_MAMUT) wb_engine_midi(&x->engine, c->d0, c->d1, c->d2);
        return;
    case WB_LIVE_PANIC: panic(live); return;
    case WB_LIVE_PLAY: live->playing = true; return;
    case WB_LIVE_STOP: live->playing = false; return;
    case WB_LIVE_LOCATE: {
        live->position = c->frame;
        /* The first score event at or after the new position. */
        size_t lo = 0, hi = live->score_count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (live->score[mid].frame < c->frame) lo = mid + 1;
            else hi = mid;
        }
        live->score_next = lo;
        panic(live);
        return;
    }
    case WB_LIVE_GAIN: if (x) x->gain = c->value; return;
    case WB_LIVE_PLAYBACK_GAIN: if (c->d0 < 2) live->take_gain[c->d0] = c->value; return;
    case WB_LIVE_SELECT_TAKE: if (c->d0 < 2 && live->take[c->d0]) live->take_selected = c->d0; return;
    case WB_LIVE_RESET_SAFETY: live->muted = false; live->over_run = 0; return;
    }
}

void wb_live_process(wb_live *live, float *out, size_t frames) {
    uint32_t tail = atomic_load_explicit(&live->ring.tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&live->ring.head, memory_order_acquire);
    while (tail != head) apply(live, &live->ring.cmd[tail++ % WB_LIVE_RING]);
    atomic_store_explicit(&live->ring.tail, tail, memory_order_release);

    unsigned take = live->take_selected;
    const float *samples = live->take[take];
    uint64_t take_frames = live->take_frames[take];
    float take_gain = live->take_gain[take];
    float peak_left = 0, peak_right = 0;
    double square = 0;
    for (size_t i = 0; i < frames; i++) {
        float left = 0, right = 0;
        while (live->playing && live->score_next < live->score_count
               && live->score[live->score_next].frame <= live->position) {
            const wb_note_event *e = &live->score[live->score_next++];
            int slot = e->part < WB_PARTS ? live->part_slot[e->part] : -1;
            if (slot < 0 || !live->slot[slot].active) continue;
            wb_live_slot *x = &live->slot[slot];
            if (x->kind != WB_PART_MAMUT) wb_engine_midi(&x->engine, e->status, e->kind ? e->d1 : e->note, e->d2);
            else if (e->kind == 0xb0u && e->d1 == 64) ma_voice_pool_set_sustain(x->pool, 0, e->d2 >= 64);
            else if (e->kind) continue;
            else if (e->on) {
                if (ma_voice_pool_note_on(x->pool, 0, e->note, e->velocity))
                    ma_voice_pool_set_poly_pressure(x->pool, 0, e->note, x->pressure);
            } else (void)ma_voice_pool_note_off(x->pool, 0, e->note, 0);
        }
        /* The first contribution is assigned, later ones added: samples
         * stay as the engines made them (no +0 from a zero start), as
         * render_midi writes them, so a recorded take plays bit-exactly. */
        bool have = false;
        if (live->playing) {
            if (samples && live->position < take_frames) {
                left = samples[2 * live->position] * take_gain;
                right = samples[2 * live->position + 1] * take_gain;
                have = true;
            }
            live->position++;
            /* Stop when both the take and the live score are done. */
            if ((!samples || live->position >= take_frames) && live->score_next >= live->score_count)
                live->playing = false;
        }
        for (unsigned s = 0; s < WB_LIVE_SLOTS; s++) {
            wb_live_slot *x = &live->slot[s];
            if (!x->active) continue;
            float l, r;
            if (x->kind == WB_PART_MAMUT) {
                ma_frame f = ma_voice_pool_tick(x->pool);
                l = f.left; r = f.right;
            } else {
                tw_stereo y = wb_engine_tick(&x->engine);
                l = y.l; r = y.r;
            }
            if (have) {
                left += l * x->gain;
                right += r * x->gain;
            } else {
                left = l * x->gain;
                right = r * x->gain;
                have = true;
            }
        }
        /* Safety: a non-finite sample, or more than 10 ms above 0 dBFS,
         * mutes the output and panics every instrument. */
        bool finite = isfinite(left) && isfinite(right);
        bool over = fabsf(left) > 1 || fabsf(right) > 1;
        live->over_run = over ? live->over_run + 1 : 0;
        if (!live->muted && (!finite || live->over_run > live->rate / 100)) {
            live->muted = true;
            panic(live);
        }
        if (live->muted || !finite) left = right = 0;
        out[2 * i] = left;
        out[2 * i + 1] = right;
        if (fabsf(left) > peak_left) peak_left = fabsf(left);
        if (fabsf(right) > peak_right) peak_right = fabsf(right);
        square += (double)left * left + (double)right * right;
    }
    live->last_peak_left = peak_left;
    live->last_peak_right = peak_right;
    live->last_rms = frames ? (float)sqrt(square / (2.0 * (double)frames)) : 0;
}

void wb_live_publish(wb_live *live, float budget_ms, float period_ms, uint64_t xruns) {
    uint32_t sequence = atomic_load_explicit(&live->meter_sequence, memory_order_relaxed);
    atomic_store_explicit(&live->meter_sequence, sequence + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    wb_live_meters *m = &live->meters;
    m->peak_left = live->last_peak_left;
    m->peak_right = live->last_peak_right;
    m->rms = live->last_rms;
    m->budget_ms = budget_ms;
    if (budget_ms > m->budget_peak_ms) m->budget_peak_ms = budget_ms;
    m->period_ms = period_ms;
    m->position = live->position;
    m->xruns = xruns;
    m->periods++;
    m->muted = live->muted;
    m->playing = live->playing;
    unsigned voices = 0;
    for (unsigned s = 0; s < WB_LIVE_SLOTS; s++)
        if (live->slot[s].active && live->slot[s].kind == WB_PART_MAMUT)
            voices += live->slot[s].pool->diagnostics.active_slots;
    m->active_voices = voices;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&live->meter_sequence, sequence + 2, memory_order_release);
}

wb_live_meters wb_live_read(wb_live *live) {
    wb_live_meters copy;
    for (;;) {
        uint32_t before = atomic_load_explicit(&live->meter_sequence, memory_order_acquire);
        if (before & 1u) continue;
        atomic_thread_fence(memory_order_acquire);
        memcpy(&copy, &live->meters, sizeof copy);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&live->meter_sequence, memory_order_acquire) == before) return copy;
    }
}
