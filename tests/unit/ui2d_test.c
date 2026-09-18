/* UI essentials (docs/PLAN-2d.md step 3): nine-slice geometry, text2d wrapping and
 * alignment, and per-layer clip rectangles (which also mask picking). */
#include <string.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite2d.h"
#include "sk_camera3d.h"
#include "sk_logger.h"
#include "sk_pick.h"
#include "sk_scene.h"
#include "sk_shape2d.h"
#include "sk_sprite2d.h"
#include "sk_sprite3d.h"
#include "internal/sk_texture.h"
#include "sk_text.h"
#include "sk_text2d.h"
#include "sk_texture.h"
#include "sk_window.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

void test_nine_slice(void)
{
    float dest[2], source[2];

    /* 16 px borders of a 64 px region, drawn 256 px wide: the borders keep their
     * size on screen (16/256) and take a quarter of the source each */
    CHECK(sk_sprite2d_nine_slice_axis(16, 16, 256, 64, dest, source));
    CHECK_NEAR(dest[0], 16.0f / 256.0f, 1e-6);
    CHECK_NEAR(dest[1], 1.0f - 16.0f / 256.0f, 1e-6);
    CHECK_NEAR(source[0], 0.25f, 1e-6);
    CHECK_NEAR(source[1], 0.75f, 1e-6);

    /* uneven borders */
    CHECK(sk_sprite2d_nine_slice_axis(8, 24, 100, 64, dest, source));
    CHECK_NEAR(dest[0], 0.08f, 1e-6);
    CHECK_NEAR(dest[1], 0.76f, 1e-6);
    CHECK_NEAR(source[0], 0.125f, 1e-6);
    CHECK_NEAR(source[1], 0.625f, 1e-6);

    /* one-sided: the other edge stays at the end of the sprite */
    CHECK(sk_sprite2d_nine_slice_axis(10, 0, 50, 20, dest, source));
    CHECK_NEAR(dest[0], 0.2f, 1e-6);
    CHECK_NEAR(dest[1], 1.0f, 1e-6);
    CHECK_NEAR(source[1], 1.0f, 1e-6);

    /* drawn smaller than its borders: they shrink to fill it, in order */
    CHECK(sk_sprite2d_nine_slice_axis(16, 16, 24, 64, dest, source));
    CHECK_NEAR(dest[0], 0.5f, 1e-6);
    CHECK_NEAR(dest[1], 0.5f, 1e-6);
    CHECK(dest[0] <= dest[1]);
    CHECK_NEAR(source[0], 0.25f, 1e-6); /* the source split is unchanged */

    /* borders wider than the source region share it */
    CHECK(sk_sprite2d_nine_slice_axis(30, 10, 200, 20, dest, source));
    CHECK_NEAR(source[0], 0.75f, 1e-6);
    CHECK_NEAR(source[1], 0.75f, 1e-6);

    /* no borders, or nothing to draw into */
    CHECK(!sk_sprite2d_nine_slice_axis(0, 0, 100, 64, dest, source));
    CHECK(!sk_sprite2d_nine_slice_axis(8, 8, 0, 64, dest, source));
    CHECK(!sk_sprite2d_nine_slice_axis(8, 8, 100, 0, dest, source));
}

