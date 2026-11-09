/* Workbench session model (docs/workbench-plan.md §3).
 *
 * A session holds rules, not notes: sources, the form, stems, parts with
 * their engine configuration, orchestration rules, grooves, curves, lanes
 * and markers.  Everything is fixed capacity so a session is one value that
 * can be copied, compared and snapshotted without ownership questions.
 *
 * Object order is meaningful and kept: stem order is the stem summation
 * order and part order is the summation order inside a stem, so both are
 * part of the bit-exact result of a render. */
#ifndef WB_SESSION_H
#define WB_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    WB_ID_MAX = 31,
    WB_TEXT_MAX = 127,
    WB_PATH_MAX = 255,
    WB_PARAM_MAX = 47,

    WB_SOURCES = 16,
    WB_STEMS = 8,
    WB_PARTS = 16,
    WB_SECTIONS = 64,
    WB_RULES = 256,
    WB_GROOVES = 16,
    WB_PATTERNS = 16,
    WB_PATTERN_STEPS = 32,
    WB_USES = 64,
    WB_HITS = 32,
    WB_CURVES = 16,
    WB_LANES = 64,
    WB_POINTS = 256,
    WB_MARKERS = 128,
    WB_IMPORTS = 16,
    WB_OVERLAYS = 1024,
};

typedef char wb_id[WB_ID_MAX + 1];

/* The object kinds of a session file, in canonical write order. */
typedef enum : uint8_t {
    WB_KIND_SESSION,
    WB_KIND_SOURCE,
    WB_KIND_FORM,
    WB_KIND_STEM,
    WB_KIND_PART,
    WB_KIND_RULE,
    WB_KIND_GROOVE,
    WB_KIND_CURVE,
    WB_KIND_LANE,
    WB_KIND_MARKER,
    WB_KIND_IMPORT,
    WB_KIND_OVERLAY,
    WB_KIND_COUNT
} wb_kind;

typedef enum : uint8_t { WB_ENGINE_AUTO, WB_ENGINE_SCALAR } wb_render_engine;
/* desk: the MA3 show-desk semantics (exact tick->frame, releases before
 * attacks, stems summed from zero, fades).  render_midi: render_midi's
 * (double-precision frames, file order, last event + tail, raw sums), for
 * reproducing its organ and ep73 renders. */
typedef enum : uint8_t { WB_COMPAT_DESK, WB_COMPAT_RENDER_MIDI } wb_compat;
typedef enum : uint8_t { WB_PART_MAMUT, WB_PART_ORGAN, WB_PART_EP73 } wb_part_engine;
typedef enum : uint8_t {
    WB_DEDUPE_RESTRIKE, WB_DEDUPE_KEEP_LOUDEST, WB_DEDUPE_OFF
} wb_dedupe;
typedef enum : uint8_t { WB_SHAPE_LINEAR, WB_SHAPE_SMOOTHSTEP, WB_SHAPE_STEP } wb_shape;
typedef enum : uint8_t { WB_LANE_SET, WB_LANE_MUL, WB_LANE_ADD } wb_lane_op;
typedef enum : uint8_t { WB_VOICE_ALL, WB_VOICE_TOP, WB_VOICE_REST } wb_voice;
typedef enum : uint8_t { WB_OVERLAY_ADD, WB_OVERLAY_DELETE } wb_overlay_op;
typedef enum : uint8_t {
    WB_SPLIT_TRACK_CHANNEL, WB_SPLIT_TRACK, WB_SPLIT_CHANNEL, WB_SPLIT_PROGRAM, WB_SPLIT_MANUAL
} wb_split;

/* Controller streams a rule forwards into its part (plan §5.2): any of the
 * 128 CCs plus pitch bend, channel pressure and poly pressure.  Forwarded
 * events keep their exact ticks and are dispatched at their sample. */
enum { WB_CTL_BEND = 128, WB_CTL_PRESSURE = 129, WB_CTL_POLY = 130, WB_CTL_COUNT = 131 };
typedef struct { uint64_t bits[3]; } wb_ctlset;

typedef struct {
    char name[WB_TEXT_MAX + 1];
    char title[WB_TEXT_MAX + 1];   /* derived MIDI title; empty: name */
    uint32_t rate;               /* 44100 | 48000 | 96000 */
    uint32_t control_frames;     /* lane and curve evaluation period */
    wb_render_engine engine;
    wb_compat compat;
} wb_session_head;

