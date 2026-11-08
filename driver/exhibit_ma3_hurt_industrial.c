/* MA3 Hurt, industrial (2026-11-07). The original performance as transcribed
 * (driver/make_hurt_industrial.py -> notes-midi/local/hurt-industrial-20261107.mid)
 * on nine Mamut pools plus hosted Orbita metal percussion:
 *   - an industrial kit (kick, snare, hat) over the original sections;
 *   - the original toms' rhythm as Orbita metal strikes: a struck five-moon
 *     bank in full ring breakup, driven, whose moons are captured onto small
 *     integer ratios more firmly as the song climbs (out of tune -> in tune);
 *   - the vocal on Glas (formant bank, Orbita formants, formant grains);
 *   - Orbita on the pad's VCO2 ratio.
 * Single-pass ma3_show render. Notes: docs/ma3-hurt-industrial.md. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "ma_orbita.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "ma3_hurt_material.h"
#include "ma3_hurt_parts.h"
#pragma GCC diagnostic pop

enum { I_ARP, I_KEYS, I_BASS, I_VOCAL, I_PAD, I_GTR, I_KICK, I_SNARE, I_HAT, I_PARTS,
       I_METAL = I_PARTS, I_CHANNELS };
enum { ST_TONAL, ST_BASS, ST_PADS, ST_VOCAL, ST_GUITAR, ST_RHYTHM, ST_COUNT };
enum { I_PPQ = 480, I_BAR = 4 * I_PPQ, I_US_PER_QUARTER = 740740, I_START_BAR = 8,
       I_END_BAR = 100, I_SECONDS = 283, CONTROL_FRAMES = 480,
       TICKS_PER_CONTROL = CONTROL_FRAMES / ORBIT_TICK };
static const char *I_NAMES[I_CHANNELS] = {
    "arp", "keys", "bass", "vocal", "pad", "gtr", "kick", "snare", "hat", "metal",
};

static ma3_show show;
static bool include[I_CHANNELS];
/* --gtr-fat (operator): the distorted guitar's repeated-note motif (from
 * bar 68) fat and wetter but with a clear pulse: four detuned voices and a
 * sub-octave VCO2, a filter-envelope click on every note, a short release
 * so the larger room does not smear the beat. */
static bool gtr_fat;
/* --engine scalar: every pool on the scalar engine (A/B against the lanes). */
static bool engine_scalar;
static constexpr float GTR_FAT_GAIN = 1.683f;   /* motif 1 dB under the previous guitar (measured) */
/* --rhythm ma3: the MA3 Hurt kick and pulse (channel 7) instead of the
 * industrial kit; driver/make_hurt_industrial.py --rhythm ma3. */
static bool rhythm_ma3;

static size_t i_frame(uint64_t tick) { return (size_t)((tick * 740740u + 5000u) / 10000u); }
/* Source bar (the transcription's numbering) of a frame. */
static double i_bar(size_t frame) {
    return (double)frame * 10000.0 / (740740.0 * I_BAR) + I_START_BAR;
}
static float ramp(double bar, double b0, double b1, float v0, float v1) {
    if (bar <= b0) return v0;
    if (bar >= b1) return v1;
    return v0 + (v1 - v0) * (float)((bar - b0) / (b1 - b0));
}
/* Form energy over the original sections. */
static float energy(double bar) {
    static const float at[][2] = {
        {8, .20f}, {24, .40f}, {40, .70f}, {48, .30f}, {68, .65f},
        {76, .85f}, {91, 1.0f}, {92, .55f}, {100, .15f},
    };
    constexpr unsigned n = sizeof at / sizeof at[0];
    if (bar <= at[0][0]) return at[0][1];
    for (unsigned i = 1; i < n; i++)
        if (bar < at[i][0])
            return at[i-1][1] + (at[i][1] - at[i-1][1]) * (float)((bar - at[i-1][0]) / (at[i][0] - at[i-1][0]));
    return at[n-1][1];
}

