#include "pb_seq.h"

bool pb_pattern_valid(const pb_pattern *p) {
    if (p->bpm < PB_SEQ_BPM_MIN || p->bpm > PB_SEQ_BPM_MAX || p->steps < 1 || p->steps > PB_SEQ_STEPS
        || p->swing > PB_SEQ_SWING_MAX || p->root < 12 || p->root > 115)
        return false;
    for (unsigned i = 0; i < PB_SEQ_STEPS; i++) {
        const pb_step *s = &p->step[i];
        if (s->mask >> PB_SEQ_SPAN || s->velocity < 1 || s->velocity > 127 || s->gate < 1 || s->gate > 100
            || s->octave < -PB_SEQ_OCTAVE_MAX || s->octave > PB_SEQ_OCTAVE_MAX)
            return false;
    }
    return true;
}

pb_pattern pb_pattern_empty(uint8_t root, uint32_t bpm) {
    pb_pattern p = { .bpm = bpm, .steps = 16, .root = root };
    for (unsigned i = 0; i < PB_SEQ_STEPS; i++) p.step[i] = (pb_step){ .velocity = 100, .gate = 50 };
    return p;
}

bool pb_pattern_has_notes(const pb_pattern *p) {
    for (unsigned i = 0; i < p->steps; i++) if (p->step[i].mask) return true;
    return false;
}

bool pb_sequence_valid(const pb_sequence *s) {
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (!pb_pattern_valid(&s->channel[c]) || s->channel[c].bpm != s->channel[0].bpm
            || s->channel[c].swing != s->channel[0].swing)
            return false;
    return true;
}

pb_sequence pb_sequence_single(const pb_pattern *p) {
    pb_sequence s = { .channel[0] = *p };
    for (unsigned c = 1; c < PB_CHANNELS; c++) {
        s.channel[c] = pb_pattern_empty(p->root, p->bpm);
        s.channel[c].swing = p->swing;
    }
    return s;
}

unsigned pb_sequence_steps(const pb_sequence *s) {
    unsigned steps = 0;
    for (unsigned c = 0; c < PB_CHANNELS; c++)
        if (!s->mute[c] && pb_pattern_has_notes(&s->channel[c]) && s->channel[c].steps > steps)
            steps = s->channel[c].steps;
    return steps ? steps : s->channel[0].steps;
}

int pb_step_note(const pb_pattern *p, unsigned step, unsigned row) {
    int note = (int)p->root - 12 + (int)row + 12 * p->step[step % PB_SEQ_STEPS].octave;
    return note >= 0 && note <= 127 ? note : -1;
}

/* Frames per step = rate·60 / (bpm·4) = rate·1500 / centi-BPM. */
static uint64_t swing_frames(const pb_seq_run *r) {
    return (uint64_t)r->origin_swing * r->rate * 1500u / (100u * (uint64_t)r->origin_bpm);
}

uint64_t pb_seq_step_frame(const pb_seq_run *r, uint64_t k) {
    uint64_t j = k - r->origin_step;
    uint64_t n = (uint64_t)r->rate * 1500u, d = r->origin_bpm;
    uint64_t base = j / d * n + j % d * n / d;
    uint64_t sw = swing_frames(r);
    return r->origin_frame + base + (k & 1u ? sw : 0) - (r->origin_step & 1u ? sw : 0);
}

void pb_seq_start(pb_seq_run *r, const pb_pattern *p, uint32_t rate) {
    *r = (pb_seq_run){ .rate = rate, .origin_bpm = p->bpm, .origin_swing = p->swing, .playing = true };
}

uint32_t pb_seq_until(const pb_seq_run *r, uint32_t limit) {
    if (!r->playing) return limit;
    uint64_t next = r->next_boundary;
    for (unsigned i = 0; i < r->held_count; i++) if (r->held_off[i] < next) next = r->held_off[i];
    uint64_t gap = next > r->frame ? next - r->frame : 0;
    return gap < limit ? (uint32_t)gap : limit;
}

bool pb_seq_at_boundary(const pb_seq_run *r) { return r->playing && r->next_boundary == r->frame; }

void pb_seq_adopt(pb_seq_run *r, const pb_pattern *next) {
    if (next->bpm != r->origin_bpm || next->swing != r->origin_swing) {
        r->origin_frame = r->next_boundary;
        r->origin_step = r->step;
        r->origin_bpm = next->bpm;
        r->origin_swing = next->swing;
    }
    if (r->index >= next->steps) r->index = 0;
}

static void push(pb_seq_event *out, size_t max, size_t *n, uint8_t note, uint8_t velocity, bool on) {
    if (*n < max) out[(*n)++] = (pb_seq_event){ .note = note, .velocity = velocity, .on = on };
}

static void drop(pb_seq_run *r, unsigned i) {
    r->held_count--;
    r->held_note[i] = r->held_note[r->held_count];
    r->held_off[i] = r->held_off[r->held_count];
}

