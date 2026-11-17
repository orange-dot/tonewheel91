/* SimpleChorus/SimpleReverb behavior from Mamut; fixed freestanding storage.
 * Coefficients and provenance: docs/ma-constants.md section 13.
 *
 * One kernel, two hosts: the insert rack's chorus and reverb blocks (RK2,
 * docs/ma-rack-plan.md) and the standalone ma_effects pair. A line is never
 * cleared on the render thread (RKD7): a restart puts the write index at
 * zero and counts the frames written since, and an entry that has not been
 * written since reads as zero. */
#include "ma_internal.h"

static float bound(float x, float lo, float hi) {
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) return lo;
    return x < lo ? lo : x > hi ? hi : x;
}

void ma_chorus_restart(ma_chorus *c) {
    c->write_index = 0;
    c->written = 0;
    c->phase = 0;
}
void ma_reverb_restart(ma_reverb *r) {
    r->write_index = 0;
    r->written = 0;
    r->damped_left = r->damped_right = 0;
}
unsigned ma_chorus_length(float sample_rate_hz) { return (unsigned)(sample_rate_hz * .080f + .5f); }
unsigned ma_reverb_length(float sample_rate_hz) { return (unsigned)(sample_rate_hz * .340f + .5f); }

void ma_effects_init(ma_effects *fx, float rate) {
    *fx = (ma_effects){ .sample_rate_hz = tw_sample_rate_hz(rate), .meter = { .ceiling = 1 } };
    fx->chorus.length = ma_chorus_length(fx->sample_rate_hz);
    fx->reverb.length = ma_reverb_length(fx->sample_rate_hz);
    fx->chorus.controls = (ma_chorus_controls){ .depth = .35f, .rate_hz = .25f };
    fx->reverb.controls = (ma_reverb_controls){ .size = .65f, .damping = .55f };
}
void ma_effects_reset(ma_effects *fx) {
    ma_chorus_restart(&fx->chorus);
    ma_reverb_restart(&fx->reverb);
    fx->meter = (ma_fx_meter){ .ceiling = 1 };
}
void ma_effects_set_chorus(ma_effects *fx, ma_chorus_controls c) {
    c.mix = bound(c.mix, 0, 1);
    c.depth = bound(c.depth, 0, 1);
    c.rate_hz = bound(c.rate_hz, .03f, 20);
    if (c.enabled && !fx->chorus.controls.enabled) ma_chorus_restart(&fx->chorus);
    fx->chorus.controls = c;
}
void ma_effects_set_reverb(ma_effects *fx, ma_reverb_controls c) {
    c.mix = bound(c.mix, 0, 1);
    c.size = bound(c.size, 0, 1);
    c.damping = bound(c.damping, 0, .99f);
    if (c.enabled && !fx->reverb.controls.enabled) ma_reverb_restart(&fx->reverb);
    fx->reverb.controls = c;
}

/* Donor sanitize_sample: finite, tiny flush, hard bound. This protects delay
 * feedback too; diagnostics include internal writes as well as wet outputs.
 * The bound is the meter's ceiling: 1 for the pair after the safety, where
 * the donor had it; the rack's blocks sit before master and set it higher. */
static float sanitize(ma_fx_meter *m, float x) {
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) { m->sanitizations++; return 0; }
    float a = tw_fabsf(x);
    if (a > m->pre_peak) m->pre_peak = a;
    if (a > m->ceiling) { m->clips++; x = x < 0 ? -m->ceiling : m->ceiling; }
    else if (a < 1e-20f) x = 0;
    a = tw_fabsf(x);
    if (a > m->post_peak) m->post_peak = a;
    return x;
}
/* While the line has not wrapped since its restart, the write index equals
 * the count, so the entries written are exactly those below it. */
