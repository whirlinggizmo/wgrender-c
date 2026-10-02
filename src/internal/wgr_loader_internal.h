#ifndef WGRI_INTERNAL_LOADER_H
#define WGRI_INTERNAL_LOADER_H

#include <stdbool.h>

#include "wgr_types.h"

/* A resource type's loading, split for the asset pipeline (docs/HISTORY.md, "Loading pipeline (background preparation, budgeted GPU upload)").
 * A resource loads on create (wgri_resource_create, then wgri_asset_load): its handle
 * comes back at once, PENDING, and the pipeline makes the file local, prepares it and
 * then fills it in:
 *
 *   prepare  any thread: read and decode the file into CPU data (the slow part).
 *            Touches no handles, sokol_gfx or other shared state.
 *   fill     main thread: one step of filling in the PENDING `resource` from that data
 *            (GPU uploads), one step per call so the pipeline can spread big
 *            resources over frames. The resource core makes it READY when the last
 *            step returns WGRI_LOADER_DONE, and FAILED when any step of the load fails.
 *   discard  free prepared data, at any point (filled in or not). */

typedef enum {
    WGRI_LOADER_DONE = 0, /* the resource is filled in */
    WGRI_LOADER_MORE,     /* call fill again, in this frame or a later one */
    WGRI_LOADER_FAILED,   /* it can't be (logged why) */
} wgri_loader_step_t;

typedef struct wgri_loader {
    const char *name; /* "texture", for logs */
    void *(*prepare)(const char *path);   /* CPU data for the file at local `path`, or NULL (logged why) */
    wgri_loader_step_t (*fill)(void *prepared, const char *path, wgr_handle_t resource);
    void (*discard)(void *prepared);
} wgri_loader_t;

/* Load the file at asset path `path` (normalized: wgri_asset_normalize_path) into
 * `resource`, which the caller made PENDING, with `loader`'s prepare and fill.
 * The file is made local as an ensured one is (the cache, a download, a redirect, a
 * mapped variant and its fallback, the files it names), then prepared on a worker and
 * filled in on the main thread within the upload budget. Every outcome comes in a
 * later frame, never inside this call: READY after fill's last step, or FAILED, both
 * told to the resource core. False only when the request can't be held (the asset
 * layer isn't running, or no room): the caller marks the resource FAILED. */
bool wgri_asset_load(const wgri_loader_t *loader, const char *path, wgr_handle_t resource);

/* Forget the load into `resource`, released while it loads: the file is still made
 * local, but fill isn't called and what was prepared is discarded. */
void wgri_asset_load_cancel(wgr_handle_t resource);

/* Rewrite paths with `extension` when they're ensured, before anything is fetched or
 * cached: the texture module turns textures/rock.ktx into the variant this GPU can use
 * (textures/rock.bc7.ktx, ...). `map` writes the path to use into `out`, and into
 * `fallback` the path to use instead when that file is missing (textures/rock.png),
 * or "" for none; false leaves the path as it was. Not for files fetched from an
 * explicit URL (the caller chose the file). Registrations persist across
 * wgri_asset_init. */
typedef bool (*wgri_asset_path_mapper_fn)(const char *path, char *out, size_t out_size, char *fallback,
                                        size_t fallback_size);
void wgri_asset_register_path_mapper(const char *extension, wgri_asset_path_mapper_fn map);

#endif // WGRI_INTERNAL_LOADER_H
