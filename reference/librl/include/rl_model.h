#ifndef RL_MODEL_H
#define RL_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

rl_handle_t rl_model_get_default_asset(void);
rl_handle_t rl_model_load_asset(const char *filename);
void rl_model_destroy_asset(rl_handle_t asset_handle);
rl_handle_t rl_model_create(rl_handle_t asset_handle);
rl_handle_t rl_model_create_from_file(const char *filename);
bool rl_model_set_asset(rl_handle_t handle, rl_handle_t asset_handle);
bool rl_model_set_transform(rl_handle_t handle,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
bool rl_model_set_visible(rl_handle_t handle, bool visible);
bool rl_model_set_pickable(rl_handle_t handle, bool pickable);
bool rl_model_is_visible(rl_handle_t handle);
bool rl_model_is_pickable(rl_handle_t handle);
void rl_model_draw(rl_handle_t handle);
bool rl_model_is_valid(rl_handle_t handle);
bool rl_model_is_valid_strict(rl_handle_t handle);
int  rl_model_get_animation_count(rl_handle_t handle);
int  rl_model_get_animation_frame_count(rl_handle_t handle, int animation_index);
void rl_model_update_animation(rl_handle_t handle, int animation_index, int frame);
bool rl_model_set_animation(rl_handle_t handle, int animation_index);
bool rl_model_set_animation_speed(rl_handle_t handle, float speed);
bool rl_model_set_animation_loop(rl_handle_t handle, bool should_loop);
bool rl_model_set_tint(rl_handle_t handle, rl_handle_t color_handle);
bool rl_model_animate(rl_handle_t handle, float delta_seconds);
void rl_model_destroy(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_MODEL_H
