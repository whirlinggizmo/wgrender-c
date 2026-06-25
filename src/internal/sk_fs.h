#ifndef SK_INTERNAL_FS_H
#define SK_INTERNAL_FS_H

#include <stdbool.h>
#include <stddef.h>

#include "sk_types.h"

/* Local filesystem layer — pure storage, no network (acquisition/fetch lives in
 * sk_asset). Desktop: real files via the C stdio FS, relative to the working dir.
 * Web: IDBFS mounted into MEMFS under a root dir; init mounts + restores (IDBFS→
 * MEMFS) so reads see persisted files, and writes flush back (MEMFS→IDBFS). The
 * restore await uses JSPI (see docs/PLAN-sk_fs.md). */

void sk_fs_init(const char *root_dir); /* root_dir NULL -> platform default */
void sk_fs_deinit(void);

/* Override the local root (base dir reads/writes resolve against). */
void sk_fs_set_root(const char *root);

/* Build the directly-openable local path for `path` (root + path). */
void sk_fs_resolve(const char *path, char *out, size_t out_size);

/* True once the local store is usable. Desktop: always. Web: after the IDBFS→
 * MEMFS restore completes. ensure must not read/fetch until ready. */
bool sk_fs_is_ready(void);

/* Is `path` present in the local store right now? */
bool sk_fs_exists(const char *path);

/* Read an entire file into a malloc'd buffer (NUL-terminated for convenience;
 * `*out_size` excludes the terminator). Caller frees with sk_fs_read_free. */
bool sk_fs_read(const char *path, unsigned char **out_data, int *out_size);
void sk_fs_read_free(unsigned char *data);

/* Write a file (creating parent dirs) and persist it (web: idbfs flush). */
bool sk_fs_write(const char *path, const unsigned char *data, int size);

/* Persist pending writes to the backing store (web idbfs syncfs; no-op desktop). */
void sk_fs_flush(void);

#endif // SK_INTERNAL_FS_H
