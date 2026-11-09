/* MA3 Hurt, 2026-11-07 orchestration (edition 2 with --edition 2: no noise
 * wash or wash intro, derived kick on channel 5 and backbeat snare on 9): ten Mamut parts, one per channel of
 * notes-midi/local/hurt-orch-20261107.mid (driver/make_hurt_orchestration.py),
 * rendered in one pass by ma3_show. The score layer of the earlier exhibit
 * is not used; only its patch helpers are shared. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "ma3_hurt_material.h"
#include "ma3_hurt_parts.h"
#pragma GCC diagnostic pop

enum { O_ARP, O_KEYS, O_BASS, O_VOCAL, O_PAD, O_WASH, O_DRIVE, O_LEAD, O_TOMS, O_STICK, O_PARTS };
enum { ST_TONAL, ST_BASS, ST_PADS, ST_VOCAL, ST_LEAD, ST_RHYTHM, ST_COUNT };
enum { O_PPQ = 480, O_BAR = 4 * O_PPQ, O_US_PER_QUARTER = 740740, O_END_BAR = 100, O_SECONDS = 306,
       O_KICK = O_WASH, O_SNARE = O_STICK, O2_INTRO_BARS = 8, O2_SECONDS = 283 };
static const char *const O_NAMES[2][O_PARTS] = {
    { "arp", "keys", "bass", "vocal", "pad", "wash", "drive", "lead", "toms", "stick" },
    { "arp", "keys", "bass", "vocal", "pad", "kick", "drive", "lead", "toms", "snare" },
};

static ma3_show show;
static unsigned edition = 1;

/* 48 kHz at 740740 us per 480-tick quarter: exactly 74.0740 frames per tick. */
static size_t o_frame(uint64_t tick) { return (size_t)((tick * 740740u + 5000u) / 10000u); }
/* Bars are always counted in the source score; edition 2 starts at bar 8. */
static float o_bar(size_t frame) {
    return (float)frame * 10000.0f / (740740.0f * O_BAR) + (edition == 2 ? O2_INTRO_BARS : 0);
}

/* Form energy from the score's sections: noise intro, first verse, bass and
 * pad entry, drums and piano, quiet second verse, driven guitar, the climax
 * up to the bar-91 hit, and the held E ending. */
static float energy(float bar) {
    static const float at[][2] = {
        {0, .10f}, {8, .20f}, {24, .35f}, {40, .60f}, {48, .30f}, {68, .55f},
        {76, .85f}, {91, 1.0f}, {92, .60f}, {100, .15f},
    };
    constexpr unsigned n = sizeof at / sizeof at[0];
    if (bar <= at[0][0]) return at[0][1];
    for (unsigned i = 1; i < n; i++)
        if (bar < at[i][0])
            return at[i-1][1] + (at[i][1] - at[i-1][1]) * (bar - at[i-1][0]) / (at[i][0] - at[i-1][0]);
    return at[n-1][1];
}

static void controls(ma_voice_pool *p, const ma3_show *s, size_t frame) {
    float bar = o_bar(frame), e = energy(bar);
    ma_voice_pool_set_macro(&p[O_PAD], MA_MACRO_BLOOM, .05f + .12f * e);
    ma_voice_pool_set_lfo(&p[O_PAD], .008f, .08f);
    if (edition == 1) ma_voice_pool_set_macro(&p[O_WASH], MA_MACRO_SWARM, .02f + .06f * e);
    ma_voice_pool_set_macro(&p[O_DRIVE], MA_MACRO_HEAT, .14f + .18f * e);
    ma_voice_pool_set_mod_wheel(&p[O_LEAD], 0, .06f + .12f * e);
    /* The vocal darkens across its own span, bars 8..92. */
    float t = (bar - 8) / 84; t = t < 0 ? 0 : t > 1 ? 1 : t;
    float progress = t * t * (3 - 2 * t);
    ma_patch vocal = s->part[O_VOCAL].patch;
    vocal.filter_cutoff_hz = 900 - 560 * progress;
    vocal.filter_resonance = .16f + .05f * progress;
    vocal.filter_drive = .16f + .05f * progress;
    vocal.mixer_pressure = .14f + .04f * progress;
    ma_voice_pool_set_patch(&p[O_VOCAL], &vocal);
    ma_voice_pool_set_channel_pressure(&p[O_VOCAL], 0, .10f + .16f * e);
    /* The held ending recedes over bars 94..100 instead of stopping. */
    if (bar >= 94) {
        float g = 1 - .75f * fminf((bar - 94) / 6, 1);
        static const unsigned held[] = { O_PAD, O_DRIVE, O_LEAD, O_BASS };
        for (unsigned i = 0; i < sizeof held / sizeof held[0]; i++) {
            ma_patch q = s->part[held[i]].patch;
            q.master_level *= g;
            ma_voice_pool_set_patch(&p[held[i]], &q);
        }
    }
}

