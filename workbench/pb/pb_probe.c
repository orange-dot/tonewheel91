/* The probe worker and its passes (docs/patchbench-plan.md §4). */
#define _GNU_SOURCE
#include "../../src/ma_voice_impl.h"
#define ML_W 1
#include "../../src/ma_lane_ops.h"
#include "../../src/ma_filter_kernel.h"

#include "pb_probe.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../driver/ma_patch_file.h"
#include "pb_derive.h"

static constexpr double PI = 3.14159265358979323846;

/* ---- the tick ---------------------------------------------------------------
 * src/ma_voice.c synth_tick, stage for stage: voice_front_end (part A, BCS,
 * part B), the source block, the decimators, Glas, the filter, the back end
 * and the output.  Every call is the core's own function; only the order is
 * restated here, and the identity test pins it. */
static ma_frame tick(ma_synth *s, pb_taps *t, ma_voice_front *front) {
    ma_voice_front f;
    float source[8], at_2x[2];
    float y4 = s->ladder.y4_guess, guard1 = s->oscillator1.guard_gain, guard2 = s->oscillator2.guard_gain;
    ma_voice_front_a(s, 0.0f, &f);
    float bcs = 0.0f;
    if (f.controls.bcs_amount > 0.0f)
        bcs = ma_voice_render_bcs(s, f.base_hz, f.controls.bcs_amount, f.controls.bcs_regime, y4);
    ma_voice_front_b_shared(s, bcs, guard1, guard2, &f);
    const ma_render_controls *controls = &f.controls;
    float envelope_amount = ma_voice_filter_envelope_amount(s, controls);
    float filter_modulation = s->filter_envelope.level * envelope_amount;
    s->filter_cutoff_effective_hz = ma_voice_effective_filter_cutoff(s, controls, envelope_amount, f.note_position);
    f.filter_g = ma_filter_prewarp(s->filter_cutoff_effective_hz, s->sample_rate_hz);
    f.source = ma_voice_prepare_source_frame_guarded(s, controls, f.base_hz, filter_modulation, guard1, guard2);
    for (int k = 0; k < 2; k++)
        f.raster[k] = controls->raster_mix > 0.0f
                    ? ma_voice_render_raster(s, controls, f.base_hz, 2.0f * s->sample_rate_hz) : 0.0f;
    ma_voice_render_source_block(s, &f.controls, &f.source, f.base_hz, f.noise, f.guard1, f.guard2, source);
    ma_voice_decimate_source(s, source, at_2x);
    bool glas = ma_glas_active(&s->glas);
    s->glas.frame_grain_mix = glas ? ma_glas_prepare(s, f.base_hz) : 0.0f;
    float filtered = ma_voice_filter(s, &f, at_2x);
    float ladder = filtered;
    if (glas) filtered = ma_glas_formant(s, filtered);
    float output = voice_back_end(s, &f, filtered);
    ma_frame y = ma_voice_render_output(s, &f.controls, output);
    if (t) {
        memcpy(t->source, source, sizeof source);
        t->at_2x[0] = at_2x[0];
        t->at_2x[1] = at_2x[1];
        for (int k = 0; k < 2; k++) {
            t->raster[k] = f.raster[k];
            t->mozaik[k] = f.mozaik[k] * f.mozaik_gain[k];
            t->grains[k] = s->glas.grain_out[k] * s->glas.frame_grain_mix;
        }
        t->pressure = s->ladder.pressure_output;
        t->ladder = ladder;
        t->glas = filtered;
        t->vca = output;
        t->output = y;
        t->cutoff_hz = s->filter_cutoff_effective_hz;
    }
    if (front) *front = f;
    return y;
}

ma_frame pb_probe_tick(ma_synth *s, pb_taps *taps) { return tick(s, taps, nullptr); }

uint32_t pb_mozaik_slope_q32(float control) { return ma_voice_mozaik_slope_q32(control); }
uint32_t pb_mozaik_detent_q32(int d) { return d >= 0 && d < 5 ? MOZAIK_DETENT[d].q32 : 0; }

/* ---- helpers --------------------------------------------------------------- */

typedef struct {
    float ring[PB_CAPTURE];
    uint64_t written;
} ring;

