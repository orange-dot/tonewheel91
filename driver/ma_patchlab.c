/* Patchlab: a deliberately small hosted patch librarian, deterministic WAV
 * renderer and live ANSI/termios editor for the freestanding Mamut Analog
 * voice.  ALSA is opened only by the interactive mode. */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

#include "../src/mamutanalog.h"
#include "host_parse.h"
#include "live_io.h"
#include "ma_patch_file.h"
#include "wav.h"

enum {
    PATCH_BANK_MAX = 128,
    PATCH_PATH_MAX = 4096,
    SCREEN_FIELDS = 15,
};

typedef struct {
    ma_patch_document document;
    char path[PATCH_PATH_MAX];
    bool dirty;
} bank_entry;

typedef struct {
    bank_entry entry[PATCH_BANK_MAX];
    size_t count;
    char directory[PATCH_PATH_MAX];
} patch_bank;

typedef struct {
    struct termios saved;
    int saved_flags;
    unsigned escape_state;
    bool active;
} patch_terminal;

typedef struct {
    patch_bank *bank;
    ma_synth synth;
    size_t selected_patch;
    size_t selected_field;
    int active_note;
    bool naming;
    char name[MA_PATCH_NAME_MAX + 1];
    size_t name_length;
    char status[160];
} patchlab;

static volatile sig_atomic_t stop_flag;

static void on_signal(int signal_number) {
    (void)signal_number;
    stop_flag = 1;
}

static bool has_patch_suffix(const char *name) {
    static const char suffix[] = ".mapatch";
    size_t length = strlen(name), suffix_length = sizeof suffix - 1;
    return length > suffix_length
        && !strcmp(name + length - suffix_length, suffix);
}

static int compare_entries(const void *left, const void *right) {
    const bank_entry *a = left, *b = right;
    return strcasecmp(a->document.name, b->document.name);
}

static bool bank_has_name(const patch_bank *bank, const char *name) {
    for (size_t i = 0; i < bank->count; i++)
        if (!strcasecmp(bank->entry[i].document.name, name)) return true;
    return false;
}

static bool bank_append(patch_bank *bank, ma_patch_document document,
                        const char *path) {
    if (bank->count == PATCH_BANK_MAX) return false;
    bank_entry *entry = &bank->entry[bank->count++];
    *entry = (bank_entry){ .document = document };
    if (path) {
        int length = snprintf(entry->path, sizeof entry->path, "%s", path);
        if (length < 0 || (size_t)length >= sizeof entry->path) {
            bank->count--;
            return false;
        }
    }
    return true;
}

static bool bank_add_compiled_fallbacks(patch_bank *bank) {
    static const struct {
        const char *name;
        const ma_patch *patch;
    } builtin[] = {
        { "Tepih", &ma_patch_tepih },
        { "Lead", &ma_patch_lead },
        { "Dubina", &ma_patch_dubina },
    };
    for (size_t i = 0; i < sizeof builtin / sizeof *builtin; i++) {
        if (bank_has_name(bank, builtin[i].name)) continue;
        ma_patch_document document = { .value = *builtin[i].patch };
        snprintf(document.name, sizeof document.name, "%s", builtin[i].name);
        if (!bank_append(bank, document, nullptr)) return false;
    }
    return true;
}