static ma_patch keys_patch(void) {
    ma_patch p = pluck_patch(0);
    p.vco1 = (ma_vco_controls){ .saw_level = .05f, .triangle_level = .40f, .sine_level = .55f, .pulse_width = .5f };
    p.vco2 = (ma_vco_controls){ .triangle_level = .20f, .sine_level = .70f, .pulse_width = .5f };
    p.vco2_level = .26f; p.vco2_interval = 12; p.vco2_fine_cents = 1.5f;
    p.noise_level = .004f; p.mozaik_mix = .03f; p.bcs_amount = .05f;
    p.filter_cutoff_hz = 1250; p.filter_resonance = .12f; p.filter_env_amount = .35f;
    p.filter_keytrack = .25f;
    p.amp_adsr = (ma_adsr){ 4, 1400, .22f, 1300 };
    p.filter_adsr = (ma_adsr){ 2, 900, .25f, 1000 };
    p.width = .50f; p.master_level = .15f;
    return p;
}
static ma_patch wash_patch(void) {
    ma_patch p = ma_patch_dubina;
    p.vco1 = p.vco2 = (ma_vco_controls){ .pulse_width = .5f };
    p.vco2_level = 0; p.crossmod_amount = 0; p.noise_level = .9f;
    p.mozaik_mix = 0; p.raster_mix = 0; p.bcs_amount = 0;
    p.filter_cutoff_hz = 420; p.filter_resonance = .10f; p.filter_keytrack = 0;
    p.filter_drive = .05f; p.filter_env_amount = .25f;
    p.amp_adsr = (ma_adsr){ 1400, 1500, .80f, 2600 };
    p.filter_adsr = (ma_adsr){ 1600, 2000, .50f, 2500 };
    for (unsigned i = 0; i < MA_MACRO_COUNT; i++) p.macro[i] = 0;
    p.body_drive = .05f; p.width = .95f; p.crossfeed = .10f; p.master_level = .20f;
    return p;
}
static ma_patch drive_patch(void) {
    ma_patch p = tenor_patch(0);
    p.vco1.saw_level = .30f; p.vco2.saw_level = .14f;
    p.raster_mix = .06f; p.bcs_amount = .35f;
    p.filter_cutoff_hz = 1100; p.filter_resonance = .17f;
    p.filter_drive = .30f; p.filter_env_amount = .30f;
    p.amp_adsr = (ma_adsr){ 6, 400, .85f, 600 };
    p.filter_adsr = (ma_adsr){ 4, 500, .45f, 700 };
    p.macro[MA_MACRO_HEAT] = .20f; p.body_drive = .30f;
    p.width = .60f; p.master_level = .13f;
    return p;
}
static ma_patch lead_patch(void) {
    ma_patch p = ma_dark_lead_patch();
    p.vco1.saw_level = .18f; p.vco2.saw_level = .08f;
    p.filter_cutoff_hz = 1300;
    p.amp_adsr = (ma_adsr){ 25, 600, .75f, 900 };
    p.filter_adsr = (ma_adsr){ 20, 700, .40f, 900 };
    p.bcs_amount = .12f; p.bcs_regime = .28f;
    return p;
}
static ma_patch toms_patch(void) {
    ma_patch p = drum_patch(KICK);
    p.filter_cutoff_hz = 240; p.filter_env_amount = .50f;
    p.amp_adsr = (ma_adsr){ 6, 380, 0, 420 };
    p.filter_adsr = (ma_adsr){ 1, 90, 0, 120 };
    p.master_level = .40f;
    return p;
}
static ma_patch stick_patch(void) {
    ma_patch p = { 0 };
    p.vco1 = (ma_vco_controls){ .triangle_level = .30f, .sine_level = .30f, .pulse_width = .5f };
    p.vco2.pulse_width = .5f;
    p.noise_level = .50f;
    p.filter_cutoff_hz = 2600; p.filter_resonance = .25f; p.filter_drive = .08f;
    p.filter_env_amount = .40f;
    p.amp_adsr = (ma_adsr){ 1, 70, 0, 80 };
    p.filter_adsr = (ma_adsr){ 1, 50, 0, 60 };
    p.body_drive = .10f; p.width = .20f; p.master_level = .25f;
    return p;
}
static ma_patch snare_patch(void) {
    ma_patch p = { 0 };
    p.vco1 = (ma_vco_controls){ .triangle_level = .35f, .sine_level = .30f, .pulse_width = .5f };
    p.vco2.pulse_width = .5f;
    p.noise_level = .65f;
    p.filter_cutoff_hz = 2800; p.filter_resonance = .20f; p.filter_drive = .10f;
    p.filter_env_amount = .45f;
    p.amp_adsr = (ma_adsr){ 1, 140, 0, 160 };
    p.filter_adsr = (ma_adsr){ 1, 90, 0, 120 };
    p.body_drive = .18f; p.width = .30f; p.master_level = .30f;
    return p;
}
static ma_patch part_patch(unsigned part) {
    if (edition == 2 && part == O_KICK) return drum_patch(KICK);
    if (edition == 2 && part == O_SNARE) return snare_patch();
    switch (part) {
    case O_ARP: return hurt_voice_patch(PLUCK);
    case O_KEYS: return keys_patch();
    case O_BASS: return hurt_voice_patch(BASS);
    case O_VOCAL: return hurt_voice_patch(VOCAL);
    case O_PAD: {
        ma_patch p = hurt_voice_patch(PAD);
        p.amp_adsr.release_ms = 4000;
        return p;
    }
    case O_WASH: return wash_patch();
    case O_DRIVE: return drive_patch();
    case O_LEAD: return lead_patch();
    case O_TOMS: return toms_patch();
    default: return stick_patch();
    }
}
static void parts_init(ma3_show *s) {
    /* Limits cover the score's held peak times unison (checked by ma3_check);
     * released tails above that are stolen and reported. */
    const unsigned limits[] = { 16, 8, 4, 8, 16, 4, 16, 12, 8, 4 };
    const unsigned unison[] = { 1, 1, 1, 1, 2, 1, 2, 2, 1, 1 };
    const unsigned stems[] = { ST_TONAL, ST_TONAL, ST_BASS, ST_VOCAL, ST_PADS, ST_PADS,
                               ST_LEAD, ST_LEAD, ST_RHYTHM, ST_RHYTHM };
    float gain[] = { 1.0f, 1.2f, 1.6f, 2.2f, 1.6f, 4.0f, 1.2f, 1.2f, .6f, .6f };
    float ch[] = { .12f, .10f, 0, .08f, .28f, .30f, .14f, .16f, 0, 0 };
    float rv[] = { .18f, .24f, .05f, .18f, .30f, .35f, .20f, .26f, .10f, .12f };
    if (edition == 2) {
        gain[O_KICK] = 1.4f; ch[O_KICK] = 0; rv[O_KICK] = .04f;
        gain[O_SNARE] = 1.1f; rv[O_SNARE] = .14f;
        gain[O_TOMS] = 1.0f;
    }
    for (unsigned p = 0; p < O_PARTS; p++) {
        ma3_part_init(&s->part[p], O_NAMES[edition - 1][p], part_patch(p), limits[p], unison[p], stems[p], gain[p], ch[p], rv[p]);
        s->part[p].config.detune_cents = p == O_LEAD ? 6 : p == O_DRIVE ? 5 : 4;
    }
}

