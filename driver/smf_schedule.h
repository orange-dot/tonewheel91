#ifndef SMF_SCHEDULE_H
#define SMF_SCHEDULE_H
#include "smf.h"

/* Parsed PPQ file to absolute frames, preserving tempo and equal-time order.
 * Caller frees *out. The legacy duration is last event frame + tail frames. */
bool smf_make_event_frames(const smf_file *midi, unsigned rate,
    double tail_seconds, size_t **out, size_t *out_frames);
/* The frame at which each tempo event takes effect and its tempo in quarter
 * notes per minute, by the same tick-to-frame conversion. Caller frees both;
 * both are null for a file without tempo events. */
bool smf_make_tempo_frames(const smf_file *midi, unsigned rate, size_t **out_frames,
    float **out_bpm);
#endif
