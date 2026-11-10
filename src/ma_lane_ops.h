/* Mamut Analog lane width. Code written against this header runs one voice
 * (ML_W 1: float, bool, uint64_t) or eight pool voices per AVX2 vector
 * (ML_W 8) and performs the same float operations either way, so the two
 * widths give the same bits for each voice.
 *
 * Arithmetic is ordinary C: + - * / work on ml_f at both widths, and a float
 * operand is broadcast at width 8. Comparisons give an ml_m mask (bool, or
 * all-ones/zero int32 lanes); masks combine with & and |. What C cannot spell
 * the same way at both widths is named here:
 *
 *   ml_if(m, a, b)    m ? a : b, lane by lane
 *   ml_keep(m, a)     m ? a : +0
 *   ml_any, ml_all    whether some or every lane of a mask is set
 *   ml_fmadd          a b + c, fused (only where the scalar contract fuses)
 *   ml_u64_*          the 48-bit phase accumulators
 *   ml_load, ml_store one row of ML_W floats
 *   ml_trunc, ml_int_to_f, ML_INT, ml_ifi   int32 lanes
 *   ml_i16_pair       two neighbouring entries of an int16 table row
 *   ml_pick4          one of four constants per lane
 *
 * Include once per translation unit, after defining ML_W. */
#ifndef MA_LANE_OPS_H
#define MA_LANE_OPS_H

#include <stdint.h>

#if !defined(ML_W)
#error "define ML_W (1 or 8) before including ma_lane_ops.h"
#endif

#if ML_W == 1

typedef float ml_f;
typedef bool ml_m;
typedef int32_t ml_int;
typedef uint64_t ml_u64;

#define ML(x) (x)
#define ML_U64(x) ((uint64_t)(x))
#define ML_TRUE true
#define ML_FALSE false

static inline ml_f ml_if(ml_m m, ml_f a, ml_f b) { return m ? a : b; }
static inline ml_f ml_keep(ml_m m, ml_f a) { return m ? a : 0.0f; }
static inline ml_int ml_ifi(ml_m m, ml_int a, ml_int b) { return m ? a : b; }
static inline ml_m ml_not(ml_m m) { return !m; }
static inline bool ml_any(ml_m m) { return m; }
static inline bool ml_all(ml_m m) { return m; }
static inline ml_f ml_fmadd(ml_f a, ml_f b, ml_f c) { return __builtin_fmaf(a, b, c); }
static inline ml_f ml_fnmadd(ml_f a, ml_f b, ml_f c) { return __builtin_fmaf(-a, b, c); }
static inline ml_f ml_abs(ml_f x) { return tw_fabsf(x); }
static inline ml_f ml_load(const float *row) { return *row; }
static inline void ml_store(float *row, ml_f x) { *row = x; }
/* Adds one to count[lane] for each lane set in m. */
static inline void ml_tally(uint32_t *count, ml_m m) { *count += m; }
#define ML_INT(x) ((int32_t)(x))
static inline ml_int ml_trunc(ml_f x) { return (int32_t)x; }
static inline ml_f ml_int_to_f(ml_int i) { return (float)i; }
/* table[index], 0 <= index < 4 */
static inline ml_f ml_pick4(const float table[4], ml_int index) { return table[index]; }
/* table[at + index] and table[at + (index + 1) % length], length a power
 * of two */
static inline void ml_i16_pair(const int16_t *table, ml_int at, ml_int index, int length,
                               ml_int *first, ml_int *second) {
    *first = table[at + index];
    *second = table[at + ((index + 1) & (length - 1))];
}

