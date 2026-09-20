/* Colors are values (docs/PLAN-color.md): packing, the helpers, and what the
 * renderer unpacks them to. */
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "test.h"
#include "tests.h"

void test_color_values(void)
{
    /* every built-in matches the components its definition documents: the literal
       and the packing function are checked against each other, not derived */
    CHECK(WGR_COLOR_LIGHTGRAY   == wgr_color_rgba(200, 200, 200, 255));
    CHECK(WGR_COLOR_GRAY        == wgr_color_rgba(130, 130, 130, 255));
    CHECK(WGR_COLOR_DARKGRAY    == wgr_color_rgba( 80,  80,  80, 255));
    CHECK(WGR_COLOR_YELLOW      == wgr_color_rgba(255, 255,   0, 255));
    CHECK(WGR_COLOR_GOLD        == wgr_color_rgba(255, 203,   0, 255));
    CHECK(WGR_COLOR_ORANGE      == wgr_color_rgba(255, 161,   0, 255));
    CHECK(WGR_COLOR_PINK        == wgr_color_rgba(255, 109, 194, 255));
    CHECK(WGR_COLOR_RED         == wgr_color_rgba(230,  41,  55, 255));
    CHECK(WGR_COLOR_MAROON      == wgr_color_rgba(190,  33,  45, 255));
    CHECK(WGR_COLOR_GREEN       == wgr_color_rgba(  0, 228,  48, 255));
    CHECK(WGR_COLOR_LIME        == wgr_color_rgba(  0, 158,  47, 255));
    CHECK(WGR_COLOR_DARKGREEN   == wgr_color_rgba(  0, 117,  44, 255));
    CHECK(WGR_COLOR_SKYBLUE     == wgr_color_rgba(102, 191, 255, 255));
    CHECK(WGR_COLOR_BLUE        == wgr_color_rgba(  0, 121, 241, 255));
    CHECK(WGR_COLOR_DARKBLUE    == wgr_color_rgba(  0,  82, 172, 255));
    CHECK(WGR_COLOR_PURPLE      == wgr_color_rgba(200, 122, 255, 255));
    CHECK(WGR_COLOR_VIOLET      == wgr_color_rgba(135,  60, 190, 255));
    CHECK(WGR_COLOR_DARKPURPLE  == wgr_color_rgba(112,  31, 126, 255));
    CHECK(WGR_COLOR_BEIGE       == wgr_color_rgba(211, 176, 131, 255));
    CHECK(WGR_COLOR_BROWN       == wgr_color_rgba(127, 106,  79, 255));
    CHECK(WGR_COLOR_DARKBROWN   == wgr_color_rgba( 76,  63,  47, 255));
    CHECK(WGR_COLOR_WHITE       == wgr_color_rgba(255, 255, 255, 255));
    CHECK(WGR_COLOR_BLACK       == wgr_color_rgba(  0,   0,   0, 255));
    CHECK(WGR_COLOR_BLANK       == wgr_color_rgba(  0,   0,   0,   0));
    CHECK(WGR_COLOR_MAGENTA     == wgr_color_rgba(255,   0, 255, 255));
    CHECK(WGR_COLOR_RAYWHITE    == wgr_color_rgba(245, 245, 245, 255));

    /* packed 0xRRGGBBAA, components clamped */
    CHECK(wgr_color_rgba(255, 0, 0, 255) == 0xFF0000FFu);
    CHECK(wgr_color_rgba(0, 0, 0, 0) == WGR_COLOR_BLANK);
    CHECK(wgr_color_rgba(255, 255, 255, 255) == WGR_COLOR_WHITE);
    CHECK(wgr_color_rgba(230, 41, 55, 255) == WGR_COLOR_RED);
    CHECK(wgr_color_rgba(300, -5, 128, 999) == 0xFF0080FFu);

    /* alpha */
    CHECK(wgr_color_with_alpha(WGR_COLOR_RED, 0) == 0xE6293700u);
    CHECK(wgr_color_with_alpha(WGR_COLOR_BLANK, 255) == 0x000000FFu);
    CHECK(wgr_color_with_alpha(WGR_COLOR_WHITE, 128) == 0xFFFFFF80u);

    /* blending: endpoints exact, halfway even per component */
    CHECK(wgr_color_lerp(WGR_COLOR_BLACK, WGR_COLOR_WHITE, 0.0f) == WGR_COLOR_BLACK);
    CHECK(wgr_color_lerp(WGR_COLOR_BLACK, WGR_COLOR_WHITE, 1.0f) == WGR_COLOR_WHITE);
    CHECK(wgr_color_lerp(0x000000FFu, 0xFFFFFFFFu, 0.5f) == 0x808080FFu);
    CHECK(wgr_color_lerp(WGR_COLOR_BLACK, WGR_COLOR_WHITE, -1.0f) == WGR_COLOR_BLACK); /* clamped */
    CHECK(wgr_color_lerp(WGR_COLOR_BLACK, WGR_COLOR_WHITE, 2.0f) == WGR_COLOR_WHITE);

    /* float components, 0..1, rounded to the nearest 8-bit step and clamped */
    CHECK(wgr_color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f) == WGR_COLOR_WHITE);
    CHECK(wgr_color_rgbaf(0.0f, 0.0f, 0.0f, 1.0f) == WGR_COLOR_BLACK);
    CHECK(wgr_color_rgbaf(0.0f, 0.0f, 0.0f, 0.0f) == WGR_COLOR_BLANK);
    CHECK(wgr_color_rgbaf(2.0f, -1.0f, 0.5f, 1.0f) == 0xFF0080FFu); /* saturates, never wraps */
    CHECK(wgr_color_rgbaf(230.0f / 255.0f, 41.0f / 255.0f, 55.0f / 255.0f, 1.0f) == WGR_COLOR_RED);

    /* components back out */
    CHECK(wgr_color_get_red(WGR_COLOR_RED) == 230);
    CHECK(wgr_color_get_green(WGR_COLOR_RED) == 41);
    CHECK(wgr_color_get_blue(WGR_COLOR_RED) == 55);
    CHECK(wgr_color_get_alpha(WGR_COLOR_RED) == 255);
    CHECK(wgr_color_get_alpha(WGR_COLOR_BLANK) == 0);
    CHECK(wgr_color_get_red(wgr_color_rgba(300, 0, 0, 255)) == 255); /* clamped on the way in */

    /* components -> color -> components */
    for (int v = 0; v <= 255; v += 17) {
        const wgr_color_t c = wgr_color_rgba(v, 255 - v, v / 2, 255);
        CHECK(wgr_color_get_red(c) == v);
        CHECK(wgr_color_get_green(c) == 255 - v);
        CHECK(wgr_color_get_blue(c) == v / 2);
        CHECK(wgr_color_rgbaf((float)v / 255.0f, 0.0f, 0.0f, 1.0f) == wgr_color_rgba(v, 0, 0, 255));
    }

    /* what the renderer sees: normalized 0..1, alpha included */
    const wgri_colorf_t white = wgri_color_unpack(WGR_COLOR_WHITE);
    CHECK_NEAR(white.r, 1.0, 1e-6);
    CHECK_NEAR(white.a, 1.0, 1e-6);
    const wgri_colorf_t blank = wgri_color_unpack(WGR_COLOR_BLANK);
    CHECK_NEAR(blank.r, 0.0, 1e-6);
    CHECK_NEAR(blank.a, 0.0, 1e-6);
    const wgri_colorf_t red = wgri_color_unpack(WGR_COLOR_RED);
    CHECK_NEAR(red.r, 230.0 / 255.0, 1e-6);
    CHECK_NEAR(red.g, 41.0 / 255.0, 1e-6);
    CHECK_NEAR(red.b, 55.0 / 255.0, 1e-6);

    /* round trip */
    for (int v = 0; v <= 255; v += 17) {
        CHECK_NEAR(wgri_color_unpack(wgr_color_rgba(v, v, v, v)).g, (double)v / 255.0, 1e-6);
    }
}
