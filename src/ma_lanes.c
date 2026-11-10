/* Mamut Analog vector lanes: eight pool voices per AVX2 vector.
 *
 * The DSP is the shared kernels (ma_osc_kernel.h, ma_filter_kernel.h,
 * ma_sources_kernel.h, ma_glas_kernel.h) at ML_W 8; the one-voice tick runs
 * the same kernels at ML_W 1, so each lane performs its voice's float
 * operations in the same order and gives the same bits. This file holds what
 * only the lanes need: CPU detection, gathering awake pool slots into the
 * lane-major bank and scattering them back, the per-lane front and back ends
 * around the kernels, and the loads and stores between the bank and the
 * kernels' state. The build targets x86-64-v3 (AVX2, FMA); the pool still
 * enters the lanes only after runtime detection. */
#include "ma_raster_table.h"
#include "ma_voice_impl.h"

#if !defined(MA_SOURCE_EVIDENCE) && defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>

bool ma_lanes_supported(void) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
    if (!(c & bit_OSXSAVE) || !(c & bit_AVX) || !(c & bit_FMA)) return false;
    unsigned xcr0 = 0, xcr0_high = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xcr0_high) : "c"(0));
    (void)xcr0_high;
    if ((xcr0 & 6u) != 6u) return false;          /* OS saves XMM and YMM */
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
    return (b & bit_AVX2) != 0;
}

void ma_lanes_begin(ma_voice_lanes *b) {
    *b = (ma_voice_lanes){};
}

void ma_lanes_gather(ma_voice_lanes *b, unsigned lane, const ma_synth *s) {
    const ma_oscillator *osc[2] = { &s->oscillator1, &s->oscillator2 };
    for (int k = 0; k < 2; k++) {
        b->phase_q48[k][lane] = osc[k]->phase_q48;
        b->triangle[k][lane] = osc[k]->triangle;
        b->sync_residual[k][lane] = osc[k]->sync_residual;
        b->guard_gain[k][lane] = osc[k]->guard_gain;
        b->guard_target[k][lane] = osc[k]->guard_target;
        b->guard_step[k][lane] = osc[k]->guard_step;
        b->guard_frames[k][lane] = osc[k]->guard_frames;
        b->triangle_initialized[k][lane] = osc[k]->triangle_initialized ? -1 : 0;
        uint8_t own = s->oversample_history_pos[k];
        b->own_history_pos[k][lane] = own;
        for (int j = 0; j < 31; j++)
            b->history[k][j][lane] = s->oversample_history[k][(own + j) % 31];
    }
    b->sync_run_q48[lane] = s->oscillator2.sync_run_q48;
    for (int k = 0; k < 3; k++) b->sync_carry[k][lane] = s->oscillator2.sync_carry[k];
    uint8_t own = s->ladder.output_pos;
    b->own_output_pos[lane] = own;
    for (int j = 0; j < 7; j++)
        b->output_history[j][lane] = s->ladder.output_history[(own + j) % 7];
    for (int k = 0; k < 4; k++) b->ladder_state[k][lane] = s->ladder.state[k];
    b->y4_guess[lane] = s->ladder.y4_guess;
    b->pressure_input[lane] = s->ladder.pressure_input;
    b->pressure_output[lane] = s->ladder.pressure_output;
    b->reset_count[lane] = s->ladder.reset_count;
    const ma_glas *g = &s->glas;
    for (int i = 0; i < MA_FORMANTS; i++) {
        b->glas_s1[i][lane] = g->svf[i].s1;
        b->glas_s2[i][lane] = g->svf[i].s2;
        b->glas_g[i][lane] = g->g[i];
        b->glas_k[i][lane] = g->k[i];
        for (int j = 0; j < MA_GRAINS_PER_FORMANT; j++) {
            const ma_grain *q = &g->grain[i][j];
            int x = i * MA_GRAINS_PER_FORMANT + j;
            b->grain_phase[x][lane] = q->phase;
            b->grain_step[x][lane] = q->step;
            b->grain_envelope[x][lane] = q->envelope;
            b->grain_decay[x][lane] = q->decay;
            b->grain_attack_pos[x][lane] = q->attack_pos;
            b->grain_attack_step[x][lane] = q->attack_step;
            b->grain_gain[x][lane] = q->gain;
            b->grain_active[x][lane] = q->active ? -1 : 0;
        }
    }
    b->glas_epoch[lane] = g->coefficient_epoch;
    b->active |= (uint8_t)(1u << lane);
}

void ma_lanes_scatter(const ma_voice_lanes *b, unsigned lane, ma_synth *s) {
    ma_oscillator *osc[2] = { &s->oscillator1, &s->oscillator2 };
    for (int k = 0; k < 2; k++) {
        osc[k]->phase_q48 = b->phase_q48[k][lane];
        osc[k]->triangle = b->triangle[k][lane];
        osc[k]->sync_residual = b->sync_residual[k][lane];
        osc[k]->guard_gain = b->guard_gain[k][lane];
        osc[k]->guard_target = b->guard_target[k][lane];
        osc[k]->guard_step = b->guard_step[k][lane];
        osc[k]->guard_frames = (uint16_t)b->guard_frames[k][lane];
        osc[k]->triangle_initialized = b->triangle_initialized[k][lane] != 0;
        uint8_t own = b->own_history_pos[k][lane];
        for (int j = 0; j < 31; j++)
            s->oversample_history[k][(own + j) % 31] = b->history[k][j][lane];
        s->oversample_history_pos[k] = (uint8_t)((own + b->history_pos[k]) % 31);
    }
    s->oscillator2.sync_run_q48 = b->sync_run_q48[lane];
    for (int k = 0; k < 3; k++) s->oscillator2.sync_carry[k] = b->sync_carry[k][lane];
    uint8_t own = b->own_output_pos[lane];
    for (int j = 0; j < 7; j++)
        s->ladder.output_history[(own + j) % 7] = b->output_history[j][lane];
    s->ladder.output_pos = (uint8_t)((own + b->output_pos) % 7);
    for (int k = 0; k < 4; k++) s->ladder.state[k] = b->ladder_state[k][lane];
    s->ladder.y4_guess = b->y4_guess[lane];
    s->ladder.pressure_input = b->pressure_input[lane];
    s->ladder.pressure_output = b->pressure_output[lane];
    s->ladder.reset_count = b->reset_count[lane];
    ma_glas *g = &s->glas;
    for (int i = 0; i < MA_FORMANTS; i++) {
        g->svf[i].s1 = b->glas_s1[i][lane];
        g->svf[i].s2 = b->glas_s2[i][lane];
        for (int j = 0; j < MA_GRAINS_PER_FORMANT; j++) {
            ma_grain *q = &g->grain[i][j];
            int x = i * MA_GRAINS_PER_FORMANT + j;
            q->phase = b->grain_phase[x][lane];
            q->step = b->grain_step[x][lane];
            q->envelope = b->grain_envelope[x][lane];
            q->decay = b->grain_decay[x][lane];
            q->attack_pos = b->grain_attack_pos[x][lane];
            q->attack_step = b->grain_attack_step[x][lane];
            q->gain = b->grain_gain[x][lane];
            q->active = b->grain_active[x][lane] != 0;
        }
    }
}

