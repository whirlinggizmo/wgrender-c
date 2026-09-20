#include <time.h>
/* UI essentials (docs/PLAN-2d.md step 3): nine-slice geometry, text2d wrapping and
 * alignment, and per-layer clip rectangles (which also mask picking). */
#include <string.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_font_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite2d_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_sprite_batch_internal.h"
#include "wgr_camera3d.h"
#include "wgr_logger.h"
#include "wgr_pick.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "wgr.h"
#include "wgr_shape2d.h"
#include "wgr_shape3d.h"
#include "wgr_sprite2d.h"
#include "wgr_sprite3d.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_text.h"
#include "wgr_color.h"
#include "wgr_text2d.h"
#include "wgr_text3d.h"
#include "wgr_texture.h"
#include "wgr_window.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

void test_nine_slice(void)
{
    float dest[2], source[2];

    /* 16 px borders of a 64 px region, drawn 256 px wide: the borders keep their
     * size on screen (16/256) and take a quarter of the source each */
    CHECK(wgr_sprite2d_nine_slice_axis(16, 16, 256, 64, dest, source));
    CHECK_NEAR(dest[0], 16.0f / 256.0f, 1e-6);
    CHECK_NEAR(dest[1], 1.0f - 16.0f / 256.0f, 1e-6);
    CHECK_NEAR(source[0], 0.25f, 1e-6);
    CHECK_NEAR(source[1], 0.75f, 1e-6);

    /* uneven borders */
    CHECK(wgr_sprite2d_nine_slice_axis(8, 24, 100, 64, dest, source));
    CHECK_NEAR(dest[0], 0.08f, 1e-6);
    CHECK_NEAR(dest[1], 0.76f, 1e-6);
    CHECK_NEAR(source[0], 0.125f, 1e-6);
    CHECK_NEAR(source[1], 0.625f, 1e-6);

    /* one-sided: the other edge stays at the end of the sprite */
    CHECK(wgr_sprite2d_nine_slice_axis(10, 0, 50, 20, dest, source));
    CHECK_NEAR(dest[0], 0.2f, 1e-6);
    CHECK_NEAR(dest[1], 1.0f, 1e-6);
    CHECK_NEAR(source[1], 1.0f, 1e-6);

    /* drawn smaller than its borders: they shrink to fill it, in order */
    CHECK(wgr_sprite2d_nine_slice_axis(16, 16, 24, 64, dest, source));
    CHECK_NEAR(dest[0], 0.5f, 1e-6);
    CHECK_NEAR(dest[1], 0.5f, 1e-6);
    CHECK(dest[0] <= dest[1]);
    CHECK_NEAR(source[0], 0.25f, 1e-6); /* the source split is unchanged */

    /* borders wider than the source region share it */
    CHECK(wgr_sprite2d_nine_slice_axis(30, 10, 200, 20, dest, source));
    CHECK_NEAR(source[0], 0.75f, 1e-6);
    CHECK_NEAR(source[1], 0.75f, 1e-6);

    /* no borders, or nothing to draw into */
    CHECK(!wgr_sprite2d_nine_slice_axis(0, 0, 100, 64, dest, source));
    CHECK(!wgr_sprite2d_nine_slice_axis(8, 8, 0, 64, dest, source));
    CHECK(!wgr_sprite2d_nine_slice_axis(8, 8, 100, 0, dest, source));
}

