/* MA3-PERF oscillator SIMD spike (2026-11-06): can the per-sample oscillator
 * block of synth_tick run eight voices as eight AVX2 lanes, bit-exact?
 *
 * The block, as synth_tick runs it once per output sample per voice:
 *   set_guard_target(VCO2), 8 x render_source_substep, next_guard_gain x 2.
 * Every path is lane-masked: sparse/full previews, sine, cross-mod, hard-sync
 * events, guard transitions and triangle initialisation. A path is only
 * evaluated when some lane needs it; its result is masked either way.
 *
 * Exact conversions AVX2 lacks:
 *   (float)(uint32_t)x   two exact 16-bit halves, one rounding
 *   (float)x, x <= 2^48  two exact 24-bit halves, one rounding
 *   (uint64_t)f, f >= 0  mantissa shifted by the exponent in 64-bit lanes
 *
 * White-box: includes ma_voice.c; its scalar functions are the reference.
 * Only vector functions are target("avx2") (no FMA); scalar timing code keeps
 * the production flags. Not shipping code.
 *
 *   spike_ma_osc_simd --check   randomized controls, all five patches, the
 *                               whole state of both oscillators compared
 *                               bit-for-bit after every sample
 *   spike_ma_osc_simd --bench   cycles per voice-sample, per patch, scalar
 *                               vs 8 lanes, replaying identical inputs */
#define _GNU_SOURCE
#include "../src/ma_voice.c"
#include <immintrin.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { LANES = 8, SUBSTEPS = 8, BENCH_SAMPLES = 1024, BENCH_TRIALS = 15 };
#define AVX2 __attribute__((target("avx2")))
static constexpr float RATE = 48000.0f;

typedef __m256 v8f;
typedef __m256i v8i;
typedef struct { v8i lo, hi; } v8u64;   /* lanes 0-3 in lo, 4-7 in hi */

/* ---------------- lane helpers ---------------- */
#define F(x) _mm256_set1_ps(x)
AVX2 static inline bool any8(v8f m) { return !_mm256_testz_ps(m, m); }
AVX2 static inline v8f blend8(v8f a, v8f b, v8f m) { return _mm256_blendv_ps(a, b, m); }
AVX2 static inline v8f cmp_lt(v8f a, v8f b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
AVX2 static inline v8f cmp_gt(v8f a, v8f b) { return _mm256_cmp_ps(a, b, _CMP_GT_OQ); }
AVX2 static inline v8f cmp_ge(v8f a, v8f b) { return _mm256_cmp_ps(a, b, _CMP_GE_OQ); }
AVX2 static inline v8f add8(v8f a, v8f b) { return _mm256_add_ps(a, b); }
AVX2 static inline v8f sub8(v8f a, v8f b) { return _mm256_sub_ps(a, b); }
AVX2 static inline v8f mul8(v8f a, v8f b) { return _mm256_mul_ps(a, b); }
AVX2 static inline v8f div8(v8f a, v8f b) { return _mm256_div_ps(a, b); }

AVX2 static inline v8u64 u64_set1(uint64_t x) {
    return (v8u64){ _mm256_set1_epi64x((long long)x), _mm256_set1_epi64x((long long)x) };
}
AVX2 static inline v8u64 u64_add(v8u64 a, v8u64 b) {
    return (v8u64){ _mm256_add_epi64(a.lo, b.lo), _mm256_add_epi64(a.hi, b.hi) };
}
AVX2 static inline v8u64 u64_sub(v8u64 a, v8u64 b) {
    return (v8u64){ _mm256_sub_epi64(a.lo, b.lo), _mm256_sub_epi64(a.hi, b.hi) };
}
AVX2 static inline v8u64 u64_and(v8u64 a, v8u64 b) {
    return (v8u64){ _mm256_and_si256(a.lo, b.lo), _mm256_and_si256(a.hi, b.hi) };
}
AVX2 static inline v8u64 u64_srl(v8u64 a, int n) {
    return (v8u64){ _mm256_srli_epi64(a.lo, n), _mm256_srli_epi64(a.hi, n) };
}
/* low 32 bits of each 64-bit lane, packed as 8 x i32 in lane order */
AVX2 static inline v8i u64_low32(v8u64 v) {
    const v8i idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    return _mm256_permute2x128_si256(_mm256_permutevar8x32_epi32(v.lo, idx),
                                     _mm256_permutevar8x32_epi32(v.hi, idx), 0x20);
}
AVX2 static inline v8u64 mask64(v8f m) {          /* 32-bit lane mask -> 64-bit */
    v8i i = _mm256_castps_si256(m);
    return (v8u64){ _mm256_cvtepi32_epi64(_mm256_castsi256_si128(i)),
                    _mm256_cvtepi32_epi64(_mm256_extracti128_si256(i, 1)) };
}
AVX2 static inline v8f mask32(v8u64 m) { return _mm256_castsi256_ps(u64_low32(m)); }
AVX2 static inline v8u64 u64_blend(v8u64 a, v8u64 b, v8f m) {
    v8u64 m64 = mask64(m);
    return (v8u64){ _mm256_blendv_epi8(a.lo, b.lo, m64.lo), _mm256_blendv_epi8(a.hi, b.hi, m64.hi) };
}
AVX2 static inline v8f u64_ge(v8u64 a, uint64_t bound) {   /* a >= bound, both < 2^62 */
    v8u64 b = u64_set1(bound - 1);
    return mask32((v8u64){ _mm256_cmpgt_epi64(a.lo, b.lo), _mm256_cmpgt_epi64(a.hi, b.hi) });
}

/* (float)(uint32_t)u, exact: hi*65536 and lo are exact, their sum rounds once */
AVX2 static inline v8f u32_to_f(v8i u) {
    v8f hi = _mm256_cvtepi32_ps(_mm256_srli_epi32(u, 16));
    v8f lo = _mm256_cvtepi32_ps(_mm256_and_si256(u, _mm256_set1_epi32(0xffff)));
    return add8(mul8(hi, F(65536.0f)), lo);
}
/* phase_turns: (float)(uint32_t)(q48 >> 16) * 0x1p-32f */
AVX2 static inline v8f turns8(v8u64 q) {
    return mul8(u32_to_f(u64_low32(u64_srl(q, 16))), F(0x1p-32f));
}
/* (float)x for x <= 2^48, exact: two 24-bit halves, one rounding */
AVX2 static inline v8f u48_to_f(v8u64 x) {
    v8f hi = _mm256_cvtepi32_ps(u64_low32(u64_srl(x, 24)));
    v8f lo = _mm256_cvtepi32_ps(u64_low32(u64_and(x, u64_set1(0xffffffu))));
    return add8(mul8(hi, F(16777216.0f)), lo);
}
/* (uint64_t)f for +-0 <= f < 2^63: truncate mantissa * 2^(e - 150) */
AVX2 static inline v8i f_to_u64_half(v8i b) {
    v8i e = _mm256_and_si256(_mm256_srli_epi64(b, 23), _mm256_set1_epi64x(0xff));
    v8i m = _mm256_or_si256(_mm256_and_si256(b, _mm256_set1_epi64x(0x7fffff)),
                            _mm256_set1_epi64x(0x800000));
    v8i left = _mm256_sub_epi64(e, _mm256_set1_epi64x(150));    /* <0 -> huge -> 0 */
    v8i right = _mm256_sub_epi64(_mm256_set1_epi64x(150), e);
    return _mm256_or_si256(_mm256_sllv_epi64(m, left), _mm256_srlv_epi64(m, right));
}
AVX2 static inline v8u64 f_to_u64(v8f f) {
    v8i bits = _mm256_castps_si256(f);
    return (v8u64){ f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_castsi256_si128(bits))),
                    f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_extracti128_si256(bits, 1))) };
}