#define ML_W 8
#include "ma_lane_ops.h"

/* Per-sample lane inputs: the front end's products for each lane. */
typedef struct {
    alignas(32) float c1[5][MA_LANES], c2[5][MA_LANES];   /* saw pulse tri sine pw */
    alignas(32) float div1[MA_LANES], div2[MA_LANES], mixdiv[MA_LANES], rate[MA_LANES];
    alignas(32) float v1hz[MA_LANES], v1step[MA_LANES], v2hz[MA_LANES], v2step[MA_LANES];
    /* triangle integrator gain and unit-pulse increment */
    alignas(32) float v1tg[MA_LANES], v1ts[MA_LANES], v2tg[MA_LANES], v2ts[MA_LANES];
    alignas(32) float v1seed[MA_LANES];                   /* unmodulated VCO1 step */
    alignas(32) float envr[MA_LANES], sync[MA_LANES], base[MA_LANES], noise[MA_LANES];
    alignas(32) float g1[MA_LANES], g2[MA_LANES], crossmod[MA_LANES], v2level[MA_LANES];
    alignas(32) float noiselevel[MA_LANES], guard_hz[MA_LANES], guard_frames_f[MA_LANES];
    alignas(32) float raster_mix[MA_LANES], mozaik_mix[MA_LANES], pressure[MA_LANES];
    alignas(32) float drive[MA_LANES], resonance[MA_LANES], filter_g[MA_LANES];
    alignas(32) float mozaik_gain[2][MA_LANES], mozaik[2][MA_LANES], raster[2][MA_LANES];
    /* Glas formant grains (ma-constants.md 10C): this frame's grain mix and
     * its two 2x samples, rendered per lane by ma_glas_prepare. */
    alignas(32) float grain_mix[MA_LANES], grain[2][MA_LANES];
    alignas(32) uint64_t v1q[MA_LANES], v2q[MA_LANES];
    alignas(32) int32_t act1[MA_LANES], act2[MA_LANES], guard_frames_i[MA_LANES];
} ml_in;

static void ml_benign(ml_in *in) {
    *in = (ml_in){};
    for (int l = 0; l < MA_LANES; l++) {
        in->div1[l] = in->div2[l] = in->mixdiv[l] = in->rate[l] = 1.0f;
        in->guard_frames_f[l] = 1.0f;
        in->guard_frames_i[l] = 1;
    }
}

/* Kept for white-box probes; the lane tick writes lane inputs directly. */
[[maybe_unused]] static void ml_fill(ml_in *in, unsigned l, const ma_voice_front *f,
                    const ma_synth *s) {
    const ma_vco_controls *v[2] = { &f->source.vco1, &f->source.vco2 };
    float (*c[2])[MA_LANES] = { in->c1, in->c2 };
    for (int k = 0; k < 2; k++) {
        c[k][0][l] = v[k]->saw_level;
        c[k][1][l] = v[k]->pulse_level;
        c[k][2][l] = v[k]->triangle_level;
        c[k][3][l] = v[k]->sine_level;
        c[k][4][l] = v[k]->pulse_width;
    }
    in->div1[l] = f->source.vco1_divisor;
    in->div2[l] = f->source.vco2_divisor;
    in->mixdiv[l] = f->source.mixer_divisor;
    in->rate[l] = f->source.rate_hz;
    in->v1hz[l] = f->source.vco1_hz;
    in->v1step[l] = f->source.vco1_step;
    in->v1q[l] = f->source.vco1_step_q48;
    in->v1seed[l] = f->source.vco1_seed_step;
    in->v1tg[l] = f->source.vco1_triangle_gain;
    in->v1ts[l] = f->source.vco1_triangle_step;
    in->v2tg[l] = f->source.vco2_triangle_gain;
    in->v2ts[l] = f->source.vco2_triangle_step;
    in->v2hz[l] = f->source.vco2_hz;
    in->v2step[l] = f->source.vco2_step;
    in->v2q[l] = f->source.vco2_step_q48;
    in->envr[l] = f->source.envelope_ratio;
    in->sync[l] = f->source.sync;
    in->act1[l] = (int32_t)f->source.vco1_active;
    in->act2[l] = (int32_t)f->source.vco2_active;
    in->base[l] = f->base_hz;
    in->noise[l] = f->noise;
    in->g1[l] = f->guard1;
    in->g2[l] = f->guard2;
    in->crossmod[l] = f->controls.crossmod_amount;
    in->v2level[l] = f->controls.vco2_level;
    in->noiselevel[l] = f->controls.noise_level;
    in->guard_hz[l] = 0.45f * s->sample_rate_hz;
    uint16_t frames = smoothing_frames(s->sample_rate_hz);
    in->guard_frames_i[l] = frames;
    in->guard_frames_f[l] = (float)frames;
    in->raster_mix[l] = f->controls.raster_mix;
    in->mozaik_mix[l] = f->controls.mozaik_mix;
    in->pressure[l] = f->controls.mixer_pressure;
    in->drive[l] = f->controls.filter_drive;
    in->resonance[l] = f->controls.filter_resonance;
    in->filter_g[l] = f->filter_g;
    for (int k = 0; k < 2; k++) {
        in->mozaik_gain[k][l] = f->mozaik_gain[k];
        in->mozaik[k][l] = f->mozaik[k];
        in->raster[k][l] = f->raster[k];
    }
}

