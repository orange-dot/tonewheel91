/* Mamut Analog core — freestanding C23, fixed caller-owned state.
 * Constants and numeric contracts: docs/ma-constants.md. */
#ifndef MAMUTANALOG_H
#define MAMUTANALOG_H

#include "tonewheel.h"

typedef struct {
    float attack_ms;
    float decay_ms;
    float sustain;
    float release_ms;
} ma_adsr;

typedef enum {
    MA_ENVELOPE_IDLE,
    MA_ENVELOPE_ATTACK,
    MA_ENVELOPE_DECAY,
    MA_ENVELOPE_SUSTAIN,
    MA_ENVELOPE_RELEASE,
} ma_envelope_stage;

typedef struct {
    float level;
    float completion_error;
    ma_envelope_stage stage;
} ma_envelope;

typedef struct {
    float left;
    float right;
} ma_frame;

typedef enum {
    MA_MACRO_GRAVITACIJA,
    MA_MACRO_BLOOM,
    MA_MACRO_HEAT,
    MA_MACRO_RUIN,
    MA_MACRO_SWARM,
    MA_MACRO_COUNT,
} ma_macro_id;

typedef struct {
    float saw_level;
    float pulse_level;
    float triangle_level;
    float pulse_width;
} ma_vco_controls;

typedef struct {
    uint64_t phase_q48;
    float triangle;
    float sync_residual;
    float guard_gain;
    float guard_target;
    float guard_step;
    uint16_t guard_frames;
    bool triangle_initialized;
} ma_oscillator;

typedef struct {
    uint32_t frac_q32;
    uint32_t applied_phason_q32;
    uint32_t pending_phason_q32;
    uint32_t tiles_emitted;
    float tile_pos;
    float tile_len;
    float tile_sign;
    float guard_gain;
    float guard_target;
    float guard_step;
    uint16_t guard_frames;
    bool has_pending_phason;
} ma_mozaik;

typedef struct {
    float state[4];
    float y4_guess;
    float output_history[7];
    float pressure_input;
    float pressure_output;
    uint32_t reset_count;
    uint8_t output_pos;
} ma_ladder;

typedef struct {
    float sample_rate_hz;
    ma_vco_controls vco1;
    ma_vco_controls vco2;
    float vco2_level;
    int vco2_interval;
    float vco2_fine_cents;
    float sync_amount;
    float sync_softness;
    float crossmod_amount;
    float noise_level;
    float mozaik_mix;
    float mozaik_contrast;
    uint32_t mozaik_slope_q32;
    uint32_t mozaik_phason_q32;
    float mozaik_drift;
    float mixer_pressure;
    float filter_cutoff_hz;
    float filter_g;
    float filter_cutoff_effective_hz;
    float filter_resonance;
    float filter_drive;
    float filter_env_amount;
    float filter_keytrack;
    ma_adsr amp_adsr;
    ma_adsr filter_adsr;
    ma_envelope amp_envelope;
    ma_envelope filter_envelope;
    float macro[MA_MACRO_COUNT];
    float body_drive;
    float width;
    float master_level;
    uint64_t noise_state;
    ma_oscillator oscillator1;
    ma_oscillator oscillator2;
    ma_mozaik mozaik;
    ma_ladder ladder;
#if defined(MA_SOURCE_EVIDENCE)
    float oversample_history[3][31];
    uint8_t oversample_history_pos[3];
#else
    float oversample_history[2][31];
    uint8_t oversample_history_pos[2];
#endif
    uint8_t note;
    uint8_t velocity;
    bool note_active;
} ma_synth;

/* MIDI-domain table lookups. Values 128..255 are hostile input and return
 * zero instead of indexing outside the pinned tables. */
[[nodiscard]] float ma_note_frequency_hz(uint8_t note);
[[nodiscard]] float ma_velocity_level(uint8_t velocity);
[[nodiscard]] float ma_velocity_filter(uint8_t velocity);

void ma_synth_init(ma_synth *s, float sample_rate_hz);
void ma_synth_note_on(ma_synth *s, uint8_t note, uint8_t velocity);
void ma_synth_note_off(ma_synth *s, uint8_t note);
[[nodiscard]] ma_frame ma_synth_tick(ma_synth *s);

void ma_synth_set_vco1(ma_synth *s, ma_vco_controls controls);
void ma_synth_set_vco2(ma_synth *s, ma_vco_controls controls, float level,
                       int interval, float fine_cents);
void ma_synth_set_oscillator_modulation(ma_synth *s, float sync_amount,
                                        float sync_softness,
                                        float crossmod_amount,
                                        float noise_level);
void ma_synth_set_mozaik(ma_synth *s, float mix, float slope,
                         float contrast, float phason, float drift);
void ma_synth_set_filter(ma_synth *s, float cutoff_hz, float resonance,
                         float drive, float mixer_pressure);
void ma_synth_set_filter_modulation(ma_synth *s, float envelope_amount,
                                    float keytrack);
void ma_synth_set_amp_adsr(ma_synth *s, ma_adsr adsr);
void ma_synth_set_filter_adsr(ma_synth *s, ma_adsr adsr);
void ma_synth_set_macro(ma_synth *s, ma_macro_id macro, float value);

#endif