/* ---------------- the scalar helpers, lane by lane ---------------- */
AVX2 static inline v8f clamp8(v8f v, float lo, float hi) {   /* v<lo ? lo : v>hi ? hi : v */
    return blend8(blend8(v, F(hi), cmp_gt(v, F(hi))), F(lo), cmp_lt(v, F(lo)));
}
AVX2 static inline v8f abs8(v8f x) {
    return _mm256_and_ps(x, _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)));
}
AVX2 static inline v8f smooth5(v8f x) {      /* x*x*x*(10 + x*(-15 + 6*x)) */
    return mul8(mul8(mul8(x, x), x), add8(F(10.0f), mul8(x, add8(F(-15.0f), mul8(F(6.0f), x)))));
}
AVX2 static inline v8f wrap8(v8f p) {
    p = blend8(p, sub8(p, F(1.0f)), cmp_ge(p, F(1.0f)));
    return blend8(p, add8(p, F(1.0f)), cmp_lt(p, F(0.0f)));
}
AVX2 static inline v8f poly_blep8(v8f phase, v8f step) {
    v8f valid = cmp_gt(step, F(FLT_EPSILON));
    v8f edge = mul8(F(8.0f), step);
    edge = blend8(edge, F(0.49f), cmp_gt(edge, F(0.49f)));
    v8f low = _mm256_and_ps(cmp_lt(phase, edge), valid);
    v8f high = _mm256_andnot_ps(low, _mm256_and_ps(cmp_gt(phase, sub8(F(1.0f), edge)), valid));
    v8f either = _mm256_or_ps(low, high);
    if (!any8(either)) return _mm256_setzero_ps();
    v8f xl = mul8(F(0.5f), add8(div8(phase, edge), F(1.0f)));
    v8f xh = mul8(F(0.5f), add8(div8(sub8(phase, F(1.0f)), edge), F(1.0f)));
    return _mm256_or_ps(_mm256_and_ps(low, mul8(F(2.0f), sub8(smooth5(xl), F(1.0f)))),
                        _mm256_and_ps(high, mul8(F(2.0f), smooth5(xh))));
}
AVX2 static inline v8f saw8(v8f phase, v8f blep) {
    return clamp8(sub8(sub8(mul8(F(2.0f), phase), F(1.0f)), blep), -1.25f, 1.25f);
}
AVX2 static inline v8f pulse_wb8(v8f phase, v8f width, v8f step, v8f blep) {
    v8f s = blend8(F(-1.0f), F(1.0f), cmp_lt(phase, width));
    s = add8(s, blep);
    s = sub8(s, poly_blep8(wrap8(sub8(phase, width)), step));
    return clamp8(s, -1.25f, 1.25f);
}
AVX2 static inline v8f pulse8(v8f phase, v8f width, v8f step) {
    return pulse_wb8(phase, width, step, poly_blep8(phase, step));
}
AVX2 static inline v8f tri_raw8(v8f p) {
    return sub8(F(1.0f), mul8(F(4.0f), abs8(sub8(p, F(0.5f)))));
}
AVX2 static inline v8f sin_turns8(v8f p) {
    v8f t = blend8(blend8(sub8(p, F(1.0f)), sub8(F(0.5f), p), cmp_lt(p, F(0.75f))), p, cmp_lt(p, F(0.25f)));
    v8f s = mul8(t, t);
    return mul8(t, add8(F(6.28318531f), mul8(s, add8(F(-41.3417022f), mul8(s, add8(F(81.6052493f),
                mul8(s, add8(F(-76.7058597f), mul8(s, F(42.0586939f))))))))));
}
AVX2 static inline v8f sat8(v8f x) {
    x = blend8(blend8(x, F(-3.0f), cmp_lt(x, F(-3.0f))), F(3.0f), cmp_gt(x, F(3.0f)));
    return div8(mul8(x, add8(F(27.0f), mul8(x, x))), add8(F(27.0f), mul8(mul8(F(9.0f), x), x)));
}
static float sat_drive;   /* tw_sat(1.45f), computed by the scalar function */
AVX2 static inline v8f mamut_sine8(v8f phase) {
    v8f warm = div8(sat8(mul8(F(1.45f), sin_turns8(phase))), F(sat_drive));
    v8f second = sin_turns8(wrap8(add8(mul8(F(2.0f), phase), F(0.07f))));
    return mul8(F(0.95591217f), add8(warm, mul8(F(0.07f), second)));
}