void test_text2d_layout(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_font_init();
    wgr_text_init();
    wgr_text2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    wgr_handle_t text = wgr_text2d_create(0);
    CHECK(wgr_text2d_set_text(text, "one two three four"));
    CHECK(wgr_text2d_set_size(text, 16));
    CHECK(wgr_text2d_set_position(text, 100, 50));

    const float full_width = wgr_text2d_measure_width(text);
    const float line_height = wgr_text2d_measure_height(text);
    CHECK(full_width > 0.0f && line_height > 0.0f);

    /* wrapped: the block is the box wide and taller, by whole lines */
    CHECK(wgr_text2d_set_max_width(text, full_width * 0.5f));
    const float wrapped_height = wgr_text2d_measure_height(text);
    CHECK(wgr_text2d_measure_width(text) < full_width);
    CHECK_NEAR(wrapped_height, line_height * 3.0f, 0.01); /* "one two" / "three" / "four" */

    /* a word longer than the box keeps its own line (no mid-word break) */
    CHECK(wgr_text2d_set_text(text, "unbreakable"));
    CHECK(wgr_text2d_set_max_width(text, 10));
    CHECK_NEAR(wgr_text2d_measure_height(text), line_height, 0.01);
    CHECK(wgr_text2d_measure_width(text) > 10.0f);

    /* newlines always break */
    CHECK(wgr_text2d_set_max_width(text, 0));
    CHECK(wgr_text2d_set_text(text, "a\nb\nc"));
    CHECK_NEAR(wgr_text2d_measure_height(text), line_height * 3.0f, 0.01);

    /* alignment moves the block around the position: picks follow it */
    CHECK(wgr_text2d_set_text(text, "pick me"));
    const float w = wgr_text2d_measure_width(text), h = wgr_text2d_measure_height(text);
    CHECK(wgr_pick_object(text, 0, 100 + w * 0.5f, 50 + h * 0.5f).hit); /* left/top by default */
    CHECK(!wgr_pick_object(text, 0, 100 - w * 0.25f, 50 + h * 0.5f).hit);

    CHECK(wgr_text2d_set_align(text, WGR_TEXT_ALIGN_CENTER, WGR_TEXT_ALIGN_MIDDLE));
    CHECK(wgr_pick_object(text, 0, 100 - w * 0.25f, 50 - h * 0.25f).hit);
    CHECK(wgr_pick_object(text, 0, 100 + w * 0.25f, 50 + h * 0.25f).hit);
    CHECK(!wgr_pick_object(text, 0, 100 + w * 0.75f, 50).hit);

    CHECK(wgr_text2d_set_align(text, WGR_TEXT_ALIGN_RIGHT, WGR_TEXT_ALIGN_BOTTOM));
    CHECK(wgr_pick_object(text, 0, 100 - w * 0.5f, 50 - h * 0.5f).hit);
    CHECK(!wgr_pick_object(text, 0, 100 + 1.0f, 50 - h * 0.5f).hit);

    /* an axis only takes its own values */
    CHECK(!wgr_text2d_set_align(text, WGR_TEXT_ALIGN_TOP, WGR_TEXT_ALIGN_MIDDLE));
    CHECK(!wgr_text2d_set_align(text, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_CENTER));

    /* a wrapped block is its box wide for alignment, however short the lines are */
    CHECK(wgr_text2d_set_align(text, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_TOP));
    CHECK(wgr_text2d_set_text(text, "hi"));
    CHECK(wgr_text2d_set_max_width(text, 400));
    CHECK(wgr_pick_object(text, 0, 100 + 300, 50 + h * 0.5f).hit);
    CHECK(!wgr_pick_object(text, 0, 100 + 500, 50 + h * 0.5f).hit);

    wgr_text2d_draw(text); /* wrapping and alignment draw without trouble */
    wgr_text2d_destroy(text);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_text2d_deinit();
    wgr_font_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

void test_scene_clip(void)
{
    const vec2_t screen = wgr_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_shape2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(wgr_window_set_size(800, 600));

    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 5, 0, 0, 0, 0, 1, 0);
    const wgr_handle_t scene = wgr_scene_create();
    wgr_scene_set_active_camera(scene, camera);

    /* a row of 2D panels on layer 1, inside a 200x100 window at (100, 100) */
    const wgr_handle_t inside = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(inside, 60, 40, 0);
    wgr_shape2d_set_transform(inside, 120, 120, 0, 1, 1);
    const wgr_handle_t outside = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(outside, 60, 40, 0);
    wgr_shape2d_set_transform(outside, 120, 260, 0, 1, 1); /* below the window */
    wgr_scene_add(scene, inside, 1);
    wgr_scene_add(scene, outside, 1);

    CHECK(wgr_scene_pick(scene, 0, 150, 140).handle == inside);
    CHECK(wgr_scene_pick(scene, 0, 150, 280).handle == outside);

    CHECK(wgr_scene_set_clip(scene, 1, 100, 100, 200, 100));
    CHECK(wgr_scene_pick(scene, 0, 150, 140).handle == inside);
    CHECK(wgr_scene_pick(scene, 0, 150, 280).handle == 0); /* clipped away: not picked */

    /* another layer isn't clipped */
    const wgr_handle_t other = wgr_shape2d_create();
    wgr_shape2d_set_rectangle(other, 60, 40, 0);
    wgr_shape2d_set_transform(other, 400, 400, 0, 1, 1);
    wgr_scene_add(scene, other, 2);
    CHECK(wgr_scene_pick(scene, 0, 430, 420).handle == other);

    /* interaction picks the same way (a clipped member can't be hovered) */
    CHECK(wgr_scene_set_interactive(scene, true));
    wgr_scene_update_interaction();
    CHECK(wgr_scene_get_hovered(scene) == 0);

    /* a rectangle with no size removes the clip */
    CHECK(wgr_scene_set_clip(scene, 1, 100, 100, 0, 100));
    CHECK(wgr_scene_pick(scene, 0, 150, 280).handle == outside);

    /* at most 8 clipped layers per scene */
    for (int layer = 0; layer < 8; layer++) {
        CHECK(wgr_scene_set_clip(scene, layer, 0, 0, 10, 10));
    }
    CHECK(!wgr_scene_set_clip(scene, 8, 0, 0, 10, 10));
    CHECK(wgr_scene_set_clip(scene, 3, 0, 0, 20, 20)); /* a layer already clipped still moves */
    CHECK(wgr_scene_set_clip(scene, 3, 0, 0, 0, 0));   /* and frees its slot */
    CHECK(wgr_scene_set_clip(scene, 8, 0, 0, 10, 10));
    CHECK(!wgr_scene_set_clip(0, 0, 0, 0, 10, 10));

    wgr_scene_draw(scene); /* clipped layers draw without trouble */

    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_scene_destroy(scene);
    wgr_shape2d_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* sprite3d in a 2D world (docs/PLAN-2d.md step 4): the source rectangle, the world
 * extent and the pivot, checked through picking — the quad picked is the quad drawn. */
void test_sprite3d_2d_world(void)
{
    const vec2_t screen = wgr_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(wgr_window_set_size(800, 600));

    /* looking down -Z at the XY plane, 8 world units tall: 600 px / 8 = 75 px per unit */
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_ORTHOGRAPHIC);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_camera3d_set_ortho_height(camera, 8.0f);
    wgr_camera3d_set_active(camera);

    const wgr_handle_t sprite = wgr_sprite3d_create(wgr_texture_get_default());
    wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_FREE);
    wgr_sprite3d_set_transform(sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);

    /* 1x1 centered on the origin: the middle of the screen, 75 px across */
    CHECK(wgr_pick_object(sprite, camera, 400, 300).hit);
    CHECK(wgr_pick_object(sprite, camera, 400 + 30, 300).hit);
    CHECK(!wgr_pick_object(sprite, camera, 400 + 50, 300).hit);
    CHECK(!wgr_pick_object(sprite, camera, 400, 300 - 50).hit);

    /* extent: 1 wide, 2 tall — a 16x32 sheet cell drawn with square pixels */
    CHECK(wgr_sprite3d_set_extent(sprite, 1.0f, 2.0f));
    CHECK(wgr_pick_object(sprite, camera, 400, 300 - 50).hit);   /* taller now */
    CHECK(!wgr_pick_object(sprite, camera, 400 + 50, 300).hit);  /* still 1 wide */
    CHECK(!wgr_sprite3d_set_extent(sprite, 0.0f, 2.0f));         /* refused */
    CHECK(!wgr_sprite3d_set_extent(sprite, 1.0f, -1.0f));

    /* pivot at the bottom edge: the quad stands on the sprite's position */
    CHECK(wgr_sprite3d_set_pivot(sprite, 0.5f, 1.0f));
    CHECK(wgr_pick_object(sprite, camera, 400, 300 - 100).hit);  /* the quad is above it */
    CHECK(!wgr_pick_object(sprite, camera, 400, 300 + 20).hit);  /* nothing below */
    CHECK(wgr_sprite3d_set_pivot(sprite, 0.5f, 0.5f));
    CHECK(wgr_pick_object(sprite, camera, 400, 300 + 20).hit);   /* centered again */

    /* the source rectangle doesn't move the quad, and an empty one means the whole
       texture (defaults), so picking is unchanged */
    CHECK(wgr_sprite3d_set_source(sprite, 32, 16, 16, 16));
    CHECK(wgr_pick_object(sprite, camera, 400, 300).hit);
    CHECK(wgr_sprite3d_set_source(sprite, 0, 0, 0, 0));
    CHECK(wgr_pick_object(sprite, camera, 400, 300).hit);
    CHECK(!wgr_sprite3d_set_source(0, 0, 0, 16, 16));
    CHECK(!wgr_sprite3d_set_pivot(0, 0.5f, 0.5f));

    /* set_size is the square shorthand */
    CHECK(wgr_sprite3d_set_size(sprite, 2.0f));
    CHECK(wgr_pick_object(sprite, camera, 400 + 50, 300).hit);
    CHECK(wgr_pick_object(sprite, camera, 400, 300 - 50).hit);

    wgr_sprite3d_draw(sprite); /* draws with a source rectangle without trouble */
    wgr_sprite3d_destroy(sprite);

    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_sprite3d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* One layout for all text: the immediate path measures and draws the same laid-out
 * block as text2d, and text3d wraps and aligns in world units. */
