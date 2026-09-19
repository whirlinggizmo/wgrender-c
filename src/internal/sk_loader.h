#ifndef SK_INTERNAL_LOADER_H
#define SK_INTERNAL_LOADER_H

#include <stdbool.h>

#include "sk_types.h"

/* A resource type's loading, split for the asset pipeline (docs/PLAN-pipeline.md):
 *
 *   prepare  any thread: read and decode the file into CPU data (the slow part).
 *            Touches no handles, sokol_gfx or other shared state.
 *   finish   main thread: create the resource from that data (GPU uploads), one
 *            step per call so the pipeline can spread big resources over frames.
 *
 * The sync create functions (sk_texture_create(path), ...) run both halves in a
 * row through sk_loader_create. */

typedef enum {
    SK_LOADER_DONE = 0, /* the resource exists: *resource holds one reference */
    SK_LOADER_MORE,     /* call finish again */
    SK_LOADER_FAILED,   /* nothing was created (logged why) */
} sk_loader_step_t;

typedef struct {
    const char *name; /* "texture", for logs */
    /* CPU data for `path`, or NULL when it can't be loaded (logged why). */
    void *(*prepare)(const char *path);
    /* One step of creating the resource under `path` from `prepared`. */
    sk_loader_step_t (*finish)(void *prepared, const char *path, sk_handle_t *resource);
    /* Free prepared data, at any point (finished or not). Resources that finish
     * already created stay alive. */
    void (*discard)(void *prepared);
    /* The live resource loaded from `path`, with a reference added, or 0. */
    sk_handle_t (*find)(const char *path);
    /* Drop a reference (the pipeline's, after the callback ran). */
    void (*release)(sk_handle_t resource);
} sk_loader_t;

/* Load synchronously: find, or prepare and finish every step. The resource holds
 * one reference for the caller; 0 on failure. */
sk_handle_t sk_loader_create(const sk_loader_t *loader, const char *path);

/* Register `loader` for file extensions (with the dot, matched case-insensitively):
 * files ensured with those extensions are prepared before their callback fires.
 * Registrations persist across sk_asset_init, like dependency listers. */
void sk_asset_register_loader(const char *extension, const sk_loader_t *loader);

/* Rewrite paths with `extension` when they're ensured, before anything is fetched or
 * cached: the texture module turns textures/rock.ktx into the variant this GPU can use
 * (textures/rock.bc7.ktx, ...). `map` writes the path to use into `out`, and into
 * `fallback` the path to use instead when that file is missing (textures/rock.png),
 * or "" for none; false leaves the path as it was. Not for files fetched from an
 * explicit URL (the caller chose the file). Registrations persist across
 * sk_asset_init. */
typedef bool (*sk_asset_path_mapper_fn)(const char *path, char *out, size_t out_size, char *fallback,
                                        size_t fallback_size);
void sk_asset_register_path_mapper(const char *extension, sk_asset_path_mapper_fn map);

#endif // SK_INTERNAL_LOADER_H