typedef struct { v8f saw, pulse, tri, sine, pw; } vco8;

AVX2 static inline v8f raw_mix8(v8f phase, const vco8 *c, v8f divisor) {
    v8f saw = sub8(mul8(F(2.0f), phase), F(1.0f));
    v8f pulse = blend8(F(-1.0f), F(1.0f), cmp_lt(phase, c->pw));
    v8f sample = add8(add8(mul8(c->saw, saw), mul8(c->pulse, pulse)), mul8(c->tri, tri_raw8(phase)));
    v8f sine = cmp_gt(c->sine, F(0.0f));
    if (any8(sine)) sample = blend8(sample, add8(sample, mul8(c->sine, mamut_sine8(phase))), sine);
    return div8(sample, divisor);
}

typedef struct {
    v8u64 phase;
    v8f triangle, sync_residual, guard_gain, guard_target, guard_step;
    v8i guard_frames;
    v8f tri_init;                                  /* lane mask */
} osc8;

AVX2 static inline v8f preview8(const osc8 *o, const vco8 *c, v8f step, v8f divisor, v8i active) {
    v8f phase = turns8(o->phase);
    v8f blep = poly_blep8(phase, step);
    v8f saw_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(active, _mm256_set1_epi32(WAVE_SAW)), _mm256_set1_epi32(WAVE_SAW)));
    v8f pulse_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(active, _mm256_set1_epi32(WAVE_PULSE)), _mm256_set1_epi32(WAVE_PULSE)));
    v8f tri_on = _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(active, _mm256_set1_epi32(WAVE_TRIANGLE)), _mm256_set1_epi32(WAVE_TRIANGLE)));
    v8f saw = _mm256_and_ps(saw_on, saw8(phase, blep));
    v8f pulse = _mm256_and_ps(pulse_on, pulse_wb8(phase, c->pw, step, blep));
    v8f tri = _mm256_and_ps(tri_on, blend8(tri_raw8(phase), clamp8(o->triangle, -1.0f, 1.0f), o->tri_init));
    v8f sample = add8(add8(mul8(c->saw, saw), mul8(c->pulse, pulse)), mul8(c->tri, tri));
    v8f sine = cmp_gt(c->sine, F(0.0f));
    if (any8(sine)) sample = blend8(sample, add8(sample, mul8(c->sine, mamut_sine8(phase))), sine);
    return div8(sample, divisor);
}

AVX2 static inline v8f render_osc8(osc8 *o, v8f preview, v8f step, v8f sync_correction) {
    v8f phase = turns8(o->phase);
    if (!_mm256_testc_ps(o->tri_init, _mm256_castsi256_ps(_mm256_set1_epi32(-1)))) {
        o->triangle = blend8(tri_raw8(phase), o->triangle, o->tri_init);
        o->tri_init = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    }
    v8f sample = add8(add8(preview, o->sync_residual), sync_correction);
    o->sync_residual = _mm256_setzero_ps();
    o->triangle = clamp8(add8(o->triangle, mul8(mul8(pulse8(phase, F(0.5f), step), F(4.0f)), step)),
                         -1.25f, 1.25f);
    return clamp8(sample, -1.25f, 1.25f);
}

static float guard_frames_f;
static int guard_frames_i;
AVX2 static inline void set_guard_target8(osc8 *o, v8f target) {
    v8f changed = _mm256_cmp_ps(target, o->guard_target, _CMP_NEQ_UQ);
    if (!any8(changed)) return;
    o->guard_target = blend8(o->guard_target, target, changed);
    o->guard_frames = _mm256_blendv_epi8(o->guard_frames, _mm256_set1_epi32(guard_frames_i),
                                         _mm256_castps_si256(changed));
    o->guard_step = blend8(o->guard_step, div8(sub8(target, o->guard_gain), F(guard_frames_f)), changed);
}
AVX2 static inline void next_guard_gain8(osc8 *o) {
    v8i zero = _mm256_setzero_si256();
    v8i active = _mm256_xor_si256(_mm256_cmpeq_epi32(o->guard_frames, zero), _mm256_set1_epi32(-1));
    v8f act = _mm256_castsi256_ps(active);
    o->guard_gain = blend8(o->guard_gain, add8(o->guard_gain, o->guard_step), act);
    o->guard_frames = _mm256_sub_epi32(o->guard_frames, _mm256_and_si256(active, _mm256_set1_epi32(1)));
    v8f done = _mm256_and_ps(act, _mm256_castsi256_ps(_mm256_cmpeq_epi32(o->guard_frames, zero)));
    o->guard_gain = blend8(o->guard_gain, o->guard_target, done);
}

