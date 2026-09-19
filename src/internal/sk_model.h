#ifndef SK_INTERNAL_MODEL_H
#define SK_INTERNAL_MODEL_H

#include <stdint.h>

#include "internal/sk_asset.h"
#include "internal/sk_math.h"
#include <sk_texture.h> /* public; "" would find internal/sk_texture.h */

void sk_model_init(void);
void sk_model_deinit(void);

/* Draw queued model items [first, first + count) into the current render pass,
 * in queue order. Called by sk_render_end() while replaying the frame's command
 * list (models use a custom sg pipeline, so they must run inside the pass). */
void sk_model_draw_items(int first, int count);

/* Clear the model draw queue after the frame has been drawn. */
void sk_model_flush(void); /* the frame's joint matrices into the joint texture */
void sk_model_end_frame(void);

/* The files a glTF file needs (its buffers, and the images its textures use: a
 * compressed one this GPU can use instead of the texture's own), for sk_asset; exposed
 * for tests. */
void sk_model_list_gltf_dependencies(const unsigned char *data, int size, sk_asset_add_dependency_fn add,
                                     void *context);

/* Picking helpers (pure; exposed for tests). */

/* Linear blend skinning of one position, as the skinned vertex shader does:
 * sum of weights[i] * (joints[joint_index[i]] * position). Joint indices at or
 * past joint_count are ignored. */
vec3_t sk_model_skin_position(const sk_mat4_t *joints, int joint_count, vec3_t position,
                              const uint8_t joint_index[4], const float weights[4]);

/* Alpha (0..1) of an 8-bit alpha image at texture coordinate (u, v), nearest
 * texel, wrapped outside 0..1 like the GPU sampler (repeat, clamp or mirror). */
float sk_model_sample_alpha(const uint8_t *alpha, int width, int height, float u, float v,
                            sk_texture_wrap_t wrap_u, sk_texture_wrap_t wrap_v);

/* Per-vertex tangents (4 floats: xyz, w = bitangent sign) for normal mapping,
 * from positions (3), normals (3, unit) and texture coordinates (2) of an
 * indexed triangle list. Pure; exposed for tests. */
void sk_model_generate_tangents(const float *positions, const float *normals, const float *uvs, int vertex_count,
                                const uint32_t *indices, int index_count, float *tangents);

#endif // SK_INTERNAL_MODEL_H
