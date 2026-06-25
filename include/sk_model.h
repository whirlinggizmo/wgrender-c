#ifndef SK_MODEL_H
#define SK_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* glTF/glb models (cgltf).
 *
 * Two layers, mirroring the rest of libsk (see docs/ARCHITECTURE.md):
 *   - A *Mesh* resource (kind MESH) owns the loaded, shared data: CPU geometry
 *     (incl. retained pick data), GPU buffers, skeleton and animation clips.
 *     Meshes are deduplicated by source path and reference counted.
 *   - A *Model* object (kind MODEL) is a lightweight scene drawable that
 *     references a Mesh and carries its own transform, tint, visibility and
 *     animation playback state.
 *
 * Load a Mesh from a path (sk_mesh_create), then spawn one or more Models from
 * it (sk_model_create). One Mesh can back many Models. */

/* Mesh resource. Loads (or returns a shared, deduped) Mesh from a path and adds
 * a reference owned by the caller; release it with sk_mesh_destroy. */
sk_handle_t sk_mesh_create(const char *path);
void        sk_mesh_destroy(sk_handle_t mesh);

/* Model object: a drawable instance of a Mesh (kind MODEL). */
sk_handle_t sk_model_create(sk_handle_t mesh);

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
