#ifndef RL_INTERNAL_TEXTURE_H
#define RL_INTERNAL_TEXTURE_H

#include <stdbool.h>
#include <raylib.h>

#include "rl.h"

void rl_texture_init(void);
void rl_texture_deinit(void);
Texture2D *rl_texture_get_ptr(rl_handle_t handle);
Texture2D *rl_texture_get_placeholder(void);
bool rl_texture_retain(rl_handle_t handle);
void rl_texture_release(rl_handle_t handle);

#endif // RL_INTERNAL_TEXTURE_H
