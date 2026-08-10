/* tw91 — the live driver: ALSA rawmidi in -> organ -> ALSA PCM out.
 * One thread, synchronous; snd_pcm_writei is the loop's clock.
 * The one permitted dependency: libasound (see docs/design.md).
 *
 * MIDI map: notes 36..96; poly key pressure (0xA0) is per-note key depth,
 * 0..127 over the travel; CC11 swell; CC70..78 drawbars 1..9 (M2, value ->
 * digit 0..8); CC120/CC123 panic (M2). Percussion (M3), value >= 64 is the
 * "on" position of each toggle: CC80 on/off, CC81 2nd/3rd harmonic, CC82
 * fast/slow decay, CC83 soft/normal volume. Vibrato/chorus (M4): CC84,
 * value/19 -> off, V1, V2, V3, C1, C2, C3. Drive (M5): CC85, value/127 ->
 * drive 0..1. Rotary (M6): CC86 mode, value/32 -> bypass, chorale,
 * tremolo, brake; CC87 the live speed switch, >= 64 tremolo else chorale;
 * CC88 balance, CC89 width, CC90 rotary drive, each value/127. Velocity
 * -> contact stagger.
 *
 * -2 selects the two-manual touch-surface protocol: notes and key depth
 * on channels 1 and 2 (upper and lower manual) both land on the one manual
 * the engine has today, CCs are honored on channel 1 only (the console
 * channel),
 * and everything else — including channel-2 drawbars, which carry a
 * lower registration the engine cannot represent yet — is ignored.
 * Without -2 the driver stays channel-agnostic, byte-for-byte as before.
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/tonewheel.h"
#include "host_parse.h"
#include "live_io.h"
#include "midi_map.h"

static volatile sig_atomic_t stop_flag;
static void on_signal(int sig) { (void)sig; stop_flag = 1; }

static tw_instrument inst; /* ~100 KB of fixed state; static by design */

static unsigned long xruns;
static organ_midi_map midi_map;

/* -2: the two-manual touch-surface protocol (channel-aware); see the
 * header comment. Off by default: channel-agnostic, as always. */
static bool two_manual;

/* In -2 mode, decide whether a channel message participates at all:
 * notes on channels 1..2 (0-based 0..1) merge onto the one manual, CCs
 * count only from channel 1 (the console channel), the rest is dropped.
 * Key depth is a manual gesture, not a console control, so it rides the
 * note gate and not the CC one. */
static bool accept_msg(const tw_midi_msg *m) {
    if (!two_manual) return true;
    int ch = m->status & 0x0F;
    switch (m->status & 0xF0) {
    case 0x90:
    case 0x80:
    case 0xA0: return ch <= 1;
    case 0xB0: return ch == 0;
    default:   return false;
    }
}

static void apply_msg(const tw_midi_msg *m) {
    if (!accept_msg(m)) return;
    organ_midi_apply(&midi_map, &inst, m->status, m->d1, m->d2);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [-d pcm] [-m rawmidi] [-r rate] [-p period]"
            " [-n periods] [-g gain] [-e demo_secs] [-2]\n"
            "  -d  ALSA PCM out (default \"default\"; the rig: hw:CARD=AG06AG03)\n"
            "  -m  ALSA rawmidi in, e.g. hw:2,0,0 (none = play the demo or idle)\n"
            "  -r  sample rate 44100..192000 (default 48000)\n"
            "  -p  period frames (default 128)   -n  periods (default 3)\n"
            "  -g  master gain (default 0.0625)  -e  demo chord for N seconds\n"
            "  -2  two-manual protocol: notes+depth ch1+ch2 merge, CCs ch1 only\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *pcm_dev = "default", *midi_dev = nullptr;
    unsigned rate = 48000, nperiods = 3;
    snd_pcm_uframes_t period = 128;
    float gain = 0.0625f;
    uint64_t demo_secs = 0;

    int c;
    while ((c = getopt(argc, argv, "d:m:r:p:n:g:e:2h")) != -1) {
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
        case '2': two_manual = true; break;
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
        printf("midi: %s (notes 36..96, poly key pressure = key depth,"
               " CC11 swell, CC70-78 drawbars,"
               " CC120/123 panic, CC80-83 percussion on/harmonic/speed/volume,"
               " CC84 vibrato off/V1-V3/C1-C3, CC85 drive, CC86 rotary mode,"
               " CC87 speed switch, CC88-90 balance/width/rotary drive)%s\n",
               midi_dev,
               two_manual ? "; two-manual: notes+depth ch1+ch2, CCs ch1" : "");
    }

    tw_instrument_init(&inst, (float)rate);
    organ_midi_map_init(&midi_map, false, false);
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
            if (frame <= on && on < frame + (int64_t)period) {
                tw_organ_note(&inst.organ, 60, true, 100);
                tw_organ_note(&inst.organ, 64, true, 100);
                tw_organ_note(&inst.organ, 67, true, 100);
            }
            if (frame <= off && off < frame + (int64_t)period) {
                tw_organ_note(&inst.organ, 60, false, 0);
                tw_organ_note(&inst.organ, 64, false, 0);
                tw_organ_note(&inst.organ, 67, false, 0);
            }
            if (frame >= demo_end) break;
        }
        for (snd_pcm_uframes_t i = 0; i < period; i++) {
            tw_stereo s = tw_instrument_tick_stereo(&inst);
            audio.stereo[2 * i] = s.l * gain;
            audio.stereo[2 * i + 1] = s.r * gain;
        }
        if (live_pcm_write(&audio, &xruns) < 0) {
            result = 1;
            stop_flag = 1;
        }
        frame += (int64_t)period;
    }

    if (midi) snd_rawmidi_close(midi);
    live_pcm_close(&audio, result == 0);
    printf("\nstopped: %.1f s rendered, %lu note events, %lu depth events,"
           " %lu ccs, %lu xruns, %u out-of-compass\n", (double)frame / rate,
           midi_map.stats.notes, midi_map.stats.depths, midi_map.stats.ccs, xruns,
           inst.organ.out_of_compass);
    return result;
}
