#define _POSIX_C_SOURCE 200809L
#include "wb_analysis.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../driver/wav.h"
#include "wb_format.h"
#include "wb_hash.h"

[[gnu::format(printf, 2, 3)]]
static bool fail(wb_error *error, const char *format, ...) {
    if (error) {
        error->line = 0;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return false;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool wb_audio_load(const char *path, wb_audio *a, wb_error *error) {
    *a = (wb_audio){ 0 };
    char *bytes;
    size_t size;
    if (!wb_read_file(path, &bytes, &size)) return fail(error, "cannot read %s", path);
    const uint8_t *d = (const uint8_t *)bytes;
    bool ok = size >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4);
    unsigned format = 0, bits = 0;
    const uint8_t *data = nullptr;
    size_t data_size = 0;
    for (size_t at = 12; ok && at + 8 <= size;) {
        uint32_t length = le32(d + at + 4);
        if (length > size - at - 8) { ok = false; break; }
        if (!memcmp(d + at, "fmt ", 4) && length >= 16) {
            format = d[at + 8] | d[at + 9] << 8;
            a->channels = d[at + 10] | d[at + 11] << 8;
            a->rate = le32(d + at + 12);
            bits = d[at + 22] | d[at + 23] << 8;
        } else if (!memcmp(d + at, "data", 4)) {
            data = d + at + 8;
            data_size = length;
        }
        at += 8 + length + (length & 1u);
    }
    ok = ok && data && format == 3 && bits == 32 && a->channels == 2;
    if (ok) {
        a->frames = data_size / 8;
        a->sample = malloc(a->frames ? a->frames * 8 : 1);
        ok = a->sample != nullptr;
        if (ok) memcpy(a->sample, data, a->frames * 8);
    }
    free(bytes);
    if (!ok) {
        wb_audio_free(a);
        return fail(error, "%s: not a stereo float32 WAV", path);
    }
    return true;
}

void wb_audio_free(wb_audio *a) {
    free(a->sample);
    *a = (wb_audio){ 0 };
}

static double db(double x) { return x > 0 ? 20.0 * log10(x) : -INFINITY; }

void wb_measure_audio(const float *x, uint64_t frames, unsigned rate, wb_measure *m) {
    *m = (wb_measure){ .frames = frames };
    double square = 0, mono = 0, cross = 0, ls = 0, rs = 0;
    for (uint64_t i = 0; i < frames; i++) {
        double l = x[2 * i], r = x[2 * i + 1];
        double a = fabs(l) > fabs(r) ? fabs(l) : fabs(r);
        if (a > m->peak) m->peak = a;
        square += l * l + r * r;
        double c = (l + r) / 2;
        mono += c * c;
        cross += l * r;
        ls += l * l;
        rs += r * r;
    }
    uint64_t tail = rate / 10;
    for (uint64_t i = frames > tail ? frames - tail : 0; i < frames; i++) {
        double a = fmax(fabs(x[2 * i]), fabs(x[2 * i + 1]));
        if (a > m->last_100ms_peak) m->last_100ms_peak = a;
    }
    if (frames) {
        m->rms = sqrt(square / (2.0 * frames));
        m->mono_rms = sqrt(mono / frames);
    }
    m->correlation = ls * rs > 0 ? cross / sqrt(ls * rs) : 0;
    m->peak_dbfs = db(m->peak);
    m->rms_dbfs = db(m->rms);
    m->crest_db = m->peak_dbfs - m->rms_dbfs;
    m->mono_loss_db = m->rms > 0 ? db(m->mono_rms / m->rms) : 0;
}

static float target_peak(void) {
    return nextafterf((float)pow(10.0, -3.0 / 20.0), 0.0f);
}

static bool write_scaled(const wb_audio *a, const char *out, float gain) {
    float *y = malloc(a->frames ? a->frames * 8 : 8);
    if (!y) return false;
    bool ok = true;
    for (uint64_t i = 0; i < 2 * a->frames && ok; i++) {
        y[i] = a->sample[i] * gain;
        ok = isfinite(y[i]) && fabsf(y[i]) < 1;
    }
    ok = ok && wav_write_f32(out, y, a->frames, a->rate, 2) == 0;
    free(y);
    return ok;
}

bool wb_listening_copy(const wb_audio *mix, const char *out, double *gain, char sha256[65],
                       wb_error *error) {
    wb_measure m;
    wb_measure_audio(mix->sample, mix->frames, mix->rate, &m);
    if (!(m.peak > 0)) return fail(error, "silent mix: no listening copy");
    *gain = (double)target_peak() / m.peak;
    if (!write_scaled(mix, out, (float)*gain)) return fail(error, "cannot write %s", out);
    if (!wb_sha256_file_hex(out, sha256)) return fail(error, "cannot hash %s", out);
    return true;
}

bool wb_ab_gains(const wb_audio *a, const wb_audio *b, double *ga, double *gb) {
    wb_measure ma, mb;
    wb_measure_audio(a->sample, a->frames, a->rate, &ma);
    wb_measure_audio(b->sample, b->frames, b->rate, &mb);
    if (!(ma.peak > 0 && mb.peak > 0 && ma.rms > 0 && mb.rms > 0)) return false;
    double t = target_peak();
    double common = fmin(ma.rms * t / ma.peak, mb.rms * t / mb.peak);
    *ga = common / ma.rms;
    *gb = common / mb.rms;
    return true;
}

size_t wb_loudness_lane(const wb_audio *a, uint64_t window, float *out, size_t max) {
    size_t n = 0;
    for (uint64_t at = 0; at < a->frames && n < max; at += window, n++) {
        uint64_t end = at + window < a->frames ? at + window : a->frames;
        double square = 0;
        for (uint64_t i = at; i < end; i++)
            square += (double)a->sample[2 * i] * a->sample[2 * i]
                    + (double)a->sample[2 * i + 1] * a->sample[2 * i + 1];
        double rms = sqrt(square / (2.0 * (double)(end - at)));
        out[n] = (float)db(rms);
    }
    return n;
}