size_t pb_seq_fire(pb_seq_run *r, const pb_pattern *p, pb_seq_event *out, size_t max) {
    size_t n = 0;
    if (!r->playing) return 0;
    for (unsigned i = 0; i < r->held_count;)
        if (r->held_off[i] == r->frame) {
            push(out, max, &n, r->held_note[i], 0, false);
            drop(r, i);
        } else i++;
    if (r->next_boundary != r->frame) return n;

    const pb_step *s = &p->step[r->index];
    uint64_t start = pb_seq_step_frame(r, r->step), end = pb_seq_step_frame(r, r->step + 1);
    uint64_t length = end - start, gate = length * s->gate / 100u;
    uint64_t off = s->tie ? UINT64_MAX : start + (gate ? gate : 1);
    bool continued[PB_SEQ_HELD] = { false };
    for (unsigned row = 0; row < PB_SEQ_SPAN; row++) {
        if (!(s->mask >> row & 1u)) continue;
        int note = pb_step_note(p, r->index, row);
        if (note < 0) continue;
        bool tied = false;
        for (unsigned i = 0; i < r->held_count && !tied; i++)
            if (r->held_note[i] == note && r->held_off[i] == UINT64_MAX) {
                tied = continued[i] = true;
                r->held_off[i] = off;
            }
        if (tied || r->held_count >= PB_SEQ_HELD) continue;
        push(out, max, &n, (uint8_t)note, s->velocity, true);
        continued[r->held_count] = true;
        r->held_note[r->held_count] = (uint8_t)note;
        r->held_off[r->held_count++] = off;
    }
    /* Tied notes the new step does not continue end after its note-ons. */
    for (unsigned i = r->held_count; i-- > 0;)
        if (!continued[i] && r->held_off[i] == UINT64_MAX) {
            push(out, max, &n, r->held_note[i], 0, false);
            drop(r, i);
        }
    r->sounding = r->index;
    r->index = (r->index + 1) % p->steps;
    r->step++;
    r->next_boundary = end;
    return n;
}

void pb_seq_skip(pb_seq_run *r, uint32_t frames) { r->frame += frames; }

size_t pb_seq_stop(pb_seq_run *r, pb_seq_event *out, size_t max) {
    size_t n = 0;
    for (unsigned i = 0; i < r->held_count; i++) push(out, max, &n, r->held_note[i], 0, false);
    r->held_count = 0;
    r->playing = false;
    return n;
}

static const char *const BUILTIN[PB_PAT_COUNT] = {
    "pulse", "octaves", "fifths", "arpeggio", "bass line", "chord stabs", "held chord",
    "velocity ladder", "pitch ladder", "tied line",
};

const char *pb_builtin_name(pb_builtin b) { return b < PB_PAT_COUNT ? BUILTIN[b] : "?"; }

static void note_at(pb_pattern *p, unsigned step, int semitone) {
    p->step[step].mask |= UINT32_C(1) << (unsigned)(semitone + 12);
}

pb_pattern pb_builtin_pattern(pb_builtin b, uint8_t root, uint32_t bpm) {
    pb_pattern p = pb_pattern_empty(root, bpm);
    switch (b) {
    case PB_PAT_PULSE:
        for (unsigned i = 0; i < 16; i += 4) note_at(&p, i, 0);
        break;
    case PB_PAT_OCTAVES:
        for (unsigned i = 0; i < 16; i += 2) note_at(&p, i, i & 2u ? 12 : 0);
        break;
    case PB_PAT_FIFTHS:
        for (unsigned i = 0; i < 16; i += 2) note_at(&p, i, i & 2u ? 7 : 0);
        break;
    case PB_PAT_ARPEGGIO: {
        static const int UP[] = { 0, 4, 7, 12, 7, 4 };
        for (unsigned i = 0; i < 16; i++) note_at(&p, i, UP[i % 6]);
        break;
    }
    case PB_PAT_BASS: {
        static const int LINE[16] = { -12, 99, -12, 0, 99, -12, -5, 99, -12, 99, -9, -7, 99, -12, 0, -2 };
        for (unsigned i = 0; i < 16; i++)
            if (LINE[i] != 99) { note_at(&p, i, LINE[i]); p.step[i].gate = 35; }
        break;
    }
    case PB_PAT_STABS:
        for (unsigned i = 0; i < 16; i += 4) {
            note_at(&p, i, 0); note_at(&p, i, 3); note_at(&p, i, 7); note_at(&p, i, 10);
            p.step[i].gate = 25;
        }
        break;
    case PB_PAT_HELD_CHORD:
        p.steps = 8;
        for (unsigned i = 0; i < 8; i++) {
            note_at(&p, i, 0); note_at(&p, i, 4); note_at(&p, i, 7); note_at(&p, i, 11);
            p.step[i].tie = i < 7;
            p.step[i].gate = 100;
        }
        break;
    case PB_PAT_VELOCITY_LADDER:
        p.steps = 8;
        for (unsigned i = 0; i < 8; i++) {
            note_at(&p, i, 0);
            p.step[i].velocity = (uint8_t)(20 + i * 107 / 7);
            p.step[i].gate = 70;
        }
        break;
    case PB_PAT_PITCH_LADDER:
        /* C1..C6 for a C root at C3: the octave lane walks the compass. */
        p.steps = 6;
        for (unsigned i = 0; i < 6; i++) {
            note_at(&p, i, 0);
            p.step[i].octave = (int8_t)((int)i - 2);
            p.step[i].gate = 80;
        }
        break;
    case PB_PAT_TIED_LINE: {
        static const int LINE[8] = { 0, 3, 7, 5, 3, 10, 7, 0 };
        p.steps = 8;
        for (unsigned i = 0; i < 8; i++) {
            note_at(&p, i, LINE[i]);
            p.step[i].tie = i & 1u ? false : true;
            p.step[i].gate = 90;
        }
        break;
    }
    case PB_PAT_COUNT: break;
    }
    return p;
}