/* Per-sample inputs, lane-major: what synth_tick holds across the substeps. */
typedef struct {
    float c1[5][LANES], c2[5][LANES];              /* saw, pulse, tri, sine, pw */
    float div1[LANES], div2[LANES], mixdiv[LANES], rate[LANES], v1hz[LANES], v1step[LANES],
          v2hz[LANES], v2step[LANES], envr[LANES], sync[LANES], base[LANES], noise[LANES],
          g1[LANES], g2[LANES], crossmod[LANES], v2level[LANES], noiselevel[LANES];
    uint64_t v1q[LANES], v2q[LANES];
    int32_t act1[LANES], act2[LANES];
} lanes_in;

static void gather(lanes_in *in, int lane, const ma_render_controls *c, const ma_source_frame *f,
                   float base, float noise, float g1, float g2) {
    const ma_vco_controls *v[2] = { &f->vco1, &f->vco2 };
    float (*dst[2])[LANES] = { in->c1, in->c2 };
    for (int k = 0; k < 2; k++) {
        dst[k][0][lane] = v[k]->saw_level; dst[k][1][lane] = v[k]->pulse_level;
        dst[k][2][lane] = v[k]->triangle_level; dst[k][3][lane] = v[k]->sine_level;
        dst[k][4][lane] = v[k]->pulse_width;
    }
    in->div1[lane] = f->vco1_divisor; in->div2[lane] = f->vco2_divisor;
    in->mixdiv[lane] = f->mixer_divisor; in->rate[lane] = f->rate_hz;
    in->v1hz[lane] = f->vco1_hz; in->v1step[lane] = f->vco1_step; in->v1q[lane] = f->vco1_step_q48;
    in->v2hz[lane] = f->vco2_hz; in->v2step[lane] = f->vco2_step; in->v2q[lane] = f->vco2_step_q48;
    in->envr[lane] = f->envelope_ratio; in->sync[lane] = f->sync;
    in->act1[lane] = (int32_t)f->vco1_active; in->act2[lane] = (int32_t)f->vco2_active;
    in->base[lane] = base; in->noise[lane] = noise; in->g1[lane] = g1; in->g2[lane] = g2;
    in->crossmod[lane] = c->crossmod_amount; in->v2level[lane] = c->vco2_level;
    in->noiselevel[lane] = c->noise_level;
}

AVX2 static inline v8u64 load_u64(const uint64_t a[LANES]) {
    return (v8u64){ _mm256_loadu_si256((const __m256i *)a), _mm256_loadu_si256((const __m256i *)(a + 4)) };
}
AVX2 static inline vco8 load_vco(const float c[5][LANES]) {
    return (vco8){ _mm256_loadu_ps(c[0]), _mm256_loadu_ps(c[1]), _mm256_loadu_ps(c[2]),
                   _mm256_loadu_ps(c[3]), _mm256_loadu_ps(c[4]) };
}

