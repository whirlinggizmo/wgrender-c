#include "internal/wgr_internal.h"
#include "internal/wgr_scene.h"
#include "wgr_camera3d.h"
#include "wgr_logger.h"
#include "wgr_pick.h"
#include "wgr_scene.h"
#include "wgr_shape3d.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-3f

/* wgr_pick_object, pickable flags, stats, and scene picks skipping non-pickable
 * objects. No GPU needed: shapes pick on the CPU. The headless screen is 1x1
 * logical pixel, so (0.5, 0.5) is its center. */
void test_pick_object(void)
{
    wgr_camera3d_init();
    wgr_scene_init();
    wgr_shape3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);

    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_handle_t front = wgr_shape3d_create(), back = wgr_shape3d_create();
    wgr_shape3d_set_cube(front, 2, 2, 2);
    wgr_shape3d_set_cube(back, 2, 2, 2);
    wgr_shape3d_set_transform(back, 0, 0, -5, 0, 0, 0, 1, 1, 1);

    wgr_pick_reset_stats();
    wgr_pick_result_t r = wgr_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == front);
    CHECK_NEAR(r.distance, 8.99f, EPS); /* from the near plane (0.01 in front of the camera at z 10) to the face at z 1 */
    wgr_pick_stats_t stats = wgr_pick_get_stats();
    CHECK(stats.broadphase_tests == 1 && stats.broadphase_rejects == 0);
    CHECK(stats.narrowphase_tests == 1 && stats.narrowphase_hits == 1);

    /* a miss is rejected by the bounding box */
    wgr_shape3d_set_transform(front, 50, 0, 0, 0, 0, 0, 1, 1, 1);
    r = wgr_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(!r.hit && r.handle == 0);
    stats = wgr_pick_get_stats();
    CHECK(stats.broadphase_tests == 2 && stats.broadphase_rejects == 1);
    wgr_shape3d_set_transform(front, 0, 0, 0, 0, 0, 0, 1, 1, 1);

    /* not pickable: never hit, directly or in a scene, and it doesn't block what's behind */
    wgr_handle_t scene = wgr_scene_create();
    wgr_scene_add(scene, front, 0);
    wgr_scene_add(scene, back, 0);
    r = wgr_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == front);
    CHECK(wgr_shape3d_set_pickable(front, false));
    r = wgr_pick_object(front, camera, 0.5f, 0.5f);
    CHECK(!r.hit);
    r = wgr_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == back);
    CHECK_NEAR(r.distance, 13.99f, EPS);
    wgr_shape3d_set_visible(back, false); /* hidden objects aren't hit either */
    r = wgr_scene_pick(scene, camera, 0.5f, 0.5f);
    CHECK(!r.hit);

    wgr_pick_reset_stats();
    stats = wgr_pick_get_stats();
    CHECK(stats.broadphase_tests == 0 && stats.narrowphase_hits == 0);
    CHECK(!wgr_pick_object(0, camera, 0.5f, 0.5f).hit);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_shape3d_deinit();
    wgr_scene_deinit();
    wgr_camera3d_deinit();
}

/* Rectangles, circles, lines and line strips: building, bounds, picking. */
void test_shape_3d(void)
{
    wgr_camera3d_init();
    wgr_scene_init();
    wgr_shape3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);

    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);

    /* a rectangle facing the camera is hit at its plane; turned edge-on it isn't */
    wgr_handle_t rect = wgr_shape3d_create();
    CHECK(wgr_shape3d_set_rectangle(rect, 2, 1));
    wgr_pick_result_t r = wgr_pick_object(rect, camera, 0.5f, 0.5f);
    CHECK(r.hit);
    CHECK_NEAR(r.distance, 9.99f, EPS);
    CHECK_VEC3_NEAR(r.normal_world, 0, 0, 1, EPS);
    wgr_shape3d_set_transform(rect, 0.9f, 0, 0, 0, 0, 0, 1, 1, 1); /* still covers the center (half width 1) */
    CHECK(wgr_pick_object(rect, camera, 0.5f, 0.5f).hit);
    wgr_shape3d_set_transform(rect, 1.1f, 0, 0, 0, 0, 0, 1, 1, 1); /* now just misses */
    CHECK(!wgr_pick_object(rect, camera, 0.5f, 0.5f).hit);
    wgr_shape3d_set_transform(rect, 0, 0, 0, 0, 1.5707963f, 0, 1, 1, 1); /* edge-on */
    CHECK(!wgr_pick_object(rect, camera, 0.5f, 0.5f).hit);

    /* circles are picked anywhere inside the outline */
    wgr_handle_t circle = wgr_shape3d_create();
    CHECK(wgr_shape3d_set_circle(circle, 1));
    wgr_shape3d_set_transform(circle, 0.7f, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(wgr_pick_object(circle, camera, 0.5f, 0.5f).hit);
    wgr_shape3d_set_transform(circle, 1.2f, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(!wgr_pick_object(circle, camera, 0.5f, 0.5f).hit);

    /* lines have no area */
    wgr_handle_t line = wgr_shape3d_create();
    CHECK(wgr_shape3d_set_line(line, -1, 0, 0, 1, 0, 0));
    CHECK(!wgr_pick_object(line, camera, 0.5f, 0.5f).hit);

    /* strips: built point by point, emptied by set_line_strip */
    wgr_handle_t strip = wgr_shape3d_create();
    CHECK(!wgr_shape3d_add_point(strip, 0, 0, 0)); /* not a strip yet */
    CHECK(wgr_shape3d_set_line_strip(strip));
    for (int i = 0; i < 100; i++) {
        CHECK(wgr_shape3d_add_point(strip, (float)i, (float)(i % 3), 0));
    }
    CHECK(wgr_shape3d_get_point_count(strip) == 100);
    vec3_t lmin, lmax;
    wgr_mat4_t model;
    CHECK(wgr_drawable_bounds(strip, &lmin, &lmax, &model));
    CHECK_VEC3_NEAR(lmin, 0, 0, 0, EPS);
    CHECK_VEC3_NEAR(lmax, 99, 2, 0, EPS);
    CHECK(wgr_shape3d_set_line_strip(strip));
    CHECK(wgr_shape3d_get_point_count(strip) == 0);
    CHECK(!wgr_drawable_bounds(strip, &lmin, &lmax, &model)); /* empty: nothing to pick */
    CHECK(wgr_shape3d_set_cube(strip, 1, 1, 1));
    CHECK(wgr_shape3d_get_point_count(strip) == 0);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_shape3d_deinit();
    wgr_scene_deinit();
    wgr_camera3d_deinit();
}
