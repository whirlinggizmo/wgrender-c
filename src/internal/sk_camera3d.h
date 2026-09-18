#ifndef SK_INTERNAL_CAMERA3D_H
#define SK_INTERNAL_CAMERA3D_H

#include <stdbool.h>

#include "internal/sk_math.h"
#include "sk_types.h"
/* the public header has the same file name; <> skips this directory */
#include <sk_camera3d.h>

/* Clip planes shared by every 3D path. Orthographic uses a symmetric range so
 * content behind the camera position still shows, as sokol_gl did before. */
#define SK_CAMERA3D_PERSPECTIVE_NEAR 0.01f
#define SK_CAMERA3D_PERSPECTIVE_FAR 1000.0f
#define SK_CAMERA3D_ORTHOGRAPHIC_NEAR -1000.0f
#define SK_CAMERA3D_ORTHOGRAPHIC_FAR 1000.0f

typedef struct {
    vec3_t position;
    vec3_t target;
    vec3_t up;
    float fov;          /* perspective: vertical field of view, radians */
    float ortho_height; /* orthographic: full visible height, world units */
    sk_camera3d_projection_t projection;
} sk_camera3d_t;

void sk_camera3d_init(void);
void sk_camera3d_deinit(void);

/* Ensure there is an active camera (falls back to the built-in default). */
bool sk_camera3d_ensure_active(void);

/* Fetch the parameters of the currently active camera. */
bool sk_camera3d_get_active_data(sk_camera3d_t *out);
/* Changes whenever a camera, or which one is active, changes: a cheap check for
 * whether camera-derived state (sprite batches) is still current. */
unsigned sk_camera3d_revision(void);
/* Fetch a camera's parameters (0: the active camera) without changing which is active. */
bool sk_camera3d_get_data(sk_handle_t camera, sk_camera3d_t *out);

/* The single source of truth for camera matrices: sokol_gl 3D mode, models and
 * picking all use these, so they can't disagree about projection or view.
 * Perspective uses fov (radians); orthographic uses ortho_height (world units). */
sk_mat4_t sk_camera3d_projection(const sk_camera3d_t *cam, float aspect);
sk_mat4_t sk_camera3d_view(const sk_camera3d_t *cam);

#endif // SK_INTERNAL_CAMERA3D_H
