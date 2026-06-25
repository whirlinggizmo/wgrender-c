#ifndef RL_INTERNAL_PICK_SCENE_H
#define RL_INTERNAL_PICK_SCENE_H

#include <raylib.h>

#include "rl_pick.h"
#include "rl_types.h"

/* Scene pick path: reuse one camera + mouse ray (avoids per-member GetMouseRay). */
rl_pick_result_t rl_pick_model_with_camera_ray(Camera3D camera_data,
                                               Ray ray,
                                               rl_handle_t model);
rl_pick_result_t rl_pick_sprite3d_with_camera_ray(Camera3D camera_data,
                                                 Ray ray,
                                                 rl_handle_t sprite3d);
rl_pick_result_t rl_pick_shape_with_camera_ray(Camera3D camera_data,
                                               Ray ray,
                                               rl_handle_t shape);

rl_pick_result_t rl_pick_text3d_with_camera_ray(Camera3D camera_data,
                                                Ray ray,
                                                rl_handle_t text3d);

#endif // RL_INTERNAL_PICK_SCENE_H
