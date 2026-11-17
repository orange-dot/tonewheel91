/* Insert rack: the chain host (RK1), the parametric EQ (RK1b), the utility
 * block, and the RK2 kinds: chorus and reverb (the MA3-2 kernels of
 * ma_effects.c), delay, saturator and compressor. Freestanding C23: no OS,
 * no libm, no allocation. Plan and decisions: docs/ma-rack-plan.md
 * (RKD1..RKD12, RK1b, RK2). */
#include <stddef.h>

#include "ma_internal.h"

static constexpr float FADE_SECONDS = .010f;     /* RKD6 [decision] */
static constexpr float LOG2_10 = 3.32192809f;
static constexpr float MAX_RATIO = .45f;         /* frequency clamp, x fs */
static constexpr float MIN_HZ = 10.0f;
/* RK2 [decision]: a delay's read distance follows a time change like a tape
 * machine changing speed; the saturator's tilts turn about one pivot; the
 * compressor's RMS detector averages the squared signal over one window. */
static constexpr float DELAY_SLEW_S = .050f;
static constexpr float TILT_PIVOT_HZ = 650.0f;
static constexpr float RMS_SECONDS = .010f;
static constexpr float DB_PER_OCTAVE = 6.02059991f;   /* 20 log10 2 */
static constexpr float TEMPO_DEFAULT = 120.0f;
static constexpr float TEMPO_MIN = 20.0f;
static constexpr float TEMPO_MAX = 400.0f;

static_assert(MA_EQ_BANDS <= 64, "the moved-band mask is a uint64_t");

/* --- parameter tables ----------------------------------------------------- */

static const ma_rack_param_info EQ_HEAD[MA_EQ_FIRST_BAND] = {
    [MA_EQ_BAND_COUNT] = { 0, MA_EQ_BANDS, 0, MA_RACK_STEPPED },
    [MA_EQ_OUTPUT_DB]  = { -24, 24, 0, MA_RACK_LINEAR },
};
static const ma_rack_param_info EQ_FIELD[MA_EQ_BAND_FIELDS] = {
    [MA_EQ_TYPE]    = { 0, MA_EQ_TYPE_COUNT - 1, MA_EQ_BELL, MA_RACK_STEPPED },
    [MA_EQ_FREQ_HZ] = { 10, 22000, 1000, MA_RACK_LOG },
    [MA_EQ_GAIN_DB] = { -24, 24, 0, MA_RACK_LINEAR },
    [MA_EQ_Q]       = { .1f, 40, 1, MA_RACK_LOG },
    [MA_EQ_ON]      = { 0, 1, 1, MA_RACK_TOGGLE },
};
static const ma_rack_param_info UTILITY_INFO[MA_UTILITY_PARAM_COUNT] = {
    [MA_UTILITY_GAIN_DB] = { -24, 24, 0, MA_RACK_LINEAR },
    [MA_UTILITY_WIDTH]   = { 0, 2, 1, MA_RACK_LINEAR },
    [MA_UTILITY_PAN]     = { -1, 1, 0, MA_RACK_LINEAR },
};

static const ma_rack_param_info CHORUS_INFO[MA_CHORUS_PARAM_COUNT] = {
    [MA_CHORUS_DEPTH]   = { 0, 1, .35f, MA_RACK_LINEAR },
    [MA_CHORUS_RATE_HZ] = { .03f, 20, .25f, MA_RACK_LOG },
};
static const ma_rack_param_info REVERB_INFO[MA_REVERB_PARAM_COUNT] = {
    [MA_REVERB_SIZE]    = { 0, 1, .65f, MA_RACK_LINEAR },
    [MA_REVERB_DAMPING] = { 0, .99f, .55f, MA_RACK_LINEAR },
};
static const ma_rack_param_info DELAY_INFO[MA_DELAY_PARAM_COUNT] = {
    [MA_DELAY_TIME_S]     = { .001f, 2.7f, .35f, MA_RACK_LOG },
    [MA_DELAY_SYNC]       = { 0, MA_DELAY_SYNC_COUNT - 1, MA_DELAY_SYNC_FREE, MA_RACK_STEPPED },
    [MA_DELAY_FEEDBACK]   = { 0, .95f, .35f, MA_RACK_LINEAR },
    [MA_DELAY_PINGPONG]   = { 0, 1, 0, MA_RACK_TOGGLE },
    [MA_DELAY_LOWCUT_HZ]  = { 20, 2000, 120, MA_RACK_LOG },
    [MA_DELAY_HIGHCUT_HZ] = { 500, 20000, 6000, MA_RACK_LOG },
};
/* The input gain's initial value puts the rack's measured chord level
 * (about -20 dBFS, docs/ma-rack-rk1.md) at the knee of the triode curve. */
static const ma_rack_param_info SATURATOR_INFO[MA_SATURATOR_PARAM_COUNT] = {
    [MA_SATURATOR_DRIVE]        = { 0, 1, .5f, MA_RACK_LINEAR },
    [MA_SATURATOR_SHAPE]        = { 0, 1, 0, MA_RACK_LINEAR },
    [MA_SATURATOR_INPUT_DB]     = { -12, 48, 20, MA_RACK_LINEAR },
    [MA_SATURATOR_TILT_PRE_DB]  = { -12, 12, 0, MA_RACK_LINEAR },
    [MA_SATURATOR_TILT_POST_DB] = { -12, 12, 0, MA_RACK_LINEAR },
    [MA_SATURATOR_OUTPUT_DB]    = { -24, 24, 0, MA_RACK_LINEAR },
};
/* The threshold's initial value sits under the rack's measured chord level,
 * about -31 dBFS RMS (docs/ma-rack-rk1.md), so a new compressor works on a
 * chord and leaves a single quiet note alone. */
static const ma_rack_param_info COMPRESSOR_INFO[MA_COMPRESSOR_PARAM_COUNT] = {
    [MA_COMPRESSOR_DETECTOR]     = { 0, MA_COMPRESSOR_DETECTOR_COUNT - 1, MA_COMPRESSOR_RMS, MA_RACK_STEPPED },
    [MA_COMPRESSOR_THRESHOLD_DB] = { -60, 0, -36, MA_RACK_LINEAR },
    [MA_COMPRESSOR_RATIO]        = { 1, 20, 4, MA_RACK_LOG },
    [MA_COMPRESSOR_KNEE_DB]      = { 0, 24, 6, MA_RACK_LINEAR },
    [MA_COMPRESSOR_ATTACK_MS]    = { .1f, 200, 10, MA_RACK_LOG },
    [MA_COMPRESSOR_RELEASE_MS]   = { 5, 2000, 150, MA_RACK_LOG },
    [MA_COMPRESSOR_MAKEUP_DB]    = { -24, 24, 0, MA_RACK_LINEAR },
};

/* Per kind: its parameter table (the EQ's is built from two), the mix a new
 * block starts with, and how many a chain may hold (0 = no cap). */