static inline ml_u64 ml_u64_add(ml_u64 a, ml_u64 b) { return a + b; }
static inline ml_u64 ml_u64_sub(ml_u64 a, ml_u64 b) { return a - b; }
static inline ml_u64 ml_u64_and(ml_u64 a, ml_u64 b) { return a & b; }
static inline ml_u64 ml_u64_shl(ml_u64 a, int n) { return a << n; }
static inline ml_m ml_u64_gt(ml_u64 a, ml_u64 b) { return a > b; }
static inline ml_m ml_u64_ge(ml_u64 a, uint64_t bound) { return a >= bound; }
static inline ml_m ml_u64_nonzero(ml_u64 a) { return a != 0; }
static inline bool ml_u64_any(ml_u64 a) { return a != 0; }
static inline ml_u64 ml_u64_if(ml_m m, ml_u64 a, ml_u64 b) { return m ? a : b; }
static inline ml_u64 ml_u64_keep(ml_m m, ml_u64 a) { return m ? a : 0; }

/* phase_turns, and the exact integer/float conversions of the phase code */
static inline ml_f ml_turns(ml_u64 q) { return (float)(uint32_t)(q >> 16) * 0x1p-32f; }
static inline ml_f ml_u48_to_f(ml_u64 x) { return (float)x; }
static inline ml_u64 ml_f_to_u64(ml_f f) { return (uint64_t)f; }

#elif ML_W == 8

#if !defined(__AVX2__) || !defined(__FMA__)
#error "the vector lanes need AVX2 and FMA (x86-64: -march=x86-64-v3)"
#endif
#include <immintrin.h>

typedef __m256 ml_f;
typedef __m256i ml_i;                            /* raw 256-bit integer */
typedef int32_t ml_v8i __attribute__((vector_size(32)));
typedef ml_v8i ml_m;                             /* 0 or -1 per lane */
typedef ml_v8i ml_int;
typedef struct { ml_i lo, hi; } ml_u64;          /* lanes 0-3, 4-7 */

static inline ml_f ml_splat(float x) { return _mm256_set1_ps(x); }
static inline ml_f ml_same(ml_f x) { return x; }
/* A float or an ml_f, as an ml_f. */
#define ML(x) _Generic((x), float: ml_splat, default: ml_same)(x)
#define ML_TRUE ((ml_m){} == 0)
#define ML_FALSE ((ml_m){})

static inline ml_f ml_if_(ml_m m, ml_f a, ml_f b) {
    return _mm256_blendv_ps(b, a, _mm256_castsi256_ps((ml_i)m));
}
#define ml_if(m, a, b) ml_if_((m), ML(a), ML(b))
static inline ml_f ml_keep(ml_m m, ml_f a) {
    return _mm256_and_ps(_mm256_castsi256_ps((ml_i)m), a);
}
static inline ml_int ml_ifi(ml_m m, ml_int a, ml_int b) {
    return (ml_int)_mm256_blendv_epi8((ml_i)b, (ml_i)a, (ml_i)m);
}
static inline ml_m ml_not(ml_m m) { return ~m; }
static inline bool ml_any_m(ml_m m) { return !_mm256_testz_si256((ml_i)m, (ml_i)m); }
static inline bool ml_any_ps(ml_f m) { return !_mm256_testz_ps(m, m); }
/* Masks from the older lane code are __m256; both kinds are accepted. */
#define ml_any(m) _Generic((m), ml_m: ml_any_m, default: ml_any_ps)(m)
static inline bool ml_all(ml_m m) { return _mm256_testc_si256((ml_i)m, (ml_i)ML_TRUE); }
#define ml_fmadd(a, b, c) _mm256_fmadd_ps(ML(a), ML(b), ML(c))     /* ma_fmadd */
#define ml_fnmadd(a, b, c) _mm256_fnmadd_ps(ML(a), ML(b), ML(c))   /* ma_fnmadd */
static inline ml_f ml_abs(ml_f x) {
    return _mm256_and_ps(x, _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)));
}
static inline ml_f ml_load(const float *row) { return _mm256_loadu_ps(row); }
static inline void ml_store(float *row, ml_f x) { _mm256_storeu_ps(row, x); }
static inline void ml_tally(uint32_t *count, ml_m m) {
    unsigned bits = (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps((ml_i)m));
    for (; bits; bits &= bits - 1) count[__builtin_ctz(bits)]++;
}
#define ML_INT(x) ((ml_int){} + (x))
static inline ml_int ml_trunc(ml_f x) { return (ml_int)_mm256_cvttps_epi32(x); }
static inline ml_f ml_int_to_f(ml_int i) { return _mm256_cvtepi32_ps((ml_i)i); }
static inline ml_f ml_pick4(const float table[4], ml_int index) {
    const ml_f row = _mm256_setr_ps(table[0], table[1], table[2], table[3],
                                    table[3], table[3], table[3], table[3]);
    return _mm256_permutevar8x32_ps(row, (ml_i)index);
}
/* One 32-bit gather per row fetches the int16 pair (index, index + 1). The
 * last index wraps to 0 within its row, so it reads the pair before it and,
 * when any lane needs it, entry 0; no read leaves the row. */
