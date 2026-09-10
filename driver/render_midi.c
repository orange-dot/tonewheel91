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
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/tonewheel.h"
#include "../src/epiano.h"
#include "host_parse.h"
#include "midi_map.h"
#include "ma_midi_render.h"
#include "smf.h"
#include "smf_schedule.h"
#include "wav.h"

static constexpr double RENDER_MAX_SECONDS = 7200.0;
static midi_map_stats stats;
static ma_midi_stats ma_stats;
static ma_pool_diagnostics ma_pool_stats;
static uint64_t ma_fx_clips, ma_fx_sanitizations;
static float ma_fx_pre_peak, ma_fx_post_peak;

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [options] in.mid\n"
            "  -I name  instrument: organ (default), ep73 or mamutanalog\n"
            "  --patch name       Mamut factory patch: Tepih (default), Lead,\n"
            "                     Dubina, Raster, Prizma or Granica\n"
            "  --voice-limit N    Mamut slots 1..64, including release (default 32)\n"
            "  --unison N         Mamut slots per note: 1/2/4/8 (default 1)\n"
            "  --detune cents     Mamut unison +/- range 0..100 (default 7)\n"
            "  --seed N           Mamut session seed, decimal uint64\n"
            "  --block-frames N   Mamut render block 1..4096 (default 128)\n"
            "  --chorus-mix x     enable chorus, mix 0..1 (default disabled)\n"
            "  --chorus-depth x   0..1 (default .35)\n"
            "  --chorus-rate hz   .03..20 (default .25)\n"
            "  --reverb-mix x     enable reverb, mix 0..1 (default disabled)\n"
            "  --reverb-size x    0..1 (default .65)\n"
            "  --reverb-damping x 0...99 (default .55)\n"
            "  -c list  MIDI channels to render, e.g. 0,4 (default: all)\n"
            "  -R digits  registration, nine digits (default 888000000)\n"
            "  -v mode  vibrato 0..6 = off,V1..V3,C1..C3 (default 0)\n"
            "  -D drive drive knob 0..1 (default 0)\n"
            "  -C cab   ep73 cabinet 0..1 (default 0 = bypass)\n"
            "  -N cond  ep73 condition 0..1 (default: the shipped value)\n"
            "  -m mode  rotary 0..3 = bypass,chorale,tremolo,brake (default 0)\n"
            "  -p 0|1   percussion on (2nd/fast/normal; default 0)\n"
            "  -f       octave-fold out-of-compass notes into 36..96\n"
            "  -g gain  WAV gain (default 0.125; Mamut default 1)\n"
            "  -w wear  M7 wear knob 0..1 (default 0 = idealized reference)\n"
            "  -r rate  sample rate 44100..192000 (default 48000)\n"
            "  -t secs  release tail after the last event (default 2)\n"
            "  -o path  output WAV (default render.wav)\n",
            argv0);
}

typedef enum { INSTRUMENT_ORGAN, INSTRUMENT_EP, INSTRUMENT_MAMUT } instrument_id;
typedef struct {
    const uint8_t *reg;
    int vib, perc, rot;
    float drive, gain, wear, cab, cond;
    bool fold;
    instrument_id instrument;
    ma_voice_pool_config pool;
    const ma_patch *patch;
    const char *patch_name;
    unsigned block_frames;
    ma_chorus_controls chorus;
    ma_reverb_controls reverb;
    unsigned rate;
} settings_t;

static double render_ma(const smf_event *events, size_t count, const settings_t *s,
    float *buf, size_t frames, const size_t *event_frames, uint32_t *out_of_compass) {
    static ma_midi_renderer renderer;
    if (!ma_midi_renderer_init(&renderer, s->rate, &s->pool, s->patch,
        UINT16_MAX, events, event_frames, count)) return NAN;
    ma_effects_set_chorus(&renderer.midi.pool.effects, s->chorus);
    ma_effects_set_reverb(&renderer.midi.pool.effects, s->reverb);
    for (size_t offset = 0; offset < frames;) {
        size_t n = frames - offset;
        if (n > s->block_frames) n = s->block_frames;
        if (!ma_midi_render(&renderer, buf + 2 * offset, n)) return NAN;
        offset += n;
    }
    float peak = 0;
    for (size_t i = 0; i < 2 * frames; i++) {
        buf[i] *= s->gain;
        if (!isfinite(buf[i])) return NAN;
        if (tw_fabsf(buf[i]) > peak) peak = tw_fabsf(buf[i]);
    }
    ma_stats = renderer.midi.stats;
    ma_pool_stats = renderer.midi.pool.diagnostics;
    ma_fx_clips = renderer.midi.pool.effects.clips;
    ma_fx_sanitizations = renderer.midi.pool.effects.sanitizations;
    ma_fx_pre_peak = renderer.midi.pool.effects.pre_peak;
    ma_fx_post_peak = renderer.midi.pool.effects.post_peak;
    *out_of_compass = 0;
    return peak;
}

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


