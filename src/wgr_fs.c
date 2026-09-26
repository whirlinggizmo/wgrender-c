#include "internal/wgr_fs_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#if defined(_WIN32)
#include <direct.h> /* _mkdir: Windows' mkdir takes no mode; _getcwd for getcwd */
#define mkdir(path, mode) _mkdir(path)
#endif

#include "internal/wgr_internal_internal.h"
#include "wgr_logger.h"

/* Local storage. Desktop: stdio relative to the working dir (root defaults to ""
 * so paths resolve as-is). Web: files are read and written in MEMFS under `root`
 * (default "/wgr"), and kept between visits in an IndexedDB store, one record per
 * file: init reads only the store's list of paths, a cached file is read into
 * MEMFS when it's needed (wgri_fs_cache_read_begin), and a write stores the file.
 * Each file's metadata, if it has any, is in a second store ("meta", same key), all
 * of which init reads with the list. Desktop keeps it in sidecars under ".meta/".
 * No network here — acquisition/fetch lives in wgr_asset. */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WGR_FS_DEFAULT_ROOT "/wgr"

/* Open the store and read its keys (full MEMFS paths). We can't await it (JSPI
 * can't suspend inside sokol's RAF-driven callbacks), so this is a polled barrier:
 * Module.wgr_fs_state is 0 pending / 1 ready / 2 no store (files still work in
 * MEMFS, nothing persists), surfaced via wgri_fs_is_ready(). */
/* Bump to invalidate every cached file on the next visit (see the note inside). */
#define WGR_FS_CACHE_EPOCH 3

EM_JS(void, wgr_fs_store_open, (const char *root_c, int epoch), {
    const root = UTF8ToString(root_c);
    Module.wgr_fs_state = 0;
    Module.wgr_fs_keys = new Set();
    Module.wgr_fs_meta = new Map();    /* full path -> the stored file's metadata */
    Module.wgr_fs_pending = new Map(); /* full path -> its last store write in flight */
    Module.wgr_fs_ops = 0;
    Module.wgr_fs_cleared = 0;         /* writes up to this op were cleared away */
    Module.wgr_fs_reads = new Map();
    Module.wgr_fs_next_read = 1;
    const done = (state, err) => {
        Module.wgr_fs_state = state;
        performance.mark("wgr:fs-ready"); /* tools/webstart.py */
        if (err) console.warn("wgr_fs: no persistent cache", err);
    };
    try {
        FS.mkdirTree(root);
        /* version 2 added "meta"; a version 1 cache keeps its files, which have no
           metadata and so are fetched once more, unconditionally */
        const open = indexedDB.open("wgr_fs:" + root, 2);
        open.onupgradeneeded = () => {
            const db = open.result;
            if (!db.objectStoreNames.contains("files")) db.createObjectStore("files");
            if (!db.objectStoreNames.contains("meta")) db.createObjectStore("meta");
        };
        open.onerror = () => done(2, open.error);
        open.onsuccess = () => {
            Module.wgr_fs_db = open.result;
            /* A cached file can be wrong in a way nothing notices: until 2026-09-20 a
               compressing host's gzip bytes were stored under an asset's name, and a
               font made of those is accepted by fontstash and then exhausts its scratch
               buffer rather than failing. A file that fails loudly is re-fetched; this
               is for the rest. Bump WGR_FS_CACHE_EPOCH to throw every cached file away
               once, on the next visit, for everyone. */
            const listKeys = () => {
                /* the metadata too, which is small: a check of it has to answer now */
                const tx = Module.wgr_fs_db.transaction(["files", "meta"]);
                const keys = tx.objectStore("files").getAllKeys();
                const metaKeys = tx.objectStore("meta").getAllKeys();
                const metas = tx.objectStore("meta").getAll(); /* in the same key order */
                tx.oncomplete = () => {
                    for (const key of keys.result) Module.wgr_fs_keys.add(key);
                    metaKeys.result.forEach((key, i) => {
                        if (Module.wgr_fs_keys.has(key)) Module.wgr_fs_meta.set(key, metas.result[i]);
                    });
                    done(1);
                };
                tx.onabort = () => done(2, tx.error);
            };
            const epochKey = "\u0000wgr_cache_epoch";
            const store = Module.wgr_fs_db.transaction("files").objectStore("files");
            const got = store.get(epochKey);
            got.onsuccess = () => {
                if (got.result === epoch) {
                    listKeys();
                    return;
                }
                console.info("wgr_fs: cache from an older build, clearing it");
                const tx = Module.wgr_fs_db.transaction(["files", "meta"], "readwrite");
                const files = tx.objectStore("files");
                files.clear();
                tx.objectStore("meta").clear();
                files.put(epoch, epochKey);
                tx.oncomplete = () => listKeys();
                tx.onabort = () => done(2, tx.error);
            };
            got.onerror = () => listKeys(); /* can't tell: keep what is there */
            /* the cache as IDBFS kept it, restored whole at every start (before
               2026-09-20), under the mount point of the day: "/sk" until the library
               was renamed, and whatever this build mounts now */
            try { indexedDB.deleteDatabase(root); } catch (e) {}
            try { indexedDB.deleteDatabase("/sk"); } catch (e) {}
        };
    } catch (e) {
        done(2, e);
    }
});

