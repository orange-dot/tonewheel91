/* Hurt for two manuals and monophonic pedals. Hosted interpretation;
 * source provenance and performance limits: docs/organ-hurt.md. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/tonewheel.h"
#include "host_parse.h"
#include "smf.h"
#include "wav.h"

enum { RATE = 48000, BARS = 85, BAR = 1920, SECONDS = 253,
       EVENTS = 4096, BLOCK = 4096, COMBS = 4, DELAY = 2000 };
enum { RIGHT, LEFT, PEDALS, PARTS, MIX = PARTS, OUTPUTS };
static const char *const NAMES[] = { "right", "left", "pedals", "mix" };
typedef struct { uint32_t tick; uint8_t part, note, velocity; bool on; } event;
typedef struct { event events[EVENTS]; size_t count; unsigned vocal, replies; } score;
typedef struct { uint32_t start, end; uint8_t note, velocity; } phrase_note;
typedef struct {
    float line[2][COMBS][DELAY], low[2][COMBS], input[2], output[2];
    unsigned position[2][COMBS];
    float diffuse[2][2][640];
    unsigned diffuse_position[2][2];
} room;
typedef struct { float peak; double square; uint64_t hash; } meter;

static bool load_midi(const char *path, uint16_t mask, smf_file *midi) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return false; }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length > 0 && length <= 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)length) : 0;
    ok = ok && bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file)) ok = false;
    smf_error error = { 0 };
    if (ok) ok = smf_parse(bytes, (size_t)length, mask, midi, &error);
    free(bytes);
    if (!ok) fprintf(stderr, "%s: %s\n", path, error.message ? error.message : "read failed");
    return ok;
}

static bool add(score *s, uint32_t tick, unsigned part, unsigned note,
                unsigned velocity, bool on) {
    if (s->count == EVENTS || tick > BARS * BAR || part >= PARTS || note > 127) return false;
    s->events[s->count++] = (event){ tick, (uint8_t)part, (uint8_t)note, (uint8_t)velocity, on };
    return true;
}

static bool note(score *s, uint32_t start, uint32_t end, unsigned part,
                 unsigned pitch, unsigned velocity) {
    return end > start && add(s, start, part, pitch, velocity, true)
        && add(s, end, part, pitch, 0, false);
}

static int compare(const void *a, const void *b) {
    const event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    if (x->on != y->on) return x->on ? 1 : -1;
    if (x->part != y->part) return (int)x->part - y->part;
    return (int)x->note - y->note;
}

static uint64_t release_tick(const smf_file *midi, size_t start) {
    const smf_event *on = &midi->events[start];
    for (size_t i = start + 1; i < midi->event_count; ++i) {
        const smf_event *e = &midi->events[i];
        unsigned type = e->status & 0xf0u;
        if ((e->status & 15u) == (on->status & 15u) && e->d1 == on->d1
            && (type == 0x80u || (type == 0x90u && !e->d2))) return e->tick;
    }
    return UINT64_MAX;
}

static bool arrange(score *s, const smf_file *backing, const smf_file *vocal) {
    if (backing->format != 1 || backing->tracks != 6 || backing->division != 480
        || backing->event_count != 2980 || backing->tempo_count != 1
        || backing->tempos[0].tick || backing->tempos[0].us_per_quarter != 714285
        || vocal->format != 1 || vocal->tracks != 11 || vocal->division != 480
        || vocal->tempo_count != 1 || vocal->tempos[0].tick
        || vocal->tempos[0].us_per_quarter != 740740) return false;
    phrase_note melody[256] = { 0 };
    unsigned count = 0;
    for (size_t i = 0; i < vocal->event_count; ++i) {
        const smf_event *e = &vocal->events[i];
        if ((e->status & 0xf0u) != 0x90u || !e->d2) continue;
        uint64_t end = release_tick(vocal, i);
        if (count == 256 || e->tick < 8u * BAR || e->d1 < 45 || e->d1 > 69
            || end <= e->tick || end > 93u * BAR) return false;
        melody[count++] = (phrase_note){ (uint32_t)e->tick - 8u * BAR,
            (uint32_t)end - 8u * BAR, (uint8_t)(e->d1 + 12), e->d2 };
    }
    if (count != 200) return false;
    bool ok = true;
    for (unsigned i = 0; i < count; ++i) {
        /* Remove transcription overlaps so the right hand remains one line.
         * Preserve every attack, pitch contour and genuine breath. */
        if (i + 1 < count && melody[i].end > melody[i+1].start)
            melody[i].end = melody[i+1].start;
        ok &= note(s, melody[i].start, melody[i].end, RIGHT, melody[i].note, melody[i].velocity);
    }
    s->vocal = count;
    for (size_t i = 0; i < backing->event_count; ++i) {
        const smf_event *e = &backing->events[i];
        if (e->status != 0x92u || !e->d2) continue;
        uint64_t end = release_tick(backing, i);
        if (end > BARS * BAR || end <= e->tick) return false;
        if (end - e->tick < 120) continue;
        bool free = true;
        for (unsigned n = 0; n < count; ++n)
            if (e->tick < melody[n].end && end > melody[n].start) free = false;
        if (!free) continue;
        ok &= note(s, (uint32_t)e->tick, (uint32_t)end, RIGHT, e->d1, 74);
        s->replies++;
    }

    unsigned weight[BARS][12] = { 0 }, low[BARS] = { 0 }, bass[BARS] = { 0 };
    for (size_t i = 0; i < backing->event_count; ++i) {
        const smf_event *e = &backing->events[i];
        if ((e->status & 0xf0u) != 0x90u || !e->d2) continue;
        unsigned bar = (unsigned)(e->tick / BAR), ch = e->status & 15u;
        if (bar >= BARS) continue;
        if (ch == 4 && (!bass[bar] || e->d1 < bass[bar])) bass[bar] = e->d1;
        if (ch != 0 && ch != 6) continue;
        weight[bar][e->d1 % 12]++;
        if (!low[bar] || e->d1 < low[bar]) low[bar] = e->d1;
    }
    unsigned previous[3] = { 0 }, previous_root = 0;
    for (unsigned bar = 0; bar <= BARS; ++bar) {
        unsigned chord[3] = { 0 }, root = 0;
        if (bar < BARS) {
            root = bass[bar] ? bass[bar] : low[bar] ? low[bar] : previous_root;
            if (!root) root = 35;
            while (root > 42) root -= 12;
            while (root < 31) root += 12;
            unsigned total = 0, pc[3] = { root % 12, (root + 3) % 12, (root + 7) % 12 };
            for (unsigned p = 0; p < 12; ++p) total += weight[bar][p];
            if (!total && previous[0]) memcpy(chord, previous, sizeof chord);
            else {
                weight[bar][pc[0]] = 0;
                for (unsigned v = 1; v < 3; ++v) {
                    for (unsigned p = 0; p < 12; ++p)
                        if (weight[bar][p] > weight[bar][pc[v]]) pc[v] = p;
                    weight[bar][pc[v]] = 0;
                }
                /* Closest compact inversion to the previous left-hand grip. */
                unsigned best = UINT32_MAX;
                for (unsigned floor = 43; floor <= 54; ++floor) {
                    unsigned candidate[3] = { 0 }, cost = 0;
                    for (unsigned v = 0; v < 3; ++v) {
                        candidate[v] = floor + (pc[v] + 12 - floor % 12) % 12;
                        for (unsigned n = 0; n < v; ++n)
                            if (candidate[v] == candidate[n]) candidate[v] += 12;
                    }
                    for (unsigned a = 0; a < 3; ++a)
                        for (unsigned b = a + 1; b < 3; ++b)
                            if (candidate[a] > candidate[b]) {
                                unsigned t = candidate[a]; candidate[a] = candidate[b]; candidate[b] = t;
                            }
                    if (candidate[0] == candidate[1] || candidate[1] == candidate[2]
                        || candidate[2] - candidate[0] > 12) continue;
                    for (unsigned v = 0; v < 3; ++v)
                        cost += (unsigned)abs((int)candidate[v] - (int)(previous[v] ? previous[v] : 48 + 4 * v));
                    if (cost < best) { best = cost; memcpy(chord, candidate, sizeof chord); }
                }
                if (best == UINT32_MAX) {
                    fprintf(stderr, "no compact left voicing at bar %u (%u/%u/%u)\n", bar, pc[0], pc[1], pc[2]);
                    return false;
                }
            }
        }
        uint32_t at = bar * BAR;
        for (unsigned v = 0; v < 3; ++v) {
            bool keep = false;
            for (unsigned n = 0; n < 3; ++n) if (previous[v] == chord[n]) keep = true;
            if (previous[v] && !keep) ok &= add(s, at, LEFT, previous[v], 0, false);
            keep = false;
            for (unsigned n = 0; n < 3; ++n) if (chord[v] == previous[n]) keep = true;
            if (chord[v] && !keep) ok &= add(s, at, LEFT, chord[v], 70, true);
        }
        if (root != previous_root) {
            if (previous_root) ok &= add(s, at, PEDALS, previous_root, 0, false);
            if (root) ok &= add(s, at, PEDALS, root, 76, true);
        }
        memcpy(previous, chord, sizeof previous);
        previous_root = root;
    }
    qsort(s->events, s->count, sizeof *s->events, compare);
    return ok;
}