int main(int argc, char **argv) {
    uint8_t reg[TW_DRAWBARS] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    settings_t st = {
        .reg = reg,
        .gain = 0.125f,
        .cond = EP_CONDITION_DEFAULT,
        .rate = 48000,
        .pool = ma_voice_pool_default_config,
        .patch = &ma_patch_tepih,
        .patch_name = "Tepih",
        .block_frames = 128,
        .chorus = {.depth = .35f, .rate_hz = .25f},
        .reverb = {.size = .65f, .damping = .55f},
    };
    bool gain_set = false, ma_options = false, legacy_options = false;
    const char *out_path = "render.wav";
    double tail_s = 2.0;
    uint16_t chan_mask = UINT16_MAX;

    enum { OPT_PATCH = 256, OPT_LIMIT, OPT_UNISON, OPT_DETUNE, OPT_SEED, OPT_BLOCK,
        OPT_CHORUS_MIX, OPT_CHORUS_DEPTH, OPT_CHORUS_RATE,
        OPT_REVERB_MIX, OPT_REVERB_SIZE, OPT_REVERB_DAMPING };
    static const struct option options[] = {
        {"patch", required_argument, NULL, OPT_PATCH},
        {"voice-limit", required_argument, NULL, OPT_LIMIT},
        {"unison", required_argument, NULL, OPT_UNISON},
        {"detune", required_argument, NULL, OPT_DETUNE},
        {"seed", required_argument, NULL, OPT_SEED},
        {"block-frames", required_argument, NULL, OPT_BLOCK},
        {"chorus-mix", required_argument, NULL, OPT_CHORUS_MIX},
        {"chorus-depth", required_argument, NULL, OPT_CHORUS_DEPTH},
        {"chorus-rate", required_argument, NULL, OPT_CHORUS_RATE},
        {"reverb-mix", required_argument, NULL, OPT_REVERB_MIX},
        {"reverb-size", required_argument, NULL, OPT_REVERB_SIZE},
        {"reverb-damping", required_argument, NULL, OPT_REVERB_DAMPING},
        {"help", no_argument, NULL, 'h'},
        {0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "I:c:R:v:D:C:N:m:p:fg:w:r:t:o:h", options, NULL)) != -1) {
        if (c >= OPT_PATCH && c <= OPT_REVERB_DAMPING) ma_options = true;
        if (c > 0 && c < 256 && strchr("RvDCNmpfw", c)) legacy_options = true;
        switch (c) {
        case 'I':
            if (!strcmp(optarg, "ep73")) st.instrument = INSTRUMENT_EP;
            else if (!strcmp(optarg, "organ")) st.instrument = INSTRUMENT_ORGAN;
            else if (!strcmp(optarg, "mamutanalog")) st.instrument = INSTRUMENT_MAMUT;
            else { usage(argv[0]); return 2; }
            break;
        case OPT_PATCH: {
            const char *names[] = {"Tepih", "Lead", "Dubina", "Raster", "Prizma", "Granica"};
            const ma_patch *patches[] = {&ma_patch_tepih, &ma_patch_lead, &ma_patch_dubina,
                &ma_patch_raster, &ma_patch_prizma, &ma_patch_granica};
            unsigned i = 0;
            while (i < 6 && strcmp(optarg, names[i])) i++;
            if (i == 6) { usage(argv[0]); return 2; }
            st.patch = patches[i]; st.patch_name = names[i];
            break;
        }
        case OPT_LIMIT: case OPT_UNISON: case OPT_SEED: case OPT_BLOCK: {
            uint64_t value = 0;
            uint64_t max = c == OPT_LIMIT ? 64 : c == OPT_UNISON ? 8
                : c == OPT_BLOCK ? 4096 : UINT64_MAX;
            if (!host_parse_u64(optarg, c == OPT_SEED ? 0 : 1, max, &value)) {
                usage(argv[0]); return 2;
            }
            if (c == OPT_UNISON && value != 1 && value != 2 && value != 4 && value != 8) {
                usage(argv[0]); return 2;
            }
            if (c == OPT_LIMIT) st.pool.limit = (uint8_t)value;
            else if (c == OPT_UNISON) st.pool.unison_count = (uint8_t)value;
            else if (c == OPT_BLOCK) st.block_frames = (unsigned)value;
            else st.pool.session_seed = value;
            break;
        }
        case OPT_DETUNE:
            if (!parse_float(optarg, 0, 100, &st.pool.detune_cents)) { usage(argv[0]); return 2; }
            break;
        case OPT_CHORUS_MIX: case OPT_CHORUS_DEPTH: case OPT_CHORUS_RATE:
        case OPT_REVERB_MIX: case OPT_REVERB_SIZE: case OPT_REVERB_DAMPING: {
            float value = 0;
            double lo = c == OPT_CHORUS_RATE ? .03 : 0;
            double hi = c == OPT_CHORUS_RATE ? 20 : c == OPT_REVERB_DAMPING ? .99 : 1;
            if (!parse_float(optarg, lo, hi, &value)) { usage(argv[0]); return 2; }
            if (c == OPT_CHORUS_MIX) { st.chorus.mix = value; st.chorus.enabled = true; }
            else if (c == OPT_CHORUS_DEPTH) st.chorus.depth = value;
            else if (c == OPT_CHORUS_RATE) st.chorus.rate_hz = value;
            else if (c == OPT_REVERB_MIX) { st.reverb.mix = value; st.reverb.enabled = true; }
            else if (c == OPT_REVERB_SIZE) st.reverb.size = value;
            else st.reverb.damping = value;
            break;
        }
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
            gain_set = true;
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
    if ((ma_options && st.instrument != INSTRUMENT_MAMUT)
        || (legacy_options && st.instrument == INSTRUMENT_MAMUT)
        || st.pool.limit % st.pool.unison_count) {
        fprintf(stderr, "render_midi: incompatible instrument options or pool limit/unison\n");
        return 2;
    }
    if (st.instrument == INSTRUMENT_MAMUT && !gain_set) st.gain = 1;

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
    if (!smf_make_event_frames(&midi, st.rate, tail_s, &event_frame, &frames)) {
        fprintf(stderr, "render_midi: render exceeds the checked duration"
                        " or allocation limits\n");
        smf_dispose(&midi);
        return 1;
    }
    /* A zero-tail Mamut render still dispatches/samples the last event.
     * Preserve the legacy organ/EP duration convention. */
    if (st.instrument == INSTRUMENT_MAMUT && midi.event_count
        && frames == event_frame[midi.event_count-1]) frames++;
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
    if (st.instrument == INSTRUMENT_MAMUT)
        printf("  instrument mamutanalog, patch %s, limit %u, unison %u, "
               "detune +/-%.2f cents, seed %" PRIu64 ", block %u, gain %g\n",
               st.patch_name, st.pool.limit, st.pool.unison_count,
               (double)st.pool.detune_cents, st.pool.session_seed,
               st.block_frames, (double)st.gain);
    else if (st.instrument == INSTRUMENT_EP)
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
                    const size_t *, uint32_t *) = st.instrument == INSTRUMENT_MAMUT
                        ? render_ma : st.instrument == INSTRUMENT_EP ? render_ep : render;
    double peak = paint(midi.events, midi.event_count, &st, buf, frames,
                        event_frame, &ooc);
    if (!isfinite(peak)) {
        free(buf); free(event_frame); smf_dispose(&midi);
        fprintf(stderr, "render_midi: Mamut render failed\n"); return 1;
    }
    uint64_t h1 = tw_fnv1a64(buf, buffer_bytes, 0);
    peak = paint(midi.events, midi.event_count, &st, buf, frames,
                 event_frame, &ooc);
    uint64_t h2 = tw_fnv1a64(buf, buffer_bytes, 0);
    if (st.instrument == INSTRUMENT_MAMUT) {
        printf("  applied: %" PRIu64 " note-ons, %" PRIu64 " note-offs, %" PRIu64
               " bends, %" PRIu64 " channel pressures, %" PRIu64 " poly pressures, %" PRIu64 " ccs\n",
               ma_stats.note_ons, ma_stats.note_offs, ma_stats.bends,
               ma_stats.channel_pressures, ma_stats.poly_pressures, ma_stats.ccs);
        printf("  ignored: %" PRIu64 " ccs, %" PRIu64 " messages, %" PRIu64
               " invalid, %" PRIu64 " unmatched releases; allocation failures %" PRIu64 "\n",
               ma_stats.ignored_ccs, ma_stats.unsupported_messages, ma_stats.invalid_messages,
               ma_stats.unmatched_note_offs, ma_stats.allocation_failures);
        printf("  pool: active %u, peak %u, steals released/sustained/held %" PRIu64
               "/%" PRIu64 "/%" PRIu64 ", sleeps %" PRIu64 ", wakes %" PRIu64 "\n",
               ma_pool_stats.active_slots, ma_pool_stats.peak_slots, ma_pool_stats.steals_released,
               ma_pool_stats.steals_sustained, ma_pool_stats.steals_held,
               ma_pool_stats.sleeps, ma_pool_stats.wakes);
        printf("  effects (including feedback writes): pre/post peak %.6f/%.6f, "
               "clips %" PRIu64 ", sanitizations %" PRIu64 "; pool %zu bytes\n",
               (double)ma_fx_pre_peak, (double)ma_fx_post_peak,
               ma_fx_clips, ma_fx_sanitizations, sizeof(ma_voice_pool));
    } else printf("  applied: %lu notes, %lu %s, %lu ccs, %lu folded,"
           " %u out-of-compass\n",
           stats.notes, st.instrument == INSTRUMENT_EP ? stats.pressures : stats.depths,
           st.instrument == INSTRUMENT_EP ? "key pressures" : "depths",
           stats.ccs, stats.folded, ooc);
    printf("  peak %.3f, FNV64 %016llx %s\n", peak, (unsigned long long)h1,
           h1 == h2 ? "(two runs identical)" : "MISMATCH");

    int rc = !isfinite(peak) || h1 != h2 ? -1 : wav_write_f32(out_path, buf, frames, st.rate, 2);
    printf("  wav: %s%s\n", out_path, rc ? " (WRITE FAILED)" : "");
    free(buf);
    free(event_frame);
    smf_dispose(&midi);
    return rc == 0 && h1 == h2 ? 0 : 1;
}
