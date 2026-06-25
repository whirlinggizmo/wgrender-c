#ifndef RL_INTERNAL_SPRITE3D_H
#define RL_INTERNAL_SPRITE3D_H

#include <stdbool.h>
#include <raylib.h>

#include "internal/rl_render_pass.h"
#include "rl_types.h"

void rl_sprite3d_init(void);
void rl_sprite3d_deinit(void);
bool rl_sprite3d_has_render_pass(rl_handle_t handle, rl_render_pass_t pass);
void rl_sprite3d_draw_pass(rl_handle_t handle, rl_render_pass_t pass);
bool rl_sprite3d_get_transform(rl_handle_t handle,
                               float *position_x, float *position_y, float *position_z,
                               float *rotation_x, float *rotation_y, float *rotation_z,
                               float *scale_x, float *scale_y, float *scale_z);
float rl_sprite3d_get_size_internal(rl_handle_t handle);
bool rl_sprite3d_get_ray_collision(rl_handle_t handle,
                                   Camera3D camera,
                                   Ray ray,
                                   float position_x,
                                   float position_y,
                                   float position_z,
                                   float rotation_x,
                                   float rotation_y,
                                   float rotation_z,
                                   float scale_x,
                                   float scale_y,
                                   float scale_z,
                                   float size,
                                   RayCollision *collision);
bool rl_sprite3d_get_ray_collision_ex(rl_handle_t handle,
                                      Camera3D camera,
                                      Ray ray,
                                      float position_x,
                                      float position_y,
                                      float position_z,
                                      float rotation_x,
                                      float rotation_y,
                                      float rotation_z,
                                      float scale_x,
                                      float scale_y,
                                      float scale_z,
                                      float size,
                                      RayCollision *collision,
                                      bool *broadphase_tested,
                                   bool *broadphase_rejected,
                                   bool *narrowphase_ran);

bool rl_sprite3d_scene_pick_broadphase(rl_handle_t handle, Ray ray);

#endif // RL_INTERNAL_SPRITE3D_H
