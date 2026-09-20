#ifndef WGR_TEST_H
#define WGR_TEST_H

/* Minimal unit test harness. A test is a `void fn(void)` listed in tests.h and
 * main.c. CHECK* macros record a failure and keep going, so one run shows every
 * failing check in a test. */

#include <math.h>
#include <stdio.h>

extern int wgr_test_failures; /* failed checks in the current test */

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "    %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);      \
            wgr_test_failures++;                                                        \
        }                                                                              \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                              \
    do {                                                                               \
        double actual_ = (double)(actual), expected_ = (double)(expected);             \
        if (!(fabs(actual_ - expected_) <= (double)(eps))) {                           \
            fprintf(stderr, "    %s:%d: CHECK_NEAR(%s, %s): got %g, expected %g\n",    \
                    __FILE__, __LINE__, #actual, #expected, actual_, expected_);       \
            wgr_test_failures++;                                                        \
        }                                                                              \
    } while (0)

#define CHECK_VEC3_NEAR(v, ex, ey, ez, eps)                                            \
    do {                                                                               \
        CHECK_NEAR((v).x, (ex), (eps));                                                \
        CHECK_NEAR((v).y, (ey), (eps));                                                \
        CHECK_NEAR((v).z, (ez), (eps));                                                \
    } while (0)

#endif // WGR_TEST_H
