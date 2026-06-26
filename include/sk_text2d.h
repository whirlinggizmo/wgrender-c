#ifndef SK_TEXT2D_H
#define SK_TEXT2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Text2d object (kind TEXT2D): a placed string drawn from a Font resource,
 * with retained STATE. The string and its placement/color are set once and
 * stored on the object, so your code doesn't re-pass them every frame — but the
 * geometry is still shaped on each sk_text2d_draw() (it delegates to the
 * immediate sk_text path), i.e. same per-frame cost as sk_text_draw_ex(). For
 * normal UI text that cost is negligible; cached/retained geometry would only
 * pay off for very large amounts of text and isn't implemented.
 *
 * `font` may be 0 (or a font whose asset hasn't finished loading): the object
 * renders with the built-in bitmap fallback font and sharpens to the TTF once a
 * ready font is attached via sk_text2d_set_font. So you can create, place, and
 * show text before its font asset is ready — the same create-now / set-resource-
 * later pattern as sprite3d/model/sound. */
sk_handle_t sk_text2d_create(sk_handle_t font); /* font may be 0 (attach later) */
bool sk_text2d_set_font(sk_handle_t handle, sk_handle_t font);
bool sk_text2d_set_text(sk_handle_t handle, const char *text); /* copied */
bool sk_text2d_set_position(sk_handle_t handle, float x, float y);
bool sk_text2d_set_size(sk_handle_t handle, float size);
bool sk_text2d_set_color(sk_handle_t handle, sk_handle_t color);
bool sk_text2d_set_visible(sk_handle_t handle, bool visible);
bool sk_text2d_is_visible(sk_handle_t handle);

/* Rendered extent of the current text at the current size (0 if no text). Uses
 * TTF metrics when the font is ready, else the bitmap fallback's. */
float sk_text2d_measure_width(sk_handle_t handle);
float sk_text2d_measure_height(sk_handle_t handle);

void sk_text2d_draw(sk_handle_t handle);
void sk_text2d_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXT2D_H
