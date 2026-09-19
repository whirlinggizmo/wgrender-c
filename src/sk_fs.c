#include "internal/sk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <direct.h> /* _mkdir: Windows' mkdir takes no mode */
#define mkdir(path, mode) _mkdir(path)
#endif

#include "internal/sk_internal.h"
#include "sk_logger.h"

/* Local storage. Desktop: stdio relative to the working dir (root defaults to ""
 * so paths resolve as-is). Web: files are read and written in MEMFS under `root`
 * (default "/sk"), and kept between visits in an IndexedDB store, one record per
 * file: init reads only the store's list of paths, a cached file is read into
 * MEMFS when it's needed (sk_fs_cache_read_begin), and a write stores the file.
 * No network here — acquisition/fetch lives in sk_asset. */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define SK_FS_DEFAULT_ROOT "/sk"

/* Open the store and read its keys (full MEMFS paths). We can't await it (JSPI
 * can't suspend inside sokol's RAF-driven callbacks), so this is a polled barrier:
 * Module.sk_fs_state is 0 pending / 1 ready / 2 no store (files still work in
 * MEMFS, nothing persists), surfaced via sk_fs_is_ready(). */
EM_JS(void, sk_fs_store_open, (const char *root_c), {
    const root = UTF8ToString(root_c);
    Module.sk_fs_state = 0;
    Module.sk_fs_keys = new Set();
    Module.sk_fs_reads = new Map();
    Module.sk_fs_next_read = 1;
    const done = (state, err) => {
        Module.sk_fs_state = state;
        performance.mark("sk:fs-ready"); /* tools/webstart.mjs */
        if (err) console.warn("sk_fs: no persistent cache", err);
    };
    try {
        FS.mkdirTree(root);
        const open = indexedDB.open("sk_fs:" + root, 1);
        open.onupgradeneeded = () => open.result.createObjectStore("files");
        open.onerror = () => done(2, open.error);
        open.onsuccess = () => {
            Module.sk_fs_db = open.result;
            const keys = Module.sk_fs_db.transaction("files").objectStore("files").getAllKeys();
            keys.onsuccess = () => {
                for (const key of keys.result) Module.sk_fs_keys.add(key);
                done(1);
            };
            keys.onerror = () => done(2, keys.error);
            /* the cache as IDBFS kept it, restored whole at every start (before 2026-09-20) */
            try { indexedDB.deleteDatabase(root); } catch (e) {}
        };
    } catch (e) {
        done(2, e);
    }
});

EM_JS(int, sk_fs_store_state, (void), { return Module.sk_fs_state | 0; });

EM_JS(int, sk_fs_store_has, (const char *full_c), {
    return Module.sk_fs_keys && Module.sk_fs_keys.has(UTF8ToString(full_c)) ? 1 : 0;
});

/* Read a cached file into MEMFS; returns an id for sk_fs_store_read_state. */
EM_JS(int, sk_fs_store_read, (const char *full_c), {
    const full = UTF8ToString(full_c);
    const id = Module.sk_fs_next_read++;
    const fail = (err) => {
        Module.sk_fs_keys.delete(full);
        Module.sk_fs_reads.set(id, 2);
        console.warn("sk_fs: couldn't read " + full + " from the cache", err);
    };
    Module.sk_fs_reads.set(id, 0);
    try {
        const get = Module.sk_fs_db.transaction("files").objectStore("files").get(full);
        get.onsuccess = () => {
            if (!(get.result instanceof Blob)) return fail("missing");
            get.result.arrayBuffer().then((buffer) => { /* read off the main thread */
                FS.mkdirTree(full.substring(0, full.lastIndexOf("/")) || "/");
                FS.writeFile(full, new Uint8Array(buffer), { canOwn: true }); /* no copy */
                Module.sk_fs_reads.set(id, 1);
            }).catch(fail);
        };
        get.onerror = () => fail(get.error);
    } catch (e) {
        fail(e);
    }
    return id;
});

/* 0 pending, 1 read (the id is done), 2 failed (the id is done). */
EM_JS(int, sk_fs_store_read_state, (int id), {
    const state = Module.sk_fs_reads.get(id) | 0;
    if (state !== 0) Module.sk_fs_reads.delete(id);
    return state;
});

