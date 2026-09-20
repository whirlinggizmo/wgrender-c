#include <math.h>

/* Retained 2D shapes (docs/PLAN-2d.md step 2): exact-area picking under the 2D
 * transform, and 2D/3D routing (a shape handle is one or the other). */
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "wgr_camera3d.h"
#include "wgr_logger.h"
#include "wgr_pick.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "wgr_shape2d.h"
#include "wgr_shape3d.h"
#include "wgr_window.h"
#include "test.h"
#include "tests.h"

#include "internal/wgr_shape2d_internal.h"
#include "wgr_color.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

static bool hit(wgr_handle_t shape, float x, float y)
{
    return wgr_pick_object(shape, 0, x, y).hit;
}

void test_shape2d(void)
{
    const vec2_t screen = wgr_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_shape2d_init();
    wgri_shape3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(wgr_window_set_size(800, 600));

    /* rounded rectangle: 100x50 from (10, 20), corners of radius 10 */
    wgr_handle_t rect = wgr_shape2d_create();
    CHECK(wgr_shape2d_set_rectangle(rect, 100, 50, 10));
    CHECK(wgr_shape2d_set_transform(rect, 10, 20, 0, 1, 1));
    CHECK(hit(rect, 60, 45));   /* middle */
    CHECK(hit(rect, 11, 45));   /* left edge */
    CHECK(!hit(rect, 11, 21));  /* cut off by the rounded corner */
    CHECK(hit(rect, 13, 23));   /* inside the corner arc */
    CHECK(!hit(rect, 9, 45));   /* outside */
    CHECK(hit(rect, 60, 45) && wgr_pick_object(rect, 0, 60, 45).handle == rect);
    CHECK(wgr_shape2d_set_outline(rect, 5));
    CHECK(!hit(rect, 60, 45));  /* an outline's middle is empty */
    CHECK(hit(rect, 12, 45));   /* within 5 of the edge */
    CHECK(!hit(rect, 16, 45));

    /* pivot: a fraction of the bounds, and rotation turns around it */
    wgr_handle_t card = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(card, 100, 50, 0);
    wgr_shape2d_set_transform(card, 500, 100, 0, 1, 1);
    CHECK(hit(card, 540, 120));  /* top-left origin by default */
    CHECK(!hit(card, 460, 90));
    CHECK(wgr_shape2d_set_pivot(card, 0.5f, 0.5f));
    CHECK(hit(card, 460, 90));   /* centered on its position now */
    CHECK(hit(card, 540, 120));
    CHECK(!hit(card, 560, 100));
    wgr_shape2d_set_transform(card, 500, 100, 1.5707963f, 1, 1); /* a quarter turn about the center */
    CHECK(hit(card, 500, 140));
    CHECK(!hit(card, 540, 100));

    /* a circle's default pivot is its center; (0, 0) moves it to the bounds' corner */
    wgr_handle_t dot = wgr_shape2d_create();
    wgr_shape2d_set_circle(dot, 10);
    wgr_shape2d_set_transform(dot, 600, 100, 0, 1, 1);
    CHECK(hit(dot, 600, 100));
    CHECK(wgr_shape2d_set_pivot(dot, 0.0f, 0.0f));
    CHECK(!hit(dot, 600, 100));
    CHECK(hit(dot, 610, 110)); /* center is now a radius down and right */

    /* lines keep their explicit endpoints */
    wgr_handle_t wire = wgr_shape2d_create();
    wgr_shape2d_set_line(wire, 0, 0, 50, 0, 4);
    wgr_shape2d_set_transform(wire, 600, 300, 0, 1, 1);
    CHECK(wgr_shape2d_set_pivot(wire, 0.5f, 0.5f));
    CHECK(hit(wire, 625, 300));

    /* rotated a quarter turn around its top-left corner: 100 wide becomes 100 down */
    wgr_handle_t bar = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(bar, 100, 20, 0);
    wgr_shape2d_set_transform(bar, 200, 200, 1.5707963f, 1, 1);
    CHECK(hit(bar, 190, 250));
    CHECK(!hit(bar, 250, 210));

    /* scaled */
    wgr_handle_t square = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(square, 10, 10, 0);
    wgr_shape2d_set_transform(square, 0, 0, 0, 2, 2);
    CHECK(hit(square, 15, 15));
    CHECK(!hit(square, 21, 15));

    /* outlined circle */
    wgr_handle_t ring = wgr_shape2d_create();
    wgr_shape2d_set_circle(ring, 20);
    wgr_shape2d_set_transform(ring, 300, 300, 0, 1, 1);
    CHECK(hit(ring, 300, 300));
    wgr_shape2d_set_outline(ring, 4);
    CHECK(!hit(ring, 300, 300));
    CHECK(hit(ring, 318, 300));
    CHECK(!hit(ring, 321, 300));

    /* thick line */
    wgr_handle_t line = wgr_shape2d_create();
    CHECK(wgr_shape2d_set_line(line, 0, 0, 100, 0, 6));
    wgr_shape2d_set_transform(line, 400, 400, 0, 1, 1);
    CHECK(hit(line, 450, 402));
    CHECK(!hit(line, 450, 404));
    CHECK(!hit(line, 505, 400)); /* past the end */

    /* 2D over 3D in one scene; the 3D shape is still picked through the camera */
    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_handle_t cube = wgr_shape3d_create();
    wgr_shape3d_set_cube(cube, 2, 2, 2);
    wgr_handle_t panel = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(panel, 40, 40, 0);
    wgr_shape2d_set_transform(panel, 380, 280, 0, 1, 1);
    wgr_handle_t scene = wgr_scene_create();
    wgr_scene_set_active_camera(scene, camera);
    wgr_scene_add(scene, cube, 0);
    wgr_scene_add(scene, panel, 0);
    CHECK(wgr_scene_pick(scene, 0, 400, 300).handle == panel);
    wgr_shape2d_set_visible(panel, false);
    CHECK(wgr_scene_pick(scene, 0, 400, 300).handle == cube);
    CHECK(wgr_pick_object(cube, camera, 400, 300).hit);
    CHECK(!wgr_pick_object(panel, camera, 400, 300).hit); /* hidden */
    wgr_scene_draw(scene);                                /* draws both kinds without trouble */

    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_scene_destroy(scene);
    wgri_shape2d_deinit();
    wgri_shape3d_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}