void test_text2d_layout(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_font_init();
    sk_text_init();
    sk_text2d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);

    sk_handle_t text = sk_text2d_create(0);
    CHECK(sk_text2d_set_text(text, "one two three four"));
    CHECK(sk_text2d_set_size(text, 16));
    CHECK(sk_text2d_set_position(text, 100, 50));

    const float full_width = sk_text2d_measure_width(text);
    const float line_height = sk_text2d_measure_height(text);
    CHECK(full_width > 0.0f && line_height > 0.0f);

    /* wrapped: the block is the box wide and taller, by whole lines */
    CHECK(sk_text2d_set_max_width(text, full_width * 0.5f));
    const float wrapped_height = sk_text2d_measure_height(text);
    CHECK(sk_text2d_measure_width(text) < full_width);
    CHECK_NEAR(wrapped_height, line_height * 3.0f, 0.01); /* "one two" / "three" / "four" */

    /* a word longer than the box keeps its own line (no mid-word break) */
    CHECK(sk_text2d_set_text(text, "unbreakable"));
    CHECK(sk_text2d_set_max_width(text, 10));
    CHECK_NEAR(sk_text2d_measure_height(text), line_height, 0.01);
    CHECK(sk_text2d_measure_width(text) > 10.0f);

    /* newlines always break */
    CHECK(sk_text2d_set_max_width(text, 0));
    CHECK(sk_text2d_set_text(text, "a\nb\nc"));
    CHECK_NEAR(sk_text2d_measure_height(text), line_height * 3.0f, 0.01);

    /* alignment moves the block around the position: picks follow it */
    CHECK(sk_text2d_set_text(text, "pick me"));
    const float w = sk_text2d_measure_width(text), h = sk_text2d_measure_height(text);
    CHECK(sk_pick_object(text, 0, 100 + w * 0.5f, 50 + h * 0.5f).hit); /* left/top by default */
    CHECK(!sk_pick_object(text, 0, 100 - w * 0.25f, 50 + h * 0.5f).hit);

    CHECK(sk_text2d_set_align(text, SK_TEXT_ALIGN_CENTER, SK_TEXT_ALIGN_MIDDLE));
    CHECK(sk_pick_object(text, 0, 100 - w * 0.25f, 50 - h * 0.25f).hit);
    CHECK(sk_pick_object(text, 0, 100 + w * 0.25f, 50 + h * 0.25f).hit);
    CHECK(!sk_pick_object(text, 0, 100 + w * 0.75f, 50).hit);

    CHECK(sk_text2d_set_align(text, SK_TEXT_ALIGN_RIGHT, SK_TEXT_ALIGN_BOTTOM));
    CHECK(sk_pick_object(text, 0, 100 - w * 0.5f, 50 - h * 0.5f).hit);
    CHECK(!sk_pick_object(text, 0, 100 + 1.0f, 50 - h * 0.5f).hit);

    /* an axis only takes its own values */
    CHECK(!sk_text2d_set_align(text, SK_TEXT_ALIGN_TOP, SK_TEXT_ALIGN_MIDDLE));
    CHECK(!sk_text2d_set_align(text, SK_TEXT_ALIGN_LEFT, SK_TEXT_ALIGN_CENTER));

    /* a wrapped block is its box wide for alignment, however short the lines are */
    CHECK(sk_text2d_set_align(text, SK_TEXT_ALIGN_LEFT, SK_TEXT_ALIGN_TOP));
    CHECK(sk_text2d_set_text(text, "hi"));
    CHECK(sk_text2d_set_max_width(text, 400));
    CHECK(sk_pick_object(text, 0, 100 + 300, 50 + h * 0.5f).hit);
    CHECK(!sk_pick_object(text, 0, 100 + 500, 50 + h * 0.5f).hit);

    sk_text2d_draw(text); /* wrapping and alignment draw without trouble */
    sk_text2d_destroy(text);

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_text2d_deinit();
    sk_font_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

void test_scene_clip(void)
{
    const vec2_t screen = sk_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_shape2d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(sk_window_set_size(800, 600));

    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 5, 0, 0, 0, 0, 1, 0);
    const sk_handle_t scene = sk_scene_create();
    sk_scene_set_active_camera(scene, camera);

    /* a row of 2D panels on layer 1, inside a 200x100 window at (100, 100) */
    const sk_handle_t inside = sk_shape2d_create();
    sk_shape2d_set_rectangle(inside, 60, 40, 0);
    sk_shape2d_set_transform(inside, 120, 120, 0, 1, 1);
    const sk_handle_t outside = sk_shape2d_create();
    sk_shape2d_set_rectangle(outside, 60, 40, 0);
    sk_shape2d_set_transform(outside, 120, 260, 0, 1, 1); /* below the window */
    sk_scene_add(scene, inside, 1);
    sk_scene_add(scene, outside, 1);

    CHECK(sk_scene_pick(scene, 0, 150, 140).handle == inside);
    CHECK(sk_scene_pick(scene, 0, 150, 280).handle == outside);

    CHECK(sk_scene_set_clip(scene, 1, 100, 100, 200, 100));
    CHECK(sk_scene_pick(scene, 0, 150, 140).handle == inside);
    CHECK(sk_scene_pick(scene, 0, 150, 280).handle == 0); /* clipped away: not picked */

    /* another layer isn't clipped */
    const sk_handle_t other = sk_shape2d_create();
    sk_shape2d_set_rectangle(other, 60, 40, 0);
    sk_shape2d_set_transform(other, 400, 400, 0, 1, 1);
    sk_scene_add(scene, other, 2);
    CHECK(sk_scene_pick(scene, 0, 430, 420).handle == other);

    /* interaction picks the same way (a clipped member can't be hovered) */
    CHECK(sk_scene_set_interactive(scene, true));
    sk_scene_update_interaction();
    CHECK(sk_scene_get_hovered(scene) == 0);

    /* a rectangle with no size removes the clip */
    CHECK(sk_scene_set_clip(scene, 1, 100, 100, 0, 100));
    CHECK(sk_scene_pick(scene, 0, 150, 280).handle == outside);

    /* at most 8 clipped layers per scene */
    for (int layer = 0; layer < 8; layer++) {
        CHECK(sk_scene_set_clip(scene, layer, 0, 0, 10, 10));
    }
    CHECK(!sk_scene_set_clip(scene, 8, 0, 0, 10, 10));
    CHECK(sk_scene_set_clip(scene, 3, 0, 0, 20, 20)); /* a layer already clipped still moves */
    CHECK(sk_scene_set_clip(scene, 3, 0, 0, 0, 0));   /* and frees its slot */
    CHECK(sk_scene_set_clip(scene, 8, 0, 0, 10, 10));
    CHECK(!sk_scene_set_clip(0, 0, 0, 0, 10, 10));

    sk_scene_draw(scene); /* clipped layers draw without trouble */

    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_scene_destroy(scene);
    sk_shape2d_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

