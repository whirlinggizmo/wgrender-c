#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_scene.h"
#include "sk_scene.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

static const sk_camera3d_t CAM = {
    .position = {0, 0, 10},
    .target = {0, 0, 0},
    .up = {0, 1, 0},
    .fov = 0.785398163f,
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

/* Scene membership (a hash index with holes closed by tidy): a long random run of
 * adds, removes, relayers and forgets agrees with a plain array at every step, with
 * picks in between (they tidy the members). Handles are arbitrary nonzero values. */
void test_scene_membership(void)
{
    enum { HANDLES = 600, STEPS = 20000 };
    static bool member[HANDLES];
    unsigned seed = 7u;
    bool agree = true;

    sk_camera3d_init();
    sk_scene_init();
    const sk_handle_t scene = sk_scene_create();
    for (int step = 0; step < STEPS; step++) {
        seed = seed * 1664525u + 1013904223u;
        const int i = (int)((seed >> 8) % HANDLES), op = (int)((seed >> 20) % 5);
        const sk_handle_t handle = (sk_handle_t)(1000u + (unsigned)i * 7919u); /* spread over the hash */
        if (op <= 1) {
            CHECK(sk_scene_add(scene, handle, (int)(seed % 4)));
            member[i] = true;
        } else if (op == 2) {
            agree = agree && sk_scene_remove(scene, handle) == member[i];
            member[i] = false;
        } else if (op == 3) {
            sk_scene_forget(handle);
            member[i] = false;
        } else {
            agree = agree && sk_scene_set_layer(scene, handle, (int)(seed % 4)) == member[i];
        }
        if (step % 97 == 0) {
            (void)sk_scene_pick(scene, 0, 1, 1); /* tidies */
            for (int j = 0; j < HANDLES; j++) {
                const sk_handle_t h = (sk_handle_t)(1000u + (unsigned)j * 7919u);
                agree = agree && sk_scene_set_layer(scene, h, 1) == member[j];
            }
        }
    }
    CHECK(agree);
    sk_scene_clear(scene);
    CHECK(!sk_scene_set_layer(scene, (sk_handle_t)1000u, 0));
    sk_scene_destroy(scene);
    sk_scene_deinit();
    sk_camera3d_deinit();
}

/* Many items take the radix path: the same order as the comparison sort, ties included,
 * with negative depths (behind the camera) and repeats. */
void test_scene_sort_transparent_many(void)
{
    enum { N = 3000 };
    static sk_transparent_item_t items[N];
    unsigned seed = 3u;
    bool ordered = true;

    for (int i = 0; i < N; i++) {
        seed = seed * 1664525u + 1013904223u;
        items[i] = (sk_transparent_item_t){.handle = (sk_handle_t)(i + 1), .order = i,
                                           .depth = (float)((int)(seed >> 16) % 400 - 100) * 0.25f};
    }
    sk_scene_sort_transparent(items, N);
    for (int i = 1; i < N; i++) {
        ordered = ordered && (items[i - 1].depth > items[i].depth ||
                              (items[i - 1].depth == items[i].depth && items[i - 1].order < items[i].order));
    }
    CHECK(ordered);
}