static void ring_push(ring *r, float x) { r->ring[r->written++ % PB_CAPTURE] = x; }

static void ring_out(const ring *r, pb_tap_capture *c, float rate, float f0) {
    uint64_t n = r->written < PB_CAPTURE ? r->written : PB_CAPTURE;
    for (uint64_t i = 0; i < n; i++) c->x[i] = r->ring[(r->written - n + i) % PB_CAPTURE];
    c->count = (unsigned)n;
    c->rate_hz = rate;
    double two = 2.0 * rate / (f0 > 1 ? f0 : 1);
    c->cycle = two < n ? (unsigned)two : (unsigned)n;
}

static void spectrum(pb_tap_capture *c, float f0) {
    size_t n = PB_SPECTRUM;
    while (n > c->count && n > 64) n >>= 1;
    if (c->count < n) {
        memset(c->db, 0, sizeof c->db);
        return;
    }
    pb_spectrum_db(c->x + c->count - n, n, c->db);
    for (size_t i = n / 2 + 1; i <= PB_SPECTRUM / 2; i++) c->db[i] = -180;
    pb_classify(c->db, n, c->rate_hz, f0, 32, &c->harmonics);
}

typedef struct {
    pb_probe *owner;         /* null for a synchronous pass */
    uint64_t generation;
    unsigned rate;
    ma_synth *settled;       /* phase A's voice at the end of its main run */
    ma_voice_front settled_front;
} pass;

static bool cancelled(const pass *p) {
    return p->owner && atomic_load_explicit(&p->owner->latest, memory_order_relaxed) != p->generation;
}

