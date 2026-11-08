/* Orbita primitive; see ma_orbita.h. Hosted code (driver layer). */
#include "ma_orbita.h"

#include <math.h>

static constexpr float WINDOW = 0.06f;        /* relative attraction window [DONOR] */
static constexpr float CAPTURE_TOL = 0.002f;  /* ~3.5 cents [DONOR]                 */
static constexpr float EXIT_TOL = 0.015f;     /* ~26 cents [DONOR]                  */
static constexpr float RATE_SCALE = 0.12f;    /* rate = dissipation^2 * .12 [DONOR] */
static constexpr float KIRKWOOD_SCALE = 0.12f;/* repulsive if w < .12 k [DONOR]     */
static constexpr float HOME_LO = 1.2f;                         /* [DONOR]   */
static constexpr float HOME_SPAN = 2.6f;                       /* [DONOR]   */
static constexpr float KICK_SCALE = 0.05f;    /* |dr| <= .05 amount [DONOR]         */
static constexpr float CAPTURED_SLEW = 0.25f; /* per tick toward v [DECISION]       */
static constexpr float SNAP_TOL = 1e-6f;      /* relative exact snap [DECISION]     */
static constexpr float RELEASE_NUDGE = 1e-3f; /* off the unstable point [DECISION]  */
static constexpr float HOME_CLEARANCE = 1e-4f;/* home never on a value [DECISION]   */
static constexpr float ROCHE_BASE = 0.55f;                     /* [DONOR] */
static constexpr float ROCHE_STEP = 0.08f;                     /* [DONOR] */
static constexpr float ROCHE_WIDTH = 0.15f;                    /* [DONOR] */
static constexpr float RING_ATTACK_S = 0.030f;                 /* [DONOR] */
static constexpr float RING_RELEASE_S = 0.300f;                /* [DONOR] */
static constexpr float RING_DELTA_BASE = 0.02f;                /* [DONOR] */
static constexpr float RING_DELTA_SPAN = 0.10f;                /* [DONOR] */
static constexpr float GUARD_FULL = 0.40f; /* x fs [DECISION] */
static constexpr float GUARD_ZERO = 0.45f; /* x fs [DECISION] */

#define E(p, q) { p, q, (float)(p) / (float)(q), 1.0f / (float)((p) * (q)) }
const orbit_entry ORBIT_TABLE[ORBIT_ENTRIES] = {
    E(1, 1), E(5, 4), E(4, 3), E(3, 2), E(8, 5), E(5, 3), E(7, 4), E(2, 1),
    E(9, 4), E(7, 3), E(5, 2), E(8, 3), E(3, 1), E(7, 2), E(4, 1),
};
#undef E

