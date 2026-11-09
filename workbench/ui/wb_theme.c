#define _POSIX_C_SOURCE 200809L
#include "wb_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Dark first; the light theme keeps the same contrast roles. */
static const wb_theme THEMES[2] = {
    [WB_THEME_DARK] = {
        .background = { 0x15, 0x17, 0x1a, 0xff }, .panel = { 0x1c, 0x1f, 0x23, 0xff },
        .line = { 0x2e, 0x33, 0x39, 0xff }, .text = { 0xcc, 0xcf, 0xd4, 0xff },
        .dim = { 0x7b, 0x81, 0x8a, 0xff }, .select = { 0x2a, 0x3a, 0x4f, 0xff },
        .clip = { 0xe0, 0x55, 0x50, 0xff }, .steal_released = { 0xd9, 0x9a, 0x3a, 0xff },
        .steal_held = { 0xe0, 0x55, 0x50, 0xff }, .cached = { 0x5c, 0xb8, 0x5c, 0xff },
        .stale = { 0x6c, 0x71, 0x78, 0xff }, .live = { 0x4a, 0x90, 0xd9, 0xff },
    },
    [WB_THEME_LIGHT] = {
        .background = { 0xf4, 0xf4, 0xf2, 0xff }, .panel = { 0xfb, 0xfb, 0xfa, 0xff },
        .line = { 0xd2, 0xd4, 0xd6, 0xff }, .text = { 0x1d, 0x20, 0x24, 0xff },
        .dim = { 0x6a, 0x70, 0x78, 0xff }, .select = { 0xcf, 0xe0, 0xf5, 0xff },
        .clip = { 0xc0, 0x30, 0x2b, 0xff }, .steal_released = { 0xb0, 0x70, 0x10, 0xff },
        .steal_held = { 0xc0, 0x30, 0x2b, 0xff }, .cached = { 0x2e, 0x8b, 0x3e, 0xff },
        .stale = { 0x8a, 0x8f, 0x96, 0xff }, .live = { 0x1f, 0x6f, 0xc4, 0xff },
    },
};

const wb_theme *wb_theme_get(wb_theme_kind kind) {
    return &THEMES[kind == WB_THEME_LIGHT ? WB_THEME_LIGHT : WB_THEME_DARK];
}

static struct nk_color mix(struct nk_color a, struct nk_color b, float t) {
    return nk_rgba((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t),
                   (int)(a.b + (b.b - a.b) * t), 0xff);
}

void wb_theme_apply(struct nk_context *ctx, const wb_theme *t) {
    struct nk_color table[NK_COLOR_COUNT];
    struct nk_color hover = mix(t->panel, t->text, 0.08f);
    struct nk_color active = mix(t->panel, t->text, 0.16f);
    table[NK_COLOR_TEXT] = t->text;
    table[NK_COLOR_WINDOW] = t->panel;
    table[NK_COLOR_HEADER] = t->background;
    table[NK_COLOR_BORDER] = t->line;
    table[NK_COLOR_BUTTON] = t->panel;
    table[NK_COLOR_BUTTON_HOVER] = hover;
    table[NK_COLOR_BUTTON_ACTIVE] = active;
    table[NK_COLOR_TOGGLE] = t->background;
    table[NK_COLOR_TOGGLE_HOVER] = hover;
    table[NK_COLOR_TOGGLE_CURSOR] = t->live;
    table[NK_COLOR_SELECT] = t->panel;
    table[NK_COLOR_SELECT_ACTIVE] = t->select;
    table[NK_COLOR_SLIDER] = t->background;
    table[NK_COLOR_SLIDER_CURSOR] = t->dim;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = t->text;
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = t->live;
    table[NK_COLOR_PROPERTY] = t->background;
    table[NK_COLOR_EDIT] = t->background;
    table[NK_COLOR_EDIT_CURSOR] = t->text;
    table[NK_COLOR_COMBO] = t->background;
    table[NK_COLOR_CHART] = t->background;
    table[NK_COLOR_CHART_COLOR] = t->dim;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = t->live;
    table[NK_COLOR_SCROLLBAR] = t->panel;
    table[NK_COLOR_SCROLLBAR_CURSOR] = t->line;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = t->dim;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = t->text;
    table[NK_COLOR_TAB_HEADER] = t->background;
    table[NK_COLOR_KNOB] = t->background;
    table[NK_COLOR_KNOB_CURSOR] = t->dim;
    table[NK_COLOR_KNOB_CURSOR_HOVER] = t->text;
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = t->live;
    nk_style_from_table(ctx, table);

    /* Density: no rounding, 1 px borders, 4 px padding at most. */
    struct nk_style *s = &ctx->style;
    s->window.rounding = 0;
    s->window.border = 1;
    s->window.padding = nk_vec2(4, 2);
    s->window.spacing = nk_vec2(4, 0);
    s->window.group_padding = nk_vec2(2, 2);
    s->window.min_row_height_padding = 2;
    s->window.header.padding = nk_vec2(4, 2);
    s->window.header.label_padding = nk_vec2(2, 2);
    s->button.rounding = 0;
    s->button.border = 1;
    s->button.padding = nk_vec2(4, 1);
    s->selectable.rounding = 0;
    s->selectable.padding = nk_vec2(4, 1);
    s->edit.rounding = 0;
    s->property.rounding = 0;
    s->combo.rounding = 0;
    s->tab.rounding = 0;
    s->scrollv.rounding = 0;
    s->scrollh.rounding = 0;
}

static const char *const CANDIDATES[] = {
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Regular.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
};

struct nk_font *wb_font_load(struct nk_context *ctx, float pixels, char *path,
                             size_t path_size) {
    const char *chosen = getenv("WB_FONT");
    if (chosen && access(chosen, R_OK)) chosen = nullptr;
    for (size_t i = 0; !chosen && i < sizeof CANDIDATES / sizeof *CANDIDATES; i++)
        if (!access(CANDIDATES[i], R_OK)) chosen = CANDIDATES[i];
    struct nk_font_atlas *atlas = nk_sdl_font_stash_begin(ctx);
    struct nk_font_config config = nk_font_config(pixels);
    config.oversample_h = 2;
    config.oversample_v = 1;
    config.pixel_snap = true;
    struct nk_font *font = chosen ? nk_font_atlas_add_from_file(atlas, chosen, pixels, &config)
                                  : nullptr;
    if (!font) {
        chosen = nullptr;
        font = nk_font_atlas_add_default(atlas, pixels, &config);
    }
    nk_sdl_font_stash_end(ctx);
    if (font) nk_style_set_font(ctx, &font->handle);
    snprintf(path, path_size, "%s", chosen ? chosen : "");
    return font;
}