/* --- patches --------------------------------------------------------------- */

static ma_patch keys_patch(void) {
    ma_patch p = pluck_patch(0);
    p.vco1 = (ma_vco_controls){ .saw_level = .08f, .triangle_level = .40f, .sine_level = .52f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .triangle_level = .20f, .sine_level = .70f, .pulse_width = .5f };
    p.vco2_level = .26f; p.vco2_interval = 12; p.vco2_fine_cents = 1.5f;
    p.noise_level = .004f; p.mozaik_mix = .03f; p.bcs_amount = .05f;
    p.filter_cutoff_hz = 1350; p.filter_resonance = .12f; p.filter_env_amount = .35f;
    p.filter_keytrack = .25f; p.filter_drive = .12f;
    p.amp_adsr = (ma_adsr){ 4, 1400, .22f, 1300 };
    p.filter_adsr = (ma_adsr){ 2, 900, .25f, 1000 };
    p.body_drive = .12f; p.width = .50f; p.master_level = .15f;
    return p;
}
static ma_patch bass_patch_industrial(void) {
    ma_patch p = hurt_voice_patch(BASS);
    p.vco1.saw_level = .25f;
    p.filter_cutoff_hz = 520; p.filter_drive = .40f; p.mixer_pressure = .30f;
    p.body_drive = .35f;
    return p;
}
static ma_patch gtr_patch(void) {
    ma_patch p = tenor_patch(0);
    p.vco1.saw_level = .55f; p.vco1.pulse_level = .12f; p.vco1.pulse_width = .42f;
    p.vco2.saw_level = .32f; p.vco2_level = .55f; p.vco2_fine_cents = 9;
    p.raster_mix = .08f; p.bcs_amount = .40f; p.bcs_regime = .30f;
    p.mixer_pressure = .45f;
    p.filter_cutoff_hz = 1500; p.filter_resonance = .18f;
    p.filter_drive = .55f; p.filter_env_amount = .30f;
    p.amp_adsr = (ma_adsr){ 5, 400, .88f, 600 };
    p.filter_adsr = (ma_adsr){ 4, 500, .50f, 700 };
    p.macro[MA_MACRO_HEAT] = .30f; p.body_drive = .50f;
    p.width = .80f; p.master_level = .15f;
    return p;
}
static ma_patch kick_patch(void) {
    ma_patch p = drum_patch(KICK);
    p.noise_level = .06f;
    p.filter_cutoff_hz = 150; p.filter_drive = .45f; p.filter_env_amount = .80f;
    p.amp_adsr = (ma_adsr){ 1, 220, 0, 260 };
    p.body_drive = .55f; p.master_level = .55f;
    return p;
}
static ma_patch snare_patch(void) {
    ma_patch p = { 0 };
    p.vco1 = (ma_vco_controls){ .triangle_level = .35f, .sine_level = .30f, .pulse_width = .5f };
    p.vco2.pulse_width = .5f;
    p.noise_level = .85f;
    p.mixer_pressure = .60f;
    p.filter_cutoff_hz = 3200; p.filter_resonance = .22f; p.filter_drive = .70f;
    p.filter_env_amount = .45f;
    /* 3 ms attack: the 1 ms version peaked 7 dB above the rest of the mix. */
    p.amp_adsr = (ma_adsr){ 3, 170, 0, 200 };
    p.filter_adsr = (ma_adsr){ 1, 90, 0, 120 };
    /* Saturated in the voice: noise peaks sat 7 dB over the mix at drive .35/.50. */
    p.body_drive = .80f; p.width = .30f; p.master_level = 1.0f;
    return p;
}
static ma_patch hat_patch(void) {
    ma_patch p = { 0 };
    p.vco1.pulse_width = p.vco2.pulse_width = .5f;
    p.noise_level = .95f;
    p.filter_cutoff_hz = 9000; p.filter_resonance = .15f; p.filter_drive = .20f;
    p.filter_env_amount = .30f;
    p.amp_adsr = (ma_adsr){ 1, 45, 0, 50 };
    p.filter_adsr = (ma_adsr){ 1, 30, 0, 40 };
    p.body_drive = .25f; p.width = .50f; p.master_level = .90f;
    return p;
}

