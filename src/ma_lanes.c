/* Mamut Analog vector lanes: eight pool voices per AVX2 vector, bit-exact
 * against the scalar tick in ma_voice.c. */
#include "ma_raster_table.h"
#include "ma_voice_impl.h"

/* ---------------------------------------------------------------------------
 * Vector lanes (2026-11-06): eight pool voices per AVX2 vector, bit-exact.
 * The scalar front and back ends still run per voice. Four stages run eight
 * lanes wide: the oscillator block, the 8x->4x->2x decimators, the 2x
 * mix/pressure/ladder steps, and the 7-tap output decimator. Every lane
 * performs its voice's scalar float operations in the scalar order, fusing
 * exactly where the scalar code calls ma_fmadd/ma_fnmadd (ml_fmadd/ml_fnmadd
 * here) and nowhere else, with an exact replacement for each integer/float
 * conversion AVX2 lacks (see docs/ma3-simd-osc-spike-20261106.md). Only these
 * functions are target("avx2,fma"); the pool calls them after runtime
 * detection.
 * ------------------------------------------------------------------------ */
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

#define ML_TARGET __attribute__((target("avx2,fma")))
#define ML_F(x) _mm256_set1_ps(x)
typedef __m256 ml_f;
typedef __m256i ml_i;
typedef struct { ml_i lo, hi; } ml_u64;          /* lanes 0-3, 4-7 */

/* Per-sample lane inputs: the front end's products for each lane. */
typedef struct {
    alignas(32) float c1[5][MA_LANES], c2[5][MA_LANES];   /* saw pulse tri sine pw */
    alignas(32) float div1[MA_LANES], div2[MA_LANES], mixdiv[MA_LANES], rate[MA_LANES];
    alignas(32) float v1hz[MA_LANES], v1step[MA_LANES], v2hz[MA_LANES], v2step[MA_LANES];
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
    bool grain_any;
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

/* ---- lane primitives ---- */
ML_TARGET static inline ml_f ml_ld(const float *a) { return _mm256_loadu_ps(a); }
ML_TARGET static inline bool ml_any(ml_f m) { return !_mm256_testz_ps(m, m); }
ML_TARGET static inline ml_f ml_sel(ml_f a, ml_f b, ml_f m) { return _mm256_blendv_ps(a, b, m); }
ML_TARGET static inline ml_f ml_lt(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
ML_TARGET static inline ml_f ml_gt(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_GT_OQ); }
ML_TARGET static inline ml_f ml_ge(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_GE_OQ); }
ML_TARGET static inline ml_f ml_le(ml_f a, ml_f b) { return _mm256_cmp_ps(a, b, _CMP_LE_OQ); }
ML_TARGET static inline ml_f ml_add(ml_f a, ml_f b) { return _mm256_add_ps(a, b); }
ML_TARGET static inline ml_f ml_sub(ml_f a, ml_f b) { return _mm256_sub_ps(a, b); }
ML_TARGET static inline ml_f ml_mul(ml_f a, ml_f b) { return _mm256_mul_ps(a, b); }
ML_TARGET static inline ml_f ml_fmadd(ml_f a, ml_f b, ml_f c) { return _mm256_fmadd_ps(a, b, c); }   /* ma_fmadd */
ML_TARGET static inline ml_f ml_fnmadd(ml_f a, ml_f b, ml_f c) { return _mm256_fnmadd_ps(a, b, c); } /* ma_fnmadd */
ML_TARGET static inline ml_f ml_div(ml_f a, ml_f b) { return _mm256_div_ps(a, b); }
ML_TARGET static inline ml_f ml_and(ml_f a, ml_f b) { return _mm256_and_ps(a, b); }

ML_TARGET static inline ml_u64 ml_u64_set(uint64_t x) {
    return (ml_u64){ _mm256_set1_epi64x((long long)x), _mm256_set1_epi64x((long long)x) };
}
ML_TARGET static inline ml_u64 ml_u64_ld(const uint64_t a[MA_LANES]) {
    return (ml_u64){ _mm256_loadu_si256((const ml_i *)a), _mm256_loadu_si256((const ml_i *)(a + 4)) };
}
ML_TARGET static inline void ml_u64_st(uint64_t a[MA_LANES], ml_u64 v) {
    _mm256_storeu_si256((ml_i *)a, v.lo);
    _mm256_storeu_si256((ml_i *)(a + 4), v.hi);
}
ML_TARGET static inline ml_u64 ml_u64_add(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_add_epi64(a.lo, b.lo), _mm256_add_epi64(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_sub(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_sub_epi64(a.lo, b.lo), _mm256_sub_epi64(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_and(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_and_si256(a.lo, b.lo), _mm256_and_si256(a.hi, b.hi) };
}
ML_TARGET static inline ml_u64 ml_u64_srl(ml_u64 a, int n) {
    return (ml_u64){ _mm256_srli_epi64(a.lo, n), _mm256_srli_epi64(a.hi, n) };
}
/* low 32 bits of each 64-bit lane, as 8 x i32 in lane order */
ML_TARGET static inline ml_i ml_u64_low32(ml_u64 v) {
    const ml_i idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    return _mm256_permute2x128_si256(_mm256_permutevar8x32_epi32(v.lo, idx),
                                     _mm256_permutevar8x32_epi32(v.hi, idx), 0x20);
}
ML_TARGET static inline ml_u64 ml_mask64(ml_f m) {
    ml_i i = _mm256_castps_si256(m);
    return (ml_u64){ _mm256_cvtepi32_epi64(_mm256_castsi256_si128(i)),
                     _mm256_cvtepi32_epi64(_mm256_extracti128_si256(i, 1)) };
}
ML_TARGET static inline ml_u64 ml_u64_sel(ml_u64 a, ml_u64 b, ml_f m) {
    ml_u64 m64 = ml_mask64(m);
    return (ml_u64){ _mm256_blendv_epi8(a.lo, b.lo, m64.lo), _mm256_blendv_epi8(a.hi, b.hi, m64.hi) };
}
ML_TARGET static inline ml_f ml_u64_ge(ml_u64 a, uint64_t bound) {   /* both < 2^62 */
    ml_u64 b = ml_u64_set(bound - 1);
    return _mm256_castsi256_ps(ml_u64_low32((ml_u64){
        _mm256_cmpgt_epi64(a.lo, b.lo), _mm256_cmpgt_epi64(a.hi, b.hi) }));
}
/* (float)(uint32_t)u exactly: two exact 16-bit halves, one rounding */
ML_TARGET static inline ml_f ml_u32_to_f(ml_i u) {
    ml_f hi = _mm256_cvtepi32_ps(_mm256_srli_epi32(u, 16));
    ml_f lo = _mm256_cvtepi32_ps(_mm256_and_si256(u, _mm256_set1_epi32(0xffff)));
    return ml_add(ml_mul(hi, ML_F(65536.0f)), lo);
}
ML_TARGET static inline ml_f ml_turns(ml_u64 q) {          /* phase_turns */
    return ml_mul(ml_u32_to_f(ml_u64_low32(ml_u64_srl(q, 16))), ML_F(0x1p-32f));
}
/* (float)x exactly for x <= 2^48: two exact 24-bit halves, one rounding */
ML_TARGET static inline ml_f ml_u48_to_f(ml_u64 x) {
    ml_f hi = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_srl(x, 24)));
    ml_f lo = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_and(x, ml_u64_set(0xffffffu))));
    return ml_add(ml_mul(hi, ML_F(16777216.0f)), lo);
}
/* (uint64_t)f for +-0 <= f < 2^63: truncated mantissa * 2^(e - 150) */
ML_TARGET static inline ml_i ml_f_to_u64_half(ml_i bits) {
    ml_i e = _mm256_and_si256(_mm256_srli_epi64(bits, 23), _mm256_set1_epi64x(0xff));
    ml_i m = _mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi64x(0x7fffff)),
                             _mm256_set1_epi64x(0x800000));
    return _mm256_or_si256(
        _mm256_sllv_epi64(m, _mm256_sub_epi64(e, _mm256_set1_epi64x(150))),
        _mm256_srlv_epi64(m, _mm256_sub_epi64(_mm256_set1_epi64x(150), e)));
}
ML_TARGET static inline ml_u64 ml_f_to_u64(ml_f f) {
    ml_i bits = _mm256_castps_si256(f);
    return (ml_u64){
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_castsi256_si128(bits))),
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_extracti128_si256(bits, 1))) };
}