/* render_source_substep for eight voices */
AVX2 static inline v8f substep8(osc8 *o1, osc8 *o2, const lanes_in *in, const vco8 *c1, const vco8 *c2) {
    const v8f zero = _mm256_setzero_ps(), one = F(1.0f);
    const v8u64 phase_mask = u64_set1(PHASE_MASK_Q48);
    v8f div2 = _mm256_loadu_ps(in->div2), v2step = _mm256_loadu_ps(in->v2step);
    v8u64 v2q = load_u64(in->v2q);
    v8f sync = _mm256_loadu_ps(in->sync);
    v8f v2preview = preview8(o2, c2, v2step, div2, _mm256_loadu_si256((const __m256i *)in->act2));

    v8f v1hz = _mm256_loadu_ps(in->v1hz), v1step = _mm256_loadu_ps(in->v1step);
    v8u64 v1q = load_u64(in->v1q);
    v8f crossmod = _mm256_loadu_ps(in->crossmod);
    v8f cm = _mm256_cmp_ps(crossmod, zero, _CMP_NEQ_UQ);
    if (any8(cm)) {
        v8f ratio = clamp8(add8(one, mul8(mul8(v2preview, crossmod), F(0.25f))), 0.25f, 4.0f);
        v8f hz = mul8(mul8(_mm256_loadu_ps(in->base), ratio), _mm256_loadu_ps(in->envr));
        v8f step = clamp8(div8(hz, _mm256_loadu_ps(in->rate)), 0.0f, 0.45f);
        v8u64 q = f_to_u64(mul8(step, F(0x1p48f)));
        v1hz = blend8(v1hz, hz, cm);
        v1q = u64_blend(v1q, q, cm);
        v1step = blend8(v1step, turns8(q), cm);
    }
    set_guard_target8(o1, blend8(zero, one, cmp_lt(v1hz, F(0.45f * RATE))));

    v8u64 sum1 = u64_add(o1->phase, v1q);
    v8f wrap = u64_ge(sum1, PHASE_ONE_Q48);
    v8f sync_on = cmp_gt(sync, zero);
    v8f event = _mm256_and_ps(_mm256_and_ps(wrap, sync_on), cmp_gt(v1step, F(FLT_EPSILON)));
    v8f event_fraction = one, sync_now = zero, sync_next = zero;
    v8u64 reset_q = u64_set1(0);
    if (any8(event)) {
        v8f ef = clamp8(div8(u48_to_f(u64_sub(u64_set1(PHASE_ONE_Q48), o1->phase)), u48_to_f(v1q)),
                        0.0f, 1.0f);
        v8u64 advance = f_to_u64(mul8(u48_to_f(v2q), ef));
        v8u64 event_q = u64_and(u64_add(o2->phase, advance), phase_mask);
        v8u64 reset = u64_and(f_to_u64(mul8(u48_to_f(event_q), sub8(one, sync))), phase_mask);
        v8f jump = sub8(raw_mix8(turns8(reset), c2, div2), raw_mix8(turns8(event_q), c2, div2));
        v8f before = mul8(F(0.5f), sub8(one, ef));
        v8f after = sub8(one, mul8(F(0.5f), ef));
        event_fraction = blend8(one, ef, event);
        sync_now = _mm256_and_ps(event, mul8(jump, smooth5(before)));
        sync_next = _mm256_and_ps(event, mul8(jump, sub8(smooth5(after), one)));
        reset_q = u64_blend(reset_q, reset, event);
    }

    v8f v1preview = preview8(o1, c1, v1step, _mm256_loadu_ps(in->div1),
                             _mm256_loadu_si256((const __m256i *)in->act1));
    v8f vco1 = render_osc8(o1, v1preview, v1step, zero);
    v8f vco2 = render_osc8(o2, v2preview, v2step, sync_now);
    o2->sync_residual = add8(o2->sync_residual, sync_next);
    o1->phase = u64_and(sum1, phase_mask);

    v8u64 advanced = u64_and(u64_add(o2->phase, v2q), phase_mask);
    v8f reset_lanes = _mm256_and_ps(wrap, sync_on);
    if (any8(reset_lanes)) {
        v8f remaining = sub8(one, event_fraction);
        v8u64 rest = f_to_u64(mul8(u48_to_f(v2q), remaining));
        v8u64 restarted = u64_and(u64_add(reset_q, rest), phase_mask);
        v8f reset_phase = turns8(reset_q);
        v8f tri = clamp8(add8(tri_raw8(reset_phase),
                              mul8(mul8(mul8(pulse8(reset_phase, F(0.5f), mul8(v2step, remaining)), F(4.0f)),
                                        v2step), remaining)), -1.25f, 1.25f);
        o2->phase = u64_blend(advanced, restarted, reset_lanes);
        o2->triangle = blend8(o2->triangle, tri, reset_lanes);
        o2->tri_init = _mm256_or_ps(o2->tri_init, reset_lanes);
    } else {
        o2->phase = advanced;
    }
    v8f mix = add8(add8(mul8(_mm256_loadu_ps(in->g1), vco1),
                        mul8(mul8(_mm256_loadu_ps(in->g2), _mm256_loadu_ps(in->v2level)), vco2)),
                   mul8(_mm256_loadu_ps(in->noiselevel), _mm256_loadu_ps(in->noise)));
    return div8(mix, _mm256_loadu_ps(in->mixdiv));
}

/* The oscillator block synth_tick runs once per output sample. */
AVX2 static void block8(osc8 *o1, osc8 *o2, const lanes_in *in, float out[SUBSTEPS][LANES]) {
    vco8 c1 = load_vco(in->c1), c2 = load_vco(in->c2);
    set_guard_target8(o2, blend8(_mm256_setzero_ps(), F(1.0f),
                                 cmp_lt(_mm256_loadu_ps(in->v2hz), F(0.45f * RATE))));
    for (int k = 0; k < SUBSTEPS; k++) _mm256_storeu_ps(out[k], substep8(o1, o2, in, &c1, &c2));
    next_guard_gain8(o1);
    next_guard_gain8(o2);
}

static void block_scalar(ma_synth *s, ma_render_controls *c, const ma_source_frame *f,
                         float base, float noise, float g1, float g2, float out[SUBSTEPS]) {
    set_guard_target(&s->oscillator2, f->vco2_hz < 0.45f * s->sample_rate_hz ? 1.0f : 0.0f,
                     s->sample_rate_hz);
    for (int k = 0; k < SUBSTEPS; k++)
        out[k] = render_source_substep(s, c, f, base, noise, g1, g2);
    (void)next_guard_gain(&s->oscillator1);
    (void)next_guard_gain(&s->oscillator2);
}