static const struct {
    const ma_rack_param_info *info;
    unsigned count;
    float mix;
    unsigned cap;
} KIND[MA_RACK_KIND_COUNT] = {
    [MA_RACK_EQ]         = { nullptr, MA_EQ_PARAM_COUNT, 1, 0 },
    [MA_RACK_UTILITY]    = { UTILITY_INFO, MA_UTILITY_PARAM_COUNT, 1, 0 },
    [MA_RACK_CHORUS]     = { CHORUS_INFO, MA_CHORUS_PARAM_COUNT, .30f, MA_RACK_CHORUSES },
    [MA_RACK_REVERB]     = { REVERB_INFO, MA_REVERB_PARAM_COUNT, .25f, MA_RACK_REVERBS },
    [MA_RACK_DELAY]      = { DELAY_INFO, MA_DELAY_PARAM_COUNT, .30f, MA_RACK_DELAYS },
    [MA_RACK_SATURATOR]  = { SATURATOR_INFO, MA_SATURATOR_PARAM_COUNT, 1, 0 },
    [MA_RACK_COMPRESSOR] = { COMPRESSOR_INFO, MA_COMPRESSOR_PARAM_COUNT, 1, 0 },
};

unsigned ma_rack_param_count(ma_rack_kind kind) {
    return kind < MA_RACK_KIND_COUNT ? KIND[kind].count : 0;
}

float ma_rack_mix_initial(ma_rack_kind kind) {
    return kind < MA_RACK_KIND_COUNT ? KIND[kind].mix : 0;
}

static bool info_at(ma_rack_kind kind, unsigned index, ma_rack_param_info *info) {
    if (kind >= MA_RACK_KIND_COUNT || index >= KIND[kind].count) return false;
    if (kind == MA_RACK_EQ)
        *info = index < MA_EQ_FIRST_BAND ? EQ_HEAD[index]
              : EQ_FIELD[(index - MA_EQ_FIRST_BAND) % MA_EQ_BAND_FIELDS];
    else
        *info = KIND[kind].info[index];
    return true;
}

float ma_delay_sync_beats(unsigned sync) {
    static const float BEATS[MA_DELAY_SYNC_COUNT] = {
        [MA_DELAY_SYNC_32] = .125f, [MA_DELAY_SYNC_16T] = 1.0f / 6, [MA_DELAY_SYNC_16] = .25f,
        [MA_DELAY_SYNC_8T] = 1.0f / 3, [MA_DELAY_SYNC_16D] = .375f, [MA_DELAY_SYNC_8] = .5f,
        [MA_DELAY_SYNC_4T] = 2.0f / 3, [MA_DELAY_SYNC_8D] = .75f, [MA_DELAY_SYNC_4] = 1,
        [MA_DELAY_SYNC_2T] = 4.0f / 3, [MA_DELAY_SYNC_4D] = 1.5f, [MA_DELAY_SYNC_2] = 2,
        [MA_DELAY_SYNC_1] = 4,
    };
    return sync < MA_DELAY_SYNC_COUNT ? BEATS[sync] : 0;
}

bool ma_rack_param_info_at(ma_rack_kind kind, unsigned index, ma_rack_param_info *info) {
    return info && info_at(kind, index, info);
}

static bool finite(float x) { return x >= -FLT_MAX && x <= FLT_MAX; }

/* Volatile byte reads keep the compiler from emitting a hosted memcpy (the
 * core audit allows memset only). Used between frames, never per sample. */
static void copy_bytes(void *to, const void *from, size_t n) {
    unsigned char *dst = to;
    const volatile unsigned char *src = from;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

void ma_rack_slot_init(ma_rack_slot_spec *slot, ma_rack_kind kind, uint32_t id) {
    if (!slot) return;
    *slot = (ma_rack_slot_spec){ 0 };
    unsigned count = ma_rack_param_count(kind);
    if (!count || !id) return;
    slot->id = id;
    slot->kind = kind;
    slot->enabled = true;
    slot->mix = KIND[kind].mix;
    for (unsigned i = 0; i < count; i++) {
        ma_rack_param_info info = { 0 };
        (void)info_at(kind, i, &info);
        slot->param[i] = info.initial;
    }
}

static bool slot_valid(const ma_rack_slot_spec *s) {
    if (s->kind == MA_RACK_EMPTY) {
        if (s->id || s->enabled || s->mix != 0) return false;
        for (unsigned i = 0; i < MA_RACK_PARAMS; i++)
            if (s->param[i] != 0) return false;
        return true;
    }
    unsigned count = ma_rack_param_count(s->kind);
    if (!count || !s->id || !finite(s->mix) || s->mix < 0 || s->mix > 1) return false;
    for (unsigned i = 0; i < MA_RACK_PARAMS; i++) {
        float v = s->param[i];
        if (i >= count) {
            if (v != 0) return false;
            continue;
        }
        ma_rack_param_info info = { 0 };
        (void)info_at(s->kind, i, &info);
        if (!finite(v) || v < info.minimum || v > info.maximum) return false;
        if (info.curve == MA_RACK_TOGGLE && v != 0 && v != 1) return false;
        if (info.curve == MA_RACK_STEPPED && v != (float)(int)v) return false;
    }
    if (s->kind == MA_RACK_EQ) {
        /* Canonical: a band past the count holds its initial values. */
        unsigned bands = (unsigned)s->param[MA_EQ_BAND_COUNT];
        for (unsigned b = bands; b < MA_EQ_BANDS; b++)
            for (unsigned f = 0; f < MA_EQ_BAND_FIELDS; f++)
                if (s->param[MA_EQ_BAND(b, f)] != EQ_FIELD[f].initial) return false;
    }
    return true;
}

bool ma_rack_spec_valid(const ma_rack_spec *spec) {
    if (!spec) return false;
    unsigned held[MA_RACK_KIND_COUNT] = { 0 };
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++) {
        if (!slot_valid(&spec->slot[i])) return false;
        ma_rack_kind kind = spec->slot[i].kind;
        if (kind == MA_RACK_EMPTY) continue;
        if (KIND[kind].cap && ++held[kind] > KIND[kind].cap) return false;   /* RKD8 */
        for (unsigned j = 0; j < i; j++)
            if (spec->slot[j].kind != MA_RACK_EMPTY && spec->slot[j].id == spec->slot[i].id)
                return false;
    }
    return true;
}

bool ma_rack_spec_empty(const ma_rack_spec *spec) {
    if (!spec) return true;
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++)
        if (spec->slot[i].kind != MA_RACK_EMPTY) return false;
    return true;
}

/* --- the session's chorus and reverb as blocks ----------------------------- */

static float bound(float x, float lo, float hi) {
    if (!finite(x)) return lo;
    return x < lo ? lo : x > hi ? hi : x;
}

/* Both kinds have two parameters, in the order of the old controls. */
static bool session_block(ma_rack_spec *spec, ma_rack_kind kind, uint32_t id, float mix, float a, float b) {
    if (!spec) return false;
    mix = bound(mix, 0, 1);
    a = bound(a, KIND[kind].info[0].minimum, KIND[kind].info[0].maximum);
    b = bound(b, KIND[kind].info[1].minimum, KIND[kind].info[1].maximum);
    ma_rack_slot_spec *slot = nullptr;
    int last = -1;
    unsigned occupied = 0;
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++) {
        if (spec->slot[i].kind == MA_RACK_EMPTY) continue;
        if (spec->slot[i].kind == kind && !slot) slot = &spec->slot[i];
        else if (spec->slot[i].id == id) return false;   /* the reserved id is taken */
        last = (int)i;
        occupied++;
    }
    if (!slot) {
        if (mix == 0) return true;
        if (occupied == MA_RACK_SLOTS) return false;
        if (last == MA_RACK_SLOTS - 1) {            /* close the gaps to free the end */
            unsigned n = 0;
            for (unsigned i = 0; i < MA_RACK_SLOTS; i++) {
                if (spec->slot[i].kind == MA_RACK_EMPTY) continue;
                if (n != i) {
                    copy_bytes(&spec->slot[n], &spec->slot[i], sizeof spec->slot[n]);
                    spec->slot[i] = (ma_rack_slot_spec){ 0 };
                }
                n++;
            }
            last = (int)n - 1;
        }
        unsigned at = (unsigned)(last + 1);
        if (kind == MA_RACK_CHORUS && last >= 0 && spec->slot[last].kind == MA_RACK_REVERB
            && spec->slot[last].id == MA_RACK_ID_REVERB) {
            copy_bytes(&spec->slot[at], &spec->slot[last], sizeof spec->slot[at]);
            at = (unsigned)last;
        }
        slot = &spec->slot[at];
        ma_rack_slot_init(slot, kind, id);
    }
    slot->enabled = true;
    slot->mix = mix;
    slot->param[0] = a;
    slot->param[1] = b;
    return true;
}

