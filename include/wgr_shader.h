#ifndef WGR_SHADER_H
#define WGR_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_resource.h"
#include "wgr_types.h"

/* Custom material shaders (resources). See docs/HISTORY.md, "Materials and shaders", "Custom shaders".
 *
 * - Write a fragment shader (and optionally a vertex hook) against shaders/wgr.glsl,
 *   then compile it for every backend with tools/pack_shader.py name.glsl, which writes
 *   name.wgrshader. Load that file here: it loads on create, like any resource.
 * - Use it with wgr_material_create_custom(shader): its parameters and textures are set
 *   by the names in your shader, with the wgr_material_set_* functions.
 * - Loaded on create and released like any resource (wgr_resource.h); materials hold
 *   their own references. */

/* The shader at asset path `path`, loading on create (wgr_resource.h): PENDING at
 * once, then READY, or FAILED in a later frame for a file that is missing, fails to
 * download, isn't a .wgrshader of this format version, or that this graphics backend
 * refuses, and FAILED at once for a path outside the asset root or before wgr_run has
 * started the asset layer. 0 only when there's no room for another shader. A custom
 * material made from it takes it in any status (wgr_material_create_custom says what
 * it does until the shader is READY). */
wgr_handle_t wgr_shader_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif // WGR_SHADER_H