static inline ml_f ml_ld(const float *a) { return _mm256_loadu_ps(a); }

/* The oscillators, the filter stage, the raster and BCS sources and the
 * Glas DSP, shared with the one-voice tick in ma_osc.c, ma_filter.c,
 * ma_sources.c and ma_glas.c. */
#include "ma_osc_kernel.h"
#include "ma_filter_kernel.h"
#include "ma_sources_kernel.h"
#include "ma_glas_kernel.h"

/* The kernel's oscillator state, from the lane bank and back. */
static ml_osc ml_osc_load(const ma_voice_lanes *b, int k) {
    return (ml_osc){
        .phase_q48 = ml_u64_ld(b->phase_q48[k]),
        .triangle = ml_ld(b->triangle[k]),
        .sync_residual = ml_ld(b->sync_residual[k]),
        .guard_gain = ml_ld(b->guard_gain[k]),
        .guard_target = ml_ld(b->guard_target[k]),
        .guard_step = ml_ld(b->guard_step[k]),
        .guard_frames = (ml_int)_mm256_loadu_si256((const ml_i *)b->guard_frames[k]),
        .triangle_initialized =
            (ml_m)_mm256_loadu_si256((const ml_i *)b->triangle_initialized[k]),
    };
}
static void ml_osc_store(ma_voice_lanes *b, int k, const ml_osc *o) {
    ml_u64_st(b->phase_q48[k], o->phase_q48);
    _mm256_storeu_ps(b->triangle[k], o->triangle);
    _mm256_storeu_ps(b->sync_residual[k], o->sync_residual);
    _mm256_storeu_ps(b->guard_gain[k], o->guard_gain);
    _mm256_storeu_ps(b->guard_target[k], o->guard_target);
    _mm256_storeu_ps(b->guard_step[k], o->guard_step);
    _mm256_storeu_si256((ml_i *)b->guard_frames[k], (ml_i)o->guard_frames);
    _mm256_storeu_si256((ml_i *)b->triangle_initialized[k], (ml_i)o->triangle_initialized);
}
static ml_osc_pair ml_pair_load(const ma_voice_lanes *b) {
    return (ml_osc_pair){
        .vco1 = ml_osc_load(b, 0),
        .vco2 = ml_osc_load(b, 1),
        .sync_run_q48 = ml_u64_ld(b->sync_run_q48),
        .sync_carry = { ml_ld(b->sync_carry[0]), ml_ld(b->sync_carry[1]),
                        ml_ld(b->sync_carry[2]) },
    };
}
static void ml_pair_store(ma_voice_lanes *b, const ml_osc_pair *v) {
    ml_osc_store(b, 0, &v->vco1);
    ml_osc_store(b, 1, &v->vco2);
    ml_u64_st(b->sync_run_q48, v->sync_run_q48);
    for (int k = 0; k < 3; k++) _mm256_storeu_ps(b->sync_carry[k], v->sync_carry[k]);
}
static ml_vco ml_vco_load(const float c[5][MA_LANES]) {
    return (ml_vco){ ml_ld(c[0]), ml_ld(c[1]), ml_ld(c[2]), ml_ld(c[3]), ml_ld(c[4]) };
}
static ml_shapes ml_shapes_load(const int32_t active[MA_LANES]) {
    ml_int bits = (ml_int)_mm256_loadu_si256((const ml_i *)active);
    return (ml_shapes){ (bits & WAVE_SAW) != 0, (bits & WAVE_PULSE) != 0,
                        (bits & WAVE_TRIANGLE) != 0 };
}
static void ml_shapes_store(int32_t active[MA_LANES], ml_shapes on) {
    ml_int bits = (on.saw & WAVE_SAW) | (on.pulse & WAVE_PULSE) | (on.triangle & WAVE_TRIANGLE);
    _mm256_storeu_si256((ml_i *)active, (ml_i)bits);
}
/* The kernel's frame inputs, from the lane inputs the front end wrote. */
static ml_source ml_source_load(const ml_in *in) {
    return (ml_source){
        .vco1 = ml_vco_load(in->c1),
        .vco2 = ml_vco_load(in->c2),
        .vco1_on = ml_shapes_load(in->act1),
        .vco2_on = ml_shapes_load(in->act2),
        .vco1_divisor = ml_ld(in->div1),
        .vco2_divisor = ml_ld(in->div2),
        .mixer_divisor = ml_ld(in->mixdiv),
        .rate_hz = ml_ld(in->rate),
        .vco1_hz = ml_ld(in->v1hz),
        .vco1_step = ml_ld(in->v1step),
        .vco1_seed_step = ml_ld(in->v1seed),
        .vco1_triangle_gain = ml_ld(in->v1tg),
        .vco1_triangle_step = ml_ld(in->v1ts),
        .vco1_step_q48 = ml_u64_ld(in->v1q),
        .vco2_hz = ml_ld(in->v2hz),
        .vco2_step = ml_ld(in->v2step),
        .vco2_triangle_gain = ml_ld(in->v2tg),
        .vco2_triangle_step = ml_ld(in->v2ts),
        .vco2_step_q48 = ml_u64_ld(in->v2q),
        .envelope_ratio = ml_ld(in->envr),
        .sync = ml_ld(in->sync),
        .base_hz = ml_ld(in->base),
        .noise = ml_ld(in->noise),
        .guard1 = ml_ld(in->g1),
        .guard2 = ml_ld(in->g2),
        .crossmod_amount = ml_ld(in->crossmod),
        .vco2_level = ml_ld(in->v2level),
        .noise_level = ml_ld(in->noiselevel),
        .guard_hz = ml_ld(in->guard_hz),
        .guard_frames = (ml_int)_mm256_loadu_si256((const ml_i *)in->guard_frames_i),
        .guard_frames_f = ml_ld(in->guard_frames_f),
    };
}

