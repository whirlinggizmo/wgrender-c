#ifndef SK_TEXT_H
#define SK_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* First-milestone text uses sokol_debugtext (built-in bitmap fonts), so there
 * is no font handle yet. fontSize is interpreted as a pixel height and mapped
 * to an integer glyph scale. */

void sk_text_draw_fps(int x, int y);
/* The FPS counter in a TrueType font (0 = built-in), size in pixels, and color. */
void sk_text_draw_fps_ex(sk_handle_t font, float x, float y, float size, sk_handle_t color);
void sk_text_draw(const char *text, int x, int y, int font_size, sk_handle_t color);
int sk_text_measure(const char *text, int font_size);

/* TrueType text via a font handle (fontstash). (x, y) is the top-left of the
 * text; size is the pixel height. If `font` is 0/invalid this falls back to the
 * built-in bitmap font. */
void   sk_text_draw_ex(sk_handle_t font, const char *text, float x, float y,
                       float size, sk_handle_t color);
vec2_t sk_text_measure_ex(sk_handle_t font, const char *text, float size);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXT_H