bool ma_rack_spec_set_chorus(ma_rack_spec *spec, float mix, float depth, float rate_hz) {
    return session_block(spec, MA_RACK_CHORUS, MA_RACK_ID_CHORUS, mix, depth, rate_hz);
}

bool ma_rack_spec_set_reverb(ma_rack_spec *spec, float mix, float size, float damping) {
    return session_block(spec, MA_RACK_REVERB, MA_RACK_ID_REVERB, mix, size, damping);
}

bool ma_rack_spec_set_effects(ma_rack_spec *spec, ma_chorus_controls c, ma_reverb_controls r) {
    return (!c.enabled || ma_rack_spec_set_chorus(spec, c.mix, c.depth, c.rate_hz))
        && (!r.enabled || ma_rack_spec_set_reverb(spec, r.mix, r.size, r.damping));
}

/* --- smoothers: the voice's 6 ms linear ramp ------------------------------ */

static void smoother_snap(ma_smoother *s, float v) {
    *s = (ma_smoother){ .current = v, .target = v };
}
static bool smoother_to(ma_smoother *s, float target, uint16_t frames) {
    if (target == s->target) return false;
    s->target = target;
    s->remaining = frames;
    s->step = (target - s->current) / (float)frames;
    return true;
}
static float smoother_next(ma_smoother *s) {
    if (s->remaining == 0) return s->current;
    s->current += s->step;
    if (--s->remaining == 0) s->current = s->target;
    return s->current;
}

/* --- freestanding coefficient math ---------------------------------------- */

/* 2^x for |x| < 126: integer part into the exponent, 2^(f - .5) by its
 * degree-7 Taylor series in (f - .5) ln 2, error below 1e-8 relative. */
static float exp2_approx(float x) {
    if (!(x > -126.0f)) x = -126.0f;
    if (x > 126.0f) x = 126.0f;
    int32_t i = (int32_t)x;
    if ((float)i > x) i--;
    float t = (x - (float)i - .5f) * .693147181f;
    float p = 1 + t * (1 + t * (.5f + t * (1.0f / 6 + t * (1.0f / 24
            + t * (1.0f / 120 + t * (1.0f / 720 + t * (1.0f / 5040)))))));
    union { uint32_t u; float f; } scale = { .u = (uint32_t)(i + 127) << 23 };
    return p * 1.41421356f * scale.f;
}

/* log2(x) for a positive normal x: the exponent, plus 2 atanh(z) / ln 2 with
 * z = (m - 1) / (m + 1) for the mantissa m in [sqrt(1/2), sqrt 2), by its
 * odd series to z^9; error below 3e-7. */
static float log2_approx(float x) {
    union { float f; uint32_t u; } v = { .f = x };
    int32_t e = (int32_t)(v.u >> 23 & 255u) - 127;
    v.u = (v.u & UINT32_C(0x007fffff)) | UINT32_C(0x3f800000);
    float m = v.f;
    if (m > 1.41421356f) {
        m *= .5f;
        e++;
    }
    float z = (m - 1) / (m + 1), z2 = z * z;
    float p = z * (1 + z2 * (1.0f / 3 + z2 * (1.0f / 5 + z2 * (1.0f / 7 + z2 * (1.0f / 9)))));
    return (float)e + p * 2.88539008f;
}

/* tan(pi f / fs) from the repo sine kernel, f clamped to [10 Hz, .45 fs]. */
static float tan_pi(float hz, float fs) {
    float x = hz / fs;
    if (x < MIN_HZ / fs) x = MIN_HZ / fs;
    if (x > MAX_RATIO) x = MAX_RATIO;
    float p = .5f * x;
    return tw_sin_turns(p) / tw_sin_turns(p + .25f);
}

static void svf_set(ma_rack_svf *f, float g, float k, float m0, float m1, float m2) {
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
    f->m0 = m0;
    f->m1 = m1;
    f->m2 = m2;
}

static float snap(float v) { return tw_fabsf(v) < 1e-20f ? 0.0f : v; }

/* One TPT state-variable step (Zavalishin/Simper): v1 band, v2 low. */
static float svf_tick(ma_rack_svf *f, int ch, float v0) {
    float v3 = v0 - f->s2[ch];
    float v1 = f->a1 * f->s1[ch] + f->a2 * v3;
    float v2 = f->s2[ch] + f->a2 * f->s1[ch] + f->a3 * v3;
    f->s1[ch] = snap(2.0f * v1 - f->s1[ch]);
    f->s2[ch] = snap(2.0f * v2 - f->s2[ch]);
    return f->m0 * v0 + f->m1 * v1 + f->m2 * v2;
}

static void svf_clear(ma_rack_svf *f) {
    f->s1[0] = f->s1[1] = f->s2[0] = f->s2[1] = 0;
    f->active = false;
}

/* Simper's forms: A = 10^(dB/40), k = 1/Q (the bell: 1/(Q A)). */
static void band_coefficients(ma_rack_svf *f, const ma_smoother *p, unsigned b, float fs) {
    float hz = p[MA_EQ_BAND(b, MA_EQ_FREQ_HZ)].current;
    float db = p[MA_EQ_BAND(b, MA_EQ_GAIN_DB)].current;
    float q = p[MA_EQ_BAND(b, MA_EQ_Q)].current;
    float g = tan_pi(hz, fs), k = 1.0f / q;
    switch (f->type) {
    case MA_EQ_BELL: {
        float a = exp2_approx(db * (LOG2_10 / 40));
        k = 1.0f / (q * a);
        svf_set(f, g, k, 1, k * (a * a - 1), 0);
        return;
    }
    case MA_EQ_LOW_SHELF: {
        float a = exp2_approx(db * (LOG2_10 / 40)), root = exp2_approx(db * (LOG2_10 / 80));
        svf_set(f, g / root, k, 1, k * (a - 1), a * a - 1);
        return;
    }
    case MA_EQ_HIGH_SHELF: {
        float a = exp2_approx(db * (LOG2_10 / 40)), root = exp2_approx(db * (LOG2_10 / 80));
        svf_set(f, g * root, k, a * a, k * (1 - a) * a, 1 - a * a);
        return;
    }
    case MA_EQ_LOW_CUT: svf_set(f, g, k, 1, -k, -1); return;
    case MA_EQ_HIGH_CUT: svf_set(f, g, k, 0, 0, 1); return;
    default: svf_set(f, g, k, 1, -k, 0); return;   /* notch */
    }
}

