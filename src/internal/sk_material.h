#ifndef SK_INTERNAL_MATERIAL_H
#define SK_INTERNAL_MATERIAL_H

#include <stdbool.h>

#include <sk_material.h> /* the public header; "" would find this file */
#include "sk_types.h"

/* Material data read by the model renderer and picking. See sk_material.h and
 * docs/PLAN-materials.md. */

typedef enum {
    SK_MATERIAL_TEXTURE_BASE_COLOR = 0,
    SK_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
    SK_MATERIAL_TEXTURE_NORMAL,
    SK_MATERIAL_TEXTURE_OCCLUSION,
    SK_MATERIAL_TEXTURE_EMISSIVE,
    SK_MATERIAL_TEXTURE_COUNT,
} sk_material_texture_slot_t;

/* Texture slots a material has: the built-in ones above, or a custom shader's
 * textures in the order of its list (sk_shader_t.textures). */
#define SK_MATERIAL_MAX_TEXTURES 8

/* A material texture and how it's sampled. */
typedef struct {
    sk_handle_t texture; /* referenced; 0 = none */
    int texcoord;        /* texture coordinate set, 0 or 1 */
    float offset[2];     /* texture transform (KHR_texture_transform) */
    float rotation;
    float scale[2];
    sk_texture_wrap_t wrap_u;
    sk_texture_wrap_t wrap_v;
    sk_texture_filter_t filter;
    bool mipmaps; /* false only for glTF samplers whose min filter has no mipmaps */
} sk_material_texture_t;

typedef struct {
    sk_material_shading_t shading;
    sk_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float base_color[4]; /* linear rgba */
    float emissive[3];   /* linear rgb */
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    sk_material_texture_t textures[SK_MATERIAL_MAX_TEXTURES];
    /* SK_MATERIAL_CUSTOM: the shader (referenced) and its parameter values, the
     * fragment block then the vertex block (std140, as the shader lays them out) */
    sk_handle_t shader;
    unsigned char *custom_params;
    int ref_count;
} sk_material_t;

void sk_material_init(void);
void sk_material_deinit(void);

/* Resolve without logging; NULL for 0 or a stale handle. */
const sk_material_t *sk_material_get(sk_handle_t material);

/* The texture transform as a 2x3 matrix, rows (m[0] m[1] m[2]) and
 * (m[3] m[4] m[5]): u' = m[0] u + m[1] v + m[2], v' = m[3] u + m[4] v + m[5].
 * Same as glTF KHR_texture_transform: translation * rotation * scale. Pure. */
void sk_material_uv_matrix(const sk_material_texture_t *texture, float m[6]);

/* Whether texture `name` uses its mipmaps (glTF samplers can turn them off). */
bool sk_material_set_texture_mipmaps(sk_handle_t material, const char *name, bool mipmaps);

/* A custom material whose shader is a screen effect (sk_render_add_effect): it has no
 * program for models or sprites, so they refuse it. */
bool sk_material_is_screen(sk_handle_t material);

/* Reference counting (meshes and models hold references). */
void sk_material_retain(sk_handle_t material);

#endif // SK_INTERNAL_MATERIAL_H
