/* MA3 Hurt with Orbita. The all-Mamut arrangement of exhibit_ma3_hurt, plus:
 * Orbita resonance capture on the pad and harmony VCO2 ratios, an Orbita
 * moon layer (driven, sixth stem) that follows the harmony root, and a
 * dirtier vocal. Hosted only; the core is used through its public API.
 * Automation and evidence: docs/ma3-hurt-orbita.md. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "ma_orbita.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#include "ma3_hurt_material.h"
#include "ma3_hurt_parts.h"

enum { ORBITA_STEM = 5, CONTROL_FRAMES = 480,
       TICKS_PER_CONTROL = CONTROL_FRAMES / ORBIT_TICK };
static_assert(CONTROL_FRAMES % ORBIT_TICK == 0);

static ma3_show show;
static performance_score score;

/* Song position in bars; the show's own clock (84 BPM, 4/4). */
static double bar_at(size_t frame) {
    return (double)frame * 10000.0 / 714285.0 / TICKS_PER_BAR;
}
static float ramp(double bar, double b0, double b1, float v0, float v1) {
    if (bar <= b0) return v0;
    if (bar >= b1) return v1;
    return v0 + (v1 - v0) * (float)((bar - b0) / (b1 - b0));
}

/* --- Orbita on a pool's VCO2 ratio ---------------------------------------
 * One moon per pool. The moon is the VCO2/VCO1 ratio (or VCO1/VCO2 when
 * VCO2 sits below), mapped to the pool patch's fine tune. The interval is
 * applied unsmoothed by the core, so it stays fixed; the fine must stay in
 * +-50 cents and every clamp is counted. Kicks are deterministic relative
 * displacements placed on bar lines. */
typedef struct { double bar; float relative; } vco2_kick;
typedef struct {
    part_id part;
    float home_cents;           /* VCO2 relative to VCO1 at frame 0 */
    bool below;                 /* VCO2 an octave down: moon is VCO1/VCO2 */
    const vco2_kick *kick;
    unsigned kick_count;
    float (*dissipation)(double bar);
    orbit_system moon;
    unsigned next_kick;
    uint32_t clamps;
} vco2_moon;

static float pad_dissipation(double b) {
    if (b < 22) return 0;
    if (b < 24) return ramp(b, 22, 24, 0, .07f);
    if (b < 40) return .07f;
    if (b < 48) return 0;            /* the bar-40 kick stays sour */
    if (b < 56) return ramp(b, 48, 56, 0, .06f);
    if (b < 64) return .07f;
    if (b < 80) return .10f;         /* climax: escape, glide back */
    return .04f;                     /* outro: one slow final lock */
}
static const vco2_kick PAD_KICKS[] = {
    { 32, .020f }, { 40, .024f }, { 60, -.020f },
    { 64, .022f }, { 66, -.022f }, { 68, .022f }, { 70, -.022f },
    { 72, .022f }, { 74, -.022f }, { 76, .022f }, { 78, -.022f },
    { 80, -.024f },
};
static float harmony_dissipation(double b) { return b < 64 ? 0 : .08f; }

static vco2_moon moons[] = {
    { .part = PAD, .home_cents = 16, .kick = PAD_KICKS,
      .kick_count = sizeof PAD_KICKS / sizeof *PAD_KICKS,
      .dissipation = pad_dissipation },
    { .part = HARMONY, .home_cents = -1224, .below = true,
      .dissipation = harmony_dissipation },
};
enum { MOON_COUNT = sizeof moons / sizeof *moons };

