/* Engine parity (docs/patchbench-plan.md §7): a scripted run through the
 * engine in non-real time — notes, sequencer, patch mailbox traffic, live
 * voice changes and pool swaps, on one channel and on all four — equals a
 * reference that drives one ma_voice_pool per channel a frame at a time
 * with the same calls at the same frames and sums them. */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../pb_bounce.h"
#include "../pb_engine.h"

static unsigned failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { RATE = 48000, PERIOD = 128, PERIODS = 900 };

typedef struct {
    unsigned period;
    enum { S_PLAY, S_STOP, S_ON, S_OFF, S_PATCH, S_VOICE, S_TEMPO, S_MUTE, S_PANIC } kind;
    uint8_t channel, note, velocity;
    float value;
} script_item;

/* ---- reference -------------------------------------------------------------- */

typedef struct {
    pb_pool *pool[PB_CHANNELS];
    ma_rack *rack[PB_CHANNELS];    /* each channel's own: the voice's chorus and reverb (RK2) */
    pb_voice voice[PB_CHANNELS];
    pb_sequence sequence, pending;
    bool has_pending;
    pb_seq_run seq[PB_CHANNELS];
} reference;

/* A channel's pool and its rack, the voice's chorus and reverb in place:
 * written here from the core's calls, not through pb_rt_chain. */
static bool ref_build(reference *r, unsigned c, const ma_patch *patch, const pb_voice *v) {
    r->pool[c] = pb_pool_build(RATE, patch, v);
    r->rack[c] = calloc(1, sizeof *r->rack[c]);
    if (!r->pool[c] || !r->rack[c]) return false;
    ma_rack_spec chain = { 0 };
    ma_rack_init(r->rack[c], RATE);
    if ((v->chorus_mix > 0 && !ma_rack_spec_set_chorus(&chain, v->chorus_mix, v->chorus_depth, v->chorus_rate_hz))
        || (v->reverb_mix > 0 && !ma_rack_spec_set_reverb(&chain, v->reverb_mix, v->reverb_size, v->reverb_damping))
        || !ma_rack_apply(r->rack[c], &chain))
        return false;
    ma_rack_settle(r->rack[c]);
    ma_voice_pool_attach_rack(&r->pool[c]->pool, r->rack[c]);
    r->voice[c] = *v;
    return true;
}

static void ref_free(reference *r) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        free(r->pool[c]);
        free(r->rack[c]);
    }
}

static void ref_seq(reference *r, unsigned c, const pb_seq_event *ev, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!ev[i].on) (void)ma_voice_pool_note_off(&r->pool[c]->pool, 0, ev[i].note, 0);
        else if (!r->sequence.mute[c]) (void)ma_voice_pool_note_on(&r->pool[c]->pool, 0, ev[i].note, ev[i].velocity);
}

static void ref_stop(reference *r) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_seq_event ev[PB_SEQ_HELD];
        ref_seq(r, c, ev, pb_seq_stop(&r->seq[c], ev, PB_SEQ_HELD));
    }
}

static void ref_start(reference *r) {
    for (unsigned c = 0; c < PB_CHANNELS; c++) pb_seq_start(&r->seq[c], &r->sequence.channel[c], RATE);
}

static void ref_frame(reference *r, float *out) {
    if (r->seq[0].playing) {
        if (r->has_pending && pb_seq_at_boundary(&r->seq[0])) {
            for (unsigned c = 0; c < PB_CHANNELS; c++) pb_seq_adopt(&r->seq[c], &r->pending.channel[c]);
            r->sequence = r->pending;
            r->has_pending = false;
        }
        for (unsigned c = 0; c < PB_CHANNELS; c++)
            if (!pb_seq_until(&r->seq[c], 1)) {
                pb_seq_event ev[PB_SEQ_EVENTS];
                ref_seq(r, c, ev, pb_seq_fire(&r->seq[c], &r->sequence.channel[c], ev, PB_SEQ_EVENTS));
            }
    }
    out[0] = out[1] = 0;
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        ma_frame f = ma_voice_pool_tick(&r->pool[c]->pool);
        pb_seq_skip(&r->seq[c], 1);
        float l = f.left * r->voice[c].gain, right = f.right * r->voice[c].gain;
        out[0] = c ? out[0] + l : l;
        out[1] = c ? out[1] + right : right;
    }
}

