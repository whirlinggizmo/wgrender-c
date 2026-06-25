#ifndef RL_RENDER_H
#define RL_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

void rl_render_begin(void);
void rl_render_end(void);
void rl_render_clear_background(rl_handle_t color);
void rl_render_begin_mode_2d(rl_handle_t camera);
void rl_render_end_mode_2d(void);
void rl_render_begin_mode_3d(void);
void rl_render_end_mode_3d(void);

#ifdef __cplusplus
}
#endif

#endif // RL_RENDER_H