static float unit_control(float x, uint32_t *sanitized) {
    if (!isfinite(x)) { (*sanitized)++; return 0.0f; }
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

float orbit_rel_distance(float r, float v) { return fabsf(r - v) / v; }

static bool repulsive(const orbit_system *s, int index) {
    return ORBIT_TABLE[index].weight < KIRKWOOD_SCALE * s->kirkwood;
}

static int nearest_entry(float r, float *distance) {
    int best = 0;
    float best_d = orbit_rel_distance(r, ORBIT_TABLE[0].value);
    for (int i = 1; i < ORBIT_ENTRIES; i++) {
        float d = orbit_rel_distance(r, ORBIT_TABLE[i].value);
        if (d < best_d) { best_d = d; best = i; }
    }
    *distance = best_d;
    return best;
}

static float clear_of_table(float r) {
    for (int i = 0; i < ORBIT_ENTRIES; i++)
        if (orbit_rel_distance(r, ORBIT_TABLE[i].value) < HOME_CLEARANCE)
            return ORBIT_TABLE[i].value * (1.0f + 2.0f * HOME_CLEARANCE);
    return r;
}

void orbit_set_home(orbit_system *s, uint8_t k, float ratio) {
    orbit_moon *m = &s->moon[k];
    m->home = m->ratio = m->prev = ratio;
}

void orbit_init(orbit_system *s, uint8_t count, uint64_t seed,
                       float sample_rate) {
    *s = (orbit_system){ 0 };
    s->count = count > ORBIT_MAX_MOONS ? ORBIT_MAX_MOONS : count == 0 ? 1 : count;
    s->rng = seed;
    s->sample_rate = sample_rate;
    float tick_s = (float)ORBIT_TICK / sample_rate;
    s->ring_attack = 1.0f - expf(-tick_s / RING_ATTACK_S);
    s->ring_release = 1.0f - expf(-tick_s / RING_RELEASE_S);
    /* Stratified seeded homes over [1.2, 3.8]: one stratum per moon so the
     * bank cannot clump [DECISION]; the donor fixes only the span. */
    for (uint8_t k = 0; k < s->count; k++) {
        float u = (float)(tw_splitmix64(&s->rng) >> 40) * 0x1p-24f;
        float home = HOME_LO + HOME_SPAN * ((float)k + u) / (float)s->count;
        orbit_set_home(s, k, clear_of_table(home));
        s->moon[k].captured = s->moon[k].candidate = -1;
    }
}

void orbit_set_controls(orbit_system *s, float dissipation,
                               float kirkwood, float proximity) {
    s->dissipation = unit_control(dissipation, &s->sanitized);
    s->kirkwood = unit_control(kirkwood, &s->sanitized);
    s->proximity = unit_control(proximity, &s->sanitized);
}

static void moon_tick(orbit_system *s, uint8_t k) {
    orbit_moon *m = &s->moon[k];
    float r = m->ratio;
    if (!isfinite(r) || !isfinite(m->ring)) {
        r = m->home;
        m->ring = 0.0f;
        m->captured = m->candidate = -1;
        m->hold = 0;
        s->resets++;
    }
    m->prev = r;

    if (m->captured >= 0) {
        float v = ORBIT_TABLE[m->captured].value;
        if (repulsive(s, m->captured)) {
            /* A repelling entry cannot hold a capture. r == v is the
             * unstable equilibrium, so step off it toward the moon's home
             * side before the force law takes over [DECISION]. */
            r = v * (1.0f + (m->home >= v ? RELEASE_NUDGE : -RELEASE_NUDGE));
            m->captured = -1;
            s->releases++;
        } else if (orbit_rel_distance(r, v) > EXIT_TOL) {
            m->captured = -1;
            s->escapes++;
        } else {
            r += (v - r) * CAPTURED_SLEW;
            if (fabsf(r - v) <= SNAP_TOL * v) r = v;
        }
        m->hold = 0;
        m->candidate = -1;
    }

    if (m->captured < 0) {
        float d;
        int i = nearest_entry(r, &d);
        if (d < WINDOW) {
            const orbit_entry *e = &ORBIT_TABLE[i];
            float rate = s->dissipation * s->dissipation * RATE_SCALE;
            float sign = repulsive(s, i) ? -1.0f : 1.0f;
            r -= sign * rate * e->weight * (r - e->value);
            if (sign > 0.0f && orbit_rel_distance(r, e->value) < CAPTURE_TOL) {
                if (m->candidate == i) m->hold++;
                else { m->candidate = (int8_t)i; m->hold = 1; }
                if (m->hold >= ORBIT_CAPTURE_TICKS) {
                    m->captured = (int8_t)i;
                    m->candidate = -1;
                    m->hold = 0;
                    s->captures++;
                    if (m->first_capture_tick == 0)
                        m->first_capture_tick = s->tick_count + 1;
                }
            } else {
                m->hold = 0;
                m->candidate = -1;
            }
        } else {
            m->hold = 0;
            m->candidate = -1;
        }
    }
    m->ratio = r < ORBIT_R_MIN ? ORBIT_R_MIN : r > ORBIT_R_MAX ? ORBIT_R_MAX : r;

    float roche = ROCHE_BASE + ROCHE_STEP * (float)k;
    float x = (s->proximity - roche) / ROCHE_WIDTH;
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    float excess = x * x * (3.0f - 2.0f * x);
    float c = excess > m->ring ? s->ring_attack : s->ring_release;
    m->ring += (excess - m->ring) * c;
    if (fabsf(m->ring - excess) < 1e-7f) m->ring = excess;
    if (m->ring > m->ring_peak) m->ring_peak = m->ring;
}

void orbit_tick(orbit_system *s) {
    for (uint8_t k = 0; k < s->count; k++) moon_tick(s, k);
    s->tick_count++;
}

void orbit_kick(orbit_system *s, float amount) {
    amount = unit_control(amount, &s->sanitized);
    for (uint8_t k = 0; k < s->count; k++) {
        float u = (float)(tw_splitmix64(&s->rng) >> 40) * 0x1p-23f - 1.0f;
        float r = s->moon[k].ratio + KICK_SCALE * amount * u;
        s->moon[k].ratio = r < ORBIT_R_MIN ? ORBIT_R_MIN : r > ORBIT_R_MAX ? ORBIT_R_MAX : r;
    }
}

void orbit_kick_relative(orbit_system *s, float amount) {
    amount = unit_control(amount, &s->sanitized);
    for (uint8_t k = 0; k < s->count; k++) {
        float u = (float)(tw_splitmix64(&s->rng) >> 40) * 0x1p-23f - 1.0f;
        float r = s->moon[k].ratio * (1.0f + KICK_SCALE * amount * u);
        s->moon[k].ratio = r < ORBIT_R_MIN ? ORBIT_R_MIN : r > ORBIT_R_MAX ? ORBIT_R_MAX : r;
    }
}

void orbit_kick_moon(orbit_system *s, uint8_t k, float relative) {
    s->moon[k].ratio *= 1.0f + relative;
}

static float guard_gain(float hz, float sample_rate, uint32_t *guarded) {
    float x = hz / sample_rate;
    if (x <= GUARD_FULL) return 1.0f;
    (*guarded)++;
    if (x >= GUARD_ZERO) return 0.0f;
    return (GUARD_ZERO - x) / (GUARD_ZERO - GUARD_FULL);
}

float orbit_advance(float *phase, float hz, float sample_rate) {
    float y = tw_sin_turns(*phase);
    float p = *phase + hz / sample_rate;
    p -= (float)(int)p;
    *phase = p;
    return y;
}

/* One sample of the moon bank. Ticks on the first sample of each period and
 * interpolates the ratio linearly across it [DECISION: the donor leaves the
 * per-sample ratio path open]. */
float orbit_render(orbit_system *s, float f0_hz) {
    if (s->sample_in_tick == 0) orbit_tick(s);
    float t = (float)(s->sample_in_tick + 1) / (float)ORBIT_TICK;
    if (++s->sample_in_tick == ORBIT_TICK) s->sample_in_tick = 0;

    float norm = 0.0f;
    for (uint8_t k = 0; k < s->count; k++) norm += 1.0f / (float)(k + 1);
    float sum = 0.0f;
    for (uint8_t k = 0; k < s->count; k++) {
        orbit_moon *m = &s->moon[k];
        float r = m->prev + (m->ratio - m->prev) * t;
        float delta = RING_DELTA_BASE + RING_DELTA_SPAN * m->ring;
        float hz[3] = { f0_hz * r, f0_hz * r * (1.0f - delta),
                        f0_hz * r * (1.0f + delta) };
        float gain[3] = { 1.0f - (2.0f / 3.0f) * m->ring, m->ring / 3.0f,
                          m->ring / 3.0f };
        float moon = 0.0f;
        for (int j = 0; j < 3; j++)
            moon += gain[j] * guard_gain(hz[j], s->sample_rate,
                                         &s->guard_samples)
                  * orbit_advance(&m->phase[j], hz[j], s->sample_rate);
        sum += moon / (float)(k + 1);
    }
    float y = sum / norm;
    if (fabsf(y) < 1e-30f) y = 0.0f;
    return y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
}
