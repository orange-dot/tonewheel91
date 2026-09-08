/* Chopin Op. 28/4: a free, dark Mamut Analog performance.
 * The Mutopia piano score supplies pitches; timing and orchestration are ours.
 * No audio work occurs unless --render is explicitly supplied. */
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "smf.h"
#include "wav.h"

enum { RATE = 48000, BLOCK = 4096, SOURCE_CAPACITY = 1024,
       EVENT_CAPACITY = 4096, COMB_COUNT = 4, COMB_CAPACITY = 1536,
       ALLPASS_COUNT = 2, ALLPASS_CAPACITY = 640 };
typedef enum { LEAD, CHORDS, BASS, ECHO, PART_COUNT } part_id;
enum { MIX = PART_COUNT, OUTPUT_COUNT = PART_COUNT + 1 };
static const char *const NAMES[] = { "lead", "chords", "bass", "echo", "mix" };
static constexpr char INPUT[] = "notes-midi/local/chopin-prelude-op28-no4-mutopia.mid";
static constexpr char OUTPUT[] = "build/ma_chopin_op28_4";
static constexpr double INTRO = 12.0;
static constexpr double TAIL = 12.0;
/* Zero-based source bars, including the final held chord. Stretto arrives
 * in bars 15--17; the closing silences receive their own time. */
static const double BAR_SECONDS[] = {
    6.8, 6.5, 6.4, 7.0, 6.5, 6.3, 6.4, 7.2, 6.5, 6.0, 6.2, 7.5,
    7.0, 6.6, 6.1, 5.5, 5.0, 5.7, 7.5, 7.2, 7.7, 8.0, 9.0, 9.2, 9.0, 8.0,
};

typedef struct { double begin, end; uint8_t channel, note; } source_note;
typedef struct {
    size_t frame;
    uint32_t order;
    part_id part;
    uint8_t note, velocity;
    bool on;
} score_event;
typedef struct {
    source_note source[SOURCE_CAPACITY];
    size_t source_count, count, frames;
    score_event event[EVENT_CAPACITY];
} score;
typedef struct {
    float comb_l[COMB_COUNT][COMB_CAPACITY], comb_r[COMB_COUNT][COMB_CAPACITY];
    float comb_filter_l[COMB_COUNT], comb_filter_r[COMB_COUNT];
    unsigned comb_position_l[COMB_COUNT], comb_position_r[COMB_COUNT];
    float allpass_l[ALLPASS_COUNT][ALLPASS_CAPACITY], allpass_r[ALLPASS_COUNT][ALLPASS_CAPACITY];
    unsigned allpass_position_l[ALLPASS_COUNT], allpass_position_r[ALLPASS_COUNT];
    ma_frame input_lp, wet_lp, wet_lp2;
} room;
typedef struct { ma_card_bank bank[PART_COUNT]; room reverb[PART_COUNT]; } performance;
typedef struct { float peak; double square; uint64_t hash; } metrics;

static double score_time(double beat) {
    size_t bar = (size_t)(beat / 4.0);
    double time = INTRO;
    for (size_t i = 0; i < bar; ++i) time += BAR_SECONDS[i];
    double u = (beat - 4.0 * (double)bar) / 4.0;
    /* A monotone breath within each bar, with continuous bar boundaries. */
    return time + BAR_SECONDS[bar] * (u + .025 * sin(6.283185307179586 * u));
}

static float energy_at(double time) {
    static const struct { double beat; float energy; } shape[] = {
        { 0, .10f }, { 16, .25f }, { 32, .42f }, { 44, .57f },
        { 48, .18f }, { 56, .40f }, { 64, 1.0f }, { 68, .85f },
        { 76, .30f }, { 88, .10f }, { 97, .04f }, { 103, 0 },
    };
    if (time <= INTRO) return .10f;
    for (size_t i = 1; i < sizeof shape / sizeof *shape; ++i) {
        double end = score_time(shape[i].beat);
        if (time > end) continue;
        double begin = score_time(shape[i - 1].beat);
        float u = (float)((time - begin) / (end - begin));
        u = u * u * (3 - 2 * u);
        return shape[i - 1].energy + u * (shape[i].energy - shape[i - 1].energy);
    }
    return 0;
}

static int compare_source(const void *a, const void *b) {
    const source_note *x = a, *y = b;
    if (x->channel != y->channel) return x->channel < y->channel ? -1 : 1;
    if (x->begin != y->begin) return x->begin < y->begin ? -1 : 1;
    return (int)x->note - (int)y->note;
}

