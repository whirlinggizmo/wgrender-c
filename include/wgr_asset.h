#ifndef WGR_ASSET_H
#define WGR_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Asset loading is split from resource creation (see docs/ARCHITECTURE.md):
 *
 *   1. ENSURE the file is locally available — async; fetches from the asset host
 *      if absent (desktop: a file already on disk is ready immediately).
 *   2. CREATE the resource synchronously from that local path inside the ready
 *      callback: wgr_texture_create(path), wgr_mesh_create(path), etc.
 *
 * The callback receives a PATH, never bytes — user code stays pointer-free.
 *
 * Files that reference other files are ensured together: ensuring a .gltf (or
 * .glb) also ensures the buffers and images it references, relative to it, and the
 * callback fires once all of them are local. A missing buffer fails; a missing
 * image only warns, and the model uses the placeholder texture
 * (wgr_texture_set_placeholder) in its place.
 *
 * Files are also loaded before the callback fires, so creating the resource in the
 * callback is cheap: decoding runs on worker threads, and GPU uploads run on the
 * main thread within a per-frame budget (wgr_asset_set_upload_budget). The extension
 * names the resource: .png/.jpg/.jpeg a texture, .gltf/.glb a mesh, .hdr an
 * environment, .wav/.ogg/.mp3 audio. A resource the callback doesn't create is
 * freed after it returns; pass WGR_ASSET_FILE_ONLY for a file used any other way
 * (a PNG for wgr_environment_create, say). A file that can't be loaded fires the
 * failure callback. */

typedef void (*wgr_asset_callback_fn)(const char *path, void *user_data);

typedef enum {
    WGR_ASSET_ADD_TASK_OK             =  0,
    WGR_ASSET_ADD_TASK_ERR_INVALID    = -1,
    WGR_ASSET_ADD_TASK_ERR_QUEUE_FULL = -2,
} wgr_asset_add_task_result_t;

/* Flags for wgr_asset_ensure_async (bitmask). */
enum {
    WGR_ASSET_NONE        = 0,
    WGR_ASSET_FORCE_FETCH = 1 << 0, /* re-download even if cached; no-op where no
                                      network fetch exists (desktop today) */
    WGR_ASSET_FILE_ONLY   = 1 << 1, /* only make the file local; don't load it as the
                                      resource its extension names (see below) */
};

/* Set the asset base that logical paths resolve against. On desktop this is a
 * local directory ("examples/assets"); on web it is the fetch origin
 * ("/assets/") that missing files are downloaded from and then cached. Pass the
 * same logical paths on both platforms; only the base differs. */
void wgr_asset_set_host(const char *host);
/* The asset base set with wgr_asset_set_host (without a trailing slash), or "". */
const char *wgr_asset_get_host(void);

/* Ensure an asset is locally available, then fire the callback with a directly
 * openable local path.
 *
 *   path      logical key: the cache path on web, the read path under the
 *             configured host on desktop, and (host + path) the default
 *             download location when fetched.
 *   fetch_url optional per-call override of the download SOURCE only — a URL /
 *             mirror / signed link, used verbatim; bytes are still cached and
 *             resolved under `path`. NULL = use the default host + path.
 *   flags     bitmask of WGR_ASSET_* (e.g. WGR_ASSET_FORCE_FETCH).
 *
 * Returns a task handle (kind ASSET_TASK) to attach callbacks to, or 0. */
wgr_handle_t wgr_asset_ensure_async(const char *path, const char *fetch_url,
                                  unsigned int flags);

/* Attach success/failure callbacks to a task. The managed queue fires them
 * during wgr_asset_tick() (each frame, main thread) and then frees the task. */
wgr_asset_add_task_result_t wgr_asset_add_task(wgr_handle_t task,
                                             wgr_asset_callback_fn on_success,
                                             wgr_asset_callback_fn on_failure,
                                             void *user_data);

/* Groups: one task for many files (for a level or a loading screen). A group
 * completes when all of its members have, successfully only if they all did, and
 * then fires its own callbacks (wgr_asset_add_task) with an empty path. Members
 * keep their own callbacks, if they have any, and load without them. The group
 * holds its members' resources until its callbacks have run, so they can be
 * created there (with the paths the members' callbacks received). */
wgr_handle_t wgr_asset_group_create(void);
/* Add a file task (wgr_asset_ensure_async) to a group. False for anything else, or
 * a task that's already in a group. */
bool wgr_asset_group_add(wgr_handle_t group, wgr_handle_t task);

/* Rough progress of a task or group, 0..1, for loading screens: a file counts a
 * quarter each for being fetched, its dependencies, being prepared and being
 * finished. 1 once the task has completed (its handle is no longer live). */
float wgr_asset_get_progress(wgr_handle_t task);

/* Redirects: load files from somewhere else, for mods, translations or a CDN.
 * Files whose path starts with `prefix` are looked for under `target` instead:
 *
 *   wgr_asset_add_redirect("textures/", "mods/hd/textures/");
 *       textures/rock.png loads mods/hd/textures/rock.png if it exists, else
 *       textures/rock.png
 *   wgr_asset_add_redirect("models/", "https://cdn.example.com/game/models/");
 *       a target with "://" is where the file downloads from (web; desktop builds
 *       don't download yet): it's still cached and loaded as models/...
 *
 * Rules stack: every path rule matching a file is tried, the one added last first,
 * then the file's own path, so later rules sit on top (a mod over a mod, fr-CA over
 * fr). A missing file under a path rule isn't an error; the next one is tried (on
 * the web that costs a request). A download rule doesn't stack: the newest one
 * matching a path is where it downloads from. Prefixes are plain text, matched at
 * the start of the path ("textures/", not "*.png").
 *
 * Redirects apply to files ensured with wgr_asset_ensure_async (without an explicit
 * fetch_url) and the files they reference (a model's buffers and images, found next
 * to wherever the model came from); the callback gets the path of the file found.
 * Direct wgr_*_create(path) calls load the path they're given. Up to 32 rules; false
 * when full or given an empty prefix or target. */
bool wgr_asset_add_redirect(const char *prefix, const char *target);
void wgr_asset_clear_redirects(void);

/* Ping an asset host: `on_done` fires on a later frame with the round trip in
 * milliseconds, or a negative value when it can't be reached within `timeout_ms`
 * (<= 0: 5000). `host` NULL pings the current one (wgr_asset_set_host). On the web
 * it's a HEAD request to the host (any response counts, even a 404; another origin
 * needs no CORS headers). On desktop the host is a local directory: 0 if it exists,
 * negative if not (or a URL: desktop builds don't download yet). False when
 * `on_done` is NULL or 8 pings are already waiting. */
typedef void (*wgr_asset_ping_fn)(const char *host, float milliseconds, void *user_data);
bool wgr_asset_ping_host(const char *host, int timeout_ms, wgr_asset_ping_fn on_done, void *user_data);

/* Milliseconds per frame spent finishing loads on the main thread (GPU uploads),
 * default 4. At least one step runs each frame, so one large texture can exceed
 * it: a 4096x4096 texture is one upload of ~45 ms. */
void wgr_asset_set_upload_budget(float milliseconds);

#ifdef __cplusplus
}
#endif

#endif // WGR_ASSET_H
