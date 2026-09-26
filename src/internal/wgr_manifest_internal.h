#ifndef WGRI_INTERNAL_MANIFEST_H
#define WGRI_INTERNAL_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

#include "internal/wgr_sha256_internal.h"

/* One directory's asset manifest (docs/PLAN-asset-cache.md): the content hash of each
 * file in it, and of each subdirectory's own manifest.json.
 *
 *   { "wgr_manifest": 1,
 *     "files": { "tiles.png": "sha256:<64 hex>", ... },
 *     "dirs":  { "models": "sha256:<64 hex>", ... } }
 *
 * Other top-level keys are ignored (a later version may add some). */
typedef struct {
    char *name;
    char hash[WGRI_SHA256_TEXT];
    bool dir;
} wgri_manifest_entry_t;

typedef struct {
    wgri_manifest_entry_t *entries; /* sorted by (dir, name) */
    int count;
} wgri_manifest_t;

/* Read a manifest. False, with nothing to free, for anything that isn't one exactly:
 * not JSON, another wgr_manifest version, a hash that isn't "sha256:" + 64
 * lowercase hex, a name that is empty, "." or "..", or has a "/", or the same name
 * twice. A manifest that is wrong anywhere is trusted nowhere. */
bool wgri_manifest_parse(const char *json, size_t size, wgri_manifest_t *out);

/* The hash a manifest gives a file (dir false) or a subdirectory's manifest (dir
 * true), or NULL when it doesn't list the name. */
const char *wgri_manifest_find(const wgri_manifest_t *manifest, const char *name, bool dir);

void wgri_manifest_free(wgri_manifest_t *manifest);

#endif // WGRI_INTERNAL_MANIFEST_H
