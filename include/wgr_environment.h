#ifndef WGR_ENVIRONMENT_H
#define WGR_ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Environment maps (resources): light a scene's models with the world around
 * them, and show it as the scene's background. See docs/HISTORY.md, "Environment lighting (image-based lighting) and tone mapping".
 *
 * - Load an equirectangular (latitude-longitude, 2:1) image: a Radiance .hdr
 *   (true high dynamic range, recommended) or a PNG/JPEG (sRGB, low dynamic range).
 * - Preparing its lighting takes CPU time: diffuse irradiance and blurred
 *   reflections for every roughness, a fraction of a second for a 1K image. It's
 *   done on a worker thread (where there are threads) while the frames go on.
 * - Loaded on create and released like any resource (wgr_resource.h); scenes hold
 *   their own references.
 * - Use with wgr_scene_set_environment, wgr_scene_set_background and
 *   wgr_scene_set_tonemap (wgr_scene.h). */

/* The environment at asset path `path`, loading on create (wgr_resource.h): PENDING
 * at once, then READY, or FAILED in a later frame for a file that is missing, fails
 * to download or won't decode. FAILED at once for a path outside the asset root,
 * before wgr_run has started the asset layer, or on a graphics backend that can't
 * filter half-float textures (environments are off there). 0 only when there's no
 * room for another environment. Until it is READY a scene using it is lit as if it
 * had none and draws no background; FAILED stays that way (the log says why). */
wgr_handle_t wgr_environment_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif // WGR_ENVIRONMENT_H
