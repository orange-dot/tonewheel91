/* Mamut Analog circuit-derived oscillators, the part that runs once a frame,
 * written once for both lane widths: the VCO-2 pitch law for both cores, the
 * shapes that follow from each core's pitch (B1's rise, ramp bows and swing,
 * the saw's bow, the buffer's bend) and the two cores' coupling through their
 * supply (docs/ma-vco-plan.md, V1 to V4). ma_osc.c instantiates it for one
 * voice (ML_W 1), ma_lanes.c for eight pool voices per AVX2 vector (ML_W 8).
 * The oscillator kernel (ma_osc_kernel.h) takes what this hands over.
 * Include after ma_voice_impl.h and ma_lane_ops.h. */
#ifndef MA_VCO_KERNEL_H
#define MA_VCO_KERNEL_H

/* ---- the pitch law ---- */

/* One trimmed VCO-2 core: the frequency it gives for an ideal frequency `hz`,
 * `octaves` from A4 (the comment above VCO_INV_LN2 in ma_voice_impl.h). */
static inline ml_f ml_vco_law_hz(ml_f hz, ml_f octaves, ml_f extra_hz, ml_f dead_s,
                                 ml_f temp_k) {
    ml_f e_c2 = extra_hz * (1.0f / 65.40639f) - 65.40639f * dead_s;
    ml_f e_c6 = extra_hz * (1.0f / 1046.5023f) - 1046.5023f * dead_s;
    ml_f e_a4 = extra_hz * (1.0f / 440.0f) - 440.0f * dead_s;
    ml_f scale = -0.25f * VCO_INV_LN2 * (e_c6 - e_c2) + temp_k * VCO_TEMP_SCALE_PER_K;
    ml_f offset = -VCO_INV_LN2 * e_a4 + temp_k * VCO_TEMP_OCTAVES_PER_K;
    ml_f exponential = hz * ml_octave_ratio_small(offset + scale * octaves);
    return 1.0f / (1.0f / (exponential + extra_hz) + dead_s);
}

/* ---- the shapes at a pitch ---- */

/* e^y for y <= 0 (2^z as a power of two times (2^(f/4))^4), down to about
 * 2^-126 and zero below; the shapes' few uses need a few parts in a million. */
static inline ml_f ml_vco_exp_negative(ml_f y) {
    ml_f z = y * VCO_INV_LN2;
    ml_m tiny = z < -126.0f;
    z = ml_if(tiny, -126.0f, z);
    ml_int n = ml_trunc(z);
    n = ml_ifi(ml_int_to_f(n) > z, n - 1, n);
    ml_f q = ml_octave_ratio_small(0.25f * (z - ml_int_to_f(n)));
    q = q * q;
    q = q * q;
    return ml_keep(ml_not(tiny), q * ml_exp2_int(n));
}

/* 2^x for 0 <= x <= 4. */
static inline ml_f ml_vco_exp2(ml_f x) {
    ml_f q = ml_octave_ratio_small(x * (1.0f / 16.0f));
    for (int i = 0; i < 4; i++) q = q * q;
    return q;
}

/* B1's rise less 0.5 for a pitch `octaves` from A4 (V2). */
static inline ml_f ml_vco_rise(ml_f octaves) {
    ml_f x = ml_clamp(octaves, VCO_RISE_X_LOW, VCO_RISE_X_HIGH);
    ml_f p = ML(VCO_RISE_POLY[4]);
    for (int i = 3; i >= 0; i--) p = p * x + VCO_RISE_POLY[i];
    p = ml_if(octaves < VCO_RISE_X_LOW,
              p + VCO_RISE_SLOPE_LOW * (octaves - VCO_RISE_X_LOW), p);
    p = ml_if(octaves > VCO_RISE_X_HIGH,
              p + VCO_RISE_SLOPE_HIGH * (octaves - VCO_RISE_X_HIGH), p);
    ml_f a = ml_vco_exp_negative(p);
    return ml_if(a < VCO_RISE_MAX - 0.5f, a, VCO_RISE_MAX - 0.5f);
}

/* B1's ramp bow at a pitch (V2c): exp of a cubic in octaves from A4, run on
 * along its end slopes, capped. */
static inline ml_f ml_vco_ramp_bow(const float poly[4], float slope_low, float slope_high,
                                   ml_f octaves) {
    ml_f x = ml_clamp(octaves, VCO_RISE_X_LOW, VCO_RISE_X_HIGH);
    ml_f p = ((poly[3] * x + poly[2]) * x + poly[1]) * x + poly[0];
    p = ml_if(octaves < VCO_RISE_X_LOW, p + slope_low * (octaves - VCO_RISE_X_LOW), p);
    p = ml_if(octaves > VCO_RISE_X_HIGH, p + slope_high * (octaves - VCO_RISE_X_HIGH), p);
    ml_f b = ml_vco_exp_negative(p);
    return ml_if(b < VCO_BOW_MAX, b, VCO_BOW_MAX);
}

/* The swing the shapers see at a pitch, less A4's (V2, V3, V2b): `octaves`
 * from A4 below the join, the core's frequency above it. */