/* render_bcs, eight lanes: RK4 over four substeps per sample. */
/* Per-lane scalars straight into a vector register (no store/reload, so no
 * blocked store forwarding). `get(l)` is read only for lanes in `on`; other
 * lanes take `fallback`. */
#define ML_LANE(on, l, get, fallback) (((on) >> (l) & 1u) ? (get(l)) : (fallback))
#define ML_PICK_PS(on, get, fallback) _mm256_setr_ps( \
    ML_LANE(on, 0, get, fallback), ML_LANE(on, 1, get, fallback), \
    ML_LANE(on, 2, get, fallback), ML_LANE(on, 3, get, fallback), \
    ML_LANE(on, 4, get, fallback), ML_LANE(on, 5, get, fallback), \
    ML_LANE(on, 6, get, fallback), ML_LANE(on, 7, get, fallback))
#define ML_PICK_U64(on, get) ((ml_u64){ \
    _mm256_setr_epi64x((long long)ML_LANE(on, 0, get, 0u), (long long)ML_LANE(on, 1, get, 0u), \
                       (long long)ML_LANE(on, 2, get, 0u), (long long)ML_LANE(on, 3, get, 0u)), \
    _mm256_setr_epi64x((long long)ML_LANE(on, 4, get, 0u), (long long)ML_LANE(on, 5, get, 0u), \
                       (long long)ML_LANE(on, 6, get, 0u), (long long)ML_LANE(on, 7, get, 0u)) })
static inline ml_m ml_lane_mask(unsigned on) {
    return (ML_INT((int)on) & (ml_int){ 1, 2, 4, 8, 16, 32, 64, 128 }) != 0;
}

/* BCS for the lanes in `on`. The state stays in each voice's ma_synth and is
 * read into registers directly; a lane whose state leaves the safe range
 * takes the scalar reset, as render_bcs does. */
static void ml_bcs(ma_synth *const voice[MA_LANES],
                   const ma_voice_front front[MA_LANES],
                   const float ladder_y4[MA_LANES], unsigned on,
                   float out[MA_LANES]) {
#define BCS_REGIME(l) front[l].controls.bcs_regime
#define BCS_AMOUNT(l) front[l].controls.bcs_amount
#define BCS_BASE(l) front[l].base_hz
#define BCS_RATE(l) voice[l]->sample_rate_hz
#define BCS_RE(l) voice[l]->bcs.hopf_re
#define BCS_IM(l) voice[l]->bcs.hopf_im
#define BCS_X(l) voice[l]->bcs.duffing_x
#define BCS_V(l) voice[l]->bcs.duffing_v
    const ml_bcs_in in = {
        .regime = ML_PICK_PS(on, BCS_REGIME, 0.0f),
        .amount = ML_PICK_PS(on, BCS_AMOUNT, 0.0f),
        .base_hz = ML_PICK_PS(on, BCS_BASE, 20.0f),
        .sample_rate_hz = ML_PICK_PS(on, BCS_RATE, 48000.0f),
        .feedback = ml_keep(ml_lane_mask(on), ml_ld(ladder_y4)),
    };
    const ml_bcs_vector state = {
        ML_PICK_PS(on, BCS_RE, 0.0f), ML_PICK_PS(on, BCS_IM, 0.0f),
        ML_PICK_PS(on, BCS_X, 0.0f), ML_PICK_PS(on, BCS_V, 0.0f),
    };
#undef BCS_REGIME
#undef BCS_AMOUNT
#undef BCS_BASE
#undef BCS_RATE
#undef BCS_RE
#undef BCS_IM
#undef BCS_X
#undef BCS_V
    const ml_bcs_out result = ml_bcs_sample(&in, state);
    float re[MA_LANES] = {}, im[MA_LANES] = {}, x[MA_LANES] = {}, v[MA_LANES] = {};
    float peak[MA_LANES] = {}, output[MA_LANES] = {};
    ml_store(re, result.state.hopf_re);
    ml_store(im, result.state.hopf_im);
    ml_store(x, result.state.duffing_x);
    ml_store(v, result.state.duffing_v);
    ml_store(peak, result.peak);
    ml_store(output, result.output);
    unsigned safe = (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps((ml_i)result.safe));
    for (unsigned l = 0; l < MA_LANES; l++) {
        if (!(on >> l & 1u)) continue;
        ma_bcs *bcs = &voice[l]->bcs;
        if (!(safe >> l & 1u)) {
            reset_bcs(bcs);
            out[l] = 0.0f;
            continue;
        }
        bcs->hopf_re = re[l];
        bcs->hopf_im = im[l];
        bcs->duffing_x = x[l];
        bcs->duffing_v = v[l];
        if (peak[l] > bcs->maximum_state) bcs->maximum_state = peak[l];
        bcs->output = output[l];
        out[l] = output[l];
    }
}

/* Front end, part B, for a lane: the shared part (ma_voice_front_b_shared)
 * and the plain lane inputs, written directly. Raster follows eight lanes
 * wide in ml_raster; cutoff, prewarp and the source frame follow in
 * ml_front_math. */
