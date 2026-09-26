#ifndef WGRI_INTERNAL_ASSET_H
#define WGRI_INTERNAL_ASSET_H

#include <stdbool.h>
#include <stddef.h>

/* Dependencies: files that other files reference (a .gltf's buffers and images).
 * A format registers a lister for its file extension; when the asset layer has
 * ensured such a file, it reads it, asks the lister for the URIs it references,
 * and ensures each of those (relative to the file's directory) before firing the
 * file's callbacks. The asset layer knows nothing about the formats. */

/* `required`: whether the file is unusable without it. A missing required
 * dependency fails the file's ensure; a missing optional one only logs a warning
 * (e.g. an image, which the loader replaces with a placeholder). `fallback_uri`
 * (or NULL): a file to ensure instead when `uri` is missing, e.g. a texture's own
 * image for its compressed file. */
typedef void (*wgri_asset_add_dependency_fn)(const char *uri, const char *fallback_uri, bool required,
                                           void *context);

/* Report every URI the file references by calling `add`, as written in the file
 * (non-relative URIs such as data: are skipped by the caller). `data` is the whole
 * file. */
typedef void (*wgri_asset_dependencies_fn)(const unsigned char *data, int size, wgri_asset_add_dependency_fn add,
                                         void *context);

/* `extension` includes the dot, e.g. ".gltf"; matched case-insensitively.
 * Registrations persist across wgri_asset_init (drawables register first). */
void wgri_asset_register_dependencies(const char *extension, wgri_asset_dependencies_fn list);

/* Resolve `uri`, relative to the directory of `base_path`, into `out`: decodes
 * %XX escapes, drops "." segments and applies ".." segments. Fails (false) when
 * the result would climb above the base path's top directory, or doesn't fit.
 * A leading "/" in base_path is kept. Pure; exposed for tests. */
bool wgri_asset_join_relative(const char *base_path, const char *uri, char *out, size_t out_size);

/* A path a program names (ensure, evict, a redirect), made one that stays under the
 * asset root, as wgutils' fileio does: "\\" becomes "/", empty and "." segments go,
 * ".." takes back the segment before it. False for a path that is absolute ("/" or
 * "\\" first), names a drive or has any ":", climbs above the root, is empty once
 * normalized, or doesn't fit. Pure; exposed for tests. */
bool wgri_asset_normalize_path(const char *path, char *out, size_t out_size);

/* Where the asset layer found the file at local path `local` (a redirect or a
 * fallback, wgr_asset_add_redirect), into `out`: true if elsewhere, else `local` as
 * it is. Loaders reading the files a file references (a glTF's buffers and images)
 * look them up here. Any thread. */
bool wgri_asset_found_path(const char *local, char *out, size_t out_size);

/* True when `uri` names a file relative to the referencing file; false for
 * "data:" URIs, absolute URLs ("scheme://...") and absolute paths. */
bool wgri_asset_is_relative_uri(const char *uri);

/* Prepare workers: -1 = the default (CPU cores - 1, at most 4; none without
 * threads), 0 = prepare on the main thread, one file per frame. Restarts the
 * workers when the asset layer is running; call it while nothing is loading. */
void wgri_asset_set_worker_count(int count);
int wgri_asset_get_worker_count(void);

/* When a response stops being fresh, from its Cache-Control and Age headers (either
 * may be NULL or ""), for a response received at `now` (seconds since 1970): `now`
 * plus what is left of max-age; a year for immutable without max-age; 0 (never
 * fresh) for no-cache, no-store, or neither max-age nor immutable. Pure; exposed for
 * tests. */
double wgri_asset_fresh_until(const char *cache_control, const char *age, double now);

/* Asset tasks not finished yet (for test tooling), and a warning per task saying where
 * each is stuck. */
int wgri_asset_pending_count(void);
void wgri_asset_pending_log(void);

#endif // WGRI_INTERNAL_ASSET_H