/* ---------------- state transfer and comparison ---------------- */
static void to_lanes(const ma_oscillator *o[LANES], uint64_t ph[LANES], float tri[LANES], float res[LANES],
                     float gg[LANES], float gt[LANES], float gs[LANES], int32_t gf[LANES], int32_t ti[LANES]) {
    for (int l = 0; l < LANES; l++) {
        ph[l] = o[l]->phase_q48; tri[l] = o[l]->triangle; res[l] = o[l]->sync_residual;
        gg[l] = o[l]->guard_gain; gt[l] = o[l]->guard_target; gs[l] = o[l]->guard_step;
        gf[l] = o[l]->guard_frames; ti[l] = o[l]->triangle_initialized ? -1 : 0;
    }
}
AVX2 static osc8 load_osc(const ma_synth voices[LANES], int which) {
    const ma_oscillator *o[LANES];
    for (int l = 0; l < LANES; l++) o[l] = which ? &voices[l].oscillator2 : &voices[l].oscillator1;
    uint64_t ph[LANES]; float tri[LANES], res[LANES], gg[LANES], gt[LANES], gs[LANES];
    int32_t gf[LANES], ti[LANES];
    to_lanes(o, ph, tri, res, gg, gt, gs, gf, ti);
    return (osc8){ load_u64(ph), _mm256_loadu_ps(tri), _mm256_loadu_ps(res), _mm256_loadu_ps(gg),
                   _mm256_loadu_ps(gt), _mm256_loadu_ps(gs), _mm256_loadu_si256((const __m256i *)gf),
                   _mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)ti)) };
}
static uint32_t bits_of(float v) { uint32_t b; memcpy(&b, &v, sizeof b); return b; }
/* 0 if every field of every lane is bit-identical; otherwise a description */
AVX2 static const char *compare_osc(const osc8 *v, const ma_synth voices[LANES], int which, int *lane_out) {
    uint64_t ph[LANES]; float tri[LANES], res[LANES], gg[LANES], gt[LANES], gs[LANES];
    int32_t gf[LANES], ti[LANES];
    _mm256_storeu_si256((__m256i *)ph, v->phase.lo); _mm256_storeu_si256((__m256i *)(ph + 4), v->phase.hi);
    _mm256_storeu_ps(tri, v->triangle); _mm256_storeu_ps(res, v->sync_residual);
    _mm256_storeu_ps(gg, v->guard_gain); _mm256_storeu_ps(gt, v->guard_target);
    _mm256_storeu_ps(gs, v->guard_step); _mm256_storeu_si256((__m256i *)gf, v->guard_frames);
    _mm256_storeu_si256((__m256i *)ti, _mm256_castps_si256(v->tri_init));
    for (int l = 0; l < LANES; l++) {
        const ma_oscillator *o = which ? &voices[l].oscillator2 : &voices[l].oscillator1;
        *lane_out = l;
        if (ph[l] != o->phase_q48) return "phase_q48";
        if (bits_of(tri[l]) != bits_of(o->triangle)) return "triangle";
        if (bits_of(res[l]) != bits_of(o->sync_residual)) return "sync_residual";
        if (bits_of(gg[l]) != bits_of(o->guard_gain)) return "guard_gain";
        if (bits_of(gt[l]) != bits_of(o->guard_target)) return "guard_target";
        if (bits_of(gs[l]) != bits_of(o->guard_step)) return "guard_step";
        if (gf[l] != o->guard_frames) return "guard_frames";
        if ((ti[l] != 0) != o->triangle_initialized) return "triangle_initialized";
    }
    return 0;
}

/* ---------------- deterministic inputs ---------------- */
static uint64_t rng_state = UINT64_C(0x4d414d55534f5343);
static uint32_t rng(void) { return (uint32_t)(tw_splitmix64(&rng_state) >> 32); }
static float uniform(float lo, float hi) { return lo + (hi - lo) * (float)(rng() >> 8) * 0x1p-24f; }
static float maybe_zero(float lo, float hi) { return rng() % 4 == 0 ? 0.0f : uniform(lo, hi); }

static const ma_patch *const PATCHES[5] = {
    &ma_patch_tepih, &ma_patch_lead, &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica };
static const char *const PATCH_NAMES[5] = { "Tepih", "Lead", "Raster", "Prizma", "Granica" };

static ma_synth voices[LANES];

/* A sanitized-range control perturbation: every branch of the substep is reachable. */
static void perturb(ma_render_controls *c) {
    if (rng() % 3) return;
    ma_vco_controls *v[2] = { &c->vco1, &c->vco2 };
    for (int k = 0; k < 2; k++) {
        v[k]->saw_level = maybe_zero(0.0f, 1.0f); v[k]->pulse_level = maybe_zero(0.0f, 1.0f);
        v[k]->triangle_level = maybe_zero(0.0f, 1.0f); v[k]->sine_level = maybe_zero(0.0f, 1.0f);
        v[k]->pulse_width = uniform(0.05f, 0.95f);
    }
    c->vco2_level = maybe_zero(0.0f, 1.0f); c->noise_level = maybe_zero(0.0f, 1.0f);
    c->vco2_fine_cents = uniform(-50.0f, 50.0f);
    c->sync_amount = maybe_zero(0.0f, 1.0f); c->sync_softness = uniform(0.0f, 1.0f);
    c->crossmod_amount = maybe_zero(0.0f, 1.0f);
}