/* The live fields of a voice setup, as the audio thread applies them. The
 * scripts do not move the chorus and reverb sends, so the reference's racks
 * keep the chain they were built with. */
static void ref_voice(ma_voice_pool *p, const pb_voice *a, const pb_voice *v) {
    if (v->character != a->character) ma_voice_pool_set_character(p, v->character);
    if (v->glide_s != a->glide_s) ma_voice_pool_set_glide(p, v->glide_s > 0, v->glide_s);
    if (v->lfo_depth != a->lfo_depth || v->lfo_rate_hz != a->lfo_rate_hz)
        ma_voice_pool_set_lfo(p, v->lfo_depth, v->lfo_rate_hz);
}

static uint64_t fnv(const float *x, size_t n, uint64_t h) {
    const unsigned char *b = (const unsigned char *)x;
    for (size_t i = 0; i < n * sizeof *x; i++) h = (h ^ b[i]) * UINT64_C(0x100000001b3);
    return h;
}

/* A test executor: part 1 on a second thread while part 0 runs here. */
typedef struct {
    void (*job)(void *, unsigned);
    void *arg;
} second_part;

static void *second_main(void *arg) {
    second_part *s = arg;
    s->job(s->arg, 1);
    return nullptr;
}

static void run_two_threads(void *context, void (*job)(void *, unsigned), void *arg, unsigned parts) {
    unsigned *runs = context;
    second_part second = { job, arg };
    pthread_t thread;
    bool started = parts > 1 && !pthread_create(&thread, nullptr, second_main, &second);
    job(arg, 0);
    if (started) pthread_join(thread, nullptr);
    else if (parts > 1) job(arg, 1);
    if (started) ++*runs;
}

/* Run `script` through the engine and the reference.  S_VOICE: a value
 * below zero is a new limit (a pool swap), else the character (live).
 * `spread` gives the engine an executor with a second thread, so busy
 * channels render on two threads.  Returns the FNV of the engine's PCM. */