typedef struct {
    wb_id id;
    char path[WB_PATH_MAX + 1];  /* relative to the session directory */
    char sha256[65];
    int32_t offset_bars;
    uint32_t us_per_quarter;     /* 0 keeps the source tempo map */
    double tempo_scale;          /* multiplies every tempo; 1 = unchanged */
} wb_source;

typedef struct {
    wb_id name;
    int32_t start_bar, end_bar;  /* [start, end) in source bars */
} wb_section;

typedef struct {
    int32_t start_bar, end_bar;  /* [start, end) in source bars */
    double tail_seconds;
    uint32_t length_seconds;     /* 0 derives the length from bars + tail */
    double fade_in_ms, fade_out_ms;
    unsigned section_count;
    wb_section section[WB_SECTIONS];
} wb_form;

typedef struct {
    wb_id id;
} wb_stem;

typedef struct {
    wb_id id;
    wb_part_engine engine;
    char patch[WB_PATH_MAX + 1]; /* relative to the session directory */
    uint32_t limit, unison;
    float detune_cents;
    bool seed_from_name;         /* default seed XOR FNV-1a of the id */
    uint64_t seed;               /* used when !seed_from_name */
    float character;
    float chorus, chorus_depth, chorus_rate_hz;
    float reverb, reverb_size, reverb_damping;
    float gain;                  /* linear; the UI shows dB */
    float pressure;              /* poly pressure sent with every note-on */
    wb_id stem;
    wb_dedupe dedupe;
    float pan;                   /* desk equal-power pan, -1..1; 0 is exact bypass */
} wb_part;

/* A note set over 0..127 (two 64-bit words). */
typedef struct { uint64_t bits[2]; } wb_noteset;

typedef struct {
    wb_id part;
    wb_id source;
    char track_name[WB_TEXT_MAX + 1]; /* empty: any */
    int32_t track;               /* -1: any */
    int32_t channel;             /* 1..16, -1: any */
    int32_t program;             /* -1: any */
    bool has_notes;
    wb_noteset notes;
    int32_t transpose;
    int32_t map_to;              /* -1: keep the pitch */
    int32_t fold_below;          /* -1: off; notes below go up by octaves */
    int32_t fold_above;          /* -1: off; notes above go down by octaves */
    uint32_t velocity_num, velocity_den; /* floor(v * num / den) */
    int32_t velocity_offset;
    uint32_t velocity_min, velocity_max;
    double ring_max_bars;        /* 0: no cap */
    uint32_t min_length_ticks;
    int32_t bar_in, bar_out;     /* [in, out) source bars, -1: open */
    wb_voice voice;              /* skyline split: highest sounding note or the rest */
    wb_ctlset controllers;       /* forwarded controller streams */
} wb_rule;

typedef struct {
    wb_id name;
    uint32_t velocity;
    unsigned step_count;
    uint8_t step[WB_PATTERN_STEPS];
    int8_t accent[WB_PATTERN_STEPS]; /* per listed step, added to velocity */
} wb_pattern;

typedef struct {
    int32_t from_bar, to_bar;    /* [from, to) */
    uint8_t pattern;
} wb_use;

typedef struct {
    int32_t bar;
    uint32_t velocity;
} wb_hit;

typedef struct {
    wb_id part;
    uint32_t note;
    uint32_t length_ticks;
    uint32_t steps_per_bar;
    /* Velocity wobble (bar * a + step * b) % m - m / 2, m == 0 disables. */
    uint32_t wobble_a, wobble_b, wobble_m;
    unsigned pattern_count, use_count, hit_count;
    wb_pattern pattern[WB_PATTERNS];
    wb_use use[WB_USES];
    wb_hit hit[WB_HITS];
} wb_groove;

typedef struct {
    double bar;
    float value;
} wb_point;

typedef struct {
    wb_id id;
    wb_shape shape;
    unsigned point_count;
    wb_point point[WB_POINTS];
} wb_curve;

typedef struct {
    wb_id part;
    char param[WB_PARAM_MAX + 1];
    wb_lane_op op;
    wb_shape shape;
    /* Either points, or a curve reference evaluated as
     * clamp(offset + scale * curve(bar), min, max). */
    wb_id curve;
    float scale, offset;
    bool clamp;
    float minimum, maximum;
    unsigned point_count;
    wb_point point[WB_POINTS];
} wb_lane;