/* ---- the scalar helpers, lane by lane ---- */
ML_TARGET static inline ml_f ml_clamp(ml_f v, float lo, float hi) {
    return ml_sel(ml_sel(v, ML_F(hi), ml_gt(v, ML_F(hi))), ML_F(lo), ml_lt(v, ML_F(lo)));
}
ML_TARGET static inline ml_f ml_abs(ml_f x) {
    return ml_and(x, _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)));
}
ML_TARGET static inline ml_f ml_smooth5(ml_f x) {
    return ml_mul(ml_mul(ml_mul(x, x), x),
                  ml_fmadd(x, ml_fmadd(ML_F(6.0f), x, ML_F(-15.0f)), ML_F(10.0f)));
}
ML_TARGET static inline ml_f ml_wrap(ml_f p) {
    p = ml_sel(p, ml_sub(p, ML_F(1.0f)), ml_ge(p, ML_F(1.0f)));
    return ml_sel(p, ml_add(p, ML_F(1.0f)), ml_lt(p, ML_F(0.0f)));
}
ML_TARGET static inline ml_f ml_poly_blep(ml_f phase, ml_f step) {
    ml_f valid = ml_gt(step, ML_F(FLT_EPSILON));
    ml_f edge = ml_mul(ML_F(8.0f), step);
    edge = ml_sel(edge, ML_F(0.49f), ml_gt(edge, ML_F(0.49f)));
    ml_f low = ml_and(ml_lt(phase, edge), valid);
    ml_f high = _mm256_andnot_ps(low, ml_and(ml_gt(phase, ml_sub(ML_F(1.0f), edge)), valid));
    if (!ml_any(_mm256_or_ps(low, high))) return _mm256_setzero_ps();
    ml_f xl = ml_mul(ML_F(0.5f), ml_add(ml_div(phase, edge), ML_F(1.0f)));
    ml_f xh = ml_mul(ML_F(0.5f), ml_add(ml_div(ml_sub(phase, ML_F(1.0f)), edge), ML_F(1.0f)));
    return _mm256_or_ps(ml_and(low, ml_mul(ML_F(2.0f), ml_sub(ml_smooth5(xl), ML_F(1.0f)))),
                        ml_and(high, ml_mul(ML_F(2.0f), ml_smooth5(xh))));
}
ML_TARGET static inline ml_f ml_saw(ml_f phase, ml_f blep) {
    return ml_clamp(ml_sub(ml_sub(ml_mul(ML_F(2.0f), phase), ML_F(1.0f)), blep), -1.25f, 1.25f);
}
ML_TARGET static inline ml_f ml_pulse_with_blep(ml_f phase, ml_f width, ml_f step, ml_f blep) {
    ml_f sample = ml_sel(ML_F(-1.0f), ML_F(1.0f), ml_lt(phase, width));
    sample = ml_add(sample, blep);
    sample = ml_sub(sample, ml_poly_blep(ml_wrap(ml_sub(phase, width)), step));
    return ml_clamp(sample, -1.25f, 1.25f);
}
ML_TARGET static inline ml_f ml_pulse(ml_f phase, ml_f width, ml_f step) {
    return ml_pulse_with_blep(phase, width, step, ml_poly_blep(phase, step));
}
ML_TARGET static inline ml_f ml_raw_triangle(ml_f p) {
    return ml_sub(ML_F(1.0f), ml_mul(ML_F(4.0f), ml_abs(ml_sub(p, ML_F(0.5f)))));
}
ML_TARGET static inline ml_f ml_sin_turns(ml_f p) {
    ml_f t = ml_sel(ml_sel(ml_sub(p, ML_F(1.0f)), ml_sub(ML_F(0.5f), p), ml_lt(p, ML_F(0.75f))),
                    p, ml_lt(p, ML_F(0.25f)));
    ml_f q = ml_mul(t, t);
    ml_f poly = ml_fmadd(q, ML_F(42.0586939f), ML_F(-76.7058597f));   /* ma_sin_turns */
    poly = ml_fmadd(q, poly, ML_F(81.6052493f));
    poly = ml_fmadd(q, poly, ML_F(-41.3417022f));
    poly = ml_fmadd(q, poly, ML_F(6.28318531f));
    return ml_mul(t, poly);
}
ML_TARGET static inline ml_f ml_sat(ml_f x) {               /* ma_sat */
    x = ml_sel(ml_sel(x, ML_F(-3.0f), ml_lt(x, ML_F(-3.0f))), ML_F(3.0f), ml_gt(x, ML_F(3.0f)));
    return ml_div(ml_mul(x, ml_fmadd(x, x, ML_F(27.0f))),
                  ml_fmadd(ml_mul(ML_F(9.0f), x), x, ML_F(27.0f)));
}
ML_TARGET static inline ml_f ml_mamut_sine(ml_f phase) {
    ml_f warm = ml_div(ml_sat(ml_mul(ML_F(1.45f), ml_sin_turns(phase))), ML_F(ma_sat(1.45f)));
    ml_f second = ml_sin_turns(ml_wrap(ml_add(ml_mul(ML_F(2.0f), phase), ML_F(0.07f))));
    return ml_mul(ML_F(0.95591217f), ml_fmadd(ML_F(0.07f), second, warm));
}

typedef struct { ml_f saw, pulse, tri, sine, pw; } ml_vco;
ML_TARGET static inline ml_vco ml_vco_ld(const float c[5][MA_LANES]) {
    return (ml_vco){ ml_ld(c[0]), ml_ld(c[1]), ml_ld(c[2]), ml_ld(c[3]), ml_ld(c[4]) };
}
ML_TARGET static inline ml_f ml_raw_wave_mix(ml_f phase, const ml_vco *c, ml_f divisor) {
    ml_f saw = ml_sub(ml_mul(ML_F(2.0f), phase), ML_F(1.0f));
    ml_f pulse = ml_sel(ML_F(-1.0f), ML_F(1.0f), ml_lt(phase, c->pw));
    ml_f sample = ml_fmadd(c->tri, ml_raw_triangle(phase),
                           ml_fmadd(c->pulse, pulse, ml_mul(c->saw, saw)));
    ml_f sine = ml_gt(c->sine, _mm256_setzero_ps());
    if (ml_any(sine))
        sample = ml_sel(sample, ml_fmadd(c->sine, ml_mamut_sine(phase), sample), sine);
    return ml_div(sample, divisor);
}

typedef struct {
    ml_u64 phase;
    ml_f triangle, residual, gain, target, step, init;
    ml_i frames;
} ml_osc;
ML_TARGET static inline ml_osc ml_osc_ld(const ma_voice_lanes *b, int k) {
    return (ml_osc){ ml_u64_ld(b->phase_q48[k]), ml_ld(b->triangle[k]), ml_ld(b->sync_residual[k]),
                     ml_ld(b->guard_gain[k]), ml_ld(b->guard_target[k]), ml_ld(b->guard_step[k]),
                     _mm256_castsi256_ps(_mm256_loadu_si256((const ml_i *)b->triangle_initialized[k])),
                     _mm256_loadu_si256((const ml_i *)b->guard_frames[k]) };
}
ML_TARGET static inline void ml_osc_st(ma_voice_lanes *b, int k, const ml_osc *o) {
    ml_u64_st(b->phase_q48[k], o->phase);
    _mm256_storeu_ps(b->triangle[k], o->triangle);
    _mm256_storeu_ps(b->sync_residual[k], o->residual);
    _mm256_storeu_ps(b->guard_gain[k], o->gain);
    _mm256_storeu_ps(b->guard_target[k], o->target);
    _mm256_storeu_ps(b->guard_step[k], o->step);
    _mm256_storeu_si256((ml_i *)b->triangle_initialized[k], _mm256_castps_si256(o->init));
    _mm256_storeu_si256((ml_i *)b->guard_frames[k], o->frames);
}