/* The Glas vocal (v5, take a, docs/ma3-hurt-orbita.md). */
static constexpr float VOCAL_MASTER = .124f;
static constexpr float VOCAL_EDGE_LEVEL = .733f;  /* first verse +0.7 dB over before (measured) */
static ma_patch vocal_patch(void) {
    ma_patch p = hurt_voice_patch(VOCAL);
    p.vco1 = (ma_vco_controls){ .saw_level = .55f, .triangle_level = .25f, .sine_level = .15f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .saw_level = .32f, .triangle_level = .20f, .sine_level = .08f, .pulse_width = .5f };
    p.vco2_level = .46f; p.vco2_fine_cents = -11;
    p.sync_amount = 0; p.sync_softness = 0; p.crossmod_amount = 0; p.noise_level = .012f;
    p.filter_keytrack = 0; p.filter_env_amount = 0;
    p.bcs_amount = .06f; p.bcs_regime = .20f;
    p.amp_adsr = (ma_adsr){ 60, 420, .85f, 900 };
    p.filter_adsr = (ma_adsr){ 1, 1, 1, 900 };
    p.formant_mix = .70f; p.formant_shift = -.40f; p.formant_orbita = .55f;
    p.grain_mix = .30f;
    p.body_drive = .10f;
    p.master_level = VOCAL_MASTER;
    return p;
}
static ma_patch vocal_controls(float progress) {
    ma_patch p = vocal_patch();
    p.formant_vowel = .08f + .22f * progress;
    p.filter_cutoff_hz = 1100 - 200 * progress;
    p.filter_resonance = 0;
    p.filter_drive = .30f + .10f * progress;
    p.mixer_pressure = .30f + .08f * progress;
    return p;
}

/* --vocal-edge (operator): the first verse thinner and angrier, with a
 * little dirt. Formants up (less chest), more breath, rougher grains and BCS
 * nearer its edge; the grit sits before the steep lowpass, the post-filter
 * body only a little higher. Blended out over bars 23-25. */
static bool vocal_edge;

/* --vocal-fat (operator): the first verse fat and huge, as the big late-70s
 * polyphonic analog flagships. Vocal notes starting before source bar 24 go
 * to a tenth pool: four detuned voices per note, VCO2 an octave down, saw
 * and pulse through warm pre-filter drive, gentler Glas formants, wide. */
static bool vocal_fat;
/* --vocal-fat-all (operator): every vocal note on the fat pool. */
static bool vocal_fat_all;
enum { P_VFAT = I_PARTS, FAT_END_TICK = (24 - I_START_BAR) * I_BAR };
static constexpr float VOCAL_FAT_GAIN = 1.995f;  /* first verse 1 dB over the edge take (measured) */
static ma_patch vocal_fat_patch(void) {
    ma_patch p = vocal_controls(0);
    p.vco1 = (ma_vco_controls){ .saw_level = .60f, .pulse_level = .18f, .triangle_level = .10f,
                                .sine_level = .22f, .pulse_width = .45f };
    p.vco2 = (ma_vco_controls){ .saw_level = .50f, .triangle_level = .10f, .sine_level = .15f,
                                .pulse_width = .5f };
    p.vco2_level = .85f; p.vco2_interval = -12; p.vco2_fine_cents = 0;
    p.noise_level = .03f;
    p.filter_cutoff_hz = 900; p.filter_resonance = .20f;
    p.filter_drive = .45f; p.mixer_pressure = .40f;
    p.formant_mix = .25f; p.formant_shift = -.30f; p.formant_vowel = .10f;
    p.grain_mix = .10f;
    p.bcs_amount = .15f; p.bcs_regime = .30f;
    p.body_drive = .14f; p.width = .90f;
    return p;
}
static ma_patch vocal_edge_controls(float progress, float edge) {
    ma_patch p = vocal_controls(progress);
    if (edge <= 0) return p;
    p.formant_shift += edge * (-.22f - p.formant_shift);
    p.noise_level += edge * (.05f - p.noise_level);
    p.grain_mix += edge * (.38f - p.grain_mix);
    p.bcs_amount += edge * (.25f - p.bcs_amount);
    p.bcs_regime += edge * (.36f - p.bcs_regime);
    p.filter_cutoff_hz += edge * (1200 - p.filter_cutoff_hz);
    p.filter_drive += edge * (.50f - p.filter_drive);
    p.mixer_pressure += edge * (.45f - p.mixer_pressure);
    p.body_drive += edge * (.16f - p.body_drive);
    p.master_level *= 1 + edge * (VOCAL_EDGE_LEVEL - 1);
    return p;
}