static float moon_ratio_of_cents(const vco2_moon *m, float cents) {
    return exp2f((m->below ? -cents : cents) / 1200.0f);
}
static float moon_cents(const vco2_moon *m) {
    float c = 1200.0f * log2f(m->moon.moon[0].ratio);
    return m->below ? -c : c;
}
static void moon_reset(vco2_moon *m) {
    orbit_init(&m->moon, 1, UINT64_C(0x48555254564f3031) ^ m->part, MA3_RATE);
    orbit_set_home(&m->moon, 0, moon_ratio_of_cents(m, m->home_cents));
    m->next_kick = 0;
    m->clamps = 0;
}
static ma_patch moon_patch(vco2_moon *m, double bar) {
    while (m->next_kick < m->kick_count && bar >= m->kick[m->next_kick].bar)
        orbit_kick_moon(&m->moon, 0, m->kick[m->next_kick++].relative);
    orbit_set_controls(&m->moon, m->dissipation(bar), 0, 0);
    for (unsigned i = 0; i < TICKS_PER_CONTROL; i++) orbit_tick(&m->moon);
    ma_patch p = show.part[m->part].patch;
    float fine = moon_cents(m) - 100.0f * (float)p.vco2_interval;
    if (fine > 50 || fine < -50) { m->clamps++; fine = fine > 50 ? 50 : -50; }
    p.vco2_fine_cents = fine;
    return p;
}

static constexpr float VOCAL_MASTER = .124f; /* stem RMS matched to v3 */

/* --- vocal ------------------------------------------------------------------
 * v2/v3 put grit after the filter, v4 hid it under a steep lowpass; a single
 * lowpass trades richness against the 0.8-2.5 kHz nasal band. v5 uses Glas
 * (ma-constants.md 10C): the ladder only adds grit, formant grains and a
 * dark formant bank shape the vowel, and Orbita locks the formant centres
 * on the sung note's harmonics. Measurements: docs/ma3-hurt-orbita.md. */
/* Orbita formant amount; --vocal-orbita sets it for A/B takes. */
static float vocal_orbita = .55f;

static ma_patch vocal_patch(void) {
    ma_patch p = show.part[VOCAL].patch;
    p.vco1.saw_level = .55f; p.vco1.pulse_level = 0; p.vco1.pulse_width = .50f;
    p.vco1.triangle_level = .25f; p.vco1.sine_level = .15f;
    p.vco2.saw_level = .32f; p.vco2.pulse_level = 0; p.vco2.pulse_width = .50f;
    p.vco2.triangle_level = .20f; p.vco2.sine_level = .08f;
    p.vco2_level = .46f; p.vco2_fine_cents = -11;
    p.sync_amount = 0; p.sync_softness = 0;
    p.crossmod_amount = 0; p.noise_level = .012f;
    p.filter_keytrack = 0; p.filter_env_amount = 0;
    p.bcs_amount = .06f; p.bcs_regime = .20f;
    p.amp_adsr = (ma_adsr){ 60, 420, .85f, 900 };
    /* Held at 1: velocity still scales the envelope amount (.25 * curve),
     * so a moving envelope would sweep the cutoff on every attack. */
    p.filter_adsr = (ma_adsr){ 1, 1, 1, 900 };
    p.formant_mix = .70f; p.formant_shift = -.40f; p.formant_orbita = vocal_orbita;
    p.grain_mix = .30f;
    p.body_drive = .10f;
    p.master_level = VOCAL_MASTER;
    return p;
}
static ma_patch vocal_controls(float progress) {
    ma_patch p = vocal_patch();
    p.formant_vowel = .08f + .22f * progress;   /* u -> o, toward a late */
    p.filter_cutoff_hz = 1100 - 200 * progress;
    p.filter_resonance = 0;
    p.filter_drive = .30f + .10f * progress;
    p.mixer_pressure = .30f + .08f * progress;
    return p;
}

/* --- Orbita moon layer -------------------------------------------------------
 * Five moons over the half-bar harmony root (one octave up), driven through
 * an offset tw_sat so the inharmonic sum intermodulates, then DC-blocked,
 * lowpassed and sent through its own chorus and reverb. */
static constexpr uint64_t LAYER_SEED = UINT64_C(0x48555254524f4231); /* HURTROB1 */
static constexpr float LAYER_BIAS = .35f;
static constexpr float LAYER_LOWPASS_HZ = 4200.0f;
static constexpr float LAYER_GAIN = .020f;    /* stem trim, set by balance */