static bool bank_load(patch_bank *bank, const char *directory) {
    *bank = (patch_bank){ 0 };
    int length = snprintf(bank->directory, sizeof bank->directory, "%s",
                          directory);
    if (length < 0 || (size_t)length >= sizeof bank->directory) {
        fprintf(stderr, "patch bank path is too long\n");
        return false;
    }

    DIR *dir = opendir(directory);
    if (!dir && errno != ENOENT) {
        fprintf(stderr, "patch bank %s: %s\n", directory, strerror(errno));
        return false;
    }
    if (dir) {
        struct dirent *item;
        for (;;) {
            errno = 0;
            item = readdir(dir);
            if (!item) {
                if (errno) {
                    fprintf(stderr, "cannot read patch bank: %s\n",
                            strerror(errno));
                    closedir(dir);
                    return false;
                }
                break;
            }
            if (!has_patch_suffix(item->d_name)) continue;
            char path[PATCH_PATH_MAX];
            length = snprintf(path, sizeof path, "%s/%s", directory,
                              item->d_name);
            if (length < 0 || (size_t)length >= sizeof path) {
                fprintf(stderr, "patch path is too long: %s\n", item->d_name);
                closedir(dir);
                return false;
            }
            ma_patch_document document = { 0 };
            ma_patch_error error = { 0 };
            if (!ma_patch_load(path, &document, &error)) {
                fprintf(stderr, "%s:%u: %s\n", path, error.line,
                        error.message);
                closedir(dir);
                return false;
            }
            if (bank_has_name(bank, document.name)) {
                fprintf(stderr, "duplicate patch name: %s\n", document.name);
                closedir(dir);
                return false;
            }
            if (!bank_append(bank, document, path)) {
                fprintf(stderr, "patch bank exceeds %u entries\n",
                        PATCH_BANK_MAX);
                closedir(dir);
                return false;
            }
        }
        if (closedir(dir)) {
            fprintf(stderr, "cannot close patch bank: %s\n", strerror(errno));
            return false;
        }
    }
    if (!bank_add_compiled_fallbacks(bank)) {
        fprintf(stderr, "patch bank exceeds %u entries\n", PATCH_BANK_MAX);
        return false;
    }
    qsort(bank->entry, bank->count, sizeof *bank->entry, compare_entries);
    return true;
}

static bank_entry *bank_find(patch_bank *bank, const char *name) {
    for (size_t i = 0; i < bank->count; i++)
        if (!strcasecmp(bank->entry[i].document.name, name))
            return &bank->entry[i];
    return nullptr;
}

static bool resolve_patch(patch_bank *bank, const char *selector,
                          ma_patch_document *document) {
    bank_entry *entry = bank_find(bank, selector);
    if (entry) {
        *document = entry->document;
        return true;
    }
    ma_patch_error error = { 0 };
    if (ma_patch_load(selector, document, &error)) return true;
    fprintf(stderr, "patch %s: %s", selector, error.message);
    if (error.line) fprintf(stderr, " at line %u", error.line);
    fputc('\n', stderr);
    return false;
}

static void apply_live_controls(ma_synth *synth, const ma_patch *patch) {
    ma_synth_set_vco1(synth, patch->vco1);
    ma_synth_set_vco2(synth, patch->vco2, patch->vco2_level,
                      patch->vco2_interval, patch->vco2_fine_cents);
    ma_synth_set_oscillator_modulation(
        synth, patch->sync_amount, patch->sync_softness,
        patch->crossmod_amount, patch->noise_level);
    ma_synth_set_mozaik(synth, patch->mozaik_mix, patch->mozaik_slope,
                        patch->mozaik_contrast, patch->mozaik_phason,
                        patch->mozaik_drift);
    ma_synth_set_filter(synth, patch->filter_cutoff_hz,
                        patch->filter_resonance, patch->filter_drive,
                        patch->mixer_pressure);
    ma_synth_set_filter_modulation(synth, patch->filter_env_amount,
                                   patch->filter_keytrack);
    ma_synth_set_amp_adsr(synth, patch->amp_adsr);
    ma_synth_set_filter_adsr(synth, patch->filter_adsr);
    for (int macro = 0; macro < MA_MACRO_COUNT; macro++)
        ma_synth_set_macro(synth, (ma_macro_id)macro, patch->macro[macro]);
    ma_synth_set_output(synth, patch->body_drive, patch->width,
                        patch->crossfeed, patch->master_level);
}

