#ifndef RL_DEBUG_H
#define RL_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

void rl_debug_enable_fps(int x, int y, int font_size, rl_handle_t font);
void rl_debug_disable_fps(void);

#ifdef __cplusplus
}
#endif

#endif // RL_DEBUG_H
