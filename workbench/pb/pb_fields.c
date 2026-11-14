#include "pb_fields.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define L(n, m, label, init) { n, m, label, PB_UNIT_NONE, PB_SCALE_LINEAR, init }
static const pb_field FIELDS[] = {
    L("vco1.saw", PB_MOD_VCO1, "saw", 1), L("vco1.pulse", PB_MOD_VCO1, "pulse", 0),
    L("vco1.triangle", PB_MOD_VCO1, "tri", 0), L("vco1.sine", PB_MOD_VCO1, "sine", 0),
    L("vco1.pulse_width", PB_MOD_VCO1, "pw", .5),
    L("vco2.saw", PB_MOD_VCO2, "saw", 1), L("vco2.pulse", PB_MOD_VCO2, "pulse", 0),
    L("vco2.triangle", PB_MOD_VCO2, "tri", 0), L("vco2.sine", PB_MOD_VCO2, "sine", 0),
    L("vco2.pulse_width", PB_MOD_VCO2, "pw", .5), L("vco2.level", PB_MOD_VCO2, "level", 0),
    { "vco2.interval", PB_MOD_VCO2, "int", PB_UNIT_SEMITONES, PB_SCALE_STEPPED, 0 },
    { "vco2.fine_cents", PB_MOD_VCO2, "fine", PB_UNIT_CENTS, PB_SCALE_BIPOLAR, 0 },
    L("source.sync", PB_MOD_SYNC, "sync", 0), L("source.sync_softness", PB_MOD_SYNC, "soft", 0),
    L("source.crossmod", PB_MOD_SYNC, "xmod", 0),
    L("source.noise", PB_MOD_MIXER, "noise", 0),
    L("raster.mix", PB_MOD_RASTER, "mix", 0), L("raster.position", PB_MOD_RASTER, "pos", 0),
    L("raster.warp", PB_MOD_RASTER, "warp", 0),
    L("bcs.amount", PB_MOD_BCS, "amount", 0), L("bcs.regime", PB_MOD_BCS, "regime", 0),
    L("mozaik.mix", PB_MOD_MOZAIK, "mix", 0), L("mozaik.slope", PB_MOD_MOZAIK, "slope", .5),
    L("mozaik.contrast", PB_MOD_MOZAIK, "contr", .5), L("mozaik.phason", PB_MOD_MOZAIK, "phason", 0),
    L("mozaik.drift", PB_MOD_MOZAIK, "drift", 0),
    L("filter.mixer_pressure", PB_MOD_MIXER, "pressure", 0),
    { "filter.cutoff_hz", PB_MOD_FILTER, "cutoff", PB_UNIT_HZ, PB_SCALE_LOG, 8000 },
    L("filter.resonance", PB_MOD_FILTER, "res", 0), L("filter.drive", PB_MOD_FILTER, "drive", 0),
    L("filter.env_amount", PB_MOD_FILTER, "env", 0), L("filter.keytrack", PB_MOD_FILTER, "key", 0),
    { "amp.attack_ms", PB_MOD_AMP, "A", PB_UNIT_MS, PB_SCALE_LOG, 5 },
    { "amp.decay_ms", PB_MOD_AMP, "D", PB_UNIT_MS, PB_SCALE_LOG, 300 },
    L("amp.sustain", PB_MOD_AMP, "S", 1),
    { "amp.release_ms", PB_MOD_AMP, "R", PB_UNIT_MS, PB_SCALE_LOG, 300 },
    { "filter_env.attack_ms", PB_MOD_FILTER, "fA", PB_UNIT_MS, PB_SCALE_LOG, 5 },
    { "filter_env.decay_ms", PB_MOD_FILTER, "fD", PB_UNIT_MS, PB_SCALE_LOG, 300 },
    L("filter_env.sustain", PB_MOD_FILTER, "fS", 0),
    { "filter_env.release_ms", PB_MOD_FILTER, "fR", PB_UNIT_MS, PB_SCALE_LOG, 300 },
    L("macro.gravitacija", PB_MOD_MACROS, "Gravitacija", 0), L("macro.bloom", PB_MOD_MACROS, "Bloom", 0),
    L("macro.heat", PB_MOD_MACROS, "Heat", 0), L("macro.ruin", PB_MOD_MACROS, "Ruin", 0),
    L("macro.swarm", PB_MOD_MACROS, "Swarm", 0),
    L("output.body_drive", PB_MOD_OUTPUT, "body", 0), L("output.width", PB_MOD_OUTPUT, "width", .5),
    L("output.crossfeed", PB_MOD_OUTPUT, "xfeed", 0), L("output.master", PB_MOD_OUTPUT, "master", .18),
    L("glas.formant_mix", PB_MOD_GLAS, "mix", 0), L("glas.formant_vowel", PB_MOD_GLAS, "vowel", 0),
    { "glas.formant_shift", PB_MOD_GLAS, "shift", PB_UNIT_NONE, PB_SCALE_BIPOLAR, 0 },
    L("glas.formant_orbita", PB_MOD_GLAS, "orbita", 0), L("glas.grain_mix", PB_MOD_GLAS, "grain", 0),
    /* Circuit-derived oscillator pitch (docs/ma-vco-plan.md): both oscillators. */
    L("vco.analog", PB_MOD_SYNC, "analog", 0), L("vco.dead_time", PB_MOD_SYNC, "dead time", 0),
    /* The triangle core's shapers (docs/ma-vco-plan.md, V3), per oscillator. */
    L("vco1.tri_sine", PB_MOD_VCO1, "tri sine", 0), L("vco1.tri_saw", PB_MOD_VCO1, "tri saw", 0),
    L("vco2.tri_sine", PB_MOD_VCO2, "tri sine", 0), L("vco2.tri_saw", PB_MOD_VCO2, "tri saw", 0),
    /* The two cores' shared supply (V4). */
    L("vco.supply", PB_MOD_SYNC, "supply", 0),
};
#undef L