void test_text_layout_shared(void)
{
    const vec2_t screen = wgr_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_font_init();
    wgr_text_init();
    wgr_text3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    /* immediate text: newlines break lines, and the height grows by whole lines */
    const vec2_t one = wgr_text_measure_ex(0, "one", 16.0f);
    const vec2_t three = wgr_text_measure_ex(0, "one\ntwo\nthree", 16.0f);
    CHECK(one.x > 0.0f && one.y > 0.0f);
    CHECK_NEAR(three.y, one.y * 3.0f, 0.01);
    CHECK(three.x > one.x); /* "three" is the widest line */
    CHECK(wgr_text_measure_ex(0, "", 16.0f).y == 0.0f);

    /* unwrapped text measures exactly what's drawn, spaces included: a lone space has
       its advance (Clay measures " " to space the words it lays out), and trailing
       spaces count */
    const float a = wgr_text_measure_ex(0, "a", 16.0f).x, space = wgr_text_measure_ex(0, " ", 16.0f).x;
    CHECK(space > 0.0f);
    CHECK(wgr_text_measure_ex(0, "a ", 16.0f).x > a);
    CHECK_NEAR(wgr_text_measure_ex(0, "a b", 16.0f).x, a + space + wgr_text_measure_ex(0, "b", 16.0f).x, 0.5);
    CHECK_NEAR(wgr_text_measure_ex(0, "  ", 16.0f).x, space * 2.0f, 0.5);
    CHECK_NEAR(wgr_text_measure_ex(0, "a\n", 16.0f).y, one.y, 0.01); /* a trailing newline adds no line */
    CHECK_NEAR(wgr_text_measure("one", 16), (double)(int)(one.x + 0.5f), 0.51);
    wgr_text_draw_ex(0, "one\ntwo", 10.0f, 10.0f, 16.0f, WGR_COLOR_WHITE); /* draws without trouble */

    /* text3d: same splitting, sizes in world units */
    const wgr_handle_t label = wgr_text3d_create(0);
    CHECK(wgr_text3d_set_text(label, "one two three four"));
    CHECK(wgr_text3d_set_size(label, 1.0f));
    const vec2_t unwrapped = wgr_text3d_get_size(label);
    CHECK(unwrapped.x > 0.0f && unwrapped.y > 0.0f);

    CHECK(wgr_text3d_set_max_width(label, unwrapped.x * 0.5f));
    const vec2_t wrapped = wgr_text3d_get_size(label);
    CHECK(wrapped.x < unwrapped.x);
    CHECK(wrapped.y > unwrapped.y);                             /* more lines */
    CHECK_NEAR(wrapped.y, unwrapped.y * 3.0f, 0.01);            /* "one two" / "three" / "four" */
    CHECK(wgr_text3d_set_max_width(label, 0.0f));                /* off again */
    CHECK_NEAR(wgr_text3d_get_size(label).y, unwrapped.y, 0.01);

    /* newlines break without wrapping, and one line of size 1 is about 1 unit tall */
    CHECK(wgr_text3d_set_text(label, "a\nb"));
    CHECK_NEAR(wgr_text3d_get_size(label).y, unwrapped.y * 2.0f, 0.01);
    CHECK(unwrapped.y > 0.8f && unwrapped.y < 1.6f);

    /* alignment takes only its own axis's values; the size doesn't depend on it */
    CHECK(wgr_text3d_set_align(label, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_TOP));
    CHECK_NEAR(wgr_text3d_get_size(label).y, unwrapped.y * 2.0f, 0.01);
    CHECK(!wgr_text3d_set_align(label, WGR_TEXT_ALIGN_MIDDLE, WGR_TEXT_ALIGN_TOP));
    CHECK(!wgr_text3d_set_align(label, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_RIGHT));
    CHECK(!wgr_text3d_set_align(0, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_TOP));
    CHECK(!wgr_text3d_set_max_width(0, 1.0f));

    /* alignment moves the block, so picks follow it: a camera looking down -Z at
       the origin, the text placed at the origin, 8 world units of screen height */
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_ORTHOGRAPHIC);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_camera3d_set_ortho_height(camera, 8.0f);
    wgr_camera3d_set_active(camera);
    CHECK(wgr_window_set_size(800, 600));
    CHECK(wgr_text3d_set_text(label, "pick"));
    CHECK(wgr_text3d_set_facing(label, WGR_SPRITE3D_FACING_FREE));
    CHECK(wgr_text3d_set_transform(label, 0, 0, 0, 0, 0, 0));

    CHECK(wgr_text3d_set_align(label, WGR_TEXT_ALIGN_CENTER, WGR_TEXT_ALIGN_MIDDLE));
    CHECK(wgr_pick_object(label, camera, 400, 300).hit); /* centered on its position */
    CHECK(wgr_text3d_set_align(label, WGR_TEXT_ALIGN_LEFT, WGR_TEXT_ALIGN_TOP));
    CHECK(wgr_pick_object(label, camera, 420, 310).hit);       /* block runs down and to the right */
    CHECK(!wgr_pick_object(label, camera, 380, 290).hit);      /* nothing up and to the left of it */

    wgr_text3d_destroy(label);
    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_text3d_deinit();
    wgr_font_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* The four facings' quad bases (wgr_sprite3d_facing_basis, shared by sprite3d and
 * text3d), under a camera pitched 45 degrees down: spherical faces the view plane,
 * cylindrical stays upright. */
