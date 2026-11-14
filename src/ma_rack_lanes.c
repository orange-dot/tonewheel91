/* AVX2 wavefront for a settled rack EQ (docs/ma-rack-plan.md RKD25).
 *
 * The bands of an EQ are a serial chain, so the eight lanes cannot hold
 * eight bands of one sample. They hold the chain as a pipeline instead:
 * at step t, lane k runs band k on sample t - k, fed by lane k - 1's output
 * of step t - 1. A run of n frames takes n + 7 steps. Each lane performs
 * exactly the scalar svf_tick of src/ma_rack.c, operation for operation
 * (no fusion: the build has -ffp-contract=off), so the bits match; a lane
 * outside its run [k, n + k) or whose band is off neither keeps its result
 * nor moves its state. Freestanding: intrinsics only. */
#include "ma_internal.h"

#if defined(__x86_64__) && defined(__AVX2__)
#include <immintrin.h>

static inline float lane7(__m256 y) {
    __m128 hi = _mm256_extractf128_ps(y, 1);
    return _mm_cvtss_f32(_mm_shuffle_ps(hi, hi, 0xff));
}

bool ma_rack_eq_wavefront(ma_rack_block *b, uint8_t active, const ma_frame *in, ma_frame *out, size_t n) {
    static_assert(MA_EQ_BANDS == 8, "one band per AVX2 lane");
    if (n > 0x7fffffff) return false;
    float a1[8], a2[8], a3[8], m0[8], m1[8], m2[8], s[4][8];
    for (int k = 0; k < 8; k++) {
        const ma_rack_svf *f = &b->svf[k];
        bool on = active >> k & 1u;
        a1[k] = on ? f->a1 : 0;
        a2[k] = on ? f->a2 : 0;
        a3[k] = on ? f->a3 : 0;
        m0[k] = on ? f->m0 : 0;
        m1[k] = on ? f->m1 : 0;
        m2[k] = on ? f->m2 : 0;
        s[0][k] = f->s1[0];
        s[1][k] = f->s2[0];
        s[2][k] = f->s1[1];
        s[3][k] = f->s2[1];
    }
    const __m256 A1 = _mm256_loadu_ps(a1), A2 = _mm256_loadu_ps(a2), A3 = _mm256_loadu_ps(a3);
    const __m256 M0 = _mm256_loadu_ps(m0), M1 = _mm256_loadu_ps(m1), M2 = _mm256_loadu_ps(m2);
    const __m256i lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i bits = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
    const __m256 act = _mm256_castsi256_ps(
        _mm256_cmpgt_epi32(_mm256_and_si256(_mm256_set1_epi32(active), bits), _mm256_setzero_si256()));
    const __m256i up = _mm256_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6);   /* lane k <- lane k - 1 */
    const __m256 two = _mm256_set1_ps(2.0f), tiny = _mm256_set1_ps(1e-20f);
    const __m256 magnitude = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
    const __m256i before = _mm256_set1_epi32(-1), length = _mm256_set1_epi32((int)n);
    __m256 S1L = _mm256_loadu_ps(s[0]), S2L = _mm256_loadu_ps(s[1]);
    __m256 S1R = _mm256_loadu_ps(s[2]), S2R = _mm256_loadu_ps(s[3]);
    __m256 YL = _mm256_setzero_ps(), YR = _mm256_setzero_ps();

#define WAVE_STEP(V, S1, S2, Y, UPD) do {                                          \
        __m256 v3 = _mm256_sub_ps(V, S2);                                          \
        __m256 v1 = _mm256_add_ps(_mm256_mul_ps(A1, S1), _mm256_mul_ps(A2, v3));   \
        __m256 v2 = _mm256_add_ps(_mm256_add_ps(S2, _mm256_mul_ps(A2, S1)),        \
                                  _mm256_mul_ps(A3, v3));                          \
        __m256 n1 = _mm256_sub_ps(_mm256_mul_ps(two, v1), S1);                     \
        __m256 n2 = _mm256_sub_ps(_mm256_mul_ps(two, v2), S2);                     \
        n1 = _mm256_andnot_ps(_mm256_cmp_ps(_mm256_and_ps(n1, magnitude), tiny, _CMP_LT_OQ), n1); \
        n2 = _mm256_andnot_ps(_mm256_cmp_ps(_mm256_and_ps(n2, magnitude), tiny, _CMP_LT_OQ), n2); \
        __m256 y = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(M0, V), _mm256_mul_ps(M1, v1)), \
                                 _mm256_mul_ps(M2, v2));                           \
        Y = _mm256_blendv_ps(V, y, act);                                           \
        S1 = _mm256_blendv_ps(S1, n1, UPD);                                        \
        S2 = _mm256_blendv_ps(S2, n2, UPD);                                        \
    } while (0)

    for (size_t t = 0; t < n + 7; t++) {
        float xl = t < n ? in[t].left : 0.0f, xr = t < n ? in[t].right : 0.0f;
        __m256i d = _mm256_sub_epi32(_mm256_set1_epi32((int)t), lane);       /* t - k */
        __m256 valid = _mm256_castsi256_ps(
            _mm256_and_si256(_mm256_cmpgt_epi32(d, before), _mm256_cmpgt_epi32(length, d)));
        __m256 upd = _mm256_and_ps(valid, act);
        __m256 VL = _mm256_blend_ps(_mm256_permutevar8x32_ps(YL, up), _mm256_set1_ps(xl), 1);
        __m256 VR = _mm256_blend_ps(_mm256_permutevar8x32_ps(YR, up), _mm256_set1_ps(xr), 1);
        WAVE_STEP(VL, S1L, S2L, YL, upd);
        WAVE_STEP(VR, S1R, S2R, YR, upd);
        if (t >= 7) out[t - 7] = (ma_frame){ lane7(YL), lane7(YR) };
    }
#undef WAVE_STEP
    _mm256_storeu_ps(s[0], S1L);
    _mm256_storeu_ps(s[1], S2L);
    _mm256_storeu_ps(s[2], S1R);
    _mm256_storeu_ps(s[3], S2R);
    for (int k = 0; k < 8; k++)
        if (active >> k & 1u) {
            ma_rack_svf *f = &b->svf[k];
            f->s1[0] = s[0][k];
            f->s2[0] = s[1][k];
            f->s1[1] = s[2][k];
            f->s2[1] = s[3][k];
        }
    return true;
}

#else
bool ma_rack_eq_wavefront(ma_rack_block *b, uint8_t active, const ma_frame *in, ma_frame *out, size_t n) {
    (void)b; (void)active; (void)in; (void)out; (void)n;
    return false;
}
#endif