static int render_patch(const ma_patch_document *document, const char *path,
                        unsigned rate, float gain) {
    size_t frames = 12u * rate;
    if (frames > SIZE_MAX / (2 * sizeof(float))) return 1;
    float *output = calloc(2 * frames, sizeof *output);
    if (!output) {
        fprintf(stderr, "cannot allocate render buffer\n");
        return 1;
    }
    ma_synth synth;
    ma_synth_init_patch(&synth, (float)rate, &document->value);
    double sum_squares = 0.0;
    float peak = 0.0f;
    for (size_t frame = 0; frame < frames; frame++) {
        if (frame == rate / 4) ma_synth_note_on(&synth, 0, 36, 84);
        if (frame == 2u * rate + 3u * rate / 4)
            ma_synth_note_off(&synth, 0, 36, 0);
        if (frame == 3u * rate + rate / 4)
            ma_synth_note_on(&synth, 0, 48, 104);
        if (frame == 5u * rate) ma_synth_set_mod_wheel(&synth, .65f);
        if (frame == 6u * rate) ma_synth_set_channel_pressure(&synth, .50f);
        if (frame == 6u * rate + rate / 2)
            ma_synth_set_pitch_bend(&synth, 2.0f);
        if (frame == 7u * rate) ma_synth_set_pitch_bend(&synth, 0.0f);
        if (frame == 7u * rate + rate / 2)
            ma_synth_set_poly_pressure(&synth, 0, 48, .70f);
        if (frame == 8u * rate + rate / 4)
            ma_synth_note_off(&synth, 0, 48, 0);
        if (frame == 9u * rate) {
            ma_synth_set_mod_wheel(&synth, 0.0f);
            ma_synth_set_channel_pressure(&synth, 0.0f);
            ma_synth_note_on(&synth, 0, 55, 112);
        }
        if (frame == 10u * rate + rate / 2)
            ma_synth_note_off(&synth, 0, 55, 0);

        ma_frame sample = ma_synth_tick(&synth);
        if (!isfinite(sample.left) || !isfinite(sample.right)) {
            fprintf(stderr, "non-finite sample at frame %zu\n", frame);
            free(output);
            return 1;
        }
        output[2 * frame] = gain * sample.left;
        output[2 * frame + 1] = gain * sample.right;
        float magnitude = fabsf(output[2 * frame]);
        if (magnitude > peak) peak = magnitude;
        sum_squares += (double)output[2 * frame] * output[2 * frame];
    }
    uint64_t hash = tw_fnv1a64(output, 2 * frames * sizeof *output, 0);
    int result = wav_write_f32(path, output, frames, rate, 2);
    printf("render %-16s %s\n"
           "       peak %.6f  rms %.6f  FNV64 %016llx\n",
           document->name, result ? "FAILED" : path, peak,
           sqrt(sum_squares / frames), (unsigned long long)hash);
    free(output);
    return result ? 1 : 0;
}

static bool terminal_enter(patch_terminal *terminal) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "interactive Patchlab requires a terminal\n");
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &terminal->saved)) {
        perror("tcgetattr");
        return false;
    }
    terminal->saved_flags = fcntl(STDIN_FILENO, F_GETFL);
    if (terminal->saved_flags < 0) {
        perror("fcntl");
        return false;
    }
    struct termios raw = terminal->saved;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)
        || fcntl(STDIN_FILENO, F_SETFL, terminal->saved_flags | O_NONBLOCK)) {
        perror("terminal setup");
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->saved);
        return false;
    }
    terminal->active = true;
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
    if (fflush(stdout) == 0) return true;
    (void)fcntl(STDIN_FILENO, F_SETFL, terminal->saved_flags);
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->saved);
    terminal->active = false;
    return false;
}

static void terminal_leave(patch_terminal *terminal) {
    if (!terminal->active) return;
    fputs("\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    (void)fcntl(STDIN_FILENO, F_SETFL, terminal->saved_flags);
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->saved);
    terminal->active = false;
}

static void panic_voice(patchlab *lab) {
    if (lab->synth.note_active)
        ma_synth_note_off(&lab->synth, lab->synth.channel,
                          lab->synth.note, 0);
    lab->active_note = -1;
}

