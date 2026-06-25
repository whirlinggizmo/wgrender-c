#ifndef RL_INTERNAL_SHAPE_H
#define RL_INTERNAL_SHAPE_H

#include <stdbool.h>

#include <raylib.h>
#include "internal/rl_render_pass.h"
#include "rl_types.h"

void rl_shape_init(void);
void rl_shape_deinit(void);
bool rl_shape_is_valid(rl_handle_t handle);
bool rl_shape_has_render_pass(rl_handle_t handle, rl_render_pass_t pass);
void rl_shape_draw_pass(rl_handle_t handle, rl_render_pass_t pass);

bool rl_shape_is_pickable(rl_handle_t handle);
bool rl_shape_scene_pick_broadphase(rl_handle_t handle, Ray ray);
RayCollision rl_shape_get_ray_collision(rl_handle_t handle, Ray ray);

#endif // RL_INTERNAL_SHAPE_H