static void wait_quiet(const pass *p) {
    for (int i = 0; p->owner && p->owner->busy && i < 40 && p->owner->busy(p->owner->busy_arg); i++)
        nanosleep(&(struct timespec){ 0, 5000000 }, nullptr);
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static void start_voice(ma_synth *s, unsigned rate, const ma_patch *patch, uint8_t note, uint8_t velocity) {
    ma_synth_init_patch(s, (float)rate, patch);
    ma_synth_note_on(s, 0, note, velocity);
}

static void resample(const float *x, unsigned count, float *out, unsigned points) {
    for (unsigned i = 0; i < points; i++) {
        double at = count > 1 ? (double)i * (count - 1) / (points - 1) : 0;
        unsigned a = (unsigned)at;
        double frac = at - a;
        float y0 = count ? x[a] : 0, y1 = a + 1 < count ? x[a + 1] : y0;
        out[i] = (float)(y0 + (y1 - y0) * frac);
    }
}

/* A run of a solo patch, the 8x bus tapped: settles, then captures. */
static bool solo_run(const pass *p, ma_synth *s, ring *r, const ma_patch *patch, uint8_t note, uint8_t velocity,
                     unsigned settle, unsigned capture, float *base_hz) {
    pb_taps t;
    start_voice(s, p->rate, patch, note, velocity);
    r->written = 0;
    for (unsigned i = 0; i < settle + capture; i++) {
        if (!(i & 1023) && cancelled(p)) return false;
        tick(s, &t, nullptr);
        if (i >= settle) for (int k = 0; k < 8; k++) ring_push(r, t.source[k]);
    }
    if (base_hz) {
        ma_voice_front f;
        tick(s, nullptr, &f);
        *base_hz = f.base_hz;
    }
    return true;
}

static void set_effective(pb_probe_result *out, const ma_render_controls *c, const ma_synth *s) {
    for (unsigned i = 0; i < 64; i++) out->effective[i] = NAN;
    static const struct { const char *name; size_t offset; } MAP[] = {
#define M(n, m) { n, offsetof(ma_render_controls, m) }
        M("vco1.saw", vco1.saw_level), M("vco1.pulse", vco1.pulse_level), M("vco1.triangle", vco1.triangle_level),
        M("vco1.sine", vco1.sine_level), M("vco1.pulse_width", vco1.pulse_width),
        M("vco2.saw", vco2.saw_level), M("vco2.pulse", vco2.pulse_level), M("vco2.triangle", vco2.triangle_level),
        M("vco2.sine", vco2.sine_level), M("vco2.pulse_width", vco2.pulse_width), M("vco2.level", vco2_level),
        M("vco1.tri_sine", vco1.tri_sine_level), M("vco1.tri_saw", vco1.tri_saw_level),
        M("vco2.tri_sine", vco2.tri_sine_level), M("vco2.tri_saw", vco2.tri_saw_level),
        M("vco2.fine_cents", vco2_fine_cents), M("source.sync", sync_amount),
        M("source.sync_softness", sync_softness), M("source.crossmod", crossmod_amount),
        M("source.noise", noise_level), M("raster.mix", raster_mix), M("raster.position", raster_position),
        M("raster.warp", raster_warp), M("bcs.amount", bcs_amount), M("bcs.regime", bcs_regime),
        M("mozaik.mix", mozaik_mix), M("mozaik.drift", mozaik_drift), M("filter.mixer_pressure", mixer_pressure),
        M("filter.resonance", filter_resonance), M("filter.drive", filter_drive),
        M("filter.env_amount", filter_env_amount), M("filter.keytrack", filter_keytrack),
        M("output.body_drive", body_drive), M("output.width", width), M("output.crossfeed", crossfeed),
#undef M
    };
    for (size_t i = 0; i < sizeof MAP / sizeof *MAP; i++) {
        ma_patch_field_info info;
        for (size_t f = 0; ma_patch_field_info_at(f, &info); f++)
            if (!strcmp(info.name, MAP[i].name)) {
                float v;
                memcpy(&v, (const unsigned char *)c + MAP[i].offset, sizeof v);
                out->effective[f] = v;
            }
    }
    ma_patch_field_info info;
    for (size_t f = 0; ma_patch_field_info_at(f, &info); f++)
        if (!strcmp(info.name, "filter.cutoff_hz")) out->effective[f] = s->filter_cutoff_effective_hz;
    out->sync_effective = c->sync_amount * (1.0f - .75f * c->sync_softness);
    out->crossmod_effective = c->crossmod_amount;
}

/* The ladder's magnitude response, measured: the settled voice's filter
 * stage driven by sines at two levels, sources other than the test signal
 * removed. */
static bool response(const pass *p, const ma_synth *settled, const ma_voice_front *settled_front,
                     pb_probe_result *out) {
    ma_synth *t = malloc(sizeof *t);
    if (!t) return false;
    ma_voice_front f = *settled_front;
    f.controls.raster_mix = 0;
    f.controls.mozaik_mix = 0;
    out->response_low_level = .05f;
    out->response_nominal_level = .5f;
    float rate = (float)p->rate;
    for (unsigned k = 0; k < PB_RESPONSE; k++) {
        if (cancelled(p)) { free(t); return false; }
        double hz = 20.0 * pow(1000.0, (double)k / (PB_RESPONSE - 1));
        out->response_hz[k] = (float)hz;
        for (int level = 0; level < 2; level++) {
            float amplitude = level ? out->response_nominal_level : out->response_low_level;
            *t = *settled;
            memset(&t->ladder, 0, sizeof t->ladder);
            t->glas.frame_grain_mix = 0;
            unsigned settle = 2048, measure = (unsigned)fmax(2048.0, 3.0 * rate / hz);
            double sum = 0;
            double w = 2.0 * PI * hz / (2.0 * rate);
            for (unsigned i = 0; i < settle + measure; i++) {
                float in2[2] = { amplitude * (float)sin(w * (2.0 * i)), amplitude * (float)sin(w * (2.0 * i + 1)) };
                float y = ma_voice_filter(t, &f, in2);
                if (i >= settle) sum += (double)y * y;
            }
            double rms = sqrt(sum / measure), in_rms = amplitude / sqrt(2.0);
            float db = rms > 1e-9 ? (float)(20.0 * log10(rms / in_rms)) : -180.0f;
            if (level) out->response_nominal_db[k] = db;
            else out->response_low_db[k] = db;
        }
    }
    free(t);
    return true;
}

/* The body stage's transfer at the effective drive: one 100 Hz cycle at
 * full scale through a copy of the settled output state, after 200 ms. */
static void body_curve(const ma_synth *settled, const ma_voice_front *f, unsigned rate, pb_probe_result *out) {
    ma_output_state o = settled->output;
    unsigned period = rate / 100, total = rate / 5;
    float *pre = malloc(2 * period * sizeof *pre), *post = pre ? pre + period : nullptr;
    out->body_count = 0;
    if (!pre) return;
    for (unsigned i = 0; i < total; i++) {
        float x = (float)sin(2.0 * PI * i / period);
        (void)ma_output_render(&o, nullptr, x, 0.0f, f->controls.body_drive, f->controls.body_load_ratio, 1.0f);
        if (i >= total - period) {
            pre[i - (total - period)] = o.pre_body;
            post[i - (total - period)] = o.post_body;
        }
    }
    for (unsigned j = 0; j < PB_CURVE; j++) {
        unsigned at = j * (period - 1) / (PB_CURVE - 1);
        out->body_in[j] = pre[at];
        out->body_out[j] = post[at];
    }
    out->body_count = PB_CURVE;
    free(pre);
}

/* Phase A: taps, spectra, effective values, response, curves. */
static bool phase_a(pass *p, const pb_probe_request *rq, pb_probe_result *out, ma_synth *s, ring *rings) {
    double t0 = now_ms();
    unsigned rate = p->rate;
    out->patch = rq->patch;
    out->note = rq->note;
    out->velocity = rq->velocity;
    out->rate = (float)rate;
    out->note_ready = out->compass_ready = out->components_ready = out->response_ready = false;

    /* The main run: the audible patch, every tap. */
    unsigned settle = rate / 16, capture = PB_SPECTRUM / 2;
    start_voice(s, rate, &rq->patch, rq->note, rq->velocity);
    for (unsigned k = 0; k < PB_TAP_COUNT; k++) rings[k].written = 0;
    pb_taps t;
    ma_voice_front f = { 0 };
    out->tile_count = 0;
    out->portrait_count = 0;
    out->pressure_peak = 0;
    uint32_t tiles = s->mozaik.tiles_emitted;
    /* The BCS portrait: every fourth frame of the run's last 200 ms, or of
     * the whole run when it is shorter (148 ms at 48 kHz). */
    unsigned span = rate / 5 < PB_PORTRAIT * 4 ? rate / 5 : PB_PORTRAIT * 4;
    unsigned portrait_from = settle + capture > span ? settle + capture - span : 0;
    for (unsigned i = 0; i < settle + capture; i++) {
        if (!(i & 1023) && cancelled(p)) return false;
        tick(s, &t, &f);
        if (i >= portrait_from && !((i - portrait_from) & 3u) && out->portrait_count < PB_PORTRAIT) {
            unsigned j = out->portrait_count++;
            out->portrait[0][j] = s->bcs.hopf_re;
            out->portrait[1][j] = s->bcs.hopf_im;
            out->portrait[2][j] = s->bcs.duffing_x;
            out->portrait[3][j] = s->bcs.duffing_v;
            out->bcs_trace[j] = s->bcs.output;
        }
        if (i < settle) continue;
        for (int k = 0; k < 8; k++) ring_push(&rings[PB_TAP_BUS], t.source[k]);
        for (int k = 0; k < 2; k++) {
            ring_push(&rings[PB_TAP_RASTER], t.raster[k]);
            ring_push(&rings[PB_TAP_MOZAIK], t.mozaik[k]);
            ring_push(&rings[PB_TAP_GRAINS], t.grains[k]);
        }
        ring_push(&rings[PB_TAP_PRESSURE], t.pressure);
        ring_push(&rings[PB_TAP_LADDER], t.ladder);
        ring_push(&rings[PB_TAP_GLAS], t.glas);
        ring_push(&rings[PB_TAP_VCA], t.vca);
        ring_push(&rings[PB_TAP_OUTPUT], t.output.left);
        float pin = fabsf(s->ladder.pressure_input);
        if (pin > out->pressure_peak) out->pressure_peak = pin;
        if (s->mozaik.tiles_emitted != tiles && out->tile_count < PB_TILES) {
            out->tile_len[out->tile_count] = s->mozaik.tile_len;
            out->tile_sign[out->tile_count++] = s->mozaik.tile_sign;
        }
        tiles = s->mozaik.tiles_emitted;
    }
    float f0 = f.base_hz;
    out->f0 = f0;
    set_effective(out, &f.controls, s);
    out->identity = s->identity;
    out->cutoff_effective_hz = s->filter_cutoff_effective_hz;
    out->guard[0] = s->oscillator1.guard_gain;
    out->guard[1] = s->oscillator2.guard_gain;
    out->raster_mip = ma_voice_raster_mip(f0 / (2.0f * (float)rate), f.controls.raster_warp, &out->raster_blend);
    for (int k = 0; k < MA_FORMANTS; k++) {
        out->glas_centre_hz[k] = s->glas.centre_hz[k];
        out->glas_captured[k] = s->glas.moon[k].captured;
        out->glas_ratio[k] = s->glas.moon[k].ratio;
    }
    out->glas_captures = s->glas.captures;
    out->glas_releases = s->glas.releases;
    out->glas_stolen = s->glas.grains_stolen;
    out->bcs_resets = s->bcs.reset_count;
    out->bcs_maximum = s->bcs.maximum_state;
    static const float RATE_OF[PB_TAP_COUNT] = { 8, 8, 8, 2, 2, 2, 1, 1, 1, 1, 1 };
    for (unsigned k = PB_TAP_BUS; k < PB_TAP_COUNT; k++)
        ring_out(&rings[k], &out->tap[k], RATE_OF[k] * (float)rate, f0);
    for (int pt = 0; pt < PB_CURVE; pt++) {
        float x = -1.5f + 3.0f * (float)pt / (PB_CURVE - 1);
        out->pressure_curve[pt] = ml_pressure(x, f.controls.mixer_pressure);
    }
    ma_synth *settled = malloc(sizeof *settled);
    if (!settled) return false;
    *settled = *s;
    body_curve(settled, &f, rate, out);

    /* Solo runs for the two VCOs. */
    for (int v = 0; v < 2; v++) {
        pb_monitor m = { 0 };
        m.solo[v ? PB_SRC_VCO2 : PB_SRC_VCO1] = true;
        ma_patch solo = pb_derive(&rq->patch, &m);
        /* VCO2 at level zero still runs for sync and cross-mod; it is
         * drawn at unit level. */
        if (v && solo.vco2_level == 0) solo.vco2_level = 1;
        unsigned two = (unsigned)(2.0f * (float)rate / f0) + 2;
        wait_quiet(p);
        if (!solo_run(p, s, &rings[v], &solo, rq->note, rq->velocity, rate / 20, two > 1024 ? two : 1024, nullptr)) {
            free(settled);
            return false;
        }
        ring_out(&rings[v], &out->tap[v], 8.0f * (float)rate, f0);
    }
    for (unsigned k = 0; k < PB_TAP_COUNT; k++) spectrum(&out->tap[k], f0);
    /* Phase B measures the filter from this settled state. */
    free(p->settled);
    p->settled = settled;
    p->settled_front = f;
    out->pass_ms = now_ms() - t0;
    return true;
}

/* Phase B: a whole note, attack to idle, and the waveform components. */
static bool phase_b(const pass *p, const pb_probe_request *rq, pb_probe_result *out, ma_synth *s, ring *r) {
    double t0 = now_ms();
    unsigned rate = p->rate;
    const ma_patch *pt = &rq->patch;
    double hold_s = (pt->amp_adsr.attack_ms + pt->amp_adsr.decay_ms) / 1000.0 + .4;
    if (hold_s > 8) hold_s = 8;
    double release_s = pt->amp_adsr.release_ms / 1000.0 * 7.0 + .05;
    if (release_s > 8) release_s = 8;
    unsigned hold = (unsigned)(hold_s * rate), total = hold + (unsigned)(release_s * rate);
    unsigned step = (total + PB_NOTE_POINTS - 1) / PB_NOTE_POINTS;
    start_voice(s, rate, pt, rq->note, rq->velocity);
    out->note_count = 0;
    float lo = INFINITY, hi = 0, peak = 0;
    for (unsigned i = 0; i < total; i++) {
        if (!(i & 4095) && cancelled(p)) return false;
        if (i == hold) ma_synth_note_off(s, 0, rq->note, 0);
        ma_frame y = ma_synth_tick(s);
        float a = fabsf(y.left);
        if (a > peak) peak = a;
        float c = s->filter_cutoff_effective_hz;
        if (i < hold) {
            if (c < lo) lo = c;
            if (c > hi) hi = c;
        }
        if (i % step == step - 1 && out->note_count < PB_NOTE_POINTS) {
            unsigned j = out->note_count++;
            out->note_amp[j] = s->amp_envelope.level;
            out->note_filter[j] = s->filter_envelope.level;
            out->note_cutoff[j] = c;
            out->note_peak[j] = peak;
            peak = 0;
        }
        if (i > hold && s->amp_envelope.stage == MA_ENVELOPE_IDLE) break;
    }
    out->note_seconds = (float)(out->note_count * step) / (float)rate;
    out->note_release_at = (float)hold / (float)rate;
    out->cutoff_env_low_hz = lo;
    out->cutoff_env_high_hz = hi;
    out->note_ready = true;
    wait_quiet(p);
    if (!p->settled || !response(p, p->settled, &p->settled_front, out)) return false;
    out->response_ready = true;

    /* Components: each waveform of each VCO alone, at its authored level. */
    for (int v = 0; v < 2; v++)
        for (int w = 0; w < PB_COMPONENTS; w++) {
            pb_monitor m = { 0 };
            m.solo[v ? PB_SRC_VCO2 : PB_SRC_VCO1] = true;
            ma_patch solo = pb_derive(pt, &m);
            ma_vco_controls *vc = v ? &solo.vco2 : &solo.vco1;
            float level[PB_COMPONENTS] = { vc->saw_level, vc->pulse_level, vc->triangle_level, vc->sine_level,
                                           vc->tri_sine_level, vc->tri_saw_level };
            if (level[w] == 0) {
                memset(out->component[v][w], 0, sizeof out->component[v][w]);
                continue;
            }
            vc->saw_level = w == 0 ? level[0] : 0;
            vc->pulse_level = w == 1 ? level[1] : 0;
            vc->triangle_level = w == 2 ? level[2] : 0;
            vc->sine_level = w == 3 ? level[3] : 0;
            vc->tri_sine_level = w == 4 ? level[4] : 0;
            vc->tri_saw_level = w == 5 ? level[5] : 0;
            if (v && solo.vco2_level == 0) solo.vco2_level = 1;
            unsigned two = (unsigned)(2.0f * (float)rate / out->f0) + 2;
            wait_quiet(p);
            if (!solo_run(p, s, r, &solo, rq->note, rq->velocity, rate / 50, two, nullptr)) return false;
            pb_tap_capture *c = malloc(sizeof *c);
            if (!c) return false;
            ring_out(r, c, 8.0f * (float)rate, out->f0);
            resample(c->x + c->count - c->cycle, c->cycle, out->component[v][w], PB_COMPONENT_POINTS);
            free(c);
        }
    out->components_ready = true;
    out->note_ms = now_ms() - t0;
    return true;
}

/* Phase C: VCO1 at C1..C6. */
static bool phase_c(const pass *p, const pb_probe_request *rq, pb_probe_result *out, ma_synth *s, ring *r) {
    pb_monitor m = { 0 };
    m.solo[PB_SRC_VCO1] = true;
    ma_patch solo = pb_derive(&rq->patch, &m);
    pb_tap_capture *c = malloc(sizeof *c);
    if (!c) return false;
    for (int k = 0; k < PB_COMPASS; k++) {
        uint8_t note = (uint8_t)(24 + 12 * k);
        float base = 0;
        unsigned two = (unsigned)(2.0 * p->rate / (440.0 * pow(2.0, (note - 69) / 12.0))) + 2;
        wait_quiet(p);
        if (!solo_run(p, s, r, &solo, note, rq->velocity, p->rate / 10, two, &base)) {
            free(c);
            return false;
        }
        ring_out(r, c, 8.0f * (float)p->rate, base);
        resample(c->x + c->count - c->cycle, c->cycle, out->compass[k], PB_COMPASS_POINTS);
        out->compass_guard[k] = s->oscillator1.guard_gain;
    }
    free(c);
    out->compass_ready = true;
    return true;
}

void pb_probe_run(unsigned rate, const pb_probe_request *rq, pb_probe_result *out) {
    pass p = { .rate = rate };
    ma_synth *s = malloc(sizeof *s);
    ring *rings = malloc(PB_TAP_COUNT * sizeof *rings);
    if (s && rings && phase_a(&p, rq, out, s, rings) && phase_b(&p, rq, out, s, rings) && rq->compass)
        phase_c(&p, rq, out, s, rings);
    free(p.settled);
    free(rings);
    free(s);
}

/* ---- worker ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void publish(pb_probe *p) {
    pthread_mutex_lock(&p->lock);
    pb_probe_result *x = p->ready;
    p->ready = p->work;
    p->work = x;
    p->fresh = true;
    /* The next phase continues from what was just published. */
    memcpy(p->work, p->ready, sizeof *p->work);
    pthread_mutex_unlock(&p->lock);
}

