/* ep73 live driver: ALSA rawmidi in, ALSA PCM out.
 * One thread, synchronous; snd_pcm_writei is the loop's clock — the same
 * shape as the organ's driver, deliberately not merged with it.
 *
 * MIDI (docs/ep-constants.md sec 10.1): notes 28..100, velocity scaling
 * loudness and timbre; CC64 sustain pedal, >= 64 is down; CC120/CC123
 * drop all dampers. CC85 drive and CC91 tremolo; CC92 cabinet, CC93 condition and are counted but not yet wired. Poly key pressure has no meaning on this instrument — a key that
 * is down has already thrown its hammer — so it is parsed, ignored and
 * counted.
 *
 * Stereo begins at the tremolo (CC91): off, and at the mono variant, both
 * channels carry the same sample.
 */
#define _DEFAULT_SOURCE 1
#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/epiano.h"

#define MAX_PERIOD 4096

static ep_piano piano;
static volatile sig_atomic_t stop_flag;

static struct {
    unsigned long notes, pressures, ccs, reserved, xruns;
} stats;

static void on_signal(int sig) {
    (void)sig;
    stop_flag = 1;
}

static void apply_msg(const tw_midi_msg *m) {
    switch (m->status & 0xF0) {
    case 0x90:
        stats.notes++;
        ep_piano_note(&piano, m->d1, true, m->d2);
        break;
    case 0x80:
        stats.notes++;
        ep_piano_note(&piano, m->d1, false, 0);
        break;
    case 0xA0:
        stats.pressures++;
        ep_piano_key_pressure(&piano, m->d1, m->d2);
        break;
    case 0xB0:
        stats.ccs++;
        if (m->d1 == 64) ep_piano_set_sustain(&piano, m->d2 >= 64);
        else if (m->d1 == 120 || m->d1 == 123) ep_piano_panic(&piano);
        else if (m->d1 == 91) ep_piano_set_tremolo(&piano, m->d2);
        else if (m->d1 == 85) ep_piano_set_drive(&piano, (float)m->d2 / 127.0f);
        else if (m->d1 == 92) ep_piano_set_cabinet(&piano, (float)m->d2 / 127.0f);
        else if (m->d1 == 93) ep_piano_set_condition(&piano, (float)m->d2 / 127.0f);
        else stats.ccs--;
        break;
    default:
        break;
    }
}