static ma_patch part_patch(unsigned part) {
    switch (part) {
    case I_ARP: return hurt_voice_patch(PLUCK);
    case I_KEYS: return keys_patch();
    case I_BASS: return bass_patch_industrial();
    case I_VOCAL: return vocal_controls(0);
    case I_PAD: {
        ma_patch p = hurt_voice_patch(PAD);
        p.amp_adsr.release_ms = 4000;
        return p;
    }
    case I_GTR: {
        ma_patch p = gtr_patch();
        if (gtr_fat) {
            p.vco2_interval = -12; p.vco2_fine_cents = 0; p.vco2_level = .70f;
            p.filter_env_amount = .60f;
            p.filter_adsr = (ma_adsr){ 2, 160, .35f, 120 };
            p.amp_adsr = (ma_adsr){ 4, 200, .55f, 120 };
            p.width = .90f;
        }
        return p;
    }
    case I_KICK: return rhythm_ma3 ? drum_patch(KICK) : kick_patch();
    case I_SNARE: return rhythm_ma3 ? drum_patch(PULSE) : snare_patch();
    default: return hat_patch();
    }
}

/* --- Orbita on the pad's VCO2 ------------------------------------------------
 * The pad sounds at bars 24-39, 68-75 and 92-100. It enters detuned and
 * locks, escapes once at 32, re-enters sour at 68 and locks, and takes one
 * slow final lock on the closing chord. Fine tune only (interval 0). */
typedef struct { double bar; float relative; } vco2_kick;
static const vco2_kick PAD_KICKS[] = { { 32, .020f }, { 68, .024f }, { 92, -.024f } };
static orbit_system pad_moon;
static unsigned pad_next_kick;
static uint32_t pad_clamps;
static float pad_dissipation(double b) {
    if (b < 24) return 0;
    if (b < 28) return ramp(b, 24, 28, 0, .07f);
    if (b < 68) return .07f;
    if (b < 70) return 0;
    if (b < 92) return .06f;
    return .04f;
}
static void pad_reset(void) {
    orbit_init(&pad_moon, 1, UINT64_C(0x48555254494e4431), MA3_RATE);
    orbit_set_home(&pad_moon, 0, exp2f(16.0f / 1200.0f));
    pad_next_kick = 0;
    pad_clamps = 0;
}
static ma_patch pad_moon_patch(double bar) {
    while (pad_next_kick < sizeof PAD_KICKS / sizeof *PAD_KICKS && bar >= PAD_KICKS[pad_next_kick].bar)
        orbit_kick_moon(&pad_moon, 0, PAD_KICKS[pad_next_kick++].relative);
    orbit_set_controls(&pad_moon, pad_dissipation(bar), 0, 0);
    for (unsigned i = 0; i < TICKS_PER_CONTROL; i++) orbit_tick(&pad_moon);
    ma_patch p = show.part[I_PAD].patch;
    float fine = 1200.0f * log2f(pad_moon.moon[0].ratio);
    if (fine > 50 || fine < -50) { pad_clamps++; fine = fine > 50 ? 50 : -50; }
    p.vco2_fine_cents = fine;
    return p;
}

