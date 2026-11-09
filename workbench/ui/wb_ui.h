/* Workbench UI: frame layout, presets, theme and fonts (plan §4). */
#ifndef WB_UI_H
#define WB_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "../desk/wb_session.h"
#include "wb_nk.h"

/* Workspace views (plan §5), in tab order. */
typedef enum {
    WB_VIEW_SOURCES, WB_VIEW_IMPORT, WB_VIEW_ORCHESTRATE, WB_VIEW_ROLL,
    WB_VIEW_GROOVE, WB_VIEW_FORM, WB_VIEW_PATCH, WB_VIEW_MIXER, WB_VIEW_PROBE,
    WB_VIEW_RENDER, WB_VIEW_LISTEN, WB_VIEW_LEDGER, WB_VIEW_PLAY,
    WB_VIEW_COUNT
} wb_view;

const char *wb_view_name(wb_view view);
/* The milestone that fills the view in, for the placeholder text. */
const char *wb_view_milestone(wb_view view);

/* Fixed layout presets F1..F6 instead of free docking (plan §4.1). */
enum { WB_PRESET_COUNT = 6, WB_PRESET_TABS = 6 };
typedef struct {
    const char *name;
    unsigned tab_count;
    wb_view tab[WB_PRESET_TABS];
    wb_view active;
    wb_view pinned;            /* the second monitor's view */
} wb_preset;

const wb_preset *wb_preset_at(unsigned index);

/* Pixel rectangles of the global frame for one window size. */
enum {
    WB_RIBBON_HEIGHT = 44,
    WB_PARTS_WIDTH = 300,
    WB_INSPECTOR_WIDTH = 380,
    WB_ROW_HEIGHT = 18,
    WB_HEADER_HEIGHT = 20,
    WB_CONSOLE_LINES = 8,
};
typedef struct { float x, y, w, h; } wb_rect;
typedef struct {
    wb_rect ribbon, parts, workspace, inspector, console;
} wb_frame;

/* Collapsed console is one line; open is WB_CONSOLE_LINES lines. Columns
 * shrink, never vanish, so a 1920 px window keeps a usable workspace. */
wb_frame wb_frame_layout(float width, float height, bool console_open);

/* Colors carry state only (plan §4.1). */
typedef enum { WB_THEME_DARK, WB_THEME_LIGHT } wb_theme_kind;
typedef struct {
    struct nk_color background, panel, line, text, dim, select;
    struct nk_color clip, steal_released, steal_held, cached, stale, live;
} wb_theme;

const wb_theme *wb_theme_get(wb_theme_kind kind);
void wb_theme_apply(struct nk_context *ctx, const wb_theme *theme);

/* Bake the monospace UI font at `pixels` into the context's atlas.  Uses
 * WB_FONT when set, else the first installed candidate, else Nuklear's
 * built-in font.  *path receives the file used ("" for the built-in one). */
struct nk_font *wb_font_load(struct nk_context *ctx, float pixels,
                             char *path, size_t path_size);

/* nk_select_label returns the new selection state, so a selected item reads
 * true on every frame.  This reports only a click (a change of state). */
static inline bool select_clicked(struct nk_context *ctx, const char *label, nk_flags align, bool selected) {
    return (bool)nk_select_label(ctx, label, align, selected) != selected;
}

/* Process status for the ribbon: resident set in MiB, CPU percent of one
 * core since the previous call. */
typedef struct { double rss_mib, cpu_percent; } wb_proc_status;
wb_proc_status wb_proc_sample(void);

#endif