/* A gain shape at 0 dB, now and as a target, is the identity: skipped. */
static bool band_neutral(const ma_smoother *p, unsigned b, int type) {
    const ma_smoother *gain = &p[MA_EQ_BAND(b, MA_EQ_GAIN_DB)];
    return type <= MA_EQ_HIGH_SHELF && gain->current == 0 && gain->target == 0;
}

static bool band_wanted(const ma_smoother *p, unsigned b, unsigned count, int type) {
    return b < count && p[MA_EQ_BAND(b, MA_EQ_ON)].current != 0 && !band_neutral(p, b, type);
}

/* Each band crossfades its own output against its input with `weight`: a
 * band switched on, off, past the count, or changing shape fades over the
 * ramp time, swaps its coefficients at zero and fades back. Returns false
 * when no band ran and the output gain is 0 dB, so the block hands its
 * input on untouched. `moved` marks the bands whose values ramped. */
static bool eq_tick(ma_rack *r, ma_rack_block *b, ma_frame x, ma_frame *out, uint64_t moved) {
    const ma_smoother *p = b->param;
    unsigned count = (unsigned)p[MA_EQ_BAND_COUNT].current;
    float step = 1.0f / (float)r->smooth_frames;
    float v[2] = { x.left, x.right };
    bool any = false;
    unsigned span = count;
    for (unsigned k = 0; k < b->span || k < count; k++) {
        ma_rack_svf *f = &b->svf[k];
        int type = (int)p[MA_EQ_BAND(k, MA_EQ_TYPE)].current;
        bool want = band_wanted(p, k, count, type);
        if (f->type != type) {
            if (f->weight > 0) want = false;          /* fade out, then swap */
            else {
                f->type = (uint8_t)type;
                svf_clear(f);
            }
        }
        if (want && f->weight < 1) f->weight = f->weight + step >= 1 ? 1 : f->weight + step;
        else if (!want && f->weight > 0) f->weight = f->weight - step <= 0 ? 0 : f->weight - step;
        if (f->weight == 0) {
            if (f->active) svf_clear(f);
            continue;
        }
        if (k + 1 > span) span = k + 1;
        if (!f->active) {
            svf_clear(f);
            f->active = true;
            band_coefficients(f, p, k, r->sample_rate_hz);
        } else if (moved >> k & 1u) {
            band_coefficients(f, p, k, r->sample_rate_hz);
        }
        any = true;
        float w = f->weight;
        for (int ch = 0; ch < 2; ch++) {
            float y = svf_tick(f, ch, v[ch]);
            v[ch] = w == 1.0f ? y : v[ch] + w * (y - v[ch]);
        }
    }
    b->span = (uint8_t)span;
    const ma_smoother *output = &p[MA_EQ_OUTPUT_DB];
    if (output->current != 0) {
        float gain = exp2_approx(output->current * (LOG2_10 / 20));
        v[0] *= gain;
        v[1] *= gain;
        any = true;
    }
    if (!any) return false;
    *out = (ma_frame){ v[0], v[1] };
    return true;
}

static bool utility_tick(ma_rack_block *b, ma_frame x, ma_frame *out) {
    float gain_db = b->param[MA_UTILITY_GAIN_DB].current;
    float width = b->param[MA_UTILITY_WIDTH].current;
    float pan = b->param[MA_UTILITY_PAN].current;
    if (gain_db == 0 && width == 1 && pan == 0) return false;
    float l = x.left, r = x.right;
    if (width != 1) {
        float mid = .5f * (l + r), side = .5f * (l - r) * width;
        l = mid + side;
        r = mid - side;
    }
    if (pan > 0) l *= 1 - pan;
    else if (pan < 0) r *= 1 + pan;
    if (gain_db != 0) {
        float g = exp2_approx(gain_db * (LOG2_10 / 20));
        l *= g;
        r *= g;
    }
    *out = (ma_frame){ l, r };
    return true;
}

/* --- RK2 kinds --------------------------------------------------------------- */

/* The value a block's DSP uses for parameter i. */
static inline float pv(const ma_rack_block *b, unsigned i) { return b->param[i].current; }

/* One TPT one-pole lowpass step, G = g / (1 + g). */
static float lowpass(float *state, float gain, float x) {
    float v = (x - *state) * gain;
    float y = v + *state;
    *state = snap(y + v);
    return y;
}

static float pole_gain(float hz, float fs) {
    float g = tan_pi(hz, fs);
    return g / (1.0f + g);
}

/* The read distance in frames the settings ask for: free time, or a note
 * value of the host tempo; within the line. */
static float delay_target(const ma_rack *r, const ma_rack_block *b) {
    unsigned sync = (unsigned)pv(b, MA_DELAY_SYNC);
    float seconds = sync ? ma_delay_sync_beats(sync) * 60.0f / r->tempo_bpm : pv(b, MA_DELAY_TIME_S);
    float frames = seconds * r->sample_rate_hz;
    return frames < 1 ? 1 : frames > MA_DELAY_CAPACITY - 2 ? MA_DELAY_CAPACITY - 2 : frames;
}

/* Linear read `whole + frac` frames behind the write index. A frame older
 * than the restart reads as zero (RKD7). */
static float delay_tap(const float *line, const ma_rack_delay *d, uint32_t whole, float frac) {
    constexpr uint32_t MASK = MA_DELAY_CAPACITY - 1;
    float a = whole <= d->written ? line[(d->write_index - whole) & MASK] : 0.0f;
    float b = whole + 1 <= d->written ? line[(d->write_index - whole - 1) & MASK] : 0.0f;
    return a + frac * (b - a);
}

/* Stereo: each channel feeds itself back. Ping-pong: the mono input goes
 * into the left line, each line feeds the other, so the echoes alternate
 * and every one is `feedback` below the one before. The feedback passes the
 * high cut and the low cut; the first echo does not. */
static bool delay_tick(ma_rack *r, ma_rack_block *b, ma_frame x, ma_frame *out) {
    constexpr uint32_t MASK = MA_DELAY_CAPACITY - 1;
    ma_rack_delay *d = &b->delay;
    ma_rack_delay_line *line = &r->delay[d->unit];
    float fs = r->sample_rate_hz, target = delay_target(r, b);
    if (!d->primed) {
        d->write_index = d->written = 0;
        d->low[0] = d->low[1] = d->high[0] = d->high[1] = 0;
        d->low_hz = d->high_hz = 0;                  /* no setting is 0: both are computed below */
        d->target = target;
        d->lag = 0;
        d->slew = tw_one_pole_coeff(1.0f / (DELAY_SLEW_S * fs));
        d->primed = true;
    } else {
        /* The distance still to go decays by itself, so it reaches zero: a
         * one-pole on the distance would stall a few frames short in f32. */
        if (target != d->target) {
            d->lag += d->target - target;
            d->target = target;
        }
        if (d->lag != 0) {
            d->lag -= d->slew * d->lag;
            if (tw_fabsf(d->lag) < 1e-3f) d->lag = 0;
        }
    }
    d->frames = d->target + d->lag;
    if (d->frames < 1) d->frames = 1;
    float low_hz = pv(b, MA_DELAY_LOWCUT_HZ), high_hz = pv(b, MA_DELAY_HIGHCUT_HZ);
    if (low_hz != d->low_hz) {
        d->low_hz = low_hz;
        d->low_g = pole_gain(low_hz, fs);
    }
    if (high_hz != d->high_hz) {
        d->high_hz = high_hz;
        d->high_g = pole_gain(high_hz, fs);
    }
    uint32_t whole = (uint32_t)d->frames;
    float frac = d->frames - (float)whole;
    float echo[2] = { delay_tap(line->left, d, whole, frac), delay_tap(line->right, d, whole, frac) };
    float back[2];
    for (int ch = 0; ch < 2; ch++) {
        float y = lowpass(&d->high[ch], d->high_g, echo[ch]);
        back[ch] = y - lowpass(&d->low[ch], d->low_g, y);
    }
    float feedback = pv(b, MA_DELAY_FEEDBACK), wl, wr;
    if (pv(b, MA_DELAY_PINGPONG) != 0) {
        wl = .5f * (x.left + x.right) * b->gate + feedback * back[1];
        wr = feedback * back[0];
    } else {
        wl = x.left * b->gate + feedback * back[0];
        wr = x.right * b->gate + feedback * back[1];
    }
    if (!finite(wl) || !finite(wr)) {
        r->sanitizations++;
        wl = wr = 0;
    }
    line->left[d->write_index] = snap(wl);
    line->right[d->write_index] = snap(wr);
    d->write_index = (d->write_index + 1) & MASK;
    if (d->written < MA_DELAY_CAPACITY) d->written++;
    *out = (ma_frame){ echo[0], echo[1] };
    return true;
}

