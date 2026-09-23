#ifndef WGR_TEXT2D_H
#define WGR_TEXT2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_text.h" /* wgr_text_align_t, the default font */
#include "wgr_types.h"

/* Text2d object (kind TEXT2D): a placed string drawn from a Font resource,
 * with retained STATE. The string and its placement/color are set once and
 * stored on the object, so your code doesn't re-pass them every frame — but the
 * geometry is still shaped on each wgr_text2d_draw() (it delegates to the
 * immediate wgr_text path), i.e. same per-frame cost as wgr_text_draw_ex(). For
 * normal UI text that cost is negligible; cached/retained geometry would only
 * pay off for very large amounts of text and isn't implemented.
 *
 * `font` may be 0: the text uses the default font (wgr_text_set_default_font, else
 * the built-in font) until a font is attached with wgr_text2d_set_font. So you can
 * create, place and show text before its font asset is ready, the same
 * create-now / set-resource-later pattern as sprite3d, model and sound. The text
 * holds a reference to its font. */
wgr_handle_t wgr_text2d_create(wgr_handle_t font); /* font may be 0 (attach later) */
bool wgr_text2d_set_font(wgr_handle_t handle, wgr_handle_t font);
bool wgr_text2d_set_text(wgr_handle_t handle, const char *text); /* copied */
bool wgr_text2d_set_position(wgr_handle_t handle, float x, float y);
vec2_t wgr_text2d_get_position(wgr_handle_t handle); /* (0, 0) for a handle that isn't one */
bool wgr_text2d_set_size(wgr_handle_t handle, float size);
bool wgr_text2d_set_color(wgr_handle_t handle, wgr_color_t color);
bool wgr_text2d_set_visible(wgr_handle_t handle, bool visible);
bool wgr_text2d_is_visible(wgr_handle_t handle);
/* Picked by its text's rectangle (wgr_pick_object, or wgr_scene_pick when in a
 * scene, where it's drawn over 3D like sprite2d). Default: pickable. */
bool wgr_text2d_set_pickable(wgr_handle_t handle, bool pickable);
bool wgr_text2d_is_pickable(wgr_handle_t handle);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_text2d_set_enabled(wgr_handle_t text, bool enabled);
bool wgr_text2d_is_enabled(wgr_handle_t text);

/* Where the text sits relative to its position: LEFT/CENTER/RIGHT horizontally,
 * TOP/MIDDLE/BOTTOM vertically (default: left, top, so the position is the
 * block's top-left corner). Wrapped lines line up the same way inside the block,
 * and picks use the block's rectangle. False for another axis's value. */
bool wgr_text2d_set_align(wgr_handle_t handle, wgr_text_align_t horizontal, wgr_text_align_t vertical);

/* Wrap the text to `width` logical pixels, between words (a word wider than that
 * keeps a line to itself); 0 turns wrapping off (the default). Newlines in the
 * text always break a line. A wrapped block is `width` wide for alignment and
 * picking, however short its lines are. */
bool wgr_text2d_set_max_width(wgr_handle_t handle, float width);

/* Size of the laid-out text at the current size — the widest line and the lines'
 * total height (0 if no text). Uses the font it draws with (its own, or the
 * default font). */
float wgr_text2d_measure_width(wgr_handle_t handle);
float wgr_text2d_measure_height(wgr_handle_t handle);

void wgr_text2d_draw(wgr_handle_t handle);
void wgr_text2d_destroy(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_TEXT2D_H