static bool check(const score *s) {
    bool held[PARTS][128] = { 0 };
    unsigned attacks[PARTS] = { 0 }, peak[PARTS] = { 0 }, span = 0;
    static const unsigned LIMIT[] = { 1, 3, 1 };
    for (size_t i = 0; i < s->count; ++i) {
        event e = s->events[i];
        if (i && compare(&s->events[i-1], &e) > 0) return false;
        if (e.part == PEDALS ? (e.note < 24 || e.note > 48) : (e.note < 36 || e.note > 96)) return false;
        if (held[e.part][e.note] == e.on) return false;
        held[e.part][e.note] = e.on;
        if (e.on) attacks[e.part]++;
        unsigned active = 0, low = 127, high = 0;
        for (unsigned n = 0; n < 128; ++n) if (held[e.part][n]) {
            active++; if (n < low) low = n; high = n;
        }
        if (active > LIMIT[e.part]) return false;
        if (active > peak[e.part]) peak[e.part] = active;
        if (e.part == LEFT && active) {
            if (high - low > 12) return false;
            if (high - low > span) span = high - low;
        }
    }
    for (unsigned p = 0; p < PARTS; ++p) {
        for (unsigned n = 0; n < 128; ++n) if (held[p][n]) return false;
        printf("%s: %u attacks, %u held peak\n", NAMES[p], attacks[p], peak[p]);
    }
    printf("right: %u vocal attacks + %u replies; left span <= %u semitones\n", s->vocal, s->replies, span);
    return true;
}