EM_JS(int, wgr_fs_store_state, (void), { return Module.wgr_fs_state | 0; });

EM_JS(int, wgr_fs_store_has, (const char *full_c), {
    return Module.wgr_fs_keys && Module.wgr_fs_keys.has(UTF8ToString(full_c)) ? 1 : 0;
});

/* Read a cached file into MEMFS; returns an id for wgr_fs_store_read_state. */
EM_JS(int, wgr_fs_store_read, (const char *full_c), {
    const full = UTF8ToString(full_c);
    const id = Module.wgr_fs_next_read++;
    const fail = (err) => {
        Module.wgr_fs_keys.delete(full);
        Module.wgr_fs_reads.set(id, 2);
        console.warn("wgr_fs: couldn't read " + full + " from the cache", err);
    };
    Module.wgr_fs_reads.set(id, 0);
    try {
        const get = Module.wgr_fs_db.transaction("files").objectStore("files").get(full);
        get.onsuccess = () => {
            if (!(get.result instanceof Blob)) return fail("missing");
            get.result.arrayBuffer().then((buffer) => { /* read off the main thread */
                FS.mkdirTree(full.substring(0, full.lastIndexOf("/")) || "/");
                FS.writeFile(full, new Uint8Array(buffer), { canOwn: true }); /* no copy */
                Module.wgr_fs_reads.set(id, 1);
            }).catch(fail);
        };
        get.onerror = () => fail(get.error);
    } catch (e) {
        fail(e);
    }
    return id;
});

/* 0 pending, 1 read (the id is done), 2 failed (the id is done). */
EM_JS(int, wgr_fs_store_read_state, (int id), {
    const state = Module.wgr_fs_reads.get(id) | 0;
    if (state !== 0) Module.wgr_fs_reads.delete(id);
    return state;
});

/* Keep a file for later visits, with its metadata or none (asynchronous; a failure
 * only means it isn't kept). File and metadata are one transaction, so the store
 * never pairs new bytes with old validators, or old bytes with new ones. The
 * in-memory lists follow when it commits, unless a later write, delete or clear of
 * the same path has overtaken it. */
