#include "pb_derive.h"

#include "../../driver/ma_patch_file.h"

bool pb_monitor_active(const pb_monitor *m) {
    for (unsigned i = 0; i < PB_SRC_COUNT; i++) if (m->solo[i]) return true;
    for (unsigned i = 0; i < PB_STAGE_COUNT; i++) if (m->mute[i]) return true;
    return false;
}

ma_patch pb_derive(const ma_patch *authored, const pb_monitor *m) {
    ma_patch p = *authored;
    if (!m) return p;
    bool any = false;
    for (unsigned i = 0; i < PB_SRC_COUNT; i++) any = any || m->solo[i];
    if (any) {
        if (!m->solo[PB_SRC_VCO1])
            p.vco1.saw_level = p.vco1.pulse_level = p.vco1.triangle_level = p.vco1.sine_level
                = p.vco1.tri_sine_level = p.vco1.tri_saw_level = 0;
        if (!m->solo[PB_SRC_VCO2]) p.vco2_level = 0;
        if (!m->solo[PB_SRC_RASTER]) p.raster_mix = 0;
        if (!m->solo[PB_SRC_MOZAIK]) p.mozaik_mix = 0;
        if (!m->solo[PB_SRC_NOISE]) p.noise_level = 0;
    }
    if (m->mute[PB_STAGE_PRESSURE]) p.mixer_pressure = 0;
    if (m->mute[PB_STAGE_BCS]) p.bcs_amount = 0;
    if (m->mute[PB_STAGE_FILTER]) {
        p.filter_cutoff_hz = 20000;
        p.filter_resonance = p.filter_drive = p.filter_env_amount = p.filter_keytrack = 0;
    }
    if (m->mute[PB_STAGE_GLAS]) p.formant_mix = p.grain_mix = 0;
    if (m->mute[PB_STAGE_BODY]) p.body_drive = 0;
    return p;
}

ma_patch pb_morph(const ma_patch *a, const ma_patch *b, double t) {
    if (t <= 0) return *a;
    if (t >= 1) return *b;
    ma_patch p = *a;
    ma_patch_field_info info;
    for (size_t i = 0; ma_patch_field_info_at(i, &info); i++) {
        double x = 0, y = 0;
        (void)ma_patch_field_get(a, i, &x);
        (void)ma_patch_field_get(b, i, &y);
        double v = info.integer ? (t < .5 ? x : y) : (1 - t) * x + t * y;
        if (v < info.minimum) v = info.minimum;
        if (v > info.maximum) v = info.maximum;
        (void)ma_patch_field_set(&p, i, v);
    }
    return p;
}
