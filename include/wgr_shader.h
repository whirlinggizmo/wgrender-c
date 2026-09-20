#ifndef WGR_SHADER_H
#define WGR_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Custom material shaders (resources). See docs/PLAN-materials.md, "Custom shaders".
 *
 * - Write a fragment shader (and optionally a vertex hook) against shaders/wgr.glsl,
 *   then compile it for every backend with tools/shaderpack.py name.glsl, which writes
 *   name.wgrshader. Load that file here (or ensure it through wgr_asset first).
 * - Use it with wgr_material_create_custom(shader): its parameters and textures are set
 *   by the names in your shader, with the wgr_material_set_* functions.
 * - Deduplicated by path and reference counted; materials hold their own references. */

wgr_handle_t wgr_shader_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        wgr_shader_release(wgr_handle_t shader);

#ifdef __cplusplus
}
#endif

#endif // WGR_SHADER_H