static bool load_source(score *s, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return false; }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length > 0 && length <= 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)length) : NULL;
    ok = ok && bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file) != 0) ok = false;
    smf_file midi = { 0 };
    smf_error error = { 0 };
    if (ok) ok = smf_parse(bytes, (size_t)length, UINT16_MAX, &midi, &error);
    free(bytes);
    if (!ok) {
        fprintf(stderr, "MIDI read/parse failed: %s\n", error.message ? error.message : path);
        smf_dispose(&midi);
        return false;
    }
    size_t active[2][128] = { 0 }, attacks[2] = { 0 };
    for (size_t i = 0; i < midi.event_count && ok; ++i) {
        smf_event e = midi.events[i];
        unsigned kind = e.status & 0xf0, channel = e.status & 15;
        if (kind != 0x80 && kind != 0x90) continue;
        double beat = (double)e.tick / midi.division;
        if (channel < 1 || channel > 2 || beat > 103.0) { ok = false; break; }
        size_t *owner = &active[channel - 1][e.d1];
        if (kind == 0x90 && e.d2) {
            if (*owner || s->source_count == SOURCE_CAPACITY) { ok = false; break; }
            s->source[s->source_count++] = (source_note){
                .begin = beat, .channel = (uint8_t)channel, .note = e.d1,
            };
            *owner = s->source_count;
            attacks[channel - 1]++;
        } else {
            if (!*owner || beat <= s->source[*owner - 1].begin) { ok = false; break; }
            s->source[*owner - 1].end = beat;
            *owner = 0;
        }
    }
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned n = 0; n < 128; ++n)
            if (active[c][n]) ok = false;
    /* This is an arrangement of the documented Mutopia edition, not a
     * general MIDI orchestrator. Reject other layouts instead of guessing. */
    ok = ok && attacks[0] == 92 && attacks[1] == 512;
    smf_dispose(&midi);
    if (!ok) fprintf(stderr, "Expected Mutopia Op.28/4: 92 + 512 paired notes on MIDI channels 2/3, <=103 beats\n");
    if (ok) qsort(s->source, s->source_count, sizeof *s->source, compare_source);
    return ok;
}

static bool add_note(score *s, part_id part, uint8_t note, double begin,
                     double end, unsigned velocity) {
    if (!isfinite(begin) || !isfinite(end) || begin < 0 || end <= begin
        || end > 240 || note > 127 || !velocity || velocity > 127
        || s->count + 2 > EVENT_CAPACITY) return false;
    size_t first = (size_t)llround(begin * RATE), last = (size_t)llround(end * RATE);
    if (last <= first) return false;
    uint32_t order = (uint32_t)s->count;
    s->event[s->count++] = (score_event){ first, order, part, note, (uint8_t)velocity, true };
    s->event[s->count++] = (score_event){ last, order + 1, part, note, 0, false };
    return true;
}

static int compare_events(const void *a, const void *b) {
    const score_event *x = a, *y = b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    if (x->on != y->on) return x->on ? 1 : -1;
    return (x->order > y->order) - (x->order < y->order);
}

