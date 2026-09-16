#ifndef SK_MATERIAL_H
#define SK_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_types.h"

/* Materials (resources): how a surface is shaded. See docs/PLAN-materials.md.
 *
 * - A mesh loaded from glTF creates one material per glTF material
 *   (sk_mesh_get_material). They're shared by every model using that mesh, so
 *   changing one changes all of them. To change one model only, create a
 *   material and assign it with sk_model_set_material.
 * - sk_material_create returns a material with glTF's defaults (white, fully
 *   metallic, fully rough, opaque, single sided) and a reference owned by the
 *   caller. Models hold their own reference, so destroy yours when done.
 * - Shading follows glTF metallic-roughness and lights in linear color space.
 *   Colors from color handles and color textures (base color, emissive) are sRGB
 *   and converted to linear; numeric color values (vec3/vec4) are already linear,
 *   like glTF factors. Metallic-roughness, normal and occlusion textures are
 *   linear data. Textures use texture coordinate set 0.
 *
 * Parameters are set by name. Setters return false for an unknown name or a
 * value of the wrong kind.
 *
 *   name                          kind     default    notes
 *   base_color                    vec4     1,1,1,1    linear rgba; alpha drives MASK/BLEND
 *   base_color_texture            texture  none       sRGB rgba
 *   metallic                      float    1          0..1
 *   roughness                     float    1          0..1
 *   metallic_roughness_texture    texture  none       green = roughness, blue = metallic
 *   normal_texture                texture  none       tangent-space normal map
 *   normal_scale                  float    1
 *   occlusion_texture             texture  none       red = ambient occlusion
 *   occlusion_strength            float    1          0..1
 *   emissive                      vec3     0,0,0      linear rgb, may exceed 1
 *   emissive_texture              texture  none       sRGB rgb
 *
 * sk_material_set_color accepts vec3 and vec4 parameters (vec3 ignores alpha). */

typedef enum {
    SK_MATERIAL_PBR = 0,   /* glTF metallic-roughness, lit by scene lights */
    SK_MATERIAL_UNLIT = 1, /* base color x texture x tint; ignores lights (KHR_materials_unlit) */
} sk_material_shading_t;

typedef enum {
    SK_MATERIAL_ALPHA_OPAQUE = 0, /* alpha ignored */
    SK_MATERIAL_ALPHA_MASK = 1,   /* fully opaque or fully transparent, split at the cutoff */
    SK_MATERIAL_ALPHA_BLEND = 2,  /* alpha blended, drawn after opaque surfaces, back to front */
} sk_material_alpha_t;

sk_handle_t sk_material_create(sk_material_shading_t shading);
void        sk_material_destroy(sk_handle_t material);

bool sk_material_set_shading(sk_handle_t material, sk_material_shading_t shading);
sk_material_shading_t sk_material_get_shading(sk_handle_t material);

/* cutoff applies to MASK (default 0.5). */
bool sk_material_set_alpha_mode(sk_handle_t material, sk_material_alpha_t mode, float cutoff);
sk_material_alpha_t sk_material_get_alpha_mode(sk_handle_t material);
/* Double-sided surfaces aren't back-face culled; back faces are lit from their side. */
bool sk_material_set_double_sided(sk_handle_t material, bool double_sided);
bool sk_material_is_double_sided(sk_handle_t material);

bool sk_material_set_float(sk_handle_t material, const char *name, float value);
bool sk_material_set_vec3(sk_handle_t material, const char *name, float x, float y, float z);
bool sk_material_set_vec4(sk_handle_t material, const char *name, float x, float y, float z, float w);
bool sk_material_set_color(sk_handle_t material, const char *name, sk_handle_t color);
/* The material holds its own reference to the texture. 0 clears it. */
bool sk_material_set_texture(sk_handle_t material, const char *name, sk_handle_t texture);

#ifdef __cplusplus
}
#endif

#endif // SK_MATERIAL_H
