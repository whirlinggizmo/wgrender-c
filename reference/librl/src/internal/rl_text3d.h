#ifndef RL_INTERNAL_TEXT3D_H
#define RL_INTERNAL_TEXT3D_H

#include <stdbool.h>

#include <raylib.h>
#include "internal/rl_render_pass.h"
#include "rl_types.h"

void rl_text3d_init(void);
void rl_text3d_deinit(void);
bool rl_text3d_is_valid(rl_handle_t handle);
bool rl_text3d_has_render_pass(rl_handle_t handle, rl_render_pass_t pass);
void rl_text3d_draw_pass(rl_handle_t handle, rl_render_pass_t pass);
bool rl_text3d_get_position(rl_handle_t handle, float *x, float *y, float *z);

bool rl_text3d_is_pickable_internal(rl_handle_t handle);
bool rl_text3d_scene_pick_broadphase(rl_handle_t handle, Ray ray);
RayCollision rl_text3d_get_ray_collision(rl_handle_t handle, Camera3D camera, Ray ray);

#endif // RL_INTERNAL_TEXT3D_H
