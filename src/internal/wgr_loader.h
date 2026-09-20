#ifndef WGR_INTERNAL_LOADER_H
#define WGR_INTERNAL_LOADER_H

#include <stdbool.h>

#include "wgr_types.h"

/* A resource type's loading, split for the asset pipeline (docs/PLAN-pipeline.md):
 *
 *   prepare  any thread: read and decode the file into CPU data (the slow part).
 *            Touches no handles, sokol_gfx or other shared state.
 *   finish   main thread: create the resource from that data (GPU uploads), one
 *            step per call so the pipeline can spread big resources over frames.
 *
 * The sync create functions (wgr_texture_create(path), ...) run both halves in a
 * row through wgr_loader_create. */

typedef enum {
    WGR_LOADER_DONE = 0, /* the resource exists: *resource holds one reference */
    WGR_LOADER_MORE,     /* call finish again */
    WGR_LOADER_FAILED,   /* nothing was created (logged why) */
} wgr_loader_step_t;

typedef struct {
    const char *name; /* "texture", for logs */
    /* CPU data for `path`, or NULL when it can't be loaded (logged why). */
    void *(*prepare)(const char *path);
    /* One step of creating the resource under `path` from `prepared`. */
    wgr_loader_step_t (*finish)(void *prepared, const char *path, wgr_handle_t *resource);
    /* Free prepared data, at any point (finished or not). Resources that finish
     * already created stay alive. */
    void (*discard)(void *prepared);
    /* The live resource loaded from `path`, with a reference added, or 0. */
    wgr_handle_t (*find)(const char *path);
    /* Drop a reference (the pipeline's, after the callback ran). */
    void (*release)(wgr_handle_t resource);
} wgr_loader_t;

/* Load synchronously: find, or prepare and finish every step. The resource holds
 * one reference for the caller; 0 on failure. */
wgr_handle_t wgr_loader_create(const wgr_loader_t *loader, const char *path);

/* Register `loader` for file extensions (with the dot, matched case-insensitively):
 * files ensured with those extensions are prepared before their callback fires.
 * Registrations persist across wgr_asset_init, like dependency listers. */
void wgr_asset_register_loader(const char *extension, const wgr_loader_t *loader);

/* Rewrite paths with `extension` when they're ensured, before anything is fetched or
 * cached: the texture module turns textures/rock.ktx into the variant this GPU can use
 * (textures/rock.bc7.ktx, ...). `map` writes the path to use into `out`, and into
 * `fallback` the path to use instead when that file is missing (textures/rock.png),
 * or "" for none; false leaves the path as it was. Not for files fetched from an
 * explicit URL (the caller chose the file). Registrations persist across
 * wgr_asset_init. */
typedef bool (*wgr_asset_path_mapper_fn)(const char *path, char *out, size_t out_size, char *fallback,
                                        size_t fallback_size);
void wgr_asset_register_path_mapper(const char *extension, wgr_asset_path_mapper_fn map);

#endif // WGR_INTERNAL_LOADER_H
