#ifndef WGRI_INTERNAL_LOADER_H
#define WGRI_INTERNAL_LOADER_H

#include <stdbool.h>

#include "wgr_types.h"

/* A resource type's loading, split for the asset pipeline (docs/HISTORY.md, "Loading pipeline (background preparation, budgeted GPU upload)"):
 *
 *   prepare  any thread: read and decode the file into CPU data (the slow part).
 *            Touches no handles, sokol_gfx or other shared state.
 *   finish   main thread: create the resource from that data (GPU uploads), one
 *            step per call so the pipeline can spread big resources over frames.
 *
 * A resource loads on create (wgri_resource_create, then wgri_asset_load): its handle
 * comes back at once, PENDING, and the pipeline makes the file local, prepares it and
 * then fills it in:
 *
 *   fill     main thread: one step of filling in the PENDING `resource` from the data.
 *            The resource core makes it READY when the last step returns
 *            WGRI_LOADER_DONE, and FAILED when any step of the load fails.
 *
 * A loader with fill loads on create. Until every module does, the others
 * keep the older pair below: finish creates the resource, and ensure loads it by its
 * extension (wgri_asset_register_loader); their sync create functions run both halves
 * in a row through wgri_loader_create. */

typedef enum {
    WGRI_LOADER_DONE = 0, /* the resource exists: *resource holds one reference */
    WGRI_LOADER_MORE,     /* call finish again */
    WGRI_LOADER_FAILED,   /* nothing was created (logged why) */
} wgri_loader_step_t;

typedef struct wgri_loader {
    const char *name; /* "texture", for logs */
    /* CPU data for `path`, or NULL when it can't be loaded (logged why). */
    void *(*prepare)(const char *path);
    /* One step of creating the resource under `path` from `prepared`. */
    wgri_loader_step_t (*finish)(void *prepared, const char *path, wgr_handle_t *resource);
    /* Free prepared data, at any point (finished or not). Resources that finish
     * already created stay alive. */
    void (*discard)(void *prepared);
    /* The live resource loaded from `path`, with a reference added, or 0. */
    wgr_handle_t (*find)(const char *path);
    /* Drop a reference (the pipeline's, after the callback ran). */
    void (*release)(wgr_handle_t resource);
    /* Load on create (wgri_asset_load): one step of filling in `resource` from the file
     * at local path `path`. */
    wgri_loader_step_t (*fill)(void *prepared, const char *path, wgr_handle_t resource);
} wgri_loader_t;

/* Load the file at asset path `path` (normalized: wgri_asset_normalize_path) into
 * `resource`, which the caller made PENDING, with `loader`'s prepare, fill and fail.
 * The file is made local as an ensured one is (the cache, a download, a redirect, a
 * mapped variant and its fallback, the files it names), then prepared on a worker and
 * filled in on the main thread within the upload budget. Every outcome comes in a
 * later frame, never inside this call: READY after fill's last step, or FAILED, both
 * told to the resource core. False only when the request can't be held (the asset
 * layer isn't running, or no room): the caller marks the resource FAILED. */
bool wgri_asset_load(const wgri_loader_t *loader, const char *path, wgr_handle_t resource);

/* Forget the load into `resource`, released while it loads: the file is still made
 * local, but neither fill nor fail is called and what was prepared is discarded. */
void wgri_asset_load_cancel(wgr_handle_t resource);

/* Load synchronously: find, or prepare and finish every step. The resource holds
 * one reference for the caller; 0 on failure. */
wgr_handle_t wgri_loader_create(const wgri_loader_t *loader, const char *path);

/* Register `loader` for file extensions (with the dot, matched case-insensitively):
 * files ensured with those extensions are prepared before their callback fires.
 * Registrations persist across wgri_asset_init, like dependency listers. */
void wgri_asset_register_loader(const char *extension, const wgri_loader_t *loader);

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
