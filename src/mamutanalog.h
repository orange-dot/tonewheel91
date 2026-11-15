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
    /* The triangle core's shapers (docs/ma-vco-plan.md, V3): the sine of a
     * long-tailed pair and the saw of a switched pair, both on the
     * triangle. Zero levels are an exact bypass. */
    float tri_sine_level;
    float tri_saw_level;
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
    float raster_mix;
    float raster_position;
    float raster_warp;
    float bcs_amount;
    float bcs_regime;
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
    /* Glas (ma-constants.md 10C). Zero mixes are an exact bypass. */
    float formant_mix;
    float formant_vowel;
    float formant_shift;
    float formant_orbita;
    float grain_mix;
    /* Circuit-derived oscillator pitch (docs/ma-vco-plan.md). vco_analog is
     * how much of the VCO-2 pitch law applies; zero is an exact bypass.
     * vco_dead_time is the share of the saw core's reset dead time left
     * untrimmed: zero is a board with its high-frequency trim set, one a
     * board without that trim (flat at the top). */
    float vco_analog;
    float vco_dead_time;
    /* How soft the two cores' shared supply is (V4): 0 a well-decoupled
     * board, 1 a 10 ohm / 100 nF rail; the cores pull each other near
     * unison, the octave and the fifth. Scales with vco_analog. */
    float vco_supply;
} ma_patch;

/* Compiled factory bank; hosted mirrors are not core dependencies. */
extern const ma_patch ma_patch_tepih;
extern const ma_patch ma_patch_lead;
extern const ma_patch ma_patch_dubina;
extern const ma_patch ma_patch_raster;
extern const ma_patch ma_patch_prizma;
extern const ma_patch ma_patch_granica;

typedef struct {
    float current;
    float target;
    float step;
    uint16_t remaining;
} ma_smoother;

typedef struct {
    float attack;
    float decay;
    float release;
} ma_envelope_time_bias;

/* Bank-owned physical-card calibration. Time and VCA biases are fractional
 * full-character values; sustain has no stage time and is not varied. */
typedef struct {
    float amount;
    ma_smoother smoother;
    float vco1_cents;
    float vco2_cents;
    float cutoff_octaves;
    ma_envelope_time_bias amp_time_bias;
    ma_envelope_time_bias filter_time_bias;
    float vca_bias;
    uint64_t walk_state;
    float walk_target;
    float walk_cents;
    float walk_step;
    uint8_t walk_phase;
    bool assigned;
    /* The card's two VCO-2 cores (docs/ma-vco-plan.md): its temperature away
     * from the trim temperature, VCO2's own offset from it, and per core the
     * extra capacitor current (as a frequency) and the reset dead time. */
    float temp_k;
    float vco2_temp_k;
    float vco_extra_hz[2];
    float vco_dead_us[2];
} ma_character;

typedef struct {
    float current_note;
    float target_note;
    float step;
    float seconds;
    uint32_t remaining;
    bool enabled;
    bool initialized;
} ma_glide;

typedef struct {
    float phase;
    float depth;
    float rate_hz;
} ma_lfo;

typedef struct {
    ma_smoother saw_level;
    ma_smoother pulse_level;
    ma_smoother triangle_level;
    ma_smoother sine_level;
    ma_smoother pulse_width;
    ma_smoother tri_sine_level;
    ma_smoother tri_saw_level;
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
    ma_smoother raster_mix;
    ma_smoother raster_position;
    ma_smoother raster_warp;
    ma_smoother bcs_amount;
    ma_smoother bcs_regime;
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
    ma_smoother lfo_depth;
    ma_smoother lfo_rate_hz;
    ma_smoother body_drive;
    ma_smoother body_load_ratio;
    ma_smoother width;
    ma_smoother crossfeed;
    ma_smoother spatial_dispersion;
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
    /* One plus the phase run since a sync reset that touched an edge zone,
     * or 0: no such reset, or the run has passed the zone. While it is
     * nonzero, sync_carry holds how far behind the reset the wrap, the pulse
     * edge and the half-turn edge were last crossed (ma-constants.md 3.2). */
    uint64_t sync_run_q48;
    float sync_carry[3];
    float triangle;
    float sync_residual;
    float guard_gain;
    float guard_target;
    float guard_step;
    uint16_t guard_frames;
    bool triangle_initialized;
    float rise;             /* the triangle's last rise (V2); 0: none yet */
    float shaper_dc;        /* the sine shaper's coupling capacitor (V3b) */
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
    uint64_t phase_q48;
    uint8_t mip;
} ma_raster;