static void voice_front_b_lane(ma_synth *restrict s, float bcs, float guard1,
                               float guard2, ma_voice_front *restrict f,
                               ml_in *in, unsigned l) {
    ma_voice_front_b_shared(s, bcs, guard1, guard2, f);
    const ma_render_controls *controls = &f->controls;
    in->base[l] = f->base_hz;
    in->noise[l] = f->noise;
    in->g1[l] = guard1;
    in->g2[l] = guard2;
    in->crossmod[l] = controls->crossmod_amount;
    in->v2level[l] = controls->vco2_level;
    in->noiselevel[l] = controls->noise_level;
    in->guard_hz[l] = 0.45f * s->sample_rate_hz;
    uint16_t frames = smoothing_frames(s->sample_rate_hz);
    in->guard_frames_i[l] = frames;
    in->guard_frames_f[l] = (float)frames;
    in->raster_mix[l] = controls->raster_mix;
    in->mozaik_mix[l] = controls->mozaik_mix;
    in->pressure[l] = controls->mixer_pressure;
    in->drive[l] = controls->filter_drive;
    in->resonance[l] = controls->filter_resonance;
    for (int k = 0; k < 2; k++) {
        in->mozaik_gain[k][l] = f->mozaik_gain[k];
        in->mozaik[k][l] = f->mozaik[k];
    }
}

/* Front-end math for eight voices, written straight into the lane inputs:
 * filter_envelope_amount, effective_filter_cutoff, filter_prewarp and
 * prepare_source_frame_guarded, each lane in its scalar operation order.
 * Per-lane scalar inputs are gathered by the caller (ml_front_in). */
typedef struct {
    float env_level[MA_LANES], env_amount[MA_LANES], velocity_filter[MA_LANES];
    float note[MA_LANES], keytrack[MA_LANES], cutoff_hz[MA_LANES], poly[MA_LANES];
    float character[MA_LANES], cutoff_octaves[MA_LANES], vco2_cents[MA_LANES];
    float cutoff_max[MA_LANES], rate[MA_LANES];
    float vco[2][5][MA_LANES];                       /* saw pulse tri sine pw */
    float sync_amount[MA_LANES], sync_softness[MA_LANES], v2level[MA_LANES];
    float noise_level[MA_LANES], crossmod[MA_LANES], fine_cents[MA_LANES];
    float base[MA_LANES], interval[MA_LANES], g1[MA_LANES], g2[MA_LANES];
} ml_front_in;

static void ml_front_benign(ml_front_in *x) {
    *x = (ml_front_in){};
    for (int l = 0; l < MA_LANES; l++) {
        x->cutoff_hz[l] = 1000.0f; x->cutoff_max[l] = 20000.0f;
        x->rate[l] = 48000.0f; x->interval[l] = 1.0f;
    }
}

static void ml_front_gather(ml_front_in *x, unsigned l, const ma_synth *s,
                            const ma_voice_front *f, float guard1, float guard2) {
    const ma_render_controls *c = &f->controls;
    x->env_level[l] = s->filter_envelope.level;
    x->env_amount[l] = c->filter_env_amount;
    x->velocity_filter[l] = ma_velocity_filter(s->velocity);
    x->note[l] = f->note_position;
    x->keytrack[l] = c->filter_keytrack;
    x->cutoff_hz[l] = c->filter_cutoff_hz;
    x->poly[l] = c->poly_pressure;
    x->character[l] = s->character.smoother.current;
    x->cutoff_octaves[l] = s->character.cutoff_octaves;
    x->vco2_cents[l] = s->character.vco2_cents;
    float cutoff_max = 0.42f * s->sample_rate_hz;
    if (cutoff_max > 20000.0f) cutoff_max = 20000.0f;
    x->cutoff_max[l] = cutoff_max;
    x->rate[l] = s->sample_rate_hz;
    const ma_vco_controls *v[2] = { &c->vco1, &c->vco2 };
    for (int k = 0; k < 2; k++) {
        x->vco[k][0][l] = v[k]->saw_level; x->vco[k][1][l] = v[k]->pulse_level;
        x->vco[k][2][l] = v[k]->triangle_level; x->vco[k][3][l] = v[k]->sine_level;
        x->vco[k][4][l] = v[k]->pulse_width;
    }
    x->sync_amount[l] = c->sync_amount; x->sync_softness[l] = c->sync_softness;
    x->v2level[l] = c->vco2_level; x->noise_level[l] = c->noise_level;
    x->crossmod[l] = c->crossmod_amount; x->fine_cents[l] = c->vco2_fine_cents;
    x->base[l] = f->base_hz;
    x->interval[l] = interval_ratio(s->vco2_interval);
    x->g1[l] = guard1; x->g2[l] = guard2;
}

static void ml_vco_store(float c[5][MA_LANES], ml_vco v) {
    ml_store(c[0], v.saw_level);
    ml_store(c[1], v.pulse_level);
    ml_store(c[2], v.triangle_level);
    ml_store(c[3], v.sine_level);
    ml_store(c[4], v.pulse_width);
}

/* The filter coefficient and the source frame of eight voices, written into
 * the lane inputs; cutoff_out gets each lane's effective cutoff (the caller
 * stores it per voice). */