EM_JS(void, wgr_fs_store_put, (const char *full_c, const unsigned char *data, int size, int has_meta,
                               const char *etag_c, const char *modified_c, double fresh_until, const char *hash_c), {
    if (!Module.wgr_fs_db) return;
    const full = UTF8ToString(full_c);
    const meta = has_meta ? {
        etag: UTF8ToString(etag_c),
        lastModified: UTF8ToString(modified_c),
        freshUntil: fresh_until,
        hash: UTF8ToString(hash_c),
    } : null;
    /* a Blob: reading it back doesn't unpack the bytes on the main thread */
    const blob = new Blob([HEAPU8.slice(data, data + size)]);
    const op = ++Module.wgr_fs_ops;
    Module.wgr_fs_pending.set(full, op);
    const settle = () => {
        const current = Module.wgr_fs_pending.get(full) === op;
        if (current) Module.wgr_fs_pending.delete(full);
        return current && op > Module.wgr_fs_cleared;
    };
    try {
        const tx = Module.wgr_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").put(blob, full);
        if (meta) tx.objectStore("meta").put(meta, full);
        else tx.objectStore("meta").delete(full);
        tx.oncomplete = () => {
            if (!settle()) return;
            Module.wgr_fs_keys.add(full);
            if (meta) Module.wgr_fs_meta.set(full, meta);
            else Module.wgr_fs_meta.delete(full);
        };
        tx.onabort = () => {
            settle();
            console.warn("wgr_fs: couldn't cache " + full, tx.error);
        };
    } catch (e) {
        settle();
        console.warn("wgr_fs: couldn't cache " + full, e);
    }
});

/* Copy a cached file's metadata into C's buffers; 0 when it has none. A value that
 * doesn't fit is left empty, not cut (wgri_fs_meta_t). */
EM_JS(int, wgr_fs_store_meta_get, (const char *full_c, char *etag, int etag_size, char *modified,
                                   int modified_size, char *hash, int hash_size, double *fresh_until), {
    const meta = Module.wgr_fs_meta && Module.wgr_fs_meta.get(UTF8ToString(full_c));
    if (!meta) return 0;
    const copy = (text, out, out_size) => {
        text = typeof text === "string" ? text : "";
        stringToUTF8(lengthBytesUTF8(text) < out_size ? text : "", out, out_size);
    };
    copy(meta.etag, etag, etag_size);
    copy(meta.lastModified, modified, modified_size);
    copy(meta.hash, hash, hash_size);
    HEAPF64[fresh_until >> 3] = +meta.freshUntil || 0;
    return 1;
});

/* Replace a cached file's metadata; 0 when the file isn't in the store, or a write of
 * it is still in flight (the write brings its own). */
EM_JS(int, wgr_fs_store_meta_set, (const char *full_c, const char *etag_c, const char *modified_c,
                                   double fresh_until, const char *hash_c), {
    const full = UTF8ToString(full_c);
    if (!Module.wgr_fs_db || !Module.wgr_fs_keys.has(full) || Module.wgr_fs_pending.has(full)) return 0;
    const meta = {
        etag: UTF8ToString(etag_c),
        lastModified: UTF8ToString(modified_c),
        freshUntil: fresh_until,
        hash: UTF8ToString(hash_c),
    };
    /* at once: it describes the same bytes as before, so a failure to keep it leaves
       the store's older metadata, which is still true of them */
    Module.wgr_fs_meta.set(full, meta);
    try {
        const tx = Module.wgr_fs_db.transaction("meta", "readwrite");
        tx.objectStore("meta").put(meta, full);
        tx.onabort = () => console.warn("wgr_fs: couldn't keep the metadata of " + full, tx.error);
    } catch (e) {
        console.warn("wgr_fs: couldn't keep the metadata of " + full, e);
    }
    return 1;
});

/* Forget one cached file, or all of them. A cached file can be wrong -- a host that
 * compresses served us gzip bytes under an asset's name until 2026-09-20 -- and
 * without this there is no way back: the bad copy is read in preference to the
 * network, for good. */
