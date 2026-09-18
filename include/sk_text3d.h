#ifndef SK_TEXT3D_H
#define SK_TEXT3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_sprite3d.h"
#include "sk_types.h"

/* Text3d object (kind TEXT3D): a string placed in the 3D world, drawn with a
 * TrueType font (sk_font_create), centered on its position.
 *
 * - size is the font size in world units (default 1): roughly the height from the
 *   lowest descender to the highest ascender.
 * - Facing uses sprite3d's modes: face the camera (default), face it with world up
 *   kept, lie flat facing up, or FREE (oriented by the rotation, like a sign).
 * - Depth-tested against the scene; a scene sorts it with other transparent parts.
 * - Font 0 (at create, or until set) draws with the default font
 *   (sk_text_set_default_font, else the built-in font). The text holds a reference
 *   to its font.
 * - Picked by its text's rectangle (sk_pick_object, sk_scene_pick). */

sk_handle_t sk_text3d_create(sk_handle_t font);
void        sk_text3d_destroy(sk_handle_t text);
bool        sk_text3d_set_font(sk_handle_t text, sk_handle_t font);
bool        sk_text3d_set_text(sk_handle_t text, const char *string); /* copied */
bool        sk_text3d_set_size(sk_handle_t text, float size);
bool        sk_text3d_set_transform(sk_handle_t text, float x, float y, float z,
                                    float rotation_x, float rotation_y, float rotation_z); /* radians */
bool        sk_text3d_set_facing(sk_handle_t text, sk_sprite3d_facing_t facing);
bool        sk_text3d_set_color(sk_handle_t text, sk_color_t color);
bool        sk_text3d_set_visible(sk_handle_t text, bool visible);
bool        sk_text3d_is_visible(sk_handle_t text);
bool        sk_text3d_set_pickable(sk_handle_t text, bool pickable); /* default: pickable */
bool        sk_text3d_is_pickable(sk_handle_t text);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_text3d_set_enabled(sk_handle_t text, bool enabled);
bool sk_text3d_is_enabled(sk_handle_t text);
/* World-space width and height of the current text ((0, 0) until the font loads). */
vec2_t      sk_text3d_get_size(sk_handle_t text);
/* Draw now (inside 3D mode); scenes draw their members themselves. */
void        sk_text3d_draw(sk_handle_t text);

/* Draw text once at a 3D point, facing the camera, line height `size` in world
 * units (inside 3D mode). */
void sk_text_draw_3d(sk_handle_t font, const char *text, float x, float y, float z, float size, sk_color_t color);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXT3D_H
