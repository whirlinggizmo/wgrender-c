#ifndef WGRI_INTERNAL_MODEL_H
#define WGRI_INTERNAL_MODEL_H

#include <stdint.h>

#include "internal/wgr_asset_internal.h"
#include "internal/wgr_math_internal.h"
#include "wgr_texture.h"

void wgri_model_init(void);
void wgri_model_deinit(void);

/* Draw queued model items [first, first + count) into the current render pass,
 * in queue order. Called by wgr_render_end() while replaying the frame's command
 * list (models use a custom sg pipeline, so they must run inside the pass). */
void wgri_model_draw_items(int first, int count);

/* Clear the model draw queue after the frame has been drawn. */
void wgri_model_flush(void); /* the frame's joint matrices into the joint texture */
void wgri_model_end_frame(void);

/* The files a glTF file needs (its buffers, and the images its textures use: a
 * compressed one this GPU can use instead of the texture's own), for wgr_asset; exposed
 * for tests. */
void wgri_model_list_gltf_dependencies(const unsigned char *data, int size, wgri_asset_add_dependency_fn add,
                                     void *context);

/* The model's joint matrices as the skinned shader reads them (16 floats each, in
 * order), and how many; 0 when it has no skin or no mesh yet. The pointer is valid
 * until the model is posed again. Exposed for tests. */
int wgri_model_get_joint_matrices(wgr_handle_t model, const float **matrices);

/* Shadows (src/wgr_shadow.c). Whether any model queued for this lighting environment
 * casts, and drawing those casters into the open depth pass from the light's point of
 * view (`light_view_proj`: world -> the light's clip space). */
/* Placements and primitives queued for the frame so far. The queue grows as a scene
 * needs it and stops at a ceiling; past that a frame's remaining models aren't drawn.
 * For tests. */
void wgri_model_queue_counts(int *placements, int *primitives, int *placement_ceiling);

/* How many draw calls the frame's items came to: equal ones submitted inside an
 * unordered region go up as one (docs/PLAN-instancing.md). Read after the passes. */
int wgri_model_draw_call_count(void);
int wgri_model_shadow_draw_call_count(void);

/* A region of a frame where the models submitted may be drawn in any order, so equal
 * ones can be batched (wgr_scene declares it; opaque parts only). */
void wgri_model_begin_unordered(void);
void wgri_model_end_unordered(void);

/* The frame's per-placement records, after a flush: 32 floats each, laid out as
 * docs/PLAN-instancing.md describes. For tests. */
const float *wgri_model_instance_records(int *count);

bool wgri_model_has_shadow_casters(int light_env);
bool wgri_model_has_shadow_receivers(int light_env);
void wgri_model_draw_shadow_casters(int light_env, const wgri_mat4_t *light_view_proj);

/* Picking helpers (pure; exposed for tests). */

/* Linear blend skinning of one position, as the skinned vertex shader does:
 * sum of weights[i] * (joints[joint_index[i]] * position). Joint indices at or
 * past joint_count are ignored. */
vec3_t wgri_model_skin_position(const wgri_mat4_t *joints, int joint_count, vec3_t position,
                              const uint8_t joint_index[4], const float weights[4]);

/* Alpha (0..1) of an 8-bit alpha image at texture coordinate (u, v), nearest
 * texel, wrapped outside 0..1 like the GPU sampler (repeat, clamp or mirror). */
float wgri_model_sample_alpha(const uint8_t *alpha, int width, int height, float u, float v,
                            wgr_texture_wrap_t wrap_u, wgr_texture_wrap_t wrap_v);

/* Per-vertex tangents (4 floats: xyz, w = bitangent sign) for normal mapping,
 * from positions (3), normals (3, unit) and texture coordinates (2) of an
 * indexed triangle list. Pure; exposed for tests. */
void wgri_model_generate_tangents(const float *positions, const float *normals, const float *uvs, int vertex_count,
                                const uint32_t *indices, int index_count, float *tangents);

#endif // WGRI_INTERNAL_MODEL_H