EM_JS(void, wgr_fs_store_delete, (const char *full_c), {
    const full = UTF8ToString(full_c);
    if (Module.wgr_fs_keys) Module.wgr_fs_keys.delete(full);
    if (Module.wgr_fs_meta) Module.wgr_fs_meta.delete(full);
    if (!Module.wgr_fs_db) return;
    const op = ++Module.wgr_fs_ops; /* overtakes a write still in flight */
    Module.wgr_fs_pending.set(full, op);
    const settle = () => {
        if (Module.wgr_fs_pending.get(full) === op) Module.wgr_fs_pending.delete(full);
    };
    try {
        const tx = Module.wgr_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").delete(full);
        tx.objectStore("meta").delete(full);
        tx.oncomplete = settle;
        tx.onabort = settle;
    } catch (e) {
        settle();
        console.warn("wgr_fs: couldn't forget " + full, e);
    }
});

EM_JS(void, wgr_fs_store_clear, (void), {
    if (Module.wgr_fs_keys) Module.wgr_fs_keys.clear();
    if (Module.wgr_fs_meta) Module.wgr_fs_meta.clear();
    if (!Module.wgr_fs_db) return;
    Module.wgr_fs_cleared = Module.wgr_fs_ops; /* writes in flight land before the clear */
    try {
        const tx = Module.wgr_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").clear();
        tx.objectStore("meta").clear();
    } catch (e) {
        console.warn("wgr_fs: couldn't clear the cache", e);
    }
});
#else
#define WGR_FS_DEFAULT_ROOT ""
#endif

static char wgr_fs_root[256];

