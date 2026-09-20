#ifndef WGR_INTERNAL_SPRITE3D_H
#define WGR_INTERNAL_SPRITE3D_H

#include "wgr_sprite3d.h"

#include "internal/wgr_camera3d_internal.h"
#include "wgr_types.h"

/* The unit right and up directions of a quad with `facing` (rotation in radians,
 * used by WGR_SPRITE3D_FACING_FREE) for camera `cam`. Shared by sprite3d and text3d. */
void wgr_sprite3d_facing_basis(wgr_sprite3d_facing_t facing, vec3_t rotation, const wgr_camera3d_t *cam,
                              vec3_t *right, vec3_t *up);

#endif // WGR_INTERNAL_SPRITE3D_H