typedef struct {
    float hopf_re;
    float hopf_im;
    float duffing_x;
    float duffing_v;
    float output;
    float maximum_state;
    uint64_t reset_count;
} ma_bcs;

/* Glas: four formant centres shared by a formant bank after the ladder and
 * a pitch-synchronous formant-grain source; Orbita moons can capture the
 * centres on the voice's harmonics. ma-constants.md 10C. */
enum { MA_FORMANTS = 4, MA_GRAINS_PER_FORMANT = 4 };

typedef struct {
    float s1;
    float s2;
} ma_formant_svf;

typedef struct {
    float ratio;
    int16_t captured;   /* harmonic number, or -1 */
    int16_t candidate;
    uint8_t hold;
} ma_formant_moon;

typedef struct {
    float phase;
    float step;
    float envelope;
    float decay;
    float attack_pos;
    float attack_step;
    float gain;
    bool active;
} ma_grain;

typedef struct {
    float formant_mix;
    float formant_vowel;
    float formant_shift;
    float formant_orbita;
    float grain_mix;
    ma_smoother mix_smoother;
    ma_smoother vowel_smoother;
    ma_smoother shift_smoother;
    ma_smoother grain_smoother;
    ma_formant_svf svf[MA_FORMANTS];
    ma_formant_moon moon[MA_FORMANTS];
    float centre_hz[MA_FORMANTS];
    float g[MA_FORMANTS];
    float k[MA_FORMANTS];
    ma_grain grain[MA_FORMANTS][MA_GRAINS_PER_FORMANT];
    float pulse_phase;
    float grain_out[2];
    float frame_grain_mix;  /* this frame's grain mix; 0 when Glas is idle */
    uint8_t grain_cursor[MA_FORMANTS];
    uint8_t tick_pos;
    bool retrigger;
    bool coefficients_ready;
    uint64_t captures;
    uint64_t releases;
    uint64_t resets;
    uint64_t grains_launched;
    uint64_t grains_stolen;
    uint32_t coefficient_epoch;  /* bumped by every Orbita tick */
} ma_glas;

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
    float vco_analog;
    float vco_dead_time;
    float vco_supply;
    float vco2_bend;        /* this frame: VCO2's law over VCO1's, minus one */
    /* this frame's circuit shapes (V2): each triangle's rise less 0.5, the
     * swing its shapers see less 1, and the saw's bow; all 0 without the law */
    float vco_skew[2];
    float vco_swell[2];
    float vco_bow;
    float vco_ramp_bow[2][2];   /* each triangle's rise and fall bow (V2c), 0 = straight */
    float vco_buffer;           /* B1's buffer bend (V2d): the amount, 0 = none */
    /* this frame's coupling of the two cores (V4): lock range in Hz (0 =
     * none), VCO2 over VCO1 near p:q, and the phase it locks at */
    float vco_couple_hz, vco_couple_p, vco_couple_q, vco_couple_jump;
    float sync_amount;
    float sync_softness;
    float crossmod_amount;
    float noise_level;
    float raster_mix;
    float raster_position;
    float raster_warp;
    float bcs_amount;
    float bcs_regime;
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
    ma_glide glide;
    ma_lfo lfo;
    ma_character character;
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
    ma_raster raster;
    ma_bcs bcs;
    ma_mozaik mozaik;
    ma_ladder ladder;
    ma_output_state output;
    ma_glas glas;
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
    MA_CARD_SUSTAINED,
    MA_CARD_RELEASED,
} ma_card_phase;

typedef struct {
    uint64_t age;
    uint8_t channel;
    uint8_t note;
    ma_card_phase phase;
} ma_card_owner;

typedef struct {
    float position;
    float left;
    float right;
} ma_card_pan;

typedef struct {
    ma_output_state output;
    ma_card_pan pan[MA_CARD_COUNT];
    float width;
    float dispersion;
    float pre_side;
    float post_side;
} ma_stereo_state;

