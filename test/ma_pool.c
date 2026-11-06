#include "../src/mamutanalog.h"
#include <assert.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static ma_voice_pool a, b;
static void init(unsigned limit, unsigned unison, float rate) {
    ma_voice_pool_config c = ma_voice_pool_default_config;
    c.limit = (uint8_t)limit;
    c.unison_count = (uint8_t)unison;
    assert(ma_voice_pool_init(&a, rate, &c, &ma_patch_tepih));
}
static void tick(unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        ma_frame f = ma_voice_pool_tick(&a);
        assert(isfinite(f.left) && isfinite(f.right));
        assert(fabsf(f.left) <= 1 && fabsf(f.right) <= 1);
        assert(a.diagnostics.active_slots <= a.config.limit);
    }
}
static void invariant(void) {
    unsigned active = 0;
    for (unsigned i = 0; i < a.config.limit; i++) {
        if (!a.slot[i].awake) continue;
        active++;
        ma_pool_group g = a.group[a.slot[i].group];
        assert(g.phase != MA_CARD_IDLE);
        assert(g.allocation_id == a.slot[i].allocation_id);
        assert(g.note == a.slot[i].voice.note);
        assert(g.channel == a.slot[i].voice.channel);
    }
    assert(active == a.diagnostics.active_slots);
    for (unsigned g = 0; g < a.config.limit; g++) {
        if (a.group[g].phase != MA_CARD_HELD && a.group[g].phase != MA_CARD_SUSTAINED) continue;
        unsigned members = 0;
        for (unsigned i = 0; i < a.config.limit; i++)
            members += a.slot[i].awake && a.slot[i].group == g;
        assert(members == a.config.unison_count);
    }
}
static void allocation(void) {
    for (unsigned u = 1; u <= 8; u *= 2) {
        init(64, u, 48000);
        for (unsigned i = 0; i < 64 / u; i++) {
            assert(ma_voice_pool_note_on(&a, (uint8_t)(i % 16), (uint8_t)i, 100) == i + 1);
            invariant();
        }
        assert(a.diagnostics.active_slots == 64);
        tick(64);
        assert(ma_voice_pool_note_on(&a, 0, 100, 100));
        assert(a.diagnostics.steals_held == 1);
        invariant();
        for (unsigned i = 0; i < u; i++) {
            float expected = u == 1 ? 0 : 7.0f * (float)(2 * (int)i - (int)u + 1) / (float)(u - 1);
            assert(a.slot[i].detune_cents == expected);
        }
        ma_voice_pool_panic(&a);
        assert(!ma_voice_pool_note_off(&a, 0, 100, 0));
    }
    for (unsigned limit = 1; limit <= 64; limit++) {
        init(limit, 1, 48000);
        for (unsigned i = 0; i < limit + 2; i++) assert(ma_voice_pool_note_on(&a, 0, 60, 100));
        assert(a.diagnostics.active_slots == limit);
        assert(a.diagnostics.steals_held == 2);
    }
    init(4, 1, 48000);
    uint64_t x = ma_voice_pool_note_on(&a, 2, 60, 100);
    uint64_t y = ma_voice_pool_note_on(&a, 2, 60, 100);
    assert(ma_voice_pool_note_on(&a, 3, 60, 100));
    ma_voice_pool_set_sustain(&a, 2, true);
    assert(ma_voice_pool_note_off(&a, 2, 60, 42) == x);
    assert(ma_voice_pool_note_off(&a, 2, 60, 0) == y);
    assert(!ma_voice_pool_note_off(&a, 2, 60, 0));
    assert(a.group[2].phase == MA_CARD_HELD);
    ma_voice_pool_set_sustain(&a, 2, false);
    assert(a.group[0].phase == MA_CARD_RELEASED);
    assert(ma_voice_pool_note_on(&a, 0, 80, 100));
    a.slot[0].voice.amp_envelope.level = .7f;
    a.slot[1].voice.amp_envelope.level = .1f;
    assert(ma_voice_pool_note_on(&a, 0, 81, 100));
    assert(a.slot[1].voice.note == 81 && a.diagnostics.steals_released == 1);
    assert(ma_voice_pool_note_on(&a, 0, 82, 100));
    ma_voice_pool_set_sustain(&a, 0, true);
    assert(ma_voice_pool_note_off(&a, 0, 81, 0));
    assert(ma_voice_pool_note_on(&a, 0, 83, 100));
    assert(a.diagnostics.steals_sustained == 1);
    a.next_allocation = 0;
    assert(!ma_voice_pool_note_on(&a, 0, 60, 100));
    invariant();
}
static void group_priority(void) {
    init(4, 2, 48000);
    assert(ma_voice_pool_note_on(&a, 0, 60, 100));
    assert(ma_voice_pool_note_on(&a, 0, 64, 100));
    assert(ma_voice_pool_note_off(&a, 0, 60, 0));
    assert(ma_voice_pool_note_off(&a, 0, 64, 0));
    assert(ma_voice_pool_note_on(&a, 0, 67, 100));
    assert(a.slot[0].voice.note == 67); /* equal released levels: oldest */
    init(8, 2, 48000);
    for (unsigned i = 0; i < 4; i++)
        assert(ma_voice_pool_note_on(&a, 0, (uint8_t)(60+i), 100));
    assert(ma_voice_pool_note_off(&a, 0, 60, 0));
    assert(ma_voice_pool_note_off(&a, 0, 61, 0));
    a.slot[0].voice.amp_envelope.level = .01f;
    a.slot[1].voice.amp_envelope.level = .8f;
    a.slot[2].voice.amp_envelope.level = .3f;
    a.slot[3].voice.amp_envelope.level = .3f;
    assert(ma_voice_pool_note_on(&a, 0, 80, 100));
    assert(a.slot[2].voice.note == 80 && a.slot[3].voice.note == 80);
    assert(a.slot[0].voice.note == 60); /* group maximum, not quietest member */
    assert(a.diagnostics.steals_released == 1);
    invariant();
    ma_voice_pool_set_sustain(&a, 0, true);
    assert(ma_voice_pool_note_off(&a, 0, 62, 0));
    assert(ma_voice_pool_note_off(&a, 0, 63, 0));
    a.slot[4].voice.amp_envelope.level = .4f;
    a.slot[5].voice.amp_envelope.level = .4f;
    a.slot[6].voice.amp_envelope.level = .4f;
    a.slot[7].voice.amp_envelope.level = .4f;
    assert(ma_voice_pool_note_on(&a, 0, 81, 100)); /* released beats sustained */
    assert(a.diagnostics.steals_released == 2);
    assert(ma_voice_pool_note_on(&a, 0, 82, 100)); /* equal sustained: oldest */
    assert(a.slot[4].voice.note == 82 && a.slot[5].voice.note == 82);
    assert(a.diagnostics.steals_sustained == 1);
    invariant();
    /* One member finishes its release first. Reclaim another whole group;
     * the new unison may span the free slot and one stolen slot. */
    init(4, 2, 48000);
    assert(ma_voice_pool_note_on(&a, 0, 60, 100));
    assert(ma_voice_pool_note_on(&a, 0, 64, 100));
    tick(100);
    assert(ma_voice_pool_note_off(&a, 0, 60, 0));
    a.slot[0].voice.amp_envelope = (ma_envelope){ .stage = MA_ENVELOPE_IDLE };
    tick(1);
    assert(a.diagnostics.active_slots == 3);
    assert(ma_voice_pool_note_on(&a, 0, 67, 100));
    assert(a.diagnostics.active_slots == 4 && a.diagnostics.steals_released == 1);
    invariant();
}

