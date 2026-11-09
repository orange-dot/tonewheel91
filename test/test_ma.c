/* Core checks for the Mamut Analog voice, bank and sources. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"
#include "../src/mamutanalog.h"
#include "check.h"

static void test_ma_foundation(void) {
    CHECK(ma_note_frequency_hz(69) == 440.0f,
          "MA MIDI note 69 must be exactly 440 Hz");
    CHECK(ma_note_frequency_hz(128) == 0.0f
          && ma_note_frequency_hz(255) == 0.0f,
          "MA hostile note-table indices must return zero");

    double previous_hz = 0.0;
    double worst_pitch_relative = 0.0;
    for (int note = 0; note < 128; note++) {
        double got = ma_note_frequency_hz((uint8_t)note);
        double expected = 440.0 * pow(2.0, (note - 69) / 12.0);
        double relative = fabs(got / expected - 1.0);
        if (relative > worst_pitch_relative) worst_pitch_relative = relative;
        CHECK(got > previous_hz,
              "MA note table must rise at note %d: %.9g after %.9g",
              note, got, previous_hz);
        previous_hz = got;
    }
    CHECK(worst_pitch_relative < 1.0e-7,
          "MA note table worst relative error %.3g", worst_pitch_relative);

    CHECK(ma_velocity_level(0) == 0.0f
          && ma_velocity_filter(0) == 0.0f
          && ma_velocity_level(127) == 1.0f
          && ma_velocity_filter(127) == 1.0f,
          "MA velocity table endpoints");
    CHECK(ma_velocity_level(128) == 0.0f
          && ma_velocity_filter(255) == 0.0f,
          "MA hostile velocity-table indices must return zero");
    double previous_level = 0.0, previous_filter = 0.0;
    double worst_level_relative = 0.0, worst_filter_relative = 0.0;
    for (int velocity = 1; velocity < 128; velocity++) {
        double level = ma_velocity_level((uint8_t)velocity);
        double filter = ma_velocity_filter((uint8_t)velocity);
        double x = velocity / 127.0;
        double level_relative = fabs(level / pow(x, 0.78) - 1.0);
        double filter_relative = fabs(filter / pow(x, 1.08) - 1.0);
        if (level_relative > worst_level_relative)
            worst_level_relative = level_relative;
        if (filter_relative > worst_filter_relative)
            worst_filter_relative = filter_relative;
        CHECK(level > previous_level && filter > previous_filter,
              "MA velocity tables must rise at velocity %d", velocity);
        previous_level = level;
        previous_filter = filter;
    }
    CHECK(worst_level_relative < 1.0e-7
          && worst_filter_relative < 1.0e-7,
          "MA velocity table relative errors %.3g / %.3g",
          worst_level_relative, worst_filter_relative);

    ma_synth synth;
    ma_synth_init(&synth, 48000.0f);
    CHECK(synth.sample_rate_hz == 48000.0f,
          "MA sample rate must reach the public state");
    CHECK(synth.vco1.saw_level == 0.70f
          && synth.vco1.pulse_level == 0.25f
          && synth.vco1.triangle_level == 0.15f
          && synth.vco1.sine_level == 0.20f
          && synth.vco1.pulse_width == 0.50f,
          "MA VCO1 factory controls");
    CHECK(synth.vco2.saw_level == 0.35f
          && synth.vco2.pulse_level == 0.20f
          && synth.vco2.triangle_level == 0.55f
          && synth.vco2.pulse_width == 0.50f
          && synth.vco2_level == 0.62f
          && synth.vco2_interval == 0
          && synth.vco2_fine_cents == 7.0f,
          "MA VCO2 factory controls");
    CHECK(synth.sync_amount == 0.0f
          && synth.sync_softness == 0.0f
          && synth.crossmod_amount == 0.0f
          && synth.noise_level == 0.02f,
          "MA oscillator-modulation and noise defaults");
    CHECK(synth.mozaik_mix == 0.20f
          && synth.mozaik_contrast == 1.618034f
          && synth.mozaik_slope_q32 == UINT32_C(0x9e3779b9)
          && synth.mozaik_phason_q32 == 0
          && synth.mozaik_drift == 0.05f,
          "MA Mozaik factory controls");
    CHECK(synth.mixer_pressure == 0.15f
          && synth.filter_cutoff_hz == 900.0f
          && synth.filter_resonance == 0.18f
          && synth.filter_drive == 0.12f
          && synth.filter_env_amount == 0.30f
          && synth.filter_keytrack == 0.45f,
          "MA mixer/filter factory controls");
    CHECK(synth.amp_adsr.attack_ms == 600.0f
          && synth.amp_adsr.decay_ms == 1600.0f
          && synth.amp_adsr.sustain == 0.82f
          && synth.amp_adsr.release_ms == 3000.0f
          && synth.filter_adsr.attack_ms == 350.0f
          && synth.filter_adsr.decay_ms == 1800.0f
          && synth.filter_adsr.sustain == 0.50f
          && synth.filter_adsr.release_ms == 2600.0f,
          "MA envelope factory controls");
    CHECK(synth.amp_envelope.stage == MA_ENVELOPE_IDLE
          && synth.amp_envelope.level == 0.0f
          && synth.filter_envelope.stage == MA_ENVELOPE_IDLE
          && synth.filter_envelope.level == 0.0f,
          "MA envelopes must initialize idle at literal zero");
    CHECK(synth.body_drive == 0.10f && synth.width == 0.70f
          && synth.master_level == 0.18f
          && synth.noise_state == UINT64_C(0x4d414e4f49534531),
          "MA output and deterministic-noise defaults");
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        CHECK(synth.macro[macro] == 0.0f,
              "MA macro %d must initialize to exact bypass", macro);

    ma_synth lead;
    ma_synth_init_patch(&lead, 48000.0f, &ma_patch_lead);
    CHECK(lead.vco1.saw_level == 0.75f
          && lead.vco1.pulse_level == 0.30f
          && lead.vco1.triangle_level == 0.05f
          && lead.vco1.sine_level == 0.10f
          && lead.vco1.pulse_width == 0.43f,
          "MA Lead VCO1 controls");
    CHECK(lead.vco2.saw_level == 0.55f
          && lead.vco2.pulse_level == 0.25f
          && lead.vco2.triangle_level == 0.10f
          && lead.vco2.pulse_width == 0.57f
          && lead.vco2_level == 0.55f
          && lead.vco2_interval == 0
          && lead.vco2_fine_cents == 7.0f,
          "MA Lead VCO2 controls");
    CHECK(lead.sync_amount == 0.22f
          && lead.sync_softness == 0.18f
          && lead.crossmod_amount == 0.12f
          && lead.noise_level == 0.01f
          && lead.mozaik_mix == 0.08f
          && lead.mozaik_contrast_control == 0.60f
          && lead.mozaik_contrast == 1.72f
          && lead.mozaik_drift == 0.03f,
          "MA Lead source controls");
    CHECK(lead.mixer_pressure == 0.32f
          && lead.filter_cutoff_hz == 1900.0f
          && lead.filter_resonance == 0.30f
          && lead.filter_drive == 0.35f
          && lead.filter_env_amount == 0.62f
          && lead.filter_keytrack == 0.62f,
          "MA Lead filter controls");
    CHECK(lead.amp_adsr.attack_ms == 12.0f
          && lead.amp_adsr.decay_ms == 160.0f
          && lead.amp_adsr.sustain == 0.68f
          && lead.amp_adsr.release_ms == 240.0f
          && lead.filter_adsr.attack_ms == 6.0f
          && lead.filter_adsr.decay_ms == 260.0f
          && lead.filter_adsr.sustain == 0.18f
          && lead.filter_adsr.release_ms == 220.0f,
          "MA Lead envelope controls");
    CHECK(lead.macro[MA_MACRO_GRAVITACIJA] == 0.12f
          && lead.macro[MA_MACRO_BLOOM] == 0.04f
          && lead.macro[MA_MACRO_HEAT] == 0.25f
          && lead.macro[MA_MACRO_RUIN] == 0.22f
          && lead.macro[MA_MACRO_SWARM] == 0.03f
          && lead.body_drive == 0.15f
          && lead.width == 0.45f
          && lead.crossfeed == 0.08f
          && lead.master_level == 0.18f,
          "MA Lead identity and output controls");

    ma_synth dubina;
    ma_synth_init_patch(&dubina, 48000.0f, &ma_patch_dubina);
    CHECK(dubina.vco2.sine_level == 0.85f
          && dubina.vco2_level == 0.90f
          && dubina.vco2_interval == -12
          && dubina.vco1.sine_level == 0.15f,
          "MA Dubina must be VCO2-sine dominant");
    ma_synth_note_on(&dubina, 0, 48, 100);
    for (int frame = 0; frame < 32; frame++) (void)ma_synth_tick(&dubina);
    ma_synth_apply_patch(&dubina, &ma_patch_lead);
    CHECK(!dubina.note_active && dubina.sample_rate_hz == 48000.0f
          && dubina.vco1.saw_level == 0.75f
          && dubina.amp_envelope.stage == MA_ENVELOPE_IDLE
          && dubina.oscillator1.phase_q48 == 0,
          "MA patch apply must reset the voice at the period boundary");

    ma_patch hostile_patch = ma_patch_tepih;
    hostile_patch.vco1.sine_level = INFINITY;
    hostile_patch.vco2.sine_level = -1.0f;
    hostile_patch.filter_cutoff_hz = INFINITY;
    hostile_patch.vco2_interval = 99;
    ma_synth_init_patch(&dubina, 48000.0f, &hostile_patch);
    CHECK(dubina.vco1.sine_level == 0.20f
          && dubina.vco2.sine_level == 0.0f
          && dubina.filter_cutoff_hz == 900.0f
          && dubina.vco2_interval == 24,
          "MA hostile patch values must sanitize to the core contract");

    ma_synth null_patch;
    ma_synth_init_patch(&null_patch, 48000.0f, nullptr);
    CHECK(null_patch.vco1.sine_level == 0.20f
          && null_patch.filter_cutoff_hz == 900.0f,
          "MA null patch must select Tepih");

    ma_synth hostile;
    ma_synth_init(&hostile, NAN);
    CHECK(hostile.sample_rate_hz == 48000.0f,
          "MA non-finite sample rate must select 48 kHz");
    ma_synth_init(&hostile, 192001.0f);
    CHECK(hostile.sample_rate_hz == 48000.0f,
          "MA out-of-domain sample rate must select 48 kHz");
    ma_synth_init(&hostile, 44100.0f);
    CHECK(hostile.sample_rate_hz == 44100.0f,
          "MA lower sample-rate boundary must be accepted");

    ma_synth_set_amp_adsr(&synth, (ma_adsr){
        .attack_ms = -4.0f,
        .decay_ms = NAN,
        .sustain = 3.0f,
        .release_ms = INFINITY,
    });
    CHECK(synth.amp_adsr.attack_ms == 1.0f
          && synth.amp_adsr.decay_ms == 1600.0f
          && synth.amp_adsr.sustain == 1.0f
          && synth.amp_adsr.release_ms == 3000.0f,
          "MA amp ADSR must clamp finite values and default non-finite ones");
    ma_synth_set_filter_adsr(&synth, (ma_adsr){
        .attack_ms = 25000.0f,
        .decay_ms = 0.0f,
        .sustain = -1.0f,
        .release_ms = 25000.0f,
    });
    CHECK(synth.filter_adsr.attack_ms == 20000.0f
          && synth.filter_adsr.decay_ms == 1.0f
          && synth.filter_adsr.sustain == 0.0f
          && synth.filter_adsr.release_ms == 20000.0f,
          "MA filter ADSR must clamp to its public domain");
    ma_synth_set_filter_modulation(&synth, NAN, 2.0f);
    CHECK(synth.filter_env_amount == 0.30f
          && synth.filter_keytrack == 1.0f,
          "MA filter modulation must default non-finite and clamp finite input");
    ma_synth_set_filter_modulation(&synth, -1.0f, INFINITY);
    CHECK(synth.filter_env_amount == 0.0f
          && synth.filter_keytrack == 0.45f,
          "MA filter modulation must clamp amount and default keytrack");

    ma_synth_set_macro(&synth, MA_MACRO_GRAVITACIJA, 2.0f);
    ma_synth_set_macro(&synth, MA_MACRO_BLOOM, NAN);
    CHECK(synth.macro[MA_MACRO_GRAVITACIJA] == 1.0f
          && synth.macro[MA_MACRO_BLOOM] == 0.0f,
          "MA macro setter must sanitize to [0,1]");
    float macros_before[MA_MACRO_COUNT];
    memcpy(macros_before, synth.macro, sizeof macros_before);
    ma_synth_set_macro(&synth, (ma_macro_id)MA_MACRO_COUNT, 0.5f);
    CHECK(memcmp(macros_before, synth.macro, sizeof macros_before) == 0,
          "MA invalid macro identifiers must be a no-op");
    ma_synth_set_output(&synth, NAN, 2.0f, -1.0f, INFINITY);
    CHECK(synth.body_drive == 0.10f && synth.width == 1.0f
          && synth.crossfeed == 0.0f && synth.master_level == 0.18f,
          "MA output setter must default non-finite and clamp finite input");
    CHECK(sizeof synth < 1024u * 1024u,
          "MA caller-owned state unexpectedly large: %zu bytes", sizeof synth);
}

static void test_ma_oscillators(void) {
    static const ma_vco_controls saw_only = {
        .saw_level = 1.0f,
        .pulse_width = 0.5f,
    };
    static const ma_vco_controls silent = {
        .pulse_width = 0.5f,
    };

    ma_synth synth;
    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_mozaik(&synth, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_synth_set_filter(&synth, 20000.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(&synth, 0.0f, 0.0f);
    ma_synth_set_filter_adsr(&synth, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_set_vco1(&synth, saw_only);
    ma_synth_set_vco2(&synth, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&synth, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_note_on(&synth, 0, 69, 127);
    for (int i = 0; i < 1024; i++) (void)ma_synth_tick(&synth);
    int rising = 0;
    float peak = 0.0f;
    (void)ma_synth_tick(&synth);
    float previous = synth.ladder.pressure_input;
    for (int i = 1; i < 48000; i++) {
        ma_frame frame = ma_synth_tick(&synth);
        CHECK(frame.left == frame.right && isfinite(frame.left),
              "MA source tick %d must be finite dual mono", i);
        float source = synth.ladder.pressure_input;
        if (previous <= 0.0f && source > 0.0f) rising++;
        if (fabsf(source) > peak) peak = fabsf(source);
        previous = source;
    }
    CHECK(rising >= 438 && rising <= 442,
          "MA VCO1 A4 produced %d rising crossings", rising);
    CHECK(peak > 0.90f && peak <= 1.25f,
          "MA PolyBLEP saw peak %.6f outside its contract", (double)peak);

    ma_synth_set_vco1(&synth, (ma_vco_controls){
        .sine_level = 1.0f,
        .pulse_width = 0.5f,
    });
    for (int i = 0; i < 1024; i++) (void)ma_synth_tick(&synth);
    rising = 0;
    peak = 0.0f;
    (void)ma_synth_tick(&synth);
    previous = synth.ladder.pressure_input;
    for (int i = 1; i < 48000; i++) {
        (void)ma_synth_tick(&synth);
        float source = synth.ladder.pressure_input;
        if (previous <= 0.0f && source > 0.0f) rising++;
        if (fabsf(source) > peak) peak = fabsf(source);
        previous = source;
    }
    CHECK(rising >= 438 && rising <= 442,
          "MA VCO1 sine A4 produced %d rising crossings", rising);
    CHECK(peak > 0.99f && peak <= 1.01f,
          "MA VCO1 sine peak %.6f outside its contract", (double)peak);

    ma_synth_set_vco1(&synth, (ma_vco_controls){
        .saw_level = NAN,
        .pulse_level = -2.0f,
        .triangle_level = 8.0f,
        .sine_level = INFINITY,
        .pulse_width = INFINITY,
    });
    CHECK(synth.vco1.saw_level == 0.70f
          && synth.vco1.pulse_level == 0.0f
          && synth.vco1.triangle_level == 1.0f
          && synth.vco1.sine_level == 0.20f
          && synth.vco1.pulse_width == 0.50f,
          "MA VCO1 setter must default non-finite and clamp finite input");
    ma_synth_set_vco2(&synth, (ma_vco_controls){
        .saw_level = -1.0f,
        .pulse_level = 2.0f,
        .triangle_level = NAN,
        .sine_level = 2.0f,
        .pulse_width = 0.99f,
    }, INFINITY, -99, -80.0f);
    CHECK(synth.vco2.saw_level == 0.0f
          && synth.vco2.pulse_level == 1.0f
          && synth.vco2.triangle_level == 0.55f
          && synth.vco2.sine_level == 1.0f
          && synth.vco2.pulse_width == 0.95f
          && synth.vco2_level == 0.62f
          && synth.vco2_interval == -24
          && synth.vco2_fine_cents == -50.0f,
          "MA VCO2 grouped setter sanitization");
    ma_synth_set_oscillator_modulation(&synth, INFINITY, -1.0f, 2.0f, NAN);
    CHECK(synth.sync_amount == 0.0f && synth.sync_softness == 0.0f
          && synth.crossmod_amount == 1.0f && synth.noise_level == 0.02f,
          "MA oscillator-modulation setter sanitization");

    ma_synth_init(&synth, 48000.0f);
    ma_synth_note_on(&synth, 0, 60, 200);
    CHECK(synth.note_active && synth.note == 60 && synth.velocity == 127,
          "MA note-on must accept the note and clamp hostile velocity");
    ma_synth_note_off(&synth, 0, 61, 0);
    CHECK(synth.note_active, "MA nonmatching note-off must not release");
    ma_synth_note_on(&synth, 0, 200, 100);
    CHECK(synth.note_active && synth.note == 60,
          "MA hostile note-on must leave the voice unchanged");
    ma_synth_note_on(&synth, 0, 60, 0);
    CHECK(!synth.note_active, "MA velocity-zero note-on must release");
    CHECK(ma_synth_tick(&synth).left == 0.0f,
          "MA released source must be inaudible while state advances");

    ma_synth no_sync, zero_sync;
    ma_synth_init(&no_sync, 48000.0f);
    ma_synth_init(&zero_sync, 48000.0f);
    ma_synth_set_oscillator_modulation(&no_sync, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_oscillator_modulation(&zero_sync, 0.0f, 1.0f, 0.0f, 0.0f);
    ma_synth_note_on(&no_sync, 0, 96, 127);
    ma_synth_note_on(&zero_sync, 0, 96, 127);
    int sync_zero_mismatch = 0;
    for (int i = 0; i < 12000; i++) {
        ma_frame a = ma_synth_tick(&no_sync);
        ma_frame b = ma_synth_tick(&zero_sync);
        sync_zero_mismatch += memcmp(&a, &b, sizeof a) != 0;
    }
    CHECK(sync_zero_mismatch == 0,
          "MA sync amount zero must skip reset and residual exactly");

    ma_synth free_run, hard_sync;
    ma_synth_init(&free_run, 48000.0f);
    ma_synth_init(&hard_sync, 48000.0f);
    ma_synth_set_vco1(&free_run, silent);
    ma_synth_set_vco1(&hard_sync, silent);
    ma_synth_set_vco2(&free_run, saw_only, 1.0f, 7, 0.0f);
    ma_synth_set_vco2(&hard_sync, saw_only, 1.0f, 7, 0.0f);
    ma_synth_set_oscillator_modulation(&free_run, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_oscillator_modulation(&hard_sync, 1.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_note_on(&free_run, 0, 72, 127);
    ma_synth_note_on(&hard_sync, 0, 72, 127);
    int hard_sync_delta = 0;
    for (int i = 0; i < 12000; i++) {
        ma_frame a = ma_synth_tick(&free_run);
        ma_frame b = ma_synth_tick(&hard_sync);
        hard_sync_delta += memcmp(&a, &b, sizeof a) != 0;
        CHECK(isfinite(b.left) && fabsf(b.left) <= 1.25f,
              "MA hard-sync sample %d escaped its bound", i);
    }
    CHECK(hard_sync_delta > 1000,
          "MA full sync must audibly change the slave, %d samples differ",
          hard_sync_delta);

    ma_synth plain, crossed;
    ma_synth_init(&plain, 48000.0f);
    ma_synth_init(&crossed, 48000.0f);
    ma_synth_set_vco1(&plain, saw_only);
    ma_synth_set_vco1(&crossed, saw_only);
    ma_synth_set_vco2(&plain, saw_only, 0.0f, 12, 0.0f);
    ma_synth_set_vco2(&crossed, saw_only, 0.0f, 12, 0.0f);
    ma_synth_set_oscillator_modulation(&plain, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_oscillator_modulation(&crossed, 0.0f, 0.0f, 1.0f, 0.0f);
    ma_synth_note_on(&plain, 0, 84, 127);
    ma_synth_note_on(&crossed, 0, 84, 127);
    int crossmod_delta = 0;
    for (int i = 0; i < 12000; i++) {
        ma_frame a = ma_synth_tick(&plain);
        ma_frame b = ma_synth_tick(&crossed);
        crossmod_delta += memcmp(&a, &b, sizeof a) != 0;
        CHECK(isfinite(b.left) && fabsf(b.left) <= 1.25f,
              "MA cross-mod sample %d escaped its bound", i);
    }
    CHECK(crossmod_delta > 1000,
          "MA cross-mod must alter VCO1, %d samples differ", crossmod_delta);

    ma_synth noise_off, noise_on;
    ma_synth_init(&noise_off, 48000.0f);
    ma_synth_init(&noise_on, 48000.0f);
    ma_synth_set_vco1(&noise_off, silent);
    ma_synth_set_vco1(&noise_on, silent);
    ma_synth_set_vco2(&noise_off, silent, 0.0f, 0, 0.0f);
    ma_synth_set_vco2(&noise_on, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&noise_off, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_oscillator_modulation(&noise_on, 0.0f, 0.0f, 0.0f, 1.0f);
    ma_synth_note_on(&noise_off, 0, 60, 127);
    ma_synth_note_on(&noise_on, 0, 60, 127);
    for (int i = 0; i < 1000; i++) {
        (void)ma_synth_tick(&noise_off);
        (void)ma_synth_tick(&noise_on);
    }
    ma_synth_set_oscillator_modulation(&noise_off, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(memcmp(&noise_off.noise_state, &noise_on.noise_state,
                 sizeof noise_on.noise_state) == 0,
          "MA noise timeline must advance while its level is zero");
    /* Flush the VCO boundary. The downstream nonlinear ladder legitimately
     * remembers the earlier source level, so compare its exact input. */
    for (int i = 0; i < 316; i++) {
        (void)ma_synth_tick(&noise_off);
        (void)ma_synth_tick(&noise_on);
    }
    for (int i = 0; i < 972; i++) {
        (void)ma_synth_tick(&noise_off);
        (void)ma_synth_tick(&noise_on);
        CHECK(memcmp(&noise_off.ladder.pressure_input,
                     &noise_on.ladder.pressure_input,
                     sizeof noise_on.ladder.pressure_input) == 0,
              "MA noise timeline diverged after zero-level frame %d", i);
    }

    ma_synth guarded;
    ma_synth_init(&guarded, 44100.0f);
    ma_synth_set_vco1(&guarded, silent);
    ma_synth_set_vco2(&guarded, saw_only, 1.0f, 24, 0.0f);
    ma_synth_set_oscillator_modulation(&guarded, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_note_on(&guarded, 0, 127, 127);
    for (int i = 0; i < 512; i++) (void)ma_synth_tick(&guarded);
    CHECK(guarded.oscillator2.guard_gain == 0.0f
          && guarded.oscillator2.guard_frames == 0
          && guarded.ladder.pressure_input == 0.0f,
          "MA source guard must fade an out-of-domain VCO to exact zero");
}

