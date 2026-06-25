#include "internal/sk_fs.h"

#include <stdio.h>
#include <string.h>

#include "internal/sk_internal.h"

/* Phase 2 (desktop seam): pure stdio-backed storage. Paths resolve as-is against
 * the working directory (the root_dir prefix + the web IDBFS mount/restore/flush
 * land in a later phase, gated under __EMSCRIPTEN__). On web today this still uses
 * the emscripten libc FS (MEMFS), which is empty until the fetch+cache path
 * exists — so asset examples won't find files on web until then. */

static char sk_fs_root[512];

void sk_fs_init(const char *root_dir)
{
    sk_fs_root[0] = '\0';
    if (root_dir != NULL) {
        size_t n = strlen(root_dir);
        if (n >= sizeof(sk_fs_root)) n = sizeof(sk_fs_root) - 1;
        memcpy(sk_fs_root, root_dir, n);
        sk_fs_root[n] = '\0';
    }
}

void sk_fs_deinit(void)
{
    sk_fs_root[0] = '\0';
}

bool sk_fs_is_ready(void)
{
    /* Desktop storage is always ready; the web restore barrier arrives later. */
    return true;
}

bool sk_fs_exists(const char *path)
{
    FILE *f = (path != NULL) ? fopen(path, "rb") : NULL;
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}