void test_sprite3d_facings(void)
{
    const wgr_camera3d_t pitched = {.position = {0, 10, 10}, .target = {0, 0, 0}, .up = {0, 1, 0}};
    const float h = 0.70710678f;
    vec3_t right, up;

    /* spherical: tilts back with the camera, so it faces it exactly */
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &pitched, &right, &up);
    CHECK_VEC3_NEAR(right, 1, 0, 0, 1e-5);
    CHECK_VEC3_NEAR(up, 0, h, -h, 1e-5);

    /* cylindrical: same right, but upright however far the camera looks down */
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA_FIXED_Y, (vec3_t){0, 0, 0}, &pitched, &right, &up);
    CHECK_VEC3_NEAR(right, 1, 0, 0, 1e-5);
    CHECK_VEC3_NEAR(up, 0, 1, 0, 1e-6);

    /* ... and it still turns about Y to follow a camera moving around it */
    const wgr_camera3d_t side = {.position = {10, 10, 0}, .target = {0, 0, 0}, .up = {0, 1, 0}};
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA_FIXED_Y, (vec3_t){0, 0, 0}, &side, &right, &up);
    CHECK_VEC3_NEAR(right, 0, 0, -1, 1e-5);
    CHECK_VEC3_NEAR(up, 0, 1, 0, 1e-6);

    /* looking straight down (up hint along -Z): no horizontal direction from the
       view, so it takes the camera's own right instead of collapsing */
    const wgr_camera3d_t down = {.position = {0, 10, 0}, .target = {0, 0, 0}, .up = {0, 0, -1}};
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA_FIXED_Y, (vec3_t){0, 0, 0}, &down, &right, &up);
    CHECK_NEAR(right.x * right.x + right.y * right.y + right.z * right.z, 1.0, 1e-5);
    CHECK_NEAR(right.y, 0.0, 1e-6);
    CHECK_VEC3_NEAR(up, 0, 1, 0, 1e-6);

    /* flat on the ground and free don't depend on the camera at all */
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_Y_UP, (vec3_t){0, 0, 0}, &pitched, &right, &up);
    CHECK_VEC3_NEAR(right, 1, 0, 0, 1e-6);
    CHECK_VEC3_NEAR(up, 0, 0, -1, 1e-6);
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_FREE, (vec3_t){0, 1.5707963f, 0}, &pitched, &right, &up);
    CHECK_VEC3_NEAR(right, 0, 0, -1, 1e-5); /* a quarter turn about Y */
    CHECK_VEC3_NEAR(up, 0, 1, 0, 1e-5);
}