ML_TARGET static inline ml_f ml_preview(const ml_osc *o, const ml_vco *c, ml_f step,
                                        ml_f divisor, const int32_t active[MA_LANES]) {
    ml_i act = _mm256_loadu_si256((const ml_i *)active);
    ml_f phase = ml_turns(o->phase);
    ml_f blep = ml_poly_blep(phase, step);
    ml_f saw_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_SAW)), _mm256_set1_epi32(WAVE_SAW)));
    ml_f pulse_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_PULSE)), _mm256_set1_epi32(WAVE_PULSE)));
    ml_f tri_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(
        _mm256_and_si256(act, _mm256_set1_epi32(WAVE_TRIANGLE)), _mm256_set1_epi32(WAVE_TRIANGLE)));
    ml_f saw = ml_and(saw_on, ml_saw(phase, blep));
    ml_f pulse = ml_and(pulse_on, ml_pulse_with_blep(phase, c->pw, step, blep));
    ml_f tri = ml_and(tri_on, ml_sel(ml_raw_triangle(phase),
                                     ml_clamp(o->triangle, -1.0f, 1.0f), o->init));
    ml_f sample = ml_fmadd(c->tri, tri, ml_fmadd(c->pulse, pulse, ml_mul(c->saw, saw)));
    ml_f sine = ml_gt(c->sine, _mm256_setzero_ps());
    if (ml_any(sine))
        sample = ml_sel(sample, ml_fmadd(c->sine, ml_mamut_sine(phase), sample), sine);
    return ml_div(sample, divisor);
}

ML_TARGET static inline ml_f ml_render_oscillator(ml_osc *o, ml_f preview, ml_f step,
                                                  ml_f sync_correction) {
    ml_f phase = ml_turns(o->phase);
    const ml_f all = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    if (!_mm256_testc_ps(o->init, all)) {
        o->triangle = ml_sel(ml_raw_triangle(phase), o->triangle, o->init);
        o->init = all;
    }
    ml_f sample = ml_add(ml_add(preview, o->residual), sync_correction);
    o->residual = _mm256_setzero_ps();
    o->triangle = ml_clamp(ml_fmadd(ml_mul(ml_pulse(phase, ML_F(0.5f), step), ML_F(4.0f)), step,
                                    o->triangle), -1.25f, 1.25f);
    return ml_clamp(sample, -1.25f, 1.25f);
}

ML_TARGET static inline void ml_set_guard_target(ml_osc *o, ml_f target, const ml_in *in) {
    ml_f changed = _mm256_cmp_ps(target, o->target, _CMP_NEQ_UQ);
    if (!ml_any(changed)) return;
    o->target = ml_sel(o->target, target, changed);
    o->frames = _mm256_blendv_epi8(o->frames, _mm256_loadu_si256((const ml_i *)in->guard_frames_i),
                                   _mm256_castps_si256(changed));
    o->step = ml_sel(o->step, ml_div(ml_sub(target, o->gain), ml_ld(in->guard_frames_f)), changed);
}
ML_TARGET static inline void ml_next_guard_gain(ml_osc *o) {
    ml_i zero = _mm256_setzero_si256();
    ml_i active = _mm256_xor_si256(_mm256_cmpeq_epi32(o->frames, zero), _mm256_set1_epi32(-1));
    ml_f act = _mm256_castsi256_ps(active);
    o->gain = ml_sel(o->gain, ml_add(o->gain, o->step), act);
    o->frames = _mm256_sub_epi32(o->frames, _mm256_and_si256(active, _mm256_set1_epi32(1)));
    ml_f done = ml_and(act, _mm256_castsi256_ps(_mm256_cmpeq_epi32(o->frames, zero)));
    o->gain = ml_sel(o->gain, o->target, done);
}

/* render_source_substep, eight lanes */
ML_TARGET static inline ml_f ml_substep(ml_osc *o1, ml_osc *o2, const ml_in *in,
                                        const ml_vco *c1, const ml_vco *c2, ml_f guard_hz) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    const ml_u64 mask48 = ml_u64_set(PHASE_MASK_Q48);
    ml_f div2 = ml_ld(in->div2), v2step = ml_ld(in->v2step), sync = ml_ld(in->sync);
    ml_u64 v2q = ml_u64_ld(in->v2q);
    ml_f v2preview = ml_preview(o2, c2, v2step, div2, in->act2);

    ml_f v1hz = ml_ld(in->v1hz), v1step = ml_ld(in->v1step);
    ml_u64 v1q = ml_u64_ld(in->v1q);
    ml_f crossmod = ml_ld(in->crossmod);
    ml_f cm = _mm256_cmp_ps(crossmod, zero, _CMP_NEQ_UQ);
    if (ml_any(cm)) {
        ml_f ratio = ml_clamp(ml_add(one, ml_mul(ml_mul(v2preview, crossmod), ML_F(0.25f))),
                              0.25f, 4.0f);
        ml_f hz = ml_mul(ml_mul(ml_ld(in->base), ratio), ml_ld(in->envr));
        ml_f step = ml_clamp(ml_div(hz, ml_ld(in->rate)), 0.0f, 0.45f);
        ml_u64 q = ml_f_to_u64(ml_mul(step, ML_F(0x1p48f)));
        v1hz = ml_sel(v1hz, hz, cm);
        v1q = ml_u64_sel(v1q, q, cm);
        v1step = ml_sel(v1step, ml_turns(q), cm);
    }
    ml_set_guard_target(o1, ml_sel(zero, one, ml_lt(v1hz, guard_hz)), in);

    ml_u64 sum1 = ml_u64_add(o1->phase, v1q);
    ml_f wrap = ml_u64_ge(sum1, PHASE_ONE_Q48);
    ml_f sync_on = ml_gt(sync, zero);
    ml_f event = ml_and(ml_and(wrap, sync_on), ml_gt(v1step, ML_F(FLT_EPSILON)));
    ml_f event_fraction = one, sync_now = zero, sync_next = zero;
    ml_u64 reset_q = ml_u64_set(0);
    if (ml_any(event)) {
        ml_f fraction = ml_clamp(ml_div(ml_u48_to_f(ml_u64_sub(ml_u64_set(PHASE_ONE_Q48), o1->phase)),
                                        ml_u48_to_f(v1q)), 0.0f, 1.0f);
        ml_u64 advance = ml_f_to_u64(ml_mul(ml_u48_to_f(v2q), fraction));
        ml_u64 event_q = ml_u64_and(ml_u64_add(o2->phase, advance), mask48);
        ml_u64 reset = ml_u64_and(ml_f_to_u64(ml_mul(ml_u48_to_f(event_q), ml_sub(one, sync))), mask48);
        ml_f jump = ml_sub(ml_raw_wave_mix(ml_turns(reset), c2, div2),
                           ml_raw_wave_mix(ml_turns(event_q), c2, div2));
        ml_f before = ml_mul(ML_F(0.5f), ml_sub(one, fraction));
        ml_f after = ml_sub(one, ml_mul(ML_F(0.5f), fraction));
        event_fraction = ml_sel(one, fraction, event);
        sync_now = ml_and(event, ml_mul(jump, ml_smooth5(before)));
        sync_next = ml_and(event, ml_mul(jump, ml_sub(ml_smooth5(after), one)));
        reset_q = ml_u64_sel(reset_q, reset, event);
    }

    ml_f v1preview = ml_preview(o1, c1, v1step, ml_ld(in->div1), in->act1);
    ml_f vco1 = ml_render_oscillator(o1, v1preview, v1step, zero);
    ml_f vco2 = ml_render_oscillator(o2, v2preview, v2step, sync_now);
    o2->residual = ml_add(o2->residual, sync_next);
    o1->phase = ml_u64_and(sum1, mask48);

    ml_u64 advanced = ml_u64_and(ml_u64_add(o2->phase, v2q), mask48);
    ml_f restart = ml_and(wrap, sync_on);
    if (ml_any(restart)) {
        ml_f remaining = ml_sub(one, event_fraction);
        ml_u64 rest = ml_f_to_u64(ml_mul(ml_u48_to_f(v2q), remaining));
        ml_u64 restarted = ml_u64_and(ml_u64_add(reset_q, rest), mask48);
        ml_f reset_phase = ml_turns(reset_q);
        ml_f tri = ml_clamp(ml_fmadd(
            ml_mul(ml_mul(ml_pulse(reset_phase, ML_F(0.5f), ml_mul(v2step, remaining)),
                          ML_F(4.0f)), v2step),
            remaining, ml_raw_triangle(reset_phase)), -1.25f, 1.25f);
        o2->phase = ml_u64_sel(advanced, restarted, restart);
        o2->triangle = ml_sel(o2->triangle, tri, restart);
        o2->init = _mm256_or_ps(o2->init, restart);
    } else {
        o2->phase = advanced;
    }
    ml_f mix = ml_add(ml_add(ml_mul(ml_ld(in->g1), vco1),
                             ml_mul(ml_mul(ml_ld(in->g2), ml_ld(in->v2level)), vco2)),
                      ml_mul(ml_ld(in->noiselevel), ml_ld(in->noise)));
    return ml_div(mix, ml_ld(in->mixdiv));
}

