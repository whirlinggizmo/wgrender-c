#ifndef RL_SCENE_H
#define RL_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_pick.h"
#include "rl_types.h"

rl_handle_t rl_scene_create(void);
void        rl_scene_destroy(rl_handle_t scene);

bool rl_scene_add(rl_handle_t scene, rl_handle_t drawable, int layer);
bool rl_scene_set_layer(rl_handle_t scene, rl_handle_t drawable, int layer);
bool rl_scene_remove(rl_handle_t scene, rl_handle_t drawable);
void rl_scene_clear(rl_handle_t scene);

void rl_scene_set_active_camera(rl_handle_t scene, rl_handle_t camera);

void rl_scene_draw(rl_handle_t scene);

rl_pick_result_t rl_scene_pick(rl_handle_t scene,
                               rl_handle_t camera,
                               float mouse_x,
                               float mouse_y);

bool rl_scene_pick_to_scratch(rl_handle_t scene,
                              rl_handle_t camera,
                              float mouse_x,
                              float mouse_y);

#ifdef __cplusplus
}
#endif

#endif // RL_SCENE_H
