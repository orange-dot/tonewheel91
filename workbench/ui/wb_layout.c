#define _POSIX_C_SOURCE 200809L
#include "wb_ui.h"

#include <stdio.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static const char *const VIEW_NAMES[WB_VIEW_COUNT] = {
    "Sources", "Import", "Orchestrate", "Roll", "Groove", "Form", "Patch",
    "Mixer", "Probe", "Render", "Listen", "Ledger", "Play",
};

static const char *const VIEW_MILESTONES[WB_VIEW_COUNT] = {
    "W4", "W4", "W4", "W4", "W4", "W4", "W5", "W5", "W6", "W6", "W10", "W10", "W11",
};

const char *wb_view_name(wb_view view) {
    return view < WB_VIEW_COUNT ? VIEW_NAMES[view] : "?";
}

const char *wb_view_milestone(wb_view view) {
    return view < WB_VIEW_COUNT ? VIEW_MILESTONES[view] : "?";
}

static const wb_preset PRESETS[WB_PRESET_COUNT] = {
    { "Orchestrate", 6,
      { WB_VIEW_SOURCES, WB_VIEW_IMPORT, WB_VIEW_ORCHESTRATE, WB_VIEW_ROLL,
        WB_VIEW_GROOVE, WB_VIEW_FORM },
      WB_VIEW_ORCHESTRATE, WB_VIEW_ROLL },
    { "Patch", 2, { WB_VIEW_PATCH, WB_VIEW_MIXER }, WB_VIEW_PATCH, WB_VIEW_ROLL },
    { "Mix/Probe", 3, { WB_VIEW_MIXER, WB_VIEW_PROBE, WB_VIEW_RENDER },
      WB_VIEW_PROBE, WB_VIEW_PATCH },
    { "Listen", 2, { WB_VIEW_LISTEN, WB_VIEW_RENDER }, WB_VIEW_LISTEN, WB_VIEW_LEDGER },
    { "Ledger", 2, { WB_VIEW_LEDGER, WB_VIEW_LISTEN }, WB_VIEW_LEDGER, WB_VIEW_LISTEN },
    { "Play", 2, { WB_VIEW_PLAY, WB_VIEW_PATCH }, WB_VIEW_PLAY, WB_VIEW_PATCH },
};

const wb_preset *wb_preset_at(unsigned index) {
    return index < WB_PRESET_COUNT ? &PRESETS[index] : nullptr;
}

static float shrink(float wanted, float available, float share) {
    float limit = available * share;
    return wanted < limit ? wanted : limit;
}

wb_frame wb_frame_layout(float width, float height, bool console_open) {
    wb_frame f = { 0 };
    float console = console_open ? WB_HEADER_HEIGHT + WB_CONSOLE_LINES * WB_ROW_HEIGHT
                                 : WB_ROW_HEIGHT + 2;
    float ribbon = WB_RIBBON_HEIGHT;
    if (ribbon + console > height) console = height > ribbon ? height - ribbon : 0;
    float body = height - ribbon - console;
    if (body < 0) body = 0;
    float parts = shrink(WB_PARTS_WIDTH, width, 0.18f);
    float inspector = shrink(WB_INSPECTOR_WIDTH, width, 0.22f);
    float workspace = width - parts - inspector;
    if (workspace < 0) workspace = 0;
    f.ribbon = (wb_rect){ 0, 0, width, ribbon };
    f.parts = (wb_rect){ 0, ribbon, parts, body };
    f.workspace = (wb_rect){ parts, ribbon, workspace, body };
    f.inspector = (wb_rect){ parts + workspace, ribbon, inspector, body };
    f.console = (wb_rect){ 0, ribbon + body, width, console };
    return f;
}

wb_proc_status wb_proc_sample(void) {
    static double last_cpu = -1, last_wall = 0;
    wb_proc_status s = { 0 };
    FILE *statm = fopen("/proc/self/statm", "r");
    if (statm) {
        unsigned long size = 0, resident = 0;
        if (fscanf(statm, "%lu %lu", &size, &resident) == 2)
            s.rss_mib = (double)resident * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
        fclose(statm);
    }
    struct rusage usage;
    struct timespec now;
    if (getrusage(RUSAGE_SELF, &usage) == 0 && clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        double cpu = (double)usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6
                   + (double)usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1e-6;
        double wall = (double)now.tv_sec + now.tv_nsec * 1e-9;
        if (last_cpu >= 0 && wall > last_wall)
            s.cpu_percent = 100.0 * (cpu - last_cpu) / (wall - last_wall);
        last_cpu = cpu;
        last_wall = wall;
    }
    return s;
}
