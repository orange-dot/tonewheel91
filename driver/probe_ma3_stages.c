/* MA3-PERF stage probe (2026-11-06): where do the vector engine's cycles go?
 * White-box: includes ma_voice.c, ma_lanes.c and ma_pool.c and re-runs the
 * vector render path (ma_voice_pool_render -> pool_tick -> ma_lanes_tick ->
 * ml_kernel) with a TSC read between stages. The instrumented copy must
 * produce the same frames and slot state as the real API on a twin pool; the
 * probe checks that bit-for-bit and fails otherwise. Stage shares are TSC ticks
 * on one quiet core (relative, so the constant-rate TSC suffices).
 *
 *   probe_ma3_stages --run   ten 8-slot wet cases (pin with taskset)
 *
 * Not shipping code; not part of `make test`. */
#define _GNU_SOURCE
#include "../src/ma_voice.c"
#include "../src/ma_lanes.c"
#include "../src/ma_pool.c"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#define AVX2 __attribute__((target("avx2")))
enum { FRAMES = 128, WARMUP = 256, BLOCKS = 1024 };
/* Front-end sub-stages run stage-major across the gathered voices: each voice
 * still performs its own operations in order, and voices share no state. */
enum { ST_GATHER, ST_CONTROL, ST_FE_CONTROLS, ST_FE_PITCH, ST_FE_BCS, ST_FE_ENVELOPES,
       ST_FE_CUTOFF, ST_FE_NOISE, ST_FE_SOURCE, ST_FE_MOZAIK, ST_FE_RASTER, ST_FILL,
       ST_OSC, ST_DECIMATE, ST_FILTER, ST_BACK, ST_SLOTS, ST_OUTPUT, ST_COUNT };
static const char *const STAGE[ST_COUNT] = {
    "gather_scatter", "control_pan", "fe_controls", "fe_pitch", "fe_bcs", "fe_envelopes",
    "fe_cutoff_prewarp", "fe_noise", "fe_source_frame", "fe_mozaik", "fe_raster", "lane_fill",
    "osc_block", "decimators", "filter_fir7", "back_end", "slot_loop", "output_effects" };
static uint64_t stage[ST_COUNT];
static inline uint64_t tsc(void) { _mm_lfence(); uint64_t t = __rdtsc(); _mm_lfence(); return t; }

/* ml_kernel, split by stage */
AVX2 static void kernel_timed(ma_voice_lanes *b, const ml_in *in, float filtered[MA_LANES]) {
    uint64_t t0 = tsc();
    ml_osc o1 = ml_osc_ld(b, 0), o2 = ml_osc_ld(b, 1);
    ml_vco c1 = ml_vco_ld(in->c1), c2 = ml_vco_ld(in->c2);
    ml_f guard_hz = ml_ld(in->guard_hz);
    ml_set_guard_target(&o2, ml_sel(_mm256_setzero_ps(), _mm256_set1_ps(1.0f),
                                    ml_lt(ml_ld(in->v2hz), guard_hz)), in);
    ml_f source[8];
    for (int substep = 0; substep < 8; substep++)
        source[substep] = ml_substep(&o1, &o2, in, &c1, &c2, guard_hz);
    ml_next_guard_gain(&o1);
    ml_next_guard_gain(&o2);
    ml_osc_st(b, 0, &o1);
    ml_osc_st(b, 1, &o2);
    uint64_t t1 = tsc();
    ml_f at_2x[2];
    int substep = 0;
    for (int step_2x = 0; step_2x < 2; step_2x++) {
        for (int step_4x = 0; step_4x < 2; step_4x++) {
            ml_push31(b->history[0], &b->history_pos[0], source[substep++]);
            ml_push31(b->history[0], &b->history_pos[0], source[substep++]);
            ml_push31(b->history[1], &b->history_pos[1],
                      ml_decimate31(b->history[0], b->history_pos[0]));
        }
        at_2x[step_2x] = ml_decimate31(b->history[1], b->history_pos[1]);
    }
    uint64_t t2 = tsc();
    ml_f state[4] = { ml_ld(b->ladder_state[0]), ml_ld(b->ladder_state[1]),
                      ml_ld(b->ladder_state[2]), ml_ld(b->ladder_state[3]) };
    ml_f y4 = ml_ld(b->y4_guess);
    ml_f pressure = ml_ld(in->pressure), g = ml_ld(in->filter_g);
    ml_f drive = ml_ld(in->drive), resonance = ml_ld(in->resonance);
    for (int step = 0; step < 2; step++) {
        ml_f mixed = ml_mix(in, at_2x[step], step);
        ml_f pressured = ml_pressure(mixed, pressure);
        _mm256_storeu_ps(b->pressure_input, mixed);
        _mm256_storeu_ps(b->pressure_output, pressured);
        ml_push7(b->output_history, &b->output_pos,
                 ml_ladder(b, state, &y4, pressured, g, drive, resonance));
    }
    for (int s = 0; s < 4; s++) _mm256_storeu_ps(b->ladder_state[s], state[s]);
    _mm256_storeu_ps(b->y4_guess, y4);
    _mm256_storeu_ps(filtered, ml_decimate7(b->output_history, b->output_pos));
    uint64_t t3 = tsc();
    stage[ST_OSC] += t1 - t0; stage[ST_DECIMATE] += t2 - t1; stage[ST_FILTER] += t3 - t2;
}

