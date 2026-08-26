#include "ma_dark_lead.h"

ma_patch ma_dark_lead_patch(void) {
    ma_patch patch = ma_patch_lead;
    patch.vco1.saw_level = .04f;
    patch.vco1.pulse_level = 0.0f;
    patch.vco1.triangle_level = .34f;
    patch.vco1.sine_level = .52f;
    patch.vco2.saw_level = .03f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .31f;
    patch.vco2.sine_level = .49f;
    patch.vco2_level = .68f;
    patch.sync_amount = .055f;
    patch.crossmod_amount = .035f;
    patch.noise_level = 0.0f;
    patch.mozaik_mix = 0.0f;
    patch.mixer_pressure = .05f;
    patch.filter_cutoff_hz = 880.0f;
    patch.filter_resonance = .18f;
    patch.filter_drive = .085f;
    patch.filter_env_amount = .18f;
    patch.amp_adsr = (ma_adsr){ 520.0f, 1800.0f, .78f, 9000.0f };
    patch.filter_adsr = (ma_adsr){ 800.0f, 2200.0f, .38f, 7800.0f };
    patch.macro[MA_MACRO_GRAVITACIJA] = .06f;
    patch.macro[MA_MACRO_BLOOM] = .10f;
    patch.macro[MA_MACRO_HEAT] = 0.0f;
    patch.macro[MA_MACRO_RUIN] = 0.0f;
    patch.macro[MA_MACRO_SWARM] = 0.0f;
    patch.body_drive = .045f;
    patch.width = .58f;
    patch.crossfeed = .20f;
    patch.master_level = .20f;
    return patch;
}