/* Sprite pools start small and grow: well past the old fixed sizes (1024 sprite3d,
 * 4096 sprite2d) every handle still resolves to its own sprite, and a scene with
 * that many transparent sprites draws (its transparent list grows too). */
void test_sprite_pools_grow(void)
{
    enum { COUNT = 6000 };
    static wgr_handle_t sprites3d[COUNT], sprites2d[COUNT];
    bool created = true, positions = true, set2d = true;

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite3d_init();
    wgr_sprite2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 50, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);
    for (int i = 0; i < COUNT; i++) {
        sprites3d[i] = wgr_sprite3d_create(wgr_texture_get_default());
        sprites2d[i] = wgr_sprite2d_create(wgr_texture_get_default());
        created = created && sprites3d[i] != 0 && sprites2d[i] != 0;
        wgr_sprite3d_set_transform(sprites3d[i], (float)i, 0, 0, 0, 0, 0, 1, 1, 1);
        wgr_scene_add(scene, sprites3d[i], 0);
    }
    CHECK(created);
    for (int i = 0; i < COUNT; i++) {
        positions = positions && wgr_sprite3d_get_position(sprites3d[i]).x == (float)i;
        set2d = set2d && wgr_sprite2d_set_position(sprites2d[i], (float)i, 0);
    }
    CHECK(positions);
    CHECK(set2d);

    wgr_render_begin();
    wgr_scene_draw(scene);
    wgr_render_end();

    for (int i = 0; i < COUNT; i++) {
        wgr_sprite3d_destroy(sprites3d[i]);
        wgr_sprite2d_destroy(sprites2d[i]);
    }
    CHECK(wgr_sprite3d_get_position(sprites3d[0]).x == 0); /* stale: resolves to nothing */

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_scene_destroy(scene);
    wgr_sprite2d_deinit();
    wgr_sprite3d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Sprites and sokol_gl shapes alternating thousands of times in one frame: each switch
 * is a render command, far past the list's first size, and every sprite still draws. */
