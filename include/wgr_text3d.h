#ifndef WGR_TEXT3D_H
#define WGR_TEXT3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "wgr_sprite3d.h"
#include "wgr_text.h" /* wgr_text_align_t */
#include "wgr_types.h"

/* Text3d object (kind TEXT3D): a string placed in the 3D world, drawn with a
 * TrueType font (wgr_font_create), centered on its position.
 *
 * - size is the font size in world units (default 1): one line's height, from the
 *   lowest descender to the highest ascender.
 * - Newlines break lines; wgr_text3d_set_max_width wraps between words, and
 *   wgr_text3d_set_align says where the block sits relative to the position.
 * - Facing uses sprite3d's modes: face the camera (default), face it with world up
 *   kept, lie flat facing up, or FREE (oriented by the rotation, like a sign).
 * - Depth-tested against the scene; a scene sorts it with other transparent parts.
 * - Font 0 (at create, or until set) draws with the default font
 *   (wgr_text_set_default_font, else the built-in font). The text holds a reference
 *   to its font.
 * - Picked by its text's rectangle (wgr_pick_object, wgr_scene_pick). */

wgr_handle_t wgr_text3d_create(wgr_handle_t font);
void        wgr_text3d_destroy(wgr_handle_t text);
bool        wgr_text3d_set_font(wgr_handle_t text, wgr_handle_t font);
bool        wgr_text3d_set_text(wgr_handle_t text, const char *string); /* copied */
bool        wgr_text3d_set_size(wgr_handle_t text, float size);
/* Where the text sits relative to its position: LEFT/CENTER/RIGHT horizontally,
 * TOP/MIDDLE/BOTTOM vertically. Default: centered both ways, so the position is
 * the middle of the block. Wrapped lines line up the same way inside it. */
bool        wgr_text3d_set_align(wgr_handle_t text, wgr_text_align_t horizontal, wgr_text_align_t vertical);
/* Wrap the text to `width` world units, between words (a word wider than that
 * keeps a line to itself); 0 turns wrapping off (the default). Newlines in the
 * text always break a line, wrapped or not. */
bool        wgr_text3d_set_max_width(wgr_handle_t text, float width);
bool        wgr_text3d_set_transform(wgr_handle_t text, float x, float y, float z,
                                    float rotation_x, float rotation_y, float rotation_z); /* radians */
/* One part of the transform, leaving the other as it is; the getters read them back
 * (0, 0, 0 for a handle that isn't one). A 3D text's size stands in for a scale. */
bool        wgr_text3d_set_position(wgr_handle_t text, float x, float y, float z);
bool        wgr_text3d_set_rotation(wgr_handle_t text, float x, float y, float z); /* radians */
vec3_t      wgr_text3d_get_position(wgr_handle_t text);
vec3_t      wgr_text3d_get_rotation(wgr_handle_t text); /* radians */
bool        wgr_text3d_set_facing(wgr_handle_t text, wgr_sprite3d_facing_t facing);
bool        wgr_text3d_set_color(wgr_handle_t text, wgr_color_t color);
bool        wgr_text3d_set_visible(wgr_handle_t text, bool visible);
bool        wgr_text3d_is_visible(wgr_handle_t text);
bool        wgr_text3d_set_pickable(wgr_handle_t text, bool pickable); /* default: pickable */
bool        wgr_text3d_is_pickable(wgr_handle_t text);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_text3d_set_enabled(wgr_handle_t text, bool enabled);
bool wgr_text3d_is_enabled(wgr_handle_t text);
/* World-space width and height of the current text ((0, 0) until the font loads). */
vec2_t      wgr_text3d_get_size(wgr_handle_t text);
/* Draw now (inside 3D mode); scenes draw their members themselves. */
void        wgr_text3d_draw(wgr_handle_t text);

/* Draw text once at a 3D point, facing the camera, line height `size` in world
 * units (inside 3D mode). */
void wgr_text_draw_3d(wgr_handle_t font, const char *text, float x, float y, float z, float size, wgr_color_t color);

#ifdef __cplusplus
}
#endif

#endif // WGR_TEXT3D_H
