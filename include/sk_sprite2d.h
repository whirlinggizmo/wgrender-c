#ifndef SK_SPRITE2D_H
#define SK_SPRITE2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_types.h"

/* Sprite2d object: a textured quad in screen space that references a Texture
 * resource. See docs/PLAN-sprite2d.md.
 *
 * - Coordinates are logical pixels: top-left origin, y down. On high-DPI displays
 *   one logical pixel spans several framebuffer pixels.
 * - Angles are radians; positive rotates clockwise on screen (y points down).
 * - Draw it in a scene (sk_scene_add): a scene draws all 3D first, then its 2D
 *   members by layer and insertion order, and sk_scene_pick tests 2D members first,
 *   topmost first. Or draw it directly with sk_sprite2d_draw, in call order.
 * - A sprite with no texture (or one still loading) isn't drawn or picked. */

sk_handle_t sk_sprite2d_create(sk_handle_t texture);  /* texture may be 0, set later */
void        sk_sprite2d_destroy(sk_handle_t sprite);

bool sk_sprite2d_set_texture(sk_handle_t sprite, sk_handle_t texture);

/* Region of the texture to show, in texture pixels (sprite sheets, atlases).
 * Default: the whole texture. width or height <= 0 resets to the whole texture. */
bool sk_sprite2d_set_source(sk_handle_t sprite, float x, float y, float width, float height);

bool sk_sprite2d_set_position(sk_handle_t sprite, float x, float y);  /* where the pivot goes */
bool sk_sprite2d_set_rotation(sk_handle_t sprite, float angle);       /* radians, around the pivot */
bool sk_sprite2d_set_scale(sk_handle_t sprite, float x, float y);     /* multiplies size; negative flips */

/* On-screen size in logical pixels before scale. width or height <= 0 means the
 * source region's size (the default). */
bool sk_sprite2d_set_size(sk_handle_t sprite, float width, float height);

/* Point that position refers to and rotation turns around, as a fraction of the
 * sprite: (0, 0) top-left, (1, 1) bottom-right. Default (0.5, 0.5), the center. */
bool sk_sprite2d_set_pivot(sk_handle_t sprite, float x, float y);

bool sk_sprite2d_set_tint(sk_handle_t sprite, sk_handle_t color);    /* default: white (0) */
bool sk_sprite2d_set_visible(sk_handle_t sprite, bool visible);
bool sk_sprite2d_is_visible(sk_handle_t sprite);
bool sk_sprite2d_set_pickable(sk_handle_t sprite, bool pickable);   /* default: pickable */
bool sk_sprite2d_is_pickable(sk_handle_t sprite);

/* When enabled, picks on texels with alpha below `threshold` (0..1) pass through. */
bool sk_sprite2d_set_pick_alpha_test(sk_handle_t sprite, bool enable, float threshold);

void sk_sprite2d_draw(sk_handle_t sprite);  /* immediate, outside a scene */

#ifdef __cplusplus
}
#endif

#endif // SK_SPRITE2D_H
