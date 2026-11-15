/* White-box exactness gate: the eight-lane front-end math (ml_front_vco,
 * ml_front_math) against ma_voice_vco_frame, filter_envelope_amount,
 * effective_filter_cutoff, filter_prewarp and prepare_source_frame_guarded.
 * Random voices and controls, including paths the pool twin rarely reaches:
 * poly pressure, card character, extreme pitches, every interval, cross-mod
 * off and on, zero waveform levels and zero guards; the circuit's law off,
 * on, and mixed within a bank, with and without a card (a pool voice always
 * has one). Every output field compared bit for bit. Skips without AVX2. */
#include "../src/ma_lanes.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t seed = UINT64_C(0x4d41465230313031);
static float uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(tw_splitmix64(&seed) >> 40) * 0x1p-24f;
}
static float sometimes_zero(float lo, float hi) {
    return tw_splitmix64(&seed) % 4 == 0 ? 0.0f : uniform(lo, hi);
}
static uint32_t bits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
#define SAME(a, b, what) do { if (bits(a) != bits(b)) { \
    fprintf(stderr, "ma_front_lanes: %s mismatch, trial %u lane %u: %a vs %a\n", \
            what, trial, l, (double)(a), (double)(b)); return 1; } } while (0)

int main(void) {
#if defined(__x86_64__)
    if (!ma_lanes_supported()) { puts("ma_front_lanes: no AVX2; skipped"); return 0; }
    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    static ma_synth voice[MA_LANES];
    unsigned compared = 0, crossmod_off = 0, poly = 0, character = 0;
    unsigned law = 0, law_off = 0, card = 0, coupled = 0;
    for (unsigned trial = 0; trial < 50000; trial++) {
        ma_voice_front front[MA_LANES] = {};
        ma_source_frame expected[MA_LANES];
        ma_synth *lane[MA_LANES];
        float expected_cutoff[MA_LANES], expected_g[MA_LANES], g1[MA_LANES], g2[MA_LANES];
        float expected_base[MA_LANES];
        /* One bank in eight has the law off in every voice, one in eight
         * on in every voice; the rest are mixed. */
        unsigned bank = (unsigned)(tw_splitmix64(&seed) % 8);
        ml_front_in fx;
        ml_front_benign(&fx);
        ml_in in;
        ml_benign(&in);
        for (unsigned l = 0; l < MA_LANES; l++) {
            ma_synth *s = &voice[l];
            s->sample_rate_hz = rates[tw_splitmix64(&seed) % 4];
            s->velocity = (uint8_t)(1 + tw_splitmix64(&seed) % 127);
            s->filter_envelope.level = uniform(0.0f, 1.0f);
            s->character.smoother.current = sometimes_zero(0.0f, 1.0f);
            s->character.cutoff_octaves = uniform(-2.0f, 2.0f);
            s->character.vco2_cents = uniform(-60.0f, 60.0f);
            s->vco2_interval = (int)(tw_splitmix64(&seed) % 61) - 30;
            lane[l] = s;
            bool law_on = bank == 0 ? false : bank == 1 ? true : tw_splitmix64(&seed) % 2;
            s->vco_analog = law_on ? uniform(0.01f, 1.0f) : 0.0f;
            s->vco_dead_time = sometimes_zero(0.0f, 1.0f);
            s->vco_supply = sometimes_zero(0.0f, 1.0f);
            s->character.assigned = tw_splitmix64(&seed) % 4 != 0;
            s->character.temp_k = uniform(-2.0f, 2.0f);
            s->character.vco2_temp_k = uniform(-0.5f, 0.5f);
            s->character.walk_cents = uniform(-3.0f, 3.0f);
            for (int k = 0; k < 2; k++) {
                s->character.vco_extra_hz[k] = uniform(0.208f, 0.280f);
                s->character.vco_dead_us[k] = uniform(6.49f, 6.60f);
            }
            ma_render_controls *c = &front[l].controls;
            ma_vco_controls *v[2] = { &c->vco1, &c->vco2 };
            for (int k = 0; k < 2; k++) {
                v[k]->saw_level = sometimes_zero(0.0f, 1.0f);
                v[k]->pulse_level = sometimes_zero(0.0f, 1.0f);
                v[k]->triangle_level = sometimes_zero(0.0f, 1.0f);
                v[k]->sine_level = sometimes_zero(0.0f, 1.0f);
                v[k]->pulse_width = uniform(0.05f, 0.95f);
            }
            c->filter_env_amount = uniform(-1.0f, 1.0f);
            c->filter_keytrack = uniform(0.0f, 1.0f);
            c->filter_cutoff_hz = uniform(20.0f, 20000.0f);
            c->poly_pressure = sometimes_zero(0.0f, 1.0f);
            c->sync_amount = sometimes_zero(0.0f, 1.0f);
            c->sync_softness = uniform(0.0f, 1.0f);
            c->vco2_level = sometimes_zero(0.0f, 1.0f);
            c->noise_level = sometimes_zero(0.0f, 1.0f);
            c->crossmod_amount = sometimes_zero(0.0f, 1.0f);
            c->vco2_fine_cents = uniform(-100.0f, 100.0f);
            front[l].note_position = uniform(0.0f, 127.0f);
            front[l].base_hz = tw_splitmix64(&seed) % 8 == 0 ? uniform(0.0f, 60000.0f)
                                                             : uniform(8.0f, 4200.0f);
            g1[l] = sometimes_zero(0.0f, 1.0f);
            g2[l] = sometimes_zero(0.0f, 1.0f);
            front[l].pitch_delta = sometimes_zero(-14.0f, 14.0f);
            front[l].detune_cents = sometimes_zero(-100.0f, 100.0f);
            float amount = ma_voice_filter_envelope_amount(s, c);
            float modulation = s->filter_envelope.level * amount;
            expected_cutoff[l] = ma_voice_effective_filter_cutoff(s, c, amount, front[l].note_position);
            expected_g[l] = ma_filter_prewarp(expected_cutoff[l], s->sample_rate_hz);
            expected_base[l] = ma_voice_vco_frame(s, front[l].base_hz, front[l].note_position,
                                                  front[l].pitch_delta, front[l].detune_cents,
                                                  c->vco2_fine_cents);
            expected[l] = ma_voice_prepare_source_frame_guarded(s, c, expected_base[l], modulation,
                                                                g1[l], g2[l]);
            crossmod_off += c->crossmod_amount == 0.0f;
            poly += c->poly_pressure > 0.0f;
            character += s->character.smoother.current > 0.0f;
            law += law_on;
            law_off += !law_on;
            card += law_on && s->character.assigned;
            coupled += expected[l].couple_hz > 0.0f;
        }
        ml_front_vco(lane, front, 0xffu, &fx);
        for (unsigned l = 0; l < MA_LANES; l++) {
            SAME(front[l].base_hz, expected_base[l], "base_hz");
            ml_front_gather(&fx, l, lane[l], &front[l], g1[l], g2[l]);
        }
        float cutoff[MA_LANES];
        ml_front_math(&fx, &in, cutoff);
        for (unsigned l = 0; l < MA_LANES; l++) {
            const ma_source_frame *e = &expected[l];
            SAME(cutoff[l], expected_cutoff[l], "cutoff");
            SAME(in.filter_g[l], expected_g[l], "filter_g");
            const ma_vco_controls *ev[2] = { &e->vco1, &e->vco2 };
            float (*got[2])[MA_LANES] = { in.c1, in.c2 };
            for (int k = 0; k < 2; k++) {
                SAME(got[k][0][l], ev[k]->saw_level, "saw");
                SAME(got[k][1][l], ev[k]->pulse_level, "pulse");
                SAME(got[k][2][l], ev[k]->triangle_level, "triangle");
                SAME(got[k][3][l], ev[k]->sine_level, "sine");
                SAME(got[k][4][l], ev[k]->pulse_width, "pulse_width");
            }
            if ((unsigned)in.act1[l] != e->vco1_active || (unsigned)in.act2[l] != e->vco2_active) {
                fprintf(stderr, "ma_front_lanes: active mismatch, trial %u lane %u\n", trial, l);
                return 1;
            }
            SAME(in.div1[l], e->vco1_divisor, "vco1_divisor");
            SAME(in.div2[l], e->vco2_divisor, "vco2_divisor");
            SAME(in.mixdiv[l], e->mixer_divisor, "mixer_divisor");
            SAME(in.rate[l], e->rate_hz, "rate");
            SAME(in.v1hz[l], e->vco1_hz, "vco1_hz");
            SAME(in.v1step[l], e->vco1_step, "vco1_step");
            SAME(in.v2hz[l], e->vco2_hz, "vco2_hz");
            SAME(in.v2step[l], e->vco2_step, "vco2_step");
            SAME(in.v1seed[l], e->vco1_seed_step, "vco1_seed_step");
            SAME(in.v1tg[l], e->vco1_triangle_gain, "vco1_triangle_gain");
            SAME(in.v1ts[l], e->vco1_triangle_step, "vco1_triangle_step");
            SAME(in.v2tg[l], e->vco2_triangle_gain, "vco2_triangle_gain");
            SAME(in.v2ts[l], e->vco2_triangle_step, "vco2_triangle_step");
            SAME(in.envr[l], e->envelope_ratio, "envelope_ratio");
            SAME(in.sync[l], e->sync, "sync");
            SAME(in.skew1[l], e->vco1_skew, "vco1_skew");
            SAME(in.skew2[l], e->vco2_skew, "vco2_skew");
            SAME(in.swell1[l], e->vco1_swell, "vco1_swell");
            SAME(in.swell2[l], e->vco2_swell, "vco2_swell");
            SAME(in.bow[l], e->saw_bow, "saw_bow");
            for (int k = 0; k < 2; k++) {
                SAME(in.ramp_bow[k][l], e->vco1_ramp_bow[k], "vco1_ramp_bow");
                SAME(in.ramp_bow[2 + k][l], e->vco2_ramp_bow[k], "vco2_ramp_bow");
            }
            SAME(in.tri_buffer[l], e->tri_buffer, "tri_buffer");
            SAME(in.couple[0][l], e->couple_hz, "couple_hz");
            if (e->couple_hz > 0.0f) {       /* the row is read only when it couples */
                SAME(in.couple[1][l], e->couple_p, "couple_p");
                SAME(in.couple[2][l], e->couple_q, "couple_q");
                SAME(in.couple[3][l], e->couple_jump, "couple_jump");
            }
            if (in.v1q[l] != e->vco1_step_q48 || in.v2q[l] != e->vco2_step_q48) {
                fprintf(stderr, "ma_front_lanes: q48 mismatch, trial %u lane %u\n", trial, l);
                return 1;
            }
            compared++;
        }
    }
    assert(compared == 400000 && crossmod_off > 1000 && poly > 1000 && character > 1000);
    assert(law > 100000 && law_off > 100000 && card > 50000 && law - card > 20000 && coupled > 20000);
    printf("ma_front_lanes: %u lane-frames bit-identical (circuit frame, cutoff, prewarp, "
           "source frame); %u cross-mod off, %u poly pressure, %u character; the law on in %u "
           "(%u with a card, %u coupling), off in %u\n", compared, crossmod_off, poly, character,
           law, card, coupled, law_off);
#else
    puts("ma_front_lanes: not x86-64; skipped");
#endif
    return 0;
}
