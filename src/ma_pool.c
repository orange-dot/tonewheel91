/* MA2-7 bounded digital voice pool; only awake slots run voice DSP. */
#include "ma_internal.h"

const ma_voice_pool_config ma_voice_pool_default_config = {
    .limit = 32, .unison_count = 1, .detune_cents = 7,
    .session_seed = MA_POOL_DEFAULT_SEED,
};

static uint64_t slot_seed(const ma_voice_pool *p, unsigned slot) {
    return p->config.session_seed
        ^ ((uint64_t)(slot + 1) * UINT64_C(0x9e3779b97f4a7c15));
}
static float draw(uint64_t *state) {
    return (float)(tw_splitmix64(state) >> 40) / 8388608.0f - 1.0f;
}
static ma_character character(const ma_voice_pool *p, unsigned slot) {
    uint64_t state = slot_seed(p, slot);
    ma_character c = { .assigned = true, .amount = .2f,
        .smoother = { .current = .2f, .target = .2f } };
    c.vco1_cents = 6 * draw(&state);
    c.vco2_cents = 8 * draw(&state);
    c.cutoff_octaves = .12f * draw(&state);
    c.amp_time_bias.attack = .08f * draw(&state);
    c.amp_time_bias.decay = .08f * draw(&state);
    c.amp_time_bias.release = .08f * draw(&state);
    c.filter_time_bias.attack = .08f * draw(&state);
    c.filter_time_bias.decay = .08f * draw(&state);
    c.filter_time_bias.release = .08f * draw(&state);
    c.vca_bias = .03f * draw(&state);
    c.walk_state = tw_splitmix64(&state);
    return c;
}

/* Volatile byte reads prevent compiler-generated hosted memcpy calls. This
 * bounded copy runs only at init/reset/wake, never in the sample loop. */
static void copy_voice(ma_synth *to, const ma_synth *from) {
    unsigned char *dst = (unsigned char *)to;
    const volatile unsigned char *src = (const volatile unsigned char *)from;
    for (size_t i = 0; i < sizeof *to; i++) dst[i] = src[i];
}

static void initialize_slots(ma_voice_pool *p) {
    p->next_allocation = 1;
    p->transition_frames = (uint32_t)(p->control.sample_rate_hz * .005f + .5f);
    p->group_gain = p->config.unison_count == 1 ? 1.0f
        : p->config.unison_count == 2 ? .7071067811865475f
        : p->config.unison_count == 4 ? .5f : .3535533905932738f;
    p->pan_width = -1;
    p->output = p->control.output;
    for (unsigned i = 0; i < MA_POOL_CAPACITY; i++) {
        copy_voice(&p->slot[i].voice, &p->control);
        p->slot[i].voice.character = character(p, i);
    }
}

bool ma_voice_pool_init(ma_voice_pool *p, float rate,
    const ma_voice_pool_config *config, const ma_patch *patch) {
    if (!p) return false;
    ma_voice_pool_config c = config ? *config : ma_voice_pool_default_config;
    if (c.limit < 1 || c.limit > MA_POOL_CAPACITY
        || (c.unison_count != 1 && c.unison_count != 2
            && c.unison_count != 4 && c.unison_count != 8)
        || c.limit % c.unison_count
        || !(c.detune_cents >= 0 && c.detune_cents <= 100)) return false;
    *p = (ma_voice_pool){ .config = c };
    ma_synth_init_patch(&p->control, rate, patch ? patch : &ma_patch_tepih);
    initialize_slots(p);
    return true;
}

void ma_voice_pool_reset(ma_voice_pool *p) {
    ma_synth control;
    copy_voice(&control, &p->control);
    ma_voice_snap_controls(&control);
    float amount = p->slot[0].voice.character.amount;
    ma_voice_pool_config config = p->config;
    *p = (ma_voice_pool){ 0 };
    p->config = config;
    copy_voice(&p->control, &control);
    initialize_slots(p);
    ma_voice_pool_set_character(p, amount);
}