static uint64_t run_script(const char *name, const ma_patch start_patch[PB_CHANNELS],
                           const pb_voice start_voice[PB_CHANNELS], const pb_sequence *start_sequence,
                           const script_item *script, unsigned count, bool spread, unsigned *swaps) {
    ma_patch patch[PB_CHANNELS];
    pb_voice voice[PB_CHANNELS];
    memcpy(patch, start_patch, sizeof patch);
    memcpy(voice, start_voice, sizeof voice);
    pb_sequence sequence = *start_sequence;
    pb_engine *e = pb_engine_create(RATE, patch, voice, &sequence);
    reference r = { .sequence = sequence };
    bool built = e != nullptr;
    for (unsigned c = 0; c < PB_CHANNELS; c++) built = ref_build(&r, c, &patch[c], &voice[c]) && built;
    CHECK(built, "%s: engine or reference pools", name);
    if (!built) return 0;
    unsigned spread_runs = 0;
    if (spread) {
        e->executor = (ma_pool_executor){ run_two_threads, &spread_runs, 2 };
        e->parallel = true;
    }
    float *out = calloc(2 * PERIOD, sizeof *out), ref[2];
    uint64_t he = UINT64_C(0xcbf29ce484222325);
    size_t mismatch = 0, first = SIZE_MAX;
    double energy = 0;
    unsigned item = 0;
    for (unsigned period = 0; period < PERIODS; period++) {
        /* UI side of both: the mailboxes take the last value of a period. */
        unsigned from = item;
        bool patched[PB_CHANNELS] = { false }, sequenced = false;
        pb_pool *swapped[PB_CHANNELS] = { nullptr };
        pb_voice next[PB_CHANNELS];
        memcpy(next, voice, sizeof next);
        for (; item < count && script[item].period == period; item++) {
            const script_item *s = &script[item];
            unsigned c = s->channel;
            switch (s->kind) {
            case S_PLAY: pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_PLAY }); break;
            case S_STOP: pb_engine_send(e, (pb_event){ .kind = PB_EV_SEQ_STOP }); break;
            case S_PANIC: pb_engine_send(e, (pb_event){ .kind = PB_EV_PANIC }); break;
            case S_ON: pb_engine_send(e, (pb_event){ PB_EV_NOTE_ON, s->note, s->velocity, s->channel }); break;
            case S_OFF: pb_engine_send(e, (pb_event){ PB_EV_NOTE_OFF, s->note, 0, s->channel }); break;
            case S_PATCH:
                patch[c].filter_cutoff_hz = s->value;
                pb_engine_set_patch(e, c, &patch[c]);
                patched[c] = true;
                break;
            case S_TEMPO:
            case S_MUTE:
                if (s->kind == S_MUTE) sequence.mute[c] = s->value != 0;
                else for (unsigned k = 0; k < PB_CHANNELS; k++) sequence.channel[k].bpm = (uint32_t)s->value;
                pb_engine_set_sequence(e, &sequence);
                sequenced = true;
                break;
            case S_VOICE:
                if (s->value < 0) {
                    next[c].limit = (uint8_t)-s->value;
                    swapped[c] = pb_pool_build(RATE, &patch[c], &next[c]);
                } else next[c].character = s->value;
                CHECK(pb_engine_set_voice(e, c, &next[c], &patch[c]), "%s: set voice at period %u", name, period);
                break;
            }
        }
        /* Reference: the adopt order of pb_rt.c, then events, then frames. */
        for (unsigned c = 0; c < PB_CHANNELS; c++) {
            if (swapped[c]) {
                free(r.pool[c]);
                r.pool[c] = swapped[c];
                ma_voice_pool_attach_rack(&r.pool[c]->pool, r.rack[c]);   /* the rack outlives the pool (RKD4) */
                r.seq[c].held_count = 0;
            } else ref_voice(&r.pool[c]->pool, &voice[c], &next[c]);
            voice[c] = r.voice[c] = next[c];
            if (patched[c]) ma_voice_pool_set_patch(&r.pool[c]->pool, &patch[c]);
        }
        if (sequenced) {
            r.pending = sequence;
            r.has_pending = true;
        }
        if (r.has_pending && !r.seq[0].playing) {
            r.sequence = r.pending;
            r.has_pending = false;
        }
        for (unsigned i = from; i < item; i++) {
            const script_item *s = &script[i];
            ma_voice_pool *p = &r.pool[s->channel]->pool;
            if (s->kind == S_PLAY) {
                if (r.seq[0].playing) ref_stop(&r);
                if (r.has_pending) { r.sequence = r.pending; r.has_pending = false; }
                ref_start(&r);
            } else if (s->kind == S_STOP && r.seq[0].playing) ref_stop(&r);
            else if (s->kind == S_PANIC) {
                if (r.seq[0].playing) ref_stop(&r);
                for (unsigned c = 0; c < PB_CHANNELS; c++) ma_voice_pool_panic(&r.pool[c]->pool);
            } else if (s->kind == S_ON) (void)ma_voice_pool_note_on(p, 0, s->note, s->velocity);
            else if (s->kind == S_OFF) (void)ma_voice_pool_note_off(p, 0, s->note, 0);
        }

        pb_rt_process(e, out, PERIOD);
        pb_rt_publish(e, 0, 0, 0);
        pb_engine_collect(e);
        for (unsigned i = 0; i < PERIOD; i++) {
            ref_frame(&r, ref);
            if (memcmp(ref, out + 2 * i, sizeof ref)) {
                if (first == SIZE_MAX) first = (size_t)period * PERIOD + i;
                mismatch++;
            }
            energy += (double)ref[0] * ref[0];
        }
        he = fnv(out, 2 * PERIOD, he);
    }
    CHECK(!mismatch, "%s: engine differs from the reference in %zu frames, first at %zu", name, mismatch, first);
    CHECK(energy > 1, "%s: the scripted run is silent (energy %g)", name, energy);
    CHECK(!spread || spread_runs > 100, "%s: the channels were spread over two threads only %u times", name,
          spread_runs);
    pb_meters m = pb_engine_read(e);
    if (swaps) *swaps = (unsigned)m.pool_swaps;
    printf("%s: %u periods, energy %.1f, PCM FNV %016llx", name, PERIODS, energy, (unsigned long long)he);
    if (spread) printf(", %u runs on two threads", spread_runs);
    printf("\n");
    free(out);
    ref_free(&r);
    pb_engine_destroy(e);
    return he;
}

/* One channel sounds, as the engine did before it had channels; the other
 * three idle on the same patch. */
