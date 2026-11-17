/* Patchbench application state, shared by the main loop (pb_app.c), the
 * module panels (pb_modules.c) and the note and analysis views
 * (pb_views.c).  docs/patchbench-plan.md §2. */
#ifndef PB_APP_H
#define PB_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "../desk/wb_patch.h"
#include "../ui/wb_nk.h"
#include "../ui/wb_ui.h"
#include "pb_derive.h"
#include "pb_engine.h"
#include "pb_fields.h"
#include "pb_host.h"
#include "pb_probe.h"
#include "pb_state.h"
#include "pb_undo.h"
#include "pb_widgets.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct nk_context *ctx;
    SDL_WindowID id;
} pb_window;

enum { PB_LIBRARY_MAX = 512 };

typedef struct {
    char path[PB_PATH_MAX];
    char label[160];
} pb_library_entry;

/* A channel's document while another channel is on the desk: the fields of
 * the document block of pb_app, parked. */
typedef struct {
    char path[PB_PATH_MAX];
    char name[MA_PATCH_NAME_MAX + 1];
    bool chain, factory;
    char parent_ref[PB_PATH_MAX];
    ma_patch parent;
    bool has_parent;
    ma_patch slot[2];
    bool slot_valid[2];
    unsigned current;
    bool dirty;
    ma_rack_spec rack[2];
    ma_rack_spec parent_rack;
    uint32_t rack_next_id;
} pb_document;

typedef struct pb_app {
    /* Document: the authored patch, A and B, of the channel on the desk
     * (bench.selected).  The other channels' documents are in `parked`. */
    char path[PB_PATH_MAX];          /* the open file, "" when new */
    char name[MA_PATCH_NAME_MAX + 1];
    bool chain, factory;             /* .wbpatch; a read-only factory file */
    char parent_ref[PB_PATH_MAX];    /* chain parent as written in the file */
    ma_patch parent;
    bool has_parent;
    ma_patch slot[2];
    bool slot_valid[2];
    unsigned current;                /* 0 = A, 1 = B */
    bool dirty;
    pb_undo *undo;
    pb_monitor monitor;
    bool module_mute[PB_MOD_COUNT];
    /* Insert rack (RK1, docs/ma-rack-plan.md): authored per A/B slot beside
     * the patch, saved with it (.mapatch v5, .wbpatch rack=1). */
    ma_rack_spec rack[2];
    ma_rack_spec parent_rack;        /* the chain parent's, for overrides */
    bool rack_mute[MA_RACK_SLOTS];   /* M: monitor only, never authored */
    int rack_selected;               /* slot under edit, -1 none */
    unsigned rack_insert;            /* the empty slot (1-based) asking for a kind, 0 none */
    uint32_t rack_next_id;
    ma_rack_spec sent_rack[PB_CHANNELS];
    ma_rack_spec audition_rack;
    /* EQ editor (RK1b): replaces the two module rows while open. */
    bool eq_open, eq_analyzer;
    int eq_band;                     /* selected band, -1 none */
    float eq_range;                  /* +- dB shown */
    int eq_drag;                     /* band under drag, -1 none */
    float eq_drag_y, eq_drag_q;
    uint64_t eq_wheel_at;
    /* Sequencer channels: each has a document; one is on the desk. */
    pb_document parked[PB_CHANNELS];
    bool channel_solo[PB_CHANNELS];  /* monitor only, not saved */
    int channel_request;             /* channel to put on the desk at the next frame, -1 none */
    ma_patch clipboard;
    pb_module clipboard_module;
    bool has_clipboard;
    /* Bench state (.pbench). */
    pb_bench bench;
    char bench_path[PB_PATH_MAX];
    char library[PB_PATH_MAX];       /* $WB_HOME/patchbench */
    double morph;                    /* 0 = off; morph between snapshots */
    unsigned morph_a, morph_b;
    /* Engine. */
    unsigned rate;
    pb_engine *engine;
    bool audio_on;
    /* Live-path tuning (2026-11-11): PB_LIVE_LOCK, PB_LIVE_PIN, PB_LIVE_HOLD
     * (each 1 by default, 0 turns it off). */
    bool live_lock, live_pin, live_hold, live_worker;
    pb_power_hold power;
    pb_cpu_layout cpus;
    uint64_t overloads_seen;
    bool demotion_seen;
    char device[64];
    ma_patch sent_patch[PB_CHANNELS];
    pb_voice sent_voice[PB_CHANNELS];
    pb_sequence sent_sequence;
    bool sent_valid;
    pb_meters meters;
    /* Probe. */
    pb_probe probe;
    const pb_probe_result *shown;
    ma_patch probed;
    uint8_t probed_note, probed_velocity;
    bool probed_compass, probe_posted;
    /* Live input. */
    int octave, velocity;
    bool latch;
    uint8_t key_note[512];
    bool key_down[512];
    bool sounding[128];
    /* UI. */
    pb_ui ui;
    /* Module fit: how much each module's content ran past its frame, taken
     * off its first picture on the next frame (pb_draw_module). */
    float module_shrink[PB_MOD_COUNT];
    pb_module module_drawing;
    bool module_picture_done;
    wb_theme_kind theme;
    pb_window main;
    char font_path[256];
    bool quit, smoke;
    char status[256];
    bool status_error;
    unsigned seq_step_selected;
    int scope_tap;                   /* pb_tap, or -1 for the live output */
    bool note_view, compass_sheet, spectrum_hold;
    float spectrum_peak[PB_SPECTRUM / 2 + 1];
    float live_db[PB_SPECTRUM / 2 + 1];
    uint64_t live_db_at;
    bool library_open, find_open;
    pb_library_entry *library_list;
    unsigned library_count;
    int library_selected;
    bool auditioning;
    ma_patch audition;
    char find[48];
    int find_length;
    uint64_t last_autosave_ms;
    float dpi_scale;
} pb_app;

