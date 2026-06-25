#ifndef SK_SPRITE3D_H
#define SK_SPRITE3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

typedef enum {
    SK_SPRITE3D_FACING_CAMERA = 0,         /* fully faces the camera        */
    SK_SPRITE3D_FACING_CAMERA_FIXED_Y = 1, /* faces camera, world up locked */
    SK_SPRITE3D_FACING_Y_UP = 2,           /* flat in XZ plane, normal +Y   */
} sk_sprite3d_facing_t;

sk_handle_t sk_sprite3d_create(sk_handle_t texture);
bool sk_sprite3d_set_texture(sk_handle_t handle, sk_handle_t texture);
bool sk_sprite3d_set_transform(sk_handle_t handle,
                               float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z, /* radians */
                               float scale_x, float scale_y, float scale_z);
bool sk_sprite3d_set_size(sk_handle_t handle, float size);
bool sk_sprite3d_set_facing(sk_handle_t handle, int facing);
bool sk_sprite3d_set_tint(sk_handle_t handle, sk_handle_t color);
bool sk_sprite3d_set_visible(sk_handle_t handle, bool visible);
bool sk_sprite3d_is_visible(sk_handle_t handle);
/* When enabled, picking ignores hits on texels whose alpha is below `threshold`
 * (0..1). Requires the sprite's texture to be created with
 * sk_texture_create_pickable(); otherwise picking falls back to the quad. */
bool sk_sprite3d_set_pick_alpha_test(sk_handle_t handle, bool enable, float threshold);
void sk_sprite3d_draw(sk_handle_t handle);
void sk_sprite3d_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_SPRITE3D_H