static void test_parity(void) {
    static const script_item SCRIPT[] = {
        { 0, S_PLAY, 0, 0, 0, 0 },
        { 30, S_ON, 0, 72, 90, 0 },
        { 90, S_PATCH, 0, 0, 0, 900.0f },        /* cutoff */
        { 91, S_PATCH, 0, 0, 0, 1400.0f },
        { 92, S_PATCH, 0, 0, 0, 2100.0f },       /* three in a row: latest wins per period */
        { 140, S_OFF, 0, 72, 0, 0 },
        { 200, S_VOICE, 0, 0, 0, .35f },         /* character: live */
        { 260, S_TEMPO, 0, 0, 0, 13000 },        /* tempo change: adopted at a boundary */
        { 420, S_VOICE, 0, 0, 0, -16 },          /* limit 16: a pool swap */
        { 520, S_ON, 0, 55, 110, 0 },
        { 600, S_PANIC, 0, 0, 0, 0 },
        { 640, S_PLAY, 0, 0, 0, 0 },
        { 820, S_STOP, 0, 0, 0, 0 },
    };
    ma_patch patch[PB_CHANNELS];
    pb_voice voice[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) patch[c] = ma_patch_lead, voice[c] = pb_voice_default();
    pb_pattern pattern = pb_builtin_pattern(PB_PAT_ARPEGGIO, 48, 12000);
    pb_sequence sequence = pb_sequence_single(&pattern);
    unsigned swaps = 0;
    run_script("engine parity", patch, voice, &sequence, SCRIPT, sizeof SCRIPT / sizeof *SCRIPT, false, &swaps);
    CHECK(swaps == 1, "one pool swap to limit 16 (%u)", swaps);
}

/* Four channels with their own patches, voice setups and patterns of
 * different lengths: live notes, patch and voice edits and pool swaps per
 * channel, a mute and an unmute, a tempo change for all.  The same run with
 * the channels spread over two threads gives the same PCM. */
static void test_channels(void) {
    static const script_item SCRIPT[] = {
        { 0, S_PLAY, 0, 0, 0, 0 },
        { 20, S_ON, 2, 67, 80, 0 },              /* a live note on channel 3 */
        { 60, S_PATCH, 1, 0, 0, 700.0f },        /* channel 2's cutoff */
        { 60, S_PATCH, 0, 0, 0, 1800.0f },       /* and channel 1's, the same period */
        { 110, S_OFF, 2, 67, 0, 0 },
        { 150, S_MUTE, 1, 0, 0, 1 },             /* channel 2 off at the next step */
        { 210, S_VOICE, 2, 0, 0, .4f },          /* channel 3's character: live */
        { 240, S_MUTE, 3, 0, 0, 0 },             /* channel 4 comes in */
        { 300, S_TEMPO, 0, 0, 0, 14500 },        /* every channel, at one boundary */
        { 360, S_VOICE, 1, 0, 0, -8 },           /* channel 2 to limit 8: its pool swaps, the others play on */
        { 380, S_MUTE, 1, 0, 0, 0 },
        { 470, S_ON, 3, 50, 100, 0 },
        { 470, S_ON, 0, 79, 70, 0 },
        { 560, S_PANIC, 0, 0, 0, 0 },
        { 600, S_PLAY, 0, 0, 0, 0 },
        { 700, S_VOICE, 0, 0, 0, -4 },           /* two swaps in one period */
        { 700, S_VOICE, 3, 0, 0, -16 },
        { 830, S_STOP, 0, 0, 0, 0 },
    };
    ma_patch patch[PB_CHANNELS] = { ma_patch_lead, ma_patch_dubina, ma_patch_tepih, ma_patch_prizma };
    pb_voice voice[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) voice[c] = pb_voice_default();
    voice[1].limit = 4;
    voice[1].glide_s = .06f;
    voice[1].gain = 22;
    voice[2].unison = 2;
    voice[2].detune_cents = 11;
    voice[2].chorus_mix = .4f;
    voice[2].gain = 9;
    voice[3].reverb_mix = .3f;
    voice[3].gain = 12;
    pb_sequence sequence = {
        .channel = { pb_builtin_pattern(PB_PAT_ARPEGGIO, 60, 12000), pb_builtin_pattern(PB_PAT_BASS, 36, 12000),
                     pb_builtin_pattern(PB_PAT_HELD_CHORD, 48, 12000), pb_builtin_pattern(PB_PAT_TIED_LINE, 55, 12000) },
        .mute = { false, false, false, true },
    };
    sequence.channel[1].steps = 12;              /* against 16, 8 and 8 */
    for (unsigned c = 0; c < PB_CHANNELS; c++) sequence.channel[c].swing = 20;
    CHECK(pb_sequence_valid(&sequence), "the four-channel sequence");
    unsigned swaps = 0;
    unsigned count = sizeof SCRIPT / sizeof *SCRIPT;
    uint64_t serial = run_script("channel parity", patch, voice, &sequence, SCRIPT, count, false, &swaps);
    CHECK(swaps == 3, "three pool swaps (%u)", swaps);
    uint64_t spread = run_script("channel parity, two threads", patch, voice, &sequence, SCRIPT, count, true, nullptr);
    CHECK(serial == spread, "spreading the channels over two threads changed the PCM");

    /* Each channel is heard: the run without it differs. */
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        pb_voice quiet[PB_CHANNELS];
        memcpy(quiet, voice, sizeof quiet);
        quiet[c].gain = 0;
        char name[40];
        snprintf(name, sizeof name, "  without channel %u", c + 1);
        CHECK(run_script(name, patch, quiet, &sequence, SCRIPT, count, false, nullptr) != serial,
              "channel %u adds nothing to the mix", c + 1);
    }
}