/* A first-order tilt of t dB about the pivot: H = (1 + A s) / (A + s) with
 * A = 10^(t/40) and s in units of the pivot, so 1/A at DC, A far above, and
 * unit magnitude at the pivot; the tilt of -t is its exact inverse, in the
 * sampled filter too, because both share the pivot's warped frequency. As a
 * one-pole lowpass at A times the pivot: H = lowpass / A + A (x - lowpass). */
static void tilt_set(ma_rack_tilt *t, float db, float pivot) {
    t->db = db;
    t->high = exp2_approx(db * (LOG2_10 / 40));
    t->low = 1.0f / t->high;
    float g = t->high * pivot;
    t->g = g / (1.0f + g);
}

static float tilt_tick(const ma_rack_tilt *t, float *state, float x) {
    float low = lowpass(state, t->g, x);
    return t->db == 0 ? x : t->low * low + t->high * (x - low);
}

/* Per channel: tilt, into the drive stage at the input gain and back out
 * at its inverse (the stage has unit small-signal gain, so the block does
 * too), tilt, output gain. */
static bool saturator_tick(ma_rack *r, ma_rack_block *b, ma_frame x, ma_frame *out) {
    ma_rack_saturator *s = &b->saturator;
    float drive = pv(b, MA_SATURATOR_DRIVE), shape = pv(b, MA_SATURATOR_SHAPE);
    float input_db = pv(b, MA_SATURATOR_INPUT_DB), output_db = pv(b, MA_SATURATOR_OUTPUT_DB);
    float pre_db = pv(b, MA_SATURATOR_TILT_PRE_DB), post_db = pv(b, MA_SATURATOR_TILT_POST_DB);
    if (drive == 0 && pre_db == 0 && post_db == 0 && output_db == 0) {
        s->primed = false;
        return false;
    }
    if (!s->primed) {
        for (int ch = 0; ch < 2; ch++) {
            tw_drive_init(&s->triode[ch], r->sample_rate_hz);
            tw_drive_init(&s->odd[ch], r->sample_rate_hz);
            tw_drive_set_kernel(&s->odd[ch], true);
            s->pre[ch] = s->post[ch] = 0;
        }
        s->pivot = tan_pi(TILT_PIVOT_HZ, r->sample_rate_hz);
        s->drive = 0;                                /* what tw_drive_init set */
        s->input_db = s->output_db = 0;
        s->input_gain = s->input_inverse = s->output_gain = 1;
        tilt_set(&s->tilt_pre, 0, s->pivot);
        tilt_set(&s->tilt_post, 0, s->pivot);
        s->primed = true;
    }
    if (drive != s->drive) {
        s->drive = drive;
        for (int ch = 0; ch < 2; ch++) {
            tw_drive_set(&s->triode[ch], drive);
            tw_drive_set(&s->odd[ch], drive);
        }
    }
    if (input_db != s->input_db) {
        s->input_db = input_db;
        s->input_gain = exp2_approx(input_db * (LOG2_10 / 20));
        s->input_inverse = 1.0f / s->input_gain;
    }
    if (pre_db != s->tilt_pre.db) tilt_set(&s->tilt_pre, pre_db, s->pivot);
    if (post_db != s->tilt_post.db) tilt_set(&s->tilt_post, post_db, s->pivot);
    if (output_db != s->output_db) {
        s->output_db = output_db;
        s->output_gain = exp2_approx(output_db * (LOG2_10 / 20));
    }
    float v[2] = { x.left, x.right };
    for (int ch = 0; ch < 2; ch++) {
        float y = tilt_tick(&s->tilt_pre, &s->pre[ch], v[ch]);
        if (drive != 0) {
            float u = y * s->input_gain;
            float triode = shape < 1 ? tw_drive_tick(&s->triode[ch], u) : 0.0f;
            float odd = shape > 0 ? tw_drive_tick(&s->odd[ch], u) : 0.0f;
            y = (shape <= 0 ? triode : shape >= 1 ? odd : triode + shape * (odd - triode)) * s->input_inverse;
        }
        y = tilt_tick(&s->tilt_post, &s->post[ch], y);
        if (output_db != 0) y *= s->output_gain;
        v[ch] = y;
    }
    *out = (ma_frame){ v[0], v[1] };
    return true;
}

/* Feed-forward, both channels under one gain: the level in dB (the larger
 * channel's magnitude, or the mean square of both over RMS_SECONDS), the
 * static curve with a soft knee (Giannoulis, Massberg and Reiss, JAES 2012),
 * then attack and release on the gain change in dB. While the change and
 * the makeup are both 0 dB the frame passes untouched. */
static bool compressor_tick(ma_rack *r, ma_rack_block *b, ma_frame x, ma_frame *out) {
    ma_rack_compressor *c = &b->compressor;
    float ratio = pv(b, MA_COMPRESSOR_RATIO), makeup = pv(b, MA_COMPRESSOR_MAKEUP_DB);
    if (ratio == 1 && makeup == 0) {
        c->primed = false;
        return false;
    }
    float fs = r->sample_rate_hz;
    if (!c->primed) {
        *c = (ma_rack_compressor){ .rms_c = tw_one_pole_coeff(1.0f / (RMS_SECONDS * fs)), .primed = true };
    }
    float attack = pv(b, MA_COMPRESSOR_ATTACK_MS), release = pv(b, MA_COMPRESSOR_RELEASE_MS);
    if (attack != c->attack_ms) {
        c->attack_ms = attack;
        c->attack_c = tw_one_pole_coeff(1000.0f / (attack * fs));
    }
    if (release != c->release_ms) {
        c->release_ms = release;
        c->release_c = tw_one_pole_coeff(1000.0f / (release * fs));
    }
    constexpr float FLOOR_DB = -120.0f;
    float left = tw_fabsf(x.left), right = tw_fabsf(x.right);
    float peak = left > right ? left : right;
    float square = .5f * (x.left * x.left + x.right * x.right);
    if (!(square < 1e30f)) square = 1e30f;
    c->mean_square = snap(c->mean_square + c->rms_c * (square - c->mean_square));
    float level_db;
    if ((unsigned)pv(b, MA_COMPRESSOR_DETECTOR) == MA_COMPRESSOR_RMS)
        level_db = c->mean_square > 1e-12f ? log2_approx(c->mean_square) * (.5f * DB_PER_OCTAVE) : FLOOR_DB;
    else
        level_db = peak > 1e-6f ? log2_approx(peak) * DB_PER_OCTAVE : FLOOR_DB;
    float over = level_db - pv(b, MA_COMPRESSOR_THRESHOLD_DB), knee = pv(b, MA_COMPRESSOR_KNEE_DB);
    float slope = 1.0f / ratio - 1.0f, target;
    if (2 * over <= -knee) target = 0;
    else if (2 * over < knee) {
        float t = over + .5f * knee;
        target = slope * t * t / (2 * knee);
    } else target = slope * over;
    float next = c->reduction_db + (target < c->reduction_db ? c->attack_c : c->release_c) * (target - c->reduction_db);
    c->reduction_db = target == 0 && next > -1e-4f ? 0.0f : next;
    float total = c->reduction_db + makeup;
    if (total == 0) return false;
    float gain = exp2_approx(total * (LOG2_10 / 20));
    *out = (ma_frame){ x.left * gain, x.right * gain };
    return true;
}

