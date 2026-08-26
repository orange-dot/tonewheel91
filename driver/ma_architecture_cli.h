#ifndef MA_ARCHITECTURE_CLI_H
#define MA_ARCHITECTURE_CLI_H

typedef struct {
    double duration_seconds;
    unsigned rate_hz;
    const char *output_path;
} ma_arch_cli_options;

typedef enum {
    MA_ARCH_CLI_OK,
    MA_ARCH_CLI_HELP,
    MA_ARCH_CLI_ERROR,
} ma_arch_cli_status;

ma_arch_cli_status ma_arch_cli_parse(int argc, char *argv[],
                                     ma_arch_cli_options *options,
                                     const char **reason);

#endif