typedef struct {
    orbit_system bank;
    ma_effects fx;
    float f0, f0_target, glide;
    float gain, gain_target, drive, drive_target, smooth;
    float dc_x, dc_y, dc_r, lp, lp_a;
    unsigned next_kick;
} moon_layer;
static moon_layer layer_state;

typedef struct { double bar; float amount; } layer_kick;
static const layer_kick LAYER_KICKS[] = {
    { 24, .4f }, { 28, .4f }, { 32, .4f }, { 36, .4f }, { 56, .4f }, { 60, .4f },
    { 64, .8f }, { 66, .8f }, { 68, .8f }, { 70, .8f }, { 72, .8f }, { 74, .8f },
    { 76, .8f }, { 78, .8f },
};

static float layer_gain(double b) {
    if (b < 4) return 0;
    if (b < 8) return ramp(b, 4, 8, 0, .50f);
    if (b < 24) return .55f;
    if (b < 40) return .75f;
    if (b < 48) return .90f;
    if (b < 56) return .65f;
    if (b < 64) return .75f;
    if (b < 80) return ramp(b, 64, 80, .80f, 1.0f);
    return ramp(b, 80, 84.5, 1.0f, 0);
}
static float layer_drive(double b) {
    if (b < 24) return 1.6f;
    if (b < 40) return 2.6f;
    if (b < 48) return 2.2f;
    if (b < 64) return 2.6f;
    if (b < 80) return ramp(b, 64, 78, 3.0f, 6.0f);
    return ramp(b, 80, 84, 6.0f, 2.0f);
}
static float layer_dissipation(double b) {
    if (b < 8) return 0;
    if (b < 24) return ramp(b, 8, 24, .15f, .35f);
    if (b < 48) return .60f;
    if (b < 56) return .50f;
    if (b < 80) return .60f;
    return .80f;
}
static float layer_kirkwood(double b) {
    if (b < 40 || b >= 52) return 0;
    if (b < 44) return ramp(b, 40, 44, 0, 1);
    if (b < 48) return 1;
    return ramp(b, 48, 52, 1, 0);
}
static float layer_proximity(double b) {
    if (b < 64) return 0;
    if (b < 80) return ramp(b, 64, 76, 0, 1);
    return ramp(b, 80, 82, 1, 0);
}
static float root_hz(double b) {
    size_t h = (size_t)(b * 2);
    if (h >= BARS * 2) h = BARS * 2 - 1;
    return ma_note_frequency_hz((uint8_t)(score.root[h] + 12));
}

static void layer_reset(void) {
    moon_layer *L = &layer_state;
    *L = (moon_layer){ 0 };
    orbit_init(&L->bank, ORBIT_MAX_MOONS, LAYER_SEED, MA3_RATE);
    ma_effects_init(&L->fx, MA3_RATE);
    ma_effects_set_chorus(&L->fx, (ma_chorus_controls){ true, .30f, .30f, .35f });
    ma_effects_set_reverb(&L->fx, (ma_reverb_controls){ true, .34f, .70f, .50f });
    L->f0 = L->f0_target = root_hz(0);
    L->glide = 1.0f - expf(-1.0f / (.060f * MA3_RATE));
    L->smooth = 1.0f - expf(-1.0f / (.020f * MA3_RATE));
    L->drive = L->drive_target = layer_drive(0);
    L->dc_r = 1.0f - 6.28318531f * 20.0f / MA3_RATE;
    L->lp_a = 1.0f - expf(-6.28318531f * LAYER_LOWPASS_HZ / MA3_RATE);
}
static void layer_controls(double b) {
    moon_layer *L = &layer_state;
    while (L->next_kick < sizeof LAYER_KICKS / sizeof *LAYER_KICKS
           && b >= LAYER_KICKS[L->next_kick].bar)
        orbit_kick_relative(&L->bank, LAYER_KICKS[L->next_kick++].amount);
    orbit_set_controls(&L->bank, layer_dissipation(b), layer_kirkwood(b),
                       layer_proximity(b));
    L->f0_target = root_hz(b);
    L->gain_target = layer_gain(b) * LAYER_GAIN;
    L->drive_target = layer_drive(b);
}
static void layer(const ma3_show *s, size_t frame, ma_frame stems[MA3_STEMS]) {
    (void)s;
    moon_layer *L = &layer_state;
    if (frame == 0) layer_reset();
    if (frame % CONTROL_FRAMES == 0) layer_controls(bar_at(frame));
    L->f0 += (L->f0_target - L->f0) * L->glide;
    L->gain += (L->gain_target - L->gain) * L->smooth;
    L->drive += (L->drive_target - L->drive) * L->smooth;
    float y = orbit_render(&L->bank, L->f0);
    float x = L->drive * y;
    float z = tw_sat(x + LAYER_BIAS) - tw_sat(LAYER_BIAS);
    float dc = z - L->dc_x + L->dc_r * L->dc_y;
    L->dc_x = z; L->dc_y = dc;
    L->lp += (dc - L->lp) * L->lp_a;
    float v = .5f * L->gain * L->lp;
    ma_frame w = ma_effects_tick(&L->fx, (ma_frame){ v, v });
    stems[ORBITA_STEM].left += w.left;
    stems[ORBITA_STEM].right += w.right;
}