static bool arrange(score *s) {
    bool ok = add_note(s, BASS, 40, .4, 10.9, 62);
    const uint8_t opening[] = { 52, 55, 59 };
    for (size_t i = 0; i < sizeof opening; ++i)
        ok = ok && add_note(s, CHORDS, opening[i], 2.0 + .18 * i, 10.6, 52);
    ok = ok && add_note(s, ECHO, 71, 6.0, 9.3, 48);
    size_t left = 0;
    while (left < s->source_count && s->source[left].channel == 1) ++left;
    bool echoed[26] = { false };
    for (size_t i = 0; i < left && ok; ++i) {
        source_note n = s->source[i];
        double begin = score_time(n.begin), end = score_time(n.end);
        unsigned velocity = 72 + (unsigned)(24 * energy_at(begin));
        ok = add_note(s, LEAD, n.note, begin, end - fmin(.025, (end - begin) * .05), velocity);
        unsigned bar = (unsigned)(n.begin / 4);
        bool answer = bar == 3 || bar == 7 || bar == 11 || bar == 14 || bar == 19 || bar == 21;
        if (answer && !echoed[bar] && n.note >= 64 && n.end - n.begin >= 1.0) {
            /* A short lower-register recollection, not another full melody. */
            ok = ok && add_note(s, ECHO, n.note - 12, end + .35, end + 1.55, 52);
            echoed[bar] = true;
        }
    }
    /* Group left-hand attacks. Keep every changed voicing; reduce repeated
     * eighths to quarters except during the stretto. Never prolong across a rest. */
    size_t group[SOURCE_CAPACITY] = { 0 }, groups = 0;
    for (size_t i = left; i < s->source_count; ++i)
        if (i == left || s->source[i].begin != s->source[i - 1].begin) group[groups++] = i;
    group[groups] = s->source_count;
    bool keep[SOURCE_CAPACITY] = { false };
    for (size_t g = 0; g < groups; ++g) {
        double beat = s->source[group[g]].begin;
        size_t count = group[g + 1] - group[g];
        bool changed = !g || count != group[g] - group[g - 1];
        if (!changed)
            for (size_t j = 0; j < count; ++j)
                if (s->source[group[g] + j].note != s->source[group[g - 1] + j].note)
                    changed = true;
        keep[g] = changed || fabs(beat - round(beat)) < .001 || (beat >= 60 && beat < 72);
    }
    double bass_begin = 0, bass_end = 0, bass_beat = -100;
    uint8_t bass_note = 0;
    for (size_t g = 0; g < groups && ok; ++g) {
        if (!keep[g]) continue;
        size_t i = group[g], next = g + 1;
        while (next < groups && !keep[next]) ++next;
        double beat = s->source[i].begin, begin = score_time(beat);
        double next_time = next < groups ? score_time(s->source[group[next]].begin) : score_time(103);
        float energy = energy_at(begin);
        for (size_t j = i; j < group[g + 1]; ++j) {
            source_note n = s->source[j];
            if (n.note < 44) continue;
            double end = score_time(n.end);
            /* Join an omitted identical pulse, but stop at its actual release. */
            for (size_t h = g + 1; h < next; ++h)
                for (size_t k = group[h]; k < group[h + 1]; ++k)
                    if (s->source[k].note == n.note) end = score_time(s->source[k].end);
            end = fmin(end, next_time - .045);
            ok = ok && add_note(s, CHORDS, n.note, begin, end, 54 + (unsigned)(22 * energy));
        }
        uint8_t low = s->source[i].note;
        while (low > 43) low -= 12;
        while (low < 28) low += 12;
        double group_end = score_time(s->source[i].end);
        if (!bass_note || beat - bass_beat >= 2.0 || beat >= 92) {
            if (bass_note && (low != bass_note || begin > bass_end + .8)) {
                ok = ok && add_note(s, BASS, bass_note, bass_begin,
                                    fmin(bass_end, begin - .06), 70);
                bass_note = 0;
            }
            if (!bass_note) { bass_begin = begin; bass_note = low; }
            bass_beat = beat;
        }
        /* Follow the left hand's gate, including the cadential silences. */
        bass_end = group_end;
        if (next < groups && next_time - bass_end < .8) bass_end = next_time - .06;
    }
    if (bass_note) ok = ok && add_note(s, BASS, bass_note, bass_begin, bass_end, 64);
    s->frames = (size_t)llround((score_time(103) + TAIL) * RATE);
    qsort(s->event, s->count, sizeof *s->event, compare_events);
    return ok;
}

static ma_patch part_patch(part_id part) {
    ma_patch p = ma_patch_tepih;
    p.vco1 = (ma_vco_controls){
        .saw_level = part == LEAD ? .34f : .14f,
        .pulse_level = part == LEAD ? .16f : 0,
        .triangle_level = part == LEAD ? .30f : .52f,
        .sine_level = part == LEAD ? .18f : .34f,
        .pulse_width = .46f,
    };
    p.vco2 = (ma_vco_controls){
        .saw_level = part == LEAD ? .28f : .04f,
        .pulse_level = part == LEAD ? .13f : 0,
        .triangle_level = part == LEAD ? .22f : .42f,
        .sine_level = part == LEAD ? .22f : .54f,
        .pulse_width = .54f,
    };
    p.vco2_interval = 0;
    p.vco2_fine_cents = part == LEAD ? 6.5f : -2.5f;
    p.vco2_level = part == LEAD ? .68f : .45f;
    p.raster_mix = p.mozaik_mix = p.noise_level = 0;
    p.sync_amount = part == LEAD ? .006f : 0;
    p.crossmod_amount = part == LEAD ? .035f : 0;
    for (unsigned i = 0; i < MA_MACRO_COUNT; ++i) p.macro[i] = 0;
    p.filter_cutoff_hz = part == BASS ? 260 : part == LEAD ? 1380 : 500;
    p.filter_resonance = part == LEAD ? .14f : .10f;
    p.filter_keytrack = part == LEAD ? .25f : 0;
    p.filter_env_amount = part == LEAD ? .24f : .12f;
    p.filter_drive = part == LEAD ? .10f : .045f;
    p.mixer_pressure = .04f;
    p.bcs_amount = part == LEAD ? .11f : .05f;
    p.bcs_regime = part == LEAD ? .24f : .20f;
    p.mozaik_mix = 0;
    p.amp_adsr = part == LEAD ? (ma_adsr){ 48, 600, .78f, 750 }
               : part == CHORDS ? (ma_adsr){ 210, 800, .65f, 1500 }
               : part == BASS ? (ma_adsr){ 110, 650, .72f, 850 }
               : (ma_adsr){ 420, 1000, .58f, 2300 };
    p.filter_adsr = p.amp_adsr;
    p.body_drive = part == LEAD ? .12f : .05f;
    p.width = part == BASS ? 0 : part == LEAD ? .62f : .75f;
    p.crossfeed = part == LEAD ? .20f : .16f;
    p.master_level = part == LEAD ? .11f : part == CHORDS ? .12f : .14f;
    return p;
}

