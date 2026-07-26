/* render_midi — offline renderer: Standard MIDI File in, stereo WAV out.
 * Hosted (driver layer); the deterministic twin of the live path for
 * whole songs: the same channel-message map as tw91 (poly key pressure
 * = per-note key depth, CC11 swell,
 * CC70-78 drawbars, CC80-83 percussion, CC84 vibrato, CC85 drive,
 * CC86-90 rotary mode/speed-switch/balance/width/drive, CC120/123
 * panic), the same core, no real-time clock — event ticks convert to
 * sample frames through the file's tempo map. Renders are two-run
 * FNV-checked and logged in docs/renders.md for cross-milestone
 * comparison. Since M6 the output is interleaved stereo; with the
 * rotary bypassed (the default) both channels carry the mono chain
 * bit-identically.
 *
 * SMF support: format 0/1, PPQ division (SMPTE rejected), running
 * status, tempo changes from any track. Channel filtering (-c) picks
 * the instrument's part out of a full arrangement; -f octave-folds
 * out-of-compass notes into 36..96, as a player would.
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"
#include "wav.h"

typedef struct {
    uint64_t tick;
    uint32_t seq; /* stable order for equal ticks: file order */
    uint8_t status, d1, d2;
} ev_t;

typedef struct {
    uint64_t tick;
    uint32_t seq;
    uint32_t us_per_q;
} tempo_t;

