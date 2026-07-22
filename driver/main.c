/* tw91 — the live driver: ALSA rawmidi in -> organ -> ALSA PCM out.
 * One thread, synchronous; snd_pcm_writei is the loop's clock.
 * The one permitted dependency: libasound (see docs/design.md).
 *
 * MIDI map: notes 36..96; CC11 swell; CC70..78 drawbars 1..9 (M2, value ->
 * digit 0..8); CC120/CC123 panic (M2). Percussion (M3), value >= 64 is the
 * "on" position of each toggle: CC80 on/off, CC81 2nd/3rd harmonic, CC82
 * fast/slow decay, CC83 soft/normal volume. Vibrato/chorus (M4): CC84,
 * value/19 -> off, V1, V2, V3, C1, C2, C3. Drive (M5): CC85, value/127 ->
 * drive 0..1. Rotary (M6): CC86 mode, value/32 -> bypass, chorale,
 * tremolo, brake; CC87 the live speed switch, >= 64 tremolo else chorale;
 * CC88 balance, CC89 width, CC90 rotary drive, each value/127. Velocity
 * -> contact stagger.
 *
 * -2 selects the two-manual touch-surface protocol: notes on channels 1
 * and 2 (upper and lower manual) both land on the one manual the engine
 * has today, CCs are honored on channel 1 only (the console channel),
 * and everything else — including channel-2 drawbars, which carry a
 * lower registration the engine cannot represent yet — is ignored.
 * Without -2 the driver stays channel-agnostic, byte-for-byte as before.
 */
#define _DEFAULT_SOURCE 1
#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/tonewheel.h"

#define MAX_PERIOD 4096

static volatile sig_atomic_t stop_flag;
static void on_signal(int sig) { (void)sig; stop_flag = 1; }

static tw_instrument inst; /* ~100 KB of fixed state; static by design */

static struct {
    unsigned long notes, ccs, xruns;
} stats;

/* tw_organ_set_percussion takes all four tablets at once; CCs toggle one
 * at a time, so the driver keeps its own shadow of the other three.
 * Defaults mirror tw_organ_init's: off, 2nd, fast, NORMAL. */
static struct { bool on, third, slow, normal; } perc_state = { false, false, false, true };

/* -2: the two-manual touch-surface protocol (channel-aware); see the
 * header comment. Off by default: channel-agnostic, as always. */
static bool two_manual;

/* In -2 mode, decide whether a channel message participates at all:
 * notes on channels 1..2 (0-based 0..1) merge onto the one manual, CCs
 * count only from channel 1 (the console channel), the rest is dropped. */
static bool accept_msg(const tw_midi_msg *m) {
    if (!two_manual) return true;
    int ch = m->status & 0x0F;
    switch (m->status & 0xF0) {
    case 0x90:
    case 0x80: return ch <= 1;
    case 0xB0: return ch == 0;
    default:   return false;
    }
}

static void apply_percussion_cc(void) {
    tw_organ_set_percussion(&inst.organ, perc_state.on, perc_state.third,
                            perc_state.slow, perc_state.normal);
}

