#include <math.h>
#include <stdio.h>

#include "ma_architecture_cli.h"
#include "ma_architecture_render.h"
#include "ma_architecture_score.h"

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [-d seconds] [-r rate] [-o path]\n"
            "       %s -h\n",
            program, program);
}

int main(int argc, char *argv[]) {
    ma_arch_cli_options options = { 0 };
    const char *reason = 0;
    ma_arch_cli_status status = ma_arch_cli_parse(argc, argv, &options,
                                                  &reason);
    if (status == MA_ARCH_CLI_HELP) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (status == MA_ARCH_CLI_ERROR) {
        fprintf(stderr, "%s: %s\n", argv[0],
                reason ? reason : "invalid option");
        usage(stderr, argv[0]);
        return 2;
    }

    ma_arch_score score = { 0 };
    if (!ma_arch_score_build(&score)
        || !ma_arch_score_validate(&score, &reason)) {
        fprintf(stderr, "%s: score: %s\n", argv[0],
                reason ? reason : "build failed");
        return 1;
    }
    ma_arch_render_result result = { 0 };
    if (ma_arch_render_file(&score, options.output_path,
                            options.duration_seconds, options.rate_hz,
                            &result, &reason) < 0) {
        fprintf(stderr, "%s: %s\n", argv[0], reason ? reason : "render failed");
        return 1;
    }
    const ma_arch_render_metrics *metrics = &result.second_pass;
    double rms = sqrt(metrics->sum_squares / (2.0 * metrics->frames));
    printf("Mamut Arhitektura I — %.3f s, %u Hz, %llu stereo frames\n",
           options.duration_seconds, options.rate_hz,
           (unsigned long long)metrics->frames);
    printf("  %u notes, %u-voice peak, peak %.6f, RMS %.6f\n",
           metrics->notes, metrics->peak_voices,
           (double)metrics->peak, rms);
    printf("  FNV64 %016llx (two block passes identical)\n",
           (unsigned long long)metrics->hash);
    printf("  wav: %s\n", options.output_path);
    return 0;
}
