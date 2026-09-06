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

enum { RATE = 48000, TAIL_FRAMES = RATE * 10, BLOCK = 4096, BANK_COUNT = 4 };
static const char DEFAULT_INPUT[] = "/home/dev/Downloads/VANGELIS.Blade runner.MID";
static const char DEFAULT_OUTPUT[] =
    "build/ma_blade_runner_main_titles_ma2_full.wav";
static constexpr float MASTER_GAIN = 8.52f;

typedef struct { ma_card_bank bank[BANK_COUNT]; unsigned notes, steals, panics; } show;

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned notes;
    unsigned steals;
    unsigned panics;
    unsigned nonfinite;
    unsigned clipped;
} render_metrics;

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
    ma_patch p = id == 0 ? ma_patch_prizma : id == 1 ? ma_patch_granica
                         : id == 2 ? ma_patch_prizma : ma_patch_raster;
    if (id == 0) {
        p.raster_mix = .24f; p.raster_position = .28f; p.raster_warp = .08f;
        p.bcs_amount = .08f; p.bcs_regime = .16f;
        p.vco1.saw_level = .30f; p.vco1.triangle_level = .34f; p.vco1.sine_level = .42f;
        p.vco2.saw_level = .12f; p.vco2.triangle_level = .44f; p.vco2.sine_level = .34f;
        p.vco2_level = .58f; p.filter_cutoff_hz = 760.0f; p.filter_resonance = .13f;
        p.amp_adsr = (ma_adsr){ 900.0f, 2200.0f, .78f, 6500.0f }; p.master_level = .14f;
    } else if (id == 1) {
        p.raster_mix = .14f; p.raster_position = .16f; p.raster_warp = .06f;
        p.bcs_amount = .64f; p.bcs_regime = .56f;
        p.filter_cutoff_hz = 520.0f; p.filter_drive = .12f;
        p.amp_adsr = (ma_adsr){ 45.0f, 360.0f, .72f, 2800.0f }; p.master_level = .14f;
    } else if (id == 2) {
        p.raster_mix = .36f; p.raster_position = .42f; p.raster_warp = .16f;
        p.bcs_amount = .26f; p.bcs_regime = .38f;
        p.filter_cutoff_hz = 1050.0f; p.filter_drive = .05f;
        p.amp_adsr = (ma_adsr){ 180.0f, 800.0f, .66f, 4600.0f }; p.master_level = .12f;
    } else {
        p.raster_mix = .72f; p.raster_position = .66f; p.raster_warp = .28f;
        p.bcs_amount = .16f; p.bcs_regime = .46f;
        p.vco1.pulse_level = .03f; p.vco1.sine_level = .48f; p.vco1.saw_level = .24f;
        p.filter_cutoff_hz = 1400.0f; p.amp_adsr = (ma_adsr){ 8.0f, 180.0f, .45f, 1700.0f };
        p.master_level = .08f;
    }
    return p;
}

static void init_show(show *s) {
    static const float ACCENT_DETUNE[MA_CARD_COUNT] = {
        -.08f, -.04f, 0.0f, .04f, .08f,
    };
    *s = (show){ 0 };
    for (unsigned i = 0; i < BANK_COUNT; i++) {
        ma_patch p = patch_for(i);
        ma_card_bank_init_patch(&s->bank[i], RATE, &p);
        for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
            ma_synth_set_lfo(&s->bank[i].card[slot],
                             i == 2 ? .014f : .008f,
                             i == 2 ? .17f : .07f);
            if (i == 2)
                ma_synth_set_glide(&s->bank[i].card[slot], true, .09f);
            if (i == 3)
                ma_synth_set_pitch_bend(&s->bank[i].card[slot],
                                         ACCENT_DETUNE[slot]);
        }
        if (i == 3) ma_card_bank_set_unison(&s->bank[i], true);
    }
}