/* --- Orbita metal -----------------------------------------------------------
 * Each strike re-seeds a five-moon bank on a free voice, starts it in full
 * ring breakup, and lets the section's dissipation pull its moons onto small
 * integer ratios while it rings. Driven through an offset tw_sat, DC-blocked,
 * lowpassed and sent through its own room into the rhythm stem. */
enum { METAL_VOICES = 4, METAL_EVENTS = 512 };
typedef struct { size_t frame; uint8_t note, velocity; } metal_event;
typedef struct { orbit_system bank; float env, decay, f0, gain; bool active; } metal_voice;
static metal_event metal_events[METAL_EVENTS];
static unsigned metal_count;
static struct {
    metal_voice v[METAL_VOICES];
    ma_effects fx;
    unsigned next, cursor;
    uint64_t strikes;
    float dc_x, dc_y, lp;
} metal;
static constexpr float METAL_BIAS = .25f;
static constexpr float METAL_GAIN = .0126f;   /* trim, set by balance */
static constexpr float METAL_LOWPASS_HZ = 6000.0f;

static float metal_dissipation(double b) {
    if (b < 24) return 0;
    if (b < 40) return .30f;
    if (b < 48) return .60f;
    if (b < 68) return .20f;
    if (b < 76) return .60f;
    return .90f;
}
static float metal_drive(double b) { return b < 40 ? 1.8f : b < 76 ? 2.6f : 3.6f; }

static void metal_reset(void) {
    for (unsigned i = 0; i < METAL_VOICES; i++) metal.v[i] = (metal_voice){ 0 };
    metal.next = metal.cursor = 0;
    metal.strikes = 0;
    metal.dc_x = metal.dc_y = metal.lp = 0;
    ma_effects_init(&metal.fx, MA3_RATE);
    ma_effects_set_chorus(&metal.fx, (ma_chorus_controls){ false, 0, .3f, .3f });
    ma_effects_set_reverb(&metal.fx, (ma_reverb_controls){ true, .26f, .55f, .45f });
}
static void metal_strike(const metal_event *e, double bar) {
    metal_voice *v = &metal.v[metal.next];
    metal.next = (metal.next + 1) % METAL_VOICES;
    orbit_init(&v->bank, ORBIT_MAX_MOONS, UINT64_C(0x4d4554414c303031) + 0x9e3779b97f4a7c15u * ++metal.strikes,
               MA3_RATE);
    orbit_set_controls(&v->bank, metal_dissipation(bar), 0, 1);
    for (unsigned k = 0; k < ORBIT_MAX_MOONS; k++) v->bank.moon[k].ring = 1;  /* struck: broken up at once */
    v->f0 = ma_note_frequency_hz(e->note);
    float tau = e->note <= 50 ? .38f : e->note <= 55 ? .28f : .20f;
    v->decay = expf(-1.0f / (tau * MA3_RATE));
    v->env = 1;
    v->gain = (float)e->velocity / 127.0f;
    v->active = true;
}
static float metal_render(double bar) {
    float y = 0;
    for (unsigned i = 0; i < METAL_VOICES; i++) {
        metal_voice *v = &metal.v[i];
        if (!v->active) continue;
        y += v->gain * v->env * orbit_render(&v->bank, v->f0);
        v->env *= v->decay;
        if (v->env < 1e-4f) v->active = false;
    }
    float x = metal_drive(bar) * y;
    float z = tw_sat(x + METAL_BIAS) - tw_sat(METAL_BIAS);
    float dc = z - metal.dc_x + (1.0f - 6.28318531f * 20.0f / MA3_RATE) * metal.dc_y;
    metal.dc_x = z; metal.dc_y = dc;
    metal.lp += (dc - metal.lp) * (1.0f - expf(-6.28318531f * METAL_LOWPASS_HZ / MA3_RATE));
    return metal.lp;
}
static void layer(const ma3_show *s, size_t frame, ma_frame stems[MA3_STEMS]) {
    (void)s;
    if (frame == 0) metal_reset();
    double bar = i_bar(frame);
    while (metal.cursor < metal_count && metal_events[metal.cursor].frame == frame)
        metal_strike(&metal_events[metal.cursor++], bar);
    float m = METAL_GAIN * metal_render(bar);
    ma_frame w = ma_effects_tick(&metal.fx, (ma_frame){ m, m });
    stems[ST_RHYTHM].left += w.left;
    stems[ST_RHYTHM].right += w.right;
}

