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
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"
#include "host_parse.h"
#include "midi_map.h"
#include "smf.h"
#include "wav.h"

static constexpr double RENDER_MAX_SECONDS = 7200.0;
static midi_map_stats stats;

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [options] in.mid\n"
            "  -I name  instrument: organ (default) or ep73\n"
            "  -c list  MIDI channels to render, e.g. 0,4 (default: all)\n"
            "  -R digits  registration, nine digits (default 888000000)\n"
            "  -v mode  vibrato 0..6 = off,V1..V3,C1..C3 (default 0)\n"
            "  -D drive drive knob 0..1 (default 0)\n"
            "  -C cab   ep73 cabinet 0..1 (default 0 = bypass)\n"
            "  -N cond  ep73 condition 0..1 (default: the shipped value)\n"
            "  -m mode  rotary 0..3 = bypass,chorale,tremolo,brake (default 0)\n"
            "  -p 0|1   percussion on (2nd/fast/normal; default 0)\n"
            "  -f       octave-fold out-of-compass notes into 36..96\n"
            "  -g gain  master gain (default 0.125)\n"
            "  -w wear  M7 wear knob 0..1 (default 0 = idealized reference)\n"
            "  -r rate  sample rate 44100..192000 (default 48000)\n"
            "  -t secs  release tail after the last event (default 2)\n"
            "  -o path  output WAV (default render.wav)\n",
            argv0);
}

typedef struct {
    const uint8_t *reg;
    int vib, perc, rot;
    float drive, gain, wear, cab, cond;
    bool fold, ep;
    unsigned rate;
} settings_t;

/* The ep73 twin of render(). Stereo from the tremolo on; with it off the
 * two channels are identical, so the buffer, the WAV and the two-run FNV
 * contract are the organ's unchanged. */
static double render_ep(const smf_event *ev, size_t nev, const settings_t *s,
                        float *buf, size_t frames, const size_t *ev_frame,
                        uint32_t *out_of_compass) {
    ep_piano p;
    ep_piano_init(&p, s->rate);
    ep_piano_set_drive(&p, s->drive);  /* -D, the same flag the organ uses */
    ep_piano_set_cabinet(&p, s->cab);  /* -C, ep73 only                    */
    ep_piano_set_condition(&p, s->cond); /* -N, ep73 only                  */
    ep_midi_map map;
    ep_midi_map_init(&map, s->fold);

    size_t next = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        while (next < nev && ev_frame[next] <= i) {
            ep_midi_apply(&map, &p, ev[next].status, ev[next].d1, ev[next].d2);
            next++;
        }
        tw_stereo y = ep_piano_tick_stereo(&p);
        buf[2 * i] = y.l * s->gain;
        buf[2 * i + 1] = y.r * s->gain;
        float a = tw_fabsf(buf[2 * i]), b = tw_fabsf(buf[2 * i + 1]);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    stats = map.stats;
    *out_of_compass = p.bank.out_of_compass;
    return (double)peak;
}

static double render(const smf_event *ev, size_t nev, const settings_t *s,
                     float *buf, size_t frames, const size_t *ev_frame,
                     uint32_t *out_of_compass) {
    tw_instrument ins;
    tw_instrument_init(&ins, s->rate);
    tw_organ_set_registration(&ins.organ, s->reg);
    tw_organ_set_wear(&ins.organ, s->wear); /* overrides the init default */
    tw_organ_set_vibrato(&ins.organ, s->vib);
    tw_instrument_set_drive(&ins, s->drive);
    tw_rotary_set_mode(&ins.rotary, s->rot);
    organ_midi_map map;
    organ_midi_map_init(&map, s->fold, s->perc != 0);
    tw_organ_set_percussion(&ins.organ, s->perc != 0, false, false, true);

    size_t next = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        while (next < nev && ev_frame[next] <= i) {
            organ_midi_apply(&map, &ins, ev[next].status, ev[next].d1, ev[next].d2);
            next++;
        }
        tw_stereo y = tw_instrument_tick_stereo(&ins);
        buf[2 * i] = y.l * s->gain;
        buf[2 * i + 1] = y.r * s->gain;
        float a = tw_fabsf(buf[2 * i]), b = tw_fabsf(buf[2 * i + 1]);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    stats = map.stats;
    *out_of_compass = ins.organ.out_of_compass;
    return (double)peak;
}

static bool parse_channels(const char *text, uint16_t *out) {
    if (!text || !*text) return false;
    uint16_t mask = 0;
    const char *p = text;
    for (;;) {
        if (*p < '0' || *p > '9') return false;
        errno = 0;
        char *end = 0;
        unsigned long channel = strtoul(p, &end, 10);
        if (errno == ERANGE || end == p || channel > 15) return false;
        mask |= (uint16_t)(1u << channel);
        if (!*end) break;
        if (*end != ',' || !end[1]) return false;
        p = end + 1;
    }
    *out = mask;
    return true;
}

static bool parse_int(const char *text, unsigned min, unsigned max, int *out) {
    uint64_t value = 0;
    if (!host_parse_u64(text, min, max, &value)) return false;
    *out = (int)value;
    return true;
}

