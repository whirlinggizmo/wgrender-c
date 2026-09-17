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
 * `font` may be 0: the text uses the default font (sk_text_set_default_font, else
 * the built-in font) until a font is attached with sk_text2d_set_font. So you can
 * create, place and show text before its font asset is ready, the same
 * create-now / set-resource-later pattern as sprite3d, model and sound. The text
 * holds a reference to its font. */
sk_handle_t sk_text2d_create(sk_handle_t font); /* font may be 0 (attach later) */
bool sk_text2d_set_font(sk_handle_t handle, sk_handle_t font);
bool sk_text2d_set_text(sk_handle_t handle, const char *text); /* copied */
bool sk_text2d_set_position(sk_handle_t handle, float x, float y);
bool sk_text2d_set_size(sk_handle_t handle, float size);
bool sk_text2d_set_color(sk_handle_t handle, sk_handle_t color);
bool sk_text2d_set_visible(sk_handle_t handle, bool visible);
bool sk_text2d_is_visible(sk_handle_t handle);
/* Picked by its text's rectangle (sk_pick_object, or sk_scene_pick when in a
 * scene, where it's drawn over 3D like sprite2d). Default: pickable. */
bool sk_text2d_set_pickable(sk_handle_t handle, bool pickable);
bool sk_text2d_is_pickable(sk_handle_t handle);

/* Rendered extent of the current text at the current size (0 if no text). Uses
 * the font it draws with (its own, or the default font). */
float sk_text2d_measure_width(sk_handle_t handle);
float sk_text2d_measure_height(sk_handle_t handle);

void sk_text2d_draw(sk_handle_t handle);
void sk_text2d_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXT2D_H