typedef struct {
    ma_synth card[MA_CARD_COUNT];
    ma_card_owner owner[MA_CARD_COUNT];
    ma_stereo_state stereo;
    uint64_t next_age;
    uint64_t ignored_release_velocities;
    uint8_t cursor;
    bool sustain;
    bool unison;
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
/* Retains sample rate and physical-card character (including its clock),
 * and resets the remaining voice/DSP. */
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
/* The VCO-2 pitch law: amount [0,1] (zero is an exact bypass) and the share
 * of the reset dead time left untrimmed [0,1]. Not smoothed: an edit moves
 * the pitch by cents, without a phase step. */
void ma_synth_set_vco_analog(ma_synth *s, float amount, float dead_time);
/* How soft the two cores' shared supply is [0,1] (V4): their coupling near
 * unison, the octave and the fifth, scaled by the analog amount. */
void ma_synth_set_vco_supply(ma_synth *s, float supply);
void ma_synth_set_oscillator_modulation(ma_synth *s, float sync_amount,
                                        float sync_softness,
                                        float crossmod_amount,
                                        float noise_level);
void ma_synth_set_raster(ma_synth *s, float mix, float position, float warp);
/* Glas formant bank and Orbita formants (ma-constants.md 10C). Mix zero is
 * an exact, state-preserving bypass when grain mix is zero too. */
void ma_synth_set_formant(ma_synth *s, float mix, float vowel, float shift,
                          float orbita);
/* Glas formant grains: crossfade into the source at mix [0,1]. */
void ma_synth_set_grain(ma_synth *s, float mix);
/* BCS is a feedback coordinate, not an audible parallel source. Amount zero
 * is an exact state-preserving bypass; regime traverses stable, edge,
 * subharmonic and recovery landmarks over the unit interval. */
void ma_synth_set_bcs(ma_synth *s, float amount, float regime);
void ma_synth_set_mozaik(ma_synth *s, float mix, float slope,
                         float contrast, float phason, float drift);
void ma_synth_set_filter(ma_synth *s, float cutoff_hz, float resonance,
                         float drive, float mixer_pressure);
void ma_synth_set_filter_modulation(ma_synth *s, float envelope_amount,
                                    float keytrack);
void ma_synth_set_amp_adsr(ma_synth *s, ma_adsr adsr);
void ma_synth_set_filter_adsr(ma_synth *s, ma_adsr adsr);
void ma_synth_set_glide(ma_synth *s, bool enabled, float seconds);
void ma_synth_set_lfo(ma_synth *s, float depth, float rate_hz);
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
void ma_card_bank_set_sustain(ma_card_bank *bank, bool down);
void ma_card_bank_panic(ma_card_bank *bank);
/* Mode changes panic first; unison NoteOn owns all five cards. */
void ma_card_bank_set_unison(ma_card_bank *bank, bool enabled);
/* Session control, default .20; finite values clamp to [0,1], others to 0.
 * Zero bypasses immediately; positive edits use the ordinary 6 ms ramp.
 * Call on the render thread, as with other bank controls. */
void ma_card_bank_set_character(ma_card_bank *bank, float amount);
/* Legacy per-card MA1 outputs in fixed slot order. Choose this or the stereo
 * entry point for a bank's lifetime; do not tick both for the same sample. */
void ma_card_bank_tick(ma_card_bank *bank,
                       ma_frame frames[static MA_CARD_COUNT]);
/* Sum raw cards through pan, shared mid body, DC block and safety. Card 0
 * owns shared output/identity controls; the bank remains single-timbre. */
[[nodiscard]] ma_frame ma_card_bank_tick_stereo(ma_card_bank *bank);
/* Common output controls on all cards; same ranges/fallbacks as MA1.
 * Direct width zero forces exact dual mono, including after stereo history. */
void ma_card_bank_set_output(ma_card_bank *bank, float body_drive,
                              float width, float crossfeed, float master_level);

/* MA2-7 digital pool. Initialize/reset only while stopped; all subsequent
 * calls belong to the render thread. No allocation or internal synchronization.
 * Configuration is session state, never part of ma_patch. IDs start at one;
 * zero means failure (including velocity-zero NoteOn, which performs NoteOff).
 * Exhausted uint64_t IDs fail without changing ownership until reset. */
enum { MA_POOL_CAPACITY = 64 };
#define MA_POOL_DEFAULT_SEED UINT64_C(0x4d41504f4f4c3031)
/* Render engine. AUTO renders eight voices per AVX2 vector when the CPU and
 * OS support AVX2 on x86-64, else one voice at a time; SCALAR always renders
 * one voice at a time. Both run the same DSP kernels and produce identical
 * bits. */
typedef enum : uint8_t { MA_POOL_ENGINE_AUTO = 0, MA_POOL_ENGINE_SCALAR = 1 } ma_pool_engine;
typedef struct {
    uint8_t limit;                 /* 1..64, divisible by unison_count */
    uint8_t unison_count;          /* 1, 2, 4, 8 */
    float detune_cents;            /* finite 0..100, symmetric +/- range */
    uint64_t session_seed;
    ma_pool_engine engine;         /* zero (AUTO) when left unset */
} ma_voice_pool_config;
extern const ma_voice_pool_config ma_voice_pool_default_config;

/* MA3-2 shared post-output effects. Session controls, outside ma_patch.
 * Disabled freezes state; false->true clears that stage in the setter.
 * Enabled mix zero advances history and returns incoming bits exactly.
 * Call setters on the render thread between frames, never concurrently. */
enum { MA_CHORUS_CAPACITY = 15360, MA_REVERB_CAPACITY = 65280 };
typedef struct { bool enabled; float mix, depth, rate_hz; } ma_chorus_controls;
typedef struct { bool enabled; float mix, size, damping; } ma_reverb_controls;
typedef struct {
    float left[MA_CHORUS_CAPACITY], right[MA_CHORUS_CAPACITY];
    unsigned length, write_index;
    float phase;
    ma_chorus_controls controls;
} ma_chorus;
typedef struct {
    float left[MA_REVERB_CAPACITY], right[MA_REVERB_CAPACITY];
    unsigned length, write_index;
    float damped_left, damped_right;
    ma_reverb_controls controls;
} ma_reverb;
typedef struct {
    ma_chorus chorus;
    ma_reverb reverb;
    float sample_rate_hz;
    uint64_t sanitizations, clips;
    float pre_peak, post_peak;
} ma_effects;
void ma_effects_init(ma_effects *fx, float sample_rate_hz);
/* Clears histories/diagnostics while retaining controls and sample rate. */
void ma_effects_reset(ma_effects *fx);
void ma_effects_set_chorus(ma_effects *fx, ma_chorus_controls controls);
void ma_effects_set_reverb(ma_effects *fx, ma_reverb_controls controls);
[[nodiscard]] ma_frame ma_effects_tick(ma_effects *fx, ma_frame input);

/* RK1 insert rack (docs/ma-rack-plan.md): one fixed serial chain of blocks
 * from a closed set of kinds, between the output DC block and the master
 * fader. ma_rack_spec is the small value a host publishes; ma_rack is the
 * caller-owned DSP state. An empty rack, a disabled block, a block at mix
 * zero and a block at neutral settings all return their input bits. */
/* EQ (RK1b): up to MA_EQ_BANDS parametric bands in series, then an output
 * gain. The first param[MA_EQ_BAND_COUNT] bands are in use; band b's fields
 * sit at param[MA_EQ_BAND(b, field)], and bands past the count hold their
 * initial values. Utility: gain dB, mid/side width (1 = unchanged), balance.
 * One enum, so the counts compare without enum-type warnings. */
enum {
    MA_EQ_BANDS = 8, MA_EQ_BAND_FIELDS = 5,
    MA_EQ_BAND_COUNT = 0, MA_EQ_OUTPUT_DB = 1, MA_EQ_FIRST_BAND = 2,
    MA_EQ_PARAM_COUNT = MA_EQ_FIRST_BAND + MA_EQ_BANDS * MA_EQ_BAND_FIELDS,
    MA_UTILITY_GAIN_DB = 0, MA_UTILITY_WIDTH = 1, MA_UTILITY_PAN = 2, MA_UTILITY_PARAM_COUNT = 3,
    MA_RACK_SLOTS = 8, MA_RACK_PARAMS = MA_EQ_PARAM_COUNT,
};
enum { MA_EQ_TYPE, MA_EQ_FREQ_HZ, MA_EQ_GAIN_DB, MA_EQ_Q, MA_EQ_ON };
#define MA_EQ_BAND(b, field) (MA_EQ_FIRST_BAND + (b) * MA_EQ_BAND_FIELDS + (field))
/* Band shapes (TPT state-variable forms). Gain applies to the bell and the
 * shelves; Q is the width of the bell and notch, the slope of the shelves
 * and the resonance of the cuts (.7071 is Butterworth, 12 dB/oct). */
typedef enum : uint8_t {
    MA_EQ_BELL, MA_EQ_LOW_SHELF, MA_EQ_HIGH_SHELF, MA_EQ_LOW_CUT, MA_EQ_HIGH_CUT, MA_EQ_NOTCH,
    MA_EQ_TYPE_COUNT,
} ma_eq_type;
typedef enum : uint8_t {
    MA_RACK_EMPTY = 0,
    MA_RACK_EQ = 1,
    MA_RACK_UTILITY = 2,
    MA_RACK_KIND_COUNT,
} ma_rack_kind;
/* STEPPED: integer values (the band count, a band's type); no ramp. */
typedef enum : uint8_t { MA_RACK_LINEAR, MA_RACK_LOG, MA_RACK_TOGGLE, MA_RACK_STEPPED } ma_rack_curve;
typedef struct {
    float minimum, maximum, initial;
    ma_rack_curve curve;
} ma_rack_param_info;
[[nodiscard]] unsigned ma_rack_param_count(ma_rack_kind kind);
[[nodiscard]] bool ma_rack_param_info_at(ma_rack_kind kind, unsigned index,
                                         ma_rack_param_info *info);

typedef struct {
    uint32_t id;                   /* nonzero and unique while occupied */
    ma_rack_kind kind;             /* MA_RACK_EMPTY: the slot is skipped */
    bool enabled;
    float mix;                     /* [0,1]; 1 is the block's full effect */
    float param[MA_RACK_PARAMS];   /* per-kind layout above; rest unused, 0 */
} ma_rack_slot_spec;
typedef struct { ma_rack_slot_spec slot[MA_RACK_SLOTS]; } ma_rack_spec;
/* An occupied, enabled slot of `kind` at its initial parameters, mix 1. */
void ma_rack_slot_init(ma_rack_slot_spec *slot, ma_rack_kind kind, uint32_t id);
/* Ids unique and nonzero, kinds known, every value finite and in range,
 * stepped and toggle values whole, unused parameters zero, EQ bands past
 * the count at their initial values. Empty slots must be all zero. */
[[nodiscard]] bool ma_rack_spec_valid(const ma_rack_spec *spec);
[[nodiscard]] bool ma_rack_spec_empty(const ma_rack_spec *spec);

typedef struct {
    float s1[2], s2[2];            /* TPT integrator states, L/R */
    float a1, a2, a3, m0, m1, m2;
    float weight;                  /* 0..1 crossfade: on/off and type change */
    uint8_t type;                  /* the shape the coefficients hold */
    bool active;
} ma_rack_svf;
typedef struct {
    uint32_t id;
    ma_rack_kind kind;
    bool live;
    bool moving;                   /* some parameter ramp may still run */
    uint8_t span;                  /* EQ bands to visit: in use or fading */
    float fade, fade_target;       /* structural crossfade, RKD6 */
    ma_smoother mix;               /* target is mix, or 0 while disabled */
    ma_smoother param[MA_RACK_PARAMS];
    ma_rack_svf svf[MA_EQ_BANDS];
} ma_rack_block;
typedef struct {
    ma_rack_block block[MA_RACK_SLOTS];  /* chain order; live ones first */
    ma_rack_block staging[MA_RACK_SLOTS]; /* adoption scratch (one rack per thread) */
    ma_rack_spec pending;          /* adopted once fade-outs reach zero */
    bool has_pending;
    float sample_rate_hz;
    float fade_step;
    uint16_t smooth_frames;
    uint64_t applies, structural_changes, adoptions, sanitizations;
    uint64_t lane_runs;            /* EQ runs taken by the AVX2 wavefront */
} ma_rack;
void ma_rack_init(ma_rack *rack, float sample_rate_hz);
/* Clears every block's history; keeps the chain and its targets. */
void ma_rack_reset(ma_rack *rack);
/* Between frames on the render thread. An invalid spec changes nothing and
 * returns false. Same id and kind keep their state (RKD6). */
bool ma_rack_apply(ma_rack *rack, const ma_rack_spec *spec);
[[nodiscard]] ma_frame ma_rack_tick(ma_rack *rack, ma_frame input);
/* True while a fade or a parameter ramp is still moving. */
[[nodiscard]] bool ma_rack_moving(const ma_rack *rack);
/* Finish every fade, ramp and pending adoption at once: for a host that
 * starts rendering, not for edits while sound is playing. */
void ma_rack_settle(ma_rack *rack);
/* n frames in place, the same bits as n calls of ma_rack_tick. Frames with
 * skip[i] set (skip may be null) pass untouched and do not tick the rack.
 * With `lanes` (the caller checked AVX2 at run time, as the pool does), a
 * settled EQ block runs as an AVX2 wavefront: lane k is band k on sample
 * t - k. */
void ma_rack_process(ma_rack *rack, ma_frame *frames, const bool *skip, size_t n, bool lanes);

typedef struct {
    ma_synth voice;
    uint64_t allocation_id;
    float detune_cents;
    float last_contribution;
    float transition_from;
    uint32_t transition_remaining;
    uint8_t group;
    bool awake;
} ma_pool_slot;

typedef struct {
    uint64_t allocation_id;
    uint8_t channel;
    uint8_t note;
    ma_card_phase phase;
} ma_pool_group;

typedef struct {
    uint64_t steals_released, steals_sustained, steals_held;
    uint64_t sleeps, wakes;
    unsigned active_slots, peak_slots;
} ma_pool_diagnostics;

/* Lane-major oscillator, decimator and ladder state of eight pool slots for
 * the vector render path. ma_voice_pool_render gathers awake slots here and
 * scatters them back, in the scalar representation, when it returns or when
 * a slot falls asleep; outside a render call the slots' ma_synth state is
 * authoritative. Ring index j holds the slot's ring entry (own position + j);
 * the shared positions restart at zero on every gather. No over-alignment:
 * pools may come from malloc, so the vector path uses unaligned loads. */
enum { MA_LANES = 8 };
typedef struct {
    uint64_t phase_q48[2][MA_LANES];
    uint64_t sync_run_q48[MA_LANES];             /* VCO2 only */
    float sync_carry[3][MA_LANES];
    float triangle[2][MA_LANES];
    float sync_residual[2][MA_LANES];
    float guard_gain[2][MA_LANES];
    float guard_target[2][MA_LANES];
    float guard_step[2][MA_LANES];
    int32_t guard_frames[2][MA_LANES];
    int32_t triangle_initialized[2][MA_LANES];   /* 0 or -1 */
    float rise[2][MA_LANES];
    float shaper_dc[2][MA_LANES];
    float history[2][31][MA_LANES];
    float output_history[7][MA_LANES];
    float ladder_state[4][MA_LANES];
    float y4_guess[MA_LANES];
    float pressure_input[MA_LANES];
    float pressure_output[MA_LANES];
    uint32_t reset_count[MA_LANES];
    uint8_t history_pos[2], output_pos;          /* shared by all lanes */
    uint8_t own_history_pos[2][MA_LANES], own_output_pos[MA_LANES];
    /* Glas (ma-constants.md 10C): formant SVF state and coefficients, and
     * the formant grains, lane-major. Grain index = formant * 4 + slot. */
    float glas_s1[MA_FORMANTS][MA_LANES], glas_s2[MA_FORMANTS][MA_LANES];
    float glas_g[MA_FORMANTS][MA_LANES], glas_k[MA_FORMANTS][MA_LANES];
    float grain_phase[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_step[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_envelope[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_decay[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_attack_pos[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_attack_step[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    float grain_gain[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];
    int32_t grain_active[MA_FORMANTS * MA_GRAINS_PER_FORMANT][MA_LANES];   /* 0 or -1 */
    uint32_t glas_epoch[MA_LANES];
    uint8_t active;                              /* gathered, still awake */
} ma_voice_lanes;

/* One run of the vector render (live-path step 7): the voice banks compute
 * up to MA_POOL_RUN frames ahead of the per-frame mix, bank by bank, so the
 * banks can run on several threads. */
enum { MA_POOL_RUN = 256, MA_POOL_BANKS = MA_POOL_CAPACITY / MA_LANES };

typedef struct {
    ma_pool_slot slot[MA_POOL_CAPACITY];
    ma_pool_group group[MA_POOL_CAPACITY];
    ma_card_pan pan[MA_POOL_CAPACITY];
    /* Control template only: never runs oscillator/filter DSP. */
    ma_synth control;
    ma_output_state output;
    ma_voice_pool_config config;
    ma_pool_diagnostics diagnostics;
    ma_effects effects;
    ma_rack *rack;                 /* caller-owned, attached after init; NULL = none */
    float bend[16], pressure[16], mod_wheel[16];
    bool sustain[16];
    uint64_t next_allocation;
    uint32_t transition_frames;
    float group_gain, pan_width, pan_dispersion;
    uint8_t cursor;
    bool lanes_enabled;            /* resolved from config.engine at init */
    ma_voice_lanes lanes[MA_POOL_CAPACITY / MA_LANES];
    /* Run state: each bank's voice outputs for the run (bank-major, so two
     * threads never share a cache line), the run frame at which a slot falls
     * asleep (-1 none), and which part computes each bank. */
    float run_out[MA_POOL_BANKS][MA_POOL_RUN][MA_LANES];
    int16_t run_sleep[MA_POOL_CAPACITY];
    uint8_t run_part[MA_POOL_BANKS];
    uint16_t run_frames;
} ma_voice_pool;
static_assert(sizeof(ma_voice_pool) < 1024u * 1024u);

/* NULL config/patch selects defaults. Invalid config leaves *pool untouched. */
[[nodiscard]] bool ma_voice_pool_init(ma_voice_pool *pool, float sample_rate_hz,
    const ma_voice_pool_config *config, const ma_patch *patch);
/* Retains current controls/configuration; clears notes, expression, tails,
 * counters and restarts deterministic session character/phase sequencing. */
void ma_voice_pool_reset(ma_voice_pool *pool);
[[nodiscard]] uint64_t ma_voice_pool_note_on(ma_voice_pool *pool,
    uint8_t channel, uint8_t note, uint8_t velocity);
[[nodiscard]] uint64_t ma_voice_pool_note_off(ma_voice_pool *pool,
    uint8_t channel, uint8_t note, uint8_t release_velocity);
void ma_voice_pool_set_sustain(ma_voice_pool *pool, uint8_t channel, bool down);
void ma_voice_pool_panic(ma_voice_pool *pool);
void ma_voice_pool_set_pitch_bend(ma_voice_pool *pool, uint8_t channel, float value);
void ma_voice_pool_set_channel_pressure(ma_voice_pool *pool, uint8_t channel, float value);
void ma_voice_pool_set_mod_wheel(ma_voice_pool *pool, uint8_t channel, float value);
void ma_voice_pool_set_poly_pressure(ma_voice_pool *pool, uint8_t channel,
    uint8_t note, float value);
/* Patch edits preserve ownership, envelopes, character and output histories. */
void ma_voice_pool_set_patch(ma_voice_pool *pool, const ma_patch *patch);
void ma_voice_pool_set_macro(ma_voice_pool *pool, ma_macro_id macro, float value);
void ma_voice_pool_set_character(ma_voice_pool *pool, float amount);
/* Glide follows each physical slot's last pitch; pitch freezes during sleep.
 * First allocation starts at its target. Reset forgets pitch history. */
void ma_voice_pool_set_glide(ma_voice_pool *pool, bool enabled, float seconds);
void ma_voice_pool_set_lfo(ma_voice_pool *pool, float depth, float rate_hz);
void ma_voice_pool_set_output(ma_voice_pool *pool, float drive, float width,
    float crossfeed, float master);
/* Insert the rack between the output DC block and the master fader. Init
 * detaches it; reset keeps it. NULL detaches. The rack's own state is never
 * touched by the pool. */
void ma_voice_pool_attach_rack(ma_voice_pool *pool, ma_rack *rack);
[[nodiscard]] ma_frame ma_voice_pool_tick(ma_voice_pool *pool);
/* Events are dispatched by the caller at block boundaries. */
void ma_voice_pool_render(ma_voice_pool *pool, ma_frame *frames, size_t count);

/* A host's way to run parts of a render on its own threads. run must call
 * job(arg, k) once for every k in [0, parts), on any thread and in any order,
 * and return when all have finished. The core starts no thread itself. */
typedef struct {
    void (*run)(void *context, void (*job)(void *arg, unsigned part), void *arg, unsigned parts);
    void *context;
    unsigned parts;                /* how many parts the host can run at once, >= 1 */
} ma_pool_executor;
/* ma_voice_pool_render with the vector engine's voice banks spread over the
 * executor's parts (live-path step 7); the same bits as ma_voice_pool_render
 * for any executor, or none. The scalar engine ignores the executor. */
void ma_voice_pool_render_parallel(ma_voice_pool *pool, ma_frame *frames, size_t count,
                                   const ma_pool_executor *executor);

#endif
