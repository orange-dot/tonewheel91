/* Workbench UI application state shared by the main loop and the views. */
#ifndef WB_APPSTATE_H
#define WB_APPSTATE_H

#include <stdbool.h>
#include <stddef.h>

#include "../../driver/ma_patch_file.h"
#include "../desk/wb_analysis.h"
#include "../desk/wb_cache.h"
#include "../desk/wb_render.h"
#include "../desk/wb_take.h"
#include "../desk/wb_patch.h"
#include "../desk/wb_engine.h"
#include "../desk/wb_format.h"
#include "../desk/wb_import.h"
#include "../desk/wb_orch.h"
#include "../desk/wb_session.h"
#include "../desk/wb_smf.h"
#include "../desk/wb_undo.h"
#include "../audio/wb_live.h"
#include "wb_jobs.h"
#include "wb_ui.h"

enum { CONSOLE_KEEP = 96, CONSOLE_LINE = 200, INSPECTOR_ROWS = 96, INSPECTOR_TEXT = 256,
       LEDGER_ROWS = 256, LISTEN_COLUMNS = 4096 };

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct nk_context *ctx;
    SDL_WindowID id;
} ui_window;

/* Statistics of one source track for the Sources view (plan §5.1). */
typedef struct {
    unsigned notes, low, high, velocity_min, velocity_median, velocity_max, held_peak;
    double duration_median_bars, duration_max_bars, first_bar, last_bar;
    unsigned long_notes, controllers;
    int program, channel;
    unsigned pitch_class[12];
    unsigned *density;             /* notes per bar */
} track_stats;

typedef struct {
    bool loaded;
    char error[160];
    wb_smf smf;
    unsigned ticks_per_bar, bars;
    track_stats track[WB_SMF_TRACKS];
} source_view;

typedef struct {
    char id[64], kind[16], label[64], fnv[24], listening[72], wall[16], verdict[160];
} ledger_row;

typedef struct app {
    ui_window main, second;
    bool has_second;

    /* Session and journal; every edit goes through the journal and saves. */
    wb_session *session;
    char path[1024], dir[1024];
    wb_journal journal;
    unsigned version;              /* bumps on every change */
    wb_derived *derived;
    bool derived_ok;
    char derived_error[WB_ERROR_MAX];
    source_view source[WB_SOURCES];

    unsigned preset;
    wb_view active[WB_PRESET_COUNT];
    bool console_open, show_keys, quit;
    wb_theme_kind theme;
    char font_path[512];

    /* Selection: the part column and the inspector's object. */
    int part;
    wb_kind inspect_kind;
    int inspect_index;
    int source_index, track_index, groove_index;

    /* Inspector rows (key, value) for the inspected object. */
    unsigned inspect_version;
    wb_kind inspected_kind;
    int inspected_index;
    unsigned inspect_rows;
    char inspect_key[INSPECTOR_ROWS][48];
    char inspect_value[INSPECTOR_ROWS][INSPECTOR_TEXT];
    char inspect_new_key[48], inspect_new_value[INSPECTOR_TEXT];

    char console[CONSOLE_KEEP][CONSOLE_LINE];
    bool console_error[CONSOLE_KEEP];
    unsigned console_count;

    wb_jobs jobs;
    wb_proc_status proc;

    /* Roll. */
    float roll_from_bar, roll_bars;
    bool roll_selected;
    int roll_part;
    double roll_bar;
    unsigned roll_note;

    /* Import. */
    char import_path[1024], import_id[32];
    int import_split, import_drums;
    bool import_keep_silence;
    wb_import_plan *plan;
    bool plan_ok;
    char plan_error[WB_ERROR_MAX];

    /* Probe and render. */
    char probe_from[16], probe_to[16], probe_solo[256], probe_preroll[8];
    char render_label[48];

    /* Ledger. */
    unsigned ledger_count;
    ledger_row ledger[LEDGER_ROWS];
    int ledger_selected, ledger_compare;
    char verdict[160];
    bool ledger_stale;

    /* Patch editor. */
    int patch_part;
    unsigned patch_version;
    bool patch_ok;
    char patch_error[WB_ERROR_MAX];
    ma_patch_document patch, patch_b;
    bool patch_b_valid;
    wb_twreg twreg;
    wb_epset epset;
    bool patch_chain;              /* the part's patch is a .wbpatch */
    wb_patch_overrides overrides;
    ma_patch patch_parent;

    /* Listen. */
    char listen_run[64];
    float listen_min[LISTEN_COLUMNS], listen_max[LISTEN_COLUMNS], listen_rms[LISTEN_COLUMNS];
    unsigned listen_columns;
    double listen_seconds;
    wb_measure listen_measure;

    /* Live audio (W7, W11). */
    wb_live *live;
    bool audio_on;
    char audio_device[64];
    int velocity, octave;          /* computer-keyboard velocity, lower row base octave */
    int rack_part[WB_LIVE_SLOTS];  /* -1: empty */
    int rack_zone[WB_LIVE_SLOTS];  /* 0 both rows, 1 lower, 2 upper */
    uint8_t key_note[512], key_slots[512];   /* sounding notes per scancode, slot mask */
    float *take[2];
    uint64_t take_frames[2];
    char take_run[2][64];
    double take_gain[2];
    unsigned take_selected;
    wb_live_meters meters;
    unsigned audio_generation;     /* bumps when a loaded instrument's file changes */

    /* Arrangement playback (W8): frozen parts from the cache, live parts on
     * the rack driven by the derived score from the playhead. */
    bool part_live[WB_PARTS];
    wb_score arrangement;
    float *preview;
    uint64_t preview_frames;
    char play_from[16];
    bool cache_state_valid;
    unsigned cache_version;
    bool part_cached[WB_PARTS];

    /* Take recording (W12): input events, not audio. */
    bool recording, take_pending;
    uint64_t record_start_us;
    wb_take_event *take_events;
    size_t take_count, take_capacity;
} app;

/* Implemented in wb_app.c, used by the views. */
[[gnu::format(printf, 3, 4)]]
void app_say(app *a, bool error, const char *format, ...);
const wb_theme *app_theme(const app *a);
/* Replace one object with new block text through the journal; saves and
 * rebuilds.  insert / remove variants for list editing. */
bool app_replace(app *a, wb_kind kind, unsigned index, const char *block);
bool app_insert(app *a, wb_kind kind, unsigned index, const char *block);
bool app_remove(app *a, wb_kind kind, unsigned index);
bool app_set_key(app *a, wb_kind kind, unsigned index, const char *key, const char *value);
void app_inspect(app *a, wb_kind kind, int index);
void app_job(app *a, const char *label, const char *const *args);
void app_reload_ledger(app *a);
bool app_load_session(app *a, const char *path);

/* Live audio (wb_app.c). */
void app_audio_toggle(app *a);
void app_audio_reload(app *a);
void app_audio_send(app *a, wb_live_cmd cmd);
bool app_load_take(app *a, unsigned index, const char *run);
void app_play_arrangement(app *a);
void app_refresh_cache_state(app *a);
void app_record_toggle(app *a);
void app_take_keep(app *a, bool keep);

/* Views (wb_views.c). */
void view_draw(app *a, struct nk_context *ctx, wb_view view, float width, float height);
void view_inspector(app *a, struct nk_context *ctx);

#endif