static void *worker(void *arg) {
    pb_probe *p = arg;
    ma_synth *s = malloc(sizeof *s);
    ring *rings = malloc(PB_TAP_COUNT * sizeof *rings);
    for (;;) {
        pthread_mutex_lock(&p->lock);
        while (!p->quit && !p->has_request) pthread_cond_wait(&p->wake, &p->lock);
        if (p->quit) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        /* Debounce: 30 ms without a newer request. */
        while (!p->quit && now_ns() - p->request_ns < 30000000u) {
            pthread_mutex_unlock(&p->lock);
            nanosleep(&(struct timespec){ 0, 5000000 }, nullptr);
            pthread_mutex_lock(&p->lock);
        }
        pb_probe_request rq = p->request;
        p->has_request = false;
        uint64_t generation = atomic_load(&p->latest);
        pthread_mutex_unlock(&p->lock);
        if (!s || !rings) continue;
        pass ps = { .owner = p, .generation = generation, .rate = p->rate };
        wait_quiet(&ps);
        p->work->generation = generation;
        if (phase_a(&ps, &rq, p->work, s, rings)) {
            publish(p);
            if (phase_b(&ps, &rq, p->work, s, rings)) {
                publish(p);
                if (rq.compass && phase_c(&ps, &rq, p->work, s, rings)) publish(p);
            }
        }
        free(ps.settled);
    }
    free(rings);
    free(s);
    return nullptr;
}

