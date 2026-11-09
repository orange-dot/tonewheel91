/* The core suite's check counter, shared by test.c and the per-line suites
 * (test_organ.c, test_ep.c, test_ma.c). */
#ifndef TEST_CHECK_H
#define TEST_CHECK_H
#include <stdio.h>

extern int fails, checks;
static const double TAU_D = 6.283185307179586;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        fails++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        putchar('\n'); \
    } \
} while (0)

void test_organ_suite(void);
void test_ep_suite(void);
void test_ma_suite(void);

#endif
