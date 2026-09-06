/* Hosted MA2 reinterpretation of the Vangelis Blade Runner Main Titles MIDI. */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mamutanalog.h"
#include "host_parse.h"
#include "smf.h"
#include "wav.h"

enum { RATE = 48000, TAIL_FRAMES = RATE * 10, BLOCK = 256, BANK_COUNT = 4 };
static const char DEFAULT_INPUT[] = "/home/dev/Downloads/VANGELIS.Blade runner.MID";
static const char DEFAULT_OUTPUT[] = "build/ma_blade_runner_main_titles.wav";

typedef struct { ma_card_bank bank[BANK_COUNT]; unsigned notes, steals, panics; } show;

/* MIDI channels in this file are one-based track roles from the source. */
static unsigned route(uint8_t channel) {
    switch (channel) {
    case 1: case 2: case 3: case 9: return 1; /* bass / low synth */
    case 4: case 5: case 6: return 2;         /* synth brass / motif */
    case 7: case 8: case 10: case 11: case 12: return 0; /* strings/pads */
    case 13: case 14: case 15: return 3;      /* harp, bells, timpani */
    default: return 0;
    }
}

static ma_patch patch_for(unsigned id) {
    ma_patch p = id == 0 ? ma_patch_tepih : id == 1 ? ma_patch_dubina
                         : id == 2 ? ma_patch_lead : ma_patch_tepih;
    if (id == 0) {
        p.vco1.saw_level = .30f; p.vco1.triangle_level = .34f; p.vco1.sine_level = .42f;
        p.vco2.saw_level = .12f; p.vco2.triangle_level = .44f; p.vco2.sine_level = .34f;
        p.vco2_level = .58f; p.filter_cutoff_hz = 760.0f; p.filter_resonance = .13f;
        p.amp_adsr = (ma_adsr){ 900.0f, 2200.0f, .78f, 6500.0f }; p.master_level = .14f;
    } else if (id == 1) {
        p.filter_cutoff_hz = 520.0f; p.filter_drive = .12f;
        p.amp_adsr = (ma_adsr){ 45.0f, 360.0f, .72f, 2800.0f }; p.master_level = .14f;
    } else if (id == 2) {
        p.filter_cutoff_hz = 1050.0f; p.filter_drive = .05f;
        p.amp_adsr = (ma_adsr){ 180.0f, 800.0f, .66f, 4600.0f }; p.master_level = .12f;
    } else {
        p.vco1.pulse_level = .03f; p.vco1.sine_level = .48f; p.vco1.saw_level = .24f;
        p.filter_cutoff_hz = 1400.0f; p.amp_adsr = (ma_adsr){ 8.0f, 180.0f, .45f, 1700.0f };
        p.master_level = .08f;
    }
    return p;
}

static void init_show(show *s) {
    *s = (show){ 0 };
    for (unsigned i = 0; i < BANK_COUNT; i++) {
        ma_patch p = patch_for(i);
        ma_card_bank_init_patch(&s->bank[i], RATE, &p);
    }
}

static void apply_event(show *s, const smf_event *e) {
    unsigned id = route(e->status & 15u); ma_card_bank *b = &s->bank[id];
    uint8_t type = e->status & 0xf0u, ch = e->status & 15u;
    if (type == 0x90u && e->d2) {
        bool full = true;
        for (unsigned i = 0; i < MA_CARD_COUNT; i++) full &= b->owner[i].phase != MA_CARD_IDLE;
        uint8_t slot = ma_card_bank_note_on(b, ch, e->d1, e->d2);
        if (slot != MA_CARD_NONE) { s->notes++; s->steals += full; }
    } else if (type == 0x80u || (type == 0x90u && !e->d2)) {
        (void)ma_card_bank_note_off(b, ch, e->d1, e->d2);
    } else if (type == 0xb0u && e->d1 == 64u) {
        ma_card_bank_set_sustain(b, e->d2 >= 64u);
    } else if (type == 0xb0u && (e->d1 == 120u || e->d1 == 123u)) {
        ma_card_bank_panic(b); s->panics++;
    } else if (type == 0xd0u || type == 0xe0u) {
        float value = type == 0xd0u ? e->d1 / 127.0f
                    : ((int)e->d1 + 128 * (int)e->d2 - 8192) / 8192.0f;
        for (unsigned i = 0; i < MA_CARD_COUNT; i++) {
            if (type == 0xd0u) ma_synth_set_channel_pressure(&b->card[i], value);
            else ma_synth_set_pitch_bend(&b->card[i], 2.0f * value);
        }
    } else if (type == 0xa0u) {
        for (unsigned i = 0; i < MA_CARD_COUNT; i++)
            if (b->owner[i].phase != MA_CARD_IDLE && b->owner[i].channel == ch
                && b->owner[i].note == e->d1)
                ma_synth_set_poly_pressure(&b->card[i], ch, e->d1, e->d2 / 127.0f);
    }
}