/* sprite3d in a 2D world (docs/PLAN-2d.md step 4): the source rectangle, the world
 * extent and the pivot, checked through picking — the quad picked is the quad drawn. */
void test_sprite3d_2d_world(void)
{
    const vec2_t screen = sk_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_sprite3d_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(sk_window_set_size(800, 600));

    /* looking down -Z at the XY plane, 8 world units tall: 600 px / 8 = 75 px per unit */
    const sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_ORTHOGRAPHIC);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_camera3d_set_ortho_height(camera, 8.0f);
    sk_camera3d_set_active(camera);

    const sk_handle_t sprite = sk_sprite3d_create(sk_texture_get_default());
    sk_sprite3d_set_facing(sprite, SK_SPRITE3D_FACING_FREE);
    sk_sprite3d_set_transform(sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);

    /* 1x1 centered on the origin: the middle of the screen, 75 px across */
    CHECK(sk_pick_object(sprite, camera, 400, 300).hit);
    CHECK(sk_pick_object(sprite, camera, 400 + 30, 300).hit);
    CHECK(!sk_pick_object(sprite, camera, 400 + 50, 300).hit);
    CHECK(!sk_pick_object(sprite, camera, 400, 300 - 50).hit);

    /* extent: 1 wide, 2 tall — a 16x32 sheet cell drawn with square pixels */
    CHECK(sk_sprite3d_set_extent(sprite, 1.0f, 2.0f));
    CHECK(sk_pick_object(sprite, camera, 400, 300 - 50).hit);   /* taller now */
    CHECK(!sk_pick_object(sprite, camera, 400 + 50, 300).hit);  /* still 1 wide */
    CHECK(!sk_sprite3d_set_extent(sprite, 0.0f, 2.0f));         /* refused */
    CHECK(!sk_sprite3d_set_extent(sprite, 1.0f, -1.0f));

    /* pivot at the bottom edge: the quad stands on the sprite's position */
    CHECK(sk_sprite3d_set_pivot(sprite, 0.5f, 1.0f));
    CHECK(sk_pick_object(sprite, camera, 400, 300 - 100).hit);  /* the quad is above it */
    CHECK(!sk_pick_object(sprite, camera, 400, 300 + 20).hit);  /* nothing below */
    CHECK(sk_sprite3d_set_pivot(sprite, 0.5f, 0.5f));
    CHECK(sk_pick_object(sprite, camera, 400, 300 + 20).hit);   /* centered again */

    /* the source rectangle doesn't move the quad, and an empty one means the whole
       texture (defaults), so picking is unchanged */
    CHECK(sk_sprite3d_set_source(sprite, 32, 16, 16, 16));
    CHECK(sk_pick_object(sprite, camera, 400, 300).hit);
    CHECK(sk_sprite3d_set_source(sprite, 0, 0, 0, 0));
    CHECK(sk_pick_object(sprite, camera, 400, 300).hit);
    CHECK(!sk_sprite3d_set_source(0, 0, 0, 16, 16));
    CHECK(!sk_sprite3d_set_pivot(0, 0.5f, 0.5f));

    /* set_size is the square shorthand */
    CHECK(sk_sprite3d_set_size(sprite, 2.0f));
    CHECK(sk_pick_object(sprite, camera, 400 + 50, 300).hit);
    CHECK(sk_pick_object(sprite, camera, 400, 300 - 50).hit);

    sk_sprite3d_draw(sprite); /* draws with a source rectangle without trouble */
    sk_sprite3d_destroy(sprite);

    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_sprite3d_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
