#ifndef SK_INTERNAL_ASSET_H
#define SK_INTERNAL_ASSET_H

#include <stdbool.h>
#include <stddef.h>

/* Dependencies: files that other files reference (a .gltf's buffers and images).
 * A format registers a lister for its file extension; when the asset layer has
 * ensured such a file, it reads it, asks the lister for the URIs it references,
 * and ensures each of those (relative to the file's directory) before firing the
 * file's callbacks. The asset layer knows nothing about the formats. */

/* `required`: whether the file is unusable without it. A missing required
 * dependency fails the file's ensure; a missing optional one only logs a warning
 * (e.g. an image, which the loader replaces with a placeholder). */
typedef void (*sk_asset_add_dependency_fn)(const char *uri, bool required, void *context);

/* Report every URI the file references by calling `add`, as written in the file
 * (non-relative URIs such as data: are skipped by the caller). `data` is the whole
 * file. */
typedef void (*sk_asset_dependencies_fn)(const unsigned char *data, int size, sk_asset_add_dependency_fn add,
                                         void *context);

/* `extension` includes the dot, e.g. ".gltf"; matched case-insensitively.
 * Registrations persist across sk_asset_init (drawables register first). */
void sk_asset_register_dependencies(const char *extension, sk_asset_dependencies_fn list);

/* Resolve `uri`, relative to the directory of `base_path`, into `out`: decodes
 * %XX escapes, drops "." segments and applies ".." segments. Fails (false) when
 * the result would climb above the base path's top directory, or doesn't fit.
 * A leading "/" in base_path is kept. Pure; exposed for tests. */
bool sk_asset_join_relative(const char *base_path, const char *uri, char *out, size_t out_size);

/* True when `uri` names a file relative to the referencing file; false for
 * "data:" URIs, absolute URLs ("scheme://...") and absolute paths. */
bool sk_asset_is_relative_uri(const char *uri);

#endif // SK_INTERNAL_ASSET_H