/* Immediate rounded rectangles and borders (docs/PLAN-ui.md): the shared outline's
 * geometry, and what each draw emits (sokol_gl vertex counts on the dummy backend). */
void test_shape2d_immediate(void)
{
    float xy[WGRI_SHAPE2D_OUTLINE_POINTS * 2];
    const int per_corner = WGRI_SHAPE2D_OUTLINE_POINTS / 4;

    /* square corners: every point of a corner's arc sits on the corner */
    const float square[4] = {0, 0, 0, 0};
    CHECK(wgri_shape2d_rounded_outline(10, 20, 100, 50, square, xy) == WGRI_SHAPE2D_OUTLINE_POINTS);
    CHECK_NEAR(xy[0], 10, 1e-4);                                /* top-left */
    CHECK_NEAR(xy[1], 20, 1e-4);
    CHECK_NEAR(xy[per_corner * 2], 110, 1e-4);                  /* top-right */
    CHECK_NEAR(xy[per_corner * 2 + 1], 20, 1e-4);
    CHECK_NEAR(xy[2 * per_corner * 2], 110, 1e-4);              /* bottom-right */
    CHECK_NEAR(xy[2 * per_corner * 2 + 1], 70, 1e-4);

    /* per-corner radii, the outline stays inside the rectangle */
    const float mixed[4] = {10, 0, 5, 20};
    wgri_shape2d_rounded_outline(0, 0, 100, 50, mixed, xy);
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (int i = 0; i < WGRI_SHAPE2D_OUTLINE_POINTS; i++) {
        min_x = fminf(min_x, xy[i * 2]), max_x = fmaxf(max_x, xy[i * 2]);
        min_y = fminf(min_y, xy[i * 2 + 1]), max_y = fmaxf(max_y, xy[i * 2 + 1]);
    }
    CHECK_NEAR(min_x, 0, 1e-4);
    CHECK_NEAR(max_x, 100, 1e-4);
    CHECK_NEAR(min_y, 0, 1e-4);
    CHECK_NEAR(max_y, 50, 1e-4);
    CHECK_NEAR(xy[0], 0, 1e-4);   /* the top-left arc starts on the left edge ... */
    CHECK_NEAR(xy[1], 10, 1e-4);  /* ... 10 down: radius 10 */
    CHECK_NEAR(xy[per_corner * 2], 100, 1e-4); /* top-right is square */
    CHECK_NEAR(xy[per_corner * 2 + 1], 0, 1e-4);

    /* radii clamp to half the shorter side: 10x4 with radius 10 rounds by 2 */
    const float huge[4] = {10, 10, 10, 10};
    wgri_shape2d_rounded_outline(0, 0, 10, 4, huge, xy);
    CHECK_NEAR(xy[1], 2, 1e-4);
    CHECK_NEAR(xy[(per_corner - 1) * 2], 2, 1e-4);

    /* what the draws emit */
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_render_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    wgr_render_begin();
    int before = sgl_num_vertices();
    wgr_shape2d_draw_rounded_rectangle(10.5f, 20.25f, 100, 50, 8, 8, 8, 8, WGR_COLOR_RED); /* fractional: floats */
    const int fan = sgl_num_vertices() - before;
    CHECK(fan == WGRI_SHAPE2D_OUTLINE_POINTS * 3); /* a triangle per outline edge */

    before = sgl_num_vertices();
    wgr_shape2d_draw_border(10, 20, 100, 50, 2, 2, 2, 2, 8, 8, 8, 8, WGR_COLOR_RED);
    CHECK(sgl_num_vertices() - before == WGRI_SHAPE2D_OUTLINE_POINTS * 6); /* two per band segment */

    before = sgl_num_vertices(); /* nothing to draw: no vertices */
    wgr_shape2d_draw_rounded_rectangle(0, 0, 0, 50, 4, 4, 4, 4, WGR_COLOR_RED);
    wgr_shape2d_draw_border(0, 0, 100, 50, 0, 0, 0, 0, 4, 4, 4, 4, WGR_COLOR_RED);
    wgr_shape2d_draw_border(0, 0, 100, -5, 2, 2, 2, 2, 4, 4, 4, 4, WGR_COLOR_RED);
    CHECK(sgl_num_vertices() == before);

    wgr_shape2d_draw_border(0, 0, 10, 10, 50, 50, 50, 50, 0, 0, 0, 0, WGR_COLOR_RED); /* wider than the box: fills it */
    CHECK(sgl_num_vertices() - before == WGRI_SHAPE2D_OUTLINE_POINTS * 6);

    before = sgl_num_vertices();
    wgr_shape2d_draw_rectangle(0.5f, 0.5f, 10.25f, 4.75f, WGR_COLOR_RED);
    CHECK(sgl_num_vertices() - before == 6); /* one quad */
    wgr_render_end();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_render_deinit();
    sg_shutdown();
}