typedef struct {
    double bar;
    wb_id name;
    char text[WB_TEXT_MAX + 1];
} wb_marker;

/* How a source was split into parts, kept so a changed file can be
 * re-imported against the same parts (plan §5.2, linkage). */
enum { WB_PITCH_SPLITS = 16 };
typedef struct {
    int32_t track;               /* source track */
    int32_t note;                /* split note, or -1 for top voice vs rest */
} wb_pitch_split;

typedef struct {
    wb_id source;
    wb_split split;
    int32_t drums;               /* drum channel 1..16 split by note groups, 0 off */
    bool program_split;
    unsigned pitch_split_count;
    wb_pitch_split pitch_split[WB_PITCH_SPLITS];
    wb_ctlset controllers;       /* forwarded by every rule the import wrote */
} wb_import;

/* A hand edit on the Roll (plan §5.4): applied to the part's notes after
 * the rules and grooves, so rules stay primary and edits stay visible. */
typedef struct {
    wb_id part;
    wb_overlay_op op;
    double bar;                  /* absolute source bar of the note start */
    double length_bars;          /* add only */
    uint32_t note, velocity;
} wb_overlay;

typedef struct {
    wb_session_head head;
    wb_form form;
    unsigned source_count, stem_count, part_count, rule_count;
    unsigned groove_count, curve_count, lane_count, marker_count, import_count, overlay_count;
    bool has_head, has_form;
    wb_source source[WB_SOURCES];
    wb_stem stem[WB_STEMS];
    wb_part part[WB_PARTS];
    wb_rule rule[WB_RULES];
    wb_groove groove[WB_GROOVES];
    wb_curve curve[WB_CURVES];
    wb_lane lane[WB_LANES];
    wb_marker marker[WB_MARKERS];
    wb_import import[WB_IMPORTS];
    wb_overlay overlay[WB_OVERLAYS];
} wb_session;

enum { WB_ERROR_MAX = 160 };
typedef struct {
    unsigned line;               /* 0 when not tied to a line */
    char message[WB_ERROR_MAX];
} wb_error;

/* Defaults for one freshly added object of each kind. */
void wb_session_init(wb_session *session);
void wb_source_default(wb_source *source);
void wb_form_default(wb_form *form);
void wb_part_default(wb_part *part);
void wb_rule_default(wb_rule *rule);
void wb_groove_default(wb_groove *groove);
void wb_curve_default(wb_curve *curve);
void wb_lane_default(wb_lane *lane);
void wb_marker_default(wb_marker *marker);
void wb_import_default(wb_import *import);
void wb_overlay_default(wb_overlay *overlay);

bool wb_ctlset_has(const wb_ctlset *set, unsigned kind);
void wb_ctlset_add(wb_ctlset *set, unsigned kind);
bool wb_ctlset_empty(const wb_ctlset *set);

const char *wb_kind_name(wb_kind kind);
bool wb_kind_from_name(const char *name, wb_kind *kind);
/* Number of objects of a kind (0/1 for the singletons) and the capacity. */
unsigned wb_kind_count(const wb_session *session, wb_kind kind);
unsigned wb_kind_capacity(wb_kind kind);
bool wb_kind_singleton(wb_kind kind);

/* Find by id (index or -1). */
int wb_find_source(const wb_session *session, const char *id);
int wb_find_stem(const wb_session *session, const char *id);
int wb_find_part(const wb_session *session, const char *id);
int wb_find_curve(const wb_session *session, const char *id);

bool wb_id_valid(const char *text);
bool wb_text_valid(const char *text, size_t max);
/* Relative path inside the session: no leading '/', no "..", no empty
 * components, printable ASCII without spaces. */
bool wb_path_valid(const char *text);

bool wb_noteset_has(const wb_noteset *set, unsigned note);
void wb_noteset_add(wb_noteset *set, unsigned note);

/* Lane parameters: every .mapatch field, plus the pool and part controls
 * below.  Returns false for unknown names. */
bool wb_lane_param_valid(const char *param);

/* Semantic checks that need the whole session: required objects, unique
 * ids, references, stems in use, unison dividing the limit, sections inside
 * the form and in order, lane parameters, curve references.  Files are not
 * touched here (see wb_check_files). */
bool wb_session_validate(const wb_session *session, wb_error *error);

#endif
