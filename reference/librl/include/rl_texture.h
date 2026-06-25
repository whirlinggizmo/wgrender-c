#ifndef RL_TEXTURE_H
#define RL_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

rl_handle_t rl_texture_get_default(void);
rl_handle_t rl_texture_create(const char *filename);
void rl_texture_destroy(rl_handle_t handle);
void rl_texture_draw_ex(rl_handle_t texture, float x, float y, float scale,
                        float rotation, rl_handle_t tint);
void rl_texture_draw_ground(rl_handle_t texture,
                            float position_x, float position_y, float position_z,
                            float width, float length, rl_handle_t tint);

#ifdef __cplusplus
}
#endif

#endif // RL_TEXTURE_H