static bool load_score(ma3_show *s, const smf_file *m, const char **reason) {
    if (m->format != 1 || m->division != O_PPQ || m->tempo_count != 1
        || m->tempos[0].tick != 0 || m->tempos[0].us_per_quarter != O_US_PER_QUARTER) {
        *reason = "MIDI is not the 2026-11-07 orchestration layout";
        return false;
    }
    unsigned attacks[O_PARTS] = { 0 };
    for (size_t i = 0; i < m->event_count; i++) {
        const smf_event *e = &m->events[i];
        unsigned type = e->status & 0xf0u, channel = e->status & 15u;
        if ((type != 0x80u && type != 0x90u) || channel >= O_PARTS
            || e->tick > (uint64_t)(O_END_BAR - (edition == 2 ? O2_INTRO_BARS : 0)) * O_BAR) {
            *reason = "unexpected event in orchestration MIDI";
            return false;
        }
        bool on = type == 0x90u && e->d2;
        attacks[channel] += on;
        if (!ma3_add_event(s, (ma3_event){ .frame = o_frame(e->tick), .part = (uint8_t)channel,
                .note = e->d1, .velocity = on ? e->d2 : 0, .on = on,
                .pressure = channel == O_VOCAL ? .22f : .12f })) {
            *reason = "event capacity exceeded";
            return false;
        }
    }
    for (unsigned p = 0; p < O_PARTS; p++)
        if (!attacks[p]) { *reason = "a part has no notes"; return false; }
    return true;
}