ML_TARGET static inline void ml_push31(float history[31][MA_LANES], uint8_t *position, ml_f x) {
    _mm256_storeu_ps(history[*position], x);
    if (++*position == 31) *position = 0;
}
ML_TARGET static inline ml_f ml_decimate31(const float history[31][MA_LANES], uint8_t position) {
    ml_f output = _mm256_setzero_ps();                /* decimate_oversample */
    for (int k = 0; k < OSC_HALFBAND_31_PAIRS; k++) {
        int tap = OSC_HALFBAND_31_PAIR[k];
        int near = position - 1 - tap, far = position - 31 + tap;
        if (near < 0) near += 31;
        if (far < 0) far += 31;
        output = ml_fmadd(ML_F(OSC_HALFBAND_31[tap]),
                          ml_add(ml_ld(history[near]), ml_ld(history[far])), output);
    }
    int centre = position - 1 - OSC_HALFBAND_31_CENTRE;
    if (centre < 0) centre += 31;
    return ml_fmadd(ML_F(OSC_HALFBAND_31[OSC_HALFBAND_31_CENTRE]), ml_ld(history[centre]), output);
}
ML_TARGET static inline void ml_push7(float history[7][MA_LANES], uint8_t *position, ml_f x) {
    _mm256_storeu_ps(history[*position], x);
    if (++*position == 7) *position = 0;
}
ML_TARGET static inline ml_f ml_decimate7(const float history[7][MA_LANES], uint8_t position) {
    ml_f output = _mm256_setzero_ps();                /* decimate_filter */
    for (int k = 0; k < FILTER_HALFBAND_7_PAIRS; k++) {
        int tap = FILTER_HALFBAND_7_PAIR[k];
        int near = position - 1 - tap, far = position - 7 + tap;
        if (near < 0) near += 7;
        if (far < 0) far += 7;
        output = ml_fmadd(ML_F(FILTER_HALFBAND_7[tap]),
                          ml_add(ml_ld(history[near]), ml_ld(history[far])), output);
    }
    int centre = position - 1 - FILTER_HALFBAND_7_CENTRE;
    if (centre < 0) centre += 7;
    return ml_fmadd(ML_F(FILTER_HALFBAND_7[FILTER_HALFBAND_7_CENTRE]), ml_ld(history[centre]), output);
}

/* mix_source_2x, eight lanes */
ML_TARGET static inline ml_f ml_mix(const ml_in *in, ml_f analog, int step) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    ml_f raster_mix = ml_ld(in->raster_mix), mozaik_mix = ml_ld(in->mozaik_mix);
    ml_f mozaik_gain = ml_ld(in->mozaik_gain[step]);
    ml_f vco1_weight = ml_sel(zero, one, ml_gt(ml_ld(in->g1), zero));
    ml_f vco2_weight = ml_sel(zero, ml_ld(in->v2level), ml_gt(ml_ld(in->g2), zero));
    ml_f weight_a = ml_add(ml_add(vco1_weight, vco2_weight), ml_ld(in->noiselevel));
    weight_a = ml_sel(weight_a, one, ml_lt(weight_a, one));
    ml_f mozaik_on = ml_and(ml_gt(mozaik_mix, zero), ml_gt(mozaik_gain, zero));
    ml_f mozaik = ml_mul(ml_mul(mozaik_mix, mozaik_gain), ml_ld(in->mozaik[step]));
    ml_f no_raster = ml_sel(analog, ml_div(ml_add(ml_mul(weight_a, analog), mozaik),
                                           ml_add(weight_a, mozaik_mix)), mozaik_on);
    ml_f weighted = ml_add(ml_mul(weight_a, analog), ml_mul(raster_mix, ml_ld(in->raster[step])));
    ml_f weight = ml_add(weight_a, raster_mix);
    weighted = ml_sel(weighted, ml_add(weighted, mozaik), mozaik_on);
    weight = ml_sel(weight, ml_add(weight, mozaik_mix), mozaik_on);
    return ml_sel(ml_div(weighted, weight), no_raster,
                  _mm256_cmp_ps(raster_mix, zero, _CMP_EQ_OQ));
}

/* apply_mixer_pressure, eight lanes */
ML_TARGET static inline ml_f ml_pressure(ml_f source, ml_f pressure) {
    ml_f off = _mm256_cmp_ps(pressure, _mm256_setzero_ps(), _CMP_EQ_OQ);
    if (_mm256_testc_ps(off, _mm256_castsi256_ps(_mm256_set1_epi32(-1)))) return source;
    ml_f gain = ml_add(ML_F(1.0f), ml_mul(ML_F(5.0f), pressure));
    ml_f shaped = ml_div(ml_sat(ml_mul(gain, source)), ml_sat(gain));
    return ml_sel(ml_add(source, ml_mul(pressure, ml_sub(shaped, source))), source, off);
}

ML_TARGET static inline ml_f ml_in_range(ml_f a) {
    return ml_and(ml_ge(a, ML_F(-FLT_MAX)), ml_le(a, ML_F(FLT_MAX)));
}
/* render_ladder_2x, eight lanes */
ML_TARGET static inline ml_f ml_ladder(ma_voice_lanes *b, ml_f state[4], ml_f *y4_guess,
                                       ml_f input, ml_f g, ml_f drive, ml_f resonance) {
    ml_f input_gain = ml_add(ML_F(1.0f), ml_mul(ml_mul(ML_F(3.0f), drive), drive));
    ml_f feedback = ml_mul(ML_F(4.65f), resonance);
    ml_f y[4], v[4], next[4], y4 = *y4_guess;
    for (int iteration = 0; iteration < 2; iteration++) {
        ml_f u = ml_sat(ml_fnmadd(feedback, y4, ml_mul(input_gain, input)));
        for (int stage = 0; stage < 4; stage++) {
            ml_f d = ml_sub(ml_sat(u), state[stage]);
            v[stage] = ml_mul(d, g);
            y[stage] = ml_fmadd(d, g, state[stage]);
            u = y[stage];
        }
        y4 = y[3];
    }
    ml_f finite = ml_in_range(y4);
    for (int stage = 0; stage < 4; stage++) {
        next[stage] = ml_add(y[stage], v[stage]);
        finite = ml_and(finite, ml_and(ml_in_range(y[stage]),
                                       ml_and(ml_in_range(v[stage]), ml_in_range(next[stage]))));
    }
    const ml_f zero = _mm256_setzero_ps();
    for (int stage = 0; stage < 4; stage++) state[stage] = ml_sel(zero, next[stage], finite);
    *y4_guess = ml_sel(zero, y4, finite);
    unsigned reset = ~(unsigned)_mm256_movemask_ps(finite) & 0xffu;
    while (reset) {
        b->reset_count[__builtin_ctz(reset)]++;
        reset &= reset - 1;
    }
    return ml_sel(zero, y[3], finite);
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
ML_TARGET static inline ml_f ml_lane_mask(unsigned on) {
    return _mm256_castsi256_ps(_mm256_cmpgt_epi32(
        _mm256_and_si256(_mm256_set1_epi32((int)on), _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128)),
        _mm256_setzero_si256()));
}

typedef struct { ml_f re, im, x, v; } ml_bcs_state;
typedef struct {
    ml_f hopf_mu, hopf_nonlinearity, duffing_frequency_ratio, duffing_damping,
         duffing_edge, duffing_cubic_stiffness, duffing_cubic_damping,
         duffing_drive, duffing_feedback, hopf_mix, duffing_mix, output_gain;
} ml_bcs_parameters;

