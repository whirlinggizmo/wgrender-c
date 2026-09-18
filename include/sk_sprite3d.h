#ifndef SK_SPRITE3D_H
#define SK_SPRITE3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Sprite3d object (kind SPRITE3D): a textured billboard in 3D that references
 * a shared Texture resource. See docs/ARCHITECTURE.md. */

typedef enum {
    SK_SPRITE3D_FACING_CAMERA = 0,         /* fully faces the camera        */
    SK_SPRITE3D_FACING_CAMERA_FIXED_Y = 1, /* faces camera, world up locked */
    SK_SPRITE3D_FACING_Y_UP = 2,           /* flat in XZ plane, normal +Y   */
    SK_SPRITE3D_FACING_FREE = 3,           /* its own rotation: the local XY plane, facing +Z */
} sk_sprite3d_facing_t;

sk_handle_t sk_sprite3d_create(sk_handle_t texture);
bool sk_sprite3d_set_texture(sk_handle_t handle, sk_handle_t texture);
bool sk_sprite3d_set_transform(sk_handle_t handle,
                               float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z, /* radians */
                               float scale_x, float scale_y, float scale_z);
/* World size of the quad before scale: set_size is the square shorthand for
 * set_extent(size, size). Default 1x1; a width or height <= 0 is refused. */
bool sk_sprite3d_set_size(sk_handle_t handle, float size);
bool sk_sprite3d_set_extent(sk_handle_t handle, float width, float height);

/* Region of the texture to show, in texture pixels (sprite sheets, atlases).
 * Default: the whole texture; width or height <= 0 resets to that. */
bool sk_sprite3d_set_source(sk_handle_t handle, float x, float y, float width, float height);

/* The point of the quad that sits on the sprite's position and that it turns
 * around, as a fraction of the quad: (0, 0) its top-left, (1, 1) its
 * bottom-right, (0.5, 0.5) its center (the default). y runs down the texture, so
 * (0.5, 1) puts the position at the bottom edge — what a sprite standing on the
 * ground wants. */
bool sk_sprite3d_set_pivot(sk_handle_t handle, float x, float y);

bool sk_sprite3d_set_facing(sk_handle_t handle, sk_sprite3d_facing_t facing);
vec3_t sk_sprite3d_get_position(sk_handle_t handle);
vec3_t sk_sprite3d_get_rotation(sk_handle_t handle); /* radians */
vec3_t sk_sprite3d_get_scale(sk_handle_t handle);
bool sk_sprite3d_set_tint(sk_handle_t handle, sk_color_t color);
bool sk_sprite3d_set_visible(sk_handle_t handle, bool visible);
bool sk_sprite3d_is_visible(sk_handle_t handle);
bool sk_sprite3d_set_pickable(sk_handle_t handle, bool pickable); /* default: pickable */
bool sk_sprite3d_is_pickable(sk_handle_t handle);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_sprite3d_set_enabled(sk_handle_t sprite, bool enabled);
bool sk_sprite3d_is_enabled(sk_handle_t sprite);
/* When enabled, picking ignores hits on texels whose alpha is below `threshold`
 * (0..1). Builds a CPU alpha mask from the texture's source path on demand. */
bool sk_sprite3d_set_pick_alpha_test(sk_handle_t handle, bool enable, float threshold);
void sk_sprite3d_draw(sk_handle_t handle);
void sk_sprite3d_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_SPRITE3D_H
