#ifndef WGRI_INTERNAL_MATERIAL_H
#define WGRI_INTERNAL_MATERIAL_H

#include <stdbool.h>

#include "internal/wgr_resource_internal.h"
#include "wgr_material.h"
#include "wgr_types.h"

/* Material data read by the model renderer and picking. See wgr_material.h and
 * docs/HISTORY.md, "Materials and shaders". */

typedef enum {
    WGRI_MATERIAL_TEXTURE_BASE_COLOR = 0,
    WGRI_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
    WGRI_MATERIAL_TEXTURE_NORMAL,
    WGRI_MATERIAL_TEXTURE_OCCLUSION,
    WGRI_MATERIAL_TEXTURE_EMISSIVE,
    WGRI_MATERIAL_TEXTURE_COUNT,
} wgri_material_texture_slot_t;

/* Texture slots a material has: the built-in ones above, or a custom shader's
 * textures in the order of its list (wgri_shader_t.textures). */
#define WGRI_MATERIAL_MAX_TEXTURES 8

/* A material texture and how it's sampled. */
typedef struct {
    wgr_handle_t texture; /* referenced; 0 = none */
    int texcoord;        /* texture coordinate set, 0 or 1 */
    float offset[2];     /* texture transform (KHR_texture_transform) */
    float rotation;
    float scale[2];
    wgr_texture_wrap_t wrap_u;
    wgr_texture_wrap_t wrap_v;
    wgr_texture_filter_t filter;
    bool mipmaps; /* false only for glTF samplers whose min filter has no mipmaps */
} wgri_material_texture_t;

typedef struct {
    wgri_resource_t resource; /* first: the resource core's part; a material is READY from the start */
    wgr_material_shading_t shading;
    wgr_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float base_color[4]; /* linear rgba */
    float emissive[3];   /* linear rgb */
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    wgri_material_texture_t textures[WGRI_MATERIAL_MAX_TEXTURES];
    /* WGR_MATERIAL_CUSTOM: the shader (referenced) and its parameter values, the
     * fragment block then the vertex block (std140, as the shader lays them out);
     * NULL until the shader is READY, its settings kept by name meanwhile */
    wgr_handle_t shader;
    unsigned char *custom_params;
    struct wgri_material_kept *kept;
    int kept_count, kept_capacity;
} wgri_material_t;

void wgri_material_init(void);
void wgri_material_deinit(void);

/* Resolve without logging; NULL for 0 or a stale handle. */
const wgri_material_t *wgri_material_get(wgr_handle_t material);

/* A custom material's shader, once it's READY, with the material's parameters laid
 * out for it and its kept settings applied (the first time); NULL while it loads, if
 * it failed, or for a built-in material. What the draw paths use. */
struct wgri_shader;
struct wgri_shader *wgri_material_custom_shader(const wgri_material_t *material);
/* Whether a custom material's shader failed to load: it then draws as
 * wgri_material_failed. */
bool wgri_material_custom_failed(const wgri_material_t *material);
/* What a custom material whose shader failed draws as: flat magenta, unlit, the
 * shader's version of the texture placeholder. */
const wgri_material_t *wgri_material_failed(void);
/* A READY screen shader's material (an effect's), and a READY surface shader's (a
 * model's or a sprite's); both false while the shader loads, which is when they
 * can't tell, so a caller refuses only what it knows is wrong. */
bool wgri_material_is_surface(wgr_handle_t material);

/* The texture transform as a 2x3 matrix, rows (m[0] m[1] m[2]) and
 * (m[3] m[4] m[5]): u' = m[0] u + m[1] v + m[2], v' = m[3] u + m[4] v + m[5].
 * Same as glTF KHR_texture_transform: translation * rotation * scale. Pure. */
void wgri_material_uv_matrix(const wgri_material_texture_t *texture, float m[6]);

/* Whether texture `name` uses its mipmaps (glTF samplers can turn them off). */
bool wgri_material_set_texture_mipmaps(wgr_handle_t material, const char *name, bool mipmaps);

/* A custom material whose shader is a screen effect (wgr_render_add_effect): it has no
 * program for models or sprites, so they refuse it. */
bool wgri_material_is_screen(wgr_handle_t material);

/* Reference counting (meshes and models hold references). */

#endif // WGRI_INTERNAL_MATERIAL_H