/* --- controls ------------------------------------------------------------- */

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    (void)s;
    if (frame == 0)
        for (unsigned m = 0; m < MOON_COUNT; m++) moon_reset(&moons[m]);
    double bar = bar_at(frame);
    float t = (float)frame / (float)tick_frame(BARS * TICKS_PER_BAR); if (t > 1) t = 1;
    float progress = t * t * (3 - 2 * t);
    float energy = .5f - .5f * cosf(6.28318530718f * t);
    /* A patch write resets macros and LFO-independent controls to the patch,
     * so the Orbita patches go first and the macro rides after them. */
    for (unsigned m = 0; m < MOON_COUNT; m++) {
        ma_patch moon = moon_patch(&moons[m], bar);
        ma_voice_pool_set_patch(&p[moons[m].part], &moon);
    }
    ma_voice_pool_set_macro(&p[PAD], MA_MACRO_BLOOM, .06f + .10f * energy);
    ma_voice_pool_set_macro(&p[TEXTURE], MA_MACRO_SWARM, .03f + .07f * energy);
    ma_voice_pool_set_lfo(&p[PAD], .008f, .08f);
    ma_voice_pool_set_lfo(&p[TEXTURE], .006f, .11f);
    ma_patch vocal = vocal_controls(progress);
    ma_voice_pool_set_patch(&p[VOCAL], &vocal);
    ma_voice_pool_set_channel_pressure(&p[VOCAL], 0, .10f + .12f * energy);
    ma_voice_pool_set_mod_wheel(&p[MELODY], 0, .08f + .10f * energy);
}

/* --- check: the Orbita timeline without audio ------------------------------ */

static bool orbita_timeline(void) {
    for (unsigned m = 0; m < MOON_COUNT; m++) moon_reset(&moons[m]);
    layer_reset();
    size_t end = (size_t)show.seconds * MA3_RATE;
    unsigned last_bar = UINT32_MAX;
    printf("Orbita timeline (bar: pad cents/captured, harmony cents/captured, "
           "layer captured/escapes/releases, ring mean)\n");
    for (size_t frame = 0; frame < end; frame += CONTROL_FRAMES) {
        double bar = bar_at(frame);
        for (unsigned m = 0; m < MOON_COUNT; m++) (void)moon_patch(&moons[m], bar);
        layer_controls(bar);
        for (unsigned i = 0; i < TICKS_PER_CONTROL; i++) orbit_tick(&layer_state.bank);
        unsigned b = (unsigned)bar;
        if (b != last_bar && b % 4 == 0) {
            last_bar = b;
            unsigned held = 0;
            float ring = 0;
            for (unsigned k = 0; k < ORBIT_MAX_MOONS; k++) {
                held += layer_state.bank.moon[k].captured >= 0;
                ring += layer_state.bank.moon[k].ring / ORBIT_MAX_MOONS;
            }
            printf("  bar %2u: pad %+8.3f c %s  harmony %+9.3f c %s  layer %u/5 held, "
                   "%u esc, %u rel, ring %.2f\n", b, (double)moon_cents(&moons[0]),
                   moons[0].moon.moon[0].captured >= 0 ? "lock" : "free",
                   (double)moon_cents(&moons[1]),
                   moons[1].moon.moon[0].captured >= 0 ? "lock" : "free",
                   held, layer_state.bank.escapes, layer_state.bank.releases,
                   (double)ring);
        }
    }
    bool ok = true;
    for (unsigned m = 0; m < MOON_COUNT; m++) {
        printf("  %s moon: %u captures, %u escapes, %u fine clamps, final %+.3f c\n",
               PART_NAMES[moons[m].part], moons[m].moon.captures,
               moons[m].moon.escapes, moons[m].clamps, (double)moon_cents(&moons[m]));
        if (moons[m].clamps || moons[m].moon.captures == 0) ok = false;
    }
    if (moons[0].moon.moon[0].captured < 0) ok = false;  /* pad ends locked */
    printf("  layer: %u captures, %u escapes, %u releases, %u resets\n",
           layer_state.bank.captures, layer_state.bank.escapes,
           layer_state.bank.releases, layer_state.bank.resets);
    return ok && layer_state.bank.resets == 0;
}