static void select_patch(patchlab *lab, size_t index) {
    panic_voice(lab);
    lab->selected_patch = index % lab->bank->count;
    ma_synth_apply_patch(&lab->synth,
        &lab->bank->entry[lab->selected_patch].document.value);
    snprintf(lab->status, sizeof lab->status, "loaded %s (voice reset)",
             lab->bank->entry[lab->selected_patch].document.name);
}

static void edit_field(patchlab *lab, double direction, bool coarse) {
    bank_entry *entry = &lab->bank->entry[lab->selected_patch];
    ma_patch_field_info info = { 0 };
    double value = 0.0;
    if (!ma_patch_field_info_at(lab->selected_field, &info)
        || !ma_patch_field_get(&entry->document.value,
                               lab->selected_field, &value)) return;
    double step = coarse ? info.coarse_step : info.fine_step;
    double next = value + direction * step;
    if (next < info.minimum) next = info.minimum;
    if (next > info.maximum) next = info.maximum;
    if (info.integer) next = round(next);
    if (!ma_patch_field_set(&entry->document.value, lab->selected_field,
                            next)) return;
    entry->dirty = true;
    apply_live_controls(&lab->synth, &entry->document.value);
    snprintf(lab->status, sizeof lab->status, "%s = %.7g",
             info.name, next);
}

static bool legal_file_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length > MA_PATCH_NAME_MAX) return false;
    for (size_t i = 0; i < length; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_'
            && name[i] != '-') return false;
    return true;
}

static void save_as(patchlab *lab) {
    if (!legal_file_name(lab->name)) {
        snprintf(lab->status, sizeof lab->status,
                 "name must use 1..%u letters, digits, '_' or '-'",
                 MA_PATCH_NAME_MAX);
        return;
    }
    bank_entry *entry = &lab->bank->entry[lab->selected_patch];
    char path[PATCH_PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s.mapatch",
                          lab->bank->directory, lab->name);
    if (length < 0 || (size_t)length >= sizeof path) {
        snprintf(lab->status, sizeof lab->status, "save path is too long");
        return;
    }
    ma_patch_document document = entry->document;
    snprintf(document.name, sizeof document.name, "%s", lab->name);
    ma_patch_error error = { 0 };
    if (!ma_patch_save(path, &document, &error)) {
        snprintf(lab->status, sizeof lab->status, "save failed: %.120s",
                 error.message);
        return;
    }
    entry->document = document;
    snprintf(entry->path, sizeof entry->path, "%s", path);
    entry->dirty = false;
    snprintf(lab->status, sizeof lab->status, "saved %.152s", path);
}

static void save_current(patchlab *lab) {
    bank_entry *entry = &lab->bank->entry[lab->selected_patch];
    if (!entry->path[0]) {
        lab->naming = true;
        lab->name_length = 0;
        lab->name[0] = 0;
        snprintf(lab->status, sizeof lab->status, "enter a save-as name");
        return;
    }
    ma_patch_error error = { 0 };
    if (!ma_patch_save(entry->path, &entry->document, &error)) {
        snprintf(lab->status, sizeof lab->status, "save failed: %.120s",
                 error.message);
        return;
    }
    entry->dirty = false;
    snprintf(lab->status, sizeof lab->status, "saved %s", entry->path);
}

static void reload_current(patchlab *lab) {
    bank_entry *entry = &lab->bank->entry[lab->selected_patch];
    if (!entry->path[0]) {
        snprintf(lab->status, sizeof lab->status,
                 "compiled fallback has no file to reload");
        return;
    }
    ma_patch_document document = { 0 };
    ma_patch_error error = { 0 };
    if (!ma_patch_load(entry->path, &document, &error)) {
        snprintf(lab->status, sizeof lab->status, "reload failed: %.118s",
                 error.message);
        return;
    }
    entry->document = document;
    entry->dirty = false;
    select_patch(lab, lab->selected_patch);
}