static void event_stress(void) {
    for (unsigned u = 1; u <= 8; u *= 2) {
        init(16, u, 48000);
        ma_patch patch = ma_patch_lead;
        patch.amp_adsr.release_ms = 1;
        ma_voice_pool_set_patch(&a, &patch);
        uint64_t seed = 8128;
        for (unsigned event = 0; event < 400; event++) {
            uint64_t x = tw_splitmix64(&seed);
            uint8_t ch = (uint8_t)(x % 4), note = (uint8_t)(48 + (x >> 8) % 12);
            switch ((x >> 16) % 6) {
            case 0: ma_voice_pool_set_sustain(&a, ch, (x >> 24) & 1); break;
            case 1: (void)ma_voice_pool_note_off(&a, ch, note, 127); break;
            case 2: ma_voice_pool_set_poly_pressure(&a, ch, note, .8f); break;
            default: assert(ma_voice_pool_note_on(&a, ch, note, 100)); break;
            }
            tick(16);
            invariant();
        }
        ma_voice_pool_panic(&a);
        tick(10000);
        assert(a.diagnostics.active_slots == 0);
        for (unsigned g = 0; g < a.config.limit; g++) assert(a.group[g].phase == MA_CARD_IDLE);
    }
}

static void config(void) {
    init(32, 1, 48000);
    b = a;
    const ma_voice_pool_config bad[] = {
        {0, 1, 7, 1, MA_POOL_ENGINE_AUTO}, {65, 1, 7, 1, MA_POOL_ENGINE_AUTO},
        {32, 3, 7, 1, MA_POOL_ENGINE_AUTO}, {31, 2, 7, 1, MA_POOL_ENGINE_AUTO},
        {32, 0, 7, 1, MA_POOL_ENGINE_AUTO}, {32, 1, NAN, 1, MA_POOL_ENGINE_AUTO},
        {32, 1, INFINITY, 1, MA_POOL_ENGINE_AUTO}, {32, 1, -1, 1, MA_POOL_ENGINE_AUTO},
        {32, 1, 101, 1, MA_POOL_ENGINE_AUTO}, {32, 1, 7, 1, (ma_pool_engine)2},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        assert(!ma_voice_pool_init(&a, 48000, &bad[i], NULL));
        assert(memcmp(&a, &b, sizeof a) == 0);
    }
    assert(!ma_voice_pool_note_on(&a, 16, 60, 100));
    assert(!ma_voice_pool_note_on(&a, 0, 128, 100));
    assert(!ma_voice_pool_note_on(&a, 0, 60, 0));
}
static void sleep_wake(void) {
    init(1, 1, 48000);
    ma_patch p = ma_patch_lead;
    p.amp_adsr = (ma_adsr){1, 1, 0, 1};
    ma_voice_pool_set_patch(&a, &p);
    assert(ma_voice_pool_note_on(&a, 4, 60, 100));
    tick(4096);
    assert(a.slot[0].awake); /* held zero sustain must still run */
    uint64_t phase = a.slot[0].voice.oscillator1.phase_q48;
    ma_voice_pool_set_sustain(&a, 4, true);
    assert(ma_voice_pool_note_off(&a, 4, 60, 0));
    tick(64);
    assert(a.slot[0].awake && a.slot[0].voice.oscillator1.phase_q48 != phase);
    ma_voice_pool_set_sustain(&a, 4, false);
    tick(4096);
    assert(!a.slot[0].awake && a.diagnostics.sleeps == 1);
    ma_synth frozen = a.slot[0].voice;
    ma_voice_pool_set_macro(&a, MA_MACRO_HEAT, .9f);
    ma_voice_pool_set_output(&a, .8f, .8f, .5f, .7f);
    ma_voice_pool_set_pitch_bend(&a, 4, 1.5f);
    ma_voice_pool_set_channel_pressure(&a, 4, .7f);
    ma_voice_pool_set_mod_wheel(&a, 4, .6f);
    tick(4096);
    assert(memcmp(&frozen, &a.slot[0].voice, sizeof frozen) == 0);
    assert(a.control.smoothers.body_drive.remaining == 0);
    ma_character identity = a.slot[0].voice.character;
    b = a;
    assert(ma_voice_pool_note_on(&a, 4, 64, 100));
    assert(ma_voice_pool_note_on(&b, 4, 64, 100));
    assert(a.slot[0].voice.oscillator1.phase_q48 == b.slot[0].voice.oscillator1.phase_q48);
    assert(memcmp(&identity, &a.slot[0].voice.character, sizeof identity) == 0);
    assert(a.slot[0].voice.pitch_bend_semitones == 1.5f);
    assert(a.slot[0].voice.channel_pressure == .7f && a.slot[0].voice.mod_wheel == .6f);
    assert(a.slot[0].voice.macro[MA_MACRO_HEAT] == .9f);
    assert(a.slot[0].voice.ladder.y4_guess == 0);
    ma_voice_pool_set_poly_pressure(&a, 3, 64, 1);
    assert(a.slot[0].voice.poly_pressure == 0);
    ma_voice_pool_set_poly_pressure(&a, 4, 64, .8f);
    assert(a.slot[0].voice.poly_pressure == .8f);
    tick(100);
    float last = a.slot[0].last_contribution;
    assert(ma_voice_pool_note_on(&a, 4, 67, 100));
    assert(a.slot[0].transition_remaining == 240);
    tick(1);
    assert(a.slot[0].last_contribution == last);
    tick(239);
    assert(a.slot[0].transition_remaining == 0);
    ma_voice_pool_panic(&a);
    tick(4096);
    ma_voice_pool_reset(&a);
    assert(a.diagnostics.active_slots == 0 && a.next_allocation == 1);
    assert(a.output.dc_lp_left == 0);
    assert(ma_voice_pool_note_on(&a, 4, 64, 100));
    uint64_t reset_phase = a.slot[0].voice.oscillator1.phase_q48;
    ma_voice_pool_reset(&a);
    assert(ma_voice_pool_note_on(&a, 4, 64, 100));
    assert(a.slot[0].voice.oscillator1.phase_q48 == reset_phase);
    ma_voice_pool_config other = a.config;
    other.session_seed++;
    assert(ma_voice_pool_init(&b, 48000, &other, NULL));
    assert(ma_voice_pool_note_on(&b, 4, 64, 100));
    assert(b.slot[0].voice.oscillator1.phase_q48 != reset_phase);
    assert(b.slot[0].voice.character.vco1_cents != a.slot[0].voice.character.vco1_cents);
    tick(128);
    ma_envelope envelope = a.slot[0].voice.amp_envelope;
    uint64_t id = a.slot[0].allocation_id;
    ma_output_state output = a.output;
    ma_voice_pool_set_patch(&a, &ma_patch_raster);
    assert(a.slot[0].allocation_id == id && a.slot[0].awake);
    assert(memcmp(&envelope, &a.slot[0].voice.amp_envelope, sizeof envelope) == 0);
    assert(memcmp(&output, &a.output, sizeof output) == 0);
    assert(a.slot[0].voice.raster_mix == ma_patch_raster.raster_mix);
}
static uint64_t deterministic(unsigned unison) {
    init(8, unison, 48000);
    ma_voice_pool_set_patch(&a, &ma_patch_granica);
    b = a;
    uint64_t hash = 0;
    for (unsigned part = 0; part < 8; part++) {
        assert(ma_voice_pool_note_on(&a, 0, (uint8_t)(48 + part), 100));
        assert(ma_voice_pool_note_on(&b, 0, (uint8_t)(48 + part), 100));
        if (part == 3) { ma_voice_pool_panic(&a); ma_voice_pool_panic(&b); }
        ma_frame frames[333];
        ma_voice_pool_render(&b, frames, 17);
        ma_voice_pool_render(&b, frames + 17, 316);
        for (unsigned j = 0; j < 333; j++) {
            ma_frame f = ma_voice_pool_tick(&a);
            assert(f.left == frames[j].left && f.right == frames[j].right);
            hash = tw_fnv1a64(&f, sizeof f, hash);
        }
        invariant();
    }
    ma_voice_pool_set_output(&a, .5f, 0, 0, 1);
    for (unsigned i = 0; i < 400; i++) {
        ma_frame f = ma_voice_pool_tick(&a);
        assert(f.left == f.right);
    }
    return hash;
}
static void stereo_gain(void) {
    init(8, 1, 48000);
    ma_voice_pool_set_output(&a, 0, 1, 0, 1);
    tick(1000);
    for (unsigned i = 0; i < 8; i++)
        assert(a.pan[i].position == -a.pan[7-i].position);
    ma_voice_pool_set_output(&a, 0, 0, 0, 1);
    assert(ma_voice_pool_note_on(&a, 0, 60, 100));
    tick(1000);
    b = a;
    assert(ma_voice_pool_note_on(&a, 0, 72, 100));
    /* Compare the remaining slot's raw contribution, before shared nonlinearities. */
    tick(100);
    for (unsigned i = 0; i < 100; i++) (void)ma_voice_pool_tick(&b);
    assert(a.slot[0].last_contribution == b.slot[0].last_contribution);
}
static void hostile(void) {
    const float rates[] = {44100, 48000, 96000, 192000};
    for (unsigned r = 0; r < 4; r++) {
        init(8, 2, rates[r]);
        ma_patch p = ma_patch_granica;
        p.filter_cutoff_hz = NAN; p.filter_resonance = INFINITY;
        p.master_level = 1; p.body_drive = 1;
        p.amp_adsr.release_ms = 1;
        ma_voice_pool_set_patch(&a, &p);
        ma_voice_pool_set_macro(&a, MA_MACRO_RUIN, NAN);
        ma_voice_pool_set_pitch_bend(&a, 0, INFINITY);
        ma_voice_pool_set_channel_pressure(&a, 0, NAN);
        ma_voice_pool_set_mod_wheel(&a, 0, -100);
        for (unsigned note = 0; note < 128; note++) {
            assert(ma_voice_pool_note_on(&a, (uint8_t)(note % 16), (uint8_t)note, 255));
            tick(32);
            invariant();
        }
        ma_voice_pool_panic(&a);
        tick((unsigned)rates[r] / 10);
        assert(a.diagnostics.active_slots == 0);
    }
}
/* The vector (AUTO) and scalar engines must agree bit-for-bit: every rendered
 * frame, every slot's full ma_synth state after each render call, and the
 * diagnostics. Short envelopes put sleeps inside render blocks; limit 12 leaves
 * a partial bank. */