ML_TARGET static inline ml_bcs_state ml_bcs_derivative(ml_bcs_state st,
    const ml_bcs_parameters *q, ml_f omega, ml_f external_drive) {
    ml_f radius2 = ml_fmadd(st.re, st.re, ml_mul(st.im, st.im));
    ml_f radial = ml_fnmadd(q->hopf_nonlinearity, radius2, q->hopf_mu);
    ml_f duffing_omega = ml_mul(omega, q->duffing_frequency_ratio);
    ml_f square = ml_mul(st.x, st.x);
    ml_f edge_damping = ml_mul(ml_sub(q->duffing_edge, q->duffing_damping), duffing_omega);
    ml_f nonlinear_damping = ml_mul(ml_mul(q->duffing_cubic_damping, duffing_omega), square);
    ml_f cubic = ml_mul(ml_mul(ml_mul(q->duffing_cubic_stiffness, duffing_omega), square), st.x);
    ml_f drive = ml_fmadd(q->duffing_drive, st.re, external_drive);
    ml_f spring = ml_fnmadd(duffing_omega, st.x,
                            ml_mul(ml_sub(edge_damping, nonlinear_damping), st.v));
    return (ml_bcs_state){
        .re = ml_fmadd(ml_mul(q->duffing_feedback, omega), st.x,
                       ml_fnmadd(omega, st.im, ml_mul(radial, st.re))),
        .im = ml_fmadd(omega, st.re, ml_mul(radial, st.im)),
        .x = ml_mul(duffing_omega, st.v),
        .v = ml_fmadd(duffing_omega, drive, ml_sub(spring, cubic)),
    };
}
ML_TARGET static inline ml_bcs_state ml_bcs_advance(ml_bcs_state st, ml_bcs_state d, ml_f scale) {
    return (ml_bcs_state){ ml_fmadd(scale, d.re, st.re), ml_fmadd(scale, d.im, st.im),
                           ml_fmadd(scale, d.x, st.x), ml_fmadd(scale, d.v, st.v) };
}
/* k1 + 2 k2 + 2 k3 + k4, left to right as in the scalar sum */
ML_TARGET static inline ml_f ml_rk4_sum(ml_f k1, ml_f k2, ml_f k3, ml_f k4) {
    return ml_add(ml_add(ml_add(k1, ml_mul(ML_F(2.0f), k2)), ml_mul(ML_F(2.0f), k3)), k4);
}

/* bcs_lerp(first->m, second->m, fraction) for landmark index lm, eight lanes:
 * the four landmark values sit in one constant vector, indexed per lane. */
ML_TARGET static inline ml_f ml_bcs_landmark_lerp(float l0, float l1, float l2, float l3,
                                                  ml_i lm, ml_f fraction) {
    const ml_f table = _mm256_setr_ps(l0, l1, l2, l3, l3, l3, l3, l3);
    ml_f first = _mm256_permutevar8x32_ps(table, lm);
    ml_f second = _mm256_permutevar8x32_ps(table, _mm256_add_epi32(lm, _mm256_set1_epi32(1)));
    return ml_add(first, ml_mul(fraction, ml_sub(second, first)));
}

/* BCS for the lanes in `on`. bcs_parameters, the clamps and the step run
 * eight lanes wide; state stays in each voice's ma_synth and is read into
 * registers directly. A lane whose state leaves the safe range takes the
 * scalar reset, exactly as render_bcs does. */
ML_TARGET static void ml_bcs(ma_synth *const voice[MA_LANES],
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
    const ml_f on_mask = ml_lane_mask(on);
    /* bcs_parameters */
    ml_f position = ml_mul(ML_PICK_PS(on, BCS_REGIME, 0.0f), ML_F(3.0f));
    ml_i landmark = _mm256_min_epi32(_mm256_cvttps_epi32(position), _mm256_set1_epi32(2));
    ml_f fraction = ml_sub(position, _mm256_cvtepi32_ps(landmark));
#define BCS_P(m) ml_bcs_landmark_lerp(BCS_LANDMARK[0].m, BCS_LANDMARK[1].m, BCS_LANDMARK[2].m, \
                                      BCS_LANDMARK[3].m, landmark, fraction)
    ml_bcs_parameters p = {
        BCS_P(hopf_mu), BCS_P(hopf_nonlinearity), BCS_P(duffing_frequency_ratio),
        BCS_P(duffing_damping), BCS_P(duffing_edge), BCS_P(duffing_cubic_stiffness),
        BCS_P(duffing_cubic_damping), BCS_P(duffing_drive), BCS_P(duffing_feedback),
        BCS_P(hopf_mix), BCS_P(duffing_mix), BCS_P(output_gain) };
#undef BCS_P
    ml_f rate = ML_PICK_PS(on, BCS_RATE, 48000.0f);
    ml_f up = ml_mul(ML_F(0.125f), rate);
    up = ml_sel(up, ML_F(8000.0f), ml_gt(up, ML_F(8000.0f)));
    ml_f f = ML_PICK_PS(on, BCS_BASE, 20.0f);
    f = ml_sel(ml_sel(f, up, ml_gt(f, up)), ML_F(20.0f), ml_lt(f, ML_F(20.0f)));
    ml_f omega = ml_mul(ML_F(BCS_TAU), f);
    ml_f h = ml_div(ML_F(1.0f), ml_mul(rate, ML_F((float)BCS_INTEGRATION_STEPS)));
    ml_f external_drive = ml_mul(ml_mul(ML_F(0.018f), ML_PICK_PS(on, BCS_AMOUNT, 0.0f)),
                                 ml_clamp(ml_and(on_mask, ml_ld(ladder_y4)), -1.0f, 1.0f));
    ml_bcs_state y = { ML_PICK_PS(on, BCS_RE, 0.0f), ML_PICK_PS(on, BCS_IM, 0.0f),
                       ML_PICK_PS(on, BCS_X, 0.0f), ML_PICK_PS(on, BCS_V, 0.0f) };
#undef BCS_REGIME
#undef BCS_AMOUNT
#undef BCS_BASE
#undef BCS_RATE
#undef BCS_RE
#undef BCS_IM
#undef BCS_X
#undef BCS_V
    for (int substep = 0; substep < BCS_INTEGRATION_STEPS; substep++) {
        ml_bcs_state k1 = ml_bcs_derivative(y, &p, omega, external_drive);
        ml_bcs_state k2 = ml_bcs_derivative(ml_bcs_advance(y, k1, ml_mul(ML_F(0.5f), h)),
                                            &p, omega, external_drive);
        ml_bcs_state k3 = ml_bcs_derivative(ml_bcs_advance(y, k2, ml_mul(ML_F(0.5f), h)),
                                            &p, omega, external_drive);
        ml_bcs_state k4 = ml_bcs_derivative(ml_bcs_advance(y, k3, h), &p, omega, external_drive);
        ml_f sixth = ml_div(h, ML_F(6.0f));
        y.re = ml_fmadd(sixth, ml_rk4_sum(k1.re, k2.re, k3.re, k4.re), y.re);
        y.im = ml_fmadd(sixth, ml_rk4_sum(k1.im, k2.im, k3.im, k4.im), y.im);
        y.x = ml_fmadd(sixth, ml_rk4_sum(k1.x, k2.x, k3.x, k4.x), y.x);
        y.v = ml_fmadd(sixth, ml_rk4_sum(k1.v, k2.v, k3.v, k4.v), y.v);
    }
    /* bcs_state_is_safe: every value finite and within the ceiling; the
     * peak is the running maximum of |value| in hopf_re, hopf_im, x, v order */
    const ml_f ceiling = ML_F(BCS_STATE_CEILING);
    ml_f safe = _mm256_castsi256_ps(_mm256_set1_epi32(-1)), peak = _mm256_setzero_ps();
    const ml_f values[4] = { y.re, y.im, y.x, y.v };
    for (int k = 0; k < 4; k++) {
        ml_f magnitude = ml_abs(values[k]);
        safe = ml_and(safe, ml_and(ml_in_range(values[k]), _mm256_cmp_ps(magnitude, ceiling, _CMP_LE_OQ)));
        peak = ml_sel(peak, magnitude, ml_gt(magnitude, peak));
    }
    ml_f output = ml_sat(ml_mul(ml_fmadd(p.hopf_mix, y.re, ml_mul(p.duffing_mix, y.x)),
                                p.output_gain));
    float re[MA_LANES], im[MA_LANES], x[MA_LANES], v[MA_LANES], pk[MA_LANES], o[MA_LANES];
    _mm256_storeu_ps(re, y.re); _mm256_storeu_ps(im, y.im);
    _mm256_storeu_ps(x, y.x); _mm256_storeu_ps(v, y.v);
    _mm256_storeu_ps(pk, peak); _mm256_storeu_ps(o, output);
    unsigned safe_bits = (unsigned)_mm256_movemask_ps(safe);
    for (unsigned l = 0; l < MA_LANES; l++) {
        if (!(on >> l & 1u)) continue;
        ma_bcs *bcs = &voice[l]->bcs;
        if (!(safe_bits >> l & 1u)) {
            reset_bcs(bcs);
            out[l] = 0.0f;
            continue;
        }
        bcs->hopf_re = re[l]; bcs->hopf_im = im[l];
        bcs->duffing_x = x[l]; bcs->duffing_v = v[l];
        if (pk[l] > bcs->maximum_state) bcs->maximum_state = pk[l];
        bcs->output = o[l];
        out[l] = o[l];
    }
}