/* pb_app.c */
[[gnu::format(printf, 3, 4)]]
void pb_say(pb_app *a, bool error, const char *format, ...);
const wb_theme *pb_theme(const pb_app *a);
ma_patch *pb_authored(pb_app *a);
ma_patch pb_audible(const pb_app *a);
ma_rack_spec *pb_authored_rack(pb_app *a);
/* The authored rack with muted blocks disabled: what the engine plays. */
ma_rack_spec pb_audible_rack(const pb_app *a);
/* Open an undo step with the authored value before an edit. */
void pb_begin_edit(pb_app *a, const char *label);
void pb_edited(pb_app *a);
void pb_note(pb_app *a, uint8_t note, bool on);
void pb_save(pb_app *a);
void pb_open(pb_app *a, const char *path);
void pb_library_scan(pb_app *a);
void pb_derive_child(pb_app *a);
void pb_flatten(pb_app *a);
/* Patch, rack and bench voice back to their neutral defaults; the name, the
 * parent and the file stay. One undo step restores the patch and the rack. */
void pb_reset(pb_app *a);
void pb_bounce_now(pb_app *a);
void pb_snapshot_store(pb_app *a, unsigned slot);
void pb_snapshot_recall(pb_app *a, unsigned slot);
void pb_swap_ab(pb_app *a);
void pb_audio_toggle(pb_app *a);
/* Sequencer channels.  The bench entry (patch file, voice setup, pattern,
 * mute) of the channel on the desk: */
pb_channel *pb_channel_now(pb_app *a);
/* Put channel `c` on the desk: its document, undo history, voice setup and
 * pattern; the keyboard plays it.  Solo, mute and morph of the desk clear. */
void pb_select_channel(pb_app *a, unsigned c);
/* What a channel plays: the desk's audible patch and rack for the selected
 * one, the parked document's current slot for the others. */
ma_patch pb_channel_patch(const pb_app *a, unsigned c);
ma_rack_spec pb_channel_rack(const pb_app *a, unsigned c);
const char *pb_channel_name(const pb_app *a, unsigned c);
bool pb_channel_dirty(const pb_app *a, unsigned c);
/* The patterns and mutes the engine plays; a solo mutes the other channels. */
pb_sequence pb_audible_sequence(const pb_app *a);

/* pb_modules.c: the twelve module panels, the macro strip, the bar, the
 * channel strip. */
void pb_draw_channels(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_module(pb_app *a, struct nk_context *ctx, pb_module module, struct nk_rect r);
void pb_draw_macros(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_bar(pb_app *a, struct nk_context *ctx, struct nk_rect r);
/* A catalog field as a bar or fader; true on change. */
bool pb_field_control(pb_app *a, struct nk_context *ctx, const char *name, bool fader);

/* pb_rack.c: the insert rack row (RK1). */
void pb_draw_rack(pb_app *a, struct nk_context *ctx, struct nk_rect r);
/* The EQ editor of the selected rack slot, over the module rows. */
void pb_draw_eq(pb_app *a, struct nk_context *ctx, struct nk_rect r);
/* Keys for the open EQ editor: Del removes the selected band, Esc closes.
 * False when the editor is closed or the key does nothing there. */
bool pb_eq_key(pb_app *a, bool remove, bool close);

/* pb_views.c: notes, scope, spectrum, level, status, library, find. */
void pb_draw_notes(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_scope(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_spectrum(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_level(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_status(pb_app *a, struct nk_context *ctx, struct nk_rect r);
void pb_draw_library(pb_app *a, struct nk_context *ctx, float width, float height);
void pb_draw_find(pb_app *a, struct nk_context *ctx, float width, float height);

/* Velocity curves of the core, for the Amp inset. */
static inline float pb_velocity_gain(uint8_t v) { return 0.25f + 0.75f * ma_velocity_level(v); }

#endif
