#ifndef SK_CAMERA3D_H
#define SK_CAMERA3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* projection modes */
#define SK_CAMERA3D_PERSPECTIVE 0
#define SK_CAMERA3D_ORTHOGRAPHIC 1

extern const sk_handle_t SK_CAMERA3D_DEFAULT;

sk_handle_t sk_camera3d_create(float position_x, float position_y, float position_z,
                               float target_x, float target_y, float target_z,
                               float up_x, float up_y, float up_z,
                               float fovy, int projection);
sk_handle_t sk_camera3d_get_default(void);
bool sk_camera3d_set(sk_handle_t handle,
                     float position_x, float position_y, float position_z,
                     float target_x, float target_y, float target_z,
                     float up_x, float up_y, float up_z,
                     float fovy, int projection);
bool sk_camera3d_set_active(sk_handle_t handle);
sk_handle_t sk_camera3d_get_active(void);
void sk_camera3d_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_CAMERA3D_H