static void release_group(ma_voice_pool *p, unsigned g, uint8_t velocity) {
    ma_pool_group *group = &p->group[g];
    group->phase = MA_CARD_RELEASED;
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake && p->slot[i].group == g)
            ma_synth_note_off(&p->slot[i].voice, group->channel, group->note, velocity);
}

uint64_t ma_voice_pool_note_off(ma_voice_pool *p, uint8_t channel,
    uint8_t note, uint8_t velocity) {
    if (channel >= 16 || note >= 128) return 0;
    unsigned chosen = MA_POOL_CAPACITY;
    for (unsigned g = 0; g < p->config.limit; g++) {
        const ma_pool_group *group = &p->group[g];
        if (group->phase == MA_CARD_HELD && group->channel == channel
            && group->note == note && (chosen == MA_POOL_CAPACITY
            || group->allocation_id < p->group[chosen].allocation_id)) chosen = g;
    }
    if (chosen == MA_POOL_CAPACITY) return 0;
    if (p->sustain[channel]) p->group[chosen].phase = MA_CARD_SUSTAINED;
    else release_group(p, chosen, velocity);
    return p->group[chosen].allocation_id;
}

static unsigned priority(ma_card_phase phase) {
    return phase == MA_CARD_RELEASED ? 0 : phase == MA_CARD_SUSTAINED ? 1 : 2;
}
static unsigned victim(const ma_voice_pool *p) {
    unsigned chosen = MA_POOL_CAPACITY;
    float levels[MA_POOL_CAPACITY] = { 0 };
    for (unsigned i = 0; i < p->config.limit; i++) {
        const ma_pool_slot *s = &p->slot[i];
        if (s->awake && s->voice.amp_envelope.level > levels[s->group])
            levels[s->group] = s->voice.amp_envelope.level;
    }
    for (unsigned g = 0; g < p->config.limit; g++) {
        const ma_pool_group *a = &p->group[g];
        if (a->phase == MA_CARD_IDLE) continue;
        if (chosen == MA_POOL_CAPACITY) { chosen = g; continue; }
        const ma_pool_group *b = &p->group[chosen];
        unsigned ap = priority(a->phase), bp = priority(b->phase);
        bool older = a->allocation_id < b->allocation_id;
        if (ap < bp || (ap == bp && (ap == 2 ? older
            : levels[g] < levels[chosen]
                || (levels[g] == levels[chosen] && older)))) chosen = g;
    }
    return chosen;
}

static void steal(ma_voice_pool *p, unsigned g) {
    ma_card_phase phase = p->group[g].phase;
    if (phase == MA_CARD_RELEASED) p->diagnostics.steals_released++;
    else if (phase == MA_CARD_SUSTAINED) p->diagnostics.steals_sustained++;
    else p->diagnostics.steals_held++;
    for (unsigned i = 0; i < p->config.limit; i++) {
        ma_pool_slot *s = &p->slot[i];
        if (!s->awake || s->group != g) continue;
        s->transition_from = s->last_contribution;
        s->transition_remaining = p->transition_frames;
        s->awake = false;
        p->diagnostics.active_slots--;
    }
    p->group[g] = (ma_pool_group){ 0 };
}

