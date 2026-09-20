/* What a text2d object holds besides its layout (src/sk_text2d.c): its font, color and
 * the three flags a scene reads — visible, pickable and enabled — and what happens to
 * its font when it goes. Layout and alignment are covered by test_text2d_layout.
 *
 * Scene layers are here too: which member a pick finds when several sit on different
 * layers over the same point. */
#include "internal/sk_camera3d.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_shape2d.h"
#include "sk_color.h"
#include "sk_font.h"
#include "sk_logger.h"
#include "sk_pick.h"
#include "sk_render.h"
#include "sk_scene.h"
#include "sk_shape2d.h"
#include "sk_text2d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define FONT "../examples/assets/fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define SCREEN 201.0f

static void begin(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_font_init();
    sk_text_init();
    sk_text2d_init();
    sk_shape2d_init();
}

static void end(void)
{
    sk_shape2d_deinit();
    sk_text2d_deinit();
    sk_text_deinit();
    sk_font_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

void test_text2d_state(void)
{
    begin();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* invalid handles below log on purpose */

    const sk_handle_t text = sk_text2d_create(0);
    CHECK(text != 0);
    CHECK(sk_text2d_set_text(text, "pick me"));
    CHECK(sk_text2d_set_size(text, 20));
    CHECK(sk_text2d_set_position(text, 20, 20));
    CHECK(sk_text2d_measure_width(text) > 0.0f);

    /* the flags start on, and each one answers back */
    CHECK(sk_text2d_is_visible(text) && sk_text2d_is_pickable(text) && sk_text2d_is_enabled(text));
    CHECK(sk_text2d_set_visible(text, false) && !sk_text2d_is_visible(text));
    CHECK(sk_text2d_set_pickable(text, false) && !sk_text2d_is_pickable(text));
    CHECK(sk_text2d_set_enabled(text, false) && !sk_text2d_is_enabled(text));
    CHECK(sk_text2d_set_visible(text, true) && sk_text2d_is_visible(text));
    CHECK(sk_text2d_set_pickable(text, true) && sk_text2d_is_pickable(text));
    CHECK(sk_text2d_set_enabled(text, true) && sk_text2d_is_enabled(text));
    CHECK(sk_text2d_set_color(text, SK_COLOR_RED));

    /* hidden or unpickable text isn't hit; the color doesn't matter either way */
    const float inside_x = 22.0f, inside_y = 22.0f;
    CHECK(sk_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(sk_text2d_set_visible(text, false));
    CHECK(!sk_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(sk_text2d_set_visible(text, true));
    CHECK(sk_text2d_set_pickable(text, false));
    CHECK(!sk_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(sk_text2d_set_pickable(text, true));
    /* disabled text still blocks the pointer: it's there, it just doesn't react */
    CHECK(sk_text2d_set_enabled(text, false));
    CHECK(sk_pick_object(text, 0, inside_x, inside_y).hit);
    CHECK(sk_text2d_set_enabled(text, true));

    /* empty text measures nothing and can't be hit */
    const sk_handle_t empty = sk_text2d_create(0);
    CHECK(sk_text2d_set_position(empty, 20, 20));
    CHECK(sk_text2d_measure_width(empty) == 0.0f);
    CHECK(sk_text2d_measure_height(empty) == 0.0f);
    CHECK(!sk_pick_object(empty, 0, inside_x, inside_y).hit);
    CHECK(sk_text2d_set_text(empty, "")); /* the same, said explicitly */
    CHECK(sk_text2d_measure_width(empty) == 0.0f);
    sk_text2d_destroy(empty);

    /* a font of its own: the text holds a reference, and gives it back when it goes */
    const sk_handle_t font = sk_font_create(FONT);
    CHECK(font != 0);
    CHECK(sk_text2d_set_font(text, font));
    const float default_width = sk_text2d_measure_width(text);
    CHECK(default_width > 0.0f);
    sk_font_release(font);             /* the text is the only holder now */
    CHECK(sk_text2d_set_font(text, 0)); /* back to the built-in font */
    CHECK(sk_text2d_measure_width(text) > 0.0f);

    const sk_handle_t kept = sk_font_create(FONT);
    CHECK(sk_text2d_set_font(text, kept));
    sk_font_release(kept);
    sk_text2d_destroy(text); /* releases the font with it */

    /* every setter refuses a handle that isn't a text2d, and says so */
    CHECK(!sk_text2d_set_text(0, "x"));
    CHECK(!sk_text2d_set_size(text, 12)); /* destroyed above */
    CHECK(!sk_text2d_set_color(text, SK_COLOR_RED));
    CHECK(!sk_text2d_set_visible(text, true));
    CHECK(!sk_text2d_set_pickable(text, true));
    CHECK(!sk_text2d_set_enabled(text, true));
    CHECK(!sk_text2d_set_font(text, 0));
    CHECK(!sk_text2d_is_visible(text) && !sk_text2d_is_pickable(text));
    CHECK(sk_text2d_measure_width(text) == 0.0f);
    sk_text2d_destroy(text); /* twice is harmless */

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    end();
}

/* Scene layers order 2D members: over the same point, the higher layer is found
 * first, whatever order they were added in, and moving a member between layers
 * changes which one wins. */
void test_scene_layer_order(void)
{
    const int was_width = sk_platform_width(), was_height = sk_platform_height();

    begin();
    sk_platform_set_window_size((int)SCREEN, (int)SCREEN);

    const sk_handle_t scene = sk_scene_create();
    const sk_handle_t low = sk_shape2d_create();
    const sk_handle_t high = sk_shape2d_create();
    CHECK(sk_shape2d_set_rectangle(low, 100, 100, 0));
    CHECK(sk_shape2d_set_rectangle(high, 100, 100, 0));
    CHECK(sk_shape2d_set_transform(low, 10, 10, 0, 1, 1));
    CHECK(sk_shape2d_set_transform(high, 10, 10, 0, 1, 1)); /* exactly on top of each other */

    /* added low-layer first, then high */
    CHECK(sk_scene_add(scene, low, 1));
    CHECK(sk_scene_add(scene, high, 5));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);

    /* the order they were added in doesn't decide it: the layer does */
    sk_scene_remove(scene, high);
    sk_scene_remove(scene, low);
    CHECK(sk_scene_add(scene, high, 5));
    CHECK(sk_scene_add(scene, low, 1));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);

    /* move the low one above and it wins instead */
    CHECK(sk_scene_set_layer(scene, low, 9));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == low);
    CHECK(sk_scene_set_layer(scene, low, 1));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);

    /* on one layer it's the drawing order that decides: the last one added is on top */
    sk_scene_remove(scene, high);
    sk_scene_remove(scene, low);
    CHECK(sk_scene_add(scene, low, 3));
    CHECK(sk_scene_add(scene, high, 3));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);
    sk_scene_remove(scene, high);
    CHECK(sk_scene_add(scene, high, 3)); /* added again: on top of itself, still last */
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);
    sk_scene_remove(scene, low);
    CHECK(sk_scene_add(scene, low, 3)); /* now low was added last */
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == low);

    /* what isn't pickable is skipped, whoever is on top */
    CHECK(sk_shape2d_set_pickable(low, false));
    CHECK(sk_scene_pick(scene, 0, 50, 50).handle == high);
    CHECK(sk_shape2d_set_pickable(high, false));
    CHECK(!sk_scene_pick(scene, 0, 50, 50).hit);
    CHECK(sk_shape2d_set_pickable(low, true) && sk_shape2d_set_pickable(high, true));

    /* and a point outside them both is a miss */
    CHECK(!sk_scene_pick(scene, 0, 180, 180).hit);

    sk_scene_destroy(scene);
    sk_shape2d_destroy(high);
    sk_shape2d_destroy(low);
    sk_platform_set_window_size(was_width, was_height);
    end();
}