static void update_spectral_arc(show *s, size_t frame) {
    if (frame % (RATE / 20u)) return;
    float seconds = frame / (float)RATE;
    float slow = .5f + .5f * tw_sin_turns(seconds / 48.0f);
    float counter = .5f + .5f * tw_sin_turns(seconds / 63.0f + .25f);
    for (unsigned slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_set_raster(&s->bank[0].card[slot], .24f,
                            .20f + .18f * slow, .06f + .08f * counter);
        ma_synth_set_raster(&s->bank[2].card[slot], .36f,
                            .34f + .16f * counter, .12f + .10f * slow);
        ma_synth_set_bcs(&s->bank[1].card[slot], .64f,
                         .44f + .20f * slow);
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

static ma_frame tick(show *s, size_t frame) {
    static const float gl[BANK_COUNT] = { .30f, .38f, .30f, .18f };
    static const float gr[BANK_COUNT] = { .34f, .26f, .38f, .22f };
    ma_frame out = { 0 };
    update_spectral_arc(s, frame);
    for (unsigned id = 0; id < BANK_COUNT; id++) {
        ma_frame card[MA_CARD_COUNT]; ma_card_bank_tick(&s->bank[id], card);
        for (unsigned i = 0; i < MA_CARD_COUNT; i++) {
            float mono = .5f * (card[i].left + card[i].right);
            out.left += gl[id] * mono; out.right += gr[id] * mono;
        }
    }
    out.left *= MASTER_GAIN; out.right *= MASTER_GAIN;
    return out;
}

static bool render(const smf_file *m, const size_t *ef, size_t frames,
                   wav_f32_writer *writer, render_metrics *metrics) {
    show s;
    float block[2 * BLOCK];
    size_t next = 0;
    unsigned reported = 0;
    init_show(&s);
    *metrics = (render_metrics){ 0 };
    fputs("  render   0%", stderr);
    fflush(stderr);
    for (size_t first = 0; first < frames; first += BLOCK) {
        size_t n = frames - first; if (n > BLOCK) n = BLOCK;
        for (size_t i = 0; i < n; i++) {
            size_t at = first + i; while (next < m->event_count && ef[next] <= at) apply_event(&s, &m->events[next++]);
            ma_frame y = tick(&s, at);
            if (!isfinite(y.left) || !isfinite(y.right)) {
                metrics->nonfinite++;
                y = (ma_frame){ 0 };
            }
            float left = fabsf(y.left), right = fabsf(y.right);
            float peak = left > right ? left : right;
            if (peak > metrics->peak) metrics->peak = peak;
            if (peak > 1.0f) metrics->clipped++;
            metrics->sum_squares += (double)y.left * y.left
                                  + (double)y.right * y.right;
            block[2*i] = y.left; block[2*i+1] = y.right;
        }
        metrics->hash = tw_fnv1a64(block, 2 * n * sizeof *block,
                                   metrics->hash);
        if (wav_f32_write(writer, block, n) < 0) return false;
        unsigned percent = (unsigned)(((first + n) * 100u) / frames);
        unsigned milestone = percent / 5u * 5u;
        if (milestone > 100u) milestone = 100u;
        if (milestone > reported) {
            reported = milestone;
            fprintf(stderr, "\rrender %3u%%", reported);
            fflush(stderr);
        }
    }
    fputc('\n', stderr);
    metrics->notes = s.notes;
    metrics->steals = s.steals;
    metrics->panics = s.panics;
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak > 1e-5f;
}

static bool render_file(const smf_file *m, const size_t *ef, size_t frames,
                        const char *output, render_metrics *metrics) {
    size_t output_size = strlen(output);
    if (output_size > SIZE_MAX - sizeof ".ma-tmp") return false;
    char *temporary = malloc(output_size + sizeof ".ma-tmp");
    if (!temporary) return false;
    memcpy(temporary, output, output_size);
    memcpy(temporary + output_size, ".ma-tmp", sizeof ".ma-tmp");

    wav_f32_writer writer = { 0 };
    bool opened = wav_f32_open(&writer, temporary, frames, RATE, 2) == 0;
    bool rendered = opened && render(m, ef, frames, &writer, metrics);
    bool closed = rendered && wav_f32_close(&writer) == 0;
    if (!closed) {
        wav_f32_abort(&writer);
        (void)remove(temporary);
    }
    bool published = closed && rename(temporary, output) == 0;
    if (closed && !published) (void)remove(temporary);
    free(temporary);
    return published;
}

int main(int argc, char **argv) {
    const char *input = DEFAULT_INPUT, *output = DEFAULT_OUTPUT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "-h")) { printf("usage: %s [-i input.mid] [-o output.wav]\n", argv[0]); return 0; }
        else { fprintf(stderr, "usage: %s [-i input.mid] [-o output.wav]\n", argv[0]); return 2; }
    }
    FILE *file = fopen(input, "rb"); if (!file) { fprintf(stderr, "%s: %s\n", input, strerror(errno)); return 1; }
    if (fseek(file, 0, SEEK_END) < 0) { fclose(file); return 1; }
    long length = ftell(file); if (length < 0 || fseek(file, 0, SEEK_SET) < 0) { fclose(file); return 1; }
    uint8_t *data = malloc((size_t)length); bool read_ok = data && fread(data, 1, (size_t)length, file) == (size_t)length; fclose(file);
    if (!read_ok) { free(data); fprintf(stderr, "could not read MIDI\n"); return 1; }
    smf_file midi = { 0 }; smf_error error = { 0 };
    if (!smf_parse(data, (size_t)length, UINT16_MAX, &midi, &error)) { fprintf(stderr, "MIDI error at %zu: %s\n", error.offset, error.message); free(data); return 1; }
    size_t *ef = NULL, frames = 0;
    render_metrics metrics = { 0 };
    bool ok = event_frames(&midi, &ef, &frames);
    if (ok) ok = render_file(&midi, ef, frames, output, &metrics);
    double rms = ok ? sqrt(metrics.sum_squares / (2.0 * frames)) : 0.0;
    printf("Mamut Analog — Blade Runner Main Titles, MA2 full spectrum\n  %.3f s + 10 s tail, %u notes, %u steals, %u panics\n  Granica bass, evolving Prizma/Raster orchestra; master gain %.2f\n  peak %.6f, RMS %.6f, finite %s, headroom %s\n  FNV64 %016llx (single streaming pass)\n  wav: %s%s\n", frames/(double)RATE-10.0, metrics.notes, metrics.steals, metrics.panics, (double)MASTER_GAIN, (double)metrics.peak, rms, metrics.nonfinite == 0 ? "yes" : "NO", metrics.clipped == 0 ? "yes" : "NO", (unsigned long long)metrics.hash, output, ok ? "" : " (FAILED)");
    free(ef); smf_dispose(&midi); free(data); return ok ? 0 : 1;
}