/* --- controls -------------------------------------------------------------- */

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    if (frame == 0) pad_reset();
    double bar = i_bar(frame);
    float e = energy(bar);
    ma_patch pad = pad_moon_patch(bar);
    if (bar >= 94) pad.master_level *= 1 - .75f * fminf((float)(bar - 94) / 6, 1);
    ma_voice_pool_set_patch(&p[I_PAD], &pad);
    ma_voice_pool_set_macro(&p[I_PAD], MA_MACRO_BLOOM, .05f + .12f * e);
    ma_voice_pool_set_lfo(&p[I_PAD], .008f, .08f);
    ma_voice_pool_set_macro(&p[I_GTR], MA_MACRO_HEAT, .20f + .25f * e);
    float t = (float)((bar - 8) / 84); t = t < 0 ? 0 : t > 1 ? 1 : t;
    float edge = vocal_edge ? ramp(bar, 23, 25, 1, 0) : 0;
    ma_patch vocal = vocal_edge_controls(t * t * (3 - 2 * t), edge);
    ma_voice_pool_set_patch(&p[I_VOCAL], &vocal);
    ma_voice_pool_set_channel_pressure(&p[I_VOCAL], 0, .10f + .12f * e);
    if (vocal_fat) ma_voice_pool_set_channel_pressure(&p[P_VFAT], 0, .10f + .12f * e);
    if (bar >= 94) {
        float g = 1 - .75f * fminf((float)(bar - 94) / 6, 1);
        static const unsigned held[] = { I_GTR, I_BASS };
        for (unsigned i = 0; i < sizeof held / sizeof held[0]; i++) {
            ma_patch q = s->part[held[i]].patch;
            q.master_level *= g;
            ma_voice_pool_set_patch(&p[held[i]], &q);
        }
    }
}

