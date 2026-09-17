#include "internal/sk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "internal/sk_internal.h"
#include "sk_logger.h"

/* Local storage. Desktop: stdio relative to the working dir (root defaults to ""
 * so paths resolve as-is). Web: IDBFS mounted at `root` (default "/sk") and
 * restored into MEMFS at init so reads see persisted files; writes flush back.
 * No network here — acquisition/fetch lives in sk_asset. */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define SK_FS_DEFAULT_ROOT "/sk"

/* Mount IDBFS at `root` and kick a non-blocking IDBFS->MEMFS restore. We can't
 * await it (JSPI can't suspend inside sokol's RAF-driven callbacks — the export
 * isn't promising-wrapped), so this is a polled barrier: Module.sk_fs_restore is
 * 0 pending / 1 ready / 2 failed, surfaced via sk_fs_is_ready(). */
EM_JS(void, sk_fs_idbfs_begin, (const char *root_c), {
    const root = UTF8ToString(root_c);
    Module.sk_fs_restore = 0;
    try {
        FS.mkdirTree(root);
        FS.mount(IDBFS, {}, root);
        FS.syncfs(true, function (err) {
            Module.sk_fs_restore = err ? 2 : 1;
            if (err) console.error("sk_fs: idbfs restore failed", err);
        });
    } catch (e) {
        console.error("sk_fs: idbfs mount failed", e);
        Module.sk_fs_restore = 2;
    }
});

EM_JS(int, sk_fs_idbfs_state, (void), { return (Module.sk_fs_restore | 0); });

/* Persist MEMFS -> IDBFS (fire-and-forget; the browser writes it back async). */
EM_JS(void, sk_fs_idbfs_flush, (void), {
    FS.syncfs(false, function (err) { if (err) console.error("sk_fs: idbfs flush failed", err); });
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
    /* Mount idbfs + kick the async restore; sk_fs_is_ready() reflects it. */
    sk_fs_idbfs_begin(sk_fs_root);
    log_info("sk_fs: idbfs mounting at %s (restoring cache)", sk_fs_root);
#else
    char *cwd = getcwd(NULL, 0);
    log_info("sk_fs: using stdio relative to working dir (absolute path=%s/%s)", cwd != NULL ? cwd : "?", sk_fs_root);
    free(cwd);
#endif
}

void sk_fs_deinit(void)
{
    sk_fs_flush();
    sk_fs_root[0] = '\0';
}

bool sk_fs_is_ready(void)
{
#ifdef __EMSCRIPTEN__
    return sk_fs_idbfs_state() != 0; /* 1 = restored, 2 = failed (empty cache) */
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
    sk_fs_flush();
    return true;
}

void sk_fs_flush(void)
{
#ifdef __EMSCRIPTEN__
    if (sk_fs_root[0] != '\0') sk_fs_idbfs_flush();
#endif
}