static bool be32(FILE *file, uint32_t value) {
    uint8_t b[] = { (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value };
    return fwrite(b, 1, sizeof b, file) == sizeof b;
}

static size_t vlq(uint8_t *bytes, uint32_t value) {
    uint8_t b[4] = { (uint8_t)(value & 127) }; size_t n = 1;
    while ((value >>= 7)) b[n++] = (uint8_t)((value & 127) | 128);
    for (size_t i = 0; i < n; ++i) bytes[i] = b[n-i-1];
    return n;
}

static bool write_score(const score *s, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    static const uint8_t HEADER[] = { 'M','T','h','d',0,0,0,6,0,1,0,4,1,224 };
    static const uint8_t TEMPO[] = { 0,255,81,3,10,230,45,0,255,88,4,4,2,24,8,0,255,47,0 };
    bool ok = fwrite(HEADER, 1, sizeof HEADER, file) == sizeof HEADER;
    ok = ok && fwrite("MTrk", 1, 4, file) == 4 && be32(file, sizeof TEMPO)
       && fwrite(TEMPO, 1, sizeof TEMPO, file) == sizeof TEMPO;
    for (unsigned p = 0; p < PARTS && ok; ++p) {
        uint8_t bytes[EVENTS * 8 + 64] = { 0 };
        size_t n = strlen(NAMES[p]), used = 0;
        bytes[used++] = 0; bytes[used++] = 255; bytes[used++] = 3; bytes[used++] = (uint8_t)n;
        memcpy(bytes + used, NAMES[p], n); used += n;
        uint32_t previous = 0;
        for (size_t i = 0; i < s->count; ++i) if (s->events[i].part == p) {
            event e = s->events[i]; used += vlq(bytes + used, e.tick - previous);
            bytes[used++] = (uint8_t)((e.on ? 0x90 : 0x80) | p);
            bytes[used++] = e.note; bytes[used++] = e.velocity; previous = e.tick;
        }
        bytes[used++] = 0; bytes[used++] = 255; bytes[used++] = 47; bytes[used++] = 0;
        ok = fwrite("MTrk", 1, 4, file) == 4 && be32(file, (uint32_t)used)
          && fwrite(bytes, 1, used, file) == used;
    }
    if (fclose(file)) ok = false;
    if (!ok) (void)remove(path);
    return ok;
}

static tw_stereo room_tick(room *r, tw_stereo x) {
    static const unsigned LENGTH[] = { 1423, 1601, 1789, 1949 };
    float out[2] = { 0 };
    for (unsigned c = 0; c < 2; ++c) {
        float input = c ? x.r : x.l;
        r->input[c] += .065f * (input - r->input[c]);
        for (unsigned d = 0; d < COMBS; ++d) {
            unsigned at = r->position[c][d]; float delayed = r->line[c][d][at];
            r->low[c][d] += .20f * (delayed - r->low[c][d]);
            r->line[c][d][at] = r->input[c] + .89f * r->low[c][d];
            r->position[c][d] = (at + 1) % (LENGTH[d] + 19 * c);
            out[c] += .25f * delayed;
        }
        for (unsigned d = 0; d < 2; ++d) {
            unsigned at = r->diffuse_position[c][d];
            float delayed = r->diffuse[c][d][at];
            float y = delayed - .5f * out[c];
            r->diffuse[c][d][at] = out[c] + .5f * y;
            r->diffuse_position[c][d] = (at + 1) % ((d ? 556 : 225) + 23 * c);
            out[c] = y;
        }
        r->output[c] += .07f * (out[c] - r->output[c]);
    }
    return (tw_stereo){ r->output[0], r->output[1] };
}

static bool render(const score *s, const char *prefix, double seconds) {
    tw_instrument manual[2] = { 0 };
    static const uint8_t REG[2][9] = { { 4,0,8,3,0,0,0,0,0 }, { 3,0,7,2,0,0,0,0,0 } };
    for (unsigned p = 0; p < 2; ++p) {
        tw_instrument_init(&manual[p], RATE);
        tw_organ_set_registration(&manual[p].organ, REG[p]);
        tw_organ_set_wear(&manual[p].organ, 0);
        tw_organ_set_percussion(&manual[p].organ, false, false, false, true);
        tw_instrument_set_drive(&manual[p], .08f);
        tw_rotary_set_mode(&manual[p].rotary, TW_ROT_CHORALE);
        tw_rotary_set_width(&manual[p].rotary, p == RIGHT ? .65f : .45f);
        tw_rotary_set_balance(&manual[p].rotary, .35f);
    }
    /* Manual foldback excludes wheels 1..12. Pedals use the same tonewheel
     * generator directly, with 16' fundamental and a quiet 8' octave. */
    tw_generator pedal = { 0 }; tw_generator_init(&pedal, RATE, .008f);
    tw_generator_set_wear(&pedal, 0);
    tw_drive pedal_drive = { 0 }; tw_drive_init(&pedal_drive, RATE);
    tw_drive_set(&pedal_drive, .07f);
    room reverb[PARTS] = { 0 }; tw_stereo low[PARTS] = { 0 };
    wav_f32_writer writer[OUTPUTS] = { 0 }; meter meters[OUTPUTS] = { 0 };
    char path[OUTPUTS][1024] = { 0 }, temp[OUTPUTS][1040] = { 0 };
    float block[OUTPUTS][BLOCK * 2] = { 0 };
    size_t frames = (size_t)(seconds * RATE + .5), next = 0, used = 0;
    bool ok = frames > 0;
    for (unsigned p = 0; p < OUTPUTS && ok; ++p) {
        int n = snprintf(path[p], sizeof path[p], "%s_%s.wav", prefix, NAMES[p]);
        ok = n > 0 && (size_t)n < sizeof path[p]; if (!ok) break;
        memcpy(temp[p], path[p], (size_t)n); memcpy(temp[p] + n, ".tmp", sizeof ".tmp");
        ok = wav_f32_open(&writer[p], temp[p], frames, RATE, 2) == 0;
        meters[p].hash = UINT64_C(14695981039346656037);
    }
    for (size_t frame = 0; frame < frames && ok; ++frame) {
        while (next < s->count && ((uint64_t)s->events[next].tick * 714285u + 5000u) / 10000u <= frame) {
            event e = s->events[next++];
            if (e.part < PEDALS) tw_organ_note(&manual[e.part].organ, e.note, e.on, e.velocity);
            else {
                float targets[TW_WHEELS] = { 0 };
                if (e.on) { targets[e.note - 24] = 1.0f; targets[e.note - 12] = .22f; }
                tw_generator_set_keyed_targets(&pedal, targets);
            }
        }
        if (frame % 480 == 0) {
            double bar = frame / (RATE * 4 * .714285);
            /* A single expression pedal can shape both manuals. */
            static const float SWELL[][2] = {
                {0,.66f}, {8,.70f}, {24,.78f}, {32,.88f}, {39,.80f},
                {42,.58f}, {48,.65f}, {64,.80f}, {68,.94f}, {79,.90f},
                {84,.60f}, {85,.50f},
            };
            size_t i = 0;
            while (i + 1 < sizeof SWELL / sizeof *SWELL && bar > SWELL[i+1][0]) i++;
            float swell = SWELL[i][1];
            if (i + 1 < sizeof SWELL / sizeof *SWELL) {
                float u = (float)(bar - SWELL[i][0]) / (SWELL[i+1][0] - SWELL[i][0]);
                u = u * u * (3 - 2 * u);
                swell += u * (SWELL[i+1][1] - swell);
            }
            for (unsigned p = 0; p < 2; ++p) tw_organ_set_swell(&manual[p].organ, swell);
        }
        float bass = tw_drive_tick(&pedal_drive, tw_generator_tick(&pedal).keyed) * .012f;
        tw_stereo dry[PARTS] = { tw_instrument_tick_stereo(&manual[0]),
                                tw_instrument_tick_stereo(&manual[1]), { bass, bass } };
        tw_stereo out[OUTPUTS] = { 0 };
        float fade = frame > 250u * RATE ? (float)(253u * RATE - frame) / (3 * RATE) : 1;
        for (unsigned p = 0; p < PARTS; ++p) {
            float gain = p == RIGHT ? .018f : p == LEFT ? .012f : 1;
            low[p].l += .085f * (gain * dry[p].l - low[p].l);
            low[p].r += .085f * (gain * dry[p].r - low[p].r);
            tw_stereo wet = room_tick(&reverb[p], low[p]);
            float send = p == PEDALS ? .13f : .62f;
            out[p] = (tw_stereo){ fade * (.70f * low[p].l + send * wet.l),
                                  fade * (.70f * low[p].r + send * wet.r) };
            out[MIX].l += out[p].l; out[MIX].r += out[p].r;
        }
        for (unsigned p = 0; p < OUTPUTS; ++p) {
            if (!isfinite(out[p].l) || !isfinite(out[p].r)) { ok = false; break; }
            float peak = fmaxf(fabsf(out[p].l), fabsf(out[p].r));
            if (peak >= 1) { ok = false; break; }
            if (peak > meters[p].peak) meters[p].peak = peak;
            meters[p].square += (double)out[p].l * out[p].l + (double)out[p].r * out[p].r;
            block[p][2 * used] = out[p].l; block[p][2 * used + 1] = out[p].r;
            const uint8_t *bytes = (const uint8_t *)&block[p][2 * used];
            for (unsigned b = 0; b < 2 * sizeof(float); ++b) {
                meters[p].hash ^= bytes[b]; meters[p].hash *= UINT64_C(1099511628211);
            }
        }
        if (++used == BLOCK || frame + 1 == frames) {
            for (unsigned p = 0; p < OUTPUTS && ok; ++p) ok = wav_f32_write(&writer[p], block[p], used) == 0;
            used = 0;
        }
        if (frame % (10 * RATE) == 0) fprintf(stderr, "rendered %.0f / %.1f s\n", frame / (double)RATE, seconds);
    }
    for (unsigned p = 0; p < OUTPUTS; ++p) {
        if (ok) ok = wav_f32_close(&writer[p]) == 0; else wav_f32_abort(&writer[p]);
    }
    for (unsigned p = 0; p < OUTPUTS; ++p) {
        if (ok && rename(temp[p], path[p])) ok = false;
        if (!ok && *temp[p]) (void)remove(temp[p]);
        printf("%s: peak %.8f RMS %.8f FNV64 %016llx\n", NAMES[p], (double)meters[p].peak,
               frames ? sqrt(meters[p].square / (2.0 * frames)) : 0,
               (unsigned long long)meters[p].hash);
    }
    return ok && meters[MIX].peak > 0;
}

int main(int argc, char **argv) {
    const char *input = "notes-midi/local/nine-inch-nails-hurt.mid";
    const char *vocal = "notes-midi/local/hurt-songparts-vocals.mid";
    const char *prefix = "build/organ_hurt";
    bool check_only = false, score_only = false; double seconds = SECONDS;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i+1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-v") && i+1 < argc) vocal = argv[++i];
        else if (!strcmp(argv[i], "-o") && i+1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-d") && i+1 < argc) {
            if (!host_parse_double(argv[++i], .01, SECONDS, &seconds)) return 2;
        } else if (!strcmp(argv[i], "--check")) check_only = true;
        else if (!strcmp(argv[i], "--score-only")) score_only = true;
        else {
            fprintf(stderr, "usage: %s [-i backing.mid] [-v vocal.mid] [-o prefix] [-d seconds] [--check|--score-only]\n", argv[0]);
            return !strcmp(argv[i], "-h") ? 0 : 2;
        }
    }
    smf_file backing = { 0 }, melody = { 0 }; score s = { 0 };
    bool ok = load_midi(input, UINT16_MAX, &backing) && load_midi(vocal, 1, &melody)
           && arrange(&s, &backing, &melody) && check(&s);
    if (ok && !check_only) {
        char path[1024] = { 0 }; int n = snprintf(path, sizeof path, "%s_score.mid", prefix);
        ok = n > 0 && (size_t)n < sizeof path && write_score(&s, path);
        if (ok && !score_only) ok = render(&s, prefix, seconds);
    }
    smf_dispose(&backing); smf_dispose(&melody);
    if (!ok) fprintf(stderr, "Hurt organ: input, performance or output validation failed\n");
    return ok ? 0 : 1;
}