static void init_performance(performance *p) {
    for (part_id part = 0; part < PART_COUNT; ++part) {
        ma_patch patch = part_patch(part);
        ma_card_bank_init_patch(&p->bank[part], RATE, &patch);
        ma_card_bank_set_character(&p->bank[part],
                                   part == LEAD ? .46f : part == BASS ? .12f : .24f);
    }
}

static bool apply_event(performance *p, score_event e) {
    ma_card_bank *bank = &p->bank[e.part];
    if (!e.on) return ma_card_bank_note_off(bank, 0, e.note, 0) != MA_CARD_NONE;
    ma_card_owner before[MA_CARD_COUNT] = { 0 };
    memcpy(before, bank->owner, sizeof before);
    uint8_t slot = ma_card_bank_note_on(bank, 0, e.note, e.velocity);
    return slot != MA_CARD_NONE && before[slot].phase != MA_CARD_HELD;
}

static bool check_score(const score *s) {
    bool held[PART_COUNT][128] = { 0 };
    unsigned count[PART_COUNT] = { 0 }, peak[PART_COUNT] = { 0 }, attacks[PART_COUNT] = { 0 };
    performance *p = calloc(1, sizeof *p);
    if (!p) return false;
    init_performance(p);
    bool ok = s->count > 0;
    for (size_t i = 0; i < s->count && ok; ++i) {
        score_event e = s->event[i];
        ok = e.frame < s->frames && held[e.part][e.note] != e.on;
        if (!ok) { fprintf(stderr, "Unbalanced/overlapping note at event %zu\n", i); break; }
        held[e.part][e.note] = e.on;
        if (e.on) { count[e.part]++; attacks[e.part]++; }
        else count[e.part]--;
        if (count[e.part] > peak[e.part]) peak[e.part] = count[e.part];
        ok = apply_event(p, e);
        if (!ok) fprintf(stderr, "Card ownership failure in %s at %.3f s\n", NAMES[e.part], e.frame / (double)RATE);
    }
    for (part_id part = 0; part < PART_COUNT; ++part) {
        printf("  %-7s %u attacks, %u held peak\n", NAMES[part], attacks[part], peak[part]);
        if (count[part] || !attacks[part]) ok = false;
    }
    printf("  %.2f s including intro/tail; %zu events; score/card check %s (no DSP ticks)\n",
           s->frames / (double)RATE, s->count, ok ? "PASS" : "FAIL");
    free(p);
    return ok;
}

