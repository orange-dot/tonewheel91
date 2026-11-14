/* Patchbench control kit (docs/patchbench-plan.md §2.3), drawn on the
 * Nuklear canvas.  One active-control record holds drag capture, focus and
 * text entry; widgets keep no state of their own. */
#ifndef PB_WIDGETS_H
#define PB_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>

#include "../ui/wb_nk.h"
#include "../ui/wb_ui.h"

/* Marks a control may show besides its value; NaN hides one. */
typedef struct {
    double parent, b, effective, mod_lo, mod_hi;
    bool inherited;              /* the value comes from the parent: drawn dim */
} pb_marks;

/* How a control maps its value: unit position ↔ value and the text. */
typedef struct {
    double (*to_unit)(const void *ctx, double value);
    double (*from_unit)(const void *ctx, double unit);
    void (*format)(const void *ctx, double value, char *text, size_t size);
    bool (*parse)(const void *ctx, const char *text, double *value);
    const void *ctx;
    double fine, coarse;         /* keyboard and modifier steps, in value units */
    double reset;                /* backspace / right-click */
    bool integer;
} pb_scale_fn;

typedef struct {
    int active;                  /* control under drag, -1 none */
    int focus;                   /* keyboard focus, -1 none */
    int editing;                 /* control in text entry, -1 none */
    char edit[32];
    int edit_length;
    double drag_value, drag_unit;
    float drag_x, drag_y;
    bool begin;                  /* the next change opens an undo step */
    int changed;                 /* control changed this frame, -1 none */
    int hover;                   /* control under the pointer: status hints */
    const wb_theme *theme;
    /* Keyboard requests for the focused control, consumed when drawn. */
    int key_steps;               /* ± fine steps */
    int key_coarse;              /* ± coarse steps */
    bool key_reset, key_enter;
    /* Controls drawn this frame, in order, for focus movement. */
    int order[256];
    unsigned char order_group[256];
    unsigned order_count;
} pb_ui;

void pb_ui_init(pb_ui *ui);
/* Call once per frame before drawing. */
void pb_ui_begin(pb_ui *ui, const wb_theme *theme);
/* Move focus within the group of the focused control (±1) or to the next
 * group (±1 with `group`). */
void pb_ui_move_focus(pb_ui *ui, int step, bool group);

/* A horizontal bar with the value printed inside.  Returns true when the
 * value changed; ui->begin is true on the first change of a gesture. */
bool pb_bar(pb_ui *ui, struct nk_context *ctx, int id, unsigned char group, const char *label, double *value,
            const pb_scale_fn *scale, const pb_marks *marks);
/* A vertical fader, label underneath. */
bool pb_fader(pb_ui *ui, struct nk_context *ctx, int id, unsigned char group, const char *label, double *value,
              const pb_scale_fn *scale, const pb_marks *marks);
/* A toggle drawn as a small lit square with a label. */
bool pb_toggle(struct nk_context *ctx, const wb_theme *theme, const char *label, bool *value);

/* Drag on a picture: true while the left button drags inside (captured);
 * *dx, *dy are the pointer movement since the last frame in pixels. */
bool pb_drag_area(pb_ui *ui, struct nk_context *ctx, int id, struct nk_rect r, float *dx, float *dy, bool *began);

/* Drawing helpers on the current window's canvas. */
struct nk_color pb_alpha(struct nk_color c, unsigned char a);
void pb_frame(struct nk_command_buffer *out, struct nk_rect r, const wb_theme *t);
void pb_text(struct nk_context *ctx, struct nk_rect r, const char *text, struct nk_color color);
/* A trace of n samples across r, y in [lo, hi] (bottom to top). */
void pb_trace(struct nk_command_buffer *out, struct nk_rect r, const float *y, unsigned n, float lo, float hi,
              struct nk_color color, float thickness);
/* x/y pairs (scatter-line), both in data units mapped into r. */
void pb_xy(struct nk_command_buffer *out, struct nk_rect r, const float *x, const float *y, unsigned n, float x0,
           float x1, float y0, float y1, struct nk_color color, float thickness);

#endif
