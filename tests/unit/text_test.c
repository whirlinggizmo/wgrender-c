#include <math.h>
/* The default font (wgr_text_set_default_font), on sokol's dummy backend. */
#include <string.h>

#include "internal/wgr_font_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "wgr_font.h"
#include "wgr_logger.h"
#include "wgr_text.h"
#include "wgr_text3d.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_color.h"
#include "wgr_render.h"
#include "wgr_texture.h"
#include "test.h"
#include "tests.h"

#include "fontstash.h"
#include "sokol_gfx.h"

#define FONT "../examples/assets/fonts/Komika/KOMIKAH_.ttf"

void test_text_default_font(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_font_init();
    wgr_text_init();
    wgr_text3d_init();

    /* built in: JetBrains Mono, used for 0 everywhere */
    CHECK(wgr_text_get_default_font() == 0);
    const vec2_t builtin = wgr_text_measure_ex(0, "[ab]", 16.0f);
    CHECK(builtin.x > 20.0f && builtin.x < 48.0f && builtin.y > 10.0f);
    CHECK(wgr_text_measure("[ab]", 16) == (int)(builtin.x + 0.5f));
    CHECK(wgr_text_measure_ex(0, "[ab]", 0.0f).x == builtin.x); /* size <= 0: 16 */

    wgr_handle_t label = wgr_text3d_create(0);
    wgr_text3d_set_text(label, "label");
    wgr_text3d_set_size(label, 1.0f);
    const float label_width = wgr_text3d_get_size(label).x;
    CHECK(label_width > 0.0f); /* 3D text with the built-in font */

    /* a font of our own as the default */
    wgr_handle_t font = wgr_font_create(FONT);
    CHECK(font != 0);
    CHECK(wgr_text_set_default_font(font));
    CHECK(wgr_text_get_default_font() == font);
    const vec2_t comic = wgr_text_measure_ex(font, "[ab]", 16.0f);
    CHECK(comic.x != builtin.x);
    CHECK(wgr_text_measure_ex(0, "[ab]", 16.0f).x == comic.x);
    CHECK(wgr_text_measure("[ab]", 16) == (int)(comic.x + 0.5f));
    CHECK(wgr_text3d_get_size(label).x != label_width);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(!wgr_text_set_default_font(12345)); /* not a font */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    CHECK(wgr_text_get_default_font() == font);

    /* the default font holds a reference: destroying ours keeps it loaded */
    wgr_font_release(font);
    CHECK(wgr_text_measure_ex(0, "[ab]", 16.0f).x == comic.x);
    CHECK(wgr_text_set_default_font(0)); /* its reference goes: the font is freed */
    CHECK(wgr_font_fons_id(font) == FONS_INVALID);
    CHECK(wgr_text_measure_ex(0, "[ab]", 16.0f).x == builtin.x);
    CHECK(wgr_text_measure_ex(font, "[ab]", 16.0f).x == builtin.x); /* freed handle: built in */
    CHECK(wgr_text3d_get_size(label).x == label_width);

    wgr_text3d_destroy(label);
    wgr_text3d_deinit();
    wgr_text_deinit();
    wgr_font_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* Fonts are refcounted and deduped by path; text objects hold references; a released
 * font's fontstash data is reused when its path is created again. */
void test_text_font_refcount(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_font_init();
    wgr_text3d_init();

    const wgr_handle_t font = wgr_font_create(FONT);
    const int fons_id = wgr_font_fons_id(font);
    CHECK(font != 0 && fons_id != FONS_INVALID);
    CHECK(wgr_font_create(FONT) == font); /* deduped: a second reference */
    wgr_font_release(font);
    CHECK(wgr_font_fons_id(font) == fons_id); /* one reference left */

    wgr_handle_t label = wgr_text3d_create(font); /* the text's reference */
    wgr_font_release(font);                      /* the last of ours */
    CHECK(wgr_font_fons_id(font) == fons_id);   /* the text keeps it */
    wgr_text3d_set_text(label, "x");
    CHECK(wgr_text3d_get_size(label).x > 0.0f);

    wgr_text3d_destroy(label); /* the last reference: freed */
    CHECK(wgr_font_fons_id(font) == FONS_INVALID);

    const wgr_handle_t again = wgr_font_create(FONT); /* reuses the parked fontstash font */
    CHECK(again != 0 && wgr_font_fons_id(again) == fons_id);
    wgr_font_release(again);

    wgr_text3d_deinit();
    wgr_font_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}

/* Text slices, DPI-correct rasterization and a growing glyph atlas (docs/PLAN-ui.md,
 * step 2). */
void test_text_slices_and_dpi(void)
{
    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_texture_init();
    wgr_render_init();
    wgr_font_init();
    wgr_text_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);

    /* slices: `length` bytes of a longer string, negative meaning up to the NUL */
    const vec2_t hello = wgr_text_measure_ex(0, "hello", 16.0f);
    const vec2_t slice = wgr_text_measure_n(0, "hello world", 5, 16.0f);
    CHECK(slice.x == hello.x && slice.y == hello.y);
    const vec2_t whole = wgr_text_measure_n(0, "hello world", -1, 16.0f);
    CHECK(whole.x == wgr_text_measure_ex(0, "hello world", 16.0f).x);
    CHECK(wgr_text_measure_n(0, "hello", 0, 16.0f).x == 0.0f && wgr_text_measure_n(0, "hello", 0, 16.0f).y == 0.0f);
    CHECK_NEAR(wgr_text_measure_n(0, "a\nb", 3, 16.0f).y, hello.y * 2.0f, 0.01); /* the newline is inside */
    CHECK_NEAR(wgr_text_measure_n(0, "a\nb", 1, 16.0f).y, hello.y, 0.01);        /* ... and here it isn't */

    /* at 2x DPI glyphs are rasterized twice as large, but measurement stays logical.
       fontstash rounds each glyph's advance to a whole rasterized pixel, so a width is
       off by up to half a pixel per glyph at 1x, a quarter at 2x, a sixth at 3x: the
       higher the DPI, the closer to the true width (measured here at 10x the size,
       where the rounding is negligible). Measuring and drawing round alike, so layout
       is consistent on any one screen. */
    const int glyphs = 12;
    const float exact = wgr_text_measure_ex(0, "Hello, world", 160.0f).x / 10.0f;
    const vec2_t at1 = wgr_text_measure_ex(0, "Hello, world", 16.0f);
    CHECK(fabsf(at1.x - exact) <= 0.5f * glyphs + 0.1f);
    wgr_platform_set_headless_dpi_scale(2.0f);
    const vec2_t at2 = wgr_text_measure_ex(0, "Hello, world", 16.0f);
    CHECK(fabsf(at2.x - exact) <= 0.25f * glyphs + 0.1f);
    CHECK_NEAR(at2.y, at1.y, 1.0); /* the line height stays logical too */
    wgr_platform_set_headless_dpi_scale(3.0f);
    CHECK(fabsf(wgr_text_measure_ex(0, "Hello, world", 16.0f).x - exact) <= 0.5f / 3.0f * glyphs + 0.1f);

    /* inside a render target the target's own pixels count: scale 1 */
    const wgr_handle_t target = wgr_texture_create_target(64, 32);
    wgr_render_begin();
    CHECK_NEAR(wgr_render_pixel_scale(), 3.0, 1e-6);
    CHECK(wgr_render_begin_texture(target));
    CHECK_NEAR(wgr_render_pixel_scale(), 1.0, 1e-6);
    CHECK(wgr_text_measure_ex(0, "Hello, world", 16.0f).x == at1.x); /* the same as at 1x */
    wgr_render_end_texture();
    wgr_text_draw_n(0, "hello world", 5, 10.5f, 10.5f, 16.0f, WGR_COLOR_WHITE); /* draws without trouble */
    wgr_render_end();
    wgr_platform_set_headless_dpi_scale(1.0f);

    /* the glyph atlas grows instead of dropping glyphs: capitals at 300 and 400 px
       don't fit in 1024 x 1024 */
    int width = 0, height = 0;
    fonsGetAtlasSize(wgr_font_context(), &width, &height);
    CHECK(width == 1024 && height == 1024);
    wgr_render_begin();
    wgr_text_draw_ex(0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0, 0, 300.0f, WGR_COLOR_WHITE);
    wgr_text_draw_ex(0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0, 0, 400.0f, WGR_COLOR_WHITE);
    wgr_render_end();
    fonsGetAtlasSize(wgr_font_context(), &width, &height);
    CHECK(width * height > 1024 * 1024); /* grown once the frame was submitted, not during it */
    wgr_render_begin();                   /* and the next frame draws from the bigger atlas */
    wgr_text_draw_ex(0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0, 0, 300.0f, WGR_COLOR_WHITE);
    wgr_text_draw_ex(0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0, 0, 400.0f, WGR_COLOR_WHITE);
    wgr_render_end();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_texture_release(target);
    wgr_text_deinit();
    wgr_font_deinit();
    wgr_render_deinit();
    wgr_texture_deinit();
    sg_shutdown();
}
