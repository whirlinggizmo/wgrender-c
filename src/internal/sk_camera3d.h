#ifndef SK_INTERNAL_CAMERA3D_H
#define SK_INTERNAL_CAMERA3D_H

#include <stdbool.h>

#include "sk_types.h"

typedef struct {
    vec3_t position;
    vec3_t target;
    vec3_t up;
    float fovy;     /* degrees (perspective) or world-units height (ortho) */
    int projection; /* SK_CAMERA3D_PERSPECTIVE / SK_CAMERA3D_ORTHOGRAPHIC */
} sk_camera3d_data_t;

void sk_camera3d_init(void);
void sk_camera3d_deinit(void);

/* Ensure there is an active camera (falls back to the built-in default). */
bool sk_camera3d_ensure_active(void);

/* Fetch the parameters of the currently active camera. */
bool sk_camera3d_get_active_data(sk_camera3d_data_t *out);

#endif // SK_INTERNAL_CAMERA3D_H