void test_sprites_interleaved(void)
{
    enum { SWITCHES = 3000 };

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    const wgr_handle_t sprite = wgr_sprite3d_create(wgr_texture_get_default());
    wgr_render_begin();
    wgr_render_begin_mode_3d();
    const double start = now_seconds();
    for (int i = 0; i < SWITCHES; i++) {
        wgr_sprite3d_draw(sprite);
        wgr_shape3d_draw_line(0, 0, 0, 1, 1, 1, WGR_COLOR_WHITE);
    }
    wgr_render_end_mode_3d();
    CHECK(wgr_render_command_count() >= 2 * SWITCHES); /* a sprite batch and a layer per switch */
    const double recorded = now_seconds();
    wgr_render_end();
    fprintf(stderr, "    (%d switches: recorded in %.2f ms, replayed in %.2f ms)\n", SWITCHES,
            (recorded - start) * 1000.0, (now_seconds() - recorded) * 1000.0);

    wgr_sprite3d_destroy(sprite);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_sprite3d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* Alpha modes: masked and additive sprites aren't sorted, so a scene groups them by
 * texture whatever their order; blended ones are sorted and interleave. */
void test_sprite3d_alpha_modes(void)
{
    enum { COUNT = 400, TEXTURES = 4 };
    static wgr_handle_t sprites[COUNT];
    wgr_handle_t textures[TEXTURES];

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite3d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    for (int t = 0; t < TEXTURES; t++) {
        const unsigned char pixel[4] = {(unsigned char)(60 * t), 200, 100, 255};
        textures[t] = wgr_texture_create_rgba(pixel, 1, 1);
    }
    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 5, 30, 0, 0, 0, 0, 1, 0);
    wgr_scene_set_active_camera(scene, camera);
    for (int i = 0; i < COUNT; i++) {
        sprites[i] = wgr_sprite3d_create(textures[i % TEXTURES]); /* textures alternate */
        wgr_sprite3d_set_transform(sprites[i], (float)(i % 20) - 10.0f, 0, (float)(i / 20) - 10.0f, 0, 0, 0, 1, 1, 1);
        wgr_scene_add(scene, sprites[i], 0);
    }

    CHECK(wgr_sprite3d_get_alpha_mode(sprites[0]) == WGR_ALPHA_BLEND); /* the default */
    CHECK(!wgr_sprite3d_set_alpha_mode(sprites[0], (wgr_alpha_mode_t)7, 0.5f));
    CHECK(!wgr_sprite3d_set_alpha_mode(0, WGR_ALPHA_MASK, 0.5f));

    /* blended: sorted back to front, the textures interleave */
    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() > 100);
    wgr_render_end();

    /* masked: one batch per texture */
    for (int i = 0; i < COUNT; i++) CHECK(wgr_sprite3d_set_alpha_mode(sprites[i], WGR_ALPHA_MASK, 0.5f));
    CHECK(wgr_sprite3d_get_alpha_mode(sprites[7]) == WGR_ALPHA_MASK);
    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() == TEXTURES);
    wgr_render_end();

    /* additive: one batch per texture too; opaque the same */
    for (int i = 0; i < COUNT; i++) wgr_sprite3d_set_alpha_mode(sprites[i], i < COUNT / 2 ? WGR_ALPHA_ADD : WGR_ALPHA_OPAQUE, 0);
    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() == 2 * TEXTURES);
    wgr_render_end();

    for (int i = 0; i < COUNT; i++) wgr_sprite3d_destroy(sprites[i]);
    for (int t = 0; t < TEXTURES; t++) wgr_texture_release(textures[t]);
    wgr_scene_destroy(scene);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_sprite3d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* sprite2d on the instanced sprite path: one batch for a run of the same texture, a
 * nine-slice sprite included; alternating textures keep their order (2D is never
 * regrouped); alpha modes. */
