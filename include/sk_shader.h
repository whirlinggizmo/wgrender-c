#ifndef SK_SHADER_H
#define SK_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Custom material shaders (resources). See docs/PLAN-materials.md, "Custom shaders".
 *
 * - Write a fragment shader (and optionally a vertex hook) against shaders/sk.glsl,
 *   then compile it for every backend with tools/shaderpack.py name.glsl, which writes
 *   name.skshader. Load that file here (or ensure it through sk_asset first).
 * - Use it with sk_material_create_custom(shader): its parameters and textures are set
 *   by the names in your shader, with the sk_material_set_* functions.
 * - Deduplicated by path and reference counted; materials hold their own references. */

sk_handle_t sk_shader_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        sk_shader_release(sk_handle_t shader);

#ifdef __cplusplus
}
#endif

#endif // SK_SHADER_H
