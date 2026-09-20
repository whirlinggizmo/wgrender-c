#ifndef WGR_TEXT_H
#define WGR_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Where a block of text sits relative to its position, per axis: LEFT / CENTER /
 * RIGHT horizontally, TOP / MIDDLE / BOTTOM vertically. Wrapped lines line up the
 * same way inside the block. */
typedef enum
{
    WGR_TEXT_ALIGN_LEFT = 0,
    WGR_TEXT_ALIGN_CENTER = 1,
    WGR_TEXT_ALIGN_RIGHT = 2,
    WGR_TEXT_ALIGN_TOP = 3,
    WGR_TEXT_ALIGN_MIDDLE = 4,
    WGR_TEXT_ALIGN_BOTTOM = 5
} wgr_text_align_t;

/* The default font: what wgr_text_draw, wgr_text_measure and wgr_text_draw_fps use,
 * and what font handle 0 means everywhere (wgr_text_draw_ex, text2d, text3d).
 *
 * Built in, it's JetBrains Mono (printable ASCII only, embedded in the library).
 * Set a font of your own for other characters (UTF-8) or another look. The default
 * font holds a reference to the font, so it stays loaded after wgr_font_release until
 * the default changes. Pass 0 to go back to the built-in font. False for a handle
 * that isn't a font.
 *
 * Sizes are pixel sizes; a size <= 0 means 16. */
bool        wgr_text_set_default_font(wgr_handle_t font);
wgr_handle_t wgr_text_get_default_font(void); /* 0 = built in */

void wgr_text_draw_fps(int x, int y);
/* The FPS counter in a TrueType font (0 = the default font), size in pixels, and color. */
void wgr_text_draw_fps_ex(wgr_handle_t font, float x, float y, float size, wgr_color_t color);
void wgr_text_draw(const char *text, int x, int y, int font_size, wgr_color_t color);
int wgr_text_measure(const char *text, int font_size);

/* Text in a font (0 = the default font). (x, y) is the top-left of the text; size
 * is the pixel height. A font that isn't loaded (invalid or destroyed handle)
 * falls back to the built-in font. */
void   wgr_text_draw_ex(wgr_handle_t font, const char *text, float x, float y,
                       float size, wgr_color_t color);
vec2_t wgr_text_measure_ex(wgr_handle_t font, const char *text, float size);

/* The same for `length` bytes of `text` (a slice of a longer string, as layout
 * libraries pass it); a negative length means up to the NUL. */
void   wgr_text_draw_n(wgr_handle_t font, const char *text, int length, float x, float y,
                      float size, wgr_color_t color);
vec2_t wgr_text_measure_n(wgr_handle_t font, const char *text, int length, float size);

#ifdef __cplusplus
}
#endif

#endif // WGR_TEXT_H