static void ml_front_math(const ml_front_in *x, ml_in *in, float cutoff_out[MA_LANES]) {
    ml_f fs = ml_ld(x->rate);
    ml_f amount = ml_filter_envelope_amount(ml_ld(x->env_amount), ml_ld(x->velocity_filter));
    ml_f level = ml_ld(x->env_level);
    const ml_cutoff_in cutoff_in = {
        .envelope_level = level,
        .envelope_amount = amount,
        .note_position = ml_ld(x->note),
        .filter_keytrack = ml_ld(x->keytrack),
        .filter_cutoff_hz = ml_ld(x->cutoff_hz),
        .poly_pressure = ml_ld(x->poly),
        .character = ml_ld(x->character),
        .cutoff_octaves = ml_ld(x->cutoff_octaves),
        .sample_rate_hz = fs,
    };
    ml_f cutoff = ml_effective_cutoff(&cutoff_in);
    ml_store(cutoff_out, cutoff);
    ml_store(in->filter_g, ml_prewarp(cutoff, fs));

    const ml_source_in source_in = {
        .vco1 = ml_vco_load(x->vco[0]),
        .vco2 = ml_vco_load(x->vco[1]),
        .sample_rate_hz = fs,
        .base_hz = ml_ld(x->base),
        .interval_ratio = ml_ld(x->interval),
        .vco2_fine_cents = ml_ld(x->fine_cents),
        .character = ml_ld(x->character),
        .vco2_cents = ml_ld(x->vco2_cents),
        .sync_amount = ml_ld(x->sync_amount),
        .sync_softness = ml_ld(x->sync_softness),
        .crossmod_amount = ml_ld(x->crossmod),
        .vco2_level = ml_ld(x->v2level),
        .noise_level = ml_ld(x->noise_level),
        .guard1 = ml_ld(x->g1),
        .guard2 = ml_ld(x->g2),
    };
    ml_source src = {};
    ml_prepare_source(&source_in, level * amount, &src);
    ml_vco_store(in->c1, src.vco1);
    ml_vco_store(in->c2, src.vco2);
    ml_shapes_store(in->act1, src.vco1_on);
    ml_shapes_store(in->act2, src.vco2_on);
    ml_store(in->div1, src.vco1_divisor);
    ml_store(in->div2, src.vco2_divisor);
    ml_store(in->mixdiv, src.mixer_divisor);
    ml_store(in->rate, src.rate_hz);
    ml_store(in->sync, src.sync);
    ml_store(in->envr, src.envelope_ratio);
    ml_store(in->v1hz, src.vco1_hz);
    ml_store(in->v1step, src.vco1_step);
    ml_store(in->v1seed, src.vco1_seed_step);
    ml_store(in->v1tg, src.vco1_triangle_gain);
    ml_store(in->v1ts, src.vco1_triangle_step);
    ml_u64_st(in->v1q, src.vco1_step_q48);
    ml_store(in->v2hz, src.vco2_hz);
    ml_store(in->v2step, src.vco2_step);
    ml_store(in->v2tg, src.vco2_triangle_gain);
    ml_store(in->v2ts, src.vco2_triangle_step);
    ml_u64_st(in->v2q, src.vco2_step_q48);
}

/* render_raster for the lanes in `on`, both 2x steps of one output sample,
 * written to in->raster (0 for other lanes, as the scalar path gives). Lanes
 * outside `on` compute on benign inputs (rate 1, everything else 0), are
 * masked out and never written back. */
static void ml_raster(ma_synth *const voice[MA_LANES],
                      const ma_voice_front front[MA_LANES],
                      unsigned on, ml_in *in) {
#define RASTER_BASE(l) front[l].base_hz
#define RASTER_RATE(l) voice[l]->sample_rate_hz
#define RASTER_POSITION(l) front[l].controls.raster_position
#define RASTER_WARP(l) front[l].controls.raster_warp
#define RASTER_PHASE(l) voice[l]->raster.phase_q48
    const ml_raster_in raster_in = {
        .base_hz = ML_PICK_PS(on, RASTER_BASE, 0.0f),
        .render_rate_hz = 2.0f * ML_PICK_PS(on, RASTER_RATE, 1.0f),
        .position = ML_PICK_PS(on, RASTER_POSITION, 0.0f),
        .warp = ML_PICK_PS(on, RASTER_WARP, 0.0f),
    };
    ml_u64 phase = ML_PICK_U64(on, RASTER_PHASE);
#undef RASTER_BASE
#undef RASTER_RATE
#undef RASTER_POSITION
#undef RASTER_WARP
#undef RASTER_PHASE
    const ml_raster_plan plan = ml_raster_plan_for(&raster_in);
    const ml_m on_mask = ml_lane_mask(on);
    for (int at_2x = 0; at_2x < 2; at_2x++)
        ml_store(in->raster[at_2x], ml_keep(on_mask, ml_raster_step(&plan, &phase)));
    uint64_t phase_out[MA_LANES] = {};
    int32_t mip_out[MA_LANES] = {};
    ml_u64_st(phase_out, phase);
    _mm256_storeu_si256((ml_i *)mip_out, (ml_i)plan.mip);
    for (unsigned l = 0; l < MA_LANES; l++)
        if (on >> l & 1u) {
            voice[l]->raster.phase_q48 = phase_out[l];
            voice[l]->raster.mip = (uint8_t)mip_out[l];
        }
}

