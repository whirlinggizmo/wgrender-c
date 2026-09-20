/* What a text2d object holds besides its layout (src/wgr_text2d.c): its font, color and
 * the three flags a scene reads — visible, pickable and enabled — and what happens to
 * its font when it goes. Layout and alignment are covered by test_text2d_layout.
 *
 * Scene layers are here too: which member a pick finds when several sit on different
 * layers over the same point. */
#include "internal/wgr_camera3d.h"
#include "internal/wgr_font.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_platform.h"
#include "internal/wgr_render.h"
#include "internal/wgr_scene.h"
#include "internal/wgr_shape2d.h"
#include "wgr_color.h"
#include "wgr_font.h"
#include "wgr_logger.h"
#include "wgr_pick.h"
#include "wgr_render.h"
#include "wgr_scene.h"
#include "wgr_shape2d.h"
#include "wgr_text2d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define FONT "../examples/assets/fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define SCREEN 201.0f

static void begin(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_font_init();
    wgr_text_init();
    wgr_text2d_init();
    wgr_shape2d_init();
}

static void end(void)
{
    wgr_shape2d_deinit();
    wgr_text2d_deinit();
    wgr_text_deinit();
    wgr_font_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

void test_text2d_state(void)
{
    begin();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* invalid handles below log on purpose */

    const wgr_handle_t text = wgr_text2d_create(0);
    CHECK(text != 0);
    CHECK(wgr_text2d_set_text(text, "pick me"));
    CHECK(wgr_text2d_set_size(text, 20));
    CHECK(wgr_text2d_set_position(text, 20, 20));
    CHECK(wgr_text2d_measure_width(text) > 0.0f);

    /* the flags start on, and each one answers back */
    CHECK(wgr_text2d_is_visible(text) && wgr_text2d_is_pickable(text) && wgr_text2d_is_enabled(text));
    CHECK(wgr_text2d_set_visible(text, false) && !wgr_text2d_is_visible(text));
    CHECK(wgr_text2d_set_pickable(text, false) && !wgr_text2d_is_pickable(text));
    CHECK(wgr_text2d_set_enabled(text, false) && !wgr_text2d_is_enabled(text));
    CHECK(wgr_text2d_set_visible(text, true) && wgr_text2d_is_visible(text));
    CHECK(wgr_text2d_set_pickable(text, true) && wgr_text2d_is_pickable(text));
    CHECK(wgr_text2d_set_enabled(text, true) && wgr_text2d_is_enabled(text));
    CHECK(wgr_text2d_set_color(text, WGR_COLOR_RED));

    /* hidden or unpickable text isn't hit; the color doesn't matter either way */
    const float inside_x = 22.0f, inside_y = 22.0f;
    CHECK(wgr_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(wgr_text2d_set_visible(text, false));
    CHECK(!wgr_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(wgr_text2d_set_visible(text, true));
    CHECK(wgr_text2d_set_pickable(text, false));
    CHECK(!wgr_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(wgr_text2d_set_pickable(text, true));
    /* disabled text still blocks the pointer: it's there, it just doesn't react */
    CHECK(wgr_text2d_set_enabled(text, false));
    CHECK(wgr_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(wgr_text2d_set_enabled(text, true));

    /* empty text measures nothing and can't be hit */
    const wgr_handle_t empty = wgr_text2d_create(0);
    CHECK(wgr_text2d_set_position(empty, 20, 20));
    CHECK(wgr_text2d_measure_width(empty) == 0.0f);
    CHECK(wgr_text2d_measure_height(empty) == 0.0f);
    CHECK(!wgr_pick_object(empty, 0, inside_x, inside_y).hit);
    CHECK(wgr_text2d_set_text(empty, "")); /* the same, said explicitly */
    CHECK(wgr_text2d_measure_width(empty) == 0.0f);
    wgr_text2d_destroy(empty);

    /* a font of its own: the text holds a reference, and gives it back when it goes */
    const wgr_handle_t font = wgr_font_create(FONT);
    CHECK(font != 0);
    CHECK(wgr_text2d_set_font(text, font));
    const float default_width = wgr_text2d_measure_width(text);
    CHECK(default_width > 0.0f);
    wgr_font_release(font);             /* the text is the only holder now */
    CHECK(wgr_text2d_set_font(text, 0)); /* back to the built-in font */
    CHECK(wgr_text2d_measure_width(text) > 0.0f);

    const wgr_handle_t kept = wgr_font_create(FONT);
    CHECK(wgr_text2d_set_font(text, kept));
    wgr_font_release(kept);
    wgr_text2d_destroy(text); /* releases the font with it */

    /* every setter refuses a handle that isn't a text2d, and says so */
    CHECK(!wgr_text2d_set_text(0, "x"));
    CHECK(!wgr_text2d_set_size(text, 12)); /* destroyed above */
    CHECK(!wgr_text2d_set_color(text, WGR_COLOR_RED));
    CHECK(!wgr_text2d_set_visible(text, true));
    CHECK(!wgr_text2d_set_pickable(text, true));
    CHECK(!wgr_text2d_set_enabled(text, true));
    CHECK(!wgr_text2d_set_font(text, 0));
    CHECK(!wgr_text2d_is_visible(text) && !wgr_text2d_is_pickable(text));
    CHECK(wgr_text2d_measure_width(text) == 0.0f);
    wgr_text2d_destroy(text); /* twice is harmless */

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    end();
}

/* Scene layers order 2D members: over the same point, the higher layer is found
 * first, whatever order they were added in, and moving a member between layers
 * changes which one wins. */
void test_scene_layer_order(void)
{
    const int was_width = wgr_platform_width(), was_height = wgr_platform_height();

    begin();
    wgr_platform_set_window_size((int)SCREEN, (int)SCREEN);

    const wgr_handle_t scene = wgr_scene_create();
    const wgr_handle_t low = wgr_shape2d_create();
    const wgr_handle_t high = wgr_shape2d_create();
    CHECK(wgr_shape2d_set_rectangle(low, 100, 100, 0));
    CHECK(wgr_shape2d_set_rectangle(high, 100, 100, 0));
    CHECK(wgr_shape2d_set_transform(low, 10, 10, 0, 1, 1));
    CHECK(wgr_shape2d_set_transform(high, 10, 10, 0, 1, 1)); /* exactly on top of each other */

    /* added low-layer first, then high */
    CHECK(wgr_scene_add(scene, low, 1));
    CHECK(wgr_scene_add(scene, high, 5));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);

    /* the order they were added in doesn't decide it: the layer does */
    wgr_scene_remove(scene, high);
    wgr_scene_remove(scene, low);
    CHECK(wgr_scene_add(scene, high, 5));
    CHECK(wgr_scene_add(scene, low, 1));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);

    /* move the low one above and it wins instead */
    CHECK(wgr_scene_set_layer(scene, low, 9));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == low);
    CHECK(wgr_scene_set_layer(scene, low, 1));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);

    /* on one layer it's the drawing order that decides: the last one added is on top */
    wgr_scene_remove(scene, high);
    wgr_scene_remove(scene, low);
    CHECK(wgr_scene_add(scene, low, 3));
    CHECK(wgr_scene_add(scene, high, 3));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);
    wgr_scene_remove(scene, high);
    CHECK(wgr_scene_add(scene, high, 3)); /* added again: on top of itself, still last */
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);
    wgr_scene_remove(scene, low);
    CHECK(wgr_scene_add(scene, low, 3)); /* now low was added last */
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == low);

    /* what isn't pickable is skipped, whoever is on top */
    CHECK(wgr_shape2d_set_pickable(low, false));
    CHECK(wgr_scene_pick(scene, 0, 50, 50).handle == high);
    CHECK(wgr_shape2d_set_pickable(high, false));
    CHECK(!wgr_scene_pick(scene, 0, 50, 50).hit);
    CHECK(wgr_shape2d_set_pickable(low, true) && wgr_shape2d_set_pickable(high, true));

    /* and a point outside them both is a miss */
    CHECK(!wgr_scene_pick(scene, 0, 180, 180).hit);

    wgr_scene_destroy(scene);
    wgr_shape2d_destroy(high);
    wgr_shape2d_destroy(low);
    wgr_platform_set_window_size(was_width, was_height);
    end();
}
