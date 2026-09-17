#ifndef SK_ENVIRONMENT_H
#define SK_ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Environment maps (resources): light a scene's models with the world around
 * them, and show it as the scene's background. See docs/PLAN-environment.md.
 *
 * - Load an equirectangular (latitude-longitude, 2:1) image: a Radiance .hdr
 *   (true high dynamic range, recommended) or a PNG/JPEG (sRGB, low dynamic range).
 * - Creating one prepares its lighting on the CPU: diffuse irradiance and blurred
 *   reflections for every roughness. That takes a fraction of a second for a 1K
 *   image, so create environments while loading, not every frame.
 * - Deduplicated by path and reference counted; scenes hold their own references.
 * - Use with sk_scene_set_environment, sk_scene_set_background and
 *   sk_scene_set_tonemap (sk_scene.h). */

sk_handle_t sk_environment_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        sk_environment_release(sk_handle_t environment);

#ifdef __cplusplus
}
#endif

#endif // SK_ENVIRONMENT_H