uint64_t ma_voice_pool_note_on(ma_voice_pool *p, uint8_t channel,
    uint8_t note, uint8_t velocity) {
    if (channel >= 16 || note >= 128) return 0;
    if (!velocity) { (void)ma_voice_pool_note_off(p, channel, note, 0); return 0; }
    if (!p->next_allocation) return 0;
    unsigned count = p->config.unison_count;
    while (p->config.limit - p->diagnostics.active_slots < count)
        steal(p, victim(p));
    unsigned g = 0;
    while (p->group[g].phase != MA_CARD_IDLE) g++;
    uint64_t id = p->next_allocation++;
    p->group[g] = (ma_pool_group){ .allocation_id = id,
        .channel = channel, .note = note, .phase = MA_CARD_HELD };
    for (unsigned member = 0; member < count; member++) {
        unsigned i = p->cursor;
        while (p->slot[i].awake) i = (i + 1) % p->config.limit;
        p->cursor = (uint8_t)((i + 1) % p->config.limit);
        ma_pool_slot *s = &p->slot[i];
        ma_character c = s->voice.character;
        copy_voice(&s->voice, &p->control);
        s->voice.character = c;
        ma_synth_set_pitch_bend(&s->voice, p->bend[channel]);
        ma_synth_set_channel_pressure(&s->voice, p->pressure[channel]);
        ma_synth_set_mod_wheel(&s->voice, p->mod_wheel[channel]);
        ma_synth_note_on(&s->voice, channel, note, velocity);
        ma_voice_snap_controls(&s->voice);
        uint64_t seed = slot_seed(p, i) ^ (id * UINT64_C(0xd1b54a32d192ed03));
        s->voice.oscillator1.phase_q48 = tw_splitmix64(&seed) >> 16;
        s->voice.oscillator2.phase_q48 = tw_splitmix64(&seed) >> 16;
        s->voice.oscillator1.triangle_initialized = false;
        s->voice.oscillator2.triangle_initialized = false;
        s->voice.raster.phase_q48 = tw_splitmix64(&seed) >> 16;
        s->voice.noise_state = tw_splitmix64(&seed);
        s->detune_cents = count == 1 ? 0 : p->config.detune_cents
            * (float)(2 * (int)member - (int)count + 1) / (float)(count - 1);
        s->allocation_id = id;
        s->group = (uint8_t)g;
        s->awake = true;
        p->diagnostics.active_slots++;
        p->diagnostics.wakes++;
    }
    if (p->diagnostics.active_slots > p->diagnostics.peak_slots)
        p->diagnostics.peak_slots = p->diagnostics.active_slots;
    return id;
}

void ma_voice_pool_set_sustain(ma_voice_pool *p, uint8_t channel, bool down) {
    if (channel >= 16) return;
    p->sustain[channel] = down;
    if (!down)
        for (unsigned g = 0; g < p->config.limit; g++)
            if (p->group[g].phase == MA_CARD_SUSTAINED
                && p->group[g].channel == channel) release_group(p, g, 0);
}
void ma_voice_pool_panic(ma_voice_pool *p) {
    for (unsigned ch = 0; ch < 16; ch++) p->sustain[ch] = false;
    for (unsigned g = 0; g < p->config.limit; g++)
        if (p->group[g].phase == MA_CARD_HELD || p->group[g].phase == MA_CARD_SUSTAINED)
            release_group(p, g, 0);
}

static float bounded(float x, float lo, float hi) {
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) return 0;
    return x < lo ? lo : x > hi ? hi : x;
}
void ma_voice_pool_set_pitch_bend(ma_voice_pool *p, uint8_t ch, float x) {
    if (ch >= 16) return;
    p->bend[ch] = bounded(x, -2, 2);
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake && p->slot[i].voice.channel == ch)
            ma_synth_set_pitch_bend(&p->slot[i].voice, p->bend[ch]);
}
void ma_voice_pool_set_channel_pressure(ma_voice_pool *p, uint8_t ch, float x) {
    if (ch >= 16) return;
    p->pressure[ch] = bounded(x, 0, 1);
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake && p->slot[i].voice.channel == ch)
            ma_synth_set_channel_pressure(&p->slot[i].voice, p->pressure[ch]);
}
void ma_voice_pool_set_mod_wheel(ma_voice_pool *p, uint8_t ch, float x) {
    if (ch >= 16) return;
    p->mod_wheel[ch] = bounded(x, 0, 1);
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake && p->slot[i].voice.channel == ch)
            ma_synth_set_mod_wheel(&p->slot[i].voice, p->mod_wheel[ch]);
}
void ma_voice_pool_set_poly_pressure(ma_voice_pool *p, uint8_t ch,
    uint8_t note, float x) {
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake)
            ma_synth_set_poly_pressure(&p->slot[i].voice, ch, note, x);
}
void ma_voice_pool_set_macro(ma_voice_pool *p, ma_macro_id macro, float x) {
    ma_synth_set_macro(&p->control, macro, x);
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake) ma_synth_set_macro(&p->slot[i].voice, macro, x);
}
void ma_voice_pool_set_character(ma_voice_pool *p, float x) {
    for (unsigned i = 0; i < MA_POOL_CAPACITY; i++)
        p->slot[i].voice.character.amount = bounded(x, 0, 1);
}
void ma_voice_pool_set_output(ma_voice_pool *p, float drive, float width,
    float crossfeed, float master) {
    ma_synth_set_output(&p->control, drive, width, crossfeed, master);
    if (p->control.width == 0) p->control.smoothers.width = (ma_smoother){ 0 };
}
void ma_voice_pool_set_patch(ma_voice_pool *p, const ma_patch *patch) {
    ma_voice_patch_controls(&p->control, patch);
    if (p->control.width == 0) p->control.smoothers.width = (ma_smoother){ 0 };
    for (unsigned i = 0; i < p->config.limit; i++)
        if (p->slot[i].awake) ma_voice_patch_controls(&p->slot[i].voice, patch);
}