/* Oscillator block, decimators, filter steps and output decimator. */
static void ml_kernel(ma_voice_lanes *b, const ml_in *in, float filtered[MA_LANES]) {
    ml_osc_pair oscillators = ml_pair_load(b);
    const ml_source source_in = ml_source_load(in);
    ml_f source[8] = {}, at_2x[2] = {};
    ml_source_block(&oscillators, &source_in, source);
    ml_pair_store(b, &oscillators);
    ml_decimate_source(b->history[0][0], b->history[1][0], b->history_pos, source, at_2x);

    ml_ladder ladder = {
        .state = { ml_ld(b->ladder_state[0]), ml_ld(b->ladder_state[1]),
                   ml_ld(b->ladder_state[2]), ml_ld(b->ladder_state[3]) },
        .y4_guess = ml_ld(b->y4_guess),
        .pressure_input = ml_ld(b->pressure_input),
        .pressure_output = ml_ld(b->pressure_output),
        .output_history = b->output_history[0],
        .output_pos = &b->output_pos,
        .reset_count = b->reset_count,
    };
    const ml_filter_in filter_in = {
        .guard1 = ml_ld(in->g1),
        .guard2 = ml_ld(in->g2),
        .vco2_level = ml_ld(in->v2level),
        .noise_level = ml_ld(in->noiselevel),
        .raster_mix = ml_ld(in->raster_mix),
        .mozaik_mix = ml_ld(in->mozaik_mix),
        .mozaik_gain = { ml_ld(in->mozaik_gain[0]), ml_ld(in->mozaik_gain[1]) },
        .mozaik = { ml_ld(in->mozaik[0]), ml_ld(in->mozaik[1]) },
        .raster = { ml_ld(in->raster[0]), ml_ld(in->raster[1]) },
        .grain_mix = ml_ld(in->grain_mix),
        .grain = { ml_ld(in->grain[0]), ml_ld(in->grain[1]) },
        .mixer_pressure = ml_ld(in->pressure),
        .filter_g = ml_ld(in->filter_g),
        .filter_drive = ml_ld(in->drive),
        .filter_resonance = ml_ld(in->resonance),
    };
    ml_store(filtered, ml_filter_block(&ladder, &filter_in, at_2x));
    for (int stage = 0; stage < 4; stage++) ml_store(b->ladder_state[stage], ladder.state[stage]);
    ml_store(b->y4_guess, ladder.y4_guess);
    ml_store(b->pressure_input, ladder.pressure_input);
    ml_store(b->pressure_output, ladder.pressure_output);
}

/* Glas, lane-major (ma_glas.c arithmetic, operation for operation). */
static void ml_glas_zero(ma_voice_lanes *b, unsigned lane) {
    for (int i = 0; i < MA_FORMANTS; i++) b->glas_s1[i][lane] = b->glas_s2[i][lane] = 0.0f;
    for (int x = 0; x < MA_FORMANTS * MA_GRAINS_PER_FORMANT; x++) {
        b->grain_phase[x][lane] = b->grain_step[x][lane] = b->grain_envelope[x][lane] = 0.0f;
        b->grain_decay[x][lane] = b->grain_attack_pos[x][lane] = 0.0f;
        b->grain_attack_step[x][lane] = b->grain_gain[x][lane] = 0.0f;
        b->grain_active[x][lane] = 0;
    }
}
static void ml_glas_launch(ma_voice_lanes *b, unsigned lane, ma_glas *g, float rate) {
    for (int i = 0; i < MA_FORMANTS; i++) {
        uint8_t slot = g->grain_cursor[i];
        g->grain_cursor[i] = (uint8_t)((slot + 1u) % MA_GRAINS_PER_FORMANT);
        int x = i * MA_GRAINS_PER_FORMANT + slot;
        if (b->grain_active[x][lane]) g->grains_stolen++;
        ma_grain q = ma_glas_new_grain(g, i, rate);
        b->grain_phase[x][lane] = q.phase;
        b->grain_step[x][lane] = q.step;
        b->grain_envelope[x][lane] = q.envelope;
        b->grain_decay[x][lane] = q.decay;
        b->grain_attack_pos[x][lane] = q.attack_pos;
        b->grain_attack_step[x][lane] = q.attack_step;
        b->grain_gain[x][lane] = q.gain;
        b->grain_active[x][lane] = -1;
        g->grains_launched++;
    }
}
/* One 2x grain sample for the lanes in `on`. */
static ml_f ml_glas_grains(ma_voice_lanes *b, unsigned on) {
    const ml_m lanes = ml_lane_mask(on);
    ml_f y = ML(0.0f);
    for (int x = 0; x < MA_FORMANTS * MA_GRAINS_PER_FORMANT; x++) {
        ml_grain grain = {
            .phase = ml_ld(b->grain_phase[x]),
            .step = ml_ld(b->grain_step[x]),
            .envelope = ml_ld(b->grain_envelope[x]),
            .decay = ml_ld(b->grain_decay[x]),
            .attack_pos = ml_ld(b->grain_attack_pos[x]),
            .attack_step = ml_ld(b->grain_attack_step[x]),
            .gain = ml_ld(b->grain_gain[x]),
            .active = (ml_m)_mm256_loadu_si256((const ml_i *)b->grain_active[x]),
        };
        if (!ml_any(grain.active & lanes)) continue;
        y = ml_grain_step(&grain, lanes, y);
        ml_store(b->grain_phase[x], grain.phase);
        ml_store(b->grain_envelope[x], grain.envelope);
        ml_store(b->grain_attack_pos[x], grain.attack_pos);
        _mm256_storeu_si256((ml_i *)b->grain_active[x], (ml_i)grain.active);
    }
    return GRAIN_NORM * y;
}
/* The formant bank on the ladder output for lanes in `on` with mix > 0.
 * Non-finite lanes return their input and are reported in *bad. */
static ml_f ml_glas_formant(ma_voice_lanes *b, ml_f x, ml_f mix, unsigned on,
                            unsigned *bad) {
    ml_f s1[MA_FORMANTS] = {}, s2[MA_FORMANTS] = {}, g[MA_FORMANTS] = {}, k[MA_FORMANTS] = {};
    for (int i = 0; i < MA_FORMANTS; i++) {
        s1[i] = ml_ld(b->glas_s1[i]);
        s2[i] = ml_ld(b->glas_s2[i]);
        g[i] = ml_ld(b->glas_g[i]);
        k[i] = ml_ld(b->glas_k[i]);
    }
    ml_m bad_lanes = ML_FALSE;
    ml_f y = ml_formant_bank(s1, s2, g, k, x, mix, ml_lane_mask(on), &bad_lanes);
    for (int i = 0; i < MA_FORMANTS; i++) {
        ml_store(b->glas_s1[i], s1[i]);
        ml_store(b->glas_s2[i], s2[i]);
    }
    *bad = (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps((ml_i)bad_lanes));
    return y;
}

