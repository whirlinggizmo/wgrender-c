#ifndef RL_CAMERA3D_H
#define RL_CAMERA3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

extern const rl_handle_t RL_CAMERA3D_DEFAULT;

rl_handle_t rl_camera3d_create(float position_x, float position_y, float position_z,
                               float target_x, float target_y, float target_z,
                               float up_x, float up_y, float up_z,
                               float fovy, int projection);
rl_handle_t rl_camera3d_get_default(void);
bool rl_camera3d_set(rl_handle_t handle,
                     float position_x, float position_y, float position_z,
                     float target_x, float target_y, float target_z,
                     float up_x, float up_y, float up_z,
                     float fovy, int projection);
bool rl_camera3d_set_active(rl_handle_t handle);
rl_handle_t rl_camera3d_get_active(void);
void rl_camera3d_destroy(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_CAMERA3D_H