static void update_pan(ma_voice_pool *p, float width, float dispersion) {
    if (p->pan_width == width && p->pan_dispersion == dispersion) return;
    p->pan_width = width;
    p->pan_dispersion = dispersion;
    for (unsigned i = 0; i < p->config.limit; i++) {
        float base = p->config.limit == 1 ? 0
            : .75f * (float)(2 * (int)i - (int)p->config.limit + 1)
                / (float)(p->config.limit - 1);
        float position = width == 0 ? 0 : base * width + base * .10f * dispersion * width;
        position = bounded(position, -1, 1);
        ma_card_pan pan = { .position = position, .left = 1, .right = 1 };
        if (position != 0) {
            float turns = (position + 1) * .125f;
            pan.left = 1.41421356237f * tw_sin_turns(.25f - turns);
            pan.right = 1.41421356237f * tw_sin_turns(turns);
        }
        p->pan[i] = pan;
    }
}

ma_frame ma_voice_pool_tick(ma_voice_pool *p) {
    ma_voice_tick_controls(&p->control);
    ma_synth *control = &p->control;
    float width = control->width == 0 ? 0 : control->smoothers.width.current;
    update_pan(p, width, control->smoothers.spatial_dispersion.current);
    float left = 0, right = 0;
    bool live_group[MA_POOL_CAPACITY] = { false };
    for (unsigned i = 0; i < p->config.limit; i++) {
        ma_pool_slot *s = &p->slot[i];
        float raw = s->awake ? ma_voice_tick_detuned(&s->voice, s->detune_cents)
            * p->group_gain : 0;
        if (s->transition_remaining) {
            float old = (float)s->transition_remaining / (float)p->transition_frames;
            raw = s->transition_from * old + raw * (1 - old);
            s->transition_remaining--;
        }
        s->last_contribution = raw;
        left += raw * p->pan[i].left;
        right += raw * p->pan[i].right;
        if (s->awake && p->group[s->group].phase == MA_CARD_RELEASED
            && s->voice.amp_envelope.stage == MA_ENVELOPE_IDLE) {
            s->awake = false;
            p->diagnostics.active_slots--;
            p->diagnostics.sleeps++;
        }
        if (s->awake) live_group[s->group] = true;
    }
    for (unsigned g = 0; g < p->config.limit; g++)
        if (!live_group[g]) p->group[g] = (ma_pool_group){ 0 };
    float mid = (.5f * left + .5f * right) * .125f;
    float side = (.5f * left - .5f * right) * .125f
        * (1 - control->smoothers.crossfeed.current);
    if (width == 0 || control->smoothers.crossfeed.current == 1) {
        float dc = .5f * p->output.dc_lp_left + .5f * p->output.dc_lp_right;
        p->output.dc_lp_left = dc;
        p->output.dc_lp_right = dc;
    }
    return ma_output_render(&p->output, mid, side,
        control->smoothers.body_drive.current,
        control->smoothers.body_load_ratio.current, control->master_level);
}
void ma_voice_pool_render(ma_voice_pool *p, ma_frame *frames, size_t count) {
    for (size_t i = 0; i < count; i++) frames[i] = ma_voice_pool_tick(p);
}
