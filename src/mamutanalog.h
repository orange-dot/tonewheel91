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
    float sine_level;
    float pulse_width;
} ma_vco_controls;

typedef struct {
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
    float mozaik_slope;
    float mozaik_contrast;
    float mozaik_phason;
    float mozaik_drift;
    float mixer_pressure;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_drive;
    float filter_env_amount;
    float filter_keytrack;
    ma_adsr amp_adsr;
    ma_adsr filter_adsr;
    float macro[MA_MACRO_COUNT];
    float body_drive;
    float width;
    float crossfeed;
    float master_level;
} ma_patch;

/* Compiled factory bank; hosted mirrors are not core dependencies. */
extern const ma_patch ma_patch_tepih;
extern const ma_patch ma_patch_lead;
extern const ma_patch ma_patch_dubina;

typedef struct {
    float current;
    float target;
    float step;
    uint16_t remaining;
} ma_smoother;

typedef struct {
    ma_smoother saw_level;
    ma_smoother pulse_level;
    ma_smoother triangle_level;
    ma_smoother sine_level;
    ma_smoother pulse_width;
} ma_vco_smoothers;

typedef struct {
    ma_vco_smoothers vco1;
    ma_vco_smoothers vco2;
    ma_smoother vco2_level;
    ma_smoother vco2_fine_cents;
    ma_smoother sync_amount;
    ma_smoother sync_softness;
    ma_smoother crossmod_amount;
    ma_smoother noise_level;
    ma_smoother mozaik_mix;
    ma_smoother mozaik_slope;
    ma_smoother mozaik_contrast;
    ma_smoother mozaik_drift;
    ma_smoother mixer_pressure;
    ma_smoother filter_cutoff_hz;
    ma_smoother filter_resonance;
    ma_smoother filter_drive;
    ma_smoother filter_env_amount;
    ma_smoother filter_keytrack;
    ma_smoother pitch_bend_semitones;
    ma_smoother poly_pressure;
    ma_smoother body_drive;
    ma_smoother body_load_ratio;
    ma_smoother width;
    ma_smoother crossfeed;
} ma_control_smoothers;

typedef struct {
    float gravitacija;
    float bloom;
    float heat;
    float ruin;
    float swarm;
    float horizont_open;
    float horizont_air;
    float horizont_span;
    float pec_mass;
    float pec_heat;
    float pec_pressure;
    float baklja_ready;
    float baklja_edge;
    float baklja_sync_bias;
    float grav_pull;
    float mass;
    float strain;
    float headroom;
    float body_focus;
    float rupture_threshold;
    float rupture_response;
    float spatial_dispersion;
} ma_identity;

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
    uint64_t sanitization_count;
    uint64_t knee_hit_count;
    uint64_t tiny_flush_count;
    float pre_peak;
    float post_peak;
    float maximum_reduction;
} ma_output_diagnostics;

typedef struct {
    tw_drive body;
    float dc_lp_left;
    float dc_lp_right;
    float dc_coefficient;
    float pre_body;
    float post_body;
    ma_output_diagnostics diagnostics;
} ma_output_state;

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
    float mozaik_slope;
    float mozaik_contrast_control;
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
    float pitch_bend_semitones;
    float channel_pressure;
    float poly_pressure;
    float mod_wheel;
    ma_identity identity_zero;
    ma_identity identity;
    ma_control_smoothers smoothers;
    float body_drive;
    float body_load_ratio;
    float width;
    float crossfeed;
    float master_level;
    uint64_t noise_state;
    ma_oscillator oscillator1;
    ma_oscillator oscillator2;
    ma_mozaik mozaik;
    ma_ladder ladder;
    ma_output_state output;
#if defined(MA_SOURCE_EVIDENCE)
    float oversample_history[3][31];
    uint8_t oversample_history_pos[3];
#else
    float oversample_history[2][31];
    uint8_t oversample_history_pos[2];
#endif
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
    uint32_t ignored_release_velocities;
    bool note_active;
} ma_synth;

static_assert(sizeof(ma_synth) < 1024u * 1024u);

enum {
    MA_CARD_COUNT = 5,
    MA_CARD_NONE = UINT8_MAX,
};

typedef enum {
    MA_CARD_IDLE,
    MA_CARD_HELD,
    MA_CARD_RELEASED,
} ma_card_phase;

typedef struct {
    uint64_t age;
    uint8_t channel;
    uint8_t note;
    ma_card_phase phase;
} ma_card_owner;

typedef struct {
    ma_synth card[MA_CARD_COUNT];
    ma_card_owner owner[MA_CARD_COUNT];
    uint64_t next_age;
    uint64_t ignored_release_velocities;
    uint8_t cursor;
} ma_card_bank;

static_assert(sizeof(ma_card_bank) < 1024u * 1024u);

/* MIDI-domain table lookups. Values 128..255 are hostile input and return
 * zero instead of indexing outside the pinned tables. */
[[nodiscard]] float ma_note_frequency_hz(uint8_t note);
[[nodiscard]] float ma_velocity_level(uint8_t velocity);
[[nodiscard]] float ma_velocity_filter(uint8_t velocity);
/* Pure MA1 safety transfer, exposed for boundary and evidence referees. */
[[nodiscard]] float ma_safety_curve(float sample);

void ma_synth_init(ma_synth *s, float sample_rate_hz);
void ma_synth_init_patch(ma_synth *s, float sample_rate_hz,
                         const ma_patch *patch);
/* Retains the initialized sample rate and resets the complete voice/DSP. */
void ma_synth_apply_patch(ma_synth *s, const ma_patch *patch);
void ma_synth_note_on(ma_synth *s, uint8_t channel, uint8_t note,
                      uint8_t velocity);
void ma_synth_note_off(ma_synth *s, uint8_t channel, uint8_t note,
                       uint8_t release_velocity);
[[nodiscard]] ma_frame ma_synth_tick(ma_synth *s);

void ma_synth_set_pitch_bend(ma_synth *s, float semitones);
void ma_synth_set_channel_pressure(ma_synth *s, float pressure);
void ma_synth_set_poly_pressure(ma_synth *s, uint8_t channel, uint8_t note,
                                float pressure);
void ma_synth_set_mod_wheel(ma_synth *s, float amount);

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
void ma_synth_set_output(ma_synth *s, float body_drive, float width,
                         float crossfeed, float master_level);

void ma_card_bank_init(ma_card_bank *bank, float sample_rate_hz);
void ma_card_bank_init_patch(ma_card_bank *bank, float sample_rate_hz,
                             const ma_patch *patch);
/* Event calls return the affected slot, or MA_CARD_NONE. */
[[nodiscard]] uint8_t ma_card_bank_note_on(ma_card_bank *bank,
                                           uint8_t channel, uint8_t note,
                                           uint8_t velocity);
[[nodiscard]] uint8_t ma_card_bank_note_off(ma_card_bank *bank,
                                            uint8_t channel, uint8_t note,
                                            uint8_t release_velocity);
/* Per-card frames retain fixed slot order; stereo summing lands later in MA2. */
void ma_card_bank_tick(ma_card_bank *bank,
                       ma_frame frames[static MA_CARD_COUNT]);

#endif
