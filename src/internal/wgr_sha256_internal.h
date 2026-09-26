#ifndef WGRI_INTERNAL_SHA256_H
#define WGRI_INTERNAL_SHA256_H

#include <stddef.h>

/* SHA-256 (FIPS 180-4), for the asset manifest's content hashes
 * (docs/PLAN-asset-cache.md). `out` gets "sha256:" + 64 lowercase hex digits and a
 * terminator: WGRI_SHA256_TEXT bytes. */
#define WGRI_SHA256_TEXT 72

void wgri_sha256_text(const unsigned char *data, size_t size, char out[WGRI_SHA256_TEXT]);

#endif // WGRI_INTERNAL_SHA256_H
