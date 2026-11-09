/* tonewheel91 tests — hosted; libm is the oracle. Run: make test
 * The core suite runs the organ, electric-piano and Mamut Analog checks in
 * that order; each line's checks and pinned signatures are in its file. */
#include "check.h"

int fails, checks;

int main(void) {
    test_organ_suite();
    test_ep_suite();
    test_ma_suite();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
