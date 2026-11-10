/* White-box gate for the hard-sync step and the triangle integrator
 * (2026-11-10, docs/ma-source-fix-20261110.md, ma-constants.md 3.1 and 3.2).
 *
 * Sync: a reset moves a saw slave down, from its phase at the event to a
 * lower one. After the event the 8x output must therefore never rise above
 * both its value before the event and the raw saw at the current phase. The
 * former code let the ordinary post-wrap correction act on a reset it had
 * not caused, which lifted the output by up to 1 for eight substeps.
 *
 * A weak sync that pulls the slave back just after its own wrap must leave
 * that wrap's smoothing alone: no substep may move further than the ordinary
 * smoothed edge does.
 *
 * Triangle: the integrator must span -1..1 with no offset at any pitch and
 * from any start phase. Uncompensated, its top fell by 20 phase steps. */
#include "../src/ma_voice_impl.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static constexpr float RATE = 48000.0f;

static ma_synth voice(void) {
    ma_synth s = { .sample_rate_hz = RATE };
    s.oscillator1.guard_gain = s.oscillator1.guard_target = 1.0f;
    s.oscillator2.guard_gain = s.oscillator2.guard_target = 1.0f;
    return s;
}

/* The largest move between two substeps, after the slave has locked. */
static float largest_move(float master_hz, float ratio, float sync) {
    ma_synth s = voice();
    s.oscillator1.triangle_initialized = s.oscillator2.triangle_initialized = true;
    ma_render_controls c = { .vco2_level = 1.0f, .sync_amount = sync };
    c.vco1.pulse_width = c.vco2.pulse_width = 0.5f;
    c.vco2.saw_level = 1.0f;
    ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, master_hz,
                                                              0.0f, 0.0f, 1.0f);
    f.vco2_hz = master_hz * ratio;
    f.vco2_step_q48 = phase_step_q48(phase_step(f.vco2_hz, f.rate_hz));
    f.vco2_step = phase_turns(f.vco2_step_q48);
    float previous = 0.0f, largest = 0.0f;
    for (int i = 0; i < 60000; i++) {
        float y = ma_voice_render_source_substep(&s, &c, &f, master_hz, 0.0f,
                                                 0.0f, 1.0f);
        if (i > 20000 && fabsf(y - previous) > largest)
            largest = fabsf(y - previous);
        previous = y;
    }
    return largest;
}

static unsigned tracked;

static unsigned sync_events(float master_hz, float ratio, float sync) {
    ma_synth s = voice();
    s.oscillator1.triangle_initialized = s.oscillator2.triangle_initialized = true;
    ma_render_controls c = { .vco2_level = 1.0f, .sync_amount = sync };
    c.vco1.pulse_width = c.vco2.pulse_width = 0.5f;
    c.vco2.saw_level = 1.0f;
    ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, master_hz,
                                                              0.0f, 0.0f, 1.0f);
    f.vco2_hz = master_hz * ratio;
    f.vco2_step_q48 = phase_step_q48(phase_step(f.vco2_hz, f.rate_hz));
    f.vco2_step = phase_turns(f.vco2_step_q48);
    unsigned events = 0, since_event = 99;
    float before = 0.0f, previous = 0.0f;
    for (int i = 0; i < 40000; i++) {
        uint64_t master = s.oscillator1.phase_q48;
        float raw = 2.0f * phase_turns(s.oscillator2.phase_q48) - 1.0f;
        float y = ma_voice_render_source_substep(&s, &c, &f, master_hz, 0.0f,
                                                 0.0f, 1.0f);
        assert(isfinite(y) && fabsf(y) <= 1.25f);
        if (since_event < 8) {
            float ceiling = before > raw ? before : raw;
            assert(y <= ceiling + 1.0e-3f);
            since_event++;
        }
        if (s.oscillator1.phase_q48 < master) {     /* the master wrapped */
            tracked += s.oscillator2.sync_run_q48 != 0;
            /* the event's own sample already carries part of the step */
            before = y > previous ? y : previous;
            since_event = 0;
            events++;
        }
        previous = y;
    }
    return events;
}

static void triangle(float hz, float start_phase, bool seeded) {
    ma_synth s = voice();
    s.oscillator1.phase_q48 = (uint64_t)((double)start_phase * 0x1p48);
    s.oscillator1.triangle = -1.0f;
    s.oscillator1.triangle_initialized = !seeded;
    s.oscillator2.triangle_initialized = true;
    ma_render_controls c = { 0 };
    c.vco1.pulse_width = c.vco2.pulse_width = 0.5f;
    c.vco1.triangle_level = 1.0f;
    ma_source_frame f = ma_voice_prepare_source_frame_guarded(&s, &c, hz, 0.0f,
                                                              1.0f, 0.0f);
    ma_voice_seed_triangles(&s, &f);
    double sum = 0.0;
    float low = 2.0f, high = -2.0f;
    int n = 8 * 48000;
    for (int i = 0; i < n; i++) {
        float y = ma_voice_render_source_substep(&s, &c, &f, hz, 0.0f, 1.0f, 0.0f);
        sum += y;
        if (y < low) low = y;
        if (y > high) high = y;
    }
    assert(s.oscillator2.sync_run_q48 == 0);        /* no sync, no run */
    assert(high <= 1.0f && high >= 0.99f);
    assert(low >= -1.0f && low <= -0.99f);
    assert(fabs(sum / n) < 0.004);
}

int main(void) {
    unsigned events = 0, cases = 0;
    static const float masters[] = { 110.0f, 440.0f, 600.0f, 1760.0f };
    static const float amounts[] = { 1.0f, 0.5f, 0.22f };
    for (unsigned m = 0; m < 4; m++)
        for (unsigned a = 0; a < 3; a++)
            for (float ratio = 1.03f; ratio < 5.0f; ratio += 0.0917f) {
                events += sync_events(masters[m], ratio, amounts[a]);
                cases++;
            }
    assert(events > 30000);
    /* Only a reset that touches an edge zone is tracked. */
    assert(tracked > 1000 && tracked < events);
    /* The ordinary edge moves at most 2 S'(1/2) / 16 = 0.234 per substep, plus
     * the ramp. Unsynced and weakly synced slaves must both stay under it. */
    float free_move = 0.0f, pulled_move = 0.0f;
    static const float trebles[] = { 880.0f, 1760.0f, 3520.0f };
    for (unsigned k = 0; k < 3; k++) {
        float a = largest_move(trebles[k], 1.004f, 0.0f);
        float b = largest_move(trebles[k], 1.004f, 0.25f);
        if (a > free_move) free_move = a;
        if (b > pulled_move) pulled_move = b;
    }
    assert(free_move < 0.30f && pulled_move < 0.30f);
    static const float pitches[] = { 55.0f, 440.0f, 2093.0f, 3520.0f };
    static const float phases[] = { 0.0f, 0.13f, 0.40f, 0.77f };
    for (unsigned p = 0; p < 4; p++) {
        triangle(pitches[p], 0.0f, false);
        for (unsigned k = 0; k < 4; k++) triangle(pitches[p], phases[k], true);
    }
    printf("ma_sync: %u sync events (%u tracked) in %u cases stay under the pre-event "
           "and raw values; largest substep move %.3f free, %.3f weakly synced; triangle "
           "spans -1..1 without offset at 4 pitches, 5 starts\n",
           events, tracked, cases, free_move, pulled_move);
}