static bool write_score(const score *s, const char *path) {
    FILE *file = fopen(path, "w");
    if (!file) { perror(path); return false; }
    bool ok = fprintf(file, "frame,seconds,part,note,velocity,on\n") > 0;
    for (size_t i = 0; i < s->count && ok; ++i) {
        score_event e = s->event[i];
        ok = fprintf(file, "%zu,%.6f,%s,%u,%u,%u\n", e.frame, e.frame / (double)RATE,
                     NAMES[e.part], e.note, e.velocity, e.on) > 0;
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .24f * delayed + .76f * *filtered;
    buffer[*position] = input + .90f * *filtered;
    if (++*position == length) *position = 0;
    return delayed;
}

static float allpass_tick(float buffer[ALLPASS_CAPACITY], unsigned length,
                          unsigned *position, float input) {
    float delayed = buffer[*position];
    float output = delayed - input;
    buffer[*position] = input + .51f * delayed;
    if (++*position == length) *position = 0;
    return output;
}

/* Same dark, damped room as the Hurt exhibit; each stem owns its return. */
static ma_frame room_tick(room *r, ma_frame dry) {
    static const unsigned CL[] = { 1215, 1293, 1397, 1473 }, CR[] = { 1238, 1316, 1420, 1496 };
    static const unsigned AL[] = { 225, 556 }, AR[] = { 248, 579 };
    r->input_lp.left += .07f * (dry.left - r->input_lp.left);
    r->input_lp.right += .07f * (dry.right - r->input_lp.right);
    float left = .76f * r->input_lp.left + .24f * r->input_lp.right;
    float right = .76f * r->input_lp.right + .24f * r->input_lp.left;
    ma_frame wet = { 0 };
    for (unsigned i = 0; i < COMB_COUNT; ++i) {
        wet.left += .25f * comb_tick(r->comb_l[i], CL[i], &r->comb_position_l[i], &r->comb_filter_l[i], left);
        wet.right += .25f * comb_tick(r->comb_r[i], CR[i], &r->comb_position_r[i], &r->comb_filter_r[i], right);
    }
    for (unsigned i = 0; i < ALLPASS_COUNT; ++i) {
        wet.left = allpass_tick(r->allpass_l[i], AL[i], &r->allpass_position_l[i], wet.left);
        wet.right = allpass_tick(r->allpass_r[i], AR[i], &r->allpass_position_r[i], wet.right);
    }
    r->wet_lp.left += .10f * (wet.left - r->wet_lp.left);
    r->wet_lp.right += .10f * (wet.right - r->wet_lp.right);
    r->wet_lp2.left += .10f * (r->wet_lp.left - r->wet_lp2.left);
    r->wet_lp2.right += .10f * (r->wet_lp.right - r->wet_lp2.right);
    return r->wet_lp2;
}

static void controls(performance *p, size_t frame) {
    float energy = energy_at(frame / (double)RATE);
    for (part_id part = 0; part < PART_COUNT; ++part) {
        ma_patch base = part_patch(part);
        float cutoff = base.filter_cutoff_hz + (part == LEAD ? 1000 : part == BASS ? 90 : 320) * energy;
        float drive = base.filter_drive + (part == LEAD ? .08f : .035f) * energy;
        for (unsigned i = 0; i < MA_CARD_COUNT; ++i) {
            ma_synth *card = &p->bank[part].card[i];
            ma_synth_set_filter(card, cutoff, base.filter_resonance, drive, .04f);
            ma_synth_set_lfo(card, part == BASS ? 0 : part == LEAD
                             ? .010f + .004f * energy : .006f + .004f * energy,
                             part == LEAD ? .16f : .09f);
        }
        ma_card_bank_set_output(&p->bank[part], base.body_drive, base.width,
                                base.crossfeed, base.master_level);
    }
}

static bool render_stream(const score *s, wav_f32_writer writer[OUTPUT_COUNT]) {
    performance *p = calloc(1, sizeof *p);
    if (!p) return false;
    init_performance(p);
    float block[OUTPUT_COUNT][2 * BLOCK] = { 0 };
    metrics measured[OUTPUT_COUNT] = { 0 };
    for (unsigned i = 0; i < OUTPUT_COUNT; ++i) measured[i].hash = UINT64_C(14695981039346656037);
    const float gain[] = { .22f, .16f, .25f, .12f };
    const float direct[] = { .68f, .60f, .92f, .30f };
    const float send[] = { .82f, .70f, .16f, .95f };
    size_t next = 0, written = 0;
    bool ok = true;
    for (size_t frame = 0; frame < s->frames && ok; ++frame) {
        if (frame % 480 == 0) controls(p, frame);
        while (next < s->count && s->event[next].frame <= frame && ok)
            ok = apply_event(p, s->event[next++]);
        if (!ok) break;
        ma_frame out[OUTPUT_COUNT] = { 0 };
        float fade = fminf(1, (float)(s->frames - 1 - frame) / (4 * RATE));
        fade *= fade * (3 - 2 * fade);
        for (part_id part = 0; part < PART_COUNT; ++part) {
            ma_frame dry = ma_card_bank_tick_stereo(&p->bank[part]);
            ma_frame wet = room_tick(&p->reverb[part], dry);
            out[part].left = gain[part] * fade * (direct[part] * dry.left + send[part] * wet.left);
            out[part].right = gain[part] * fade * (direct[part] * dry.right + send[part] * wet.right);
            out[MIX].left += out[part].left;
            out[MIX].right += out[part].right;
        }
        for (unsigned part = 0; part < OUTPUT_COUNT; ++part) {
            const float sample[] = { out[part].left, out[part].right };
            for (unsigned c = 0; c < 2; ++c) {
                float x = sample[c];
                if (!isfinite(x) || fabsf(x) >= 1) {
                    fprintf(stderr, "Invalid/clipped %s sample at frame %zu\n", NAMES[part], frame);
                    ok = false;
                    break;
                }
                block[part][2 * written + c] = x;
                measured[part].peak = fmaxf(measured[part].peak, fabsf(x));
                measured[part].square += (double)x * x;
                const unsigned char *bytes = (const unsigned char *)&x;
                for (size_t j = 0; j < sizeof x; ++j) {
                    measured[part].hash ^= bytes[j];
                    measured[part].hash *= UINT64_C(1099511628211);
                }
            }
        }
        if (!ok) break;
        written++;
        if (written == BLOCK || frame + 1 == s->frames) {
            for (unsigned part = 0; part < OUTPUT_COUNT; ++part)
                if (wav_f32_write(&writer[part], block[part], written) != 0) ok = false;
            written = 0;
        }
        if (frame % (10 * RATE) == 0)
            fprintf(stderr, "  rendered %.1f / %.1f s\n", frame / (double)RATE, s->frames / (double)RATE);
    }
    for (unsigned part = 0; part < OUTPUT_COUNT; ++part)
        printf("  %s: peak %.8f RMS %.8f FNV64 %016llx\n", NAMES[part],
               (double)measured[part].peak, sqrt(measured[part].square / (2.0 * s->frames)),
               (unsigned long long)measured[part].hash);
    free(p);
    return ok && next == s->count && measured[MIX].peak > 1e-7f;
}

static bool render_files(const score *s, const char *prefix) {
    wav_f32_writer writer[OUTPUT_COUNT] = { 0 };
    char path[OUTPUT_COUNT][1024] = { 0 }, temporary[OUTPUT_COUNT][1040] = { 0 };
    bool ok = true;
    for (unsigned part = 0; part < OUTPUT_COUNT && ok; ++part) {
        int n = snprintf(path[part], sizeof path[part], "%s_%s.wav", prefix, NAMES[part]);
        ok = n > 0 && (size_t)n < sizeof path[part];
        if (!ok) break;
        snprintf(temporary[part], sizeof temporary[part], "%s.ma-tmp", path[part]);
        ok = wav_f32_open(&writer[part], temporary[part], s->frames, RATE, 2) == 0;
    }
    if (ok) ok = render_stream(s, writer);
    for (unsigned part = 0; part < OUTPUT_COUNT; ++part) {
        if (ok) ok = wav_f32_close(&writer[part]) == 0;
        else wav_f32_abort(&writer[part]);
    }
    if (ok)
        for (unsigned part = 0; part < OUTPUT_COUNT; ++part)
            if (rename(temporary[part], path[part]) != 0) { ok = false; break; }
    if (!ok) {
        fprintf(stderr, "Render or output validation failed\n");
        for (unsigned part = 0; part < OUTPUT_COUNT; ++part)
            if (*temporary[part]) (void)remove(temporary[part]);
    }
    return ok;
}

static int usage(const char *program, int status) {
    fprintf(status ? stderr : stdout,
            "usage: %s [--check | --render] [-i Mutopia.mid] [-o prefix] [--score events.csv]\n"
            "  Default: validate score/card ownership, without audio synthesis.\n"
            "  --render writes 48 kHz float32 lead/chords/bass/echo/mix WAVs.\n", program);
    return status;
}

int main(int argc, char **argv) {
    const char *input = INPUT, *output = OUTPUT, *csv = NULL;
    bool render = false, check = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--render")) render = true;
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--score") && i + 1 < argc) csv = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) return usage(argv[0], 0);
        else return usage(argv[0], 2);
    }
    if (render && check) return usage(argv[0], 2);
    score *s = calloc(1, sizeof *s);
    if (!s) return 1;
    bool ok = load_source(s, input) && arrange(s) && check_score(s);
    if (ok && csv) ok = write_score(s, csv);
    if (ok && render) ok = render_files(s, output);
    if (!ok) fprintf(stderr, "Chopin exhibit failed\n");
    free(s);
    return ok ? 0 : 1;
}