/* Front end, part B, for a lane: the scalar parts of voice_front_b (BCS
 * effect, envelopes, noise, mozaik) and the plain lane inputs, written
 * directly. Raster follows eight lanes wide in ml_raster; it reads only its
 * own state, base_hz and the raster controls, none of which this touches,
 * so moving it after mozaik changes nothing. Cutoff, prewarp and the source
 * frame follow in ml_front_math; they read only the envelope level, the
 * controls and the guards. */
static void voice_front_b_lane(ma_synth *restrict s, float bcs, float guard1,
                               float guard2, ma_voice_front *restrict f,
                               ml_in *in, unsigned l) {
    ma_render_controls *controls = &f->controls;
    if (controls->bcs_amount > 0.0f) {
        float crossmod_depth = 0.10f + 0.20f * controls->bcs_regime;
        float resonance_depth = 0.08f + 0.08f * controls->bcs_regime;
        controls->crossmod_amount = clamp_signal(
            controls->crossmod_amount
                + controls->bcs_amount * crossmod_depth * bcs,
            0.0f, 1.0f);
        controls->filter_resonance = clamp_signal(
            controls->filter_resonance
                + controls->bcs_amount * resonance_depth * bcs,
            0.0f, 1.0f);
    }
    ma_adsr filter_adsr = character_adsr(s->filter_adsr,
        s->character.filter_time_bias, f->character);
    ma_adsr amp_adsr = character_adsr(s->amp_adsr,
        s->character.amp_time_bias, f->character);
    (void)render_envelope(&s->filter_envelope, filter_adsr, s->sample_rate_hz);
    (void)render_envelope(&s->amp_envelope, amp_adsr, s->sample_rate_hz);
    f->guard1 = guard1;
    f->guard2 = guard2;
    f->noise = next_noise(&s->noise_state);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        f->mozaik_gain[at_2x] = s->mozaik.guard_gain;
        f->mozaik[at_2x] = render_mozaik(s, controls, f->base_hz,
                                         2.0f * s->sample_rate_hz);
        (void)next_mozaik_guard_gain(&s->mozaik);
    }
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

ML_TARGET static inline ml_f ml_octave_ratio_small(ml_f octaves) {
    octaves = ml_clamp(octaves, -0.25f, 0.25f);
    ml_f value = ML_F(EXP2_SMALL[5]);
    for (int i = 4; i >= 0; i--) value = ml_add(ml_mul(value, octaves), ML_F(EXP2_SMALL[i]));
    return value;
}
ML_TARGET static inline ml_f ml_phase_step(ml_f hz, ml_f rate) {
    return ml_clamp(ml_div(hz, rate), 0.0f, 0.45f);
}
ML_TARGET static inline ml_i ml_active(const ml_f level[5]) {
    const ml_f zero = _mm256_setzero_ps();
    ml_i saw = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[0], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_SAW));
    ml_i pulse = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[1], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_PULSE));
    ml_i tri = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(level[2], zero, _CMP_NEQ_UQ)), _mm256_set1_epi32(WAVE_TRIANGLE));
    return _mm256_or_si256(_mm256_or_si256(saw, pulse), tri);
}
ML_TARGET static inline ml_f ml_waveform_divisor(const ml_f level[5]) {
    ml_f sum = ml_add(ml_add(ml_add(level[0], level[1]), level[2]), level[3]);
    return ml_sel(ML_F(1.0f), sum, ml_gt(sum, ML_F(1.0f)));
}

/* Writes the source-frame fields and filter_g of `in`; cutoff_out gets
 * effective_filter_cutoff for each lane (the caller stores it per voice). */
ML_TARGET static void ml_front_math(const ml_front_in *x, ml_in *in, float cutoff_out[MA_LANES]) {
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    /* filter_envelope_amount, filter modulation */
    ml_f amount = ml_clamp(ml_add(ml_ld(x->env_amount), ml_mul(ML_F(0.25f), ml_ld(x->velocity_filter))), 0.0f, 1.0f);
    ml_f level = ml_ld(x->env_level);
    ml_f modulation = ml_mul(level, amount);
    /* effective_filter_cutoff */
    ml_f envelope = ml_add(one, ml_mul(ml_mul(ML_F(0.78f), level), amount));
    ml_f keytrack = ml_add(one, ml_mul(ml_mul(ml_mul(ml_sub(ml_ld(x->note), ML_F(60.0f)),
                                                     ML_F(1.0f / 48.0f)), ml_ld(x->keytrack)), ML_F(0.42f)));
    keytrack = ml_clamp(keytrack, 0.55f, 1.35f);
    ml_f cutoff = ml_mul(ml_mul(ml_ld(x->cutoff_hz), envelope), keytrack);
    ml_f poly = ml_ld(x->poly);
    ml_f poly_on = ml_gt(poly, zero);
    if (ml_any(poly_on))
        cutoff = ml_sel(cutoff, ml_mul(cutoff, ml_octave_ratio_small(ml_mul(ML_F(0.25f), poly))), poly_on);
    ml_f character = ml_ld(x->character);
    ml_f character_on = ml_gt(character, zero);
    if (ml_any(character_on))
        cutoff = ml_sel(cutoff, ml_mul(cutoff, ml_octave_ratio_small(ml_mul(character, ml_ld(x->cutoff_octaves)))),
                        character_on);
    ml_f cutoff_max = ml_ld(x->cutoff_max);
    cutoff = ml_sel(ml_sel(cutoff, cutoff_max, ml_gt(cutoff, cutoff_max)), ML_F(20.0f), ml_lt(cutoff, ML_F(20.0f)));
    _mm256_storeu_ps(cutoff_out, cutoff);
    /* filter_prewarp */
    ml_f fs = ml_ld(x->rate);
    ml_f xw = ml_div(cutoff, ml_mul(ML_F(2.0f), fs));
    ml_f z = ml_sub(ml_mul(ml_mul(ML_F(2.0f), xw), ML_F(1.0f / 0.42f)), one);
    ml_f b1 = zero, b2 = zero;
    for (int k = 6; k >= 1; k--) {
        ml_f bk = ml_add(ml_sub(ml_mul(ml_mul(ML_F(2.0f), z), b1), b2), ML_F(PREWARP_CHEB[k]));
        b2 = b1;
        b1 = bk;
    }
    _mm256_storeu_ps(in->filter_g, ml_mul(xw, ml_add(ml_sub(ml_mul(z, b1), b2), ML_F(PREWARP_CHEB[0]))));
    /* prepare_source_frame_guarded */
    ml_f v1[5], v2[5];
    for (int k = 0; k < 5; k++) { v1[k] = ml_ld(x->vco[0][k]); v2[k] = ml_ld(x->vco[1][k]); }
    _mm256_storeu_si256((ml_i *)in->act1, ml_active(v1));
    _mm256_storeu_si256((ml_i *)in->act2, ml_active(v2));
    _mm256_storeu_ps(in->div1, ml_waveform_divisor(v1));
    _mm256_storeu_ps(in->div2, ml_waveform_divisor(v2));
    ml_f rate = ml_mul(ML_F(8.0f), fs);
    _mm256_storeu_ps(in->rate, rate);
    _mm256_storeu_ps(in->sync, ml_mul(ml_ld(x->sync_amount),
        ml_clamp(ml_sub(one, ml_mul(ML_F(0.75f), ml_ld(x->sync_softness))), 0.0f, 1.0f)));
    v1[4] = ml_clamp(ml_add(v1[4], ml_mul(ML_F(0.120f), modulation)), 0.05f, 0.95f);
    v2[4] = ml_clamp(ml_sub(v2[4], ml_mul(ML_F(0.080f), modulation)), 0.05f, 0.95f);
    for (int k = 0; k < 5; k++) { _mm256_storeu_ps(in->c1[k], v1[k]); _mm256_storeu_ps(in->c2[k], v2[k]); }
    ml_f envelope_ratio = ml_octave_ratio_small(ml_mul(ML_F(0.125f), modulation));
    _mm256_storeu_ps(in->envr, envelope_ratio);
    ml_f vco1_weight = ml_sel(zero, one, ml_gt(ml_ld(x->g1), zero));
    ml_f vco2_weight = ml_sel(zero, ml_ld(x->v2level), ml_gt(ml_ld(x->g2), zero));
    ml_f mixer = ml_add(ml_add(vco1_weight, vco2_weight), ml_ld(x->noise_level));
    _mm256_storeu_ps(in->mixdiv, ml_sel(mixer, one, ml_lt(mixer, one)));
    ml_f base = ml_ld(x->base);
    ml_f no_crossmod = _mm256_cmp_ps(ml_ld(x->crossmod), zero, _CMP_EQ_OQ);
    ml_f v1hz = ml_mul(base, envelope_ratio);
    ml_u64 v1q = ml_f_to_u64(ml_mul(ml_phase_step(v1hz, rate), ML_F(0x1p48f)));
    ml_f v1step = ml_turns(v1q);
    _mm256_storeu_ps(in->v1hz, ml_and(no_crossmod, v1hz));
    _mm256_storeu_ps(in->v1step, ml_and(no_crossmod, v1step));
    ml_u64_st(in->v1q, ml_u64_sel(ml_u64_set(0), v1q, no_crossmod));
    ml_f fine = ml_octave_ratio_small(ml_div(ml_ld(x->fine_cents), ML_F(1200.0f)));
    ml_f v2hz = ml_mul(ml_mul(base, ml_ld(x->interval)), fine);
    if (ml_any(character_on))
        v2hz = ml_sel(v2hz, ml_mul(v2hz, ml_octave_ratio_small(
            ml_div(ml_mul(character, ml_ld(x->vco2_cents)), ML_F(1200.0f)))), character_on);
    _mm256_storeu_ps(in->v2hz, v2hz);
    ml_u64 v2q = ml_f_to_u64(ml_mul(ml_phase_step(v2hz, rate), ML_F(0x1p48f)));
    ml_u64_st(in->v2q, v2q);
    _mm256_storeu_ps(in->v2step, ml_turns(v2q));
}

