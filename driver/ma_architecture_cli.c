#include "ma_architecture_cli.h"

#include <stdint.h>
#include <string.h>

#include "host_parse.h"
#include "ma_architecture_render.h"

static ma_arch_cli_status cli_error(const char **reason, const char *message) {
    if (reason) *reason = message;
    return MA_ARCH_CLI_ERROR;
}

ma_arch_cli_status ma_arch_cli_parse(int argc, char *argv[],
                                     ma_arch_cli_options *options,
                                     const char **reason) {
    if (!options || argc < 1 || !argv)
        return cli_error(reason, "invalid command line");
    *options = (ma_arch_cli_options){
        .duration_seconds = MA_ARCH_MAX_SECONDS,
        .rate_hz = MA_ARCH_DEFAULT_RATE,
        .output_path = "build/mamut_architecture_v1.wav",
    };
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (!strcmp(argument, "-h") || !strcmp(argument, "--help")) {
            if (reason) *reason = 0;
            return MA_ARCH_CLI_HELP;
        }
        if (strcmp(argument, "-d") && strcmp(argument, "-r")
            && strcmp(argument, "-o"))
            return cli_error(reason, "unknown option");
        if (++i == argc) return cli_error(reason, "option needs a value");
        if (!strcmp(argument, "-d")) {
            double seconds = 0.0;
            if (!host_parse_double(argv[i], 0.0, MA_ARCH_MAX_SECONDS, &seconds)
                || !(seconds > 0.0))
                return cli_error(reason, "duration must be > 0 and <= 960");
            options->duration_seconds = seconds;
        } else if (!strcmp(argument, "-r")) {
            uint64_t rate = 0;
            if (!host_parse_u64(argv[i], 44100, 192000, &rate))
                return cli_error(reason, "rate must be between 44100 and 192000");
            options->rate_hz = (unsigned)rate;
        } else {
            if (!*argv[i]) return cli_error(reason, "output path is empty");
            options->output_path = argv[i];
        }
    }
    uint64_t frames = 0;
    if (!ma_arch_duration_frames(options->duration_seconds, options->rate_hz,
                                 &frames))
        return cli_error(reason, "duration and rate exceed the WAV limit");
    if (reason) *reason = 0;
    return MA_ARCH_CLI_OK;
}
