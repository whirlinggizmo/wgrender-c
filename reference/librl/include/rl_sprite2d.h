#ifndef RL_SPRITE2D_H
#define RL_SPRITE2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

rl_handle_t rl_sprite2d_get_default_texture(void);
rl_handle_t rl_sprite2d_create(rl_handle_t texture);
rl_handle_t rl_sprite2d_create_from_file(const char *filename);
bool rl_sprite2d_set_texture(rl_handle_t handle, rl_handle_t texture);
bool rl_sprite2d_set_transform(rl_handle_t handle,
                               float x, float y,
                               float scale, float rotation);
bool rl_sprite2d_set_tint(rl_handle_t handle, rl_handle_t color_handle);
bool rl_sprite2d_set_visible(rl_handle_t handle, bool visible);
bool rl_sprite2d_set_pickable(rl_handle_t handle, bool pickable);
bool rl_sprite2d_is_visible(rl_handle_t handle);
bool rl_sprite2d_is_pickable(rl_handle_t handle);
void rl_sprite2d_draw(rl_handle_t handle);
void rl_sprite2d_destroy(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_SPRITE2D_H