int main(int argc, char **argv) {
    const char *input = NULL, *prefix = NULL;
    unsigned seconds = 0;
    bool check = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, O_SECONDS, &n)) return 2;
            seconds = (unsigned)n;
        } else if (!strcmp(argv[i], "--edition") && i + 1 < argc) {
            uint64_t n = 0;
            if (!host_parse_u64(argv[++i], 1, 2, &n)) return 2;
            edition = (unsigned)n;
        } else if (!strcmp(argv[i], "--check")) check = true;
        else {
            fprintf(stderr, "Usage: %s [--edition 1|2] --check | --render PREFIX [--seconds 1..%d] [-i orchestration.mid]\n",
                    argv[0], O_SECONDS);
            return 2;
        }
    }
    if ((check && prefix) || (seconds && !prefix) || (!check && !prefix)) return 2;
    if (!input) input = edition == 1 ? "notes-midi/local/hurt-orch-20261107.mid"
                                     : "notes-midi/local/hurt-orch-20261107-v2.mid";
    show = (ma3_show){ .name = edition == 1 ? "MA3 Hurt orchestra 2026-11-07" : "MA3 Hurt orchestra 2026-11-07 v2",
        .part_count = O_PARTS, .stem_count = ST_COUNT, .seconds = edition == 1 ? O_SECONDS : O2_SECONDS,
        .stem_name = { "tonal", "bass", "pads", "vocal", "lead", "rhythm" }, .controls = controls };
    parts_init(&show);
    smf_file midi = { 0 };
    uint8_t *bytes = NULL;
    const char *reason = NULL;
    bool ok = midi_load(&midi, &bytes, input, 0x03ffu, &reason) && load_score(&show, &midi, &reason)
        && ma3_check(&show);
    if (ok && prefix) ok = ma3_render(&show, prefix, seconds);
    if (!ok) fprintf(stderr, "Hurt orchestra: %s\n", reason ? reason : "score/render validation failed");
    smf_dispose(&midi);
    free(bytes);
    return ok ? 0 : 1;
}
