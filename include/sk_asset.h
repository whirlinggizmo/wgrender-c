#ifndef SK_ASSET_H
#define SK_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Asset loading is split from resource creation (see docs/ARCHITECTURE.md):
 *
 *   1. ENSURE the file is locally available — async; fetches from the asset host
 *      if absent (desktop: a file already on disk is ready immediately).
 *   2. CREATE the resource synchronously from that local path inside the ready
 *      callback: sk_texture_create(path), sk_mesh_create(path), etc.
 *
 * The callback receives a PATH, never bytes — user code stays pointer-free.
 *
 * Files that reference other files are ensured together: ensuring a .gltf (or
 * .glb) also ensures the buffers and images it references, relative to it, and the
 * callback fires once all of them are local. A missing buffer fails; a missing
 * image only warns, and the model uses the placeholder texture
 * (sk_texture_set_placeholder) in its place. */

typedef void (*sk_asset_callback_fn)(const char *path, void *user_data);

typedef enum {
    SK_ASSET_ADD_TASK_OK             =  0,
    SK_ASSET_ADD_TASK_ERR_INVALID    = -1,
    SK_ASSET_ADD_TASK_ERR_QUEUE_FULL = -2,
} sk_asset_add_task_result_t;

/* Flags for sk_asset_ensure_async (bitmask). */
enum {
    SK_ASSET_NONE        = 0,
    SK_ASSET_FORCE_FETCH = 1 << 0, /* re-download even if cached; no-op where no
                                      network fetch exists (desktop today) */
};

/* Set the asset base that logical paths resolve against. On desktop this is a
 * local directory ("examples/assets"); on web it is the fetch origin
 * ("/assets/") that missing files are downloaded from and then cached. Pass the
 * same logical paths on both platforms; only the base differs. */
void sk_asset_set_host(const char *host);
/* The asset base set with sk_asset_set_host (without a trailing slash), or "". */
const char *sk_asset_get_host(void);

/* Ensure an asset is locally available, then fire the callback with a directly
 * openable local path.
 *
 *   path      logical key: the cache path on web, the read path under the
 *             configured host on desktop, and (host + path) the default
 *             download location when fetched.
 *   fetch_url optional per-call override of the download SOURCE only — a URL /
 *             mirror / signed link, used verbatim; bytes are still cached and
 *             resolved under `path`. NULL = use the default host + path.
 *   flags     bitmask of SK_ASSET_* (e.g. SK_ASSET_FORCE_FETCH).
 *
 * Returns a task handle (kind ASSET_TASK) to attach callbacks to, or 0. */
sk_handle_t sk_asset_ensure_async(const char *path, const char *fetch_url,
                                  unsigned int flags);

/* Attach success/failure callbacks to a task. The managed queue fires them
 * during sk_asset_tick() (each frame, main thread) and then frees the task. */
sk_asset_add_task_result_t sk_asset_add_task(sk_handle_t task,
                                             sk_asset_callback_fn on_success,
                                             sk_asset_callback_fn on_failure,
                                             void *user_data);

#ifdef __cplusplus
}
#endif

#endif // SK_ASSET_H