static void parts_init(ma3_show *s) {
    const unsigned limits[] = { 16, 8, 4, 8, 16, 16, 4, 4, 4 };
    const unsigned unison[] = { 1, 1, 1, 1, 2, 2, 1, 1, 1 };
    const unsigned stems[] = { ST_TONAL, ST_TONAL, ST_BASS, ST_VOCAL, ST_PADS, ST_GUITAR,
                               ST_RHYTHM, ST_RHYTHM, ST_RHYTHM };
    /* Balanced from single-part renders (docs/ma3-hurt-industrial.md). */
    float gain[] = { 1.26f, 2.13f, 2.0f, 2.2f, 1.6f, 2.48f, 1.6f, 1.49f, 1.21f };
    float ch[] = { .12f, .10f, 0, .28f, .28f, .14f, 0, 0, 0 };
    float rv[] = { .18f, .24f, .05f, .55f, .30f, .20f, .05f, .16f, .08f };
    if (rhythm_ma3) {
        /* The MA3 Hurt kick and pulse settings (driver/ma3_hurt_parts.h). */
        gain[I_KICK] = 1.1f; ch[I_KICK] = 0; rv[I_KICK] = .04f;
        gain[I_SNARE] = 1.7f; ch[I_SNARE] = .06f; rv[I_SNARE] = .14f;
    }
    for (unsigned p = 0; p < I_PARTS; p++) {
        ma3_part_init(&s->part[p], I_NAMES[p], part_patch(p), limits[p], unison[p], stems[p],
                      gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == I_GTR ? 7 : 4;
    }
    /* The Glas vocal's wet, dark room (docs/ma3-hurt-orbita.md, v4/v5). */
    s->part[I_VOCAL].chorus = (ma_chorus_controls){ true, .28f, .35f, .25f };
    s->part[I_VOCAL].reverb = (ma_reverb_controls){ true, .55f, .85f, .75f };
    if (gtr_fat) {
        s->part[I_GTR].config.unison_count = 4;
        s->part[I_GTR].config.limit = 24;
        s->part[I_GTR].config.detune_cents = 10;
        s->part[I_GTR].gain *= GTR_FAT_GAIN;
        s->part[I_GTR].chorus = (ma_chorus_controls){ true, .20f, .35f, .25f };
        s->part[I_GTR].reverb = (ma_reverb_controls){ true, .38f, .80f, .65f };
    }
    if (vocal_fat) {
        ma3_part_init(&s->part[P_VFAT], "vocalfat", vocal_fat_patch(), 16, 4, ST_VOCAL,
                      gain[I_VOCAL] * VOCAL_FAT_GAIN, .28f, .55f);
        s->part[P_VFAT].config.detune_cents = 10;
        s->part[P_VFAT].reverb = s->part[I_VOCAL].reverb;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    if (m->format != 1 || m->division != I_PPQ || m->tempo_count != 1
        || m->tempos[0].tick != 0 || m->tempos[0].us_per_quarter != I_US_PER_QUARTER) {
        *reason = "MIDI is not the industrial score layout";
        return false;
    }
    unsigned attacks[I_CHANNELS] = { 0 };
    /* Per-pitch FIFO of the pool that holds each sounding vocal note. */
    uint8_t queue[128][4] = { { 0 } }, head[128] = { 0 }, count[128] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= I_CHANNELS
            || e->tick > (uint64_t)(I_END_BAR - I_START_BAR) * I_BAR) {
            *reason = "unexpected event in the industrial score";
            return false;
        }
        bool on = type == 0x90u && e->d2;
        attacks[channel] += on;
        if (!include[channel]) continue;
        if (channel == I_METAL) {
            if (!on) continue;
            if (metal_count == METAL_EVENTS) { *reason = "metal capacity exceeded"; return false; }
            metal_events[metal_count++] = (metal_event){ i_frame(e->tick), e->d1, e->d2 };
            continue;
        }
        uint8_t part = (uint8_t)channel;
        if (channel == I_VOCAL && vocal_fat) {
            uint8_t n = e->d1;
            if (on) {
                if (count[n] == 4) { *reason = "vocal routing overflow"; return false; }
                part = vocal_fat_all || e->tick < FAT_END_TICK ? P_VFAT : I_VOCAL;
                queue[n][(head[n] + count[n]++) % 4] = part;
            } else if (count[n]) {
                part = queue[n][head[n]];
                head[n] = (uint8_t)((head[n] + 1) % 4);
                count[n]--;
            }
        }
        if (!ma3_add_event(s, (ma3_event){ .frame = i_frame(e->tick), .part = part,
                .note = e->d1, .velocity = on ? e->d2 : 0, .on = on,
                .pressure = channel == I_VOCAL ? .22f : .12f })) {
            *reason = "event capacity exceeded";
            return false;
        }
    }
    for (unsigned c = 0; c < I_CHANNELS; c++)
        if (!attacks[c] && !(rhythm_ma3 && (c == I_HAT || c == I_METAL))) {
            *reason = "a channel has no notes";
            return false;
        }
    /* Strikes in time order (the SMF reader returns events merged by tick). */
    for (unsigned i = 1; i < metal_count; i++)
        for (unsigned j = i; j > 0 && metal_events[j - 1].frame > metal_events[j].frame; j--) {
            metal_event t = metal_events[j]; metal_events[j] = metal_events[j - 1]; metal_events[j - 1] = t;
        }
    return true;
}