/* --- blocks and the chain ------------------------------------------------- */

static float mix_target(const ma_rack_slot_spec *slot) {
    return slot->enabled ? slot->mix : 0.0f;
}

static void block_init(ma_rack_block *b, const ma_rack_slot_spec *slot) {
    *b = (ma_rack_block){ .id = slot->id, .kind = slot->kind, .live = true,
                          .fade = 0, .fade_target = 1 };
    smoother_snap(&b->mix, mix_target(slot));
    for (unsigned i = 0; i < MA_RACK_PARAMS; i++) smoother_snap(&b->param[i], slot->param[i]);
    if (slot->kind == MA_RACK_EQ)
        for (unsigned k = 0; k < MA_EQ_BANDS; k++)
            b->svf[k].type = (uint8_t)slot->param[MA_EQ_BAND(k, MA_EQ_TYPE)];
}

/* Stepped and toggle values jump (the band weights fade them); continuous
 * values ramp. */
static void block_retarget(ma_rack_block *b, const ma_rack_slot_spec *slot, uint16_t frames) {
    (void)smoother_to(&b->mix, mix_target(slot), frames);
    unsigned count = ma_rack_param_count(b->kind);
    for (unsigned i = 0; i < count; i++) {
        ma_rack_param_info info = { 0 };
        (void)info_at(b->kind, i, &info);
        if (info.curve == MA_RACK_STEPPED || info.curve == MA_RACK_TOGGLE) {
            if (b->param[i].target != slot->param[i]) smoother_snap(&b->param[i], slot->param[i]);
        } else if (smoother_to(&b->param[i], slot->param[i], frames)) {
            b->moving = true;
        }
    }
}

static unsigned live_count(const ma_rack *r) {
    unsigned n = 0;
    while (n < MA_RACK_SLOTS && r->block[n].live) n++;
    return n;
}

void ma_rack_init(ma_rack *r, float sample_rate_hz) {
    if (!r) return;
    /* The small state only: the lines are not written here (RKD7, RKD8). */
    unsigned char *bytes = (unsigned char *)r;
    for (size_t i = 0; i < offsetof(ma_rack, chorus); i++) bytes[i] = 0;
    r->sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    float frames = r->sample_rate_hz * .006f + .5f;
    r->smooth_frames = (uint16_t)(frames < 8 ? 8 : frames > 512 ? 512 : frames);
    float fade = r->sample_rate_hz * FADE_SECONDS + .5f;
    r->fade_step = 1.0f / (float)(uint32_t)fade;
    r->tempo_bpm = TEMPO_DEFAULT;
    r->fx.ceiling = MA_RACK_FX_CEILING;
    /* The lines keep whatever they hold: a restart makes it unreadable. */
    r->chorus.length = ma_chorus_length(r->sample_rate_hz);
    r->chorus.controls = (ma_chorus_controls){ 0 };
    ma_chorus_restart(&r->chorus);
    r->reverb.length = ma_reverb_length(r->sample_rate_hz);
    r->reverb.controls = (ma_reverb_controls){ 0 };
    ma_reverb_restart(&r->reverb);
}

void ma_rack_set_tempo(ma_rack *r, float bpm) {
    if (!r) return;
    r->tempo_bpm = !finite(bpm) ? TEMPO_DEFAULT : bpm < TEMPO_MIN ? TEMPO_MIN : bpm > TEMPO_MAX ? TEMPO_MAX : bpm;
}

/* A bypassed block keeps no history: its next sound starts from silence
 * (RKD3). O(1) for the buffered kinds (RKD7), whose input then comes back
 * into the line through the gate. */
static void clear_all(ma_rack *r, ma_rack_block *b) {
    b->gate = 0;
    switch (b->kind) {
    case MA_RACK_EQ:
        for (unsigned k = 0; k < MA_EQ_BANDS; k++)
            if (b->svf[k].active || b->svf[k].weight != 0) {
                svf_clear(&b->svf[k]);
                b->svf[k].weight = 0;
            }
        b->span = 0;
        break;
    case MA_RACK_CHORUS: ma_chorus_restart(&r->chorus); break;
    case MA_RACK_REVERB: ma_reverb_restart(&r->reverb); break;
    case MA_RACK_DELAY: b->delay.primed = false; break;
    case MA_RACK_SATURATOR: b->saturator.primed = false; break;
    case MA_RACK_COMPRESSOR: b->compressor.primed = false; break;
    default: break;
    }
}

void ma_rack_reset(ma_rack *r) {
    if (!r) return;
    for (unsigned k = 0; k < MA_RACK_SLOTS; k++) {
        ma_rack_block *b = &r->block[k];
        for (unsigned s = 0; s < MA_EQ_BANDS; s++) svf_clear(&b->svf[s]);
        b->delay.primed = b->saturator.primed = b->compressor.primed = false;
    }
    ma_chorus_restart(&r->chorus);
    ma_reverb_restart(&r->reverb);
}

/* Adopt the pending chain: same id and kind carry their block over (state
 * included); a block that faded out to move comes back from zero fade. */
static void adopt(ma_rack *r) {
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) r->staging[j] = (ma_rack_block){ 0 };
    unsigned old = live_count(r), n = 0;
    bool fresh[MA_RACK_SLOTS] = { false };
    unsigned lines_held = 0;                     /* delay lines of the blocks carried over */
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) {
        const ma_rack_slot_spec *slot = &r->pending.slot[j];
        if (slot->kind == MA_RACK_EMPTY) continue;
        ma_rack_block *b = &r->staging[n];
        unsigned i = 0;
        while (i < old && !(r->block[i].id == slot->id && r->block[i].kind == slot->kind)) i++;
        if (i == old) {
            block_init(b, slot);
            fresh[n++] = true;
            continue;
        }
        copy_bytes(b, &r->block[i], sizeof *b);
        b->fade_target = 1;
        block_retarget(b, slot, r->smooth_frames);
        if (b->kind == MA_RACK_DELAY) lines_held |= 1u << b->delay.unit;
        n++;
    }
    /* A new buffered block starts its line over; a new delay takes a line
     * no carried block holds (the spec's caps leave one free). */
    for (unsigned k = 0; k < n; k++) {
        if (!fresh[k]) continue;
        ma_rack_block *b = &r->staging[k];
        if (b->kind == MA_RACK_CHORUS) ma_chorus_restart(&r->chorus);
        else if (b->kind == MA_RACK_REVERB) ma_reverb_restart(&r->reverb);
        else if (b->kind == MA_RACK_DELAY) {
            unsigned unit = 0;
            while (unit + 1 < MA_RACK_DELAYS && (lines_held >> unit & 1u)) unit++;
            b->delay.unit = (uint8_t)unit;
            lines_held |= 1u << unit;
        }
    }
    copy_bytes(r->block, r->staging, sizeof r->block);
    r->has_pending = false;
    r->adoptions++;
}

