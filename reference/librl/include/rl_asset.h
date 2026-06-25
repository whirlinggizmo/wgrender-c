#ifndef RL_ASSET_H
#define RL_ASSET_H

#include <stdbool.h>
#include <stddef.h>

#include "rl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rl_asset_callback_fn)(const char *path, void *user_data);

/* Asset host — base URL for relative paths in ensure */
int         rl_asset_set_host(const char *host);
const char *rl_asset_get_host(void);
float       rl_asset_ping_host(const char *host);

/* Ensure — make a file local under rl_fs root_dir, fetching if absent.
 *
 * If local_path already exists on disk, returns 0 immediately (no fetch).
 * Otherwise fetches from src. If src is NULL, resolves local_path against the
 * configured asset host. Returns an error if src is NULL and no host is set.
 *
 * The synchronous variant requires JSPI_EXPORTS on wasm (uses EM_ASYNC_JS
 * internally). The async variant returns a handle to poll. */
int         rl_asset_ensure(const char *local_path, const char *src);
rl_handle_t rl_asset_ensure_async(const char *local_path, const char *src);
bool        rl_asset_poll_task(rl_handle_t handle);
int         rl_asset_finish_task(rl_handle_t handle);
const char *rl_asset_get_task_path(rl_handle_t handle);
void        rl_asset_free_task(rl_handle_t handle);

/* Group async — ensure a set of files in parallel */
rl_handle_t rl_asset_ensure_many_async(const char *const *paths, size_t count);
rl_handle_t rl_asset_ensure_many_from_scratch_async(size_t count);

/* Managed queue — fire callbacks when tasks complete, pumped each frame */
typedef enum {
    RL_ASSET_ADD_TASK_OK             =  0,
    RL_ASSET_ADD_TASK_ERR_INVALID    = -1,
    RL_ASSET_ADD_TASK_ERR_QUEUE_FULL = -2,
} rl_asset_add_task_result_t;

rl_asset_add_task_result_t rl_asset_add_task(rl_handle_t handle,
                                             rl_asset_callback_fn on_success,
                                             rl_asset_callback_fn on_failure,
                                             void *user_data);
void rl_asset_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* RL_ASSET_H */