static bool event_frames(const smf_file *m, size_t **out, size_t *frames) {
    size_t *f = m->event_count ? malloc(m->event_count * sizeof *f) : NULL;
    if (m->event_count && !f) return false;
    double seconds = 0.0; uint64_t tick0 = 0; uint32_t us = 500000; size_t tempo = 0; size_t last = 0;
    for (size_t i = 0; i < m->event_count; i++) {
        while (tempo < m->tempo_count && m->tempos[tempo].tick <= m->events[i].tick) {
            seconds += (double)(m->tempos[tempo].tick - tick0) * us * 1e-6 / m->division;
            tick0 = m->tempos[tempo].tick; us = m->tempos[tempo].us_per_quarter; tempo++;
        }
        double at = seconds + (double)(m->events[i].tick - tick0) * us * 1e-6 / m->division;
        if (!isfinite(at) || at < 0 || at > 7200) { free(f); return false; }
        f[i] = (size_t)(at * RATE + .5); if (f[i] > last) last = f[i];
    }
    if (last > SIZE_MAX - TAIL_FRAMES) { free(f); return false; }
    *out = f; *frames = last + TAIL_FRAMES; return true;
}

static ma_frame tick(show *s) {
    static const float gl[BANK_COUNT] = { .30f, .38f, .30f, .18f };
    static const float gr[BANK_COUNT] = { .34f, .26f, .38f, .22f };
    ma_frame out = { 0 };
    for (unsigned id = 0; id < BANK_COUNT; id++) {
        ma_frame card[MA_CARD_COUNT]; ma_card_bank_tick(&s->bank[id], card);
        for (unsigned i = 0; i < MA_CARD_COUNT; i++) {
            float mono = .5f * (card[i].left + card[i].right);
            out.left += gl[id] * mono; out.right += gr[id] * mono;
        }
    }
    out.left *= 1.35f; out.right *= 1.35f;
    return out;
}

static bool render(const smf_file *m, const size_t *ef, size_t frames,
                   const char *path, unsigned pass, uint64_t *hash, unsigned *notes,
                   unsigned *steals, unsigned *panics) {
    show s; init_show(&s); wav_f32_writer w = { 0 };
    if (wav_f32_open(&w, path, frames, RATE, 2) < 0) return false;
    float block[2 * BLOCK]; size_t next = 0; uint64_t h = 0;
    unsigned reported = 0;
    fprintf(stderr, "render pass %u/2:   0%%", pass);
    fflush(stderr);
    for (size_t first = 0; first < frames; first += BLOCK) {
        size_t n = frames - first; if (n > BLOCK) n = BLOCK;
        for (size_t i = 0; i < n; i++) {
            size_t at = first + i; while (next < m->event_count && ef[next] <= at) apply_event(&s, &m->events[next++]);
            ma_frame y = tick(&s); if (!isfinite(y.left) || !isfinite(y.right)) y = (ma_frame){ 0 };
            block[2*i] = y.left; block[2*i+1] = y.right;
        }
        h = tw_fnv1a64(block, 2 * n * sizeof *block, h);
        if (wav_f32_write(&w, block, n) < 0) { wav_f32_abort(&w); return false; }
        unsigned percent = (unsigned)(((first + n) * 100u) / frames);
        unsigned milestone = percent / 5u * 5u;
        if (milestone > 100u) milestone = 100u;
        if (milestone > reported) {
            reported = milestone;
            fprintf(stderr, "\rrender pass %u/2: %3u%%", pass, reported);
            fflush(stderr);
        }
    }
    fputc('\n', stderr);
    if (wav_f32_close(&w) < 0) return false;
    *hash = h; *notes = s.notes; *steals = s.steals; *panics = s.panics; return true;
}

int main(int argc, char **argv) {
    const char *input = DEFAULT_INPUT, *output = DEFAULT_OUTPUT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else { fprintf(stderr, "usage: %s [-i input.mid] [-o output.wav]\n", argv[0]); return 2; }
    }
    FILE *file = fopen(input, "rb"); if (!file) { fprintf(stderr, "%s: %s\n", input, strerror(errno)); return 1; }
    if (fseek(file, 0, SEEK_END) < 0) { fclose(file); return 1; }
    long length = ftell(file); if (length < 0 || fseek(file, 0, SEEK_SET) < 0) { fclose(file); return 1; }
    uint8_t *data = malloc((size_t)length); bool read_ok = data && fread(data, 1, (size_t)length, file) == (size_t)length; fclose(file);
    if (!read_ok) { free(data); fprintf(stderr, "could not read MIDI\n"); return 1; }
    smf_file midi = { 0 }; smf_error error = { 0 };
    if (!smf_parse(data, (size_t)length, UINT16_MAX, &midi, &error)) { fprintf(stderr, "MIDI error at %zu: %s\n", error.offset, error.message); free(data); return 1; }
    size_t *ef = NULL, frames = 0; bool ok = event_frames(&midi, &ef, &frames); uint64_t h1 = 0, h2 = 0; unsigned n1=0,s1=0,p1=0,n2=0,s2=0,p2=0;
    if (ok) ok = render(&midi, ef, frames, "/tmp/ma-main-titles-first.wav", 1, &h1, &n1, &s1, &p1);
    if (ok) ok = render(&midi, ef, frames, output, 2, &h2, &n2, &s2, &p2) && h1 == h2 && n1 == n2;
    printf("Mamut Analog — Blade Runner Main Titles\n  %.3f s + 10 s tail, %u notes, %u steals, %u panics\n  FNV64 %016llx %s\n  wav: %s%s\n", frames/(double)RATE-10.0, n2, s2, p2, (unsigned long long)h2, ok ? "two runs identical" : "FAILED", output, ok ? "" : " (FAILED)");
    free(ef); smf_dispose(&midi); free(data); remove("/tmp/ma-main-titles-first.wav"); return ok ? 0 : 1;
}