static void maybe_adopt(ma_rack *r) {
    unsigned n = live_count(r);
    for (unsigned k = 0; k < n; k++)
        if (r->block[k].fade_target == 0 && r->block[k].fade != 0) return;
    adopt(r);
}

bool ma_rack_apply(ma_rack *r, const ma_rack_spec *spec) {
    if (!r || !ma_rack_spec_valid(spec)) return false;
    r->applies++;
    copy_bytes(&r->pending, spec, sizeof *spec);

    /* Match the live chain against the new order, then keep the longest
     * run of matches whose order is unchanged (an LIS over at most 8). */
    unsigned old = live_count(r), fresh = 0;
    int order[MA_RACK_SLOTS];      /* new position of old block i, or -1 */
    int slot_of[MA_RACK_SLOTS];    /* spec slot of old block i */
    for (unsigned i = 0; i < old; i++) {
        order[i] = -1;
        slot_of[i] = -1;
        unsigned position = 0;
        for (unsigned j = 0; j < MA_RACK_SLOTS; j++) {
            const ma_rack_slot_spec *s = &spec->slot[j];
            if (s->kind == MA_RACK_EMPTY) continue;
            if (s->id == r->block[i].id && s->kind == r->block[i].kind) {
                order[i] = (int)position;
                slot_of[i] = (int)j;
            }
            position++;
        }
    }
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) fresh += spec->slot[j].kind != MA_RACK_EMPTY;
    int run[MA_RACK_SLOTS], from[MA_RACK_SLOTS];
    int best = -1;
    for (unsigned i = 0; i < old; i++) {
        run[i] = 0;
        from[i] = -1;
        if (order[i] < 0) continue;
        run[i] = 1;
        for (unsigned h = 0; h < i; h++)
            if (order[h] >= 0 && order[h] < order[i] && run[h] + 1 > run[i]) {
                run[i] = run[h] + 1;
                from[i] = (int)h;
            }
        if (best < 0 || run[i] > run[best]) best = (int)i;
    }
    bool kept[MA_RACK_SLOTS] = { 0 };
    unsigned kept_count = 0;
    for (int i = best; i >= 0; i = from[i]) {
        kept[i] = true;
        kept_count++;
    }

    bool structural = kept_count != old || kept_count != fresh;
    for (unsigned i = 0; i < old; i++) {
        ma_rack_block *b = &r->block[i];
        if (kept[i]) {
            block_retarget(b, &spec->slot[slot_of[i]], r->smooth_frames);
            b->fade_target = 1;
        } else {
            b->fade_target = 0;
        }
    }
    if (!structural) {
        r->has_pending = false;
        return true;
    }
    r->structural_changes++;
    r->has_pending = true;
    maybe_adopt(r);
    return true;
}

static ma_frame block_tick(ma_rack *r, ma_rack_block *b, ma_frame x) {
    float mix = smoother_next(&b->mix);
    if (b->fade < b->fade_target) {
        b->fade += r->fade_step;
        if (b->fade >= b->fade_target) b->fade = b->fade_target;
    } else if (b->fade > b->fade_target) {
        b->fade -= r->fade_step;
        if (b->fade <= b->fade_target) b->fade = b->fade_target;
    }
    /* Advance the ramps; note which EQ bands moved (their coefficients are
     * recomputed this frame, the final one included). */
    uint64_t moved = 0;
    if (b->moving) {
        bool still = false;
        unsigned count = ma_rack_param_count(b->kind);
        for (unsigned i = 0; i < count; i++) {
            if (!b->param[i].remaining) continue;
            (void)smoother_next(&b->param[i]);
            still |= b->param[i].remaining != 0;
            if (b->kind == MA_RACK_EQ && i >= MA_EQ_FIRST_BAND)
                moved |= UINT64_C(1) << ((i - MA_EQ_FIRST_BAND) / MA_EQ_BAND_FIELDS);
        }
        b->moving = still;
    }
    float m = mix * b->fade;
    if (m == 0.0f) {
        clear_all(r, b);
        return x;
    }
    if (b->gate < 1 && KIND[b->kind].cap)       /* the buffered kinds are the capped ones */
        b->gate = b->gate + r->fade_step >= 1 ? 1 : b->gate + r->fade_step;
    ma_frame wet;
    bool touched = false;
    switch (b->kind) {
    case MA_RACK_EQ: touched = eq_tick(r, b, x, &wet, moved); break;
    case MA_RACK_UTILITY: touched = utility_tick(b, x, &wet); break;
    /* The two kernels mix by their own law and bound their own output, so
     * the slot's mix (with its fades) is the kernel's mix. */
    case MA_RACK_CHORUS:
        return ma_chorus_tick(&r->chorus, m, pv(b, MA_CHORUS_DEPTH), pv(b, MA_CHORUS_RATE_HZ),
                              r->sample_rate_hz, b->gate, &r->fx, x);
    case MA_RACK_REVERB:
        return ma_reverb_tick(&r->reverb, m, pv(b, MA_REVERB_SIZE), pv(b, MA_REVERB_DAMPING), b->gate,
                              &r->fx, x);
    case MA_RACK_DELAY: touched = delay_tick(r, b, x, &wet); break;
    case MA_RACK_SATURATOR: touched = saturator_tick(r, b, x, &wet); break;
    case MA_RACK_COMPRESSOR: touched = compressor_tick(r, b, x, &wet); break;
    default: break;
    }
    if (!touched) return x;
    if (!finite(wet.left) || !finite(wet.right)) {
        r->sanitizations++;
        clear_all(r, b);
        return x;
    }
    if (m == 1.0f) return wet;
    return (ma_frame){ x.left + m * (wet.left - x.left), x.right + m * (wet.right - x.right) };
}

ma_frame ma_rack_tick(ma_rack *r, ma_frame x) {
    unsigned n = live_count(r);
    for (unsigned k = 0; k < n; k++) x = block_tick(r, &r->block[k], x);
    if (r->has_pending) maybe_adopt(r);
    return x;
}

/* An EQ band still fading, or wanted but about to swap its shape, counts
 * as moving. A silent band (weight 0, not wanted) swaps lazily and does
 * not. */
static bool weights_moving(const ma_rack_block *b) {
    if (b->kind != MA_RACK_EQ) return false;
    const ma_smoother *p = b->param;
    unsigned count = (unsigned)p[MA_EQ_BAND_COUNT].current;
    for (unsigned k = 0; k < MA_EQ_BANDS; k++) {
        const ma_rack_svf *f = &b->svf[k];
        int type = (int)p[MA_EQ_BAND(k, MA_EQ_TYPE)].current;
        if (band_wanted(p, k, count, type)) {
            if (f->type != type || f->weight != 1) return true;
        } else if (f->weight != 0) {
            return true;
        }
    }
    return false;
}

