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
 * a reference owned by the caller; release it with sk_mesh_release. */
sk_handle_t sk_mesh_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        sk_mesh_release(sk_handle_t mesh);

/* The mesh's materials, one slot per glTF material (see sk_material.h). The
 * returned handle is borrowed: it stays valid while the mesh lives, and changing
 * it changes every model using the mesh. */
int         sk_mesh_get_material_count(sk_handle_t mesh);
sk_handle_t sk_mesh_get_material(sk_handle_t mesh, int slot);

/* Model object: a drawable instance of a Mesh (kind MODEL). `mesh` may be 0 to
 * create an empty model now (placed/animated immediately) and attach the mesh
 * later with sk_model_set_mesh — draw/animate no-op until then. */
sk_handle_t sk_model_create(sk_handle_t mesh);
bool sk_model_set_mesh(sk_handle_t handle, sk_handle_t mesh);

bool sk_model_set_transform(sk_handle_t handle,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
bool sk_model_set_tint(sk_handle_t handle, sk_color_t color);
/* Draw this model's material slot `slot` (a mesh material slot, 0..31) with
 * `material` instead of the mesh's; -1 sets every slot. 0 restores the mesh's
 * material. Overrides stay when the mesh changes. The model holds its own
 * reference. */
bool sk_model_set_material(sk_handle_t handle, int slot, sk_handle_t material);
/* The material the model draws slot `slot` with (borrowed), or 0. */
sk_handle_t sk_model_get_material(sk_handle_t handle, int slot);
bool sk_model_set_visible(sk_handle_t handle, bool visible);
bool sk_model_is_visible(sk_handle_t handle);
bool sk_model_set_pickable(sk_handle_t handle, bool pickable); /* default: pickable */
bool sk_model_is_pickable(sk_handle_t handle);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool sk_model_set_enabled(sk_handle_t model, bool enabled);
bool sk_model_is_enabled(sk_handle_t model);
void sk_model_draw(sk_handle_t handle);
void sk_model_destroy(sk_handle_t handle);

/* Skeletal animation (glTF skins + animations, skinned on the GPU). */
int  sk_model_get_animation_count(sk_handle_t handle);
bool sk_model_set_animation(sk_handle_t handle, int animation_index);
bool sk_model_set_animation_speed(sk_handle_t handle, float speed);
bool sk_model_set_animation_loop(sk_handle_t handle, bool loop);
/* Advance the active animation by delta_seconds (times its speed) and recompute
 * joint matrices. */
bool sk_model_animate(sk_handle_t handle, float delta_seconds);
/* Pose the active animation at `seconds` (wrapped when looping, else clamped). glTF
 * animations are timed in seconds, not frames. Set before the mesh arrives, it
 * applies once it does. */
bool  sk_model_set_animation_time(sk_handle_t handle, float seconds);
float sk_model_get_animation_time(sk_handle_t handle);
/* Length of an animation in seconds (0 for no such animation or no mesh yet). */
float sk_model_get_animation_duration(sk_handle_t handle, int animation_index);

/* True once the model has a loaded mesh to draw. */
bool sk_model_is_ready(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_MODEL_H
