/* ep73 live driver: ALSA rawmidi in, ALSA PCM out.
 * One thread, synchronous; snd_pcm_writei is the loop's clock — the same
 * shape as the organ's driver, deliberately not merged with it.
 *
 * MIDI (docs/ep-constants.md sec 10.1): notes 28..100, velocity scaling
 * loudness and timbre; CC64 sustain pedal, >= 64 is down; CC120/CC123
 * drop all dampers. CC85 drive, CC91 tremolo, CC92 cabinet and CC93
 * condition are wired. Poly key pressure has no meaning on this instrument
 * — a key that is down has already thrown its hammer — so it is parsed,
 * ignored and counted.
 *
 * Stereo begins at the tremolo (CC91): off, and at the mono variant, both
 * channels carry the same sample.
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/epiano.h"
#include "host_parse.h"
#include "live_io.h"
#include "midi_map.h"

static ep_piano piano;
static volatile sig_atomic_t stop_flag;

static unsigned long xruns;
static ep_midi_map midi_map;

static void on_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

static void apply_msg(const tw_midi_msg *m) {
    ep_midi_apply(&midi_map, &piano, m->status, m->d1, m->d2);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [-d pcm] [-m rawmidi] [-r rate] [-p period]"
            " [-n periods] [-g gain] [-e demo_secs]\n"
            "  -d  ALSA PCM out (default \"default\"; the rig: hw:CARD=AG06AG03)\n"
            "  -m  ALSA rawmidi in, e.g. hw:2,0,0 (none = play the demo or idle)\n"
            "  -r  sample rate 44100..192000 (default 48000)\n"
            "  -p  period frames (default 128)   -n  periods (default 3)\n"
            "  -g  master gain (default 0.0625)  -e  demo chord for N seconds\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *pcm_dev = "default", *midi_dev = nullptr;
    unsigned rate = 48000, nperiods = 3;
    snd_pcm_uframes_t period = 128;
    float gain = 0.0625f;
    uint64_t demo_secs = 0;

    int c;
    while ((c = getopt(argc, argv, "d:m:r:p:n:g:e:h")) != -1) {
        switch (c) {
        case 'd':
            if (!*optarg) { usage(argv[0]); return 2; }
            pcm_dev = optarg;
            break;
        case 'm':
            if (!*optarg) { usage(argv[0]); return 2; }
            midi_dev = optarg;
            break;
        case 'r': {
            uint64_t value = 0;
            if (!host_parse_u64(optarg, 44100, 192000, &value)) {
                usage(argv[0]);
                return 2;
            }
            rate = (unsigned)value;
            break;
        }
        case 'p': {
            uint64_t value = 0;
            if (!host_parse_u64(optarg, 1, LIVE_PERIOD_MAX, &value)) {
                usage(argv[0]);
                return 2;
            }
            period = (snd_pcm_uframes_t)value;
            break;
        }
        case 'n': {
            uint64_t value = 0;
            if (!host_parse_u64(optarg, 2, 64, &value)) {
                usage(argv[0]);
                return 2;
            }
            nperiods = (unsigned)value;
            break;
        }
        case 'g': {
            double value = 0.0;
            if (!host_parse_double(optarg, 0.0, 16.0, &value)) {
                usage(argv[0]);
                return 2;
            }
            gain = (float)value;
            break;
        }
        case 'e':
            if (!host_parse_u64(optarg, 0, 86400, &demo_secs)) {
                usage(argv[0]);
                return 2;
            }
            break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind != argc) { usage(argv[0]); return 2; }

    live_pcm audio = { 0 };
    if (live_pcm_open(&audio, pcm_dev, rate, period, nperiods) < 0) return 1;
    period = audio.period;

    snd_rawmidi_t *midi = nullptr;
    if (midi_dev) {
        int err = snd_rawmidi_open(&midi, nullptr, midi_dev, SND_RAWMIDI_NONBLOCK);
        if (err < 0) {
            fprintf(stderr, "rawmidi open %s: %s\n", midi_dev, snd_strerror(err));
            live_pcm_close(&audio, false);
            return 1;
        }
        printf("midi: %s (notes 28..100, velocity -> loudness and timbre,"
               " CC64 sustain, CC120/123 panic, CC85 drive, CC91 tremolo;"
               " CC92 cabinet, CC93 condition)\n",
               midi_dev);
    }

    ep_piano_init(&piano, (float)rate);
    ep_midi_map_init(&midi_map, false);
    if (signal(SIGINT, on_signal) == SIG_ERR || signal(SIGTERM, on_signal) == SIG_ERR) {
        fprintf(stderr, "signal handler setup failed\n");
        if (midi) snd_rawmidi_close(midi);
        live_pcm_close(&audio, false);
        return 1;
    }

    tw_midi_parser parser = { 0 };
    tw_midi_msg msg = { 0 };
    int64_t frame = 0;
    int64_t demo_end = demo_secs ? (int64_t)(demo_secs * rate) : -1;
    int result = 0;
    /* An E-major-seventh voicing across the middle of the compass, struck
     * at three dynamics so the demo shows the velocity-timbre law and then
     * the dampers landing. */
    static const int DEMO[4] = { 52, 59, 63, 68 };
    printf("running%s -- ctrl-c to stop\n", demo_secs ? " (demo chord)" : "");

    while (!stop_flag) {
        if (midi) {
            unsigned char buf[256];
            for (;;) {
                ssize_t count = snd_rawmidi_read(midi, buf, sizeof buf);
                if (count > 0) {
                    for (ssize_t i = 0; i < count; i++)
                    if (tw_midi_parse(&parser, buf[i], &msg)) apply_msg(&msg);
                    continue;
                }
                if (!count || count == -EAGAIN) break;
                if (count == -EINTR) {
                    if (stop_flag) break;
                    continue;
                }
                fprintf(stderr, "rawmidi read: %s\n", snd_strerror((int)count));
                result = 1;
                stop_flag = 1;
                break;
            }
        }
        if (stop_flag) break;
        if (demo_end > 0) {
            int64_t on = rate / 2, off = demo_end - rate / 2;
            if (frame <= on && on < frame + (int64_t)period)
                for (int i = 0; i < 4; i++)
                    ep_piano_note(&piano, DEMO[i], true, 40 + 28 * i);
            if (frame <= off && off < frame + (int64_t)period)
                for (int i = 0; i < 4; i++)
                    ep_piano_note(&piano, DEMO[i], false, 0);
            if (frame >= demo_end) break;
        }
        for (snd_pcm_uframes_t i = 0; i < period; i++) {
            tw_stereo y = ep_piano_tick_stereo(&piano);
            audio.stereo[2 * i] = y.l * gain;
            audio.stereo[2 * i + 1] = y.r * gain;
        }
        if (live_pcm_write(&audio, &xruns) < 0) {
            result = 1;
            stop_flag = 1;
        }
        frame += (int64_t)period;
    }

    if (midi) snd_rawmidi_close(midi);
    live_pcm_close(&audio, result == 0);
    printf("\nstopped: %.1f s rendered, %lu note events, %lu key pressures,"
           " %lu ccs, %lu xruns, %u out-of-compass\n",
           (double)frame / rate, midi_map.stats.notes, midi_map.stats.pressures,
           midi_map.stats.ccs, xruns, piano.bank.out_of_compass);
    return result;
}
