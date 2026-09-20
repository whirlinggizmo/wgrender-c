#ifndef WGR_INTERNAL_FS_H
#define WGR_INTERNAL_FS_H

#include <stdbool.h>
#include <stddef.h>

#include "wgr_types.h"

/* Local filesystem layer — pure storage, no network (acquisition/fetch lives in
 * wgr_asset). Desktop: real files via the C stdio FS, relative to the working dir.
 * Web: files in MEMFS under a root dir, kept between visits in IndexedDB, one
 * record per file. Init reads only the cache's list of files; a cached file is
 * read into MEMFS when it's needed (wgr_fs_cache_read_begin), so startup doesn't
 * grow with the cache (docs/PLAN-wgr_fs.md). */

void wgr_fs_init(const char *root_dir); /* root_dir NULL -> platform default */
void wgr_fs_deinit(void);

/* Override the local root (base dir reads/writes resolve against). */
void wgr_fs_set_root(const char *root);

/* Build the directly-openable local path for `path` (root + path). */
void wgr_fs_resolve(const char *path, char *out, size_t out_size);

/* True once the local store is usable. Desktop: always. Web: once the cache's list
 * of files is read. ensure must not read/fetch until ready. */
bool wgr_fs_is_ready(void);

/* Is `path` local (openable) right now? */
bool wgr_fs_exists(const char *path);

/* Read an entire file into a malloc'd buffer (NUL-terminated for convenience;
 * `*out_size` excludes the terminator). Caller frees with wgr_fs_read_free. */
bool wgr_fs_read(const char *path, unsigned char **out_data, int *out_size);
void wgr_fs_read_free(unsigned char *data);

/* Write a file (creating parent dirs); on web also keep it in the cache. */
bool wgr_fs_write(const char *path, const unsigned char *data, int size);

/* Web: is `path` in the cache, readable into the local store? (Desktop: never.) */
bool wgr_fs_is_cached(const char *path);

/* Read a cached file into the local store, asynchronously: begin returns an id (0
 * when it isn't cached); poll it each frame: 0 = still reading, 1 = now local,
 * -1 = failed (the file is dropped from the cache; fetch it instead). */
int wgr_fs_cache_read_begin(const char *path);
int wgr_fs_cache_read_poll(int id);

#endif // WGR_INTERNAL_FS_H
