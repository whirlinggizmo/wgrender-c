#include "internal/wgr_fs_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <direct.h> /* _mkdir: Windows' mkdir takes no mode */
#define mkdir(path, mode) _mkdir(path)
#endif

#include "internal/wgr_internal_internal.h"
#include "wgr_logger.h"

/* Local storage. Desktop: stdio relative to the working dir (root defaults to ""
 * so paths resolve as-is). Web: files are read and written in MEMFS under `root`
 * (default "/wgr"), and kept between visits in an IndexedDB store, one record per
 * file: init reads only the store's list of paths, a cached file is read into
 * MEMFS when it's needed (wgri_fs_cache_read_begin), and a write stores the file.
 * No network here — acquisition/fetch lives in wgr_asset. */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WGR_FS_DEFAULT_ROOT "/wgr"

/* Open the store and read its keys (full MEMFS paths). We can't await it (JSPI
 * can't suspend inside sokol's RAF-driven callbacks), so this is a polled barrier:
 * Module.wgr_fs_state is 0 pending / 1 ready / 2 no store (files still work in
 * MEMFS, nothing persists), surfaced via wgri_fs_is_ready(). */
/* Bump to invalidate every cached file on the next visit (see the note inside). */
#define WGR_FS_CACHE_EPOCH 2

EM_JS(void, wgr_fs_store_open, (const char *root_c, int epoch), {
    const root = UTF8ToString(root_c);
    Module.wgr_fs_state = 0;
    Module.wgr_fs_keys = new Set();
    Module.wgr_fs_reads = new Map();
    Module.wgr_fs_next_read = 1;
    const done = (state, err) => {
        Module.wgr_fs_state = state;
        performance.mark("wgr:fs-ready"); /* tools/webstart.mjs */
        if (err) console.warn("wgr_fs: no persistent cache", err);
    };
    try {
        FS.mkdirTree(root);
        const open = indexedDB.open("wgr_fs:" + root, 1);
        open.onupgradeneeded = () => open.result.createObjectStore("files");
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
                const keys = Module.wgr_fs_db.transaction("files").objectStore("files").getAllKeys();
                keys.onsuccess = () => {
                    for (const key of keys.result) Module.wgr_fs_keys.add(key);
                    done(1);
                };
                keys.onerror = () => done(2, keys.error);
            };
            const epochKey = "\u0000wgr_cache_epoch";
            const store = Module.wgr_fs_db.transaction("files").objectStore("files");
            const got = store.get(epochKey);
            got.onsuccess = () => {
                if (got.result === $0) {
                    listKeys();
                    return;
                }
                console.info("wgr_fs: cache from an older build, clearing it");
                const tx = Module.wgr_fs_db.transaction("files", "readwrite");
                const files = tx.objectStore("files");
                files.clear();
                files.put($0, epochKey);
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

/* Keep a file for later visits (asynchronous; a failure only means it isn't kept). */
EM_JS(void, wgr_fs_store_put, (const char *full_c, const unsigned char *data, int size), {
    if (!Module.wgr_fs_db) return;
    const full = UTF8ToString(full_c);
    /* a Blob: reading it back doesn't unpack the bytes on the main thread */
    const blob = new Blob([HEAPU8.slice(data, data + size)]);
    try {
        const tx = Module.wgr_fs_db.transaction("files", "readwrite");
        tx.objectStore("files").put(blob, full);
        tx.oncomplete = () => Module.wgr_fs_keys.add(full);
        tx.onabort = () => console.warn("wgr_fs: couldn't cache " + full, tx.error);
    } catch (e) {
        console.warn("wgr_fs: couldn't cache " + full, e);
    }
});

/* Forget one cached file, or all of them. A cached file can be wrong -- a host that
 * compresses served us gzip bytes under an asset's name until 2026-09-20 -- and
 * without this there is no way back: the bad copy is read in preference to the
 * network, for good. */
EM_JS(void, wgr_fs_store_delete, (const char *full_c), {
    const full = UTF8ToString(full_c);
    if (Module.wgr_fs_keys) Module.wgr_fs_keys.delete(full);
    if (!Module.wgr_fs_db) return;
    try {
        Module.wgr_fs_db.transaction("files", "readwrite").objectStore("files").delete(full);
    } catch (e) {
        console.warn("wgr_fs: couldn't forget " + full, e);
    }
});

EM_JS(void, wgr_fs_store_clear, (void), {
    if (Module.wgr_fs_keys) Module.wgr_fs_keys.clear();
    if (!Module.wgr_fs_db) return;
    try {
        Module.wgr_fs_db.transaction("files", "readwrite").objectStore("files").clear();
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
    char *cwd = getcwd(NULL, 0);
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

bool wgri_fs_remove(const char *path)
{
    char full[512];
    resolve(path, full, sizeof(full));
    if (full[0] == '\0') {
        return false;
    }
#ifdef __EMSCRIPTEN__
    wgr_fs_store_delete(full); /* the cache, so the next read goes to the network */
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
    char full[512];
    FILE *f;

    resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    mkdir_parents(full);
    f = fopen(full, "wb");
    if (f == NULL) return false;
    if (size > 0 && fwrite(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return false;
    }
    fclose(f);
#ifdef __EMSCRIPTEN__
    wgr_fs_store_put(full, data, size);
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
