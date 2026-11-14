/* Insert rack: the chain host (RK1), the parametric EQ (RK1b) and the
 * utility block. Freestanding C23: no OS, no libm, no allocation. Plan and
 * decisions: docs/ma-rack-plan.md (RKD1..RKD12, RK1b). */
#include "ma_internal.h"

static constexpr float FADE_SECONDS = .010f;     /* RKD6 [decision] */
static constexpr float LOG2_10 = 3.32192809f;
static constexpr float MAX_RATIO = .45f;         /* frequency clamp, x fs */
static constexpr float MIN_HZ = 10.0f;

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

unsigned ma_rack_param_count(ma_rack_kind kind) {
    return kind == MA_RACK_EQ ? MA_EQ_PARAM_COUNT : kind == MA_RACK_UTILITY ? MA_UTILITY_PARAM_COUNT : 0;
}

static bool info_at(ma_rack_kind kind, unsigned index, ma_rack_param_info *info) {
    if (kind == MA_RACK_EQ && index < MA_EQ_PARAM_COUNT) {
        *info = index < MA_EQ_FIRST_BAND ? EQ_HEAD[index]
              : EQ_FIELD[(index - MA_EQ_FIRST_BAND) % MA_EQ_BAND_FIELDS];
        return true;
    }
    if (kind == MA_RACK_UTILITY && index < MA_UTILITY_PARAM_COUNT) {
        *info = UTILITY_INFO[index];
        return true;
    }
    return false;
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
    slot->mix = 1;
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
    for (unsigned i = 0; i < MA_RACK_SLOTS; i++) {
        if (!slot_valid(&spec->slot[i])) return false;
        if (spec->slot[i].kind == MA_RACK_EMPTY) continue;
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
    *r = (ma_rack){ 0 };
    r->sample_rate_hz = tw_sample_rate_hz(sample_rate_hz);
    float frames = r->sample_rate_hz * .006f + .5f;
    r->smooth_frames = (uint16_t)(frames < 8 ? 8 : frames > 512 ? 512 : frames);
    float fade = r->sample_rate_hz * FADE_SECONDS + .5f;
    r->fade_step = 1.0f / (float)(uint32_t)fade;
}

static void clear_all(ma_rack_block *b) {
    for (unsigned k = 0; k < MA_EQ_BANDS; k++)
        if (b->svf[k].active || b->svf[k].weight != 0) {
            svf_clear(&b->svf[k]);
            b->svf[k].weight = 0;
        }
    b->span = 0;
}

void ma_rack_reset(ma_rack *r) {
    if (!r) return;
    for (unsigned k = 0; k < MA_RACK_SLOTS; k++)
        for (unsigned s = 0; s < MA_EQ_BANDS; s++) svf_clear(&r->block[k].svf[s]);
}

/* Adopt the pending chain: same id and kind carry their block over (state
 * included); a block that faded out to move comes back from zero fade. */
static void adopt(ma_rack *r) {
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) r->staging[j] = (ma_rack_block){ 0 };
    unsigned old = live_count(r), n = 0;
    for (unsigned j = 0; j < MA_RACK_SLOTS; j++) {
        const ma_rack_slot_spec *slot = &r->pending.slot[j];
        if (slot->kind == MA_RACK_EMPTY) continue;
        ma_rack_block *b = &r->staging[n++];
        unsigned i = 0;
        while (i < old && !(r->block[i].id == slot->id && r->block[i].kind == slot->kind)) i++;
        if (i == old) {
            block_init(b, slot);
            continue;
        }
        copy_bytes(b, &r->block[i], sizeof *b);
        b->fade_target = 1;
        block_retarget(b, slot, r->smooth_frames);
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
        clear_all(b);
        return x;
    }
    ma_frame wet;
    bool touched = b->kind == MA_RACK_EQ ? eq_tick(r, b, x, &wet, moved)
                 : b->kind == MA_RACK_UTILITY ? utility_tick(b, x, &wet) : false;
    if (!touched) return x;
    if (!finite(wet.left) || !finite(wet.right)) {
        r->sanitizations++;
        clear_all(b);
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
            settle_weights(b);
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
