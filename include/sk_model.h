#ifndef SK_MODEL_H
#define SK_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Static glTF/glb models (cgltf). Meshes are uploaded to GPU buffers and drawn
 * with a dedicated lit/textured pipeline. Skeletal animation is not handled
 * yet — see the roadmap. Models are scene drawables (kind MODEL). */

sk_handle_t sk_model_create(const char *path);
sk_handle_t sk_model_create_from_memory(const unsigned char *data, int size, const char *hint);
bool sk_model_set_transform(sk_handle_t handle,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
bool sk_model_set_tint(sk_handle_t handle, sk_handle_t color);
bool sk_model_set_visible(sk_handle_t handle, bool visible);
bool sk_model_is_visible(sk_handle_t handle);
void sk_model_draw(sk_handle_t handle);
void sk_model_destroy(sk_handle_t handle);

/* Skeletal animation (glTF skins + animations, skinned on the GPU). */
int  sk_model_get_animation_count(sk_handle_t handle);
bool sk_model_set_animation(sk_handle_t handle, int animation_index);
bool sk_model_set_animation_speed(sk_handle_t handle, float speed);
bool sk_model_set_animation_loop(sk_handle_t handle, bool loop);
/* Advance the active animation by delta_seconds and recompute joint matrices. */
bool sk_model_animate(sk_handle_t handle, float delta_seconds);

#ifdef __cplusplus
}
#endif

#endif // SK_MODEL_H