void test_sprite2d_batches(void)
{
    enum { COUNT = 40 };
    static wgr_handle_t sprites[COUNT];
    wgr_handle_t textures[2];

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    for (int t = 0; t < 2; t++) {
        const unsigned char pixels[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                                          255, 255, 255, 255, 255, 255, 255, (unsigned char)(100 * t)};
        textures[t] = wgr_texture_create_rgba(pixels, 2, 2);
    }
    const wgr_handle_t scene = wgr_scene_create();
    for (int i = 0; i < COUNT; i++) {
        sprites[i] = wgr_sprite2d_create(textures[0]);
        wgr_sprite2d_set_position(sprites[i], (float)(i * 10), 50);
        wgr_scene_add(scene, sprites[i], 0);
    }
    CHECK(wgr_sprite2d_set_nine_slice(sprites[3], 1, 1, 1, 1)); /* several quads, one sprite */
    CHECK(wgr_sprite2d_set_size(sprites[3], 60, 30));

    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() == 1);
    wgr_render_end();

    /* alternating textures: in order, a batch each */
    for (int i = 0; i < COUNT; i++) wgr_sprite2d_set_texture(sprites[i], textures[i % 2]);
    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() == COUNT);
    wgr_render_end();

    /* alpha modes */
    CHECK(wgr_sprite2d_get_alpha_mode(sprites[0]) == WGR_ALPHA_BLEND);
    CHECK(wgr_sprite2d_set_alpha_mode(sprites[0], WGR_ALPHA_ADD, 0));
    CHECK(wgr_sprite2d_get_alpha_mode(sprites[0]) == WGR_ALPHA_ADD);
    CHECK(!wgr_sprite2d_set_alpha_mode(sprites[0], (wgr_alpha_mode_t)9, 0));
    for (int i = 0; i < COUNT; i++) {
        wgr_sprite2d_set_texture(sprites[i], textures[0]);
        wgr_sprite2d_set_alpha_mode(sprites[i], i < COUNT / 2 ? WGR_ALPHA_MASK : WGR_ALPHA_BLEND, 0.5f);
    }
    wgr_render_begin();
    wgr_scene_draw(scene);
    CHECK(wgr_sprite_batch_count() == 2); /* the masked run, then the blended run */
    wgr_render_end();

    for (int i = 0; i < COUNT; i++) wgr_sprite2d_destroy(sprites[i]);
    for (int t = 0; t < 2; t++) wgr_texture_release(textures[t]);
    wgr_scene_destroy(scene);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_sprite2d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}