/* Keep a file for later visits (asynchronous; a failure only means it isn't kept). */
EM_JS(void, sk_fs_store_put, (const char *full_c, const unsigned char *data, int size), {
    if (!Module.sk_fs_db) return;
    const full = UTF8ToString(full_c);
    /* a Blob: reading it back doesn't unpack the bytes on the main thread */
    const blob = new Blob([HEAPU8.slice(data, data + size)]);
    try {
        const tx = Module.sk_fs_db.transaction("files", "readwrite");
        tx.objectStore("files").put(blob, full);
        tx.oncomplete = () => Module.sk_fs_keys.add(full);
        tx.onabort = () => console.warn("sk_fs: couldn't cache " + full, tx.error);
    } catch (e) {
        console.warn("sk_fs: couldn't cache " + full, e);
    }
});
#else
#define SK_FS_DEFAULT_ROOT ""
#endif

static char sk_fs_root[256];

/* Join the configured root with `path` (absolute paths pass through). */
static void resolve(const char *path, char *out, size_t out_size)
{
    if (path == NULL) {
        out[0] = '\0';
    } else if (sk_fs_root[0] != '\0' && path[0] != '/') {
        snprintf(out, out_size, "%s/%s", sk_fs_root, path);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

/* Create each parent directory of `full` (best effort). */
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
void sk_fs_set_root(const char *root)
{
    size_t n;
    if (root == NULL) root = "";
    snprintf(sk_fs_root, sizeof(sk_fs_root), "%s", root);
    n = strlen(sk_fs_root);
    while (n > 1 && sk_fs_root[n - 1] == '/') sk_fs_root[--n] = '\0';
}

/* Build the directly-openable local path for `path` (root + path). */
void sk_fs_resolve(const char *path, char *out, size_t out_size)
{
    resolve(path, out, out_size);
}

void sk_fs_init(const char *root_dir)
{
    const char *root = (root_dir != NULL) ? root_dir : SK_FS_DEFAULT_ROOT;
    snprintf(sk_fs_root, sizeof(sk_fs_root), "%s", root);
#ifdef __EMSCRIPTEN__
    /* Open the cache (its list of files); sk_fs_is_ready() reflects it. */
    sk_fs_store_open(sk_fs_root);
    log_info("sk_fs: files in %s, kept in IndexedDB", sk_fs_root);
#else
    char *cwd = getcwd(NULL, 0);
    log_info("sk_fs: using stdio relative to working dir (absolute path=%s/%s)", cwd != NULL ? cwd : "?", sk_fs_root);
    free(cwd);
#endif
}

void sk_fs_deinit(void)
{
    sk_fs_root[0] = '\0';
}

bool sk_fs_is_ready(void)
{
#ifdef __EMSCRIPTEN__
    return sk_fs_store_state() != 0; /* 1 = opened, 2 = no cache (files still work) */
#else
    return true;
#endif
}

bool sk_fs_exists(const char *path)
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

bool sk_fs_read(const char *path, unsigned char **out_data, int *out_size)
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

void sk_fs_read_free(unsigned char *data)
{
    free(data);
}

bool sk_fs_write(const char *path, const unsigned char *data, int size)
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
    sk_fs_store_put(full, data, size);
#endif
    return true;
}

bool sk_fs_is_cached(const char *path)
{
#ifdef __EMSCRIPTEN__
    char full[512];
    resolve(path, full, sizeof(full));
    return full[0] != '\0' && sk_fs_store_has(full);
#else
    (void)path;
    return false;
#endif
}

int sk_fs_cache_read_begin(const char *path)
{
#ifdef __EMSCRIPTEN__
    char full[512];
    resolve(path, full, sizeof(full));
    return full[0] != '\0' && sk_fs_store_has(full) ? sk_fs_store_read(full) : 0;
#else
    (void)path;
    return 0;
#endif
}

int sk_fs_cache_read_poll(int id)
{
#ifdef __EMSCRIPTEN__
    const int state = id > 0 ? sk_fs_store_read_state(id) : 2;
    return state == 0 ? 0 : (state == 1 ? 1 : -1);
#else
    (void)id;
    return -1;
#endif
}