static inline void ml_i16_pair(const int16_t *table, ml_int at, ml_int index, int length,
                               ml_int *first, ml_int *second) {
    const int *words = (const int *)(const void *)table;
    ml_m last = index == length - 1;
    ml_int word = ml_ifi(last, ML_INT(length - 2), index);
    ml_i pair = _mm256_i32gather_epi32(words, (ml_i)(at + word), 2);
    ml_i low = _mm256_srai_epi32(_mm256_slli_epi32(pair, 16), 16);
    ml_i high = _mm256_srai_epi32(pair, 16);
    *first = (ml_int)_mm256_blendv_epi8(low, high, (ml_i)last);
    *second = (ml_int)high;
    if (ml_any(last)) {
        ml_i start = _mm256_i32gather_epi32(words, (ml_i)at, 2);
        *second = (ml_int)_mm256_blendv_epi8(
            high, _mm256_srai_epi32(_mm256_slli_epi32(start, 16), 16), (ml_i)last);
    }
}

static inline ml_u64 ml_u64_set(uint64_t x) {
    return (ml_u64){ _mm256_set1_epi64x((long long)x), _mm256_set1_epi64x((long long)x) };
}
#define ML_U64(x) ml_u64_set(x)
static inline ml_u64 ml_u64_ld(const uint64_t a[8]) {
    return (ml_u64){ _mm256_loadu_si256((const ml_i *)a), _mm256_loadu_si256((const ml_i *)(a + 4)) };
}
static inline void ml_u64_st(uint64_t a[8], ml_u64 v) {
    _mm256_storeu_si256((ml_i *)a, v.lo);
    _mm256_storeu_si256((ml_i *)(a + 4), v.hi);
}
static inline ml_u64 ml_u64_add(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_add_epi64(a.lo, b.lo), _mm256_add_epi64(a.hi, b.hi) };
}
static inline ml_u64 ml_u64_sub(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_sub_epi64(a.lo, b.lo), _mm256_sub_epi64(a.hi, b.hi) };
}
static inline ml_u64 ml_u64_and(ml_u64 a, ml_u64 b) {
    return (ml_u64){ _mm256_and_si256(a.lo, b.lo), _mm256_and_si256(a.hi, b.hi) };
}
static inline ml_u64 ml_u64_srl(ml_u64 a, int n) {
    return (ml_u64){ _mm256_srli_epi64(a.lo, n), _mm256_srli_epi64(a.hi, n) };
}
static inline ml_u64 ml_u64_shl(ml_u64 a, int n) {
    return (ml_u64){ _mm256_slli_epi64(a.lo, n), _mm256_slli_epi64(a.hi, n) };
}
/* low 32 bits of each 64-bit lane, as 8 x i32 in lane order */
static inline ml_i ml_u64_low32(ml_u64 v) {
    const ml_i idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    return _mm256_permute2x128_si256(_mm256_permutevar8x32_epi32(v.lo, idx),
                                     _mm256_permutevar8x32_epi32(v.hi, idx), 0x20);
}
/* a 32-bit lane mask widened to the two 64-bit halves */
static inline ml_u64 ml_m64(ml_m m) {
    return (ml_u64){ _mm256_cvtepi32_epi64(_mm256_castsi256_si128((ml_i)m)),
                     _mm256_cvtepi32_epi64(_mm256_extracti128_si256((ml_i)m, 1)) };
}
static inline ml_m ml_u64_gt(ml_u64 a, ml_u64 b) {           /* both < 2^62 */
    return (ml_m)ml_u64_low32((ml_u64){
        _mm256_cmpgt_epi64(a.lo, b.lo), _mm256_cmpgt_epi64(a.hi, b.hi) });
}
static inline ml_m ml_u64_ge(ml_u64 a, uint64_t bound) {     /* both < 2^62 */
    return ml_u64_gt(a, ml_u64_set(bound - 1));
}
static inline ml_m ml_u64_nonzero(ml_u64 a) { return ml_u64_gt(a, ml_u64_set(0)); }
static inline bool ml_u64_any(ml_u64 a) {
    ml_i bits = _mm256_or_si256(a.lo, a.hi);
    return !_mm256_testz_si256(bits, bits);
}
static inline ml_u64 ml_u64_if(ml_m m, ml_u64 a, ml_u64 b) {
    ml_u64 m64 = ml_m64(m);
    return (ml_u64){ _mm256_blendv_epi8(b.lo, a.lo, m64.lo), _mm256_blendv_epi8(b.hi, a.hi, m64.hi) };
}
static inline ml_u64 ml_u64_keep(ml_m m, ml_u64 a) { return ml_u64_and(a, ml_m64(m)); }

