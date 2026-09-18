/* Retained 2D shapes (docs/PLAN-2d.md step 2): exact-area picking under the 2D
 * transform, and 2D/3D routing (a shape handle is one or the other). */
#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "sk_camera3d.h"
#include "sk_logger.h"
#include "sk_pick.h"
#include "sk_scene.h"
#include "sk_shape2d.h"
#include "sk_shape3d.h"
#include "sk_window.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

static bool hit(sk_handle_t shape, float x, float y)
{
    return sk_pick_object(shape, 0, x, y).hit;
}

void test_shape2d(void)
{
    const vec2_t screen = sk_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_shape2d_init();
    sk_shape3d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(sk_window_set_size(800, 600));

    /* rounded rectangle: 100x50 from (10, 20), corners of radius 10 */
    sk_handle_t rect = sk_shape2d_create();
    CHECK(sk_shape2d_set_rectangle(rect, 100, 50, 10));
    CHECK(sk_shape2d_set_transform(rect, 10, 20, 0, 1, 1));
    CHECK(hit(rect, 60, 45));   /* middle */
    CHECK(hit(rect, 11, 45));   /* left edge */
    CHECK(!hit(rect, 11, 21));  /* cut off by the rounded corner */
    CHECK(hit(rect, 13, 23));   /* inside the corner arc */
    CHECK(!hit(rect, 9, 45));   /* outside */
    CHECK(hit(rect, 60, 45) && sk_pick_object(rect, 0, 60, 45).handle == rect);
    CHECK(sk_shape2d_set_outline(rect, 5));
    CHECK(!hit(rect, 60, 45));  /* an outline's middle is empty */
    CHECK(hit(rect, 12, 45));   /* within 5 of the edge */
    CHECK(!hit(rect, 16, 45));

    /* pivot: a fraction of the bounds, and rotation turns around it */
    sk_handle_t card = sk_shape2d_create();
    sk_shape2d_set_rectangle(card, 100, 50, 0);
    sk_shape2d_set_transform(card, 500, 100, 0, 1, 1);
    CHECK(hit(card, 540, 120));  /* top-left origin by default */
    CHECK(!hit(card, 460, 90));
    CHECK(sk_shape2d_set_pivot(card, 0.5f, 0.5f));
    CHECK(hit(card, 460, 90));   /* centered on its position now */
    CHECK(hit(card, 540, 120));
    CHECK(!hit(card, 560, 100));
    sk_shape2d_set_transform(card, 500, 100, 1.5707963f, 1, 1); /* a quarter turn about the center */
    CHECK(hit(card, 500, 140));
    CHECK(!hit(card, 540, 100));

    /* a circle's default pivot is its center; (0, 0) moves it to the bounds' corner */
    sk_handle_t dot = sk_shape2d_create();
    sk_shape2d_set_circle(dot, 10);
    sk_shape2d_set_transform(dot, 600, 100, 0, 1, 1);
    CHECK(hit(dot, 600, 100));
    CHECK(sk_shape2d_set_pivot(dot, 0.0f, 0.0f));
    CHECK(!hit(dot, 600, 100));
    CHECK(hit(dot, 610, 110)); /* center is now a radius down and right */

    /* lines keep their explicit endpoints */
    sk_handle_t wire = sk_shape2d_create();
    sk_shape2d_set_line(wire, 0, 0, 50, 0, 4);
    sk_shape2d_set_transform(wire, 600, 300, 0, 1, 1);
    CHECK(sk_shape2d_set_pivot(wire, 0.5f, 0.5f));
    CHECK(hit(wire, 625, 300));

    /* rotated a quarter turn around its top-left corner: 100 wide becomes 100 down */
    sk_handle_t bar = sk_shape2d_create();
    sk_shape2d_set_rectangle(bar, 100, 20, 0);
    sk_shape2d_set_transform(bar, 200, 200, 1.5707963f, 1, 1);
    CHECK(hit(bar, 190, 250));
    CHECK(!hit(bar, 250, 210));

    /* scaled */
    sk_handle_t square = sk_shape2d_create();
    sk_shape2d_set_rectangle(square, 10, 10, 0);
    sk_shape2d_set_transform(square, 0, 0, 0, 2, 2);
    CHECK(hit(square, 15, 15));
    CHECK(!hit(square, 21, 15));

    /* outlined circle */
    sk_handle_t ring = sk_shape2d_create();
    sk_shape2d_set_circle(ring, 20);
    sk_shape2d_set_transform(ring, 300, 300, 0, 1, 1);
    CHECK(hit(ring, 300, 300));
    sk_shape2d_set_outline(ring, 4);
    CHECK(!hit(ring, 300, 300));
    CHECK(hit(ring, 318, 300));
    CHECK(!hit(ring, 321, 300));

    /* thick line */
    sk_handle_t line = sk_shape2d_create();
    CHECK(sk_shape2d_set_line(line, 0, 0, 100, 0, 6));
    sk_shape2d_set_transform(line, 400, 400, 0, 1, 1);
    CHECK(hit(line, 450, 402));
    CHECK(!hit(line, 450, 404));
    CHECK(!hit(line, 505, 400)); /* past the end */

    /* 2D over 3D in one scene; the 3D shape is still picked through the camera */
    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t cube = sk_shape3d_create();
    sk_shape3d_set_cube(cube, 2, 2, 2);
    sk_handle_t panel = sk_shape2d_create();
    sk_shape2d_set_rectangle(panel, 40, 40, 0);
    sk_shape2d_set_transform(panel, 380, 280, 0, 1, 1);
    sk_handle_t scene = sk_scene_create();
    sk_scene_set_active_camera(scene, camera);
    sk_scene_add(scene, cube, 0);
    sk_scene_add(scene, panel, 0);
    CHECK(sk_scene_pick(scene, 0, 400, 300).handle == panel);
    sk_shape2d_set_visible(panel, false);
    CHECK(sk_scene_pick(scene, 0, 400, 300).handle == cube);
    CHECK(sk_pick_object(cube, camera, 400, 300).hit);
    CHECK(!sk_pick_object(panel, camera, 400, 300).hit); /* hidden */
    sk_scene_draw(scene);                                /* draws both kinds without trouble */

    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_scene_destroy(scene);
    sk_shape2d_deinit();
    sk_shape3d_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