static int open_pcm(snd_pcm_t **pcm, const char *dev, unsigned rate,
                    snd_pcm_uframes_t *period, unsigned nperiods) {
    int err = snd_pcm_open(pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "pcm open %s: %s\n", dev, snd_strerror(err));
        if (err == -EBUSY)
            fprintf(stderr, "hint: another client holds the device"
                            " (PipeWire?); release the card's node first\n");
        return err;
    }
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(*pcm, hw);
    if ((err = snd_pcm_hw_params_set_access(*pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0
        || (err = snd_pcm_hw_params_set_format(*pcm, hw, SND_PCM_FORMAT_S32_LE)) < 0
        || (err = snd_pcm_hw_params_set_channels(*pcm, hw, 2)) < 0
        || (err = snd_pcm_hw_params_set_rate(*pcm, hw, rate, 0)) < 0) {
        fprintf(stderr, "pcm hw params (S32_LE/2ch/%u): %s\n", rate, snd_strerror(err));
        return err;
    }
    int dir = 0;
    if ((err = snd_pcm_hw_params_set_period_size_near(*pcm, hw, period, &dir)) < 0)
        return err;
    snd_pcm_uframes_t buffer = *period * nperiods;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(*pcm, hw, &buffer)) < 0)
        return err;
    if ((err = snd_pcm_hw_params(*pcm, hw)) < 0) {
        fprintf(stderr, "pcm hw commit: %s\n", snd_strerror(err));
        return err;
    }
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(*pcm, sw);
    snd_pcm_sw_params_set_start_threshold(*pcm, sw, buffer);
    snd_pcm_sw_params_set_avail_min(*pcm, sw, *period);
    if ((err = snd_pcm_sw_params(*pcm, sw)) < 0) return err;
    printf("pcm: %s, S32_LE stereo %u Hz, period %lu, buffer %lu (%.1f ms)\n",
           dev, rate, (unsigned long)*period, (unsigned long)buffer,
           1000.0 * (double)buffer / rate);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [-d pcm] [-m rawmidi] [-r rate] [-p period]"
            " [-n periods] [-g gain] [-e demo_secs]\n"
            "  -d  ALSA PCM out (default \"default\"; the rig: hw:CARD=AG06AG03)\n"
            "  -m  ALSA rawmidi in, e.g. hw:2,0,0 (none = play the demo or idle)\n"
            "  -r  sample rate (default 48000)\n"
            "  -p  period frames (default 128)   -n  periods (default 3)\n"
            "  -g  master gain (default 0.0625)  -e  demo chord for N seconds\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *pcm_dev = "default", *midi_dev = nullptr;
    unsigned rate = 48000, nperiods = 3;
    snd_pcm_uframes_t period = 128;
    float gain = 0.0625f;
    long demo_secs = 0;

    int c;
    while ((c = getopt(argc, argv, "d:m:r:p:n:g:e:h")) != -1) {
        switch (c) {
        case 'd': pcm_dev = optarg; break;
        case 'm': midi_dev = optarg; break;
        case 'r': rate = (unsigned)atoi(optarg); break;
        case 'p': period = (snd_pcm_uframes_t)atol(optarg); break;
        case 'n': nperiods = (unsigned)atoi(optarg); break;
        case 'g': gain = (float)atof(optarg); break;
        case 'e': demo_secs = atol(optarg); break;
        default: usage(argv[0]); return 2;
        }
    }
    if (period > MAX_PERIOD) period = MAX_PERIOD;

    snd_pcm_t *pcm = nullptr;
    if (open_pcm(&pcm, pcm_dev, rate, &period, nperiods) < 0) return 1;

    snd_rawmidi_t *midi = nullptr;
    if (midi_dev) {
        int err = snd_rawmidi_open(&midi, nullptr, midi_dev, SND_RAWMIDI_NONBLOCK);
        if (err < 0) {
            fprintf(stderr, "rawmidi open %s: %s\n", midi_dev, snd_strerror(err));
            return 1;
        }
        printf("midi: %s (notes 28..100, velocity -> loudness and timbre,"
               " CC64 sustain, CC120/123 panic, CC85 drive, CC91 tremolo;"
               " CC92 cabinet, CC93 condition)\n",
               midi_dev);
    }

    ep_piano_init(&piano, (float)rate);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    static float stereo[2 * MAX_PERIOD];
    static int32_t out[2 * MAX_PERIOD];
    tw_midi_parser parser = { 0 };
    tw_midi_msg msg;
    int64_t frame = 0;
    int64_t demo_end = demo_secs > 0 ? demo_secs * (int64_t)rate : -1;
    /* An E-major-seventh voicing across the middle of the compass, struck
     * at three dynamics so the demo shows the velocity-timbre law and then
     * the dampers landing. */
    static const int DEMO[4] = { 52, 59, 63, 68 };
    printf("running%s -- ctrl-c to stop\n", demo_secs ? " (demo chord)" : "");

    while (!stop_flag) {
        if (midi) {
            unsigned char buf[256];
            ssize_t r;
            while ((r = snd_rawmidi_read(midi, buf, sizeof buf)) > 0)
                for (ssize_t i = 0; i < r; i++)
                    if (tw_midi_parse(&parser, buf[i], &msg)) apply_msg(&msg);
        }
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
            float l = y.l * gain, r = y.r * gain;
            stereo[2 * i] = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
            stereo[2 * i + 1] = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
        }
        for (snd_pcm_uframes_t i = 0; i < 2 * period; i++)
            out[i] = (int32_t)(stereo[i] * 2147483392.0f);
        snd_pcm_uframes_t done = 0;
        while (done < period) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, out + 2 * done, period - done);
            if (w < 0) {
                stats.xruns++;
                w = snd_pcm_recover(pcm, (int)w, 1);
                if (w < 0) {
                    fprintf(stderr, "pcm write: %s\n", snd_strerror((int)w));
                    stop_flag = 1;
                    break;
                }
                continue;
            }
            done += (snd_pcm_uframes_t)w;
        }
        frame += (int64_t)period;
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    if (midi) snd_rawmidi_close(midi);
    printf("\nstopped: %.1f s rendered, %lu note events, %lu key pressures,"
           " %lu ccs (%lu reserved), %lu xruns, %u out-of-compass\n",
           (double)frame / rate, stats.notes, stats.pressures, stats.ccs,
           stats.reserved, stats.xruns, piano.bank.out_of_compass);
    return 0;
}
