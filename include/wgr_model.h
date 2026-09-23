#ifndef WGR_MODEL_H
#define WGR_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* glTF/glb models (cgltf).
 *
 * Two layers, mirroring the rest of libwgrender (see docs/ARCHITECTURE.md):
 *   - A *Mesh* resource (kind MESH) owns the loaded, shared data: CPU geometry
 *     (incl. retained pick data), GPU buffers, skeleton and animation clips.
 *     Meshes are deduplicated by source path and reference counted.
 *   - A *Model* object (kind MODEL) is a lightweight scene drawable that
 *     references a Mesh and carries its own transform, tint, visibility and
 *     animation playback state.
 *
 * Load a Mesh from a path (wgr_mesh_create), then spawn one or more Models from
 * it (wgr_model_create). One Mesh can back many Models. */

/* Mesh resource. Loads (or returns a shared, deduped) Mesh from a path and adds
 * a reference owned by the caller; release it with wgr_mesh_release. */
wgr_handle_t wgr_mesh_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        wgr_mesh_release(wgr_handle_t mesh);

/* Generated meshes: shapes made in code, with normals, texture coordinates (both
 * sets) and tangents, so any material lights them, normal maps and custom shaders
 * included. Centered on the origin, y up, in meters. Like a loaded mesh they're
 * resources: deduplicated (the same parameters return the same mesh, with one more
 * reference) and never changed after they're made; to size one model differently,
 * scale it (wgr_model_set_transform) or make another mesh. One material slot, white,
 * not metallic, roughness 0.5: replace it with wgr_model_set_material. 0 (logged) for
 * sizes <= 0; counts are clamped to their ranges.
 *
 *   plane     flat in XZ, facing +Y; subdivisions 0..256 cells more each way.
 *             Texture coordinates span it once (tile with the material's
 *             <texture>_scale)
 *   cube      each face its own vertices (sharp edges), textured 0..1 per face
 *   sphere    rings 2..256 from pole to pole, segments 3..512 around; u around, v
 *             pole to pole
 *   cylinder  capped; segments 3..512 around
 *   cone      tip up, capped base
 *   capsule   height from end to end (at least 2 x radius; less: a sphere); rings
 *             across both round ends
 *   torus     around y; radius to the middle of the tube, thickness the tube's
 *             radius; rings around the ring, segments around the tube (3..512) */
wgr_handle_t wgr_mesh_create_plane(float width, float length, int subdivisions);
wgr_handle_t wgr_mesh_create_cube(float width, float height, float length);
wgr_handle_t wgr_mesh_create_sphere(float radius, int rings, int segments);
wgr_handle_t wgr_mesh_create_cylinder(float radius, float height, int segments);
wgr_handle_t wgr_mesh_create_cone(float radius, float height, int segments);
wgr_handle_t wgr_mesh_create_capsule(float radius, float height, int rings, int segments);
wgr_handle_t wgr_mesh_create_torus(float radius, float thickness, int rings, int segments);

/* The mesh's materials, one slot per glTF material (see wgr_material.h). The
 * returned handle is borrowed: it stays valid while the mesh lives, and changing
 * it changes every model using the mesh. */
int         wgr_mesh_get_material_count(wgr_handle_t mesh);
wgr_handle_t wgr_mesh_get_material(wgr_handle_t mesh, int slot);

/* Model object: a drawable instance of a Mesh (kind MODEL). `mesh` may be 0 to
 * create an empty model now (placed/animated immediately) and attach the mesh
 * later with wgr_model_set_mesh — draw/animate no-op until then. */
wgr_handle_t wgr_model_create(wgr_handle_t mesh);
bool wgr_model_set_mesh(wgr_handle_t handle, wgr_handle_t mesh);

bool wgr_model_set_transform(wgr_handle_t handle,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z, /* radians */
                            float scale_x, float scale_y, float scale_z);
/* One part of the transform, leaving the others as they are; the getters read them back
 * (0, 0, 0 for a handle that isn't one). */
bool   wgr_model_set_position(wgr_handle_t handle, float x, float y, float z);
bool   wgr_model_set_rotation(wgr_handle_t handle, float x, float y, float z); /* radians */
bool   wgr_model_set_scale(wgr_handle_t handle, float x, float y, float z);
vec3_t wgr_model_get_position(wgr_handle_t handle);
vec3_t wgr_model_get_rotation(wgr_handle_t handle); /* radians */
vec3_t wgr_model_get_scale(wgr_handle_t handle);
bool wgr_model_set_tint(wgr_handle_t handle, wgr_color_t color);
/* Draw this model's material slot `slot` (a mesh material slot, 0..31) with
 * `material` instead of the mesh's; -1 sets every slot. 0 restores the mesh's
 * material. Overrides stay when the mesh changes. The model holds its own
 * reference. */
bool wgr_model_set_material(wgr_handle_t handle, int slot, wgr_handle_t material);
/* The material the model draws slot `slot` with (borrowed), or 0. */
wgr_handle_t wgr_model_get_material(wgr_handle_t handle, int slot);
bool wgr_model_set_visible(wgr_handle_t handle, bool visible);
bool wgr_model_is_visible(wgr_handle_t handle);
bool wgr_model_set_pickable(wgr_handle_t handle, bool pickable); /* default: pickable */
bool wgr_model_is_pickable(wgr_handle_t handle);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_model_set_enabled(wgr_handle_t model, bool enabled);
bool wgr_model_is_enabled(wgr_handle_t model);

/* Shadows (docs/PLAN-shadows.md), when a light in the scene casts them. Casting
 * (default): the model is drawn into the light's depth map, so it shadows what's
 * behind it — turn it off for a skybox or a glow. Receiving (default): shadows darken
 * it — turn it off for something that shouldn't be shaded by them at all. */
bool wgr_model_set_casts_shadow(wgr_handle_t model, bool casts);
bool wgr_model_casts_shadow(wgr_handle_t model);
bool wgr_model_set_receives_shadow(wgr_handle_t model, bool receives);
bool wgr_model_receives_shadow(wgr_handle_t model);
void wgr_model_draw(wgr_handle_t handle);
void wgr_model_destroy(wgr_handle_t handle);

/* Skeletal animation (glTF skins + animations, skinned on the GPU). */
int  wgr_model_get_animation_count(wgr_handle_t handle);
bool wgr_model_set_animation(wgr_handle_t handle, int animation_index);
bool wgr_model_set_animation_speed(wgr_handle_t handle, float speed);
bool wgr_model_set_animation_loop(wgr_handle_t handle, bool loop);
/* Advance the active animation by delta_seconds (times its speed) and recompute
 * joint matrices. */
bool wgr_model_animate(wgr_handle_t handle, float delta_seconds);
/* Pose the active animation at `seconds` (wrapped when looping, else clamped). glTF
 * animations are timed in seconds, not frames. Set before the mesh arrives, it
 * applies once it does. */
bool  wgr_model_set_animation_time(wgr_handle_t handle, float seconds);
float wgr_model_get_animation_time(wgr_handle_t handle);
/* Length of an animation in seconds (0 for no such animation or no mesh yet). */
float wgr_model_get_animation_duration(wgr_handle_t handle, int animation_index);

/* True once the model has a loaded mesh to draw. */
bool wgr_model_is_ready(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_MODEL_H
