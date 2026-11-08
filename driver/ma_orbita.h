/* Orbita: resonance capture, hysteretic escape and breakup. Hosted
 * primitive shared by the offline spike and the MA3 Hurt exhibit; no core
 * dependency beyond the tonewheel.h kernels. Constants and their tags:
 * docs/ma-orbita-spike-20261107.md. */
#ifndef MA_ORBITA_H
#define MA_ORBITA_H

#include <stdbool.h>
#include <stdint.h>

#include "../src/tonewheel.h"

enum {
    ORBIT_TICK = 32,          /* control samples per tick [DONOR]          */
    ORBIT_MAX_MOONS = 5,      /* v1 K [DONOR]                              */
    ORBIT_ENTRIES = 15,
    ORBIT_CAPTURE_TICKS = 8,  /* consecutive in-tolerance ticks [DONOR]    */
};

static constexpr float ORBIT_R_MIN = 0.5f; /* [DONOR] */
static constexpr float ORBIT_R_MAX = 6.0f; /* [DONOR] */

typedef struct {
    uint8_t p, q;
    float value;
    float weight; /* 1/(p q): a consonance heuristic, stated as such [DONOR] */
} orbit_entry;

extern const orbit_entry ORBIT_TABLE[ORBIT_ENTRIES];

typedef struct {
    float ratio;      /* value at the end of the current tick              */
    float prev;       /* value at its start; samples interpolate between   */
    float home;       /* seeded init; also the non-finite reset value      */
    float ring;       /* breakup amount, 0..1                              */
    float ring_peak;
    float phase[3];   /* centre, lower, upper; always running, gain-gated  */
    int8_t captured;  /* table index, or -1                                */
    int8_t candidate; /* entry currently accumulating hold ticks           */
    uint8_t hold;
    uint32_t first_capture_tick; /* 0 = never                              */
} orbit_moon;

typedef struct {
    orbit_moon moon[ORBIT_MAX_MOONS];
    uint64_t rng;
    float dissipation, kirkwood, proximity;
    float ring_attack, ring_release;
    float sample_rate;
    uint32_t tick_count;
    uint32_t sample_in_tick;
    uint8_t count;
    uint32_t captures, escapes, releases, resets, sanitized, guard_samples;
} orbit_system;

void orbit_init(orbit_system *s, uint8_t count, uint64_t seed, float sample_rate);
void orbit_set_home(orbit_system *s, uint8_t k, float ratio);
void orbit_set_controls(orbit_system *s, float dissipation, float kirkwood,
                        float proximity);
void orbit_tick(orbit_system *s);
/* Seeded per-moon kick, |dr| <= .05 amount in ratio units [DONOR]. */
void orbit_kick(orbit_system *s, float amount);
/* Seeded per-moon kick, r *= 1 + .05 amount u: equally able to escape at
 * every ratio [DECISION, docs/ma3-hurt-orbita.md]. */
void orbit_kick_relative(orbit_system *s, float amount);
/* Deterministic test hook: a relative displacement of one moon. */
void orbit_kick_moon(orbit_system *s, uint8_t k, float relative);
/* One sample of the moon bank; ticks on the first sample of each period. */
float orbit_render(orbit_system *s, float f0_hz);
/* sin(2 pi phase), then advances the phase by hz / sample_rate. */
float orbit_advance(float *phase, float hz, float sample_rate);
float orbit_rel_distance(float r, float v);

#endif