/* Both table values raster_table_sample reads, eight lanes: one 32-bit
 * gather per row fetches the int16 pair (index, index + 1). Index 255 wraps
 * to 0 within its row, so it reads the pair at 254 and, when any lane needs
 * it, entry 0 of the row; no read leaves MA_RASTER_TABLE. */
ML_TARGET static inline ml_f ml_raster_row(ml_i row, ml_i word, ml_i last, bool any_last,
                                           ml_f fraction) {
    const int *table = (const int *)(const void *)&MA_RASTER_TABLE[0][0][0];
    ml_i pair = _mm256_i32gather_epi32(table, _mm256_add_epi32(row, word), 2);
    ml_i low = _mm256_srai_epi32(_mm256_slli_epi32(pair, 16), 16);
    ml_i high = _mm256_srai_epi32(pair, 16);
    ml_i a = _mm256_blendv_epi8(low, high, last), b = high;
    if (any_last) {
        ml_i start = _mm256_i32gather_epi32(table, row, 2);
        b = _mm256_blendv_epi8(high, _mm256_srai_epi32(_mm256_slli_epi32(start, 16), 16), last);
    }
    const ml_f scale = ML_F(1.0f / 32768.0f);
    ml_f first = ml_mul(_mm256_cvtepi32_ps(a), scale);
    ml_f second = ml_mul(_mm256_cvtepi32_ps(b), scale);
    return ml_add(first, ml_mul(fraction, ml_sub(second, first)));
}

/* render_raster for the lanes in `on`, both 2x steps of one output sample,
 * written to in->raster (0 for other lanes, as the scalar path gives).
 * Everything runs eight lanes wide: the phase step and q48, raster_mip (the
 * limits halve, so the scalar walk equals a count of passing limits), family
 * and morph, the phase warp, the table reads and interpolations, the mip
 * blend and the phase advance. Lanes outside `on` compute on benign inputs
 * (rate 1, everything else 0), are masked out and never written back. */
ML_TARGET static void ml_raster(ma_synth *const voice[MA_LANES],
                                const ma_voice_front front[MA_LANES],
                                unsigned on, ml_in *in) {
#define RASTER_BASE(l) front[l].base_hz
#define RASTER_RATE(l) voice[l]->sample_rate_hz
#define RASTER_POSITION(l) front[l].controls.raster_position
#define RASTER_WARP(l) front[l].controls.raster_warp
#define RASTER_PHASE(l) voice[l]->raster.phase_q48
    const ml_f zero = _mm256_setzero_ps(), one = ML_F(1.0f);
    const ml_f on_mask = ml_lane_mask(on);
    ml_f step = ml_phase_step(ML_PICK_PS(on, RASTER_BASE, 0.0f),
                              ml_mul(ML_F(2.0f), ML_PICK_PS(on, RASTER_RATE, 1.0f)));
    const ml_u64 vstep = ml_f_to_u64(ml_mul(step, ML_F(0x1p48f)));
    const ml_f warp = ML_PICK_PS(on, RASTER_WARP, 0.0f);
    /* raster_mip */
    ml_f effective_step = ml_mul(step, ml_add(one, ml_mul(ML_F(0.8f), warp)));
    ml_i mip = _mm256_setzero_si256();
    for (int k = 0; k + 1 < MA_RASTER_MIPS; k++)
        mip = _mm256_sub_epi32(mip, _mm256_castps_si256(
            ml_gt(ml_mul(effective_step, ML_F((float)(64 >> k))), ML_F(0.45f))));
    ml_f load = ml_mul(effective_step, _mm256_cvtepi32_ps(_mm256_srlv_epi32(_mm256_set1_epi32(64), mip)));
    ml_i top = _mm256_cmpeq_epi32(mip, _mm256_set1_epi32(MA_RASTER_MIPS - 1));
    ml_f blend = ml_sel(ml_clamp(ml_mul(ml_sub(load, ML_F(0.225f)), ML_F(1.0f / 0.225f)), 0.0f, 1.0f),
                        zero, _mm256_castsi256_ps(top));
    const ml_f blend_on = ml_gt(blend, zero);
    const bool any_blend = ml_any(blend_on);
    /* family, morph and the table rows (coarse row clamped where unused) */
    ml_f family_position = ml_mul(ML_PICK_PS(on, RASTER_POSITION, 0.0f),
                                  ML_F((float)(MA_RASTER_FAMILIES - 1)));
    ml_i family = _mm256_cvttps_epi32(family_position);
    const ml_f morph = ml_sub(family_position, _mm256_cvtepi32_ps(family));
    ml_i next_family = _mm256_min_epi32(_mm256_add_epi32(family, _mm256_set1_epi32(1)),
                                        _mm256_set1_epi32(MA_RASTER_FAMILIES - 1));
    ml_i coarse = _mm256_min_epi32(_mm256_add_epi32(mip, _mm256_set1_epi32(1)),
                                   _mm256_set1_epi32(MA_RASTER_MIPS - 1));
    const ml_i mips = _mm256_set1_epi32(MA_RASTER_MIPS);
#define RASTER_ROW(f, m) _mm256_slli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(f, mips), m), 8)
    const ml_i row_a = RASTER_ROW(family, mip), row_b = RASTER_ROW(next_family, mip);
    const ml_i coarse_a = RASTER_ROW(family, coarse), coarse_b = RASTER_ROW(next_family, coarse);
#undef RASTER_ROW
    ml_u64 vphase = ML_PICK_U64(on, RASTER_PHASE);
    const ml_u64 mask48 = ml_u64_set(PHASE_MASK_Q48);
    for (int at_2x = 0; at_2x < 2; at_2x++) {
        ml_f p = ml_turns(vphase);
        ml_f warped = ml_fmadd(ml_mul(ML_F(0.12f), warp), ml_sin_turns(p), p);
        warped = ml_sel(warped, ml_add(warped, one), ml_lt(warped, zero));
        warped = ml_sel(warped, ml_sub(warped, one), ml_ge(warped, one));
        ml_f table_phase = ml_mul(warped, ML_F((float)MA_RASTER_SAMPLES));
        ml_i whole = _mm256_cvttps_epi32(table_phase);   /* (uint16_t): 0 <= table_phase < 256 */
        ml_f fraction = ml_sub(table_phase, _mm256_cvtepi32_ps(whole));
        ml_i index = _mm256_and_si256(whole, _mm256_set1_epi32(MA_RASTER_SAMPLES - 1));
        ml_i last = _mm256_cmpeq_epi32(index, _mm256_set1_epi32(MA_RASTER_SAMPLES - 1));
        bool any_last = !_mm256_testz_si256(last, last);
        ml_i word = _mm256_min_epi32(index, _mm256_set1_epi32(MA_RASTER_SAMPLES - 2));
        ml_f first = ml_raster_row(row_a, word, last, any_last, fraction);
        ml_f second = ml_raster_row(row_b, word, last, any_last, fraction);
        ml_f sample = ml_add(first, ml_mul(morph, ml_sub(second, first)));
        if (any_blend) {
            first = ml_raster_row(coarse_a, word, last, any_last, fraction);
            second = ml_raster_row(coarse_b, word, last, any_last, fraction);
            ml_f coarse_sample = ml_add(first, ml_mul(morph, ml_sub(second, first)));
            sample = ml_sel(sample, ml_add(sample, ml_mul(blend, ml_sub(coarse_sample, sample))), blend_on);
        }
        _mm256_storeu_ps(in->raster[at_2x], ml_and(on_mask, sample));
        vphase = ml_u64_and(ml_u64_add(vphase, vstep), mask48);
    }
    alignas(32) uint64_t phase[MA_LANES];
    alignas(32) int32_t mip_out[MA_LANES];
    ml_u64_st(phase, vphase);
    _mm256_store_si256((ml_i *)mip_out, mip);
    for (unsigned l = 0; l < MA_LANES; l++)
        if (on >> l & 1u) {
            voice[l]->raster.phase_q48 = phase[l];
            voice[l]->raster.mip = (uint8_t)mip_out[l];
        }
#undef RASTER_BASE
#undef RASTER_RATE
#undef RASTER_POSITION
#undef RASTER_WARP
#undef RASTER_PHASE
}

