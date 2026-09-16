/* libsk unit test runner (`make test`).
 *
 *   tests/build/unit_tests            run every test
 *   tests/build/unit_tests pick_      run tests whose name starts with "pick_"
 *
 * Tests call library internals directly and link against lib/libsk.a. Nothing
 * here opens a window or touches the GPU. */
#include <stdio.h>
#include <string.h>

#include "test.h"
#include "tests.h"

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

static const test_case_t TESTS[] = {
    {"frame_pace_unpaced", test_frame_pace_unpaced},
    {"frame_pace_schedule", test_frame_pace_schedule},
    {"frame_pace_web_skip", test_frame_pace_web_skip},
    {"handle_pool", test_handle_pool},
    {"handle_pool_reuse", test_handle_pool_reuse},
    {"math_inverse", test_math_inverse},
    {"math_trs", test_math_trs},
    {"pick_ray_sphere", test_pick_ray_sphere},
    {"pick_ray_aabb", test_pick_ray_aabb},
    {"pick_ray_triangle", test_pick_ray_triangle},
    {"pick_ray_to_local", test_pick_ray_to_local},
    {"pick_world_aabb", test_pick_world_aabb},
    {"pick_ray_from_screen", test_pick_ray_from_screen},
    {"scene_view_depth", test_scene_view_depth},
    {"scene_sort_transparent", test_scene_sort_transparent},
};

int sk_test_failures;

static int matches_filter(const char *name, int argc, char **argv)
{
    if (argc <= 1) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (strncmp(name, argv[i], strlen(argv[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int run = 0, failed = 0;

    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (!matches_filter(TESTS[i].name, argc, argv)) {
            continue;
        }
        sk_test_failures = 0;
        TESTS[i].fn();
        run++;
        if (sk_test_failures > 0) {
            failed++;
            printf("  FAIL  %s (%d failed check%s)\n", TESTS[i].name, sk_test_failures,
                   sk_test_failures == 1 ? "" : "s");
        } else {
            printf("  ok    %s\n", TESTS[i].name);
        }
    }

    if (run == 0) {
        printf("unit tests: no test matched\n");
        return 1;
    }
    printf("%s: %d of %d unit tests passed\n", failed ? "FAIL" : "PASS", run - failed, run);
    return failed ? 1 : 0;
}