bool pb_probe_start(pb_probe *p, unsigned rate) {
    *p = (pb_probe){ .rate = rate };
    p->work = calloc(1, sizeof *p->work);
    p->ready = calloc(1, sizeof *p->ready);
    p->shown = calloc(1, sizeof *p->shown);
    if (!p->work || !p->ready || !p->shown) return false;
    pthread_mutex_init(&p->lock, nullptr);
    pthread_cond_init(&p->wake, nullptr);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    p->started = pthread_create(&p->thread, &attr, worker, p) == 0;
    pthread_attr_destroy(&attr);
    return p->started;
}

void pb_probe_stop(pb_probe *p) {
    if (p->started) {
        pthread_mutex_lock(&p->lock);
        p->quit = true;
        atomic_fetch_add(&p->latest, 1);
        pthread_cond_signal(&p->wake);
        pthread_mutex_unlock(&p->lock);
        pthread_join(p->thread, nullptr);
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->wake);
    }
    free(p->work);
    free(p->ready);
    free(p->shown);
    *p = (pb_probe){ 0 };
}

void pb_probe_post(pb_probe *p, const pb_probe_request *rq) {
    if (!p->started) return;
    pthread_mutex_lock(&p->lock);
    p->request = *rq;
    p->has_request = true;
    p->request_ns = now_ns();
    atomic_fetch_add(&p->latest, 1);
    pthread_cond_signal(&p->wake);
    pthread_mutex_unlock(&p->lock);
}

const pb_probe_result *pb_probe_acquire(pb_probe *p) {
    if (!p->started) return nullptr;
    pthread_mutex_lock(&p->lock);
    if (p->fresh) {
        pb_probe_result *x = p->shown;
        p->shown = p->ready;
        p->ready = x;
        p->fresh = false;
    }
    pthread_mutex_unlock(&p->lock);
    return p->shown->generation ? p->shown : nullptr;
}
