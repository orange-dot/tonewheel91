/* SimpleChorus/SimpleReverb behavior from Mamut; fixed freestanding storage.
 * Coefficients and provenance: docs/ma-constants.md section 13. */
#include "mamutanalog.h"

static float bound(float x, float lo, float hi) {
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) return lo;
    return x < lo ? lo : x > hi ? hi : x;
}

static void clear_chorus(ma_chorus *c) {
    for (unsigned i = 0; i < MA_CHORUS_CAPACITY; i++) c->left[i] = c->right[i] = 0;
    c->write_index = 0;
    c->phase = 0;
}
static void clear_reverb(ma_reverb *r) {
    for (unsigned i = 0; i < MA_REVERB_CAPACITY; i++) r->left[i] = r->right[i] = 0;
    r->write_index = 0;
    r->damped_left = r->damped_right = 0;
}
void ma_effects_init(ma_effects *fx, float rate) {
    *fx = (ma_effects){ .sample_rate_hz = tw_sample_rate_hz(rate) };
    fx->chorus.length = (unsigned)(fx->sample_rate_hz * .080f + .5f);
    fx->reverb.length = (unsigned)(fx->sample_rate_hz * .340f + .5f);
    fx->chorus.controls = (ma_chorus_controls){ .depth = .35f, .rate_hz = .25f };
    fx->reverb.controls = (ma_reverb_controls){ .size = .65f, .damping = .55f };
}
void ma_effects_reset(ma_effects *fx) {
    clear_chorus(&fx->chorus);
    clear_reverb(&fx->reverb);
    fx->sanitizations = fx->clips = 0;
    fx->pre_peak = fx->post_peak = 0;
}
void ma_effects_set_chorus(ma_effects *fx, ma_chorus_controls c) {
    c.mix = bound(c.mix, 0, 1);
    c.depth = bound(c.depth, 0, 1);
    c.rate_hz = bound(c.rate_hz, .03f, 20);
    if (c.enabled && !fx->chorus.controls.enabled) clear_chorus(&fx->chorus);
    fx->chorus.controls = c;
}
void ma_effects_set_reverb(ma_effects *fx, ma_reverb_controls c) {
    c.mix = bound(c.mix, 0, 1);
    c.size = bound(c.size, 0, 1);
    c.damping = bound(c.damping, 0, .99f);
    if (c.enabled && !fx->reverb.controls.enabled) clear_reverb(&fx->reverb);
    fx->reverb.controls = c;
}

/* Donor sanitize_sample: finite, tiny flush, hard bound. This protects delay
 * feedback too; diagnostics include internal writes as well as wet outputs. */
static float sanitize(ma_effects *fx, float x) {
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) { fx->sanitizations++; return 0; }
    float a = tw_fabsf(x);
    if (a > fx->pre_peak) fx->pre_peak = a;
    if (a > 1) { fx->clips++; x = x < 0 ? -1 : 1; }
    else if (a < 1e-20f) x = 0;
    a = tw_fabsf(x);
    if (a > fx->post_peak) fx->post_peak = a;
    return x;
}
static float delay_read(const float *buffer, unsigned length, unsigned at, float delay) {
    delay = bound(delay, 1, (float)(length - 1));
    float pos = (float)(at + length) - delay;
    unsigned whole = (unsigned)pos;
    float frac = pos - (float)whole;
    unsigned a = whole % length, b = (a + 1) % length;
    return buffer[a] * (1 - frac) + buffer[b] * frac;
}
static ma_frame chorus_tick(ma_effects *fx, ma_frame in) {
    ma_chorus *c = &fx->chorus;
    ma_chorus_controls p = c->controls;
    if (!p.enabled) return in;
    float base = fx->sample_rate_hz * .014f;
    float mod = fx->sample_rate_hz * .006f * p.depth;
    float a = tw_sin_turns(c->phase);
    float pb = c->phase + .31f, pd = c->phase + .63f;
    float b = tw_sin_turns(pb >= 1 ? pb - 1 : pb);
    float d = tw_sin_turns(pd >= 1 ? pd - 1 : pd);
    float dl = delay_read(c->left, c->length, c->write_index, base + mod * a);
    float dr = delay_read(c->right, c->length, c->write_index, base + mod * b);
    float cross = base * .74f + mod * .45f * d;
    float cl = delay_read(c->right, c->length, c->write_index, cross * .94f);
    float cr = delay_read(c->left, c->length, c->write_index, cross * 1.06f);
    float feedback = .08f + p.depth * .12f;
    /* Sanitize hostile input before arithmetic can contaminate history. */
    float left = sanitize(fx, in.left), right = sanitize(fx, in.right);
    c->left[c->write_index] = sanitize(fx, left + dl * feedback);
    c->right[c->write_index] = sanitize(fx, right + dr * feedback);
    c->write_index = (c->write_index + 1) % c->length;
    c->phase += p.rate_hz / fx->sample_rate_hz;
    if (c->phase >= 1) c->phase -= 1;
    if (p.mix == 0) return in;
    float center = (left + right) * .5f;
    float dry = 1 - p.mix * .72f, wet = p.mix * .78f;
    return (ma_frame){
        sanitize(fx, left * dry + (dl * .66f + cl * .22f + center * .12f) * wet),
        sanitize(fx, right * dry + (dr * .66f + cr * .22f + center * .12f) * wet),
    };
}
static float tap(const float *buffer, unsigned length, unsigned at, unsigned offset) {
    return buffer[(at + length - offset) % length];
}
static ma_frame reverb_tick(ma_effects *fx, ma_frame in) {
    ma_reverb *r = &fx->reverb;
    ma_reverb_controls p = r->controls;
    if (!p.enabled) return in;
    float dl = r->left[r->write_index], dr = r->right[r->write_index];
    unsigned a = (unsigned)((float)r->length * (.16f + p.size * .28f) + .5f);
    unsigned b = (unsigned)((float)r->length * (.31f + p.size * .22f) + .5f);
    float tl = tap(r->left, r->length, r->write_index, a);
    float tr = tap(r->right, r->length, r->write_index, a);
    float cl = tap(r->right, r->length, r->write_index, b);
    float cr = tap(r->left, r->length, r->write_index, b);
    float feedback = .34f + p.size * .40f, diffusion = .16f + p.size * .22f;
    float blend = 1 - p.damping * .88f;
    r->damped_left += (dl + cl * diffusion - r->damped_left) * blend;
    r->damped_right += (dr + cr * diffusion - r->damped_right) * blend;
    if (tw_fabsf(r->damped_left) < 1e-20f) r->damped_left = 0;
    if (tw_fabsf(r->damped_right) < 1e-20f) r->damped_right = 0;
    float left = sanitize(fx, in.left), right = sanitize(fx, in.right);
    r->left[r->write_index] = sanitize(fx, left * .42f + r->damped_right * feedback);
    r->right[r->write_index] = sanitize(fx, right * .42f + r->damped_left * feedback);
    r->write_index = (r->write_index + 1) % r->length;
    if (p.mix == 0) return in;
    float dry = 1 - p.mix * .58f, wet = p.mix * (.62f + p.size * .10f);
    return (ma_frame){
        sanitize(fx, left * dry + (dl * .48f + tl * .18f + cl * .26f) * wet),
        sanitize(fx, right * dry + (dr * .48f + tr * .18f + cr * .26f) * wet),
    };
}
ma_frame ma_effects_tick(ma_effects *fx, ma_frame input) {
    return reverb_tick(fx, chorus_tick(fx, input));
}
