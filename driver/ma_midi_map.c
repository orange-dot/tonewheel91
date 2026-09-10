#include "ma_midi_map.h"
#include "ma_midi_tables.h"

bool ma_midi_map_init(ma_midi_map *map, float rate,
    const ma_voice_pool_config *config, const ma_patch *patch, uint16_t channels) {
    if (!ma_voice_pool_init(&map->pool, rate, config, patch)) return false;
    map->patch = patch ? *patch : ma_patch_tepih;
    map->stats = (ma_midi_stats){0};
    map->channel_mask = channels;
    return true;
}

static bool control_change(ma_midi_map *map, uint8_t ch, uint8_t cc, uint8_t v) {
    ma_voice_pool *pool = &map->pool;
    ma_patch *p = &map->patch;
    float unit = (float)v / 127.0f;
    if (cc >= 109 && cc <= 113) {
        unsigned macro = cc - 109;
        p->macro[macro] = unit;
        ma_voice_pool_set_macro(pool, (ma_macro_id)macro, unit);
        return true;
    }
    switch (cc) {
    case 1: ma_voice_pool_set_mod_wheel(pool, ch, unit); return true;
    case 5:
        ma_voice_pool_set_glide(pool, pool->control.glide.enabled, 10 * unit);
        return true;
    case 64: ma_voice_pool_set_sustain(pool, ch, v >= 64); return true;
    case 65:
        ma_voice_pool_set_glide(pool, v >= 64, pool->control.glide.seconds);
        return true;
    case 77:
        ma_voice_pool_set_lfo(pool, unit, pool->control.lfo.rate_hz); return true;
    case 78:
        ma_voice_pool_set_lfo(pool, pool->control.lfo.depth, MA_MIDI_LFO_HZ[v]);
        return true;
    case 93: ma_voice_pool_set_character(pool, unit); return true;
    case 107: {
        ma_chorus_controls c = pool->effects.chorus.controls;
        c.enabled = true; c.mix = unit;
        ma_effects_set_chorus(&pool->effects, c);
        return true;
    }
    case 108: {
        ma_reverb_controls c = pool->effects.reverb.controls;
        c.enabled = true; c.mix = unit;
        ma_effects_set_reverb(&pool->effects, c);
        return true;
    }
    case 120: case 123: ma_voice_pool_panic(pool); return true;
    case 7: p->master_level = unit; break;
    case 14: p->vco2_interval = (48 * v + 63) / 127 - 24; break;
    case 15: p->noise_level = unit; break;
    case 17: p->amp_adsr.decay_ms = MA_MIDI_TIME_MS[v]; break;
    case 18: p->amp_adsr.sustain = unit; break;
    case 19: p->filter_adsr.sustain = unit; break;
    case 20: p->filter_adsr.release_ms = MA_MIDI_TIME_MS[v]; break;
    case 71: p->filter_resonance = unit; break;
    case 72: p->amp_adsr.release_ms = MA_MIDI_TIME_MS[v]; break;
    case 73: p->amp_adsr.attack_ms = MA_MIDI_TIME_MS[v]; break;
    case 74: p->filter_cutoff_hz = MA_MIDI_CUTOFF_HZ[v]; break;
    case 75: p->filter_adsr.decay_ms = MA_MIDI_TIME_MS[v]; break;
    case 76: p->filter_env_amount = unit; break;
    case 79: p->filter_adsr.attack_ms = MA_MIDI_TIME_MS[v]; break;
    case 80: p->vco1.saw_level = unit; break;
    case 81: p->vco1.pulse_level = unit; break;
    case 82: p->vco1.triangle_level = unit; break;
    case 83: p->vco2.saw_level = unit; break;
    case 84: p->vco2.pulse_level = unit; break;
    case 85: p->vco2.triangle_level = unit; break;
    case 86: p->vco2_level = unit; break;
    case 87: p->vco2_fine_cents = 50.0f * ((float)v - 64) / (v < 64 ? 64 : 63); break;
    case 88: p->vco1.pulse_width = .05f + .90f * unit; break;
    case 89: p->vco2.pulse_width = .05f + .90f * unit; break;
    case 90: p->sync_amount = unit; break;
    case 91: p->crossmod_amount = unit; break;
    case 92: p->width = unit; break;
    case 94: p->mozaik_mix = unit; break;
    case 95: p->mozaik_slope = unit; break;
    case 102: p->mozaik_contrast = unit; break;
    case 103: p->mozaik_phason = (float)v / 128.0f; break;
    case 104: p->mozaik_drift = unit; break;
    case 105: p->mixer_pressure = unit; break;
    case 106: p->filter_drive = unit; break;
    /* GFM awaits MA4. CC114 cannot reinitialize a playing pool. */
    default: return false;
    }
    ma_voice_pool_set_patch(pool, p);
    return true;
}

void ma_midi_apply(ma_midi_map *map, uint8_t status, uint8_t d1, uint8_t d2) {
    if (status < 0x80 || status >= 0xf0) {
        map->stats.unsupported_messages++;
        return;
    }
    unsigned kind = status & 0xf0;
    if (d1 >= 128 || (kind != 0xc0 && kind != 0xd0 && d2 >= 128)) {
        map->stats.invalid_messages++;
        return;
    }
    uint8_t ch = status & 0x0f;
    if (!(map->channel_mask & (1u << ch))) { map->stats.filtered++; return; }
    ma_voice_pool *pool = &map->pool;
    switch (kind) {
    case 0x90:
        if (d2) {
            map->stats.note_ons++;
            if (!ma_voice_pool_note_on(pool, ch, d1, d2)) map->stats.allocation_failures++;
            return;
        }
        [[fallthrough]];
    case 0x80:
        map->stats.note_offs++;
        if (!ma_voice_pool_note_off(pool, ch, d1, d2)) map->stats.unmatched_note_offs++;
        return;
    case 0xa0:
        map->stats.poly_pressures++;
        ma_voice_pool_set_poly_pressure(pool, ch, d1, (float)d2 / 127.0f);
        return;
    case 0xd0:
        map->stats.channel_pressures++;
        ma_voice_pool_set_channel_pressure(pool, ch, (float)d1 / 127.0f);
        return;
    case 0xe0: {
        map->stats.bends++;
        int word = (int)d1 + 128 * d2;
        float bend = 2.0f * (float)(word - 8192) / (word <= 8192 ? 8192 : 8191);
        ma_voice_pool_set_pitch_bend(pool, ch, bend);
        return;
    }
    case 0xb0:
        if (control_change(map, ch, d1, d2)) map->stats.ccs++;
        else map->stats.ignored_ccs++;
        return;
    default: map->stats.unsupported_messages++; return;
    }
}
