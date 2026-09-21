#ifndef WGR_MATERIAL_H
#define WGR_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "wgr_texture.h"
#include "wgr_types.h"

/* Materials (resources): how a surface is shaded. See docs/PLAN-materials.md.
 *
 * - A mesh loaded from glTF creates one material per glTF material
 *   (wgr_mesh_get_material). They're shared by every model using that mesh, so
 *   changing one changes all of them. To change one model only, create a
 *   material and assign it with wgr_model_set_material.
 * - wgr_material_create returns a material with glTF's defaults (white, fully
 *   metallic, fully rough, opaque, single sided) and a reference owned by the
 *   caller. Models hold their own reference, so destroy yours when done.
 * - Shading follows glTF metallic-roughness and lights in linear color space.
 *   Colors from color handles and color textures (base color, emissive) are sRGB
 *   and converted to linear; numeric color values (vec3/vec4) are already linear,
 *   like glTF factors. Metallic-roughness, normal and occlusion textures are
 *   linear data. Textures use texture coordinate set 0 unless <t>_texcoord says
 *   otherwise (below).
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
 * Each texture <t> above (e.g. base_color_texture) also has:
 *
 *   <t>_texcoord                  int      0          texture coordinate set: 0 or 1
 *   <t>_offset                    vec2     0,0        texture transform (glTF
 *   <t>_rotation                  float    0          KHR_texture_transform):
 *   <t>_scale                     vec2     1,1        uv' = offset + rotate(scale * uv)
 *
 * Scale tiles the texture (2,2 repeats it twice each way); rotation is radians,
 * counterclockwise in texture space. Textures repeat, sample smoothly and use
 * mipmaps unless wgr_material_set_texture_sampling says otherwise.
 *
 * wgr_material_set_color accepts vec3 and vec4 parameters (vec3 ignores alpha).
 *
 * glTF vertex colors (COLOR_0) multiply the base color of models that have them. */

typedef enum {
    WGR_MATERIAL_PBR = 0,    /* glTF metallic-roughness, lit by scene lights */
    WGR_MATERIAL_UNLIT = 1,  /* base color x texture x tint; ignores lights (KHR_materials_unlit) */
    WGR_MATERIAL_CUSTOM = 2, /* a custom shader (wgr_material_create_custom); not for create/set_shading */
} wgr_material_shading_t;

wgr_handle_t wgr_material_create(wgr_material_shading_t shading);
/* A material drawn by a custom shader (wgr_shader.h). Its parameters and textures are
 * the ones the shader declares, set by those names with the setters below (texture
 * transforms like <t>_offset are the shader's business); the built-in names above
 * don't apply. Alpha mode and double-sided work as for built-in materials. Picking
 * treats its surfaces as solid everywhere. The material holds its own reference to
 * the shader. */
wgr_handle_t wgr_material_create_custom(wgr_handle_t shader);
/* The material's custom shader, or 0 for built-in shading. */
wgr_handle_t wgr_material_get_shader(wgr_handle_t material);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        wgr_material_release(wgr_handle_t material);

bool wgr_material_set_shading(wgr_handle_t material, wgr_material_shading_t shading);
wgr_material_shading_t wgr_material_get_shading(wgr_handle_t material);

/* cutoff applies to WGR_ALPHA_MASK (default 0.5). WGR_ALPHA_ADD isn't supported for
 * materials yet: refused. */
bool wgr_material_set_alpha_mode(wgr_handle_t material, wgr_alpha_mode_t mode, float cutoff);
wgr_alpha_mode_t wgr_material_get_alpha_mode(wgr_handle_t material);
/* Double-sided surfaces aren't back-face culled; back faces are lit from their side. */
bool wgr_material_set_double_sided(wgr_handle_t material, bool double_sided);
bool wgr_material_is_double_sided(wgr_handle_t material);

bool wgr_material_set_int(wgr_handle_t material, const char *name, int value);
bool wgr_material_set_float(wgr_handle_t material, const char *name, float value);
bool wgr_material_set_vec2(wgr_handle_t material, const char *name, float x, float y);
bool wgr_material_set_vec3(wgr_handle_t material, const char *name, float x, float y, float z);
bool wgr_material_set_vec4(wgr_handle_t material, const char *name, float x, float y, float z, float w);
bool wgr_material_set_color(wgr_handle_t material, const char *name, wgr_color_t color);
/* The material holds its own reference to the texture. 0 clears it. */
bool wgr_material_set_texture(wgr_handle_t material, const char *name, wgr_handle_t texture);
/* How texture `name` (e.g. "base_color_texture") is sampled. Default: repeat, linear. */
bool wgr_material_set_texture_sampling(wgr_handle_t material, const char *name, wgr_texture_wrap_t wrap_u,
                                      wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter);

#ifdef __cplusplus
}
#endif

#endif // WGR_MATERIAL_H