static bool parse_float(const char *text, double min, double max, float *out) {
    double value = 0.0;
    if (!host_parse_double(text, min, max, &value)) return false;
    *out = (float)value;
    return true;
}

static bool read_file(const char *path, uint8_t **out, size_t *out_size) {
    *out = 0;
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    if (length < 0 || (uintmax_t)length > SIZE_MAX) ok = false;
    if (ok && fseek(file, 0, SEEK_SET) != 0) ok = false;

    size_t size = ok ? (size_t)length : 0;
    uint8_t *data = ok ? malloc(size ? size : 1) : 0;
    if (!data) ok = false;
    if (ok && fread(data, 1, size, file) != size) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        free(data);
        return false;
    }
    *out = data;
    *out_size = size;
    return true;
}

static bool make_event_frames(const smf_file *midi, unsigned rate,
                              double tail_seconds, size_t **out,
                              size_t *out_frames) {
    *out = 0;
    *out_frames = 0;
    size_t *frames = 0;
    if (midi->event_count) {
        size_t bytes = 0;
        if (!host_size_mul(midi->event_count, sizeof *frames, &bytes))
            return false;
        frames = malloc(bytes);
        if (!frames) return false;
    }

    double seconds = 0.0;
    uint64_t segment_tick = 0;
    uint32_t us_per_quarter = 500000;
    size_t tempo = 0;
    size_t last = 0;
    double max_frame = RENDER_MAX_SECONDS * rate;
    for (size_t event = 0; event < midi->event_count; event++) {
        while (tempo < midi->tempo_count
               && midi->tempos[tempo].tick <= midi->events[event].tick) {
            seconds += (double)(midi->tempos[tempo].tick - segment_tick)
                     * us_per_quarter * 1e-6 / midi->division;
            segment_tick = midi->tempos[tempo].tick;
            us_per_quarter = midi->tempos[tempo].us_per_quarter;
            tempo++;
        }
        double at = seconds
                  + (double)(midi->events[event].tick - segment_tick)
                  * us_per_quarter * 1e-6 / midi->division;
        double frame = at * rate + 0.5;
        if (!isfinite(frame) || frame < 0.0 || frame > max_frame
            || frame > SIZE_MAX) {
            free(frames);
            return false;
        }
        frames[event] = (size_t)frame;
        last = frames[event];
    }

    double tail_frame_value = tail_seconds * rate;
    if (!isfinite(tail_frame_value) || tail_frame_value > SIZE_MAX) {
        free(frames);
        return false;
    }
    size_t total = 0;
    if (!host_size_add(last, (size_t)tail_frame_value, &total)
        || (double)total > max_frame) {
        free(frames);
        return false;
    }
    *out = frames;
    *out_frames = total;
    return true;
}