static void play_key(patchlab *lab, unsigned char key) {
    static const char keyboard[] = "zsxdcvgbhnjm,";
    const char *position = strchr(keyboard, key);
    if (!position) return;
    int note = 48 + (int)(position - keyboard);
    if (lab->active_note == note) {
        panic_voice(lab);
        return;
    }
    panic_voice(lab);
    ma_synth_note_on(&lab->synth, 0, (uint8_t)note, 108);
    lab->active_note = note;
}

static void handle_name_key(patchlab *lab, unsigned char key) {
    if (key == 27) {
        lab->naming = false;
        snprintf(lab->status, sizeof lab->status, "save-as cancelled");
    } else if (key == '\r' || key == '\n') {
        lab->naming = false;
        save_as(lab);
    } else if (key == 127 || key == 8) {
        if (lab->name_length) lab->name[--lab->name_length] = 0;
    } else if (lab->name_length < MA_PATCH_NAME_MAX
               && (isalnum(key) || key == '_' || key == '-')) {
        lab->name[lab->name_length++] = (char)key;
        lab->name[lab->name_length] = 0;
    }
}

static void handle_key(patchlab *lab, patch_terminal *terminal,
                       unsigned char key) {
    if (lab->naming) {
        handle_name_key(lab, key);
        return;
    }
    if (terminal->escape_state == 1) {
        terminal->escape_state = key == '[' ? 2 : 0;
        return;
    }
    if (terminal->escape_state == 2) {
        terminal->escape_state = 0;
        if (key == 'A') {
            if (lab->selected_field) lab->selected_field--;
        } else if (key == 'B') {
            lab->selected_field = (lab->selected_field + 1)
                                % ma_patch_field_count();
        } else if (key == 'C') edit_field(lab, 1.0, false);
        else if (key == 'D') edit_field(lab, -1.0, false);
        return;
    }
    if (key == 27) terminal->escape_state = 1;
    else if (key == 'q') stop_flag = 1;
    else if (key == ' ') panic_voice(lab);
    else if (key == 'p')
        select_patch(lab, (lab->selected_patch + 1) % lab->bank->count);
    else if (key == 'P')
        select_patch(lab, (lab->selected_patch + lab->bank->count - 1)
                          % lab->bank->count);
    else if (key == '+' || key == '=') edit_field(lab, 1.0, false);
    else if (key == '-' || key == '_') edit_field(lab, -1.0, false);
    else if (key == ']') edit_field(lab, 1.0, true);
    else if (key == '[') edit_field(lab, -1.0, true);
    else if (key == 19) save_current(lab);          /* Ctrl-S */
    else if (key == 14) {                           /* Ctrl-N */
        lab->naming = true;
        lab->name_length = 0;
        lab->name[0] = 0;
    } else if (key == 18) reload_current(lab);      /* Ctrl-R */
    else play_key(lab, key);
}

static void read_terminal(patchlab *lab, patch_terminal *terminal) {
    unsigned char input[128];
    for (;;) {
        ssize_t count = read(STDIN_FILENO, input, sizeof input);
        if (count > 0) {
            for (ssize_t i = 0; i < count; i++)
                handle_key(lab, terminal, input[i]);
            continue;
        }
        if (!count || errno == EAGAIN || errno == EWOULDBLOCK) return;
        if (errno == EINTR) continue;
        snprintf(lab->status, sizeof lab->status, "terminal read: %s",
                 strerror(errno));
        stop_flag = 1;
        return;
    }
}

