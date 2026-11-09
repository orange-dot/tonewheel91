/* Organ and ep73 part engines (docs/workbench-plan.md §3.3, W9).
 *
 * .twreg  organ registration and console    .epset  ep73 settings
 *
 * Both are strict, versioned key=value text like .mapatch, with one field
 * catalog per format.  The adapters set an instrument up in the order
 * render_midi uses, so a part with the same settings renders the same bits.
 * Notes and controllers go through the drivers' own MIDI maps
 * (midi_map.c, ep_midi_map.c), which is where the console CCs live. */
#ifndef WB_ENGINE_H
#define WB_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../driver/midi_map.h"
#include "../../src/epiano.h"
#include "../../src/tonewheel.h"
#include "wb_session.h"

enum { WB_ENGINE_NAME_MAX = 32 };

/* Optional floats use a negative value for "left at the instrument's own
 * default" and are not written. */
typedef struct {
    char name[WB_ENGINE_NAME_MAX + 1];
    uint8_t registration[TW_DRAWBARS];
    int vibrato;                   /* 0 off, 1..3 V1..V3, 4..6 C1..C3 */
    bool percussion, percussion_third, percussion_slow, percussion_soft;
    float drive;
    int rotary;                    /* 0 bypass, 1 chorale, 2 tremolo, 3 brake */
    float wear;
    bool fold;
    float rotary_balance, rotary_width, rotary_drive, swell;
} wb_twreg;

typedef struct {
    char name[WB_ENGINE_NAME_MAX + 1];
    float drive, cabinet, condition;
    bool fold;
    int tremolo;                   /* CC value 0..127, or -1 untouched */
} wb_epset;

void wb_twreg_default(wb_twreg *reg);
void wb_epset_default(wb_epset *set);

bool wb_twreg_read(const char *text, size_t size, wb_twreg *reg, wb_error *error);
bool wb_twreg_write(FILE *file, const wb_twreg *reg);
bool wb_twreg_load(const char *path, wb_twreg *reg, wb_error *error);
bool wb_twreg_save(const char *path, const wb_twreg *reg, wb_error *error);

bool wb_epset_read(const char *text, size_t size, wb_epset *set, wb_error *error);
bool wb_epset_write(FILE *file, const wb_epset *set);
bool wb_epset_load(const char *path, wb_epset *set, wb_error *error);
bool wb_epset_save(const char *path, const wb_epset *set, wb_error *error);

/* One field of either catalog, for editors: name, range, steps. */
typedef struct {
    const char *name;
    double minimum, maximum, fine_step, coarse_step;
    bool integer;
} wb_engine_field;

size_t wb_twreg_field_count(void);
const wb_engine_field *wb_twreg_field(size_t index);
size_t wb_epset_field_count(void);
const wb_engine_field *wb_epset_field(size_t index);

/* ---- instances ------------------------------------------------------------ */

typedef struct {
    wb_part_engine kind;
    tw_instrument *organ;
    organ_midi_map *organ_map;
    ep_piano *ep;
    ep_midi_map *ep_map;
} wb_engine_instance;

bool wb_engine_open_organ(wb_engine_instance *e, unsigned rate, const wb_twreg *reg);
bool wb_engine_open_ep(wb_engine_instance *e, unsigned rate, const wb_epset *set);
void wb_engine_close(wb_engine_instance *e);
/* One raw channel message through the driver's MIDI map. */
void wb_engine_midi(wb_engine_instance *e, uint8_t status, uint8_t d1, uint8_t d2);
tw_stereo wb_engine_tick(wb_engine_instance *e);
unsigned long wb_engine_notes(const wb_engine_instance *e);
uint32_t wb_engine_out_of_compass(const wb_engine_instance *e);

#endif
