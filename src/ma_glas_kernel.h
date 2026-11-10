/* Mamut Analog Glas DSP, written once for both lane widths: one step of a
 * formant grain and the formant bank (ma-constants.md 10C). The Orbita tick,
 * the grain pulse and grain launches stay per voice in ma_glas.c. ma_glas.c
 * instantiates it for one voice (ML_W 1), ma_lanes.c for eight pool voices
 * per AVX2 vector (ML_W 8). Include after ma_voice_impl.h and
 * ma_lane_ops.h. */
#ifndef MA_GLAS_KERNEL_H
#define MA_GLAS_KERNEL_H

/* ma_grain, lane by lane */
typedef struct {
    ml_f phase, step, envelope, decay, attack_pos, attack_step, gain;
    ml_m active;
} ml_grain;

/* One 2x sample of a grain, added to y, for the voices in `on`. The attack
 * is a raised cosine, .5 (1 - cos(pi a)) = .5 (1 - sin(2 pi (.25 - a/2)));
 * a grain ends when its envelope falls under GRAIN_FLOOR. */
static inline ml_f ml_grain_step(ml_grain *q, ml_m on, ml_f y) {
    ml_m live = q->active & on;
    if (!ml_any(live)) return y;
    ml_m in_attack = q->attack_pos < 1.0f;
    ml_f attack = ml_if(in_attack,
                        0.5f * (1.0f - ml_sin_turns(ml_wrap(0.25f - 0.5f * q->attack_pos))),
                        1.0f);
    y = ml_if(live, ml_fmadd(q->gain * attack * q->envelope, ml_sin_turns(q->phase), y), y);
    ml_f phase = q->phase + q->step;
    phase = ml_if(phase >= 1.0f, phase - 1.0f, phase);
    ml_f envelope = q->envelope * q->decay;
    q->attack_pos = ml_if(live & in_attack, q->attack_pos + q->attack_step, q->attack_pos);
    q->phase = ml_if(live, phase, q->phase);
    q->envelope = ml_if(live, envelope, q->envelope);
    q->active = q->active & ml_not(live & (envelope < GRAIN_FLOOR));
    return y;
}

/* The formant bank on the ladder's 1x output for the voices in `on` with a
 * positive mix: four bandpass SVFs, mixed in by `mix`. A voice whose bank
 * leaves the finite range keeps its input and is reported in *bad for the
 * caller's reset. */
static inline ml_f ml_formant_bank(ml_f s1[MA_FORMANTS], ml_f s2[MA_FORMANTS],
                                   const ml_f g[MA_FORMANTS], const ml_f k[MA_FORMANTS],
                                   ml_f x, ml_f mix, ml_m on, ml_m *bad) {
    ml_m live = on & (mix > 0.0f);
    *bad = ML_FALSE;
    if (!ml_any(live)) return x;
    ml_f bank = ML(0.0f);
    for (int i = 0; i < MA_FORMANTS; i++) {
        ml_f hp = (ml_fnmadd(k[i] + g[i], s1[i], x) - s2[i]) / ml_fmadd(g[i], g[i] + k[i], 1.0f);
        ml_f bp = ml_fmadd(g[i], hp, s1[i]);
        ml_f lp = ml_fmadd(g[i], bp, s2[i]);
        s1[i] = ml_if(live, ml_fmadd(g[i], hp, bp), s1[i]);
        s2[i] = ml_if(live, ml_fmadd(g[i], bp, lp), s2[i]);
        bank = ml_fmadd(GLAS_GAIN[i] * k[i], bp, bank);
    }
    bank = bank * FORMANT_MAKEUP;
    ml_m finite = (bank >= -FLT_MAX) & (bank <= FLT_MAX);
    *bad = live & ml_not(finite);
    return ml_if(live & finite, x + mix * (bank - x), x);
}

#endif
