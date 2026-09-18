/* Colors are values (docs/PLAN-color.md): packing, the helpers, and what the
 * renderer unpacks them to. */
#include "internal/sk_color.h"
#include "sk_color.h"
#include "test.h"
#include "tests.h"

void test_color_values(void)
{
    /* every built-in matches the components its definition documents: the literal
       and the packing function are checked against each other, not derived */
    CHECK(SK_COLOR_LIGHTGRAY   == sk_color_rgba(200, 200, 200, 255));
    CHECK(SK_COLOR_GRAY        == sk_color_rgba(130, 130, 130, 255));
    CHECK(SK_COLOR_DARKGRAY    == sk_color_rgba( 80,  80,  80, 255));
    CHECK(SK_COLOR_YELLOW      == sk_color_rgba(255, 255,   0, 255));
    CHECK(SK_COLOR_GOLD        == sk_color_rgba(255, 203,   0, 255));
    CHECK(SK_COLOR_ORANGE      == sk_color_rgba(255, 161,   0, 255));
    CHECK(SK_COLOR_PINK        == sk_color_rgba(255, 109, 194, 255));
    CHECK(SK_COLOR_RED         == sk_color_rgba(230,  41,  55, 255));
    CHECK(SK_COLOR_MAROON      == sk_color_rgba(190,  33,  45, 255));
    CHECK(SK_COLOR_GREEN       == sk_color_rgba(  0, 228,  48, 255));
    CHECK(SK_COLOR_LIME        == sk_color_rgba(  0, 158,  47, 255));
    CHECK(SK_COLOR_DARKGREEN   == sk_color_rgba(  0, 117,  44, 255));
    CHECK(SK_COLOR_SKYBLUE     == sk_color_rgba(102, 191, 255, 255));
    CHECK(SK_COLOR_BLUE        == sk_color_rgba(  0, 121, 241, 255));
    CHECK(SK_COLOR_DARKBLUE    == sk_color_rgba(  0,  82, 172, 255));
    CHECK(SK_COLOR_PURPLE      == sk_color_rgba(200, 122, 255, 255));
    CHECK(SK_COLOR_VIOLET      == sk_color_rgba(135,  60, 190, 255));
    CHECK(SK_COLOR_DARKPURPLE  == sk_color_rgba(112,  31, 126, 255));
    CHECK(SK_COLOR_BEIGE       == sk_color_rgba(211, 176, 131, 255));
    CHECK(SK_COLOR_BROWN       == sk_color_rgba(127, 106,  79, 255));
    CHECK(SK_COLOR_DARKBROWN   == sk_color_rgba( 76,  63,  47, 255));
    CHECK(SK_COLOR_WHITE       == sk_color_rgba(255, 255, 255, 255));
    CHECK(SK_COLOR_BLACK       == sk_color_rgba(  0,   0,   0, 255));
    CHECK(SK_COLOR_BLANK       == sk_color_rgba(  0,   0,   0,   0));
    CHECK(SK_COLOR_MAGENTA     == sk_color_rgba(255,   0, 255, 255));
    CHECK(SK_COLOR_RAYWHITE    == sk_color_rgba(245, 245, 245, 255));

    /* packed 0xRRGGBBAA, components clamped */
    CHECK(sk_color_rgba(255, 0, 0, 255) == 0xFF0000FFu);
    CHECK(sk_color_rgba(0, 0, 0, 0) == SK_COLOR_BLANK);
    CHECK(sk_color_rgba(255, 255, 255, 255) == SK_COLOR_WHITE);
    CHECK(sk_color_rgba(230, 41, 55, 255) == SK_COLOR_RED);
    CHECK(sk_color_rgba(300, -5, 128, 999) == 0xFF0080FFu);

    /* alpha */
    CHECK(sk_color_with_alpha(SK_COLOR_RED, 0) == 0xE6293700u);
    CHECK(sk_color_with_alpha(SK_COLOR_BLANK, 255) == 0x000000FFu);
    CHECK(sk_color_with_alpha(SK_COLOR_WHITE, 128) == 0xFFFFFF80u);

    /* blending: endpoints exact, halfway even per component */
    CHECK(sk_color_lerp(SK_COLOR_BLACK, SK_COLOR_WHITE, 0.0f) == SK_COLOR_BLACK);
    CHECK(sk_color_lerp(SK_COLOR_BLACK, SK_COLOR_WHITE, 1.0f) == SK_COLOR_WHITE);
    CHECK(sk_color_lerp(0x000000FFu, 0xFFFFFFFFu, 0.5f) == 0x808080FFu);
    CHECK(sk_color_lerp(SK_COLOR_BLACK, SK_COLOR_WHITE, -1.0f) == SK_COLOR_BLACK); /* clamped */
    CHECK(sk_color_lerp(SK_COLOR_BLACK, SK_COLOR_WHITE, 2.0f) == SK_COLOR_WHITE);

    /* float components, 0..1, rounded to the nearest 8-bit step and clamped */
    CHECK(sk_color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f) == SK_COLOR_WHITE);
    CHECK(sk_color_rgbaf(0.0f, 0.0f, 0.0f, 1.0f) == SK_COLOR_BLACK);
    CHECK(sk_color_rgbaf(0.0f, 0.0f, 0.0f, 0.0f) == SK_COLOR_BLANK);
    CHECK(sk_color_rgbaf(2.0f, -1.0f, 0.5f, 1.0f) == 0xFF0080FFu); /* saturates, never wraps */
    CHECK(sk_color_rgbaf(230.0f / 255.0f, 41.0f / 255.0f, 55.0f / 255.0f, 1.0f) == SK_COLOR_RED);

    /* components back out */
    CHECK(sk_color_get_red(SK_COLOR_RED) == 230);
    CHECK(sk_color_get_green(SK_COLOR_RED) == 41);
    CHECK(sk_color_get_blue(SK_COLOR_RED) == 55);
    CHECK(sk_color_get_alpha(SK_COLOR_RED) == 255);
    CHECK(sk_color_get_alpha(SK_COLOR_BLANK) == 0);
    CHECK(sk_color_get_red(sk_color_rgba(300, 0, 0, 255)) == 255); /* clamped on the way in */

    /* components -> color -> components */
    for (int v = 0; v <= 255; v += 17) {
        const sk_color_t c = sk_color_rgba(v, 255 - v, v / 2, 255);
        CHECK(sk_color_get_red(c) == v);
        CHECK(sk_color_get_green(c) == 255 - v);
        CHECK(sk_color_get_blue(c) == v / 2);
        CHECK(sk_color_rgbaf((float)v / 255.0f, 0.0f, 0.0f, 1.0f) == sk_color_rgba(v, 0, 0, 255));
    }

    /* what the renderer sees: normalized 0..1, alpha included */
    const sk_colorf_t white = sk_color_unpack(SK_COLOR_WHITE);
    CHECK_NEAR(white.r, 1.0, 1e-6);
    CHECK_NEAR(white.a, 1.0, 1e-6);
    const sk_colorf_t blank = sk_color_unpack(SK_COLOR_BLANK);
    CHECK_NEAR(blank.r, 0.0, 1e-6);
    CHECK_NEAR(blank.a, 0.0, 1e-6);
    const sk_colorf_t red = sk_color_unpack(SK_COLOR_RED);
    CHECK_NEAR(red.r, 230.0 / 255.0, 1e-6);
    CHECK_NEAR(red.g, 41.0 / 255.0, 1e-6);
    CHECK_NEAR(red.b, 55.0 / 255.0, 1e-6);

    /* round trip */
    for (int v = 0; v <= 255; v += 17) {
        CHECK_NEAR(sk_color_unpack(sk_color_rgba(v, v, v, v)).g, (double)v / 255.0, 1e-6);
    }
}