AVX2 static int check(unsigned samples) {
    sat_drive = tw_sat(1.45f);
    unsigned sync_wraps = 0, crossmod = 0, guard_moves = 0, sparse = 0, sine = 0;
    for (int l = 0; l < LANES; l++) {
        ma_synth_init_patch(&voices[l], RATE, PATCHES[l % 5]);
        ma_synth_note_on(&voices[l], 0, (uint8_t)(30 + 9 * l), 100);
        voices[l].oscillator1.phase_q48 = tw_splitmix64(&rng_state) & PHASE_MASK_Q48;
        voices[l].oscillator2.phase_q48 = tw_splitmix64(&rng_state) & PHASE_MASK_Q48;
    }
    osc8 o1 = load_osc(voices, 0), o2 = load_osc(voices, 1);
    static lanes_in in;
    float scalar[LANES][SUBSTEPS], simd[SUBSTEPS][LANES];
    float note[LANES];
    for (int l = 0; l < LANES; l++) note[l] = (float)(30 + 9 * l);
    for (unsigned n = 0; n < samples; n++) {
        if (n % 4096 == 0)           /* re-arm triangle initialisation on some lanes */
            for (int l = 0; l < LANES; l++)
                if (rng() % 3 == 0) {
                    voices[l].oscillator1.triangle_initialized = false;
                    voices[l].oscillator2.triangle_initialized = false;
                    o1 = load_osc(voices, 0); o2 = load_osc(voices, 1);
                }
        ma_render_controls c[LANES];
        ma_source_frame f[LANES];
        float base[LANES], noise[LANES], g1[LANES], g2[LANES];
        for (int l = 0; l < LANES; l++) {
            ma_synth *s = &voices[l];
            c[l] = next_render_controls(s);
            perturb(&c[l]);
            if (rng() % 512 == 0) note[l] = uniform(0.0f, 127.0f);
            base[l] = note_position_hz(note[l]) * (rng() % 64 == 0 ? uniform(1.0f, 40.0f) : 1.0f);
            g1[l] = s->oscillator1.guard_gain; g2[l] = s->oscillator2.guard_gain;
            noise[l] = next_noise(&s->noise_state);
            f[l] = prepare_source_frame(s, &c[l], base[l], uniform(-2.0f, 2.0f));
            gather(&in, l, &c[l], &f[l], base[l], noise[l], g1[l], g2[l]);
            crossmod += c[l].crossmod_amount != 0.0f;
            sparse += f[l].vco1_active != WAVE_ALL || f[l].vco2_active != WAVE_ALL;
            sine += f[l].vco1.sine_level > 0.0f || f[l].vco2.sine_level > 0.0f;
        }
        uint64_t phase_before[LANES];
        for (int l = 0; l < LANES; l++) phase_before[l] = voices[l].oscillator1.phase_q48;
        for (int l = 0; l < LANES; l++) {
            float before = voices[l].oscillator1.guard_target;
            block_scalar(&voices[l], &c[l], &f[l], base[l], noise[l], g1[l], g2[l], scalar[l]);
            guard_moves += voices[l].oscillator1.guard_target != before;
        }
        for (int l = 0; l < LANES; l++)   /* VCO1 wrapped during the block while sync was on */
            sync_wraps += f[l].sync > 0.0f && voices[l].oscillator1.phase_q48 < phase_before[l];
        block8(&o1, &o2, &in, simd);
        for (int l = 0; l < LANES; l++)
            for (int k = 0; k < SUBSTEPS; k++)
                if (bits_of(scalar[l][k]) != bits_of(simd[k][l])) {
                    fprintf(stderr, "output mismatch: sample %u lane %d substep %d: scalar %a simd %a\n",
                            n, l, k, (double)scalar[l][k], (double)simd[k][l]);
                    return 1;
                }
        int lane = 0;
        for (int which = 0; which < 2; which++) {
            const char *field = compare_osc(which ? &o2 : &o1, voices, which, &lane);
            if (field) {
                fprintf(stderr, "state mismatch: sample %u lane %d oscillator%d.%s\n", n, lane, which + 1, field);
                return 1;
            }
        }
    }
    printf("oscillator block: %u samples x %d lanes x %d substeps, outputs and both oscillators' "
           "full state bit-identical\n  coverage (lane-samples): %u cross-mod, %u sync-on VCO1 wraps, "
           "%u guard-target moves, %u sparse previews, %u with sine\n", samples, LANES, SUBSTEPS,
           crossmod, sync_wraps, guard_moves, sparse, sine);
    return 0;
}

/* ---------------- cycles ---------------- */
static int pmu_fd = -1;
static int pmu_event(uint64_t config, int group) {
    struct perf_event_attr a; memset(&a, 0, sizeof a);
    a.type = PERF_TYPE_HARDWARE; a.size = sizeof a; a.config = config;
    a.disabled = group < 0; a.exclude_kernel = 1; a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_GROUP;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, group, 0UL);
}
static void pmu_open(void) {
    int leader = pmu_event(PERF_COUNT_HW_CPU_CYCLES, -1);
    if (leader < 0 || pmu_event(PERF_COUNT_HW_INSTRUCTIONS, leader) < 0
        || ioctl(leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP)) { perror("perf_event_open"); exit(2); }
    pmu_fd = leader;
}
typedef struct { uint64_t cycles, instructions; } counts;
static counts pmu_now(void) {
    uint64_t v[3];
    if (read(pmu_fd, v, sizeof v) != (ssize_t)sizeof v || v[0] != 2) { perror("pmu read"); exit(1); }
    return (counts){ v[1], v[2] };
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}

/* recorded inputs: identical for the scalar and the 8-lane replay */
static ma_render_controls rec_c[BENCH_SAMPLES][LANES];
static ma_source_frame rec_f[BENCH_SAMPLES][LANES];
static float rec_base[BENCH_SAMPLES][LANES], rec_noise[BENCH_SAMPLES][LANES],
    rec_g1[BENCH_SAMPLES][LANES], rec_g2[BENCH_SAMPLES][LANES];
static lanes_in rec_in[BENCH_SAMPLES];
static float out_scalar[BENCH_SAMPLES][LANES][SUBSTEPS], out_simd[BENCH_SAMPLES][SUBSTEPS][LANES];
static ma_oscillator start1[LANES], start2[LANES];
static ma_synth expected[LANES];   /* oscillators after the scalar replay */