static uint64_t ma_mozaik_kinds(ma_synth *synth, int count) {
    uint64_t kinds = 0;
    for (int tile = 0; tile < count; tile++) {
        synth->mozaik.tile_pos = synth->mozaik.tile_len;
        (void)ma_synth_tick(synth);
        if (synth->mozaik.tile_sign > 0.0f)
            kinds |= UINT64_C(1) << tile;
    }
    return kinds;
}

static void ma_mozaik_reseat(ma_synth *synth, uint32_t slope,
                             uint32_t phason) {
    ma_synth_init(synth, 48000.0f);
    ma_synth_set_mozaik(synth, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    synth->mozaik_slope_q32 = slope;
    float sigma = (float)slope / 0x1p32f;
    synth->smoothers.mozaik_slope = (ma_smoother){
        .current = (sigma - 0.45f) / 0.30f,
        .target = (sigma - 0.45f) / 0.30f,
    };
    synth->smoothers.mozaik_drift = (ma_smoother){ 0 };
    synth->mozaik = (ma_mozaik){
        .frac_q32 = phason,
        .applied_phason_q32 = phason,
        .tile_sign = 1.0f,
        .guard_gain = 1.0f,
        .guard_target = 1.0f,
    };
}

static void test_ma_mozaik(void) {
    static const uint32_t slopes[5] = {
        UINT32_C(0x80000000), UINT32_C(0x9999999a),
        UINT32_C(0x9e3779b9), UINT32_C(0xa0000000),
        UINT32_C(0xaaaaaaab),
    };
    static const uint32_t phasons[3] = {
        0, UINT32_C(0x12345678), UINT32_C(0xdeadbeef),
    };
    static const uint64_t expected_kinds[5][3] = {
        { UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0xaaaaaaaaaaaaaaaa),
          UINT64_C(0x5555555555555555) },
        { UINT64_C(0xad6b5ad6b5ad6b5a), UINT64_C(0xad6b5ad6b5ad6b5a),
          UINT64_C(0xb5ad6b5ad6b5ad6b) },
        { UINT64_C(0xadad6d6b6b6b5b5a), UINT64_C(0x6d6d6b6b6b5b5ada),
          UINT64_C(0xb5adadad6d6d6b6b) },
        { UINT64_C(0xdadadadadadadada), UINT64_C(0xdadadadadadadada),
          UINT64_C(0x6b6b6b6b6b6b6b6b) },
        { UINT64_C(0x6db6db6db6db6db6), UINT64_C(0x6db6db6db6db6db6),
          UINT64_C(0xb6db6db6db6db6db) },
    };
    static const uint32_t expected_frac[5][3] = {
        { 0, UINT32_C(0x12345678), UINT32_C(0xdeadbeef) },
        { UINT32_C(0x66666680), UINT32_C(0x789abcf8),
          UINT32_C(0x4514256f) },
        { UINT32_C(0x8dde6e40), UINT32_C(0xa012c4b8),
          UINT32_C(0x6c8c2d2f) },
        { 0, UINT32_C(0x12345678), UINT32_C(0xdeadbeef) },
        { UINT32_C(0xaaaaaac0), UINT32_C(0xbcdf0138),
          UINT32_C(0x895869af) },
    };

    ma_synth synth;
    for (size_t slope = 0; slope < 5; slope++) {
        for (size_t phason = 0; phason < 3; phason++) {
            ma_mozaik_reseat(&synth, slopes[slope], phasons[phason]);
            uint64_t kinds = ma_mozaik_kinds(&synth, 64);
            CHECK(kinds == expected_kinds[slope][phason],
                  "MA Mozaik donor word %zu/%zu: %016llx",
                  slope, phason, (unsigned long long)kinds);
            CHECK(synth.mozaik.frac_q32 == expected_frac[slope][phason],
                  "MA Mozaik donor fraction %zu/%zu: %08x",
                  slope, phason, synth.mozaik.frac_q32);
        }
    }

    ma_mozaik_reseat(&synth, UINT32_C(0x9e3779b9), UINT32_C(0x01020304));
    CHECK(ma_mozaik_kinds(&synth, 16) == UINT64_C(0x5b5a),
          "MA Mozaik pre-phason golden word");
    synth.mozaik.pending_phason_q32 = synth.mozaik.applied_phason_q32
                                    + UINT32_C(0x10203040);
    synth.mozaik.has_pending_phason = true;
    CHECK(ma_mozaik_kinds(&synth, 16) == UINT64_C(0x6b5b)
          && synth.mozaik.frac_q32 == UINT32_C(0xd8116a64),
          "MA Mozaik boundary-phason donor golden");

    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_mozaik(&synth, -1.0f, 1.0f / 6.0f, 2.0f,
                        -0.25f, 2.0f);
    CHECK(synth.mozaik_mix == 0.0f
          && synth.mozaik_slope_q32 == UINT32_C(0x80000000)
          && synth.mozaik_contrast == 2.2f
          && synth.mozaik_phason_q32 == UINT32_C(0xc0000000)
          && synth.mozaik_drift == 1.0f,
          "MA Mozaik grouped setter maps and clamps controls");
    ma_synth_set_mozaik(&synth, NAN, NAN, NAN, INFINITY, NAN);
    CHECK(synth.mozaik_mix == 0.20f
          && synth.mozaik_slope_q32 == UINT32_C(0x9e3779b9)
          && synth.mozaik_contrast == 1.618034f
          && synth.mozaik_phason_q32 == 0
          && synth.mozaik_drift == 0.05f,
          "MA Mozaik non-finite controls restore factory values");

    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_mozaik(&synth, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 1.0f);
    for (int frame = 0; frame < 288; frame++) (void)ma_synth_tick(&synth);
    ma_synth_note_on(&synth, 0, 69, 127);
    uint32_t drift_step = (uint32_t)(0.5f * 0x1p32f / 96000.0f + 0.5f);
    for (int frame = 0; frame < 8; frame++) (void)ma_synth_tick(&synth);
    CHECK(synth.mozaik.applied_phason_q32 == drift_step
          && synth.mozaik.pending_phason_q32 == 16u * drift_step
          && synth.mozaik.has_pending_phason,
          "MA Mozaik drift requests must accumulate within a tile");

    ma_synth early, late;
    ma_synth_init(&early, 48000.0f);
    ma_synth_init(&late, 48000.0f);
    ma_synth_set_mozaik(&early, 1.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&late, 1.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    early.smoothers.mozaik_drift = (ma_smoother){ 0 };
    late.smoothers.mozaik_drift = (ma_smoother){ 0 };
    ma_synth_note_on(&early, 0, 69, 127);
    ma_synth_note_on(&late, 0, 69, 127);
    for (int frame = 0; frame < 500; frame++) {
        if (frame == 2)
            ma_synth_set_mozaik(&early, 1.0f, 0.5601133f, 0.5150284f,
                                0.25f, 0.0f);
        if (frame == 12)
            ma_synth_set_mozaik(&late, 1.0f, 0.5601133f, 0.5150284f,
                                0.25f, 0.0f);
        ma_frame a = ma_synth_tick(&early);
        ma_frame b = ma_synth_tick(&late);
        CHECK(memcmp(&a, &b, sizeof a) == 0,
              "MA Mozaik phason split an active tile at frame %d", frame);
    }
    CHECK(early.mozaik.applied_phason_q32 == UINT32_C(0x40000000)
          && late.mozaik.applied_phason_q32 == UINT32_C(0x40000000),
          "MA Mozaik pending phason must land at a tile boundary");

    ma_synth dry, moved;
    ma_synth_init(&dry, 48000.0f);
    ma_synth_init(&moved, 48000.0f);
    ma_synth_set_mozaik(&dry, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&moved, 0.0f, 1.0f, 1.0f, 0.75f, 1.0f);
    ma_synth_note_on(&dry, 0, 57, 111);
    ma_synth_note_on(&moved, 0, 57, 111);
    uint64_t dry_hash = 0;
    for (int frame = 0; frame < 12000; frame++) {
        ma_frame a = ma_synth_tick(&dry);
        ma_frame b = ma_synth_tick(&moved);
        CHECK(memcmp(&a, &b, sizeof a) == 0,
              "MA Mozaik mix zero changed frame %d", frame);
        ma_frame pre_output = {
            .left = dry.output.pre_body,
            .right = dry.output.pre_body,
        };
        dry_hash = tw_fnv1a64(&pre_output, sizeof pre_output, dry_hash);
    }
    /* Re-pinned 2026-11-09 for explicit FMA and the folded halfband FIR
     * (was b9ab7015dda6ead5; docs/ma3-fma-20261109.md, ma3-fir-fold-20261109.md). */
    CHECK(dry_hash == UINT64_C(0xb2e59619a2a55cb1),
          "MA1-5 zero-Mozaik envelope/VCA baseline is %016llx",
          (unsigned long long)dry_hash);

    ma_synth legacy;
    ma_synth_init(&legacy, 48000.0f);
    legacy.vco1.sine_level = 0.0f;
    legacy.smoothers.vco1.sine_level = (ma_smoother){ 0 };
    ma_synth_set_mozaik(&legacy, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_note_on(&legacy, 0, 57, 111);
    uint64_t legacy_hash = 0;
    for (int frame = 0; frame < 12000; frame++) {
        (void)ma_synth_tick(&legacy);
        ma_frame pre_output = {
            .left = legacy.output.pre_body,
            .right = legacy.output.pre_body,
        };
        legacy_hash = tw_fnv1a64(&pre_output, sizeof pre_output, legacy_hash);
    }
    /* Re-pinned 2026-11-09 for explicit FMA and the folded FIR (was 48685bb104788f11). */
    CHECK(legacy_hash == UINT64_C(0xa63f63bdc633854d),
          "MA VCO1 sine zero must retain the pre-sine PCM anchor: %016llx",
          (unsigned long long)legacy_hash);

    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_vco1(&synth, (ma_vco_controls){ .pulse_width = 0.5f });
    ma_synth_set_vco2(&synth, (ma_vco_controls){ .pulse_width = 0.5f },
                      0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&synth, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&synth, 1.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_synth_note_on(&synth, 0, 0, 127);
    for (int frame = 0; frame < 512; frame++) (void)ma_synth_tick(&synth);
    CHECK(synth.mozaik.guard_gain == 0.0f
          && synth.mozaik.guard_frames == 0
          && synth.ladder.pressure_input == 0.0f,
          "MA Mozaik source guard must mute out-of-domain fundamentals");
}

static void ma_filter_silence(ma_synth *synth, float cutoff_hz,
                              float resonance) {
    static const ma_vco_controls silent = { .pulse_width = 0.5f };
    ma_synth_init(synth, 48000.0f);
    ma_synth_set_vco1(synth, silent);
    ma_synth_set_vco2(synth, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(synth, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(synth, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_synth_set_filter(synth, cutoff_hz, resonance, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(synth, 0.0f, 0.0f);
    ma_synth_set_filter_adsr(synth, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_note_on(synth, 0, 69, 127);
    synth->ladder.state[0] = 0.001f;
}

static double ma_filter_rms(float resonance) {
    ma_synth synth;
    ma_filter_silence(&synth, 440.0f, resonance);
    for (int frame = 0; frame < 96000; frame++) (void)ma_synth_tick(&synth);
    double square_sum = 0.0;
    for (int frame = 0; frame < 48000; frame++) {
        double sample = ma_synth_tick(&synth).left;
        square_sum += sample * sample;
    }
    return sqrt(square_sum / 48000.0);
}

static void test_ma_filter(void) {
    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    static const float cutoff_control[] = {
        20.0f, 65.4064f, 261.626f, 900.0f, 3000.0f, 10000.0f, 20000.0f,
    };
    double worst_cutoff_relative = 0.0;
    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        for (size_t cutoff = 0;
             cutoff < sizeof cutoff_control / sizeof *cutoff_control;
             cutoff++) {
            ma_synth synth;
            ma_synth_init(&synth, rates[rate]);
            ma_synth_set_filter(&synth, cutoff_control[cutoff], 0.0f,
                                0.0f, 0.0f);
            double tangent = synth.filter_g / (1.0 - synth.filter_g);
            double recovered = 2.0 * rates[rate] * atan(tangent)
                             / (0.5 * TAU_D);
            double relative = fabs(recovered / synth.filter_cutoff_hz - 1.0);
            if (relative > worst_cutoff_relative)
                worst_cutoff_relative = relative;
        }
    }
    CHECK(worst_cutoff_relative < 0.02,
          "MA ladder cutoff recovery error %.6f%%",
          100.0 * worst_cutoff_relative);

    ma_synth dry, pressured;
    ma_synth_init(&dry, 48000.0f);
    ma_synth_init(&pressured, 48000.0f);
    ma_synth_set_filter(&dry, 900.0f, 0.18f, 0.12f, 0.0f);
    ma_synth_set_filter(&pressured, 900.0f, 0.18f, 0.12f, 1.0f);
    ma_synth_note_on(&dry, 0, 57, 127);
    ma_synth_note_on(&pressured, 0, 57, 127);
    int pressure_delta = 0;
    for (int frame = 0; frame < 12000; frame++) {
        (void)ma_synth_tick(&dry);
        (void)ma_synth_tick(&pressured);
        CHECK(memcmp(&dry.ladder.pressure_input,
                     &dry.ladder.pressure_output,
                     sizeof dry.ladder.pressure_input) == 0,
              "MA pressure zero changed source frame %d", frame);
        pressure_delta += memcmp(&pressured.ladder.pressure_input,
                                 &pressured.ladder.pressure_output,
                                 sizeof pressured.ladder.pressure_input) != 0;
    }
    CHECK(pressure_delta > 1000,
          "MA pressure endpoint changed only %d source frames", pressure_delta);

    static const float c_frequency[] = {
        65.4064f, 130.813f, 261.626f, 523.251f, 1046.50f, 2093.00f,
    };
    for (size_t note = 0; note < sizeof c_frequency / sizeof *c_frequency;
         note++) {
        ma_synth synth;
        ma_filter_silence(&synth, c_frequency[note], 1.0f);
        for (int frame = 0; frame < 96000; frame++)
            (void)ma_synth_tick(&synth);
        int rising = 0;
        float peak = 0.0f;
        float previous = ma_synth_tick(&synth).left;
        for (int frame = 1; frame < 48000; frame++) {
            float sample = ma_synth_tick(&synth).left;
            rising += previous <= 0.0f && sample > 0.0f;
            if (fabsf(sample) > peak) peak = fabsf(sample);
            previous = sample;
        }
        double cents = 1200.0 * log2(rising / (double)c_frequency[note]);
        CHECK(fabs(cents) < 30.0,
              "MA self-oscillation C%zu tuning %.3f cents", note + 2, cents);
        CHECK(peak <= 1.0f && synth.ladder.reset_count == 0,
              "MA self-oscillation C%zu escaped bounds", note + 2);
    }

    double previous_rms = ma_filter_rms(0.86f);
    for (int step = 1; step <= 7; step++) {
        float resonance = 0.86f + 0.02f * step;
        double rms = ma_filter_rms(resonance);
        CHECK(rms > previous_rms,
              "MA self-oscillation RMS fell at resonance %.2f: %.9f <= %.9f",
              (double)resonance, rms, previous_rms);
        previous_rms = rms;
    }

    ma_synth hostile;
    ma_synth_init(&hostile, 48000.0f);
    ma_synth_set_filter(&hostile, NAN, INFINITY, -1.0f, 2.0f);
    CHECK(hostile.filter_cutoff_hz == 900.0f
          && hostile.filter_resonance == 0.18f
          && hostile.filter_drive == 0.0f
          && hostile.mixer_pressure == 1.0f,
          "MA filter setter must default non-finite and clamp finite controls");
    ma_vco_controls silent = { .pulse_width = 0.5f };
    ma_synth_set_vco1(&hostile, silent);
    ma_synth_set_vco2(&hostile, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&hostile, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&hostile, 0.0f, 0.5601133f, 0.5150284f,
                        0.0f, 0.0f);
    for (int frame = 0; frame < 512; frame++) (void)ma_synth_tick(&hostile);
    hostile.ladder.state[2] = NAN;
    ma_synth_note_on(&hostile, 0, 60, 127);
    (void)ma_synth_tick(&hostile);
    CHECK(hostile.ladder.reset_count == 1
          && hostile.ladder.state[0] == 0.0f
          && hostile.ladder.state[1] == 0.0f
          && hostile.ladder.state[2] == 0.0f
          && hostile.ladder.state[3] == 0.0f
          && hostile.ladder.y4_guess == 0.0f,
          "MA non-finite ladder state must reset deterministically");

    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_synth synth;
        ma_synth_init(&synth, rates[rate]);
        ma_synth_set_filter(&synth, 1.0e9f, 1.0f, 1.0f, 1.0f);
        ma_synth_note_on(&synth, 0, 127, 255);
        float peak = 0.0f;
        bool finite = true;
        for (int frame = 0; frame < 12000; frame++) {
            ma_frame output = ma_synth_tick(&synth);
            finite = finite && isfinite(output.left) && isfinite(output.right);
            if (fabsf(output.left) > peak) peak = fabsf(output.left);
        }
        CHECK(finite && peak <= 1.0f && synth.ladder.reset_count == 0,
              "MA hostile ladder sweep at %.0f Hz: finite=%d peak=%.6f resets=%u",
              (double)rates[rate], finite, (double)peak,
              synth.ladder.reset_count);
    }
}

static void test_ma_envelopes(void) {
    static const ma_adsr timing = {
        .attack_ms = 10.0f,
        .decay_ms = 20.0f,
        .sustain = 0.40f,
        .release_ms = 30.0f,
    };
    ma_synth synth;
    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_amp_adsr(&synth, timing);
    ma_synth_set_filter_adsr(&synth, timing);
    ma_synth_note_on(&synth, 0, 60, 127);
    CHECK(synth.amp_envelope.stage == MA_ENVELOPE_ATTACK
          && synth.filter_envelope.stage == MA_ENVELOPE_ATTACK
          && synth.amp_envelope.level == 0.0f,
          "MA NoteOn must attack from the current envelope level");

    int attack_frames = 0;
    while (synth.amp_envelope.stage == MA_ENVELOPE_ATTACK
           && attack_frames < 1000) {
        (void)ma_synth_tick(&synth);
        attack_frames++;
    }
    CHECK(attack_frames >= 478 && attack_frames <= 482
          && synth.amp_envelope.stage == MA_ENVELOPE_DECAY
          && synth.amp_envelope.level == 1.0f,
          "MA 10 ms attack closed in %d frames", attack_frames);

    int decay_frames = 0;
    while (synth.amp_envelope.stage == MA_ENVELOPE_DECAY
           && decay_frames < 2000) {
        (void)ma_synth_tick(&synth);
        decay_frames++;
    }
    CHECK(decay_frames >= 958 && decay_frames <= 962
          && synth.amp_envelope.stage == MA_ENVELOPE_SUSTAIN
          && synth.amp_envelope.level == timing.sustain,
          "MA 20 ms decay closed in %d frames", decay_frames);
    for (int frame = 0; frame < 1024; frame++) (void)ma_synth_tick(&synth);
    CHECK(synth.amp_envelope.level == timing.sustain
          && synth.filter_envelope.level == timing.sustain,
          "MA sustain must hold its literal target");

    float release_start = synth.amp_envelope.level;
    ma_synth_note_off(&synth, 0, 60, 0);
    CHECK(!synth.note_active
          && synth.amp_envelope.stage == MA_ENVELOPE_RELEASE
          && synth.amp_envelope.level == release_start,
          "MA NoteOff must release from the current level");
    int release_frames = 0;
    while (synth.amp_envelope.stage == MA_ENVELOPE_RELEASE
           && release_frames < 2500) {
        (void)ma_synth_tick(&synth);
        release_frames++;
    }
    CHECK(release_frames >= 1438 && release_frames <= 1442
          && synth.amp_envelope.stage == MA_ENVELOPE_IDLE
          && synth.amp_envelope.level == 0.0f,
          "MA 30 ms release closed in %d frames", release_frames);

    ma_synth_init(&synth, 48000.0f);
    ma_synth_set_amp_adsr(&synth, (ma_adsr){
        .attack_ms = 100.0f, .decay_ms = 100.0f,
        .sustain = 0.5f, .release_ms = 100.0f,
    });
    ma_synth_set_filter_adsr(&synth, synth.amp_adsr);
    ma_synth_note_on(&synth, 0, 57, 100);
    for (int frame = 0; frame < 1000; frame++) (void)ma_synth_tick(&synth);
    ma_synth_note_off(&synth, 0, 57, 0);
    for (int frame = 0; frame < 200; frame++) (void)ma_synth_tick(&synth);
    float retrigger_level = synth.amp_envelope.level;
    ma_synth_note_on(&synth, 0, 57, 100);
    CHECK(synth.amp_envelope.stage == MA_ENVELOPE_ATTACK
          && synth.amp_envelope.level == retrigger_level,
          "MA retrigger must preserve the current level");
    (void)ma_synth_tick(&synth);
    CHECK(synth.amp_envelope.level > retrigger_level,
          "MA retrigger attack must move upward from the release level");

    static const ma_vco_controls saw_only = {
        .saw_level = 1.0f,
        .pulse_width = 0.5f,
    };
    static const ma_vco_controls silent = { .pulse_width = 0.5f };
    ma_synth low_velocity;
    ma_synth_init(&low_velocity, 48000.0f);
    ma_synth_set_vco1(&low_velocity, saw_only);
    ma_synth_set_vco2(&low_velocity, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&low_velocity, 0.0f, 0.0f,
                                       0.0f, 0.0f);
    ma_synth_set_mozaik(&low_velocity, 0.0f, 0.5601133f, 0.5150284f,
                        0.0f, 0.0f);
    ma_synth_set_filter(&low_velocity, 20000.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(&low_velocity, 0.0f, 0.0f);
    ma_synth_set_amp_adsr(&low_velocity, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 1.0f, .release_ms = 1.0f,
    });
    ma_synth_set_filter_adsr(&low_velocity, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_note_on(&low_velocity, 0, 69, 1);
    for (int frame = 0; frame < 1000; frame++)
        (void)ma_synth_tick(&low_velocity);
    ma_synth high_velocity = low_velocity;
    high_velocity.velocity = 127;
    float low_gain = 0.25f + 0.75f * ma_velocity_level(1);
    float vca_error = 0.0f;
    float vca_energy = 0.0f;
    for (int frame = 0; frame < 12000; frame++) {
        (void)ma_synth_tick(&low_velocity);
        (void)ma_synth_tick(&high_velocity);
        float low = low_velocity.output.pre_body;
        float high = high_velocity.output.pre_body;
        float error = fabsf(low - high * low_gain);
        if (error > vca_error) vca_error = error;
        vca_energy += fabsf(high);
    }
    CHECK(vca_error < 2.0e-6f && vca_energy > 100.0f,
          "MA velocity VCA error %.9f energy %.3f",
          (double)vca_error, (double)vca_energy);

    ma_synth pitched;
    ma_synth_init(&pitched, 48000.0f);
    ma_synth_set_vco1(&pitched, saw_only);
    ma_synth_set_vco2(&pitched, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&pitched, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&pitched, 0.0f, 0.5601133f, 0.5150284f,
                        0.0f, 0.0f);
    ma_synth_set_filter_modulation(&pitched, 1.0f, 0.0f);
    ma_synth_set_filter_adsr(&pitched, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 1.0f, .release_ms = 1.0f,
    });
    ma_synth_note_on(&pitched, 0, 69, 127);
    for (int frame = 0; frame < 1000; frame++) (void)ma_synth_tick(&pitched);
    int rising = 0;
    float previous = pitched.ladder.pressure_input;
    for (int frame = 1; frame < 48000; frame++) {
        (void)ma_synth_tick(&pitched);
        float sample = pitched.ladder.pressure_input;
        rising += previous <= 0.0f && sample > 0.0f;
        previous = sample;
    }
    CHECK(rising >= 478 && rising <= 482
          && pitched.filter_cutoff_effective_hz
             == 1.78f * pitched.filter_cutoff_hz,
          "MA filter-envelope routes produced %d VCO1 crossings at %.3f Hz",
          rising, (double)pitched.filter_cutoff_effective_hz);

    ma_synth low_note, high_note;
    ma_synth_init(&low_note, 48000.0f);
    ma_synth_init(&high_note, 48000.0f);
    ma_synth_set_filter_modulation(&low_note, 0.0f, 1.0f);
    ma_synth_set_filter_modulation(&high_note, 0.0f, 1.0f);
    ma_synth_set_filter_adsr(&low_note, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_set_filter_adsr(&high_note, low_note.filter_adsr);
    ma_synth_note_on(&low_note, 0, 36, 127);
    ma_synth_note_on(&high_note, 0, 84, 127);
    for (int frame = 0; frame < 1000; frame++) {
        (void)ma_synth_tick(&low_note);
        (void)ma_synth_tick(&high_note);
    }
    CHECK(fabsf(low_note.filter_cutoff_effective_hz - 711.0f) < 0.001f
          && fabsf(high_note.filter_cutoff_effective_hz - 1089.0f) < 0.001f,
          "MA keytrack anchors are %.3f and %.3f Hz",
          (double)low_note.filter_cutoff_effective_hz,
          (double)high_note.filter_cutoff_effective_hz);

    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_synth hostile;
        ma_synth_init(&hostile, rates[rate]);
        ma_synth_set_amp_adsr(&hostile, (ma_adsr){
            .attack_ms = 1.0f, .decay_ms = 20000.0f,
            .sustain = 1.0f, .release_ms = 20000.0f,
        });
        ma_synth_set_filter_adsr(&hostile, (ma_adsr){
            .attack_ms = 20000.0f, .decay_ms = 1.0f,
            .sustain = 0.0f, .release_ms = 1.0f,
        });
        ma_synth_set_filter_modulation(&hostile, 1.0f, 1.0f);
        ma_synth_note_on(&hostile, 0, 127, 255);
        bool finite = true;
        for (int frame = 0; frame < 12000; frame++) {
            if (frame == 4000) ma_synth_note_off(&hostile, 0, 127, 0);
            if (frame == 8000) ma_synth_note_on(&hostile, 0, 0, 1);
            ma_frame output = ma_synth_tick(&hostile);
            finite = finite
                  && isfinite(output.left) && isfinite(output.right)
                  && isfinite(hostile.amp_envelope.level)
                  && isfinite(hostile.filter_envelope.level)
                  && hostile.amp_envelope.level >= 0.0f
                  && hostile.amp_envelope.level <= 1.0f
                  && hostile.filter_envelope.level >= 0.0f
                  && hostile.filter_envelope.level <= 1.0f
                  && hostile.filter_cutoff_effective_hz >= 20.0f
                  && hostile.filter_cutoff_effective_hz
                     <= 0.42f * hostile.sample_rate_hz;
        }
        CHECK(finite, "MA hostile envelope sweep failed at %.0f Hz",
              (double)rates[rate]);
    }

    ma_synth first, second;
    ma_synth_init(&first, 48000.0f);
    ma_synth_init(&second, 48000.0f);
    ma_synth_set_amp_adsr(&first, (ma_adsr){
        .attack_ms = 7.0f, .decay_ms = 13.0f,
        .sustain = 0.37f, .release_ms = 11.0f,
    });
    ma_synth_set_amp_adsr(&second, first.amp_adsr);
    ma_synth_set_filter_adsr(&first, first.amp_adsr);
    ma_synth_set_filter_adsr(&second, first.amp_adsr);
    ma_synth_note_on(&first, 0, 64, 99);
    ma_synth_note_on(&second, 0, 64, 99);
    for (int frame = 0; frame < 4000; frame++) {
        if (frame == 800) {
            ma_synth_note_off(&first, 0, 64, 0);
            ma_synth_note_off(&second, 0, 64, 0);
        }
        if (frame == 1200) {
            ma_synth_note_on(&first, 0, 67, 73);
            ma_synth_note_on(&second, 0, 67, 73);
        }
        ma_frame a = ma_synth_tick(&first);
        ma_frame b = ma_synth_tick(&second);
        CHECK(memcmp(&a, &b, sizeof a) == 0
              && memcmp(&first.amp_envelope, &second.amp_envelope,
                        sizeof first.amp_envelope) == 0
              && memcmp(&first.filter_envelope, &second.filter_envelope,
                        sizeof first.filter_envelope) == 0,
              "MA envelope render diverged at frame %d", frame);
    }
}

static void test_ma_identity_and_performance(void) {
    ma_synth synth;
    ma_synth_init(&synth, 48000.0f);
    CHECK(synth.identity_zero.horizont_open == 0.25f
          && synth.identity_zero.horizont_air == 0.08f
          && synth.identity_zero.horizont_span == 0.0f
          && synth.identity_zero.mass == 0.0f
          && synth.identity_zero.strain == 0.0f
          && fabsf(synth.identity_zero.headroom - 0.7944f) < 1.0e-7f
          && synth.identity_zero.rupture_threshold == 0.78f,
          "MA identity zero frame must match the pinned frame");
    CHECK(synth.smoothers.filter_cutoff_hz.current == 900.0f
          && synth.smoothers.filter_cutoff_hz.target == 900.0f
          && synth.smoothers.sync_amount.current == 0.0f
          && synth.smoothers.body_load_ratio.current == 1.0f
          && synth.smoothers.crossfeed.current == 0.0f,
          "MA identity zero deltas must be literal zero or unity");

    ma_synth_set_macro(&synth, MA_MACRO_GRAVITACIJA, 0.40f);
    ma_synth_set_macro(&synth, MA_MACRO_BLOOM, 0.30f);
    ma_synth_set_macro(&synth, MA_MACRO_HEAT, 0.20f);
    ma_synth_set_macro(&synth, MA_MACRO_RUIN, 0.50f);
    ma_synth_set_macro(&synth, MA_MACRO_SWARM, 0.60f);
    CHECK(fabsf(synth.identity.horizont_open - 0.3000f) < 1.0e-6f
          && fabsf(synth.identity.horizont_air - 0.2980f) < 1.0e-6f
          && fabsf(synth.identity.horizont_span - 0.3020f) < 1.0e-6f
          && fabsf(synth.identity.pec_mass - 0.2350f) < 1.0e-6f
          && fabsf(synth.identity.pec_heat - 0.2080f) < 1.0e-6f
          && fabsf(synth.identity.pec_pressure - 0.3200f) < 1.0e-6f,
          "MA identity Horizont/Pec frame coefficients");
    CHECK(fabsf(synth.identity.baklja_ready - 0.3920f) < 1.0e-6f
          && fabsf(synth.identity.baklja_edge - 0.3400f) < 1.0e-6f
          && fabsf(synth.identity.baklja_sync_bias - 0.3720f) < 1.0e-6f
          && fabsf(synth.identity.mass - 0.2461f) < 1.0e-6f
          && fabsf(synth.identity.strain - 0.3456f) < 1.0e-6f
          && fabsf(synth.identity.headroom - 0.61428f) < 1.0e-6f,
          "MA identity Baklja and derived frame coefficients");
    CHECK(fabsf(synth.identity.body_focus - 0.1755f) < 1.0e-6f
          && fabsf(synth.identity.rupture_threshold - 0.62976f) < 1.0e-6f
          && fabsf(synth.identity.rupture_response - 0.3734f) < 1.0e-6f
          && fabsf(synth.identity.spatial_dispersion - 0.3241f) < 1.0e-6f,
          "MA identity body/rupture/spatial coefficients");
    CHECK(fabsf(synth.smoothers.filter_cutoff_hz.target
                 - 964.2126f) < 0.001f
          && fabsf(synth.smoothers.filter_resonance.target
                   - 0.247756f) < 1.0e-6f
          && fabsf(synth.smoothers.filter_drive.target - 0.192f) < 1.0e-6f
          && fabsf(synth.smoothers.filter_env_amount.target
                   - 0.306f) < 1.0e-6f
          && fabsf(synth.smoothers.filter_keytrack.target
                   - 0.435555f) < 1.0e-6f,
          "MA identity filter destinations");
    CHECK(fabsf(synth.smoothers.sync_amount.target - 0.1116f) < 1.0e-6f
          && fabsf(synth.smoothers.crossmod_amount.target
                   - 0.09408f) < 1.0e-6f
          && fabsf(synth.smoothers.mozaik_mix.target - 0.23f) < 1.0e-6f
          && fabsf(synth.smoothers.mozaik_contrast.target
                   - 1.666034f) < 1.0e-6f
          && fabsf(synth.smoothers.mozaik_drift.target - 0.26f) < 1.0e-6f,
          "MA identity source destinations");
    CHECK(fabsf(synth.smoothers.width.target - 0.71926f) < 1.0e-6f
          && fabsf(synth.smoothers.crossfeed.target - 0.05673f) < 1.0e-6f
          && fabsf(synth.smoothers.body_drive.target
                   - 0.143875f) < 1.0e-6f
          && fabsf(synth.smoothers.body_load_ratio.target
                   - 1.107132f) < 1.0e-6f
          && synth.mozaik.pending_phason_q32 == UINT32_C(0x13333340)
          && synth.mozaik.has_pending_phason,
          "MA identity output and pending-phason destinations");

    ma_synth overlay;
    ma_synth_init(&overlay, 48000.0f);
    ma_synth_set_macro(&overlay, MA_MACRO_GRAVITACIJA, 0.10f);
    ma_synth_set_macro(&overlay, MA_MACRO_BLOOM, 0.30f);
    ma_synth_set_macro(&overlay, MA_MACRO_RUIN, 0.20f);
    ma_synth_set_macro(&overlay, MA_MACRO_SWARM, 0.40f);
    ma_synth_set_channel_pressure(&overlay, 1.0f);
    ma_synth_set_mod_wheel(&overlay, 1.0f);
    CHECK(overlay.macro[MA_MACRO_GRAVITACIJA] == 0.10f
          && overlay.macro[MA_MACRO_BLOOM] == 0.30f
          && overlay.macro[MA_MACRO_RUIN] == 0.20f
          && overlay.macro[MA_MACRO_SWARM] == 0.40f,
          "MA performance overlays must not overwrite base macros");
    CHECK(overlay.identity.gravitacija == 0.55f
          && overlay.identity.bloom == 0.65f
          && overlay.identity.ruin == 0.55f
          && overlay.identity.swarm == 0.90f,
          "MA channel pressure and mod wheel overlay routing");
    ma_synth_set_channel_pressure(&overlay, NAN);
    ma_synth_set_mod_wheel(&overlay, INFINITY);
    ma_synth_set_pitch_bend(&overlay, -9.0f);
    CHECK(overlay.channel_pressure == 0.0f && overlay.mod_wheel == 0.0f
          && overlay.pitch_bend_semitones == -2.0f,
          "MA performance setters must default non-finite and clamp finite");

    ma_synth smooth;
    ma_synth_init(&smooth, 48000.0f);
    ma_vco_controls smooth_vco1 = smooth.vco1;
    smooth_vco1.sine_level = 0.80f;
    ma_synth_set_vco1(&smooth, smooth_vco1);
    CHECK(smooth.smoothers.vco1.sine_level.current == 0.20f
          && smooth.smoothers.vco1.sine_level.target == 0.80f
          && smooth.smoothers.vco1.sine_level.remaining == 288,
          "MA VCO1 sine must use the common smoother");
    ma_synth_set_pitch_bend(&smooth, 2.0f);
    CHECK(smooth.smoothers.pitch_bend_semitones.remaining == 288
          && smooth.smoothers.pitch_bend_semitones.current == 0.0f
          && smooth.smoothers.pitch_bend_semitones.target == 2.0f,
          "MA 48 kHz smoother must schedule 288 frames");
    (void)ma_synth_tick(&smooth);
    CHECK(fabsf(smooth.smoothers.pitch_bend_semitones.current
                 - 2.0f / 288.0f) < 1.0e-8f
          && smooth.smoothers.pitch_bend_semitones.remaining == 287,
          "MA smoother first linear step");
    for (int frame = 1; frame < 288; frame++) (void)ma_synth_tick(&smooth);
    CHECK(smooth.smoothers.pitch_bend_semitones.current == 2.0f
          && smooth.smoothers.pitch_bend_semitones.target == 2.0f
          && smooth.smoothers.pitch_bend_semitones.remaining == 0,
          "MA smoother must snap exactly to its target");
    ma_synth_init(&smooth, 44100.0f);
    ma_synth_set_pitch_bend(&smooth, 1.0f);
    CHECK(smooth.smoothers.pitch_bend_semitones.remaining == 265,
          "MA 44.1 kHz smoother length");
    ma_synth_init(&smooth, 96000.0f);
    ma_synth_set_pitch_bend(&smooth, 1.0f);
    CHECK(smooth.smoothers.pitch_bend_semitones.remaining == 512,
          "MA smoother length must clamp at 512 frames");

    ma_synth direct, zero;
    ma_synth_init(&direct, 48000.0f);
    ma_synth_init(&zero, 48000.0f);
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        ma_synth_set_macro(&zero, (ma_macro_id)macro, 0.0f);
    ma_synth_set_channel_pressure(&zero, 0.0f);
    ma_synth_set_mod_wheel(&zero, 0.0f);
    ma_synth_set_pitch_bend(&zero, 0.0f);
    ma_synth_note_on(&direct, 4, 57, 111);
    ma_synth_note_on(&zero, 4, 57, 111);
    int zero_mismatch = 0;
    for (int frame = 0; frame < 12000; frame++) {
        ma_frame a = ma_synth_tick(&direct);
        ma_frame b = ma_synth_tick(&zero);
        zero_mismatch += memcmp(&a, &b, sizeof a) != 0;
    }
    CHECK(zero_mismatch == 0,
          "MA zero macros and zero overlays must be PCM bit-identical");

    ma_synth pressure;
    ma_synth_init(&pressure, 48000.0f);
    ma_synth_set_poly_pressure(&pressure, 3, 60, 1.0f);
    CHECK(pressure.poly_pressure == 0.0f,
          "MA poly pressure must ignore an idle card");
    ma_synth_note_on(&pressure, 3, 60, 127);
    ma_synth_set_poly_pressure(&pressure, 2, 60, 1.0f);
    ma_synth_set_poly_pressure(&pressure, 3, 61, 1.0f);
    CHECK(pressure.poly_pressure == 0.0f,
          "MA poly pressure must match both channel and note");
    ma_synth_set_filter(&pressure, 900.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(&pressure, 0.0f, 0.0f);
    ma_synth_set_filter_adsr(&pressure, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_set_poly_pressure(&pressure, 3, 60, 1.0f);
    CHECK(pressure.smoothers.poly_pressure.remaining == 288,
          "MA per-note pressure must use the common smoother");
    for (int frame = 0; frame < 288; frame++) (void)ma_synth_tick(&pressure);
    CHECK(pressure.smoothers.poly_pressure.current == 1.0f
          && fabsf(pressure.filter_cutoff_effective_hz
                   - 1070.2864f) < 0.01f,
          "MA full poly pressure must add one quarter octave to cutoff");
    ma_synth_note_off(&pressure, 2, 60, 127);
    ma_synth_note_off(&pressure, 3, 61, 64);
    CHECK(pressure.note_active && pressure.ignored_release_velocities == 2,
          "MA nonmatching note-off must retain ownership and count release velocity");
    ma_synth_note_off(&pressure, 3, 60, 32);
    CHECK(!pressure.note_active && pressure.ignored_release_velocities == 3,
          "MA matching channel/note must release and count release velocity");

    static const ma_vco_controls saw_only = {
        .saw_level = 1.0f,
        .pulse_width = 0.5f,
    };
    static const ma_vco_controls silent = { .pulse_width = 0.5f };
    ma_synth bent;
    ma_synth_init(&bent, 48000.0f);
    ma_synth_set_vco1(&bent, saw_only);
    ma_synth_set_vco2(&bent, silent, 0.0f, 0, 0.0f);
    ma_synth_set_oscillator_modulation(&bent, 0.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_mozaik(&bent, 0.0f, 0.5601133f, 0.5150284f, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(&bent, 0.0f, 0.0f);
    ma_synth_set_filter_adsr(&bent, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.0f, .release_ms = 1.0f,
    });
    ma_synth_set_pitch_bend(&bent, 2.0f);
    ma_synth_note_on(&bent, 0, 69, 127);
    for (int frame = 0; frame < 1024; frame++) (void)ma_synth_tick(&bent);
    int rising = 0;
    float previous = bent.ladder.pressure_input;
    for (int frame = 1; frame < 48000; frame++) {
        (void)ma_synth_tick(&bent);
        float sample = bent.ladder.pressure_input;
        rising += previous <= 0.0f && sample > 0.0f;
        previous = sample;
    }
    CHECK(rising >= 492 && rising <= 496,
          "MA +2 semitone bend produced %d VCO1 crossings", rising);

    ma_synth dry_vca;
    ma_synth_init(&dry_vca, 48000.0f);
    ma_synth_set_filter(&dry_vca, 20000.0f, 0.0f, 0.0f, 0.0f);
    ma_synth_set_filter_modulation(&dry_vca, 0.0f, 0.0f);
    ma_synth_set_amp_adsr(&dry_vca, (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 1.0f, .release_ms = 1.0f,
    });
    ma_synth_note_on(&dry_vca, 5, 69, 127);
    for (int frame = 0; frame < 1000; frame++) (void)ma_synth_tick(&dry_vca);
    ma_synth pressure_vca = dry_vca;
    ma_synth_set_poly_pressure(&pressure_vca, 5, 69, 1.0f);
    for (int frame = 0; frame < 288; frame++) {
        (void)ma_synth_tick(&dry_vca);
        (void)ma_synth_tick(&pressure_vca);
    }
    float vca_error = 0.0f;
    for (int frame = 0; frame < 256; frame++) {
        (void)ma_synth_tick(&dry_vca);
        (void)ma_synth_tick(&pressure_vca);
        float error = fabsf(pressure_vca.output.pre_body
                            - 1.10f * dry_vca.output.pre_body);
        if (error > vca_error) vca_error = error;
    }
    CHECK(vca_error < 2.0e-6f,
          "MA full poly pressure VCA error %.9f", (double)vca_error);
}

static void test_ma_output(void) {
    ma_synth synth;
    ma_synth_init(&synth, 48000.0f);
    double dc_reference = 1.0 - exp(-TAU_D * 10.0 / 48000.0);
    CHECK(fabs((double)synth.output.dc_coefficient - dc_reference) < 2.0e-10,
          "MA 10 Hz DC coefficient %.10f vs %.10f",
          (double)synth.output.dc_coefficient, dc_reference);
    CHECK(synth.output.diagnostics.sanitization_count == 0
          && synth.output.diagnostics.knee_hit_count == 0
          && synth.output.diagnostics.tiny_flush_count == 0
          && synth.output.diagnostics.pre_peak == 0.0f
          && synth.output.diagnostics.post_peak == 0.0f
          && synth.output.diagnostics.maximum_reduction == 0.0f,
          "MA output diagnostics must initialize to zero");

    CHECK(ma_safety_curve(0.0f) == 0.0f
          && ma_safety_curve(0.98f) == 0.98f
          && ma_safety_curve(-0.98f) == -0.98f,
          "MA safety must be literal identity through the knee");
    CHECK(fabsf(ma_safety_curve(0.99f) - 0.9925f) < 2.0e-7f
          && ma_safety_curve(1.0f) == 1.0f
          && ma_safety_curve(-1.0f) == -1.0f
          && ma_safety_curve(2.0f) == 1.0f
          && ma_safety_curve(-2.0f) == -1.0f,
          "MA safety polynomial anchors");
    CHECK(ma_safety_curve(NAN) == 0.0f
          && ma_safety_curve(INFINITY) == 0.0f
          && ma_safety_curve(-INFINITY) == 0.0f,
          "MA safety must sanitize non-finite samples");

    float previous = ma_safety_curve(-2.0f);
    bool monotone = true;
    for (int index = 1; index <= 4000; index++) {
        float input = -2.0f + 4.0f * (float)index / 4000.0f;
        float sample = ma_safety_curve(input);
        monotone = monotone && sample >= previous
                && sample >= -1.0f && sample <= 1.0f;
        previous = sample;
    }
    CHECK(monotone, "MA safety curve must be monotone and bounded");

    float epsilon = 1.0e-4f;
    float knee_left = (ma_safety_curve(0.98f)
                       - ma_safety_curve(0.98f - epsilon)) / epsilon;
    float knee_right = (ma_safety_curve(0.98f + epsilon)
                        - ma_safety_curve(0.98f)) / epsilon;
    float ceiling_left = (ma_safety_curve(1.0f)
                          - ma_safety_curve(1.0f - epsilon)) / epsilon;
    float ceiling_right = (ma_safety_curve(1.0f + epsilon)
                           - ma_safety_curve(1.0f)) / epsilon;
    CHECK(fabsf(knee_left - 1.0f) < 5.0e-4f
          && fabsf(knee_right - 1.0f) < 0.02f,
          "MA safety knee slopes %.6f / %.6f",
          (double)knee_left, (double)knee_right);
    CHECK(ceiling_left >= 0.0f && ceiling_left < 0.04f
          && ceiling_right == 0.0f,
          "MA safety ceiling slopes %.6f / %.6f",
          (double)ceiling_left, (double)ceiling_right);

    ma_patch bypass_patch = ma_patch_tepih;
    bypass_patch.body_drive = 0.0f;
    bypass_patch.master_level = 1.0f;
    ma_synth bypass_a, bypass_b;
    ma_synth_init_patch(&bypass_a, 48000.0f, &bypass_patch);
    ma_synth_init_patch(&bypass_b, 48000.0f, &bypass_patch);
    bypass_b.smoothers.body_load_ratio = (ma_smoother){
        .current = 1.5f, .target = 1.5f,
    };
    bypass_b.output.body.env = 0.75f;
    bypass_b.output.body.hp_lp = -0.25f;
    tw_drive body_before = bypass_b.output.body;
    ma_synth_note_on(&bypass_a, 0, 57, 111);
    ma_synth_note_on(&bypass_b, 0, 57, 111);
    bool exact_bypass = true;
    for (int frame = 0; frame < 2048; frame++) {
        ma_frame a = ma_synth_tick(&bypass_a);
        ma_frame b = ma_synth_tick(&bypass_b);
        exact_bypass = exact_bypass
                    && memcmp(&a, &b, sizeof a) == 0
                    && bypass_a.output.pre_body == bypass_a.output.post_body
                    && bypass_b.output.pre_body == bypass_b.output.post_body;
    }
    CHECK(exact_bypass && memcmp(&bypass_b.output.body, &body_before,
                                 sizeof body_before) == 0,
          "MA body-drive zero must bypass load and leave body state untouched");

    ma_patch driven_patch = bypass_patch;
    driven_patch.body_drive = 0.5f;
    ma_synth driven;
    ma_synth_init_patch(&driven, 48000.0f, &driven_patch);
    ma_synth loaded = driven;
    loaded.smoothers.body_load_ratio = (ma_smoother){
        .current = 1.5f, .target = 1.5f,
    };
    ma_synth_note_on(&driven, 0, 57, 111);
    ma_synth_note_on(&loaded, 0, 57, 111);
    bool body_changed = false;
    bool load_changed = false;
    for (int frame = 0; frame < 2048; frame++) {
        (void)ma_synth_tick(&driven);
        (void)ma_synth_tick(&loaded);
        body_changed = body_changed
                    || driven.output.pre_body != driven.output.post_body;
        load_changed = load_changed
                    || driven.output.post_body != loaded.output.post_body;
    }
    CHECK(body_changed && load_changed && driven.output.body.env > 0.0f,
          "MA positive body drive/load must use the shared tw_drive state");

    ma_synth master_full, master_half;
    ma_synth_init_patch(&master_full, 48000.0f, &bypass_patch);
    ma_synth_init_patch(&master_half, 48000.0f, &bypass_patch);
    master_full.output.dc_coefficient = 0.0f;
    master_half.output.dc_coefficient = 0.0f;
    master_half.master_level = 0.5f;
    ma_synth_note_on(&master_full, 0, 57, 111);
    ma_synth_note_on(&master_half, 0, 57, 111);
    bool master_order = true;
    for (int frame = 0; frame < 1024; frame++) {
        ma_frame full = ma_synth_tick(&master_full);
        ma_frame half = ma_synth_tick(&master_half);
        master_order = master_order
                    && master_full.output.pre_body
                       == master_half.output.pre_body
                    && full.left
                       == ma_safety_curve(master_full.output.pre_body)
                    && half.left
                       == ma_safety_curve(0.5f * master_half.output.pre_body);
    }
    CHECK(master_order,
          "MA master level must operate immediately before safety");

    ma_synth tiny;
    ma_synth_init(&tiny, 48000.0f);
    tiny.output.dc_lp_left = 0.5e-9f;
    tiny.output.dc_lp_right = -0.5e-9f;
    ma_frame tiny_sample = ma_synth_tick(&tiny);
    CHECK(tiny.output.dc_lp_left == 0.0f
          && tiny.output.dc_lp_right == 0.0f
          && tiny.output.diagnostics.tiny_flush_count == 2
          && tiny_sample.left == 0.0f && tiny_sample.right == 0.0f,
          "MA DC blockers must count and flush tiny state");

    ma_synth poisoned;
    ma_synth_init(&poisoned, 48000.0f);
    poisoned.master_level = NAN;
    ma_frame sanitized = ma_synth_tick(&poisoned);
    CHECK(sanitized.left == 0.0f && sanitized.right == 0.0f
          && poisoned.output.diagnostics.sanitization_count == 2,
          "MA safety must zero and count non-finite stereo samples");

    ma_synth knee;
    ma_synth_init_patch(&knee, 48000.0f, &bypass_patch);
    knee.output.dc_coefficient = 0.0f;
    ma_synth_note_on(&knee, 0, 57, 127);
    knee.amp_adsr.sustain = 256.0f;
    knee.amp_envelope = (ma_envelope){
        .level = 256.0f,
        .stage = MA_ENVELOPE_SUSTAIN,
    };
    for (int frame = 0; frame < 2048; frame++) (void)ma_synth_tick(&knee);
    CHECK(knee.output.diagnostics.knee_hit_count > 0
          && knee.output.diagnostics.pre_peak > 1.0f
          && knee.output.diagnostics.post_peak == 1.0f
          && knee.output.diagnostics.maximum_reduction > 0.0f,
          "MA safety diagnostics must record knee work and reduction");

    static const float rates[] = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        ma_patch hostile_patch = ma_patch_tepih;
        hostile_patch.body_drive = 1.0f;
        hostile_patch.master_level = 1.0f;
        hostile_patch.filter_cutoff_hz = 20000.0f;
        hostile_patch.filter_resonance = 1.0f;
        hostile_patch.filter_drive = 1.0f;
        hostile_patch.mixer_pressure = 1.0f;
        for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
            hostile_patch.macro[macro] = 1.0f;
        ma_synth hostile;
        ma_synth_init_patch(&hostile, rates[rate], &hostile_patch);
        ma_synth_note_on(&hostile, 15, 127, 255);
        bool finite = true;
        uint64_t hash = 0;
        for (int frame = 0; frame < 4096; frame++) {
            if (frame == 1024) ma_synth_set_channel_pressure(&hostile, 1.0f);
            if (frame == 2048) ma_synth_set_mod_wheel(&hostile, 1.0f);
            if (frame == 3072) ma_synth_set_pitch_bend(&hostile, -2.0f);
            ma_frame output = ma_synth_tick(&hostile);
            finite = finite && isfinite(output.left) && isfinite(output.right)
                   && output.left >= -1.0f && output.left <= 1.0f
                   && memcmp(&output.left, &output.right,
                             sizeof output.left) == 0;
            hash = tw_fnv1a64(&output, sizeof output, hash);
        }
        CHECK(finite && hash != 0
              && hostile.output.diagnostics.post_peak <= 1.0f,
              "MA hostile output sweep failed at %.0f Hz",
              (double)rates[rate]);
    }

    ma_synth first, second;
    ma_synth_init(&first, 48000.0f);
    ma_synth_init(&second, 48000.0f);
    ma_synth_note_on(&first, 2, 45, 103);
    ma_synth_note_on(&second, 2, 45, 103);
    uint64_t first_hash = 0, second_hash = 0;
    for (int frame = 0; frame < 12000; frame++) {
        if (frame == 3000) {
            ma_synth_set_macro(&first, MA_MACRO_HEAT, 0.75f);
            ma_synth_set_macro(&second, MA_MACRO_HEAT, 0.75f);
        }
        if (frame == 7000) {
            ma_synth_note_off(&first, 2, 45, 64);
            ma_synth_note_off(&second, 2, 45, 64);
        }
        ma_frame a = ma_synth_tick(&first);
        ma_frame b = ma_synth_tick(&second);
        first_hash = tw_fnv1a64(&a, sizeof a, first_hash);
        second_hash = tw_fnv1a64(&b, sizeof b, second_hash);
    }
    CHECK(first_hash == second_hash && first_hash != 0
          && memcmp(&first.output.diagnostics, &second.output.diagnostics,
                    sizeof first.output.diagnostics) == 0,
          "MA output render/signature must be deterministic");
}

static void test_ma_card_bank(void) {
    ma_patch quick = ma_patch_tepih;
    quick.amp_adsr = (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.5f, .release_ms = 1.0f,
    };
    quick.filter_adsr = quick.amp_adsr;

    ma_card_bank bank;
    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    bool initialized = bank.cursor == 0 && bank.next_age == 1;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        initialized = initialized
                   && bank.owner[slot].phase == MA_CARD_IDLE
                   && bank.card[slot].sample_rate_hz == 48000.0f;
    CHECK(initialized, "MA2 fixed card bank initialization");

    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        uint8_t chosen = ma_card_bank_note_on(
            &bank, 2, (uint8_t)(60 + slot), (uint8_t)(100 + slot));
        CHECK(chosen == slot && bank.owner[slot].age == (uint64_t)slot + 1
              && bank.owner[slot].phase == MA_CARD_HELD
              && bank.owner[slot].channel == 2
              && bank.owner[slot].note == 60 + slot,
              "MA2 round-robin assignment slot %u", slot);
    }
    CHECK(bank.cursor == 0 && bank.next_age == 6,
          "MA2 full-bank cursor and monotonic age");

    uint8_t stolen = ma_card_bank_note_on(&bank, 3, 72, 255);
    CHECK(stolen == 0 && bank.cursor == 1 && bank.owner[0].age == 6
          && bank.owner[0].channel == 3 && bank.owner[0].note == 72
          && bank.card[0].velocity == 127,
          "MA2 exhaustion must steal oldest assigned card");

    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    uint8_t repeat0 = ma_card_bank_note_on(&bank, 4, 57, 90);
    uint8_t repeat1 = ma_card_bank_note_on(&bank, 4, 57, 91);
    uint8_t release0 = ma_card_bank_note_off(&bank, 4, 57, 64);
    CHECK(repeat0 == 0 && repeat1 == 1 && release0 == 0
          && bank.owner[0].phase == MA_CARD_RELEASED
          && bank.owner[1].phase == MA_CARD_HELD,
          "MA2 repeated note-off must release oldest held instance");
    uint8_t release1 = ma_card_bank_note_off(&bank, 4, 57, 32);
    CHECK(release1 == 1 && bank.owner[1].phase == MA_CARD_RELEASED
          && bank.ignored_release_velocities == 2,
          "MA2 repeated note release order and event accounting");

    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        (void)ma_card_bank_note_on(&bank, 0, (uint8_t)(48 + slot), 100);
    CHECK(ma_card_bank_note_off(&bank, 0, 48, 0) == 0,
          "MA2 oldest held card release setup");
    CHECK(ma_card_bank_note_on(&bank, 0, 80, 100) == 0
          && bank.owner[0].age == 6,
          "MA2 released cards must participate in oldest-age stealing");

    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    (void)ma_card_bank_note_on(&bank, 0, 48, 100);
    (void)ma_card_bank_note_on(&bank, 0, 49, 100);
    (void)ma_card_bank_note_on(&bank, 0, 50, 100);
    (void)ma_card_bank_note_off(&bank, 0, 49, 0);
    ma_frame frames[MA_CARD_COUNT] = { 0 };
    bool finite = true;
    for (int frame = 0; frame < 512; frame++) {
        ma_card_bank_tick(&bank, frames);
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
            finite = finite && isfinite(frames[slot].left)
                            && isfinite(frames[slot].right);
    }
    CHECK(finite && bank.owner[1].phase == MA_CARD_IDLE,
          "MA2 bank tick must reap completed releases and stay finite");
    CHECK(ma_card_bank_note_on(&bank, 0, 51, 100) == 3,
          "MA2 idle search must begin at the round-robin cursor");

    uint8_t cursor = bank.cursor;
    uint64_t age = bank.next_age;
    CHECK(ma_card_bank_note_on(&bank, 16, 60, 100) == MA_CARD_NONE
          && ma_card_bank_note_on(&bank, 0, 200, 100) == MA_CARD_NONE
          && ma_card_bank_note_off(&bank, 16, 60, 0) == MA_CARD_NONE
          && ma_card_bank_note_off(&bank, 0, 200, 0) == MA_CARD_NONE
          && bank.cursor == cursor && bank.next_age == age,
          "MA2 hostile events must not disturb allocation state");

    ma_card_bank_init(&bank, NAN);
    CHECK(bank.card[0].sample_rate_hz == 48000.0f
          && bank.card[MA_CARD_COUNT - 1].sample_rate_hz == 48000.0f,
          "MA2 card bank must inherit voice sample-rate sanitization");
}

static void test_ma_card_bank_sustain_and_panic(void) {
    ma_patch quick = ma_patch_tepih;
    quick.amp_adsr = (ma_adsr){
        .attack_ms = 1.0f, .decay_ms = 1.0f,
        .sustain = 0.5f, .release_ms = 1.0f,
    };
    quick.filter_adsr = quick.amp_adsr;

    ma_card_bank bank;
    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    (void)ma_card_bank_note_on(&bank, 3, 57, 90);
    (void)ma_card_bank_note_on(&bank, 3, 57, 91);
    ma_card_bank_set_sustain(&bank, true);
    uint8_t first = ma_card_bank_note_off(&bank, 3, 57, 64);
    uint8_t second = ma_card_bank_note_off(&bank, 3, 57, 32);
    CHECK(first == 0 && second == 1 && bank.sustain
          && bank.owner[0].phase == MA_CARD_SUSTAINED
          && bank.owner[1].phase == MA_CARD_SUSTAINED
          && bank.card[0].note_active && bank.card[1].note_active
          && bank.card[0].amp_envelope.stage != MA_ENVELOPE_RELEASE
          && bank.card[1].amp_envelope.stage != MA_ENVELOPE_RELEASE,
          "MA2 sustain must defer repeated-note releases");
    CHECK(ma_card_bank_note_off(&bank, 3, 57, 0) == MA_CARD_NONE
          && bank.ignored_release_velocities == 3,
          "MA2 sustained instances are no longer held NoteOff candidates");

    uint64_t age0 = bank.owner[0].age, age1 = bank.owner[1].age;
    ma_card_bank_set_sustain(&bank, true);
    ma_card_bank_set_sustain(&bank, false);
    CHECK(!bank.sustain && bank.owner[0].phase == MA_CARD_RELEASED
          && bank.owner[1].phase == MA_CARD_RELEASED
          && !bank.card[0].note_active && !bank.card[1].note_active
          && bank.card[0].amp_envelope.stage == MA_ENVELOPE_RELEASE
          && bank.card[1].amp_envelope.stage == MA_ENVELOPE_RELEASE
          && bank.owner[0].age == age0 && bank.owner[1].age == age1
          && bank.ignored_release_velocities == 3,
          "MA2 pedal-up must release sustained cards without reordering ages");

    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        (void)ma_card_bank_note_on(&bank, 0, (uint8_t)(48 + slot), 100);
    ma_card_bank_set_sustain(&bank, true);
    (void)ma_card_bank_note_off(&bank, 0, 48, 0);
    CHECK(bank.owner[0].phase == MA_CARD_SUSTAINED
          && ma_card_bank_note_on(&bank, 0, 80, 100) == 0
          && bank.owner[0].phase == MA_CARD_HELD
          && bank.owner[0].age == 6,
          "MA2 sustained cards must participate in oldest-age stealing");

    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    (void)ma_card_bank_note_on(&bank, 1, 60, 100);
    (void)ma_card_bank_note_on(&bank, 1, 61, 101);
    (void)ma_card_bank_note_on(&bank, 1, 62, 102);
    ma_frame frames[MA_CARD_COUNT] = { 0 };
    for (int frame = 0; frame < 64; frame++)
        ma_card_bank_tick(&bank, frames);
    (void)ma_card_bank_note_off(&bank, 1, 61, 0);
    ma_card_bank_set_sustain(&bank, true);
    (void)ma_card_bank_note_off(&bank, 1, 60, 0);
    uint64_t phases[3] = {
        bank.card[0].oscillator1.phase_q48,
        bank.card[1].oscillator1.phase_q48,
        bank.card[2].oscillator1.phase_q48,
    };
    uint8_t cursor = bank.cursor;
    uint64_t ages[3] = {
        bank.owner[0].age, bank.owner[1].age, bank.owner[2].age,
    };
    ma_card_bank_panic(&bank);
    bool released = !bank.sustain && bank.cursor == cursor
                 && bank.ignored_release_velocities == 2;
    for (uint8_t slot = 0; slot < 3; slot++)
        released = released
                && bank.owner[slot].phase == MA_CARD_RELEASED
                && bank.owner[slot].age == ages[slot]
                && !bank.card[slot].note_active
                && bank.card[slot].amp_envelope.stage == MA_ENVELOPE_RELEASE
                && bank.card[slot].oscillator1.phase_q48 == phases[slot];
    CHECK(released,
          "MA2 panic must release ownership without resetting DSP or ages");

    bool finite = true;
    for (int frame = 0; frame < 512; frame++) {
        ma_card_bank_tick(&bank, frames);
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
            finite = finite && isfinite(frames[slot].left)
                            && isfinite(frames[slot].right);
    }
    ma_card_bank_panic(&bank);
    bool idle = finite && !bank.sustain;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        idle = idle && bank.owner[slot].phase == MA_CARD_IDLE;
    CHECK(idle, "MA2 panic releases must decay to idle deterministically");
}

static void test_ma_glide_lfo_and_unison(void) {
    ma_synth baseline, bypass;
    ma_synth_init(&baseline, 48000.0f);
    ma_synth_init(&bypass, 48000.0f);
    ma_synth_set_glide(&bypass, false, 0.0f);
    ma_synth_set_lfo(&bypass, 0.0f, 1.0f);
    ma_synth_note_on(&baseline, 0, 60, 100);
    ma_synth_note_on(&bypass, 0, 60, 100);
    uint64_t baseline_hash = 0, bypass_hash = 0;
    for (int frame = 0; frame < 2048; frame++) {
        ma_frame a = ma_synth_tick(&baseline);
        ma_frame b = ma_synth_tick(&bypass);
        baseline_hash = tw_fnv1a64(&a, sizeof a, baseline_hash);
        bypass_hash = tw_fnv1a64(&b, sizeof b, bypass_hash);
    }
    CHECK(baseline_hash == bypass_hash && baseline_hash != 0,
          "MA2 glide/LFO off must preserve the MA2-2 PCM baseline");

    ma_synth voice;
    ma_synth_init(&voice, 48000.0f);
    ma_synth_set_glide(&voice, true, 1.0f);
    ma_synth_note_on(&voice, 0, 48, 100);
    CHECK(voice.glide.current_note == 48.0f && !voice.glide.remaining,
          "MA2 first played note must not glide from the idle pitch");
    ma_synth_set_glide(&voice, false, 0.0f);
    ma_synth_init(&voice, 48000.0f);
    ma_synth_note_on(&voice, 0, 48, 100);
    ma_synth_set_glide(&voice, true, 0.01f);
    ma_synth_note_on(&voice, 0, 60, 100);
    CHECK(voice.glide.current_note == 48.0f
          && voice.glide.target_note == 60.0f
          && voice.glide.remaining == 480
          && fabsf(voice.glide.step - 0.025f) < 1e-7f,
          "MA2 glide must begin at the card's preceding pitch");
    for (int frame = 0; frame < 240; frame++) (void)ma_synth_tick(&voice);
    float midpoint = voice.glide.current_note;
    ma_synth_note_on(&voice, 0, 72, 100);
    CHECK(voice.glide.current_note == midpoint
          && voice.glide.target_note == 72.0f
          && voice.glide.remaining == 480,
          "MA2 glide retarget must continue from the current pitch");
    for (int frame = 0; frame < 480; frame++) (void)ma_synth_tick(&voice);
    CHECK(voice.glide.current_note == 72.0f && !voice.glide.remaining,
          "MA2 glide must land exactly on its target");
    ma_synth_note_on(&voice, 0, 36, 100);
    (void)ma_synth_tick(&voice);
    ma_synth_set_glide(&voice, false, 9.0f);
    CHECK(voice.glide.current_note == 36.0f && !voice.glide.remaining,
          "MA2 disabling glide must snap to the current target");
    ma_synth_set_glide(&voice, true, NAN);
    CHECK(voice.glide.seconds == 0.0f,
          "MA2 hostile glide time must select the compiled default");
    ma_synth_set_glide(&voice, true, 20.0f);
    CHECK(voice.glide.seconds == 10.0f,
          "MA2 glide time must clamp to ten seconds");

    ma_synth_init(&voice, 48000.0f);
    ma_synth_note_on(&voice, 0, 60, 100);
    float phase = voice.lfo.phase;
    for (int frame = 0; frame < 512; frame++) (void)ma_synth_tick(&voice);
    CHECK(voice.lfo.phase == phase,
          "MA2 depth-zero LFO must leave phase state untouched");
    ma_synth_set_lfo(&voice, 2.0f, NAN);
    for (int frame = 0; frame < 512; frame++) (void)ma_synth_tick(&voice);
    CHECK(voice.lfo.depth == 1.0f && voice.lfo.rate_hz == 1.0f
          && voice.smoothers.lfo_depth.current == 1.0f
          && voice.lfo.phase > 0.0f,
          "MA2 LFO domains, smoothing and phase advance");
    phase = voice.lfo.phase;
    ma_synth_set_lfo(&voice, 0.0f, 20.0f);
    for (int frame = 0; frame < 64; frame++) (void)ma_synth_tick(&voice);
    CHECK(voice.lfo.phase == phase,
          "MA2 LFO bypass must freeze at the disabling phase");

    ma_patch quick = ma_patch_tepih;
    quick.amp_adsr = (ma_adsr){ 1.0f, 1.0f, .5f, 2.0f };
    quick.filter_adsr = quick.amp_adsr;
    ma_card_bank bank;
    ma_card_bank_init_patch(&bank, 48000.0f, &quick);
    (void)ma_card_bank_note_on(&bank, 2, 55, 90);
    (void)ma_card_bank_note_on(&bank, 2, 59, 91);
    ma_card_bank_set_unison(&bank, true);
    CHECK(bank.unison && !bank.sustain
          && bank.owner[0].phase == MA_CARD_RELEASED
          && bank.owner[1].phase == MA_CARD_RELEASED,
          "MA2 unison enter must perform the panic transition first");

    CHECK(ma_card_bank_note_on(&bank, 4, 64, 110) == 0,
          "MA2 unison NoteOn must return its first card");
    bool all = bank.cursor == 0 && bank.next_age == 8;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        all = all && bank.owner[slot].phase == MA_CARD_HELD
                  && bank.owner[slot].channel == 4
                  && bank.owner[slot].note == 64
                  && bank.owner[slot].age == (uint64_t)slot + 3;
    CHECK(all, "MA2 unison play must assign all five cards in slot order");

    (void)ma_card_bank_note_on(&bank, 4, 67, 111);
    all = bank.next_age == 13;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        all = all && bank.owner[slot].phase == MA_CARD_HELD
                  && bank.owner[slot].note == 67
                  && bank.owner[slot].age == (uint64_t)slot + 8;
    CHECK(all && ma_card_bank_note_off(&bank, 4, 64, 0) == MA_CARD_NONE,
          "MA2 unison must replace every card with the newest note");

    ma_card_bank_set_sustain(&bank, true);
    CHECK(ma_card_bank_note_off(&bank, 4, 67, 63) == 0,
          "MA2 unison NoteOff must identify the five-card assignment");
    all = bank.sustain;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        all = all && bank.owner[slot].phase == MA_CARD_SUSTAINED;
    CHECK(all, "MA2 unison release must cover all five cards under sustain");
    ma_card_bank_set_sustain(&bank, false);
    ma_card_bank_set_unison(&bank, false);
    all = !bank.unison && !bank.sustain;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        all = all && bank.owner[slot].phase == MA_CARD_RELEASED;
    CHECK(all, "MA2 unison leave must preserve ordinary release tails");
    CHECK(ma_card_bank_note_on(&bank, 1, 72, 100) == 0
          && bank.owner[0].phase == MA_CARD_HELD
          && bank.owner[1].phase == MA_CARD_RELEASED,
          "MA2 post-unison play must return to the ordinary allocator");
}

static uint64_t ma_raster_hash(ma_patch patch, uint8_t note) {
    ma_synth synth;
    ma_synth_init_patch(&synth, 48000.0f, &patch);
    ma_synth_note_on(&synth, 0, note, 112);
    uint64_t hash = 0;
    for (int frame = 0; frame < 4096; frame++) {
        ma_frame sample = ma_synth_tick(&synth);
        hash = tw_fnv1a64(&sample, sizeof sample, hash);
    }
    return hash;
}

static void test_ma_raster(void) {
    ma_synth baseline, bypass;
    ma_synth_init(&baseline, 48000.0f);
    ma_synth_init(&bypass, 48000.0f);
    ma_synth_set_raster(&bypass, 0.0f, 1.0f, 1.0f);
    ma_synth_note_on(&baseline, 0, 60, 100);
    ma_synth_note_on(&bypass, 0, 60, 100);
    uint64_t baseline_hash = 0, bypass_hash = 0;
    for (int frame = 0; frame < 4096; frame++) {
        ma_frame a = ma_synth_tick(&baseline);
        ma_frame b = ma_synth_tick(&bypass);
        baseline_hash = tw_fnv1a64(&a, sizeof a, baseline_hash);
        bypass_hash = tw_fnv1a64(&b, sizeof b, bypass_hash);
    }
    CHECK(baseline_hash == bypass_hash && baseline_hash != 0
          && bypass.raster.phase_q48 == 0 && bypass.raster.mip == 0,
          "MA2-DIG Raster mix-zero must preserve PCM and phase state");

    ma_patch first = ma_patch_raster;
    ma_patch middle = ma_patch_raster;
    ma_patch last = ma_patch_raster;
    first.raster_position = 0.0f;
    first.raster_warp = 0.0f;
    middle.raster_position = 0.5f;
    last.raster_position = 1.0f;
    uint64_t first_hash = ma_raster_hash(first, 60);
    uint64_t middle_hash = ma_raster_hash(middle, 60);
    uint64_t last_hash = ma_raster_hash(last, 60);
    CHECK(first_hash && middle_hash && last_hash
          && first_hash != middle_hash && middle_hash != last_hash
          && first_hash != last_hash,
          "MA2-DIG Raster spectral positions must produce distinct PCM");
    CHECK(first_hash == ma_raster_hash(first, 60)
          && middle_hash == ma_raster_hash(middle, 60)
          && last_hash == ma_raster_hash(last, 60),
          "MA2-DIG Raster rendering must be deterministic");

    ma_synth low, high;
    ma_synth_init_patch(&low, 48000.0f, &ma_patch_raster);
    ma_synth_init_patch(&high, 48000.0f, &ma_patch_raster);
    ma_synth_note_on(&low, 0, 24, 100);
    ma_synth_note_on(&high, 0, 120, 100);
    (void)ma_synth_tick(&low);
    (void)ma_synth_tick(&high);
    CHECK(low.raster.mip == 0 && high.raster.mip >= 4
          && low.raster.phase_q48 != 0 && high.raster.phase_q48 != 0,
          "MA2-DIG Raster mip selection must follow oscillator pitch");

    ma_synth hostile;
    ma_synth_init(&hostile, 48000.0f);
    ma_synth_set_raster(&hostile, NAN, INFINITY, -INFINITY);
    CHECK(hostile.raster_mix == 0.0f && hostile.raster_position == 0.0f
          && hostile.raster_warp == 0.0f,
          "MA2-DIG hostile Raster controls must select zero defaults");
    ma_synth_set_raster(&hostile, 2.0f, 2.0f, 2.0f);
    CHECK(hostile.raster_mix == 1.0f && hostile.raster_position == 1.0f
          && hostile.raster_warp == 1.0f,
          "MA2-DIG Raster controls must clamp to the unit domain");

    CHECK(ma_patch_raster.raster_mix == 1.0f
          && ma_patch_raster.vco1.saw_level == 0.0f
          && ma_patch_raster.vco1.sine_level == 0.0f
          && ma_patch_raster.mozaik_mix == 0.0f
          && ma_patch_prizma.raster_mix > 0.0f
          && ma_patch_prizma.vco1.sine_level > 0.0f,
          "MA2-DIG factory bank must contain pure Raster and hybrid Prizma");

    static constexpr float rates[] = { 44100.0f, 48000.0f, 96000.0f,
                                       192000.0f };
    static constexpr uint8_t notes[] = { 0, 60, 127 };
    bool finite = true;
    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        for (size_t note = 0; note < sizeof notes / sizeof *notes; note++) {
            ma_synth synth;
            ma_synth_init_patch(&synth, rates[rate], &ma_patch_raster);
            ma_synth_set_raster(&synth, 1.0f, 1.0f, 1.0f);
            ma_synth_note_on(&synth, 15, notes[note], 127);
            for (int frame = 0; frame < 512; frame++) {
                ma_frame sample = ma_synth_tick(&synth);
                finite = finite && isfinite(sample.left)
                                && isfinite(sample.right);
            }
        }
    }
    CHECK(finite, "MA2-DIG Raster full-domain sweep must stay finite");
}

static uint64_t ma_bcs_hash(float regime) {
    ma_patch patch = ma_patch_granica;
    patch.bcs_regime = regime;
    ma_synth synth;
    ma_synth_init_patch(&synth, 48000.0f, &patch);
    ma_synth_note_on(&synth, 0, 43, 112);
    uint64_t hash = 0;
    for (int frame = 0; frame < 8192; frame++) {
        ma_frame sample = ma_synth_tick(&synth);
        hash = tw_fnv1a64(&sample, sizeof sample, hash);
    }
    return hash;
}

static void test_ma_bcs(void) {
    ma_synth baseline, bypass;
    ma_synth_init(&baseline, 48000.0f);
    ma_synth_init(&bypass, 48000.0f);
    ma_synth_set_bcs(&bypass, 0.0f, 1.0f);
    ma_synth_note_on(&baseline, 0, 60, 100);
    ma_synth_note_on(&bypass, 0, 60, 100);
    uint64_t baseline_hash = 0, bypass_hash = 0;
    for (int frame = 0; frame < 4096; frame++) {
        ma_frame a = ma_synth_tick(&baseline);
        ma_frame b = ma_synth_tick(&bypass);
        baseline_hash = tw_fnv1a64(&a, sizeof a, baseline_hash);
        bypass_hash = tw_fnv1a64(&b, sizeof b, bypass_hash);
    }
    CHECK(baseline_hash == bypass_hash && baseline_hash != 0
          && bypass.bcs.hopf_re == 0.025f && bypass.bcs.hopf_im == 0.0f
          && bypass.bcs.duffing_x == 0.001f
          && bypass.bcs.duffing_v == 0.0f && bypass.bcs.output == 0.0f
          && bypass.bcs.maximum_state == 0.0f
          && bypass.bcs.reset_count == 0,
          "MA2-BCS amount-zero must preserve PCM and nonlinear state");

    uint64_t stable = ma_bcs_hash(0.0f);
    uint64_t edge = ma_bcs_hash(1.0f / 3.0f);
    uint64_t subharmonic = ma_bcs_hash(2.0f / 3.0f);
    uint64_t recovery = ma_bcs_hash(1.0f);
    CHECK(stable && edge && subharmonic && recovery
          && stable != edge && stable != subharmonic && stable != recovery
          && edge != subharmonic && edge != recovery
          && subharmonic != recovery,
          "MA2-BCS landmarks must produce four distinct feedback responses");
    CHECK(stable == ma_bcs_hash(0.0f)
          && subharmonic == ma_bcs_hash(2.0f / 3.0f)
          && recovery == ma_bcs_hash(1.0f),
          "MA2-BCS rendering must be deterministic");

    ma_patch control_only = ma_patch_granica;
    control_only.vco1 = (ma_vco_controls){ .pulse_width = 0.5f };
    control_only.vco2 = (ma_vco_controls){ .pulse_width = 0.5f };
    control_only.vco2_level = 0.0f;
    control_only.noise_level = 0.0f;
    control_only.raster_mix = 0.0f;
    control_only.mozaik_mix = 0.0f;
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        control_only.macro[macro] = 0.0f;
    ma_synth silent;
    ma_synth_init_patch(&silent, 48000.0f, &control_only);
    ma_synth_note_on(&silent, 0, 48, 127);
    bool direct_audio = false;
    for (int frame = 0; frame < 4096; frame++) {
        ma_frame sample = ma_synth_tick(&silent);
        direct_audio = direct_audio || sample.left != 0.0f
                                    || sample.right != 0.0f;
    }
    CHECK(!direct_audio && silent.bcs.output != 0.0f
          && silent.bcs.maximum_state > 0.0f,
          "MA2-BCS must remain a control path, not a parallel audio source");

    ma_synth hostile;
    ma_synth_init(&hostile, 48000.0f);
    ma_synth_set_bcs(&hostile, NAN, INFINITY);
    CHECK(hostile.bcs_amount == 0.0f && hostile.bcs_regime == 0.0f,
          "MA2-BCS hostile controls must select bypass defaults");
    ma_synth_set_bcs(&hostile, 2.0f, 2.0f);
    CHECK(hostile.bcs_amount == 1.0f && hostile.bcs_regime == 1.0f
          && hostile.smoothers.bcs_amount.remaining == 288
          && hostile.smoothers.bcs_regime.remaining == 288,
          "MA2-BCS controls must clamp and use the common smoother");
    hostile.smoothers.bcs_amount = (ma_smoother){
        .current = 1.0f, .target = 1.0f,
    };
    hostile.bcs.hopf_re = INFINITY;
    (void)ma_synth_tick(&hostile);
    CHECK(hostile.bcs.reset_count == 1 && hostile.bcs.hopf_re == 0.025f
          && hostile.bcs.duffing_x == 0.001f
          && hostile.bcs.output == 0.0f,
          "MA2-BCS unsafe state must recover to the pinned seed");

    static constexpr float rates[] = { 44100.0f, 48000.0f, 96000.0f,
                                       192000.0f };
    static constexpr uint8_t notes[] = { 0, 60, 127 };
    bool finite = true;
    for (size_t rate = 0; rate < sizeof rates / sizeof *rates; rate++) {
        for (size_t note = 0; note < sizeof notes / sizeof *notes; note++) {
            ma_synth synth;
            ma_synth_init_patch(&synth, rates[rate], &ma_patch_granica);
            ma_synth_set_bcs(&synth, 1.0f, 1.0f);
            ma_synth_note_on(&synth, 15, notes[note], 127);
            for (int frame = 0; frame < 1024; frame++) {
                ma_frame sample = ma_synth_tick(&synth);
                finite = finite && isfinite(sample.left)
                                && isfinite(sample.right)
                                && isfinite(synth.bcs.output)
                                && fabsf(synth.bcs.output) <= 1.0f
                                && synth.bcs.maximum_state
                                    <= 32.0f;
            }
        }
    }
    CHECK(finite, "MA2-BCS full-domain sweep must stay finite and bounded");
    CHECK(ma_patch_granica.bcs_amount > 0.0f
          && ma_patch_granica.bcs_regime > 0.5f
          && ma_patch_granica.vco1.pulse_level == 0.0f
          && ma_patch_granica.vco2.pulse_level == 0.0f,
          "MA2-BCS factory Granica patch must expose the nonlinear coordinate");
}

void test_ma_suite(void) {
    test_ma_foundation();
    test_ma_oscillators();
    test_ma_mozaik();
    test_ma_filter();
    test_ma_envelopes();
    test_ma_identity_and_performance();
    test_ma_output();
    test_ma_card_bank();
    test_ma_card_bank_sustain_and_panic();
    test_ma_glide_lfo_and_unison();
    test_ma_raster();
    test_ma_bcs();
}