static ma_voice_pool scalar_pool;
static void engine_twin(void) {
    static const unsigned limits[] = { 8, 12, 32 };
    static const ma_patch *const patches[] = {
        &ma_patch_tepih, &ma_patch_lead, &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica };
    static ma_frame lane_frames[257], scalar_frames[257];
    uint64_t sleeps = 0, frames_total = 0;
    for (unsigned li = 0; li < 3; li++)
        for (unsigned unison = 1; unison <= 4; unison *= 2) {
            ma_voice_pool_config c = ma_voice_pool_default_config;
            c.limit = (uint8_t)limits[li];
            c.unison_count = (uint8_t)unison;
            ma_patch patch = *patches[(li * 3 + unison) % 5];
            patch.amp_adsr = (ma_adsr){ 0.004f, 0.03f, 0.6f, 0.012f };
            c.engine = MA_POOL_ENGINE_AUTO;
            assert(ma_voice_pool_init(&a, 48000, &c, &patch));
            c.engine = MA_POOL_ENGINE_SCALAR;
            assert(ma_voice_pool_init(&scalar_pool, 48000, &c, &patch));
            assert(!scalar_pool.lanes_enabled);
            uint32_t seed = UINT32_C(0x6c616e65) ^ li ^ (unison << 8);
            uint8_t held[16] = { 0 };
            for (unsigned step = 0; step < 64; step++) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                uint8_t channel = (uint8_t)(seed >> 28 & 1);
                if (seed % 3) {
                    uint8_t note = (uint8_t)(36 + seed % 48);
                    held[step % 16] = note;
                    (void)ma_voice_pool_note_on(&a, channel, note, (uint8_t)(1 + (seed >> 9) % 127));
                    (void)ma_voice_pool_note_on(&scalar_pool, channel, note, (uint8_t)(1 + (seed >> 9) % 127));
                } else {
                    uint8_t note = held[(seed >> 4) % 16];
                    (void)ma_voice_pool_note_off(&a, channel, note, 0);
                    (void)ma_voice_pool_note_off(&scalar_pool, channel, note, 0);
                }
                if (step % 17 == 5) {
                    ma_voice_pool_set_sustain(&a, channel, seed & 2);
                    ma_voice_pool_set_sustain(&scalar_pool, channel, seed & 2);
                }
                unsigned n = 1 + (seed >> 12) % 257;
                ma_voice_pool_render(&a, lane_frames, n);
                ma_voice_pool_render(&scalar_pool, scalar_frames, n);
                frames_total += n;
                assert(memcmp(lane_frames, scalar_frames, n * sizeof *lane_frames) == 0);
                assert(memcmp(a.slot, scalar_pool.slot, sizeof a.slot) == 0);
                assert(memcmp(&a.diagnostics, &scalar_pool.diagnostics, sizeof a.diagnostics) == 0);
            }
            sleeps += a.diagnostics.sleeps;
        }
    assert(sleeps > 0);
    printf("ma_pool: vector and scalar engines bit-identical over %" PRIu64 " frames, %" PRIu64
           " sleeps (vector lanes %s)\n", frames_total, sleeps,
           a.lanes_enabled ? "active" : "unavailable on this CPU");
}
int main(void) {
    config(); allocation(); group_priority(); event_stress(); sleep_wake(); stereo_gain(); hostile(); engine_twin();
    const uint64_t expected[] = {
        UINT64_C(0x330b2b6d9f8f30a5), UINT64_C(0x71d5cec193f3ee93),
        UINT64_C(0xac05ed8c196f0d3b), UINT64_C(0x8bc31ab3650c18f1),
    };
    unsigned index = 0;
    for (unsigned u = 1; u <= 8; u *= 2) {
        uint64_t first = deterministic(u), second = deterministic(u);
        assert(first == expected[index++]);
        assert(first == second);
        printf("pool-unison%u hash=%016" PRIx64 "\n", u, first);
    }
    puts("ma_pool: allocation, sleep/wake, expression, stereo, block and safety pass");
}
