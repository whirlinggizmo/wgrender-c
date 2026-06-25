#ifndef SK_INTERNAL_FS_H
#define SK_INTERNAL_FS_H

#include <stdbool.h>

#include "sk_types.h"

/* Local filesystem layer — pure storage, no network (acquisition/fetch lives in
 * sk_asset). Desktop: real files via the C stdio FS. Web (a later phase): IDBFS
 * mounted into MEMFS, with restore/flush sync barriers; sk_asset's ensure uses
 * these to land fetched files locally before the sync sk_*_create(path) reads
 * them. See docs/PLAN-sk_fs.md. */

void sk_fs_init(const char *root_dir);
void sk_fs_deinit(void);

/* True once the local store is usable. Desktop: always. Web (later): after the
 * IDBFS→MEMFS restore barrier clears. ensure must not read/fetch until ready. */
bool sk_fs_is_ready(void);

/* Is `path` present in the local store right now? */
bool sk_fs_exists(const char *path);

#endif // SK_INTERNAL_FS_H