/* Join the configured root with `path` (absolute paths pass through). */
static void resolve(const char *path, char *out, size_t out_size)
{
    if (path == NULL) {
        out[0] = '\0';
    } else if (wgr_fs_root[0] != '\0' && path[0] != '/') {
        snprintf(out, out_size, "%s/%s", wgr_fs_root, path);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

/* Create each parent directory of `full` (best effort). */
static void mkdir_parents(const char *full);

void wgri_fs_make_parents(const char *path)
{
    char full[1024];
    wgri_fs_resolve(path, full, sizeof(full));
    mkdir_parents(full);
}

static void mkdir_parents(const char *full)
{
    char tmp[512];
    size_t i;
    snprintf(tmp, sizeof(tmp), "%s", full);
    for (i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
}

/* Override the local root (base dir reads/writes resolve against). Trailing
 * slashes are trimmed so resolve()'s "%s/%s" join stays clean. */
void wgri_fs_set_root(const char *root)
{
    size_t n;
    if (root == NULL) root = "";
    snprintf(wgr_fs_root, sizeof(wgr_fs_root), "%s", root);
    n = strlen(wgr_fs_root);
    while (n > 1 && wgr_fs_root[n - 1] == '/') wgr_fs_root[--n] = '\0';
}

/* Build the directly-openable local path for `path` (root + path). */
void wgri_fs_resolve(const char *path, char *out, size_t out_size)
{
    resolve(path, out, out_size);
}

void wgri_fs_init(const char *root_dir)
{
    const char *root = (root_dir != NULL) ? root_dir : WGR_FS_DEFAULT_ROOT;
    snprintf(wgr_fs_root, sizeof(wgr_fs_root), "%s", root);
#ifdef __EMSCRIPTEN__
    /* Open the cache (its list of files); wgri_fs_is_ready() reflects it. */
    wgr_fs_store_open(wgr_fs_root, WGR_FS_CACHE_EPOCH);
    log_info("wgr_fs: files in %s, kept in IndexedDB", wgr_fs_root);
#else
#if defined(_WIN32)
    char *cwd = _getcwd(NULL, 0); /* same NULL, 0 -> malloc contract as POSIX */
#else
    char *cwd = getcwd(NULL, 0);
#endif
    log_info("wgr_fs: using stdio relative to working dir (absolute path=%s/%s)", cwd != NULL ? cwd : "?", wgr_fs_root);
    free(cwd);
#endif
}

void wgri_fs_deinit(void)
{
    wgr_fs_root[0] = '\0';
}

bool wgri_fs_is_ready(void)
{
#ifdef __EMSCRIPTEN__
    return wgr_fs_store_state() != 0; /* 1 = opened, 2 = no cache (files still work) */
#else
    return true;
#endif
}

bool wgri_fs_exists(const char *path)
{
    char full[512];
    FILE *f;
    resolve(path, full, sizeof(full));
    f = (full[0] != '\0') ? fopen(full, "rb") : NULL;
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

bool wgri_fs_read(const char *path, unsigned char **out_data, int *out_size)
{
    char full[512];
    FILE *f;
    long size;
    unsigned char *bytes;

    if (out_data == NULL || out_size == NULL) return false;
    *out_data = NULL;
    *out_size = 0;

    resolve(path, full, sizeof(full));
    f = (full[0] != '\0') ? fopen(full, "rb") : NULL;
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    bytes = (unsigned char *)malloc((size_t)size + 1);
    if (bytes == NULL) { fclose(f); return false; }
    if (size > 0 && fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return false;
    }
    fclose(f);
    bytes[size] = '\0';
    *out_data = bytes;
    *out_size = (int)size;
    return true;
}

void wgri_fs_read_free(unsigned char *data)
{
    free(data);
}

#ifndef __EMSCRIPTEN__
/* Desktop metadata: a sidecar under the root's ".meta/", mirroring the file's path.
 * Never beside the file: "foo.png.meta" could be an asset's own name. Only relative
 * paths have one (an absolute path isn't the cache's). */
#define WGR_FS_META_DIR ".meta"

static bool meta_path(const char *path, char *out, size_t out_size)
{
    char rel[512];
    if (path == NULL || path[0] == '\0' || path[0] == '/') return false;
    if ((size_t)snprintf(rel, sizeof(rel), WGR_FS_META_DIR "/%s", path) >= sizeof(rel)) return false;
    resolve(rel, out, out_size);
    return true;
}

static void meta_remove(const char *path)
{
    char side[512];
    if (meta_path(path, side, sizeof(side))) remove(side);
}

/* One "key value" line each; a value is one header's worth, so never a newline. */
static bool meta_write(const char *path, const wgri_fs_meta_t *meta)
{
    char side[512];
    FILE *f;
    bool ok;
    if (!meta_path(path, side, sizeof(side))) return false;
    mkdir_parents(side);
    f = fopen(side, "wb");
    if (f == NULL) return false;
    ok = fprintf(f, "wgr_meta 1\netag %s\nlast-modified %s\nfresh-until %.17g\nhash %s\n", meta->etag,
                 meta->last_modified, meta->fresh_until, meta->hash) > 0;
    return (fclose(f) == 0) && ok;
}

/* Copy a sidecar value into a field; too long for it and it stays empty (never cut). */
static void meta_field(char *out, size_t out_size, const char *value)
{
    if (strlen(value) < out_size) snprintf(out, out_size, "%s", value);
}

static bool meta_read(const char *path, wgri_fs_meta_t *out)
{
    char side[512];
    char line[512];
    FILE *f;
    bool versioned = false;
    if (!meta_path(path, side, sizeof(side))) return false;
    f = fopen(side, "rb");
    if (f == NULL) return false;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') { /* a line longer than any field: skip the rest */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        line[--n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        if (strcmp(line, "wgr_meta 1") == 0) versioned = true;
        else if (strncmp(line, "etag ", 5) == 0) meta_field(out->etag, sizeof(out->etag), line + 5);
        else if (strncmp(line, "last-modified ", 14) == 0) meta_field(out->last_modified, sizeof(out->last_modified), line + 14);
        else if (strncmp(line, "fresh-until ", 12) == 0) out->fresh_until = strtod(line + 12, NULL);
        else if (strncmp(line, "hash ", 5) == 0) meta_field(out->hash, sizeof(out->hash), line + 5);
    }
    fclose(f);
    return versioned;
}
#endif

bool wgri_fs_meta_get(const char *path, wgri_fs_meta_t *out)
{
    char full[512];
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
#ifdef __EMSCRIPTEN__
    return wgr_fs_store_meta_get(full, out->etag, (int)sizeof(out->etag), out->last_modified,
                                 (int)sizeof(out->last_modified), out->hash, (int)sizeof(out->hash),
                                 &out->fresh_until) != 0;
#else
    if (!wgri_fs_exists(path) || !meta_read(path, out)) {
        memset(out, 0, sizeof(*out)); /* a sidecar alone, or a broken one, is none */
        return false;
    }
    return true;
#endif
}

bool wgri_fs_meta_set(const char *path, const wgri_fs_meta_t *meta)
{
    char full[512];
    if (meta == NULL) return false;
    resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
#ifdef __EMSCRIPTEN__
    return wgr_fs_store_meta_set(full, meta->etag, meta->last_modified, meta->fresh_until, meta->hash) != 0;
#else
    return wgri_fs_exists(path) && meta_write(path, meta);
#endif
}

bool wgri_fs_remove(const char *path)
{
    char full[512];
    resolve(path, full, sizeof(full));
    if (full[0] == '\0') {
        return false;
    }
#ifdef __EMSCRIPTEN__
    wgr_fs_store_delete(full); /* the cache, so the next read goes to the network */
#else
    meta_remove(path);
#endif
    return remove(full) == 0;
}

void wgri_fs_clear(void)
{
#ifdef __EMSCRIPTEN__
    wgr_fs_store_clear();
#endif
    /* the files themselves stay: on web MEMFS goes away with the page, and on desktop
       a cache directory is the program's to manage (wgr_asset_set_cache_dir) */
}

bool wgri_fs_write(const char *path, const unsigned char *data, int size)
{
    return wgri_fs_write_meta(path, data, size, NULL);
}

bool wgri_fs_write_meta(const char *path, const unsigned char *data, int size, const wgri_fs_meta_t *meta)
{
    char full[512];
    FILE *f;

    resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
#ifndef __EMSCRIPTEN__
    meta_remove(path); /* before the bytes change: it describes the old ones */
#endif
    mkdir_parents(full);
    f = fopen(full, "wb");
    if (f == NULL) return false;
    if (size > 0 && fwrite(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return false;
    }
    fclose(f);
#ifdef __EMSCRIPTEN__
    wgr_fs_store_put(full, data, size, meta != NULL, meta != NULL ? meta->etag : "",
                     meta != NULL ? meta->last_modified : "", meta != NULL ? meta->fresh_until : 0.0,
                     meta != NULL ? meta->hash : "");
#else
    if (meta != NULL && !meta_write(path, meta)) {
        /* the bytes landed; without their metadata they are only fetched once more */
        log_warn("wgr_fs: couldn't keep the metadata of %s", path);
    }
#endif
    return true;
}

bool wgri_fs_is_cached(const char *path)
{
#ifdef __EMSCRIPTEN__
    char full[512];
    resolve(path, full, sizeof(full));
    return full[0] != '\0' && wgr_fs_store_has(full);
#else
    (void)path;
    return false;
#endif
}

int wgri_fs_cache_read_begin(const char *path)
{
#ifdef __EMSCRIPTEN__
    char full[512];
    resolve(path, full, sizeof(full));
    return full[0] != '\0' && wgr_fs_store_has(full) ? wgr_fs_store_read(full) : 0;
#else
    (void)path;
    return 0;
#endif
}

int wgri_fs_cache_read_poll(int id)
{
#ifdef __EMSCRIPTEN__
    const int state = id > 0 ? wgr_fs_store_read_state(id) : 2;
    return state == 0 ? 0 : (state == 1 ? 1 : -1);
#else
    (void)id;
    return -1;
#endif
}