static const char *const MODULE_NAMES[PB_MOD_COUNT] = {
    "VCO1", "VCO2", "SYNC · XMOD · PITCH", "RASTER", "MOZAIK", "NOISE · MIXER · PRESSURE",
    "BCS", "FILTER", "GLAS", "AMP", "OUTPUT", "VOICE (bench)", "MACROS",
};

const char *pb_module_name(pb_module module) {
    return module < PB_MOD_COUNT ? MODULE_NAMES[module] : "?";
}

const pb_field *pb_field_find(const char *name) {
    for (size_t i = 0; name && i < sizeof FIELDS / sizeof *FIELDS; i++)
        if (!strcmp(FIELDS[i].name, name)) return &FIELDS[i];
    return nullptr;
}

bool pb_field_view_at(size_t index, pb_field_view *view) {
    if (!view || !ma_patch_field_info_at(index, &view->info)) return false;
    view->display = pb_field_find(view->info.name);
    return view->display != nullptr;
}

int pb_field_index(const char *name) {
    ma_patch_field_info info;
    for (size_t i = 0; name && ma_patch_field_info_at(i, &info); i++)
        if (!strcmp(info.name, name)) return (int)i;
    return -1;
}

double pb_field_clamp(const pb_field_view *v, double value) {
    if (!(value == value)) value = v->display->init;
    if (value < v->info.minimum) value = v->info.minimum;
    if (value > v->info.maximum) value = v->info.maximum;
    if (v->info.integer) value = round(value);
    return value;
}

double pb_field_to_unit(const pb_field_view *v, double value) {
    double lo = v->info.minimum, hi = v->info.maximum;
    value = pb_field_clamp(v, value);
    if (v->display->scale == PB_SCALE_LOG && lo > 0) return log(value / lo) / log(hi / lo);
    return hi > lo ? (value - lo) / (hi - lo) : 0;
}

double pb_field_from_unit(const pb_field_view *v, double unit) {
    double lo = v->info.minimum, hi = v->info.maximum;
    if (unit < 0) unit = 0;
    if (unit > 1) unit = 1;
    double value = v->display->scale == PB_SCALE_LOG && lo > 0 ? lo * pow(hi / lo, unit) : lo + unit * (hi - lo);
    return pb_field_clamp(v, value);
}

const char *pb_interval_name(int st) {
    static const char *const UP[] = { "unison", "minor second", "second", "minor third", "major third",
        "fourth", "tritone", "fifth", "minor sixth", "sixth", "minor seventh", "major seventh", "octave" };
    int a = st < 0 ? -st : st;
    if (a == 24) return st < 0 ? "two octaves down" : "two octaves";
    if (a == 12) return st < 0 ? "octave down" : "octave";
    if (a > 12) return st < 0 ? "below octave" : "above octave";
    return st < 0 ? "down" : UP[a];
}

void pb_field_format(const pb_field_view *v, double value, char *text, size_t size) {
    switch (v->display->unit) {
    case PB_UNIT_HZ:
        if (value >= 1000) snprintf(text, size, "%.2fk", value / 1000);
        else snprintf(text, size, "%.0f Hz", value);
        return;
    case PB_UNIT_MS:
        if (value >= 1000) snprintf(text, size, "%.2f s", value / 1000);
        else snprintf(text, size, "%.0f ms", value);
        return;
    case PB_UNIT_CENTS: snprintf(text, size, "%+.1f c", value); return;
    case PB_UNIT_SEMITONES: snprintf(text, size, "%+d st", (int)value); return;
    case PB_UNIT_NONE:
        if (v->display->scale == PB_SCALE_BIPOLAR) snprintf(text, size, "%+.3f", value);
        else snprintf(text, size, "%.3f", value);
        return;
    }
}

bool pb_field_parse(const pb_field_view *v, const char *text, double *value) {
    if (!text || !value) return false;
    char *end = nullptr;
    double x = strtod(text, &end);
    if (end == text || !isfinite(x)) return false;
    while (*end == ' ') end++;
    char suffix[8] = { 0 };
    size_t n = 0;
    while (*end && n < sizeof suffix - 1) suffix[n++] = (char)tolower((unsigned char)*end++);
    if (*end) return false;
    while (n && suffix[n - 1] == ' ') suffix[--n] = 0;
    pb_unit u = v->display->unit;
    if (!strcmp(suffix, "k") || !strcmp(suffix, "khz")) x *= 1000;
    else if (!strcmp(suffix, "s") && u == PB_UNIT_MS) x *= 1000;
    else if (!*suffix || (!strcmp(suffix, "hz") && u == PB_UNIT_HZ) || (!strcmp(suffix, "ms") && u == PB_UNIT_MS)
             || (!strcmp(suffix, "c") && u == PB_UNIT_CENTS) || (!strcmp(suffix, "st") && u == PB_UNIT_SEMITONES)) {
    } else return false;
    if (v->info.integer && x != round(x)) return false;
    if (!(x >= v->info.minimum && x <= v->info.maximum)) return false;
    *value = x;
    return true;
}

ma_patch pb_init_patch(void) {
    ma_patch p = { 0 };
    pb_field_view v;
    for (size_t i = 0; pb_field_view_at(i, &v); i++) (void)ma_patch_field_set(&p, i, v.display->init);
    return p;
}
