#ifndef RL_TEXT3D_H
#define RL_TEXT3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

rl_handle_t rl_text3d_create(rl_handle_t font, float size);
void rl_text3d_set_font(rl_handle_t handle, rl_handle_t font);
void rl_text3d_set_size(rl_handle_t handle, float size);
void rl_text3d_set_content(rl_handle_t handle, const char *content);
bool rl_text3d_set_transform(rl_handle_t handle,
                             float x, float y, float z,
                             float rx, float ry, float rz);
void rl_text3d_set_color(rl_handle_t handle, rl_handle_t color);
bool rl_text3d_set_facing(rl_handle_t handle, int facing);
bool rl_text3d_set_visible(rl_handle_t handle, bool visible);
bool rl_text3d_set_pickable(rl_handle_t handle, bool pickable);
bool rl_text3d_is_visible(rl_handle_t handle);
bool rl_text3d_is_pickable(rl_handle_t handle);
vec2_t rl_text3d_get_bounds(rl_handle_t handle);
bool rl_text3d_get_bounds_to_scratch(rl_handle_t handle);
void rl_text3d_draw(rl_handle_t handle);
void rl_text3d_draw_text(const char *text, rl_handle_t font,
                         float x, float y, float z,
                         float size, rl_handle_t color);
void rl_text3d_destroy(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_TEXT3D_H