/* Oscillator block, decimators, filter steps and output decimator. */
ML_TARGET static void ml_kernel(ma_voice_lanes *b, const ml_in *in, float filtered[MA_LANES]) {
    ml_osc o1 = ml_osc_ld(b, 0), o2 = ml_osc_ld(b, 1);
    ml_vco c1 = ml_vco_ld(in->c1), c2 = ml_vco_ld(in->c2);
    ml_f guard_hz = ml_ld(in->guard_hz);
    ml_set_guard_target(&o2, ml_sel(_mm256_setzero_ps(), ML_F(1.0f),
                                    ml_lt(ml_ld(in->v2hz), guard_hz)), in);
    ml_f source[8];
    for (int substep = 0; substep < 8; substep++)
        source[substep] = ml_substep(&o1, &o2, in, &c1, &c2, guard_hz);
    ml_next_guard_gain(&o1);
    ml_next_guard_gain(&o2);
    ml_osc_st(b, 0, &o1);
    ml_osc_st(b, 1, &o2);

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

    ml_f state[4] = { ml_ld(b->ladder_state[0]), ml_ld(b->ladder_state[1]),
                      ml_ld(b->ladder_state[2]), ml_ld(b->ladder_state[3]) };
    ml_f y4 = ml_ld(b->y4_guess);
    ml_f pressure = ml_ld(in->pressure), g = ml_ld(in->filter_g);
    ml_f drive = ml_ld(in->drive), resonance = ml_ld(in->resonance);
    for (int step = 0; step < 2; step++) {
        ml_f mixed = ml_mix(in, at_2x[step], step);
        if (in->grain_any) {
            /* source += grain_mix * (grain - source), only where the mix is
             * positive: the scalar path branches, so zero-mix lanes keep
             * their bits (including -0). */
            ml_f gm = ml_ld(in->grain_mix);
            ml_f blended = ml_add(mixed, ml_mul(gm, ml_sub(ml_ld(in->grain[step]), mixed)));
            mixed = ml_sel(mixed, blended, ml_gt(gm, _mm256_setzero_ps()));
        }
        ml_f pressured = ml_pressure(mixed, pressure);
        _mm256_storeu_ps(b->pressure_input, mixed);
        _mm256_storeu_ps(b->pressure_output, pressured);
        ml_push7(b->output_history, &b->output_pos,
                 ml_ladder(b, state, &y4, pressured, g, drive, resonance));
    }
    for (int stage = 0; stage < 4; stage++) _mm256_storeu_ps(b->ladder_state[stage], state[stage]);
    _mm256_storeu_ps(b->y4_guess, y4);
    _mm256_storeu_ps(filtered, ml_decimate7(b->output_history, b->output_pos));
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
ML_TARGET static ml_f ml_glas_grains(ma_voice_lanes *b, unsigned on) {
    const ml_f one = ML_F(1.0f), lanes = ml_lane_mask(on);
    ml_f y = _mm256_setzero_ps();
    for (int x = 0; x < MA_FORMANTS * MA_GRAINS_PER_FORMANT; x++) {
        ml_f act = ml_and(_mm256_castsi256_ps(_mm256_loadu_si256((const ml_i *)b->grain_active[x])), lanes);
        if (!ml_any(act)) continue;
        ml_f phase = ml_ld(b->grain_phase[x]), step = ml_ld(b->grain_step[x]);
        ml_f env = ml_ld(b->grain_envelope[x]), decay = ml_ld(b->grain_decay[x]);
        ml_f apos = ml_ld(b->grain_attack_pos[x]), astep = ml_ld(b->grain_attack_step[x]);
        ml_f gain = ml_ld(b->grain_gain[x]);
        ml_f in_attack = ml_lt(apos, one);
        ml_f attack = ml_sel(one, ml_mul(ML_F(0.5f), ml_sub(one, ml_sin_turns(
            ml_wrap(ml_sub(ML_F(0.25f), ml_mul(ML_F(0.5f), apos)))))), in_attack);
        ml_f apos_next = ml_sel(apos, ml_add(apos, astep), in_attack);
        y = ml_sel(y, ml_fmadd(ml_mul(ml_mul(gain, attack), env), ml_sin_turns(phase), y), act);
        ml_f phase_next = ml_add(phase, step);
        phase_next = ml_sel(phase_next, ml_sub(phase_next, one), ml_ge(phase_next, one));
        ml_f env_next = ml_mul(env, decay);
        ml_f alive = _mm256_andnot_ps(ml_lt(env_next, ML_F(GRAIN_FLOOR)), act);
        _mm256_storeu_ps(b->grain_phase[x], ml_sel(phase, phase_next, act));
        _mm256_storeu_ps(b->grain_envelope[x], ml_sel(env, env_next, act));
        _mm256_storeu_ps(b->grain_attack_pos[x], ml_sel(apos, apos_next, act));
        _mm256_storeu_si256((ml_i *)b->grain_active[x], _mm256_castps_si256(
            ml_sel(_mm256_castsi256_ps(_mm256_loadu_si256((const ml_i *)b->grain_active[x])), alive, act)));
    }
    return ml_mul(ML_F(GRAIN_NORM), y);
}
/* The formant bank on the ladder output for lanes in `on` with mix > 0.
 * Non-finite lanes return their input and are reported in *bad. */
ML_TARGET static ml_f ml_glas_formant(ma_voice_lanes *b, ml_f x, ml_f mix, unsigned on,
                                      unsigned *bad) {
    ml_f lanes = ml_and(ml_lane_mask(on), ml_gt(mix, _mm256_setzero_ps()));
    ml_f bank = _mm256_setzero_ps();
    for (int i = 0; i < MA_FORMANTS; i++) {
        ml_f s1 = ml_ld(b->glas_s1[i]), s2 = ml_ld(b->glas_s2[i]);
        ml_f g = ml_ld(b->glas_g[i]), k = ml_ld(b->glas_k[i]);
        ml_f hp = ml_div(ml_sub(ml_fnmadd(ml_add(k, g), s1, x), s2),
                         ml_fmadd(g, ml_add(g, k), ML_F(1.0f)));
        ml_f bp = ml_fmadd(g, hp, s1);
        ml_f s1n = ml_fmadd(g, hp, bp);
        ml_f lp = ml_fmadd(g, bp, s2);
        ml_f s2n = ml_fmadd(g, bp, lp);
        bank = ml_fmadd(ml_mul(ML_F(GLAS_GAIN[i]), k), bp, bank);
        _mm256_storeu_ps(b->glas_s1[i], ml_sel(s1, s1n, lanes));
        _mm256_storeu_ps(b->glas_s2[i], ml_sel(s2, s2n, lanes));
    }
    bank = ml_mul(bank, ML_F(FORMANT_MAKEUP));
    ml_f finite = ml_and(ml_ge(bank, ML_F(-FLT_MAX)), ml_le(bank, ML_F(FLT_MAX)));
    *bad = (unsigned)_mm256_movemask_ps(_mm256_andnot_ps(finite, lanes));
    ml_f ok = ml_and(lanes, finite);
    return ml_sel(x, ml_add(x, ml_mul(mix, ml_sub(bank, x))), ok);
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
        in.grain_any = true;
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
#undef ML_TARGET
#undef ML_F
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