static void record(int patch) {
    static const uint8_t chord[LANES] = { 36, 43, 48, 52, 55, 60, 64, 67 };
    for (int l = 0; l < LANES; l++) {
        ma_synth_init_patch(&voices[l], RATE, PATCHES[patch]);
        ma_synth_note_on(&voices[l], 0, chord[l], 100);
    }
    for (int warm = 0; warm < 256; warm++)        /* let smoothers and guards settle */
        for (int l = 0; l < LANES; l++) (void)ma_synth_tick(&voices[l]);
    for (int l = 0; l < LANES; l++) { start1[l] = voices[l].oscillator1; start2[l] = voices[l].oscillator2; }
    for (unsigned n = 0; n < BENCH_SAMPLES; n++)
        for (int l = 0; l < LANES; l++) {
            ma_synth *s = &voices[l];
            ma_render_controls c = next_render_controls(s);
            float base = note_position_hz(chord[l]);
            float g1 = s->oscillator1.guard_gain, g2 = s->oscillator2.guard_gain;
            float noise = next_noise(&s->noise_state);
            ma_source_frame f = prepare_source_frame(s, &c, base, 0.3f * tw_sin_turns((float)(n % 480) / 480.0f));
            rec_c[n][l] = c; rec_f[n][l] = f; rec_base[n][l] = base; rec_noise[n][l] = noise;
            rec_g1[n][l] = g1; rec_g2[n][l] = g2;
            gather(&rec_in[n], l, &c, &f, base, noise, g1, g2);
            float scratch[SUBSTEPS];
            block_scalar(s, &c, &f, base, noise, g1, g2, scratch);
        }
}

static void restore(void) {
    for (int l = 0; l < LANES; l++) { voices[l].oscillator1 = start1[l]; voices[l].oscillator2 = start2[l]; }
}
static counts replay_scalar(void) {
    restore();
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_SAMPLES; n++)
        for (int l = 0; l < LANES; l++)
            block_scalar(&voices[l], &rec_c[n][l], &rec_f[n][l], rec_base[n][l], rec_noise[n][l],
                         rec_g1[n][l], rec_g2[n][l], out_scalar[n][l]);
    counts b = pmu_now();
    return (counts){ b.cycles - a.cycles, b.instructions - a.instructions };
}
AVX2 static counts replay_simd(void) {
    restore();
    osc8 o1 = load_osc(voices, 0), o2 = load_osc(voices, 1);
    counts a = pmu_now();
    for (unsigned n = 0; n < BENCH_SAMPLES; n++) block8(&o1, &o2, &rec_in[n], out_simd[n]);
    counts b = pmu_now();
    int lane;
    if (compare_osc(&o1, expected, 0, &lane) || compare_osc(&o2, expected, 1, &lane)) {
        fputs("final state mismatch\n", stderr); exit(1);
    }
    return (counts){ b.cycles - a.cycles, b.instructions - a.instructions };
}

static double median_per_voice_sample(counts (*trial)(void), double *instructions) {
    uint64_t cyc[BENCH_TRIALS], ins[BENCH_TRIALS];
    (void)trial();
    for (int t = 0; t < BENCH_TRIALS; t++) { counts c = trial(); cyc[t] = c.cycles; ins[t] = c.instructions; }
    qsort(cyc, BENCH_TRIALS, sizeof *cyc, cmp_u64); qsort(ins, BENCH_TRIALS, sizeof *ins, cmp_u64);
    *instructions = (double)ins[BENCH_TRIALS / 2] / (BENCH_SAMPLES * LANES);
    return (double)cyc[BENCH_TRIALS / 2] / (BENCH_SAMPLES * LANES);
}

static int bench(void) {
    pmu_open();
    sat_drive = tw_sat(1.45f);
    printf("# %d samples x %d lanes (8-note chord, one patch per row), %d trials, median;\n"
           "# cycles/instructions per voice-sample for the whole oscillator block (8 substeps)\n",
           BENCH_SAMPLES, LANES, BENCH_TRIALS);
    puts("patch,scalar_cycles,simd_cycles,speedup,scalar_instructions,simd_instructions,outputs_identical");
    for (int p = 0; p < 5; p++) {
        record(p);
        double si, vi;
        double sc = median_per_voice_sample(replay_scalar, &si);
        memcpy(expected, voices, sizeof expected);              /* scalar final state */
        double vc = median_per_voice_sample(replay_simd, &vi);  /* checked against it */
        bool same = true;
        for (unsigned n = 0; n < BENCH_SAMPLES && same; n++)
            for (int l = 0; l < LANES; l++)
                for (int k = 0; k < SUBSTEPS; k++)
                    same = same && bits_of(out_scalar[n][l][k]) == bits_of(out_simd[n][k][l]);
        printf("%s,%.1f,%.1f,%.2f,%.1f,%.1f,%s\n", PATCH_NAMES[p], sc, vc, sc / vc, si, vi,
               same ? "yes" : "NO");
        if (!same) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "--check") && strcmp(argv[1], "--bench"))) {
        puts("Usage: spike_ma_osc_simd --check|--bench");
        return argc == 1 ? 0 : 1;
    }
    if (!__builtin_cpu_supports("avx2")) { puts("spike_ma_osc_simd: CPU has no AVX2; skipped"); return 0; }
    guard_frames_i = smoothing_frames(RATE);
    guard_frames_f = (float)guard_frames_i;
    return !strcmp(argv[1], "--bench") ? bench() : check(200000);
}
