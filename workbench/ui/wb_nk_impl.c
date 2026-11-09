/* The single implementation unit for the vendored Nuklear code and its SDL3
 * renderer backend.  Built with relaxed warnings (see workbench.mk), so the
 * Workbench's own units keep the project's -Wall -Wextra -Wpedantic. */
#define NK_IMPLEMENTATION
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "wb_nk.h"
