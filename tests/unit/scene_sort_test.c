#include "internal/sk_scene.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static const sk_camera3d_t CAM = {
    .position = {0, 0, 10},
    .target = {0, 0, 0},
    .up = {0, 1, 0},
    .fovy = 45.0f,
};

void test_scene_view_depth(void)
{
    CHECK_NEAR(sk_scene_view_depth(&CAM, (vec3_t){0, 0, 0}), 10, EPS);
    CHECK_NEAR(sk_scene_view_depth(&CAM, (vec3_t){0, 0, -5}), 15, EPS);
    CHECK_NEAR(sk_scene_view_depth(&CAM, (vec3_t){3, 4, 10}), 0, EPS); /* sideways offset doesn't count */
    CHECK_NEAR(sk_scene_view_depth(&CAM, (vec3_t){0, 0, 12}), -2, EPS); /* behind the camera */
}

void test_scene_sort_transparent(void)
{
    /* depths 5, 20, 5, 1, 20 in submission order */
    sk_transparent_item_t items[] = {
        {.handle = 100, .depth = 5, .order = 0},
        {.handle = 101, .depth = 20, .order = 1},
        {.handle = 102, .depth = 5, .order = 2},
        {.handle = 103, .depth = 1, .order = 3},
        {.handle = 104, .depth = 20, .order = 4},
    };
    const sk_handle_t expected[] = {101, 104, 100, 102, 103}; /* far to near, ties in order */

    sk_scene_sort_transparent(items, 5);
    for (int i = 0; i < 5; i++) {
        CHECK(items[i].handle == expected[i]);
    }

    /* empty and single-item lists are left alone */
    sk_scene_sort_transparent(NULL, 0);
    sk_transparent_item_t one = {.handle = 7, .depth = 3};
    sk_scene_sort_transparent(&one, 1);
    CHECK(one.handle == 7);
}