static void apply_midi(patchlab *lab, const tw_midi_msg *message) {
    uint8_t command = message->status & 0xf0;
    uint8_t channel = message->status & 0x0f;
    if (command == 0x90 && message->d2) {
        ma_synth_note_on(&lab->synth, channel, message->d1, message->d2);
        lab->active_note = message->d1;
    } else if (command == 0x80 || command == 0x90) {
        ma_synth_note_off(&lab->synth, channel, message->d1, message->d2);
        if (lab->active_note == message->d1) lab->active_note = -1;
    } else if (command == 0xa0) {
        ma_synth_set_poly_pressure(&lab->synth, channel, message->d1,
                                   message->d2 / 127.0f);
    } else if (command == 0xb0) {
        if (message->d1 == 1)
            ma_synth_set_mod_wheel(&lab->synth, message->d2 / 127.0f);
        else if (message->d1 >= 16 && message->d1 < 16 + MA_MACRO_COUNT) {
            size_t macro = message->d1 - 16;
            bank_entry *entry = &lab->bank->entry[lab->selected_patch];
            entry->document.value.macro[macro] = message->d2 / 127.0f;
            entry->dirty = true;
            ma_synth_set_macro(&lab->synth, (ma_macro_id)macro,
                               entry->document.value.macro[macro]);
        } else if (message->d1 == 120 || message->d1 == 123)
            panic_voice(lab);
    } else if (command == 0xd0) {
        ma_synth_set_channel_pressure(&lab->synth, message->d1 / 127.0f);
    } else if (command == 0xe0) {
        int bend = message->d1 | ((int)message->d2 << 7);
        float semitones = bend <= 8192
                        ? 2.0f * (bend - 8192) / 8192.0f
                        : 2.0f * (bend - 8192) / 8191.0f;
        ma_synth_set_pitch_bend(&lab->synth, semitones);
    }
}

static int read_midi(patchlab *lab, snd_rawmidi_t *midi,
                     tw_midi_parser *parser) {
    unsigned char input[256];
    tw_midi_msg message = { 0 };
    for (;;) {
        ssize_t count = snd_rawmidi_read(midi, input, sizeof input);
        if (count > 0) {
            for (ssize_t i = 0; i < count; i++)
                if (tw_midi_parse(parser, input[i], &message))
                    apply_midi(lab, &message);
            continue;
        }
        if (!count || count == -EAGAIN) return 0;
        if (count == -EINTR) continue;
        snprintf(lab->status, sizeof lab->status, "MIDI read: %s",
                 snd_strerror((int)count));
        return -1;
    }
}

static void draw_screen(const patchlab *lab, unsigned long xruns) {
    const bank_entry *entry = &lab->bank->entry[lab->selected_patch];
    printf("\033[H\033[1mMamut Analog Patchlab\033[0m  "
           "patch %zu/%zu: \033[1;36m%s%s\033[0m  xruns %lu\033[K\n",
           lab->selected_patch + 1, lab->bank->count, entry->document.name,
           entry->dirty ? " *" : "", xruns);
    printf("p/P patch  arrows field/value  +/- fine  [/] coarse  "
           "Ctrl-S save  Ctrl-N save-as  Ctrl-R reload\033[K\n");
    printf("keyboard zsxdcvgbhnjm, (C3..C4) toggles notes  space panic  "
           "q quit\033[K\n\n");

    size_t field_count = ma_patch_field_count();
    size_t first = lab->selected_field > SCREEN_FIELDS / 2
                 ? lab->selected_field - SCREEN_FIELDS / 2 : 0;
    if (first + SCREEN_FIELDS > field_count)
        first = field_count > SCREEN_FIELDS ? field_count - SCREEN_FIELDS : 0;
    for (size_t row = 0; row < SCREEN_FIELDS; row++) {
        size_t index = first + row;
        if (index >= field_count) {
            printf("\033[K\n");
            continue;
        }
        ma_patch_field_info info = { 0 };
        double value = 0.0;
        bool described = ma_patch_field_info_at(index, &info);
        bool obtained = ma_patch_field_get(&entry->document.value, index,
                                           &value);
        if (!described || !obtained) continue;
        printf("%s %2zu  %-25s %11.5g  [%g .. %g]%s\033[0m\033[K\n",
               index == lab->selected_field ? "\033[7m>" : " ",
               index + 1, info.name, value, info.minimum, info.maximum,
               info.integer ? " int" : "");
    }
    if (lab->naming)
        printf("\n\033[1;33mSave as: %s_\033[0m\033[K", lab->name);
    else
        printf("\n%s\033[K", lab->status);
    printf("\033[J");
    fflush(stdout);
}

