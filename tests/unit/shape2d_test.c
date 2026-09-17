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
#include "sk_shape.h"
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
    sk_color_init();
    sk_camera3d_init();
    sk_shape_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(sk_window_set_size(800, 600));

    /* rounded rectangle: 100x50 from (10, 20), corners of radius 10 */
    sk_handle_t rect = sk_shape_create();
    CHECK(sk_shape_set_rectangle_2d(rect, 100, 50, 10));
    CHECK(sk_shape_set_transform_2d(rect, 10, 20, 0, 1, 1));
    CHECK(hit(rect, 60, 45));   /* middle */
    CHECK(hit(rect, 11, 45));   /* left edge */
    CHECK(!hit(rect, 11, 21));  /* cut off by the rounded corner */
    CHECK(hit(rect, 13, 23));   /* inside the corner arc */
    CHECK(!hit(rect, 9, 45));   /* outside */
    CHECK(hit(rect, 60, 45) && sk_pick_object(rect, 0, 60, 45).handle == rect);
    CHECK(sk_shape_set_outline(rect, 5));
    CHECK(!hit(rect, 60, 45));  /* an outline's middle is empty */
    CHECK(hit(rect, 12, 45));   /* within 5 of the edge */
    CHECK(!hit(rect, 16, 45));

    /* rotated a quarter turn around its top-left corner: 100 wide becomes 100 down */
    sk_handle_t bar = sk_shape_create();
    sk_shape_set_rectangle_2d(bar, 100, 20, 0);
    sk_shape_set_transform_2d(bar, 200, 200, 1.5707963f, 1, 1);
    CHECK(hit(bar, 190, 250));
    CHECK(!hit(bar, 250, 210));

    /* scaled */
    sk_handle_t square = sk_shape_create();
    sk_shape_set_rectangle_2d(square, 10, 10, 0);
    sk_shape_set_transform_2d(square, 0, 0, 0, 2, 2);
    CHECK(hit(square, 15, 15));
    CHECK(!hit(square, 21, 15));

    /* outlined circle */
    sk_handle_t ring = sk_shape_create();
    sk_shape_set_circle_2d(ring, 20);
    sk_shape_set_transform_2d(ring, 300, 300, 0, 1, 1);
    CHECK(hit(ring, 300, 300));
    sk_shape_set_outline(ring, 4);
    CHECK(!hit(ring, 300, 300));
    CHECK(hit(ring, 318, 300));
    CHECK(!hit(ring, 321, 300));

    /* thick line */
    sk_handle_t line = sk_shape_create();
    CHECK(sk_shape_set_line_2d(line, 0, 0, 100, 0, 6));
    sk_shape_set_transform_2d(line, 400, 400, 0, 1, 1);
    CHECK(hit(line, 450, 402));
    CHECK(!hit(line, 450, 404));
    CHECK(!hit(line, 505, 400)); /* past the end */

    /* 2D over 3D in one scene; the 3D shape is still picked through the camera */
    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t cube = sk_shape_create();
    sk_shape_set_cube(cube, 2, 2, 2);
    sk_handle_t panel = sk_shape_create();
    sk_shape_set_rectangle_2d(panel, 40, 40, 0);
    sk_shape_set_transform_2d(panel, 380, 280, 0, 1, 1);
    sk_handle_t scene = sk_scene_create();
    sk_scene_set_active_camera(scene, camera);
    sk_scene_add(scene, cube, 0);
    sk_scene_add(scene, panel, 0);
    CHECK(sk_scene_pick(scene, 0, 400, 300).handle == panel);
    sk_shape_set_visible(panel, false);
    CHECK(sk_scene_pick(scene, 0, 400, 300).handle == cube);
    CHECK(sk_pick_object(cube, camera, 400, 300).hit);
    CHECK(!sk_pick_object(panel, camera, 400, 300).hit); /* hidden */
    sk_scene_draw(scene);                                /* draws both kinds without trouble */

    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_scene_destroy(scene);
    sk_shape_deinit();
    sk_camera3d_deinit();
    sk_color_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
