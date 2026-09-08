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

ma_frame ma_output_render(ma_output_state *output, float mid, float side,
                           float drive, float load, float master) {
    output->pre_body = mid;
    if (!(mid >= -FLT_MAX && mid <= FLT_MAX)
        || !(side >= -FLT_MAX && side <= FLT_MAX)) {
        output->post_body = mid;
        return (ma_frame){
            .left = output_safety_tick(output, mid + side),
            .right = output_safety_tick(output, mid - side),
        };
    }
    float body = mid;
    if (drive > 0.0f) {
        tw_drive_set(&output->body, drive);
        body = .25f * tw_drive_tick(&output->body, 4.0f * load * mid);
    }
    output->post_body = body;
    float left = side == 0.0f ? body : body + side;
    float right = side == 0.0f ? body : body - side;
    left = output_dc_tick(output, left, &output->dc_lp_left) * master;
    right = output_dc_tick(output, right, &output->dc_lp_right) * master;
    return (ma_frame){
        .left = output_safety_tick(output, left),
        .right = output_safety_tick(output, right),
    };
}