/* ma_lanes_tick (front A, vector BCS, front B), split by sub-stage */
static void lanes_tick_timed(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                             const float detune[MA_LANES], float out[MA_LANES]) {
    ma_voice_front front[MA_LANES];
    ml_in in;
    unsigned act = b->active;
#define EACH for (unsigned l = 0; l < MA_LANES; l++) if (act >> l & 1u)
    uint64_t t = tsc(), u;
    /* front A: controls */
    EACH fill_render_controls(voice[l], &front[l].controls);
    u = tsc(); stage[ST_FE_CONTROLS] += u - t; t = u;
    /* front A: character, glide, LFO, pitch */
    EACH {
        ma_synth *sv = voice[l]; ma_voice_front *f = &front[l];
        ma_render_controls *controls = &f->controls;
        float character = next_character(sv);
        float note_position = next_glide_note(sv);
        float pitch_delta = controls->pitch_bend_semitones + next_lfo(sv, controls);
        float base_hz = note_position_hz(note_position);
        if (detune[l] != 0.0f) base_hz *= octave_ratio_small(detune[l] / 1200.0f);
        if (pitch_delta != 0.0f) base_hz *= octave_ratio_small(pitch_delta / 12.0f);
        if (character > 0.0f)
            base_hz *= octave_ratio_small(character
                * (sv->character.vco1_cents + sv->character.walk_cents) / 1200.0f);
        f->character = character; f->note_position = note_position; f->base_hz = base_hz;
    }
    u = tsc(); stage[ST_FE_PITCH] += u - t; t = u;
    /* BCS, eight lanes */
    unsigned bcs_on = 0;
    EACH if (front[l].controls.bcs_amount > 0.0f) bcs_on |= 1u << l;
    float bcs[MA_LANES] = {};
    if (bcs_on) ml_bcs(voice, front, b->y4_guess, bcs_on, bcs);
    u = tsc(); stage[ST_FE_BCS] += u - t; t = u;
    /* front B: BCS effect on controls, envelopes */
    EACH {
        ma_synth *sv = voice[l]; ma_render_controls *controls = &front[l].controls;
        if (controls->bcs_amount > 0.0f) {
            float crossmod_depth = 0.10f + 0.20f * controls->bcs_regime;
            float resonance_depth = 0.08f + 0.08f * controls->bcs_regime;
            controls->crossmod_amount = clamp_signal(
                controls->crossmod_amount + controls->bcs_amount * crossmod_depth * bcs[l], 0.0f, 1.0f);
            controls->filter_resonance = clamp_signal(
                controls->filter_resonance + controls->bcs_amount * resonance_depth * bcs[l], 0.0f, 1.0f);
        }
        ma_adsr filter_adsr = character_adsr(sv->filter_adsr, sv->character.filter_time_bias, front[l].character);
        ma_adsr amp_adsr = character_adsr(sv->amp_adsr, sv->character.amp_time_bias, front[l].character);
        (void)render_envelope(&sv->filter_envelope, filter_adsr, sv->sample_rate_hz);
        (void)render_envelope(&sv->amp_envelope, amp_adsr, sv->sample_rate_hz);
    }
    u = tsc(); stage[ST_FE_ENVELOPES] += u - t; t = u;
    /* front B: cutoff and prewarp */
    float filter_modulation[MA_LANES];
    EACH {
        ma_synth *sv = voice[l]; ma_voice_front *f = &front[l];
        float envelope_amount = filter_envelope_amount(sv, &f->controls);
        filter_modulation[l] = sv->filter_envelope.level * envelope_amount;
        sv->filter_cutoff_effective_hz = effective_filter_cutoff(sv, &f->controls, envelope_amount, f->note_position);
        f->filter_g = filter_prewarp(sv->filter_cutoff_effective_hz, sv->sample_rate_hz);
        f->guard1 = b->guard_gain[0][l];
        f->guard2 = b->guard_gain[1][l];
    }
    u = tsc(); stage[ST_FE_CUTOFF] += u - t; t = u;
    EACH front[l].noise = next_noise(&voice[l]->noise_state);
    u = tsc(); stage[ST_FE_NOISE] += u - t; t = u;
    EACH front[l].source = ma_voice_prepare_source_frame_guarded(voice[l], &front[l].controls, front[l].base_hz,
                                                        filter_modulation[l], front[l].guard1, front[l].guard2);
    u = tsc(); stage[ST_FE_SOURCE] += u - t; t = u;
    /* mozaik and raster: disjoint state, so mozaik's two steps may precede raster's */
    EACH {
        ma_synth *sv = voice[l]; ma_voice_front *f = &front[l];
        for (int at_2x = 0; at_2x < 2; at_2x++) {
            f->mozaik_gain[at_2x] = sv->mozaik.guard_gain;
            f->mozaik[at_2x] = render_mozaik(sv, &f->controls, f->base_hz, 2.0f * sv->sample_rate_hz);
            (void)next_mozaik_guard_gain(&sv->mozaik);
        }
    }
    u = tsc(); stage[ST_FE_MOZAIK] += u - t; t = u;
    EACH {
        ma_synth *sv = voice[l]; ma_voice_front *f = &front[l];
        for (int at_2x = 0; at_2x < 2; at_2x++)
            f->raster[at_2x] = f->controls.raster_mix > 0.0f
                             ? ma_voice_render_raster(sv, &f->controls, f->base_hz, 2.0f * sv->sample_rate_hz) : 0.0f;
    }
    u = tsc(); stage[ST_FE_RASTER] += u - t; t = u;
    ml_benign(&in);
    EACH ml_fill(&in, l, &front[l], voice[l]);
    u = tsc(); stage[ST_FILL] += u - t; t = u;
    float filtered[MA_LANES];
    kernel_timed(b, &in, filtered);
    t = tsc();
    for (unsigned l = 0; l < MA_LANES; l++)
        out[l] = act >> l & 1u ? voice_back_end(voice[l], &front[l], filtered[l]) : 0.0f;
    stage[ST_BACK] += tsc() - t;
#undef EACH
}