/* (float)(uint32_t)u exactly: two exact 16-bit halves, one rounding */
static inline ml_f ml_u32_to_f(ml_i u) {
    ml_f hi = _mm256_cvtepi32_ps(_mm256_srli_epi32(u, 16));
    ml_f lo = _mm256_cvtepi32_ps(_mm256_and_si256(u, _mm256_set1_epi32(0xffff)));
    return hi * 65536.0f + lo;
}
static inline ml_f ml_turns(ml_u64 q) {          /* phase_turns */
    return ml_u32_to_f(ml_u64_low32(ml_u64_srl(q, 16))) * 0x1p-32f;
}
/* (float)x exactly for x <= 2^48: two exact 24-bit halves, one rounding */
static inline ml_f ml_u48_to_f(ml_u64 x) {
    ml_f hi = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_srl(x, 24)));
    ml_f lo = _mm256_cvtepi32_ps(ml_u64_low32(ml_u64_and(x, ml_u64_set(0xffffffu))));
    return hi * 16777216.0f + lo;
}
/* (uint64_t)f for +-0 <= f < 2^63: truncated mantissa * 2^(e - 150) */
static inline ml_i ml_f_to_u64_half(ml_i bits) {
    ml_i e = _mm256_and_si256(_mm256_srli_epi64(bits, 23), _mm256_set1_epi64x(0xff));
    ml_i m = _mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi64x(0x7fffff)),
                             _mm256_set1_epi64x(0x800000));
    return _mm256_or_si256(
        _mm256_sllv_epi64(m, _mm256_sub_epi64(e, _mm256_set1_epi64x(150))),
        _mm256_srlv_epi64(m, _mm256_sub_epi64(_mm256_set1_epi64x(150), e)));
}
static inline ml_u64 ml_f_to_u64(ml_f f) {
    ml_i bits = _mm256_castps_si256(f);
    return (ml_u64){
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_castsi256_si128(bits))),
        ml_f_to_u64_half(_mm256_cvtepu32_epi64(_mm256_extracti128_si256(bits, 1))) };
}

#else
#error "ML_W must be 1 or 8"
#endif

/* ---- at both widths ---- */

/* A ring holds ML_W floats per entry: one row of lanes. */
static inline void ml_push(float *ring, int length, uint8_t *position, ml_f x) {
    ml_store(ring + *position * ML_W, x);
    if (++*position == length) *position = 0;
}

