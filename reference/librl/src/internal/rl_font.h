#ifndef RL_INTERNAL_FONT_H
#define RL_INTERNAL_FONT_H

#include <raylib.h>

#include "rl.h"

void rl_font_init(void);
void rl_font_deinit(void);
Font rl_font_get(rl_handle_t handle);
void rl_font_set(rl_handle_t handle, Font font);

#endif // RL_INTERNAL_FONT_H
