#ifndef WGRI_INTERNAL_FS_H
#define WGRI_INTERNAL_FS_H

#include <stdbool.h>
#include <stddef.h>

#include "wgr_types.h"

/* Local filesystem layer — pure storage, no network (acquisition/fetch lives in
 * wgr_asset). Desktop: real files via the C stdio FS, relative to the working dir.
 * Web: files in MEMFS under a root dir, kept between visits in IndexedDB, one
 * record per file. Init reads only the cache's list of files; a cached file is
 * read into MEMFS when it's needed (wgri_fs_cache_read_begin), so startup doesn't
 * grow with the cache (docs/PLAN-wgr_fs.md). A cached file can carry metadata
 * (wgri_fs_meta_t): on the web in a second IndexedDB store, read at init with the
 * list; on desktop in a sidecar under the root's ".meta/". */

void wgri_fs_init(const char *root_dir); /* root_dir NULL -> platform default */
void wgri_fs_deinit(void);

/* Override the local root (base dir reads/writes resolve against). */
void wgri_fs_set_root(const char *root);

/* Desktop: a path starting with WGRI_FS_CACHE names a file under the cache root
 * rather than the root -- a download kept apart from a local host, which is only ever
 * read. No key has a ":", so the prefix can't be one's own. The cache root is the
 * root too while the host is a URL; unused on the web, whose root is the cache. */
#define WGRI_FS_CACHE "cache:"
void wgri_fs_set_cache_root(const char *root);

/* Build the directly-openable local path for `path` (root + path). */
void wgri_fs_resolve(const char *path, char *out, size_t out_size);

/* True once the local store is usable. Desktop: always. Web: once the cache's list
 * of files is read. ensure must not read/fetch until ready. */
bool wgri_fs_is_ready(void);

/* Is `path` local (openable) right now? */
bool wgri_fs_exists(const char *path);

/* Read an entire file into a malloc'd buffer (NUL-terminated for convenience;
 * `*out_size` excludes the terminator). Caller frees with wgri_fs_read_free. */
bool wgri_fs_read(const char *path, unsigned char **out_data, int *out_size);
void wgri_fs_read_free(unsigned char *data);

/* What is known about where a cached file came from, kept beside it: the response's
 * validators, how long it may be used without asking, and the hash of the bytes
 * stored (docs/PLAN-asset-cache.md). An empty string is "none". A value too long for
 * its field is dropped, never cut: a cut ETag would ask the server about some other
 * version. */
typedef struct {
    char etag[128];
    char last_modified[64];
    double fresh_until; /* seconds since 1970 (wall clock); 0 = never fresh */
    char hash[72];      /* "sha256:" + 64 hex */
    char source[1024];  /* desktop: the URL a download came from ("" = the host's own) */
} wgri_fs_meta_t;

/* Write a file (creating parent dirs); on web also keep it in the cache. Any
 * metadata the old file had is dropped: it described other bytes. */
bool wgri_fs_write(const char *path, const unsigned char *data, int size);

/* The same, with the new bytes' metadata kept with them: on the web in one
 * transaction, so a failed store never leaves one without the other; on desktop the
 * old metadata goes before the file is written and the new comes after, so an
 * interruption leaves none rather than the wrong one. */
bool wgri_fs_write_meta(const char *path, const unsigned char *data, int size, const wgri_fs_meta_t *meta);

/* A cached file's metadata: false (and `out` zeroed) when it has none. Web: of the
 * copy in the cache; desktop: of the file under the root. */
bool wgri_fs_meta_get(const char *path, wgri_fs_meta_t *out);

/* Replace a cached file's metadata, its bytes staying as they are (a 304's new
 * freshness). False when there is no such file (web: none in the cache). */
bool wgri_fs_meta_set(const char *path, const wgri_fs_meta_t *meta);

/* Forget a cached file and its metadata (web: the IndexedDB entry too), so the next
 * read fetches it again. wgri_fs_clear forgets the whole cache: on the web the store
 * and this visit's copies (MEMFS under the root); on desktop nothing (a directory's
 * files are not all the cache's: wgr_asset_clear_cache deletes its own downloads). */
bool wgri_fs_remove(const char *path);

/* Where a download of `path` is written until it is whole: under the same root's
 * ".part/", as metadata is under ".meta/" -- never beside the file, where
 * "foo.png.part" could be an asset's own name. False when it doesn't fit. Desktop. */
bool wgri_fs_partial_path(const char *path, char *out, size_t out_size);

/* Move `from` over `to` in one step, replacing what is there: a finished download
 * taking its place, so nothing reads half a file. Desktop. */
bool wgri_fs_replace(const char *from, const char *to);
void wgri_fs_clear(void);

/* Create the directories above `path` (desktop), so something outside libwgrender --
 * an asset fetcher writing a download -- can open it for writing. */
void wgri_fs_make_parents(const char *path);

/* Web: keep written files between visits (the default). Off, writes stay in MEMFS
 * for this visit and the cache is left as it is: nothing is added, and nothing in it
 * counts as cached (wgri_fs_is_cached). Desktop: files are files; ignored. */
void wgri_fs_set_persistent(bool persistent);

/* Web: is `path` in the cache, readable into the local store? (Desktop: never.) */
bool wgri_fs_is_cached(const char *path);

/* Read a cached file into the local store, asynchronously: begin returns an id (0
 * when it isn't cached); poll it each frame: 0 = still reading, 1 = now local,
 * -1 = failed (the file is dropped from the cache; fetch it instead). */
int wgri_fs_cache_read_begin(const char *path);
int wgri_fs_cache_read_poll(int id);

#endif // WGRI_INTERNAL_FS_H