/* A symmetric halfband over a ring whose newest entry sits just before
 * `position`. Tap t reads entry position - 1 - t (mod length). Each nonzero
 * pair t, length - 1 - t is added first and fused into the sum, smallest
 * coefficient first, and the centre tap comes last
 * (docs/ma3-fir-fold-20261109.md). */
static inline ml_f ml_halfband(const float *ring, int length, int position,
                               const float h[], const uint8_t pair[], int pairs,
                               int centre) {
    ml_f output = ML(0.0f);
    for (int k = 0; k < pairs; k++) {
        int tap = pair[k];
        int near = position - 1 - tap, far = position - length + tap;
        if (near < 0) near += length;
        if (far < 0) far += length;
        output = ml_fmadd(h[tap], ml_load(ring + near * ML_W) + ml_load(ring + far * ML_W),
                          output);
    }
    int middle = position - 1 - centre;
    if (middle < 0) middle += length;
    return ml_fmadd(h[centre], ml_load(ring + middle * ML_W), output);
}

/* Small helpers of the voice DSP. */

static inline ml_f ml_clamp_(ml_f v, ml_f lo, ml_f hi) {
    return ml_if(v < lo, lo, ml_if(v > hi, hi, v));
}
/* lo where v < lo, hi where v > hi, else v */
#define ml_clamp(v, lo, hi) ml_clamp_((v), ML(lo), ML(hi))
static inline ml_f ml_smooth5(ml_f x) {
    return x * x * x * ml_fmadd(x, ml_fmadd(6.0f, x, -15.0f), 10.0f);
}
static inline ml_f ml_wrap(ml_f phase) {
    phase = ml_if(phase >= 1.0f, phase - 1.0f, phase);
    return ml_if(phase < 0.0f, phase + 1.0f, phase);
}
static inline ml_f ml_sin_turns(ml_f p) {
    ml_f t = ml_if(p < 0.25f, p, ml_if(p < 0.75f, 0.5f - p, p - 1.0f));
    ml_f s = t * t;
    ml_f poly = ml_fmadd(s, 42.0586939f, -76.7058597f);
    poly = ml_fmadd(s, poly, 81.6052493f);
    poly = ml_fmadd(s, poly, -41.3417022f);
    poly = ml_fmadd(s, poly, 6.28318531f);
    return t * poly;
}
static inline ml_f ml_sat(ml_f x) {
    x = ml_if(x > 3.0f, 3.0f, ml_if(x < -3.0f, -3.0f, x));
    return x * ml_fmadd(x, x, 27.0f) / ml_fmadd(9.0f * x, x, 27.0f);
}
static inline ml_f ml_octave_ratio_small(ml_f octaves) {      /* octave_ratio_small */
    octaves = ml_clamp(octaves, -0.25f, 0.25f);
    ml_f value = ML(EXP2_SMALL[5]);
    for (int i = 4; i >= 0; i--) value = value * octaves + EXP2_SMALL[i];
    return value;
}
/* The ladder's g for a cutoff (ma-constants.md 6.1). */
static inline ml_f ml_prewarp(ml_f cutoff_hz, ml_f sample_rate_hz) {
    ml_f x = cutoff_hz / (2.0f * sample_rate_hz);
    ml_f z = 2.0f * x * (1.0f / 0.42f) - 1.0f;
    ml_f b1 = ML(0.0f), b2 = ML(0.0f);
    for (int k = 6; k >= 1; k--) {
        ml_f b = 2.0f * z * b1 - b2 + PREWARP_CHEB[k];
        b2 = b1;
        b1 = b;
    }
    return x * (z * b1 - b2 + PREWARP_CHEB[0]);
}
static inline ml_f ml_phase_step(ml_f hz, ml_f rate) {         /* phase_step */
    return ml_clamp(hz / rate, 0.0f, 0.45f);
}

#endif
