/* Hosted MA2-2 performance study. Three fixed five-card banks make the
 * arrangement audible; they are overdub roles, not the final MA2 mixer. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/mamutanalog.h"
#include "ma_dark_lead.h"
#include "wav.h"

enum {
    RATE = 48000,
    PERFORMANCE_SECONDS = 216,
    TAIL_SECONDS = 14,
    FRAME_COUNT = RATE * (PERFORMANCE_SECONDS + TAIL_SECONDS),
    BLOCK_FRAMES = 256,
    CONTROL_PERIOD = 64,
    COMB_COUNT = 4,
    COMB_CAPACITY = 1536,
    ALLPASS_COUNT = 2,
    ALLPASS_CAPACITY = 640,
};

static constexpr char OUTPUT_PATH[] =
    "build/ma_blade_runner_blues_panic.wav";
static constexpr float TWO_PI = 6.2831853071795864769f;

typedef enum {
    BANK_PAD,
    BANK_BASS,
    BANK_LEAD,
    BANK_COUNT,
} bank_id;

enum {
    PAD_MASK = 1u << BANK_PAD,
    BASS_MASK = 1u << BANK_BASS,
    LEAD_MASK = 1u << BANK_LEAD,
    ALL_BANKS_MASK = PAD_MASK | BASS_MASK | LEAD_MASK,
};

typedef struct {
    uint16_t at_seconds;
    uint8_t note[4];
    uint8_t velocity;
} chord_event;

typedef struct {
    uint16_t at_seconds;
    uint8_t held_seconds;
    uint8_t note;
    uint8_t velocity;
} note_event;

typedef enum {
    CONTROL_SUSTAIN_DOWN,
    CONTROL_SUSTAIN_UP,
    CONTROL_PANIC,
} control_kind;

typedef struct {
    uint16_t at_seconds;
    uint8_t bank_mask;
    control_kind kind;
} control_event;

typedef struct {
    float comb_l[COMB_COUNT][COMB_CAPACITY];
    float comb_r[COMB_COUNT][COMB_CAPACITY];
    float comb_filter_l[COMB_COUNT];
    float comb_filter_r[COMB_COUNT];
    unsigned comb_position_l[COMB_COUNT];
    unsigned comb_position_r[COMB_COUNT];
    float allpass_l[ALLPASS_COUNT][ALLPASS_CAPACITY];
    float allpass_r[ALLPASS_COUNT][ALLPASS_CAPACITY];
    unsigned allpass_position_l[ALLPASS_COUNT];
    unsigned allpass_position_r[ALLPASS_COUNT];
} performance_reverb;

typedef struct {
    uint64_t hash;
    double sum_squares;
    float peak;
    unsigned notes;
    unsigned steals;
    unsigned panics;
    unsigned nonfinite;
    unsigned clipped;
    unsigned peak_cards;
} performance_metrics;

typedef struct {
    ma_card_bank bank[BANK_COUNT];
    performance_reverb reverb;
    performance_metrics metrics;
} performance;

static const chord_event PAD_CHORDS[] = {
    {   0, { 42, 49, 52, 57 }, 72 },
    {  18, { 38, 45, 49, 54 }, 70 },
    {  36, { 40, 47, 52, 56 }, 71 },
    {  54, { 37, 44, 49, 53 }, 73 },
    {  74, { 42, 49, 52, 57 }, 74 },
    {  92, { 35, 42, 47, 50 }, 70 },
    { 110, { 38, 45, 49, 54 }, 72 },
    { 128, { 40, 47, 52, 56 }, 71 },
    { 146, { 42, 49, 52, 57 }, 75 },
    { 164, { 37, 44, 49, 53 }, 72 },
    { 182, { 38, 45, 49, 54 }, 73 },
    { 200, { 40, 47, 52, 56 }, 70 },
};

static const note_event BASS_NOTES[] = {
    {   0, 6, 30, 92 }, {   7, 5, 30, 84 },
    {  14, 6, 37, 88 }, {  21, 5, 33, 86 },
    {  28, 6, 28, 90 }, {  35, 5, 35, 84 },
    {  42, 6, 30, 91 }, {  49, 5, 37, 87 },
    {  56, 6, 25, 89 }, {  63, 5, 32, 85 },
    {  74, 6, 30, 94 }, {  81, 5, 37, 88 },
    {  88, 6, 33, 90 }, {  95, 5, 28, 86 },
    { 102, 6, 35, 92 }, { 109, 5, 30, 87 },
    { 116, 6, 37, 91 }, { 123, 5, 40, 86 },
    { 130, 6, 28, 90 }, { 137, 5, 25, 84 },
    { 146, 6, 30, 95 }, { 153, 5, 30, 88 },
    { 160, 6, 37, 92 }, { 167, 5, 33, 87 },
    { 174, 6, 28, 93 }, { 181, 5, 35, 86 },
    { 188, 6, 30, 94 }, { 195, 5, 37, 89 },
    { 202, 6, 25, 91 }, { 209, 5, 32, 86 },
};

static const note_event LEAD_NOTES[] = {
    {  22, 10, 54,  98 },
    {  39, 10, 57, 102 },
    {  58,  9, 61, 106 },
    {  82, 11, 54, 100 },
    { 100,  9, 59, 104 },
    { 121, 11, 57, 106 },
    { 154, 10, 54, 102 },
    { 170,  9, 57, 106 },
    { 188, 10, 61, 110 },
    { 204,  9, 59, 108 },
};

static const control_event CONTROLS[] = {
    {   0, PAD_MASK, CONTROL_SUSTAIN_DOWN },
    {  28, BASS_MASK, CONTROL_SUSTAIN_DOWN },
    {  52, BASS_MASK, CONTROL_SUSTAIN_UP },
    {  66, PAD_MASK, CONTROL_SUSTAIN_UP },
    {  72, ALL_BANKS_MASK, CONTROL_PANIC },
    {  74, PAD_MASK, CONTROL_SUSTAIN_DOWN },
    {  96, LEAD_MASK, CONTROL_SUSTAIN_DOWN },
    { 104, BASS_MASK, CONTROL_SUSTAIN_DOWN },
    { 128, BASS_MASK, CONTROL_SUSTAIN_UP },
    { 134, LEAD_MASK, CONTROL_SUSTAIN_UP },
    { 138, PAD_MASK, CONTROL_SUSTAIN_UP },
    { 144, ALL_BANKS_MASK, CONTROL_PANIC },
    { 146, PAD_MASK, CONTROL_SUSTAIN_DOWN },
    { 174, BASS_MASK, CONTROL_SUSTAIN_DOWN },
    { 198, BASS_MASK, CONTROL_SUSTAIN_UP },
    { 210, PAD_MASK, CONTROL_SUSTAIN_UP },
    { 216, ALL_BANKS_MASK, CONTROL_PANIC },
};

static size_t second_frame(unsigned seconds) {
    return (size_t)seconds * RATE;
}

static ma_patch pad_patch(void) {
    ma_patch patch = ma_patch_tepih;
    patch.vco1.saw_level = .38f;
    patch.vco1.pulse_level = .08f;
    patch.vco1.triangle_level = .34f;
    patch.vco1.sine_level = .44f;
    patch.vco2.saw_level = .16f;
    patch.vco2.pulse_level = 0.0f;
    patch.vco2.triangle_level = .48f;
    patch.vco2.sine_level = .36f;
    patch.vco2_level = .58f;
    patch.noise_level = .012f;
    patch.mozaik_mix = .13f;
    patch.filter_cutoff_hz = 820.0f;
    patch.filter_resonance = .15f;
    patch.filter_drive = .08f;
    patch.amp_adsr = (ma_adsr){ 980.0f, 2300.0f, .76f, 7200.0f };
    patch.filter_adsr = (ma_adsr){ 760.0f, 2500.0f, .44f, 6200.0f };
    patch.macro[MA_MACRO_BLOOM] = .15f;
    patch.macro[MA_MACRO_SWARM] = .06f;
    patch.body_drive = .06f;
    patch.master_level = .13f;
    return patch;
}

static ma_patch bass_patch(void) {
    ma_patch patch = ma_patch_dubina;
    patch.filter_cutoff_hz = 570.0f;
    patch.filter_resonance = .18f;
    patch.filter_drive = .14f;
    patch.amp_adsr = (ma_adsr){ 55.0f, 420.0f, .76f, 3000.0f };
    patch.filter_adsr = (ma_adsr){ 90.0f, 620.0f, .40f, 2500.0f };
    patch.master_level = .14f;
    return patch;
}

static ma_patch lead_patch(void) {
    ma_patch patch = ma_dark_lead_patch();
    patch.amp_adsr.attack_ms = 460.0f;
    patch.amp_adsr.release_ms = 8200.0f;
    patch.filter_adsr.release_ms = 7000.0f;
    patch.filter_cutoff_hz = 940.0f;
    patch.master_level = .17f;
    return patch;
}

static void performance_init(performance *p) {
    *p = (performance){ 0 };
    ma_patch pad = pad_patch();
    ma_patch bass = bass_patch();
    ma_patch lead = lead_patch();
    ma_card_bank_init_patch(&p->bank[BANK_PAD], RATE, &pad);
    ma_card_bank_init_patch(&p->bank[BANK_BASS], RATE, &bass);
    ma_card_bank_init_patch(&p->bank[BANK_LEAD], RATE, &lead);
}

static uint8_t start_note(performance *p, bank_id id, uint8_t channel,
                          uint8_t note, uint8_t velocity, float pressure) {
    ma_card_bank *bank = &p->bank[id];
    bool full = true;
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
        full = full && bank->owner[slot].phase != MA_CARD_IDLE;
    uint8_t slot = ma_card_bank_note_on(bank, channel, note, velocity);
    if (slot == MA_CARD_NONE) return slot;
    p->metrics.notes++;
    p->metrics.steals += full;
    ma_synth_set_poly_pressure(&bank->card[slot], channel, note, pressure);
    return slot;
}

static void process_chords(performance *p, size_t frame) {
    for (size_t event = 0; event < sizeof PAD_CHORDS / sizeof *PAD_CHORDS;
         event++) {
        chord_event const *chord = &PAD_CHORDS[event];
        if (frame == second_frame(chord->at_seconds + 14))
            for (uint8_t note = 0; note < 4; note++)
                (void)ma_card_bank_note_off(&p->bank[BANK_PAD], BANK_PAD,
                                            chord->note[note], 0);
        if (frame == second_frame(chord->at_seconds))
            for (uint8_t note = 0; note < 4; note++)
                (void)start_note(p, BANK_PAD, BANK_PAD, chord->note[note],
                                 chord->velocity, .18f + .02f * note);
    }
}

static void process_notes(performance *p, bank_id id, uint8_t channel,
                          const note_event *events, size_t count,
                          size_t frame) {
    for (size_t event = 0; event < count; event++) {
        note_event const *note = &events[event];
        if (frame == second_frame(note->at_seconds + note->held_seconds))
            (void)ma_card_bank_note_off(&p->bank[id], channel,
                                        note->note, 0);
        if (frame == second_frame(note->at_seconds)) {
            float pressure = id == BANK_BASS ? .24f : .38f;
            (void)start_note(p, id, channel, note->note, note->velocity,
                             pressure);
        }
    }
}

static void process_controls(performance *p, size_t frame) {
    for (size_t event = 0; event < sizeof CONTROLS / sizeof *CONTROLS;
         event++) {
        control_event const *control = &CONTROLS[event];
        if (frame != second_frame(control->at_seconds)) continue;
        for (uint8_t id = 0; id < BANK_COUNT; id++) {
            if (!(control->bank_mask & (1u << id))) continue;
            if (control->kind == CONTROL_PANIC)
                ma_card_bank_panic(&p->bank[id]);
            else
                ma_card_bank_set_sustain(
                    &p->bank[id], control->kind == CONTROL_SUSTAIN_DOWN);
        }
        p->metrics.panics += control->kind == CONTROL_PANIC;
    }
}

static void update_expression(performance *p, size_t frame) {
    float seconds = (float)frame / RATE;
    float pad_pressure = .12f + .08f * (1.0f + sinf(.041f * seconds));
    float bass_pressure = .16f + .05f * (1.0f + sinf(.067f * seconds));
    float lead_pressure = .20f + .13f * (1.0f + sinf(.053f * seconds));
    float lead_bend = .035f * sinf(TWO_PI * 3.4f * seconds)
                    + .018f * sinf(TWO_PI * .071f * seconds);
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        ma_synth_set_channel_pressure(&p->bank[BANK_PAD].card[slot],
                                      pad_pressure);
        ma_synth_set_mod_wheel(&p->bank[BANK_PAD].card[slot], .18f);
        ma_synth_set_channel_pressure(&p->bank[BANK_BASS].card[slot],
                                      bass_pressure);
        ma_synth_set_mod_wheel(&p->bank[BANK_BASS].card[slot], .07f);
        ma_synth_set_channel_pressure(&p->bank[BANK_LEAD].card[slot],
                                      lead_pressure);
        ma_synth_set_mod_wheel(&p->bank[BANK_LEAD].card[slot], .24f);
        ma_synth_set_pitch_bend(&p->bank[BANK_LEAD].card[slot], lead_bend);
    }
}

static void process_events(performance *p, size_t frame) {
    if (frame % CONTROL_PERIOD) return;
    process_controls(p, frame);
    process_chords(p, frame);
    process_notes(p, BANK_BASS, BANK_BASS, BASS_NOTES,
                  sizeof BASS_NOTES / sizeof *BASS_NOTES, frame);
    process_notes(p, BANK_LEAD, BANK_LEAD, LEAD_NOTES,
                  sizeof LEAD_NOTES / sizeof *LEAD_NOTES, frame);
    update_expression(p, frame);
}

static ma_frame mix_bank(const ma_frame frames[MA_CARD_COUNT],
                         float left_gain, float right_gain) {
    ma_frame mixed = { 0 };
    for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++) {
        float mono = .5f * (frames[slot].left + frames[slot].right);
        mixed.left += left_gain * mono;
        mixed.right += right_gain * mono;
    }
    return mixed;
}

static float comb_tick(float buffer[COMB_CAPACITY], unsigned length,
                       unsigned *position, float *filtered, float input) {
    float delayed = buffer[*position];
    *filtered = .76f * delayed + .24f * *filtered;
    buffer[*position] = input + .79f * *filtered;
    *position = (*position + 1) % length;
    return delayed;
}

static float allpass_tick(float buffer[ALLPASS_CAPACITY], unsigned length,
                          unsigned *position, float input) {
    float delayed = buffer[*position];
    float output = delayed - input;
    buffer[*position] = input + .52f * delayed;
    *position = (*position + 1) % length;
    return output;
}

static ma_frame reverb_tick(performance_reverb *reverb, ma_frame dry) {
    static const unsigned COMB_LENGTH_L[COMB_COUNT] = {
        1215, 1293, 1397, 1473,
    };
    static const unsigned COMB_LENGTH_R[COMB_COUNT] = {
        1238, 1316, 1420, 1496,
    };
    static const unsigned ALLPASS_LENGTH_L[ALLPASS_COUNT] = { 225, 556 };
    static const unsigned ALLPASS_LENGTH_R[ALLPASS_COUNT] = { 248, 579 };
    float input_l = .74f * dry.left + .26f * dry.right;
    float input_r = .74f * dry.right + .26f * dry.left;
    float wet_l = 0.0f, wet_r = 0.0f;
    for (unsigned i = 0; i < COMB_COUNT; i++) {
        wet_l += comb_tick(reverb->comb_l[i], COMB_LENGTH_L[i],
                           &reverb->comb_position_l[i],
                           &reverb->comb_filter_l[i], input_l);
        wet_r += comb_tick(reverb->comb_r[i], COMB_LENGTH_R[i],
                           &reverb->comb_position_r[i],
                           &reverb->comb_filter_r[i], input_r);
    }
    wet_l *= .25f;
    wet_r *= .25f;
    for (unsigned i = 0; i < ALLPASS_COUNT; i++) {
        wet_l = allpass_tick(reverb->allpass_l[i], ALLPASS_LENGTH_L[i],
                             &reverb->allpass_position_l[i], wet_l);
        wet_r = allpass_tick(reverb->allpass_r[i], ALLPASS_LENGTH_R[i],
                             &reverb->allpass_position_r[i], wet_r);
    }
    return (ma_frame){
        .left = .74f * dry.left + .46f * wet_l,
        .right = .74f * dry.right + .46f * wet_r,
    };
}

static ma_frame performance_tick(performance *p, size_t frame) {
    process_events(p, frame);
    ma_frame cards[BANK_COUNT][MA_CARD_COUNT] = { 0 };
    for (uint8_t id = 0; id < BANK_COUNT; id++)
        ma_card_bank_tick(&p->bank[id], cards[id]);

    ma_frame pad = mix_bank(cards[BANK_PAD], .25f, .30f);
    ma_frame bass = mix_bank(cards[BANK_BASS], .36f, .29f);
    ma_frame lead = mix_bank(cards[BANK_LEAD], .27f, .38f);
    ma_frame dry = {
        .left = pad.left + bass.left + lead.left,
        .right = pad.right + bass.right + lead.right,
    };
    ma_frame output = reverb_tick(&p->reverb, dry);
    output.left *= 1.65f;
    output.right *= 1.65f;

    unsigned active = 0;
    for (uint8_t id = 0; id < BANK_COUNT; id++)
        for (uint8_t slot = 0; slot < MA_CARD_COUNT; slot++)
            active += p->bank[id].owner[slot].phase != MA_CARD_IDLE;
    if (active > p->metrics.peak_cards) p->metrics.peak_cards = active;
    return output;
}

static bool render_pass(performance_metrics *metrics,
                        wav_f32_writer *writer) {
    performance p;
    performance_init(&p);
    float block[2 * BLOCK_FRAMES] = { 0 };
    for (size_t first = 0; first < FRAME_COUNT; first += BLOCK_FRAMES) {
        size_t count = FRAME_COUNT - first;
        if (count > BLOCK_FRAMES) count = BLOCK_FRAMES;
        for (size_t i = 0; i < count; i++) {
            ma_frame sample = performance_tick(&p, first + i);
            if (!isfinite(sample.left) || !isfinite(sample.right)) {
                p.metrics.nonfinite++;
                sample = (ma_frame){ 0 };
            }
            float left = fabsf(sample.left), right = fabsf(sample.right);
            float peak = left > right ? left : right;
            if (peak > p.metrics.peak) p.metrics.peak = peak;
            p.metrics.clipped += peak > 1.0f;
            p.metrics.sum_squares += (double)sample.left * sample.left
                                   + (double)sample.right * sample.right;
            block[2 * i] = sample.left;
            block[2 * i + 1] = sample.right;
        }
        p.metrics.hash = tw_fnv1a64(block, 2 * count * sizeof *block,
                                    p.metrics.hash);
        if (writer && wav_f32_write(writer, block, count) < 0) return false;
    }
    *metrics = p.metrics;
    return metrics->nonfinite == 0 && metrics->clipped == 0
        && metrics->peak > 1e-4f && metrics->sum_squares > 1e-8;
}

int main(void) {
    puts("Mamut Analog — Blade Runner Blues Panic");
    puts("  three hosted five-card overdub banks; MA2-2 ownership is real");
    puts("  sustain, repeated notes, oldest-card stealing and release panic");

    performance_metrics first = { 0 }, second = { 0 };
    bool first_ok = render_pass(&first, nullptr);
    wav_f32_writer writer = { 0 };
    bool opened = first_ok
               && wav_f32_open(&writer, OUTPUT_PATH, FRAME_COUNT,
                               RATE, 2) == 0;
    bool second_ok = opened && render_pass(&second, &writer);
    bool deterministic = second_ok && first.hash == second.hash;
    bool written = deterministic && wav_f32_close(&writer) == 0;
    if (!written) wav_f32_abort(&writer);
    double rms = sqrt(second.sum_squares / (2.0 * FRAME_COUNT));

    printf("  %d s + %d s tail, %u notes, %u-card peak, %u steals\n",
           PERFORMANCE_SECONDS, TAIL_SECONDS, second.notes,
           second.peak_cards, second.steals);
    printf("  %u musical panic transitions\n", second.panics);
    printf("  peak %.6f, RMS %.6f, finite %s, headroom %s\n",
           (double)second.peak, rms,
           second.nonfinite == 0 ? "yes" : "NO",
           second.clipped == 0 ? "yes" : "NO");
    printf("  FNV64 %016llx %s\n", (unsigned long long)second.hash,
           deterministic ? "(two runs identical)" : "MISMATCH");
    printf("  wav: %s%s\n", OUTPUT_PATH, written ? "" : " (FAILED)");
    return written ? 0 : 1;
}