void ma_lanes_tick(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                   const float detune_cents[MA_LANES], float out[MA_LANES]) {
    ma_voice_front front[MA_LANES];
    ml_in in;
    ml_benign(&in);
    unsigned bcs_on = 0;
    for (unsigned lane = 0; lane < MA_LANES; lane++) {
        if (!(b->active >> lane & 1u)) continue;
        ma_voice_front_a(voice[lane], detune_cents[lane], &front[lane]);
        if (front[lane].controls.bcs_amount > 0.0f) bcs_on |= 1u << lane;
    }
    float bcs[MA_LANES] = {};
    if (bcs_on) ml_bcs(voice, front, b->y4_guess, bcs_on, bcs);
    ml_front_in fx;
    ml_front_benign(&fx);
    for (unsigned lane = 0; lane < MA_LANES; lane++) {
        if (!(b->active >> lane & 1u)) continue;
        voice_front_b_lane(voice[lane], bcs[lane], b->guard_gain[0][lane],
                           b->guard_gain[1][lane], &front[lane], &in, lane);
        ml_front_gather(&fx, lane, voice[lane], &front[lane],
                        b->guard_gain[0][lane], b->guard_gain[1][lane]);
    }
    unsigned raster_on = 0;
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        if ((b->active >> lane & 1u) && front[lane].controls.raster_mix > 0.0f)
            raster_on |= 1u << lane;
    if (raster_on) ml_raster(voice, front, raster_on, &in);
    float cutoff[MA_LANES];
    ml_front_math(&fx, &in, cutoff);
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        if (b->active >> lane & 1u) voice[lane]->filter_cutoff_effective_hz = cutoff[lane];
    /* Glas, in the scalar tick's order of effects per lane: controls and the
     * Orbita tick per lane, grains lane-major into the 2x source mix, the
     * formant bank lane-major after the ladder. */
    unsigned glas_on = 0, grain_on = 0;
    float grain_gm[MA_LANES] = {};
    for (unsigned lane = 0; lane < MA_LANES; lane++) {
        if (!(b->active >> lane & 1u)) continue;
        ma_glas *g = &voice[lane]->glas;
        if (!ma_glas_active(g)) { g->frame_grain_mix = 0.0f; continue; }
        float gm = ma_glas_prepare_controls(voice[lane], front[lane].base_hz);
        g->frame_grain_mix = gm;
        glas_on |= 1u << lane;
        if (g->coefficient_epoch != b->glas_epoch[lane]) {
            for (int i = 0; i < MA_FORMANTS; i++) {
                b->glas_g[i][lane] = g->g[i];
                b->glas_k[i][lane] = g->k[i];
            }
            b->glas_epoch[lane] = g->coefficient_epoch;
        }
        if (gm > 0.0f) { grain_on |= 1u << lane; grain_gm[lane] = gm; }
    }
    if (grain_on) {
        for (int k = 0; k < 2; k++) {
            for (unsigned lane = 0; lane < MA_LANES; lane++) {
                if (!(grain_on >> lane & 1u)) continue;
                float rate = 2.0f * voice[lane]->sample_rate_hz;
                if (ma_glas_pulse(&voice[lane]->glas, front[lane].base_hz, rate))
                    ml_glas_launch(b, lane, &voice[lane]->glas, rate);
            }
            alignas(32) float y[MA_LANES];
            _mm256_store_ps(y, ml_glas_grains(b, grain_on));
            for (unsigned lane = 0; lane < MA_LANES; lane++) {
                if (!(grain_on >> lane & 1u)) continue;
                if (!(y[lane] >= -FLT_MAX && y[lane] <= FLT_MAX)) {
                    ma_glas_reset_state(&voice[lane]->glas);
                    ml_glas_zero(b, lane);
                    y[lane] = 0.0f;
                }
                voice[lane]->glas.grain_out[k] = y[lane];
            }
            _mm256_storeu_ps(in.grain[k], _mm256_load_ps(y));
        }
        _mm256_storeu_ps(in.grain_mix, _mm256_setr_ps(grain_gm[0], grain_gm[1], grain_gm[2],
            grain_gm[3], grain_gm[4], grain_gm[5], grain_gm[6], grain_gm[7]));
    }
    float filtered[MA_LANES];
    ml_kernel(b, &in, filtered);
    if (glas_on) {
        float m[MA_LANES] = {};
        for (unsigned lane = 0; lane < MA_LANES; lane++)
            if (glas_on >> lane & 1u) m[lane] = voice[lane]->glas.mix_smoother.current;
        unsigned bad = 0;
        ml_f mix = _mm256_setr_ps(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
        _mm256_storeu_ps(filtered, ml_glas_formant(b, ml_ld(filtered), mix, glas_on, &bad));
        for (unsigned lane = 0; lane < MA_LANES; lane++)
            if (bad >> lane & 1u) {
                ma_glas_reset_state(&voice[lane]->glas);
                ml_glas_zero(b, lane);
            }
    }
    for (unsigned lane = 0; lane < MA_LANES; lane++)
        out[lane] = b->active >> lane & 1u
                  ? voice_back_end(voice[lane], &front[lane], filtered[lane]) : 0.0f;
}
#undef ML_PICK_U64
#undef ML_PICK_PS
#undef ML_LANE
#else
bool ma_lanes_supported(void) { return false; }
void ma_lanes_begin(ma_voice_lanes *b) { b->active = 0; }
void ma_lanes_gather(ma_voice_lanes *b, unsigned lane, const ma_synth *s) {
    (void)b; (void)lane; (void)s;
}
void ma_lanes_scatter(const ma_voice_lanes *b, unsigned lane, ma_synth *s) {
    (void)b; (void)lane; (void)s;
}
void ma_lanes_tick(ma_voice_lanes *b, ma_synth *const voice[MA_LANES],
                   const float detune_cents[MA_LANES], float out[MA_LANES]) {
    (void)b; (void)voice; (void)detune_cents;
    for (unsigned lane = 0; lane < MA_LANES; lane++) out[lane] = 0.0f;
}
#endif