int main(int argc, char **argv) {
    uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    settings_t st = {
        .reg = reg,
        .gain = 0.125f,
        .cond = EP_CONDITION_DEFAULT,
        .rate = 48000,
    };
    const char *out_path = "render.wav";
    double tail_s = 2.0;
    uint16_t chan_mask = UINT16_MAX;

    int c;
    while ((c = getopt(argc, argv, "I:c:R:v:D:C:N:m:p:fg:w:r:t:o:h")) != -1) {
        switch (c) {
        case 'I':
            if (!strcmp(optarg, "ep73")) st.ep = true;
            else if (strcmp(optarg, "organ")) { usage(argv[0]); return 2; }
            break;
        case 'c':
            if (!parse_channels(optarg, &chan_mask)) { usage(argv[0]); return 2; }
            break;
        case 'R':
            if (strlen(optarg) != TW_DRAWBARS) { usage(argv[0]); return 2; }
            for (int i = 0; i < TW_DRAWBARS; i++) {
                if (optarg[i] < '0' || optarg[i] > '8') {
                    usage(argv[0]);
                    return 2;
                }
                reg[i] = (uint8_t)(optarg[i] - '0');
            }
            break;
        case 'v':
            if (!parse_int(optarg, 0, 6, &st.vib)) { usage(argv[0]); return 2; }
            break;
        case 'D':
            if (!parse_float(optarg, 0.0, 1.0, &st.drive)) { usage(argv[0]); return 2; }
            break;
        case 'C':
            if (!parse_float(optarg, 0.0, 1.0, &st.cab)) { usage(argv[0]); return 2; }
            break;
        case 'N':
            if (!parse_float(optarg, 0.0, 1.0, &st.cond)) { usage(argv[0]); return 2; }
            break;
        case 'm':
            if (!parse_int(optarg, 0, 3, &st.rot)) { usage(argv[0]); return 2; }
            break;
        case 'p':
            if (!parse_int(optarg, 0, 1, &st.perc)) { usage(argv[0]); return 2; }
            break;
        case 'f': st.fold = true; break;
        case 'g':
            if (!parse_float(optarg, 0.0, 16.0, &st.gain)) { usage(argv[0]); return 2; }
            break;
        case 'w':
            if (!parse_float(optarg, 0.0, 1.0, &st.wear)) { usage(argv[0]); return 2; }
            break;
        case 'r': {
            uint64_t rate = 0;
            if (!host_parse_u64(optarg, 44100, 192000, &rate)) {
                usage(argv[0]);
                return 2;
            }
            st.rate = (unsigned)rate;
            break;
        }
        case 't':
            if (!host_parse_double(optarg, 0.0, RENDER_MAX_SECONDS, &tail_s)) {
                usage(argv[0]);
                return 2;
            }
            break;
        case 'o':
            if (!*optarg) { usage(argv[0]); return 2; }
            out_path = optarg;
            break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind + 1 != argc) { usage(argv[0]); return 2; }

    uint8_t *data = 0;
    size_t data_size = 0;
    if (!read_file(argv[optind], &data, &data_size)) {
        fprintf(stderr, "render_midi: cannot read %s\n", argv[optind]);
        return 1;
    }
    smf_file midi = { 0 };
    smf_error smf_problem = { 0 };
    if (!smf_parse(data, data_size, chan_mask, &midi, &smf_problem)) {
        fprintf(stderr, "render_midi: SMF error at byte %zu: %s\n",
                smf_problem.offset, smf_problem.message);
        free(data);
        return 1;
    }
    free(data);

    size_t *event_frame = 0;
    size_t frames = 0;
    if (!make_event_frames(&midi, st.rate, tail_s, &event_frame, &frames)) {
        fprintf(stderr, "render_midi: render exceeds the checked duration"
                        " or allocation limits\n");
        smf_dispose(&midi);
        return 1;
    }
    size_t wav_limit = (UINT32_MAX - 48u) / (2u * sizeof(float));
    if (frames > wav_limit) {
        fprintf(stderr, "render_midi: render exceeds the RIFF/WAVE size limit\n");
        free(event_frame);
        smf_dispose(&midi);
        return 1;
    }

    printf("render_midi: %s\n", argv[optind]);
    printf("  format %u, %u tracks, %u ticks/quarter, %zu tempo events\n",
           midi.format, midi.tracks, midi.division, midi.tempo_count);
    printf("  channels 0x%04x: %zu events, %.1f s + %.1f s tail at %.0f Hz\n",
           chan_mask, midi.event_count,
           (double)(midi.event_count ? event_frame[midi.event_count - 1] : 0)
           / st.rate,
           tail_s, (double)st.rate);
    static const char *rot_name[4] = { "bypass", "chorale", "tremolo", "brake" };
    if (st.ep)
        printf("  instrument ep73, compass 28..100, drive %.2f, cabinet %.2f,"
               " condition %.2f, gain %g%s\n", (double)st.drive, (double)st.cab,
               (double)st.cond, (double)st.gain, st.fold ? ", octave-fold" : "");
    else
        printf("  registration %d%d%d%d%d%d%d%d%d, vibrato %d, percussion %s,"
               " drive %.2f, rotary %s, wear %.2f, gain %g%s\n",
               reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[6], reg[7],
               reg[8], st.vib, st.perc ? "on" : "off", (double)st.drive,
               rot_name[st.rot < 0 ? 0 : st.rot > 3 ? 3 : st.rot],
               (double)st.wear, (double)st.gain, st.fold ? ", octave-fold" : "");

    size_t samples = 0, buffer_bytes = 0;
    if (!host_size_mul(frames, 2, &samples)
        || !host_size_mul(samples, sizeof(float), &buffer_bytes)) {
        fprintf(stderr, "render_midi: audio buffer size overflow\n");
        free(event_frame);
        smf_dispose(&midi);
        return 1;
    }
    float *buf = malloc(buffer_bytes ? buffer_bytes : sizeof *buf);
    if (!buf) {
        fprintf(stderr, "render_midi: out of memory (%zu frames)\n", frames);
        free(event_frame);
        smf_dispose(&midi);
        return 1;
    }

    /* two-run FNV: the repo's determinism contract, applied to songs;
     * hashed over the interleaved stereo buffer since M6 */
    uint32_t ooc = 0;
    double (*paint)(const smf_event *, size_t, const settings_t *, float *, size_t,
                    const size_t *, uint32_t *) = st.ep ? render_ep : render;
    double peak = paint(midi.events, midi.event_count, &st, buf, frames,
                        event_frame, &ooc);
    uint64_t h1 = tw_fnv1a64(buf, buffer_bytes, 0);
    peak = paint(midi.events, midi.event_count, &st, buf, frames,
                 event_frame, &ooc);
    uint64_t h2 = tw_fnv1a64(buf, buffer_bytes, 0);
    printf("  applied: %lu notes, %lu %s, %lu ccs, %lu folded,"
           " %u out-of-compass\n",
           stats.notes, st.ep ? stats.pressures : stats.depths,
           st.ep ? "key pressures" : "depths",
           stats.ccs, stats.folded, ooc);
    printf("  peak %.3f, FNV64 %016llx %s\n", peak, (unsigned long long)h1,
           h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = wav_write_f32(out_path, buf, frames, st.rate, 2);
    printf("  wav: %s%s\n", out_path, rc ? " (WRITE FAILED)" : "");
    free(buf);
    free(event_frame);
    smf_dispose(&midi);
    return rc == 0 && h1 == h2 ? 0 : 1;
}
