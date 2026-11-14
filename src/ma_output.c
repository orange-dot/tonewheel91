/* Shared chassis, DC block and safety; freestanding C23. */
#include "ma_internal.h"

static float output_dc_tick(ma_output_state *output, float input, float *lp) {
    if (!(input >= -FLT_MAX && input <= FLT_MAX)) return input;
    float next = *lp + output->dc_coefficient * (input - *lp);
    if (next != 0.0f && tw_fabsf(next) < 1.0e-9f) {
        next = 0.0f;
        output->diagnostics.tiny_flush_count++;
    }
    *lp = next;
    return input - next;
}

static float output_safety_tick(ma_output_state *output, float input) {
    if (!(input >= -FLT_MAX && input <= FLT_MAX)) {
        output->diagnostics.sanitization_count++;
        return 0.0f;
    }
    float magnitude = tw_fabsf(input);
    if (magnitude > output->diagnostics.pre_peak)
        output->diagnostics.pre_peak = magnitude;
    if (magnitude > .98f) output->diagnostics.knee_hit_count++;
    float sample = ma_safety_curve(input);
    float limited = tw_fabsf(sample);
    if (limited > output->diagnostics.post_peak)
        output->diagnostics.post_peak = limited;
    float reduction = magnitude > limited ? magnitude - limited : 0.0f;
    if (reduction > output->diagnostics.maximum_reduction)
        output->diagnostics.maximum_reduction = reduction;
    return sample;
}

/* Body and DC block. A non-finite mid or side skips both (and the rack):
 * *finite is false and the frame is the raw mid +- side for the safety. */
ma_frame ma_output_front(ma_output_state *output, float mid, float side,
                         float drive, float load, bool *finite) {
    output->pre_body = mid;
    if (!(mid >= -FLT_MAX && mid <= FLT_MAX)
        || !(side >= -FLT_MAX && side <= FLT_MAX)) {
        output->post_body = mid;
        *finite = false;
        return (ma_frame){ mid + side, mid - side };
    }
    *finite = true;
    float body = mid;
    if (drive > 0.0f) {
        tw_drive_set(&output->body, drive);
        body = .25f * tw_drive_tick(&output->body, 4.0f * load * mid);
    }
    output->post_body = body;
    float left = side == 0.0f ? body : body + side;
    float right = side == 0.0f ? body : body - side;
    left = output_dc_tick(output, left, &output->dc_lp_left);
    right = output_dc_tick(output, right, &output->dc_lp_right);
    return (ma_frame){ left, right };
}

/* Master and safety; a non-finite frame goes to the safety unscaled. */
ma_frame ma_output_back(ma_output_state *output, ma_frame x, float master, bool finite) {
    if (finite) {
        x.left *= master;
        x.right *= master;
    }
    return (ma_frame){
        .left = output_safety_tick(output, x.left),
        .right = output_safety_tick(output, x.right),
    };
}

ma_frame ma_output_render(ma_output_state *output, ma_rack *rack, float mid,
                           float side, float drive, float load, float master) {
    bool finite;
    ma_frame x = ma_output_front(output, mid, side, drive, load, &finite);
    /* RKD9: the rack sees the level before the master fader. */
    if (finite && rack) x = ma_rack_tick(rack, x);
    return ma_output_back(output, x, master, finite);
}