static uint64_t read_vlq(const uint8_t *d, size_t n, size_t *i) {
    uint64_t v = 0;
    while (*i < n) {
        uint8_t b = d[(*i)++];
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    return v;
}

static int cmp_ev(const void *a, const void *b) {
    const ev_t *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq ? 1 : 0;
}

static int cmp_tempo(const void *a, const void *b) {
    const tempo_t *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq ? 1 : 0;
}

/* Octave-fold a note into the 61-key compass, as a player would. */
static int fold_note(int n) {
    while (n < TW_NOTE_MIN) n += 12;
    while (n > TW_NOTE_MAX) n -= 12;
    return n;
}

/* The same, into the ep73 compass. */
static int fold_note_ep(int n) {
    while (n < EP_NOTE_MIN) n += 12;
    while (n > EP_NOTE_MAX) n -= 12;
    return n;
}

static struct {
    unsigned long notes, depths, ccs, folded;
} stats;

/* The tw91 channel-message map, minus the rawmidi plumbing. */
static struct { bool on, third, slow, normal; } perc_state = { false, false, false, true };

static void apply_msg(tw_instrument *ins, uint8_t status, uint8_t d1, uint8_t d2,
                      bool fold) {
    /* depth folds with its note or the two would address different keys */
    int note = (fold && (status & 0xF0) <= 0xA0) ? fold_note(d1) : d1;
    if (fold && note != d1) stats.folded++;
    switch (status & 0xF0) {
    case 0x90:
        tw_organ_note(&ins->organ, note, d2 > 0, d2);
        stats.notes++;
        break;
    case 0x80:
        tw_organ_note(&ins->organ, note, false, d2);
        stats.notes++;
        break;
    case 0xA0:
        tw_organ_note_depth(&ins->organ, note, d2);
        stats.depths++;
        break;
    case 0xB0:
        stats.ccs++;
        if (d1 == 11) tw_organ_set_swell(&ins->organ, (float)d2 / 127.0f);
        else if (d1 >= 70 && d1 <= 78)
            tw_organ_set_drawbar(&ins->organ, d1 - 70, (d2 * 8 + 63) / 127);
        else if (d1 == 120 || d1 == 123) tw_organ_panic(&ins->organ);
        else if (d1 == 80 || d1 == 81 || d1 == 82 || d1 == 83) {
            if (d1 == 80) perc_state.on = d2 >= 64;
            if (d1 == 81) perc_state.third = d2 >= 64;
            if (d1 == 82) perc_state.slow = d2 >= 64;
            if (d1 == 83) perc_state.normal = d2 >= 64;
            tw_organ_set_percussion(&ins->organ, perc_state.on, perc_state.third,
                                    perc_state.slow, perc_state.normal);
        } else if (d1 == 84) tw_organ_set_vibrato(&ins->organ, d2 / 19);
        else if (d1 == 85) tw_instrument_set_drive(ins, (float)d2 / 127.0f);
        else if (d1 == 86) tw_rotary_set_mode(&ins->rotary, d2 / 32);
        else if (d1 == 87)
            tw_rotary_set_mode(&ins->rotary,
                               d2 >= 64 ? TW_ROT_TREMOLO : TW_ROT_CHORALE);
        else if (d1 == 88) tw_rotary_set_balance(&ins->rotary, (float)d2 / 127.0f);
        else if (d1 == 89) tw_rotary_set_width(&ins->rotary, (float)d2 / 127.0f);
        else if (d1 == 90) tw_rotary_set_drive(&ins->rotary, (float)d2 / 127.0f);
        else stats.ccs--;
        break;
    default:
        break;
    }
}

/* The ep73 channel-message map (docs/ep-constants.md sec 10.1). The organ
 * options above are inert here and the piano's are inert there; the two
 * instruments share this file's SMF reader and nothing else. */
static void apply_msg_ep(ep_piano *p, uint8_t status, uint8_t d1, uint8_t d2,
                         bool fold) {
    int note = (fold && (status & 0xF0) <= 0xA0) ? fold_note_ep(d1) : d1;
    if (fold && note != d1) stats.folded++;
    switch (status & 0xF0) {
    case 0x90:
        ep_piano_note(p, note, d2 > 0, d2);
        stats.notes++;
        break;
    case 0x80:
        ep_piano_note(p, note, false, 0);
        stats.notes++;
        break;
    case 0xA0:
        ep_piano_key_pressure(p, note, d2); /* parsed, ignored, counted */
        stats.depths++;
        break;
    case 0xB0:
        stats.ccs++;
        if (d1 == 64) ep_piano_set_sustain(p, d2 >= 64);
        else if (d1 == 120 || d1 == 123) ep_piano_panic(p);
        else stats.ccs--;
        break;
    default:
        break;
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [options] in.mid\n"
            "  -I name  instrument: organ (default) or ep73\n"
            "  -c list  MIDI channels to render, e.g. 0,4 (default: all)\n"
            "  -R digits  registration, nine digits (default 888000000)\n"
            "  -v mode  vibrato 0..6 = off,V1..V3,C1..C3 (default 0)\n"
            "  -D drive drive knob 0..1 (default 0)\n"
            "  -m mode  rotary 0..3 = bypass,chorale,tremolo,brake (default 0)\n"
            "  -p 0|1   percussion on (2nd/fast/normal; default 0)\n"
            "  -f       octave-fold out-of-compass notes into 36..96\n"
            "  -g gain  master gain (default 0.125)\n"
            "  -w wear  M7 wear knob 0..1 (default 0 = idealized reference)\n"
            "  -r rate  sample rate (default 48000)\n"
            "  -t secs  release tail after the last event (default 2)\n"
            "  -o path  output WAV (default render.wav)\n",
            argv0);
}

typedef struct {
    const uint8_t *reg;
    int vib, perc, rot;
    float drive, gain, wear;
    bool fold, ep;
    float rate;
} settings_t;

/* The ep73 twin of render(). Mono in, written to both channels, so the
 * buffer, the WAV and the two-run FNV contract are the organ's unchanged. */
static double render_ep(const ev_t *ev, size_t nev, const settings_t *s,
                        float *buf, int64_t frames, const int64_t *ev_frame,
                        uint32_t *out_of_compass) {
    ep_piano p;
    ep_piano_init(&p, s->rate);
    memset(&stats, 0, sizeof stats);

    size_t next = 0;
    float peak = 0.0f;
    for (int64_t i = 0; i < frames; i++) {
        while (next < nev && ev_frame[next] <= i) {
            apply_msg_ep(&p, ev[next].status, ev[next].d1, ev[next].d2, s->fold);
            next++;
        }
        float y = ep_piano_tick(&p) * s->gain;
        buf[2 * i] = y;
        buf[2 * i + 1] = y;
        float a = tw_fabsf(y);
        if (a > peak) peak = a;
    }
    *out_of_compass = p.bank.out_of_compass;
    return (double)peak;
}

static double render(const ev_t *ev, size_t nev, const settings_t *s,
                     float *buf, int64_t frames, const int64_t *ev_frame,
                     uint32_t *out_of_compass) {
    tw_instrument ins;
    tw_instrument_init(&ins, s->rate);
    tw_organ_set_registration(&ins.organ, s->reg);
    tw_organ_set_wear(&ins.organ, s->wear); /* overrides the init default */
    tw_organ_set_vibrato(&ins.organ, s->vib);
    tw_instrument_set_drive(&ins, s->drive);
    tw_rotary_set_mode(&ins.rotary, s->rot);
    perc_state = (typeof(perc_state)){ s->perc != 0, false, false, true };
    tw_organ_set_percussion(&ins.organ, perc_state.on, false, false, true);
    memset(&stats, 0, sizeof stats);

    size_t next = 0;
    float peak = 0.0f;
    for (int64_t i = 0; i < frames; i++) {
        while (next < nev && ev_frame[next] <= i) {
            apply_msg(&ins, ev[next].status, ev[next].d1, ev[next].d2, s->fold);
            next++;
        }
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        buf[2 * i] = y.l * s->gain;
        buf[2 * i + 1] = y.r * s->gain;
        float a = tw_fabsf(buf[2 * i]), b = tw_fabsf(buf[2 * i + 1]);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    *out_of_compass = ins.organ.out_of_compass;
    return (double)peak;
}

int main(int argc, char **argv) {
    uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    settings_t st = { reg, 0, 0, 0, 0.0f, 0.125f, 0.0f, false, false, 48000.0f };
    const char *out_path = "render.wav";
    double tail_s = 2.0;
    uint32_t chan_mask = 0xFFFF;

    int c;
    while ((c = getopt(argc, argv, "I:c:R:v:D:m:p:fg:w:r:t:o:h")) != -1) {
        switch (c) {
        case 'I':
            if (!strcmp(optarg, "ep73")) st.ep = true;
            else if (strcmp(optarg, "organ")) { usage(argv[0]); return 2; }
            break;
        case 'c': {
            chan_mask = 0;
            for (const char *p = optarg; *p;) {
                chan_mask |= 1u << (strtoul(p, (char **)&p, 10) & 15);
                if (*p == ',') p++;
            }
            break;
        }
        case 'R':
            if (strlen(optarg) != TW_DRAWBARS) { usage(argv[0]); return 2; }
            for (int i = 0; i < TW_DRAWBARS; i++)
                reg[i] = (uint8_t)(optarg[i] - '0') > 8 ? 8 : (uint8_t)(optarg[i] - '0');
            break;
        case 'v': st.vib = atoi(optarg); break;
        case 'D': st.drive = (float)atof(optarg); break;
        case 'm': st.rot = atoi(optarg); break;
        case 'p': st.perc = atoi(optarg); break;
        case 'f': st.fold = true; break;
        case 'g': st.gain = (float)atof(optarg); break;
        case 'w': st.wear = (float)atof(optarg); break;
        case 'r': st.rate = (float)atof(optarg); break;
        case 't': tail_s = atof(optarg); break;
        case 'o': out_path = optarg; break;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind >= argc) { usage(argv[0]); return 2; }

    FILE *fp = fopen(argv[optind], "rb");
    if (!fp) { perror(argv[optind]); return 1; }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)fsz);
    if (!d || fread(d, 1, (size_t)fsz, fp) != (size_t)fsz) {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    fclose(fp);
    size_t n = (size_t)fsz;

    if (n < 14 || memcmp(d, "MThd", 4) != 0) {
        fprintf(stderr, "not a Standard MIDI File\n");
        return 1;
    }
    int fmt = d[8] << 8 | d[9];
    int ntrk = d[10] << 8 | d[11];
    int div = d[12] << 8 | d[13];
    if (fmt > 1 || (div & 0x8000)) {
        fprintf(stderr, "unsupported SMF: format %d, division 0x%04x"
                        " (need format 0/1, PPQ)\n", fmt, div);
        return 1;
    }

    /* one pass: channel events on selected channels + the tempo map */
    ev_t *ev = malloc(n * sizeof *ev / 3 + 64);
    tempo_t *tm = malloc(n * sizeof *tm / 3 + 64);
    size_t nev = 0, ntm = 0;
    uint32_t seq = 0;
    size_t pos = 14;
    for (int t = 0; t < ntrk && pos + 8 <= n; t++) {
        if (memcmp(d + pos, "MTrk", 4) != 0) break;
        size_t len = (size_t)d[pos + 4] << 24 | (size_t)d[pos + 5] << 16
                   | (size_t)d[pos + 6] << 8 | d[pos + 7];
        size_t i = pos + 8, end = i + len;
        if (end > n) end = n;
        uint64_t tick = 0;
        uint8_t run = 0;
        while (i < end) {
            tick += read_vlq(d, end, &i);
            if (i >= end) break;
            uint8_t b = d[i];
            if (b & 0x80) { run = b; i++; }
            if (run == 0xFF) {
                uint8_t mt = d[i++];
                uint64_t l = read_vlq(d, end, &i);
                if (mt == 0x51 && l == 3)
                    tm[ntm++] = (tempo_t){ tick, seq++,
                        (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2] };
                i += l;
            } else if (run == 0xF0 || run == 0xF7) {
                i += read_vlq(d, end, &i);
            } else {
                uint8_t typ = run & 0xF0, ch = run & 0x0F;
                uint8_t d1 = d[i], d2 = 0;
                int two = typ != 0xC0 && typ != 0xD0;
                if (two) d2 = d[i + 1];
                i += two ? 2 : 1;
                if ((typ == 0x80 || typ == 0x90 || typ == 0xA0 || typ == 0xB0)
                    && (chan_mask & 1u << ch))
                    ev[nev++] = (ev_t){ tick, seq++, run, d1, d2 };
            }
        }
        pos += 8 + len;
    }
    qsort(ev, nev, sizeof *ev, cmp_ev);
    qsort(tm, ntm, sizeof *tm, cmp_tempo);

    /* ticks -> frames through the tempo map (120 bpm until the first
     * tempo event, per the SMF default) */
    int64_t *ev_frame = malloc(nev * sizeof *ev_frame + 8);
    double sec = 0.0;
    uint64_t seg_tick = 0;
    uint32_t uspq = 500000;
    size_t ti = 0;
    for (size_t e = 0; e < nev; e++) {
        while (ti < ntm && tm[ti].tick <= ev[e].tick) {
            sec += (double)(tm[ti].tick - seg_tick) * uspq * 1e-6 / div;
            seg_tick = tm[ti].tick;
            uspq = tm[ti].us_per_q;
            ti++;
        }
        double s = sec + (double)(ev[e].tick - seg_tick) * uspq * 1e-6 / div;
        ev_frame[e] = (int64_t)(s * (double)st.rate + 0.5);
    }
    int64_t frames = (nev ? ev_frame[nev - 1] : 0)
                   + (int64_t)(tail_s * (double)st.rate);

    printf("render_midi: %s\n", argv[optind]);
    printf("  format %d, %d tracks, %d ticks/quarter, %zu tempo events\n",
           fmt, ntrk, div, ntm);
    printf("  channels 0x%04x: %zu events, %.1f s + %.1f s tail at %.0f Hz\n",
           chan_mask, nev, (double)(nev ? ev_frame[nev - 1] : 0) / st.rate,
           tail_s, (double)st.rate);
    static const char *rot_name[4] = { "bypass", "chorale", "tremolo", "brake" };
    if (st.ep)
        printf("  instrument ep73, compass 28..100, gain %g%s\n",
               (double)st.gain, st.fold ? ", octave-fold" : "");
    else
        printf("  registration %d%d%d%d%d%d%d%d%d, vibrato %d, percussion %s,"
               " drive %.2f, rotary %s, wear %.2f, gain %g%s\n",
               reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[6], reg[7],
               reg[8], st.vib, st.perc ? "on" : "off", (double)st.drive,
               rot_name[st.rot < 0 ? 0 : st.rot > 3 ? 3 : st.rot],
               (double)st.wear, (double)st.gain, st.fold ? ", octave-fold" : "");

    float *buf = malloc(2 * (size_t)frames * sizeof *buf);
    if (!buf) { fprintf(stderr, "out of memory (%lld frames)\n",
                        (long long)frames); return 1; }

    /* two-run FNV: the repo's determinism contract, applied to songs;
     * hashed over the interleaved stereo buffer since M6 */
    uint32_t ooc = 0;
    double (*paint)(const ev_t *, size_t, const settings_t *, float *, int64_t,
                    const int64_t *, uint32_t *) = st.ep ? render_ep : render;
    double peak = paint(ev, nev, &st, buf, frames, ev_frame, &ooc);
    uint64_t h1 = tw_fnv1a64(buf, 2 * (size_t)frames * sizeof *buf, 0);
    peak = paint(ev, nev, &st, buf, frames, ev_frame, &ooc);
    uint64_t h2 = tw_fnv1a64(buf, 2 * (size_t)frames * sizeof *buf, 0);
    printf("  applied: %lu notes, %lu %s, %lu ccs, %lu folded,"
           " %u out-of-compass\n",
           stats.notes, stats.depths, st.ep ? "key pressures" : "depths",
           stats.ccs, stats.folded, ooc);
    printf("  peak %.3f, FNV64 %016llx %s\n", peak, (unsigned long long)h1,
           h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = wav_write_f32(out_path, buf, 2 * (long)frames, (int)st.rate, 2);
    printf("  wav: %s%s\n", out_path, rc ? " (WRITE FAILED)" : "");
    return (rc == 0 && h1 == h2) ? 0 : 1;
}
