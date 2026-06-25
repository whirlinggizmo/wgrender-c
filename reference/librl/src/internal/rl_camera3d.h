#ifndef RL_INTERNAL_CAMERA3D_H
#define RL_INTERNAL_CAMERA3D_H

#include <stdbool.h>
#include <raylib.h>

#include "rl_types.h"

void rl_camera3d_init(void);
void rl_camera3d_deinit(void);
bool rl_camera3d_get_active_camera(Camera3D *camera);
bool rl_camera3d_get_camera(rl_handle_t handle, Camera3D *camera);
bool rl_camera3d_ensure_active_camera(void);
bool rl_camera3d_get_lighting_shader(Shader *out);
bool rl_camera3d_get_skinned_lighting_shader(Shader *out);

#endif // RL_INTERNAL_CAMERA3D_H