/* The bounce is the live engine in non-real time: it equals the reference
 * driving the pools frame by frame through the same sequence. */
static void bounce_against_reference(const char *name, const ma_patch patch[PB_CHANNELS],
                                     const pb_voice voice[PB_CHANNELS], const pb_sequence *sequence, unsigned loops,
                                     uint64_t want_play) {
    float *pcm = nullptr;
    size_t frames = 0;
    CHECK(pb_bounce_render(RATE, patch, nullptr, voice, sequence, loops, .5f, &pcm, &frames), "%s", name);
    if (!pcm) return;
    reference r = { .sequence = *sequence };
    bool built = true;
    for (unsigned c = 0; c < PB_CHANNELS; c++) built = ref_build(&r, c, &patch[c], &voice[c]) && built;
    CHECK(built, "%s: reference pools", name);
    uint64_t play = pb_sequence_frames(sequence, RATE, loops);
    CHECK(play == want_play, "%s: %llu frames of play, not %llu", name, (unsigned long long)play,
          (unsigned long long)want_play);
    ref_start(&r);
    bool stopped = false;
    size_t mismatch = 0;
    double energy = 0;
    float y[2];
    for (size_t at = 0; at < frames; at++) {
        if (!stopped && at % PERIOD == 0 && at >= play) {
            ref_stop(&r);
            stopped = true;
        }
        ref_frame(&r, y);
        if (memcmp(y, pcm + 2 * at, sizeof y)) mismatch++;
        energy += (double)y[0] * y[0];
    }
    CHECK(!mismatch && energy > 1, "%s differs from the reference in %zu of %zu frames (energy %g)", name, mismatch,
          frames, energy);
    printf("%s: %zu frames, PCM FNV %016llx\n", name, frames,
           (unsigned long long)tw_fnv1a64(pcm, frames * 2 * sizeof *pcm, 0));
    free(pcm);
    ref_free(&r);
}

static void test_bounce(void) {
    ma_patch patch[PB_CHANNELS];
    pb_voice voice[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) {
        patch[c] = ma_patch_prizma;
        voice[c] = pb_voice_default();
        voice[c].unison = 2;
        voice[c].detune_cents = 9;
        voice[c].character = .3f;
        voice[c].glide_s = .05f;
    }
    pb_pattern pattern = pb_builtin_pattern(PB_PAT_TIED_LINE, 50, 14000);
    pattern.swing = 30;
    pb_sequence sequence = pb_sequence_single(&pattern);
    pb_seq_run run;
    pb_seq_start(&run, &pattern, RATE);
    bounce_against_reference("bounce parity", patch, voice, &sequence, 2, pb_seq_step_frame(&run, 16));

    /* Two more channels: a 12-step bass on another patch and a muted
     * 16-step line.  A pass is the longest pattern that sounds: 12 steps. */
    patch[1] = ma_patch_dubina;
    voice[1] = pb_voice_default();
    voice[1].gain = 20;
    sequence.channel[1] = pb_builtin_pattern(PB_PAT_BASS, 38, 14000);
    sequence.channel[1].steps = 12;
    sequence.channel[1].swing = 30;
    sequence.channel[2] = pb_builtin_pattern(PB_PAT_ARPEGGIO, 62, 14000);
    sequence.channel[2].swing = 30;
    sequence.mute[2] = true;
    bounce_against_reference("bounce parity, channels", patch, voice, &sequence, 2, pb_seq_step_frame(&run, 24));
}

