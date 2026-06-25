#ifndef RL_TEXT2D_H
#define RL_TEXT2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

rl_handle_t rl_text2d_create(rl_handle_t font, float size);
void rl_text2d_set_font(rl_handle_t handle, rl_handle_t font);
void rl_text2d_set_size(rl_handle_t handle, float size);
void rl_text2d_set_content(rl_handle_t handle, const char *content);
void rl_text2d_set_position(rl_handle_t handle, float x, float y);
void rl_text2d_set_color(rl_handle_t handle, rl_handle_t color);
bool rl_text2d_set_visible(rl_handle_t handle, bool visible);
bool rl_text2d_set_pickable(rl_handle_t handle, bool pickable);
bool rl_text2d_is_visible(rl_handle_t handle);
bool rl_text2d_is_pickable(rl_handle_t handle);
void rl_text2d_draw(rl_handle_t handle);
void rl_text2d_destroy(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_TEXT2D_H
