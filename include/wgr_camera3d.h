#ifndef WGR_CAMERA3D_H
#define WGR_CAMERA3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* Camera3d object. All angles in the libwgrender API are radians.
 *
 * A camera has both a perspective field of view and an orthographic height; the
 * projection type decides which one is used, so switching types loses nothing.
 * Defaults for a new camera: projection as given, position (0, 0, 10) looking at
 * the origin with +y up, fov pi/4 (45 degrees), ortho height 10. */

typedef enum {
    WGR_CAMERA3D_PERSPECTIVE = 0,
    WGR_CAMERA3D_ORTHOGRAPHIC = 1,
} wgr_camera3d_projection_t;

extern const wgr_handle_t WGR_CAMERA3D_DEFAULT;

wgr_handle_t wgr_camera3d_create(wgr_camera3d_projection_t projection);
wgr_handle_t wgr_camera3d_get_default(void);
void        wgr_camera3d_destroy(wgr_handle_t camera);

/* Where the camera is, what it looks at, and which way is up. */
bool wgr_camera3d_set_view(wgr_handle_t camera,
                          float position_x, float position_y, float position_z,
                          float target_x, float target_y, float target_z,
                          float up_x, float up_y, float up_z);

bool wgr_camera3d_set_projection(wgr_handle_t camera, wgr_camera3d_projection_t projection);
wgr_camera3d_projection_t wgr_camera3d_get_projection(wgr_handle_t camera);

/* Perspective: vertical field of view in radians (0 < fov < pi). */
bool  wgr_camera3d_set_fov(wgr_handle_t camera, float fov);
float wgr_camera3d_get_fov(wgr_handle_t camera);

/* Orthographic: full visible height in world units (> 0). Width follows the
 * window's aspect ratio. */
bool  wgr_camera3d_set_ortho_height(wgr_handle_t camera, float height);
float wgr_camera3d_get_ortho_height(wgr_handle_t camera);

bool        wgr_camera3d_set_active(wgr_handle_t camera);
wgr_handle_t wgr_camera3d_get_active(void);

#ifdef __cplusplus
}
#endif

#endif // WGR_CAMERA3D_H