static inline ml_f ml_vco_swell(ml_f octaves, ml_f hz) {
    ml_f x = ml_clamp(octaves, VCO_SWELL_X_LOW, VCO_SWELL_X_HIGH);
    ml_f w = x * (VCO_SWELL_POLY[0] + x * (VCO_SWELL_POLY[1] + x * VCO_SWELL_POLY[2]));
    ml_f top = ml_if(hz < VCO_SWELL_TOP_HZ, hz, VCO_SWELL_TOP_HZ);
    return ml_if((octaves > VCO_SWELL_X_HIGH) & (hz > VCO_SWELL_JOIN_HZ),
                 w + VCO_SWELL_PER_HZ * (top - VCO_SWELL_JOIN_HZ), w);
}

/* ---- one frame ---- */

/* The patch's three controls, the frame's pitch before the law, the card's
 * two cores and the coupling row of VCO2's interval (vco_couple_for). A lane
 * with amount zero keeps its pitch bit for bit and gets no shape. */
typedef struct {
    ml_f amount, dead_time, supply;
    ml_f base_hz;                       /* VCO1's ideal frequency */
    ml_f note_position, pitch_delta, detune_cents;
    ml_f interval, interval_ratio;      /* VCO2's interval: semitones, and its ratio */
    ml_f fine_cents;
    ml_m card;                          /* a voice without one is the nominal board */
    ml_f temp_k, walk_cents, vco2_temp_k;
    ml_f extra_hz[2], dead_us[2];
    ml_f couple_p, couple_q, couple_jump, couple_good, couple_soft_octaves;
} ml_vco_in;

/* What the frame's source block reads (ml_source_in): VCO1's frequency,
 * VCO2's law over VCO1's less one, and the departures from the old
 * oscillator, all zero without the law. */
typedef struct {
    ml_f base_hz, vco2_bend;
    ml_f skew[2], swell[2];
    ml_f ramp_bow[2][2];                /* each triangle's rise and fall */
    ml_f saw_bow, buffer;
    ml_f couple_hz, couple_p, couple_q, couple_jump;
} ml_vco_frame;

/* Every circuit term scales with the amount, so a small amount is a small
 * departure. The card's temperatures are an assumption; its extra currents
 * and dead times are the two boards of the SPICE study. */
static inline ml_vco_frame ml_vco_frame_of(const ml_vco_in *x) {
    ml_m on = x->amount > 0.0f;
    ml_f amount = x->amount;
    ml_f dead = amount * x->dead_time * 1e-6f;
    ml_f temp = ml_keep(x->card,
                        amount * (x->temp_k + x->walk_cents * VCO_WALK_K_PER_CENT));
    ml_f octaves1 = (x->note_position - 69.0f + x->pitch_delta) * (1.0f / 12.0f)
                  + x->detune_cents * (1.0f / 1200.0f);
    ml_f hz1 = ml_vco_law_hz(x->base_hz, octaves1,
        amount * ml_if(x->card, x->extra_hz[0], VCO_NOMINAL_EXTRA_HZ),
        dead * ml_if(x->card, x->dead_us[0], VCO_NOMINAL_DEAD_US), temp);
    ml_f ratio = x->interval_ratio * ml_octave_ratio_small(x->fine_cents / 1200.0f);
    ml_f octaves2 = octaves1 + x->interval * (1.0f / 12.0f)
                  + x->fine_cents * (1.0f / 1200.0f);
    ml_f hz2 = ml_vco_law_hz(x->base_hz * ratio, octaves2,
        amount * ml_if(x->card, x->extra_hz[1], VCO_NOMINAL_EXTRA_HZ),
        dead * ml_if(x->card, x->dead_us[1], VCO_NOMINAL_DEAD_US),
        ml_keep(x->card, temp + amount * x->vco2_temp_k));

    /* The row's lock range between the two measured boards (geometric), on
     * the lower core's frequency. */
    ml_f rel = x->couple_good * ml_vco_exp2(x->supply * x->couple_soft_octaves);
    const ml_f octaves[2] = { octaves1, octaves2 }, hz[2] = { hz1, hz2 };
    ml_vco_frame f = {
        .base_hz = ml_if(on, hz1, x->base_hz),
        .vco2_bend = ml_keep(on, hz2 / (ratio * hz1) - 1.0f),
        .saw_bow = ml_keep(on, amount * VCO_SAW_BOW),
        .buffer = ml_keep(on, amount),
        .couple_hz = ml_keep(on, amount * rel * ml_if(hz1 < hz2, hz1, hz2)),
        .couple_p = x->couple_p,
        .couple_q = x->couple_q,
        .couple_jump = x->couple_jump,
    };
    for (int k = 0; k < 2; k++) {
        f.skew[k] = ml_keep(on, amount * ml_vco_rise(octaves[k]));
        f.swell[k] = ml_keep(on, amount * ml_vco_swell(octaves[k], hz[k]));
        f.ramp_bow[k][0] = ml_keep(on, amount * ml_vco_ramp_bow(
            VCO_BOW_UP_POLY, VCO_BOW_UP_SLOPE_LOW, VCO_BOW_UP_SLOPE_HIGH, octaves[k]));
        f.ramp_bow[k][1] = ml_keep(on, -amount * ml_vco_ramp_bow(
            VCO_BOW_DOWN_POLY, VCO_BOW_DOWN_SLOPE_LOW, VCO_BOW_DOWN_SLOPE_HIGH, octaves[k]));
    }
    return f;
}

#endif