static int run_interactive(patch_bank *bank, const char *initial,
                           const char *pcm_device, const char *midi_device,
                           unsigned rate, snd_pcm_uframes_t period,
                           unsigned periods, float gain) {
    patchlab lab = { .bank = bank, .active_note = -1 };
    if (initial) {
        bank_entry *entry = bank_find(lab.bank, initial);
        if (!entry) {
            fprintf(stderr, "initial patch is not in the bank: %s\n", initial);
            return 2;
        }
        lab.selected_patch = (size_t)(entry - lab.bank->entry);
    }
    ma_synth_init_patch(&lab.synth, (float)rate,
        &lab.bank->entry[lab.selected_patch].document.value);
    snprintf(lab.status, sizeof lab.status, "ready");

    live_pcm audio = { 0 };
    if (live_pcm_open(&audio, pcm_device, rate, period, periods) < 0) return 1;
    period = audio.period;
    snd_rawmidi_t *midi = nullptr;
    if (midi_device) {
        int error = snd_rawmidi_open(&midi, nullptr, midi_device,
                                     SND_RAWMIDI_NONBLOCK);
        if (error < 0) {
            fprintf(stderr, "rawmidi open %s: %s\n", midi_device,
                    snd_strerror(error));
            live_pcm_close(&audio, false);
            return 1;
        }
    }
    patch_terminal terminal = { 0 };
    if (!terminal_enter(&terminal)) {
        if (midi) snd_rawmidi_close(midi);
        live_pcm_close(&audio, false);
        return 1;
    }
    if (signal(SIGINT, on_signal) == SIG_ERR
        || signal(SIGTERM, on_signal) == SIG_ERR) {
        snprintf(lab.status, sizeof lab.status, "signal setup failed");
        stop_flag = 1;
    }

    unsigned long xruns = 0;
    uint64_t rendered = 0, next_draw = 0;
    int result = 0;
    tw_midi_parser parser = { 0 };
    draw_screen(&lab, xruns);
    while (!stop_flag) {
        if (midi && read_midi(&lab, midi, &parser) < 0) {
            result = 1;
            break;
        }
        read_terminal(&lab, &terminal);
        if (stop_flag) break;
        for (snd_pcm_uframes_t frame = 0; frame < period; frame++) {
            ma_frame sample = ma_synth_tick(&lab.synth);
            audio.stereo[2 * frame] = gain * sample.left;
            audio.stereo[2 * frame + 1] = gain * sample.right;
        }
        if (live_pcm_write(&audio, &xruns) < 0) {
            result = 1;
            break;
        }
        rendered += period;
        if (rendered >= next_draw) {
            draw_screen(&lab, xruns);
            next_draw = rendered + rate / 20;
        }
    }
    terminal_leave(&terminal);
    if (midi) snd_rawmidi_close(midi);
    live_pcm_close(&audio, result == 0);
    printf("Patchlab stopped after %.1f s, %lu xruns\n",
           (double)rendered / rate, xruns);
    return result;
}

static void usage(const char *program) {
    fprintf(stderr,
        "usage: %s [mode] [options]\n"
        "  --list                         list the patch bank\n"
        "  --dump PATCH                   write canonical patch text\n"
        "  --render PATCH WAV             render deterministic audition\n"
        "  --patch PATCH                  initial interactive patch\n"
        "  -b, --bank DIR                 patch directory"
        " (default patches/mamutanalog)\n"
        "  -d, --device PCM               ALSA PCM (default default)\n"
        "  -m, --midi RAWMIDI             optional ALSA raw-MIDI input\n"
        "  -r, --rate HZ                  44100..192000 (default 48000)\n"
        "  -p, --period FRAMES            default 128\n"
        "  -n, --periods COUNT            default 3\n"
        "  -g, --gain GAIN                0..16 (default 0.5)\n",
        program);
}

