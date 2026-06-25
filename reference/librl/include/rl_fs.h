#ifndef RL_FS_H
#define RL_FS_H

#include <stdbool.h>
#include <stddef.h>

#include "rl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle — local jailed filesystem (IDBFS on wasm, real directory on desktop) */
int         rl_fs_init(const char *root_dir);
int         rl_fs_init_async(const char *root_dir);
void        rl_fs_deinit(void);
rl_handle_t rl_fs_deinit_async(void);
bool        rl_fs_is_initialized(void);
bool        rl_fs_is_ready(void);
int         rl_fs_flush(void);
rl_handle_t rl_fs_restore_async(void);
const char *rl_fs_get_root_dir(void);

/* File operations — all paths are jailed to root_dir */
bool rl_fs_exists(const char *path);
int  rl_fs_read(const char *path, unsigned char **out_data, size_t *out_size);
void rl_fs_read_free(unsigned char *data);
int  rl_fs_write(const char *path, const unsigned char *data, size_t size);
int  rl_fs_remove(const char *path);
int  rl_fs_mkdir(const char *path);
int  rl_fs_rmdir(const char *path);
int  rl_fs_clear(void);
void rl_fs_normalize_path(const char *path, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* RL_FS_H */
