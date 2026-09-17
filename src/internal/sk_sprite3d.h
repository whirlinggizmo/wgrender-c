#ifndef SK_INTERNAL_SPRITE3D_H
#define SK_INTERNAL_SPRITE3D_H

#include <sk_sprite3d.h> /* the public header; "" would find this file */

#include "internal/sk_camera3d.h"
#include "sk_types.h"

/* The unit right and up directions of a quad with `facing` (rotation in radians,
 * used by SK_SPRITE3D_FACING_FREE) for camera `cam`. Shared by sprite3d and text3d. */
void sk_sprite3d_facing_basis(sk_sprite3d_facing_t facing, vec3_t rotation, const sk_camera3d_t *cam,
                              vec3_t *right, vec3_t *up);

#endif // SK_INTERNAL_SPRITE3D_H