static float line_at(const float *buffer, unsigned written, unsigned index) {
    return index < written ? buffer[index] : 0.0f;
}
static float delay_read(const float *buffer, unsigned length, unsigned written, unsigned at, float delay) {
    delay = bound(delay, 1, (float)(length - 1));
    float pos = (float)(at + length) - delay;
    unsigned whole = (unsigned)pos;
    float frac = pos - (float)whole;
    unsigned a = whole % length, b = (a + 1) % length;
    return line_at(buffer, written, a) * (1 - frac) + line_at(buffer, written, b) * frac;
}
ma_frame ma_chorus_tick(ma_chorus *c, float mix, float depth, float rate_hz, float sample_rate_hz,
                        float gate, ma_fx_meter *meter, ma_frame in) {
    float base = sample_rate_hz * .014f;
    float mod = sample_rate_hz * .006f * depth;
    float a = tw_sin_turns(c->phase);
    float pb = c->phase + .31f, pd = c->phase + .63f;
    float b = tw_sin_turns(pb >= 1 ? pb - 1 : pb);
    float d = tw_sin_turns(pd >= 1 ? pd - 1 : pd);
    float dl = delay_read(c->left, c->length, c->written, c->write_index, base + mod * a);
    float dr = delay_read(c->right, c->length, c->written, c->write_index, base + mod * b);
    float cross = base * .74f + mod * .45f * d;
    float cl = delay_read(c->right, c->length, c->written, c->write_index, cross * .94f);
    float cr = delay_read(c->left, c->length, c->written, c->write_index, cross * 1.06f);
    float feedback = .08f + depth * .12f;
    /* Sanitize hostile input before arithmetic can contaminate history. */
    float left = sanitize(meter, in.left), right = sanitize(meter, in.right);
    c->left[c->write_index] = sanitize(meter, left * gate + dl * feedback);
    c->right[c->write_index] = sanitize(meter, right * gate + dr * feedback);
    c->write_index = (c->write_index + 1) % c->length;
    if (c->written < c->length) c->written++;
    c->phase += rate_hz / sample_rate_hz;
    if (c->phase >= 1) c->phase -= 1;
    if (mix == 0) return in;
    float center = (left + right) * .5f;
    float dry = 1 - mix * .72f, wet = mix * .78f;
    return (ma_frame){
        sanitize(meter, left * dry + (dl * .66f + cl * .22f + center * .12f) * wet),
        sanitize(meter, right * dry + (dr * .66f + cr * .22f + center * .12f) * wet),
    };
}
static float tap(const float *buffer, unsigned length, unsigned written, unsigned at, unsigned offset) {
    return line_at(buffer, written, (at + length - offset) % length);
}
ma_frame ma_reverb_tick(ma_reverb *r, float mix, float size, float damping, float gate,
                        ma_fx_meter *meter, ma_frame in) {
    float dl = line_at(r->left, r->written, r->write_index);
    float dr = line_at(r->right, r->written, r->write_index);
    unsigned a = (unsigned)((float)r->length * (.16f + size * .28f) + .5f);
    unsigned b = (unsigned)((float)r->length * (.31f + size * .22f) + .5f);
    float tl = tap(r->left, r->length, r->written, r->write_index, a);
    float tr = tap(r->right, r->length, r->written, r->write_index, a);
    float cl = tap(r->right, r->length, r->written, r->write_index, b);
    float cr = tap(r->left, r->length, r->written, r->write_index, b);
    float feedback = .34f + size * .40f, diffusion = .16f + size * .22f;
    float blend = 1 - damping * .88f;
    r->damped_left += (dl + cl * diffusion - r->damped_left) * blend;
    r->damped_right += (dr + cr * diffusion - r->damped_right) * blend;
    if (tw_fabsf(r->damped_left) < 1e-20f) r->damped_left = 0;
    if (tw_fabsf(r->damped_right) < 1e-20f) r->damped_right = 0;
    float left = sanitize(meter, in.left), right = sanitize(meter, in.right);
    r->left[r->write_index] = sanitize(meter, left * gate * .42f + r->damped_right * feedback);
    r->right[r->write_index] = sanitize(meter, right * gate * .42f + r->damped_left * feedback);
    r->write_index = (r->write_index + 1) % r->length;
    if (r->written < r->length) r->written++;
    if (mix == 0) return in;
    float dry = 1 - mix * .58f, wet = mix * (.62f + size * .10f);
    return (ma_frame){
        sanitize(meter, left * dry + (dl * .48f + tl * .18f + cl * .26f) * wet),
        sanitize(meter, right * dry + (dr * .48f + tr * .18f + cr * .26f) * wet),
    };
}
ma_frame ma_effects_tick(ma_effects *fx, ma_frame input) {
    ma_chorus_controls c = fx->chorus.controls;
    if (c.enabled)
        input = ma_chorus_tick(&fx->chorus, c.mix, c.depth, c.rate_hz, fx->sample_rate_hz, 1.0f, &fx->meter, input);
    ma_reverb_controls r = fx->reverb.controls;
    if (r.enabled) input = ma_reverb_tick(&fx->reverb, r.mix, r.size, r.damping, 1.0f, &fx->meter, input);
    return input;
}
