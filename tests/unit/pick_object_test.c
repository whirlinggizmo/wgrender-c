#include "internal/sk_internal.h"
#include "internal/sk_scene.h"
#include "sk_camera3d.h"
#include "sk_logger.h"
#include "sk_pick.h"
#include "sk_scene.h"
#include "sk_shape3d.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-3f

/* sk_pick_object, pickable flags, stats, and scene picks skipping non-pickable
 * objects. No GPU needed: shapes pick on the CPU. The headless screen is 1x1
 * logical pixel, so (0.5, 0.5) is its center. */
void test_pick_object(void)
{
    sk_color_init();
    sk_camera3d_init();
    sk_scene_init();
    sk_shape3d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);

    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t front = sk_shape3d_create(), back = sk_shape3d_create();
    sk_shape3d_set_cube(front, 2, 2, 2);
    sk_shape3d_set_cube(back, 2, 2, 2);
    sk_shape3d_set_transform(back, 0, 0, -5, 0, 0, 0, 1, 1, 1);

    sk_pick_reset_stats();
    sk_pick_result_t r = sk_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == front);
    CHECK_NEAR(r.distance, 8.99f, EPS); /* from the near plane (0.01 in front of the camera at z 10) to the face at z 1 */
    sk_pick_stats_t stats = sk_pick_get_stats();
    CHECK(stats.broadphase_tests == 1 && stats.broadphase_rejects == 0);
    CHECK(stats.narrowphase_tests == 1 && stats.narrowphase_hits == 1);

    /* a miss is rejected by the bounding box */
    sk_shape3d_set_transform(front, 50, 0, 0, 0, 0, 0, 1, 1, 1);
    r = sk_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(!r.hit && r.handle == 0);
    stats = sk_pick_get_stats();
    CHECK(stats.broadphase_tests == 2 && stats.broadphase_rejects == 1);
    sk_shape3d_set_transform(front, 0, 0, 0, 0, 0, 0, 1, 1, 1);

    /* not pickable: never hit, directly or in a scene, and it doesn't block what's behind */
    sk_handle_t scene = sk_scene_create();
    sk_scene_add(scene, front, 0);
    sk_scene_add(scene, back, 0);
    r = sk_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == front);
    CHECK(sk_shape3d_set_pickable(front, false));
    r = sk_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(!r.hit);
    r = sk_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == back);
    CHECK_NEAR(r.distance, 13.99f, EPS);
    sk_shape3d_set_visible(back, false); /* hidden objects aren't hit either */
    r = sk_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(!r.hit);

    sk_pick_reset_stats();
    stats = sk_pick_get_stats();
    CHECK(stats.broadphase_tests == 0 && stats.narrowphase_hits == 0);
    CHECK(!sk_pick_object(0, camera, 0.5f, 0.5f).hit);

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_shape3d_deinit();
    sk_scene_deinit();
    sk_camera3d_deinit();
    sk_color_deinit();
}

/* Rectangles, circles, lines and line strips: building, bounds, picking. */
void test_shape_3d(void)
{
    sk_color_init();
    sk_camera3d_init();
    sk_scene_init();
    sk_shape3d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);

    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);

    /* a rectangle facing the camera is hit at its plane; turned edge-on it isn't */
    sk_handle_t rect = sk_shape3d_create();
    CHECK(sk_shape3d_set_rectangle(rect, 2, 1));
    sk_pick_result_t r = sk_pick_object(rect, camera, 0.5f, 0.5f);
    CHECK(r.hit);
    CHECK_NEAR(r.distance, 9.99f, EPS);
    CHECK_VEC3_NEAR(r.normal_world, 0, 0, 1, EPS);
    sk_shape3d_set_transform(rect, 0.9f, 0, 0, 0, 0, 0, 1, 1, 1); /* still covers the center (half width 1) */
    CHECK(sk_pick_object(rect, camera, 0.5f, 0.5f).hit);
    sk_shape3d_set_transform(rect, 1.1f, 0, 0, 0, 0, 0, 1, 1, 1); /* now just misses */
    CHECK(!sk_pick_object(rect, camera, 0.5f, 0.5f).hit);
    sk_shape3d_set_transform(rect, 0, 0, 0, 0, 1.5707963f, 0, 1, 1, 1); /* edge-on */
    CHECK(!sk_pick_object(rect, camera, 0.5f, 0.5f).hit);

    /* circles are picked anywhere inside the outline */
    sk_handle_t circle = sk_shape3d_create();
    CHECK(sk_shape3d_set_circle(circle, 1));
    sk_shape3d_set_transform(circle, 0.7f, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(sk_pick_object(circle, camera, 0.5f, 0.5f).hit);
    sk_shape3d_set_transform(circle, 1.2f, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(!sk_pick_object(circle, camera, 0.5f, 0.5f).hit);

    /* lines have no area */
    sk_handle_t line = sk_shape3d_create();
    CHECK(sk_shape3d_set_line(line, -1, 0, 0, 1, 0, 0));
    CHECK(!sk_pick_object(line, camera, 0.5f, 0.5f).hit);

    /* strips: built point by point, emptied by set_line_strip */
    sk_handle_t strip = sk_shape3d_create();
    CHECK(!sk_shape3d_add_point(strip, 0, 0, 0)); /* not a strip yet */
    CHECK(sk_shape3d_set_line_strip(strip));
    for (int i = 0; i < 100; i++) {
        CHECK(sk_shape3d_add_point(strip, (float)i, (float)(i % 3), 0));
    }
    CHECK(sk_shape3d_get_point_count(strip) == 100);
    vec3_t lmin, lmax;
    sk_mat4_t model;
    CHECK(sk_drawable_bounds(strip, &lmin, &lmax, &model));
    CHECK_VEC3_NEAR(lmin, 0, 0, 0, EPS);
    CHECK_VEC3_NEAR(lmax, 99, 2, 0, EPS);
    CHECK(sk_shape3d_set_line_strip(strip));
    CHECK(sk_shape3d_get_point_count(strip) == 0);
    CHECK(!sk_drawable_bounds(strip, &lmin, &lmax, &model)); /* empty: nothing to pick */
    CHECK(sk_shape3d_set_cube(strip, 1, 1, 1));
    CHECK(sk_shape3d_get_point_count(strip) == 0);

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_shape3d_deinit();
    sk_scene_deinit();
    sk_camera3d_deinit();
    sk_color_deinit();
}