/* RK1: the engine plays the rack. A bypassed rack is bit-identical to no
 * rack, an active one changes the bounce and stays deterministic, and a
 * rack published through the mailbox reaches the audio thread: its channel
 * only. */
static void test_rack(void) {
    ma_patch patch[PB_CHANNELS];
    pb_voice voice[PB_CHANNELS];
    for (unsigned c = 0; c < PB_CHANNELS; c++) patch[c] = ma_patch_tepih, voice[c] = pb_voice_default();
    pb_pattern pattern = pb_builtin_pattern(PB_PAT_TIED_LINE, 48, 12000);
    pb_sequence sequence = pb_sequence_single(&pattern);
    ma_rack_spec bypass[PB_CHANNELS] = { 0 }, active[PB_CHANNELS] = { 0 }, elsewhere[PB_CHANNELS] = { 0 };
    ma_rack_slot_init(&bypass[0].slot[0], MA_RACK_EQ, 1);           /* neutral */
    ma_rack_slot_init(&bypass[0].slot[1], MA_RACK_UTILITY, 2);
    bypass[0].slot[1].enabled = false;
    bypass[0].slot[1].param[MA_UTILITY_GAIN_DB] = -12;
    ma_rack_slot_init(&active[0].slot[0], MA_RACK_EQ, 1);
    active[0].slot[0].param[MA_EQ_BAND_COUNT] = 1;
    active[0].slot[0].param[MA_EQ_BAND(0, MA_EQ_GAIN_DB)] = 9;
    active[0].slot[0].param[MA_EQ_BAND(0, MA_EQ_FREQ_HZ)] = 700;
    elsewhere[1] = active[0];                                       /* on a channel that plays nothing */
    float *none = nullptr, *by = nullptr, *on1 = nullptr, *on2 = nullptr, *other = nullptr;
    size_t n0 = 0, n1 = 0, n2 = 0, n3 = 0, n4 = 0;
    CHECK(pb_bounce_render(RATE, patch, nullptr, voice, &sequence, 1, .3f, &none, &n0)
          && pb_bounce_render(RATE, patch, bypass, voice, &sequence, 1, .3f, &by, &n1)
          && pb_bounce_render(RATE, patch, active, voice, &sequence, 1, .3f, &on1, &n2)
          && pb_bounce_render(RATE, patch, active, voice, &sequence, 1, .3f, &on2, &n3)
          && pb_bounce_render(RATE, patch, elsewhere, voice, &sequence, 1, .3f, &other, &n4), "rack bounces");
    if (none && by && on1 && on2 && other) {
        size_t bytes = n0 * 2 * sizeof *none;
        CHECK(n0 == n1 && !memcmp(none, by, bytes), "a bypassed rack changed the bounce");
        CHECK(n0 == n2 && memcmp(none, on1, bytes), "an active rack changed nothing");
        CHECK(n2 == n3 && !memcmp(on1, on2, bytes), "rack bounce not deterministic");
        CHECK(n0 == n4 && !memcmp(none, other, bytes), "a rack on a silent channel changed the bounce");
        printf("rack bounce: none %016llx, active %016llx\n", (unsigned long long)tw_fnv1a64(none, bytes, 0),
               (unsigned long long)tw_fnv1a64(on1, bytes, 0));
    }
    free(none);
    free(by);
    free(on1);
    free(on2);
    free(other);

    pb_engine *e = pb_engine_create(RATE, patch, voice, &sequence);
    CHECK(e != nullptr, "engine");
    if (!e) return;
    pb_engine_set_rack(e, 2, &active[0]);
    static float out[2 * PERIOD];
    pb_rt_process(e, out, PERIOD);
    const ma_rack *rack = &e->channel[2].rack;
    CHECK(rack->applies == 1 && rack->block[0].live && rack->block[0].kind == MA_RACK_EQ,
          "the mailbox rack did not reach the audio thread");
    CHECK(!e->channel[0].rack.applies && !e->channel[1].rack.applies && !e->channel[3].rack.applies,
          "a channel's rack reached another channel");
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        CHECK(e->channel[c].pool->pool.rack == &e->channel[c].rack, "channel %u's pool is not attached to its rack", c + 1);
    pb_engine_destroy(e);
}

int main(void) {
    test_parity();
    test_channels();
    test_bounce();
    test_rack();
    printf("pb_rt_test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