static bool select_parts(const char *list) {
    for (unsigned c = 0; c < I_CHANNELS; c++) include[c] = false;
    while (*list) {
        size_t n = strcspn(list, ",");
        bool found = false;
        for (unsigned c = 0; c < I_CHANNELS; c++)
            if (strlen(I_NAMES[c]) == n && !strncmp(list, I_NAMES[c], n)) include[c] = found = true;
        if (!found) return false;
        list += n + (list[n] == ',');
    }
    return true;
}

int main(int argc, char **argv) {
    const char *input = NULL, *prefix = NULL;
    const char *parts = NULL;
    unsigned seconds = 0;
    bool check = false;
    for (unsigned c = 0; c < I_CHANNELS; c++) include[c] = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--parts") && i + 1 < argc) parts = argv[++i];
        else if (!strcmp(argv[i], "--vocal-edge")) vocal_edge = true;
        else if (!strcmp(argv[i], "--vocal-fat")) vocal_fat = true;
        else if (!strcmp(argv[i], "--gtr-fat")) gtr_fat = true;
        else if (!strcmp(argv[i], "--engine") && i + 1 < argc) {
            const char *e = argv[++i];
            if (!strcmp(e, "scalar")) engine_scalar = true;
            else if (strcmp(e, "auto")) return 2;
        }
        else if (!strcmp(argv[i], "--vocal-fat-all")) vocal_fat = vocal_fat_all = true;
        else if (!strcmp(argv[i], "--rhythm") && i + 1 < argc) {
            const char *r = argv[++i];
            if (!strcmp(r, "ma3")) rhythm_ma3 = true;
            else if (strcmp(r, "industrial")) return 2;
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, I_SECONDS, &n)) return 2;
            seconds = (unsigned)n;
        } else if (!strcmp(argv[i], "--check")) check = true;
        else {
            fprintf(stderr, "Usage: %s --check | --render PREFIX [--seconds 1..%d] "
                    "[--rhythm industrial|ma3] [--vocal-edge] [--vocal-fat] [--vocal-fat-all] [--gtr-fat] [--engine auto|scalar] [--parts arp,keys,bass,vocal,pad,gtr,kick,snare|pulse,hat,metal] "
                    "[-i score.mid]\n",
                    argv[0], I_SECONDS);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (rhythm_ma3) {
        I_NAMES[I_SNARE] = "pulse";
        for (unsigned c = 0; c < I_CHANNELS; c++) include[c] = c != I_HAT && c != I_METAL;
    }
    if (parts && !select_parts(parts)) return 2;
    if (!input) input = rhythm_ma3 ? "notes-midi/local/hurt-industrial-ma3rhythm-20261107.mid"
                                   : "notes-midi/local/hurt-industrial-20261107.mid";
    show = (ma3_show){ .name = "MA3 Hurt industrial 2026-11-07",
        .part_count = vocal_fat ? I_PARTS + 1 : I_PARTS,
        .stem_count = ST_COUNT, .seconds = I_SECONDS,
        .stem_name = { "tonal", "bass", "pads", "vocal", "guitar", "rhythm" },
        .controls = controls, .layer = include[I_METAL] ? layer : NULL };
    parts_init(&show);
    if (engine_scalar)
        for (unsigned p = 0; p < show.part_count; p++)
            show.part[p].config.engine = MA_POOL_ENGINE_SCALAR;
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x07ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok) printf("  metal: %u strikes\n", metal_count);
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (ok && prefix) printf("  pad moon: %u captures, %u escapes, %u fine clamps; metal: %llu strikes\n",
                             pad_moon.captures, pad_moon.escapes, pad_clamps,
                             (unsigned long long)metal.strikes);
    if (!ok) fprintf(stderr, "Hurt industrial: %s\n", reason ? reason : "score/render validation failed");
    if (ok && pad_clamps) { fprintf(stderr, "Hurt industrial: pad fine clamped\n"); ok = false; }
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
