#ifndef RL_INTERNAL_COLOR_H
#define RL_INTERNAL_COLOR_H

#include <raylib.h>

#include "rl.h"

void rl_color_init(void);
void rl_color_deinit(void);
Color rl_color_get(rl_handle_t handle);
void rl_color_set(rl_handle_t handle, int r, int g, int b, int a);

#endif // RL_INTERNAL_COLOR_H
