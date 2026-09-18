/* Colors are values (docs/PLAN-color.md): packing, the helpers, and what the
 * renderer unpacks them to. */
#include "internal/sk_color.h"
#include "sk_color.h"
#include "test.h"
#include "tests.h"

void test_color_values(void)
{
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
