#ifndef WGR_SPRITE2D_H
#define WGR_SPRITE2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "wgr_types.h"

/* Sprite2d object: a textured quad in screen space that references a Texture
 * resource. See docs/PLAN-sprite2d.md.
 *
 * - Coordinates are logical pixels: top-left origin, y down. On high-DPI displays
 *   one logical pixel spans several framebuffer pixels.
 * - Angles are radians; positive rotates clockwise on screen (y points down).
 * - Draw it in a scene (wgr_scene_add): a scene draws all 3D first, then its 2D
 *   members by layer and insertion order, and wgr_scene_pick tests 2D members first,
 *   topmost first. Or draw it directly with wgr_sprite2d_draw, in call order.
 * - A sprite with no texture (or one still loading) isn't drawn or picked. */

wgr_handle_t wgr_sprite2d_create(wgr_handle_t texture);  /* texture may be 0, set later */
void        wgr_sprite2d_destroy(wgr_handle_t sprite);

bool wgr_sprite2d_set_texture(wgr_handle_t sprite, wgr_handle_t texture);

/* Region of the texture to show, in texture pixels (sprite sheets, atlases).
 * Default: the whole texture. width or height <= 0 resets to the whole texture. */
bool wgr_sprite2d_set_source(wgr_handle_t sprite, float x, float y, float width, float height);

bool wgr_sprite2d_set_position(wgr_handle_t sprite, float x, float y);  /* where the pivot goes */
bool wgr_sprite2d_set_rotation(wgr_handle_t sprite, float angle);       /* radians, around the pivot */
bool wgr_sprite2d_set_scale(wgr_handle_t sprite, float x, float y);     /* multiplies size; negative flips */

/* On-screen size in logical pixels before scale. width or height <= 0 means the
 * source region's size (the default). */
bool wgr_sprite2d_set_size(wgr_handle_t sprite, float width, float height);

/* Point that position refers to and rotation turns around, as a fraction of the
 * sprite: (0, 0) top-left, (1, 1) bottom-right. Default (0.5, 0.5), the center. */
bool wgr_sprite2d_set_pivot(wgr_handle_t sprite, float x, float y);

/* Nine-slice: borders in source pixels that keep their size when the sprite is
 * drawn at another size (panels, buttons, frames). The corners stay as they are,
 * the edges stretch along one axis and the middle along both; borders of 0 on an
 * axis leave it unsliced, all 0 turns nine-slice off (the default). Set the
 * on-screen size with wgr_sprite2d_set_size. Picks hit the whole rectangle: the
 * alpha test is skipped while a sprite is sliced. */
bool wgr_sprite2d_set_nine_slice(wgr_handle_t sprite, float left, float top, float right, float bottom);

bool wgr_sprite2d_set_tint(wgr_handle_t sprite, wgr_color_t color);    /* default: WGR_COLOR_WHITE */
bool wgr_sprite2d_set_visible(wgr_handle_t sprite, bool visible);
bool wgr_sprite2d_is_visible(wgr_handle_t sprite);
bool wgr_sprite2d_set_pickable(wgr_handle_t sprite, bool pickable);   /* default: pickable */
bool wgr_sprite2d_is_pickable(wgr_handle_t sprite);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_sprite2d_set_enabled(wgr_handle_t sprite, bool enabled);
bool wgr_sprite2d_is_enabled(wgr_handle_t sprite);

/* How the sprite uses its texture's alpha (default WGR_ALPHA_BLEND): blended, added
 * (glows), opaque (alpha ignored) or masked (texels below `cutoff`, 0..1, cut out). 2D
 * sprites always draw in order; the mode only changes how they're blended. */
bool wgr_sprite2d_set_alpha_mode(wgr_handle_t sprite, wgr_alpha_mode_t mode, float cutoff);
wgr_alpha_mode_t wgr_sprite2d_get_alpha_mode(wgr_handle_t sprite);
/* As wgr_sprite3d_set_material: a custom material's shader draws the sprite (in screen
 * pixels: wgr_world_pos is the pixel, and the scene's lights don't reach 2D). A
 * nine-slice sprite's wgr_uv1 spans each slice. 0: libwgrender's sprite shader. */
bool wgr_sprite2d_set_material(wgr_handle_t sprite, wgr_handle_t material);
wgr_handle_t wgr_sprite2d_get_material(wgr_handle_t sprite); /* borrowed; 0 = none */

/* When enabled, picks on texels with alpha below `threshold` (0..1) pass through. */
bool wgr_sprite2d_set_pick_alpha_test(wgr_handle_t sprite, bool enable, float threshold);

void wgr_sprite2d_draw(wgr_handle_t sprite);  /* immediate, outside a scene */

#ifdef __cplusplus
}
#endif

#endif // WGR_SPRITE2D_H