/* pool_tick(p, true), split by stage */
static ma_frame pool_tick_timed(ma_voice_pool *p) {
    uint64_t t0 = tsc();
    ma_voice_tick_controls(&p->control);
    ma_synth *control = &p->control;
    float width = control->width == 0 ? 0 : control->smoothers.width.current;
    update_pan(p, width, control->smoothers.spatial_dispersion.current);
    float left = 0, right = 0;
    bool live_group[MA_POOL_CAPACITY] = { false };
    float voice_out[MA_POOL_CAPACITY];
    stage[ST_CONTROL] += tsc() - t0;
    for (unsigned b = 0; b * MA_LANES < p->config.limit; b++) {
        if (!p->lanes[b].active) continue;
        ma_synth *voice[MA_LANES];
        float detune[MA_LANES];
        for (unsigned l = 0; l < MA_LANES; l++) {
            unsigned i = b * MA_LANES + l;
            bool here = i < p->config.limit;
            voice[l] = &p->slot[here ? i : 0].voice;
            detune[l] = here ? p->slot[i].detune_cents : 0.0f;
        }
        lanes_tick_timed(&p->lanes[b], voice, detune, &voice_out[b * MA_LANES]);
    }
    uint64_t t2 = tsc();
    for (unsigned i = 0; i < p->config.limit; i++) {
        ma_pool_slot *s = &p->slot[i];
        float raw = s->awake ? voice_out[i] * p->group_gain : 0;
        if (s->transition_remaining) {
            float old = (float)s->transition_remaining / (float)p->transition_frames;
            raw = s->transition_from * old + raw * (1 - old);
            s->transition_remaining--;
        }
        s->last_contribution = raw;
        left += raw * p->pan[i].left;
        right += raw * p->pan[i].right;
        if (s->awake && p->group[s->group].phase == MA_CARD_RELEASED
            && s->voice.amp_envelope.stage == MA_ENVELOPE_IDLE) {
            s->awake = false;
            p->diagnostics.active_slots--;
            p->diagnostics.sleeps++;
            ma_voice_lanes *bank = &p->lanes[i / MA_LANES];
            ma_lanes_scatter(bank, i % MA_LANES, &s->voice);
            bank->active &= (uint8_t)~(1u << (i % MA_LANES));
        }
        if (s->awake) live_group[s->group] = true;
    }
    for (unsigned g = 0; g < p->config.limit; g++)
        if (!live_group[g]) p->group[g] = (ma_pool_group){ 0 };
    float mid = (.5f * left + .5f * right) * .125f;
    float side = (.5f * left - .5f * right) * .125f
        * (1 - control->smoothers.crossfeed.current);
    if (width == 0 || control->smoothers.crossfeed.current == 1) {
        float dc = .5f * p->output.dc_lp_left + .5f * p->output.dc_lp_right;
        p->output.dc_lp_left = dc;
        p->output.dc_lp_right = dc;
    }
    uint64_t t3 = tsc();
    ma_frame dry = ma_output_render(&p->output, mid, side,
        control->smoothers.body_drive.current,
        control->smoothers.body_load_ratio.current, control->master_level);
    ma_frame wet = ma_effects_tick(&p->effects, dry);
    uint64_t t4 = tsc();
    stage[ST_SLOTS] += t3 - t2; stage[ST_OUTPUT] += t4 - t3;
    return wet;
}

