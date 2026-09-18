#ifndef SK_TEXT_H
#define SK_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Where a block of text sits relative to its position, per axis: LEFT / CENTER /
 * RIGHT horizontally, TOP / MIDDLE / BOTTOM vertically. Wrapped lines line up the
 * same way inside the block. */
typedef enum
{
    SK_TEXT_ALIGN_LEFT = 0,
    SK_TEXT_ALIGN_CENTER = 1,
    SK_TEXT_ALIGN_RIGHT = 2,
    SK_TEXT_ALIGN_TOP = 3,
    SK_TEXT_ALIGN_MIDDLE = 4,
    SK_TEXT_ALIGN_BOTTOM = 5
} sk_text_align_t;

/* The default font: what sk_text_draw, sk_text_measure and sk_text_draw_fps use,
 * and what font handle 0 means everywhere (sk_text_draw_ex, text2d, text3d).
 *
 * Built in, it's JetBrains Mono (printable ASCII only, embedded in the library).
 * Set a font of your own for other characters (UTF-8) or another look. The default
 * font holds a reference to the font, so it stays loaded after sk_font_release until
 * the default changes. Pass 0 to go back to the built-in font. False for a handle
 * that isn't a font.
 *
 * Sizes are pixel sizes; a size <= 0 means 16. */
bool        sk_text_set_default_font(sk_handle_t font);
sk_handle_t sk_text_get_default_font(void); /* 0 = built in */

void sk_text_draw_fps(int x, int y);
/* The FPS counter in a TrueType font (0 = the default font), size in pixels, and color. */
void sk_text_draw_fps_ex(sk_handle_t font, float x, float y, float size, sk_color_t color);
void sk_text_draw(const char *text, int x, int y, int font_size, sk_color_t color);
int sk_text_measure(const char *text, int font_size);

/* Text in a font (0 = the default font). (x, y) is the top-left of the text; size
 * is the pixel height. A font that isn't loaded (invalid or destroyed handle)
 * falls back to the built-in font. */
void   sk_text_draw_ex(sk_handle_t font, const char *text, float x, float y,
                       float size, sk_color_t color);
vec2_t sk_text_measure_ex(sk_handle_t font, const char *text, float size);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXT_H
