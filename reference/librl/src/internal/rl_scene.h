#ifndef RL_INTERNAL_SCENE_H
#define RL_INTERNAL_SCENE_H

#include "rl_types.h"

void rl_scene_init(void);
void rl_scene_deinit(void);
void rl_scene_on_drawable_destroy(rl_handle_t drawable);

#endif // RL_INTERNAL_SCENE_H