/* ma_voice_pool_render's vector path, gather/scatter timed */
static void render_timed(ma_voice_pool *p, ma_frame *frames, size_t count) {
    uint64_t t0 = tsc();
    unsigned banks = (p->config.limit + MA_LANES - 1u) / MA_LANES;
    for (unsigned b = 0; b < banks; b++) {
        ma_lanes_begin(&p->lanes[b]);
        for (unsigned l = 0; l < MA_LANES; l++) {
            unsigned i = b * MA_LANES + l;
            if (i < p->config.limit && p->slot[i].awake) ma_lanes_gather(&p->lanes[b], l, &p->slot[i].voice);
        }
    }
    stage[ST_GATHER] += tsc() - t0;
    for (size_t i = 0; i < count; i++) frames[i] = pool_tick_timed(p);
    uint64_t t1 = tsc();
    for (unsigned b = 0; b < banks; b++)
        for (unsigned l = 0; l < MA_LANES; l++)
            if (p->lanes[b].active >> l & 1u)
                ma_lanes_scatter(&p->lanes[b], l, &p->slot[b * MA_LANES + l].voice);
    stage[ST_GATHER] += tsc() - t1;
}

static ma_voice_pool timed, reference;
static void setup(ma_voice_pool *p, const ma_patch *base, unsigned bcs) {
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = 8; c.unison_count = 1; c.engine = MA_POOL_ENGINE_AUTO;
    ma_patch patch = *base;
    patch.bcs_amount = bcs ? ma_patch_granica.bcs_amount : 0;
    patch.bcs_regime = ma_patch_granica.bcs_regime;
    if (!ma_voice_pool_init(p, 48000, &c, &patch) || !p->lanes_enabled) { fputs("no vector lanes\n", stderr); exit(1); }
    for (unsigned n = 0; n < 8; n++) if (!ma_voice_pool_note_on(p, 0, (uint8_t)(36 + n), 100)) exit(1);
    ma_effects_set_chorus(&p->effects, (ma_chorus_controls){ true, .30f, .35f, .25f });
    ma_effects_set_reverb(&p->effects, (ma_reverb_controls){ true, .25f, .65f, .55f });
}

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--run")) { puts("Usage: probe_ma3_stages --run"); return argc == 1 ? 0 : 1; }
    const ma_patch *patches[] = { &ma_patch_tepih, &ma_patch_lead, &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica };
    const char *names[] = { "Tepih", "Lead", "Raster", "Prizma", "Granica" };
    /* timer cost: one tsc() pair, median of many */
    uint64_t pair = UINT64_MAX;
    for (int i = 0; i < 100000; i++) { uint64_t a = tsc(), z = tsc(); if (z - a < pair) pair = z - a; }
    printf("# tsc pair minimum %" PRIu64 " ticks; reads per sample: 12 + 4 per bank\n", pair);
    printf("patch,bcs,total_ticks_per_block,untimed_ticks_per_block");
    for (int s = 0; s < ST_COUNT; s++) printf(",%s", STAGE[s]);
    puts(",identical");
    static ma_frame a[FRAMES], b[FRAMES];
    for (unsigned p = 0; p < 5; p++)
        for (unsigned bcs = 0; bcs < 2; bcs++) {
            setup(&timed, patches[p], bcs);
            setup(&reference, patches[p], bcs);
            for (unsigned k = 0; k < WARMUP; k++) {
                render_timed(&timed, a, FRAMES);
                ma_voice_pool_render(&reference, b, FRAMES);
            }
            memset(stage, 0, sizeof stage);
            bool same = true;
            uint64_t total = 0, untimed = 0;
            for (unsigned k = 0; k < BLOCKS; k++) {
                uint64_t t0 = tsc();
                render_timed(&timed, a, FRAMES);
                uint64_t t1 = tsc();
                ma_voice_pool_render(&reference, b, FRAMES);
                uint64_t t2 = tsc();
                total += t1 - t0; untimed += t2 - t1;
                same = same && !memcmp(a, b, sizeof a);
            }
            same = same && !memcmp(timed.slot, reference.slot, sizeof timed.slot);
            printf("%s,%u,%.0f,%.0f", names[p], bcs, (double)total / BLOCKS, (double)untimed / BLOCKS);
            for (int s = 0; s < ST_COUNT; s++) printf(",%.0f", (double)stage[s] / BLOCKS);
            printf(",%s\n", same ? "yes" : "NO");
            if (!same) return 1;
            fflush(stdout);
        }
    return 0;
}