bool ma_rack_moving(const ma_rack *r) {
    if (r->has_pending) return true;
    unsigned n = live_count(r);
    for (unsigned k = 0; k < n; k++) {
        const ma_rack_block *b = &r->block[k];
        if (b->fade != b->fade_target || b->mix.remaining) return true;
        for (unsigned i = 0; i < MA_RACK_PARAMS; i++)
            if (b->param[i].remaining) return true;
        /* A bypassed block does not move its weights (it does not tick). */
        if (b->fade != 0 && b->mix.current != 0 && weights_moving(b)) return true;
    }
    return false;
}

static void smoother_finish(ma_smoother *s) {
    s->current = s->target;
    s->remaining = 0;
}

/* Settle the band weights at their targets too, so a render starts with
 * the EQ fully in place. */
static void settle_weights(ma_rack_block *b) {
    if (b->kind != MA_RACK_EQ) return;
    const ma_smoother *p = b->param;
    unsigned count = (unsigned)p[MA_EQ_BAND_COUNT].current;
    for (unsigned k = 0; k < MA_EQ_BANDS; k++) {
        ma_rack_svf *f = &b->svf[k];
        f->type = (uint8_t)p[MA_EQ_BAND(k, MA_EQ_TYPE)].current;
        f->weight = band_wanted(p, k, count, f->type) ? 1 : 0;
        f->active = false;   /* coefficients recomputed on the next tick */
    }
    b->span = (uint8_t)count;
}

void ma_rack_settle(ma_rack *r) {
    if (!r) return;
    for (int pass = 0; pass < 2; pass++) {
        unsigned n = live_count(r);
        for (unsigned k = 0; k < n; k++) {
            ma_rack_block *b = &r->block[k];
            b->fade = b->fade_target;
            smoother_finish(&b->mix);
            for (unsigned i = 0; i < MA_RACK_PARAMS; i++) smoother_finish(&b->param[i]);
            b->moving = false;
            b->gate = 1;                 /* in place: the lines take the input whole */
            settle_weights(b);
            if (b->kind == MA_RACK_DELAY && b->delay.primed) {
                b->delay.target = delay_target(r, b);
                b->delay.lag = 0;
            }
        }
        if (r->has_pending) adopt(r);   /* new blocks then settle on pass 2 */
    }
}

/* --- block-wise processing (RKD25) ----------------------------------------- */

/* A settled EQ: no ramp, no fade, every band below the count at weight 0
 * or 1 with its shape in place, every weight-1 band's coefficients computed,
 * nothing past the count still fading. Then each frame does the same work,
 * and *active is the mask of the bands that run. */
static bool eq_settled(const ma_rack_block *b, uint8_t *active) {
    if (b->kind != MA_RACK_EQ || b->moving || b->mix.remaining || b->mix.current == 0
        || b->fade != 1 || b->fade_target != 1)
        return false;
    const ma_smoother *p = b->param;
    unsigned count = (unsigned)p[MA_EQ_BAND_COUNT].current;
    if (b->span > count) return false;
    uint8_t mask = 0;
    for (unsigned k = 0; k < count; k++) {
        const ma_rack_svf *f = &b->svf[k];
        int type = (int)p[MA_EQ_BAND(k, MA_EQ_TYPE)].current;
        if (f->type != type) return false;
        if (band_wanted(p, k, count, type)) {
            if (f->weight != 1 || !f->active) return false;
            mask |= (uint8_t)(1u << k);
        } else if (f->weight != 0 || f->active) {
            return false;
        }
    }
    *active = mask;
    return true;
}

enum { RUN = 256 };

/* One settled EQ block over n frames through the wavefront: the bands into
 * a scratch run, then the output gain, the finite check and the mix exactly
 * as eq_tick and block_tick do per frame. A non-finite result restores the
 * band states and gives the run back to the per-frame path, which then
 * sanitizes at the same frame the scalar tick would. False: not taken. */
static bool eq_block(ma_rack *r, ma_rack_block *b, ma_frame *frames, size_t n) {
    uint8_t active;
    if (!eq_settled(b, &active)) return false;
    const ma_smoother *p = b->param;
    unsigned count = (unsigned)p[MA_EQ_BAND_COUNT].current;
    float out_db = p[MA_EQ_OUTPUT_DB].current;
    float m = b->mix.current * b->fade;
    b->span = (uint8_t)count;
    if (!active && out_db == 0) return true;          /* eq_tick: untouched */
    float gain = out_db != 0 ? exp2_approx(out_db * (LOG2_10 / 20)) : 1;
    ma_frame wet[RUN];
    float saved[MA_EQ_BANDS][4];
    for (size_t at = 0; at < n; at += RUN) {
        size_t run = n - at < RUN ? n - at : RUN;
        ma_frame *x = frames + at;
        for (unsigned k = 0; k < MA_EQ_BANDS; k++) {
            saved[k][0] = b->svf[k].s1[0];
            saved[k][1] = b->svf[k].s1[1];
            saved[k][2] = b->svf[k].s2[0];
            saved[k][3] = b->svf[k].s2[1];
        }
        if (active) {
            if (!ma_rack_eq_wavefront(b, active, x, wet, run)) {
                if (at == 0) return false;            /* no AVX2: nothing changed */
                for (size_t i = 0; i < run; i++) x[i] = block_tick(r, b, x[i]);
                continue;
            }
            r->lane_runs++;
        } else {
            for (size_t i = 0; i < run; i++) wet[i] = x[i];
        }
        bool finite_run = true;
        for (size_t i = 0; i < run; i++) {
            if (out_db != 0) {
                wet[i].left *= gain;
                wet[i].right *= gain;
            }
            finite_run &= finite(wet[i].left) && finite(wet[i].right);
        }
        if (!finite_run) {
            for (unsigned k = 0; k < MA_EQ_BANDS; k++) {
                b->svf[k].s1[0] = saved[k][0];
                b->svf[k].s1[1] = saved[k][1];
                b->svf[k].s2[0] = saved[k][2];
                b->svf[k].s2[1] = saved[k][3];
            }
            for (size_t i = 0; i < run; i++) x[i] = block_tick(r, b, x[i]);
            continue;
        }
        for (size_t i = 0; i < run; i++)
            x[i] = m == 1.0f ? wet[i]
                 : (ma_frame){ x[i].left + m * (wet[i].left - x[i].left),
                               x[i].right + m * (wet[i].right - x[i].right) };
    }
    return true;
}

void ma_rack_process(ma_rack *r, ma_frame *frames, const bool *skip, size_t n, bool lanes) {
    if (!r || !n) return;
    bool skipped = false;
    for (size_t i = 0; skip && i < n; i++) skipped |= skip[i];
    if (!lanes || r->has_pending || skipped) {
        for (size_t i = 0; i < n; i++)
            if (!skip || !skip[i]) frames[i] = ma_rack_tick(r, frames[i]);
        return;
    }
    /* No adoption can fall inside the run (nothing is pending), and blocks
     * share no state, so block after block gives the frame-after-frame bits. */
    unsigned live = live_count(r);
    for (unsigned k = 0; k < live; k++) {
        ma_rack_block *b = &r->block[k];
        if (!eq_block(r, b, frames, n))
            for (size_t i = 0; i < n; i++) frames[i] = block_tick(r, b, frames[i]);
    }
}