int main(int argc, char **argv) {
    const char *input = "notes-midi/local/nine-inch-nails-hurt.mid";
    const char *vocal = "notes-midi/local/hurt-songparts-vocals.mid", *prefix = NULL;
    unsigned seconds = 0;
    bool check = false, vocal_only = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vocal = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, 253, &n)) return 2;
            seconds = (unsigned)n;
        } else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--vocal-only")) vocal_only = true;
        else if (!strcmp(argv[i], "--vocal-orbita") && i + 1 < argc) {
            char *end = NULL;
            vocal_orbita = strtof(argv[++i], &end);
            if (!end || *end || !(vocal_orbita >= 0 && vocal_orbita <= 1)) return 2;
        }
        else {
            fprintf(stderr, "Usage: %s --check | --render PREFIX [--seconds 1..253] "
                    "[--vocal-only] [--vocal-orbita 0..1] [-i backing.mid] [-v vocal.mid]\n", argv[0]);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix)) return 2;
    const char *reason = NULL;
    bool ok = midi_load(&score.midi, &score.bytes, input, UINT16_MAX, &reason)
        && score_validate(&score, &reason)
        && midi_load(&score.vocal, &score.vocal_bytes, vocal, 1, &reason)
        && arrange(&score) && check_arrangement(&score);
    show = (ma3_show){ .name = "MA3 Hurt with Orbita", .part_count = PART_COUNT,
        .stem_count = 6, .seconds = 253,
        .stem_name = { "tonal", "rhythm", "melody", "vocal", "harmony", "orbita" },
        .controls = controls, .layer = vocal_only ? NULL : layer };
    /* Pools are independent, so the vocal stem of a vocal-only render is the
     * vocal stem of the full render; every other stem is silent. */
    hurt_parts_init(&show);
    show.part[VOCAL].patch = vocal_controls(0);
    /* Wetter and darker room than the base vocal (.08 chorus, .18 reverb,
     * size .65, damping .55). */
    show.part[VOCAL].chorus = (ma_chorus_controls){ true, .28f, .35f, .25f };
    show.part[VOCAL].reverb = (ma_reverb_controls){ true, .55f, .85f, .75f };
    for (size_t i = 0; ok && i < score.count; i++) {
        arrangement_event e = score.event[i];
        if (vocal_only && e.part != VOCAL) continue;
        ok = ma3_add_event(&show, (ma3_event){ .frame = tick_frame(e.tick),
            .part = (uint8_t)e.part, .note = e.note, .velocity = e.velocity,
            .on = e.on, .pressure = e.part == VOCAL ? .22f : .12f });
    }
    if (ok) ok = ma3_check(&show);
    if (ok) ok = orbita_timeline();
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Hurt Orbita: %s\n",
                     reason ? reason : "score/Orbita/render validation failed");
    score_dispose(&score);
    return ok ? 0 : 1;
}