int main(int argc, char **argv) {
    enum { MODE_LIVE, MODE_LIST, MODE_DUMP, MODE_RENDER } mode = MODE_LIVE;
    const char *bank_directory = "patches/mamutanalog";
    const char *pcm_device = "default", *midi_device = nullptr;
    const char *selector = nullptr, *initial = nullptr;
    unsigned rate = 48000, periods = 3;
    snd_pcm_uframes_t period = 128;
    float gain = .5f;
    static const struct option options[] = {
        { "list", no_argument, nullptr, 1000 },
        { "dump", required_argument, nullptr, 1001 },
        { "render", required_argument, nullptr, 1002 },
        { "patch", required_argument, nullptr, 1003 },
        { "bank", required_argument, nullptr, 'b' },
        { "device", required_argument, nullptr, 'd' },
        { "midi", required_argument, nullptr, 'm' },
        { "rate", required_argument, nullptr, 'r' },
        { "period", required_argument, nullptr, 'p' },
        { "periods", required_argument, nullptr, 'n' },
        { "gain", required_argument, nullptr, 'g' },
        { "help", no_argument, nullptr, 'h' },
        { nullptr, 0, nullptr, 0 },
    };
    int option;
    while ((option = getopt_long(argc, argv, "b:d:m:r:p:n:g:h", options,
                                  nullptr)) != -1) {
        uint64_t integer = 0;
        double real = 0.0;
        switch (option) {
        case 1000:
            if (mode != MODE_LIVE) { usage(argv[0]); return 2; }
            mode = MODE_LIST;
            break;
        case 1001:
            if (mode != MODE_LIVE) { usage(argv[0]); return 2; }
            mode = MODE_DUMP;
            selector = optarg;
            break;
        case 1002:
            if (mode != MODE_LIVE) { usage(argv[0]); return 2; }
            mode = MODE_RENDER;
            selector = optarg;
            break;
        case 1003: initial = optarg; break;
        case 'b': bank_directory = optarg; break;
        case 'd': pcm_device = optarg; break;
        case 'm': midi_device = optarg; break;
        case 'r':
            if (!host_parse_u64(optarg, 44100, 192000, &integer)) {
                usage(argv[0]); return 2;
            }
            rate = (unsigned)integer;
            break;
        case 'p':
            if (!host_parse_u64(optarg, 1, LIVE_PERIOD_MAX, &integer)) {
                usage(argv[0]); return 2;
            }
            period = (snd_pcm_uframes_t)integer;
            break;
        case 'n':
            if (!host_parse_u64(optarg, 2, 64, &integer)) {
                usage(argv[0]); return 2;
            }
            periods = (unsigned)integer;
            break;
        case 'g':
            if (!host_parse_double(optarg, 0, 16, &real)) {
                usage(argv[0]); return 2;
            }
            gain = (float)real;
            break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }

    int positional = argc - optind;
    if ((mode == MODE_RENDER && positional != 1)
        || (mode != MODE_RENDER && positional != 0)
        || (mode != MODE_LIVE && initial)) {
        usage(argv[0]);
        return 2;
    }
    patch_bank bank;
    if (!bank_load(&bank, bank_directory)) return 1;
    if (mode == MODE_LIST) {
        for (size_t i = 0; i < bank.count; i++)
            printf("%-16s %s\n", bank.entry[i].document.name,
                   bank.entry[i].path[0] ? bank.entry[i].path : "<compiled>");
        return 0;
    }
    if (mode == MODE_DUMP || mode == MODE_RENDER) {
        ma_patch_document document = { 0 };
        if (!resolve_patch(&bank, selector, &document)) return 1;
        if (mode == MODE_DUMP)
            return ma_patch_write(stdout, &document) && fflush(stdout) == 0
                 ? 0 : 1;
        return render_patch(&document, argv[optind], rate, gain);
    }
    return run_interactive(&bank, initial, pcm_device, midi_device, rate,
                           period, periods, gain);
}