static void apply_msg(const tw_midi_msg *m) {
    if (!accept_msg(m)) return;
    switch (m->status & 0xF0) {
    case 0x90:
        tw_organ_note(&inst.organ, m->d1, true, m->d2);
        stats.notes++;
        break;
    case 0x80:
        tw_organ_note(&inst.organ, m->d1, false, m->d2);
        stats.notes++;
        break;
    case 0xB0:
        stats.ccs++;
        if (m->d1 == 11) tw_organ_set_swell(&inst.organ, (float)m->d2 / 127.0f);
        else if (m->d1 >= 70 && m->d1 <= 78)
            tw_organ_set_drawbar(&inst.organ, m->d1 - 70, (m->d2 * 8 + 63) / 127);
        else if (m->d1 == 120 || m->d1 == 123) tw_organ_panic(&inst.organ);
        else if (m->d1 == 80) { perc_state.on = m->d2 >= 64; apply_percussion_cc(); }
        else if (m->d1 == 81) { perc_state.third = m->d2 >= 64; apply_percussion_cc(); }
        else if (m->d1 == 82) { perc_state.slow = m->d2 >= 64; apply_percussion_cc(); }
        else if (m->d1 == 83) { perc_state.normal = m->d2 >= 64; apply_percussion_cc(); }
        else if (m->d1 == 84) tw_organ_set_vibrato(&inst.organ, m->d2 / 19);
        else if (m->d1 == 85) tw_instrument_set_drive(&inst, (float)m->d2 / 127.0f);
        else if (m->d1 == 86) tw_rotary_set_mode(&inst.rotary, m->d2 / 32);
        else if (m->d1 == 87)
            tw_rotary_set_mode(&inst.rotary,
                               m->d2 >= 64 ? TW_ROT_TREMOLO : TW_ROT_CHORALE);
        else if (m->d1 == 88) tw_rotary_set_balance(&inst.rotary, (float)m->d2 / 127.0f);
        else if (m->d1 == 89) tw_rotary_set_width(&inst.rotary, (float)m->d2 / 127.0f);
        else if (m->d1 == 90) tw_rotary_set_drive(&inst.rotary, (float)m->d2 / 127.0f);
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
            " [-n periods] [-g gain] [-e demo_secs] [-2]\n"
            "  -d  ALSA PCM out (default \"default\"; the rig: hw:CARD=AG06AG03)\n"
            "  -m  ALSA rawmidi in, e.g. hw:2,0,0 (none = play the demo or idle)\n"
            "  -r  sample rate (default 48000)\n"
            "  -p  period frames (default 128)   -n  periods (default 3)\n"
            "  -g  master gain (default 0.0625)  -e  demo chord for N seconds\n"
            "  -2  two-manual protocol: notes ch1+ch2 merge, CCs ch1 only\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *pcm_dev = "default", *midi_dev = nullptr;
    unsigned rate = 48000, nperiods = 3;
    snd_pcm_uframes_t period = 128;
    float gain = 0.0625f;
    long demo_secs = 0;

    int c;
    while ((c = getopt(argc, argv, "d:m:r:p:n:g:e:2h")) != -1) {
        switch (c) {
        case 'd': pcm_dev = optarg; break;
        case 'm': midi_dev = optarg; break;
        case 'r': rate = (unsigned)atoi(optarg); break;
        case 'p': period = (snd_pcm_uframes_t)atol(optarg); break;
        case 'n': nperiods = (unsigned)atoi(optarg); break;
        case 'g': gain = (float)atof(optarg); break;
        case 'e': demo_secs = atol(optarg); break;
        case '2': two_manual = true; break;
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
        printf("midi: %s (notes 36..96, CC11 swell, CC70-78 drawbars,"
               " CC120/123 panic, CC80-83 percussion on/harmonic/speed/volume,"
               " CC84 vibrato off/V1-V3/C1-C3, CC85 drive, CC86 rotary mode,"
               " CC87 speed switch, CC88-90 balance/width/rotary drive)%s\n",
               midi_dev,
               two_manual ? "; two-manual: notes ch1+ch2, CCs ch1" : "");
    }

    tw_instrument_init(&inst, (float)rate);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    static float stereo[2 * MAX_PERIOD];
    static int32_t out[2 * MAX_PERIOD];
    tw_midi_parser parser = { 0 };
    tw_midi_msg msg;
    int64_t frame = 0;
    int64_t demo_end = demo_secs > 0 ? demo_secs * (int64_t)rate : -1;
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
            float l = s.l * gain, r = s.r * gain;
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
    printf("\nstopped: %.1f s rendered, %lu note events, %lu ccs, %lu xruns,"
           " %u out-of-compass\n", (double)frame / rate, stats.notes, stats.ccs,
           stats.xruns, inst.organ.out_of_compass);
    return 0;
}
