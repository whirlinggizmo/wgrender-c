#ifndef SK_CAMERA3D_H
#define SK_CAMERA3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Camera3d object. All angles in the libsk API are radians.
 *
 * A camera has both a perspective field of view and an orthographic height; the
 * projection type decides which one is used, so switching types loses nothing.
 * Defaults for a new camera: projection as given, position (0, 0, 10) looking at
 * the origin with +y up, fov pi/4 (45 degrees), ortho height 10. */

typedef enum {
    SK_CAMERA3D_PERSPECTIVE = 0,
    SK_CAMERA3D_ORTHOGRAPHIC = 1,
} sk_camera3d_projection_t;

extern const sk_handle_t SK_CAMERA3D_DEFAULT;

sk_handle_t sk_camera3d_create(sk_camera3d_projection_t projection);
sk_handle_t sk_camera3d_get_default(void);
void        sk_camera3d_destroy(sk_handle_t camera);

/* Where the camera is, what it looks at, and which way is up. */
bool sk_camera3d_set_view(sk_handle_t camera,
                          float position_x, float position_y, float position_z,
                          float target_x, float target_y, float target_z,
                          float up_x, float up_y, float up_z);

bool sk_camera3d_set_projection(sk_handle_t camera, sk_camera3d_projection_t projection);
sk_camera3d_projection_t sk_camera3d_get_projection(sk_handle_t camera);

/* Perspective: vertical field of view in radians (0 < fov < pi). */
bool  sk_camera3d_set_fov(sk_handle_t camera, float fov);
float sk_camera3d_get_fov(sk_handle_t camera);

/* Orthographic: full visible height in world units (> 0). Width follows the
 * window's aspect ratio. */
bool  sk_camera3d_set_ortho_height(sk_handle_t camera, float height);
float sk_camera3d_get_ortho_height(sk_handle_t camera);

bool        sk_camera3d_set_active(sk_handle_t camera);
sk_handle_t sk_camera3d_get_active(void);

#ifdef __cplusplus
}
#endif

#endif // SK_CAMERA3D_H
