/* The default font (sk_text_set_default_font), on sokol's dummy backend. */
#include <string.h>

#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "sk_font.h"
#include "sk_logger.h"
#include "sk_text.h"
#include "sk_text3d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define FONT "../examples/assets/fonts/Komika/KOMIKAH_.ttf"

void test_text_default_font(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_font_init();
    sk_text_init();
    sk_text3d_init();

    /* built in: JetBrains Mono, used for 0 everywhere */
    CHECK(sk_text_get_default_font() == 0);
    const vec2_t builtin = sk_text_measure_ex(0, "[ab]", 16.0f);
    CHECK(builtin.x > 20.0f && builtin.x < 48.0f && builtin.y > 10.0f);
    CHECK(sk_text_measure("[ab]", 16) == (int)(builtin.x + 0.5f));
    CHECK(sk_text_measure_ex(0, "[ab]", 0.0f).x == builtin.x); /* size <= 0: 16 */

    sk_handle_t label = sk_text3d_create(0);
    sk_text3d_set_text(label, "label");
    sk_text3d_set_size(label, 1.0f);
    const float label_width = sk_text3d_get_size(label).x;
    CHECK(label_width > 0.0f); /* 3D text with the built-in font */

    /* a font of our own as the default */
    sk_handle_t font = sk_font_create(FONT);
    CHECK(font != 0);
    CHECK(sk_text_set_default_font(font));
    CHECK(sk_text_get_default_font() == font);
    const vec2_t comic = sk_text_measure_ex(font, "[ab]", 16.0f);
    CHECK(comic.x != builtin.x);
    CHECK(sk_text_measure_ex(0, "[ab]", 16.0f).x == comic.x);
    CHECK(sk_text_measure("[ab]", 16) == (int)(comic.x + 0.5f));
    CHECK(sk_text3d_get_size(label).x != label_width);

    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(!sk_text_set_default_font(12345)); /* not a font */
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    CHECK(sk_text_get_default_font() == font);

    /* the default font holds a reference: destroying ours keeps it loaded */
    sk_font_release(font);
    CHECK(sk_text_measure_ex(0, "[ab]", 16.0f).x == comic.x);
    CHECK(sk_text_set_default_font(0)); /* its reference goes: the font is freed */
    CHECK(sk_font_fons_id(font) == FONS_INVALID);
    CHECK(sk_text_measure_ex(0, "[ab]", 16.0f).x == builtin.x);
    CHECK(sk_text_measure_ex(font, "[ab]", 16.0f).x == builtin.x); /* freed handle: built in */
    CHECK(sk_text3d_get_size(label).x == label_width);

    sk_text3d_destroy(label);
    sk_text3d_deinit();
    sk_text_deinit();
    sk_font_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

/* Fonts are refcounted and deduped by path; text objects hold references; a released
 * font's fontstash data is reused when its path is created again. */
void test_text_font_refcount(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_font_init();
    sk_text3d_init();

    const sk_handle_t font = sk_font_create(FONT);
    const int fons_id = sk_font_fons_id(font);
    CHECK(font != 0 && fons_id != FONS_INVALID);
    CHECK(sk_font_create(FONT) == font); /* deduped: a second reference */
    sk_font_release(font);
    CHECK(sk_font_fons_id(font) == fons_id); /* one reference left */

    sk_handle_t label = sk_text3d_create(font); /* the text's reference */
    sk_font_release(font);                      /* the last of ours */
    CHECK(sk_font_fons_id(font) == fons_id);   /* the text keeps it */
    sk_text3d_set_text(label, "x");
    CHECK(sk_text3d_get_size(label).x > 0.0f);

    sk_text3d_destroy(label); /* the last reference: freed */
    CHECK(sk_font_fons_id(font) == FONS_INVALID);

    const sk_handle_t again = sk_font_create(FONT); /* reuses the parked fontstash font */
    CHECK(again != 0 && sk_font_fons_id(again) == fons_id);
    sk_font_release(again);

    sk_text3d_deinit();
    sk_font_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
