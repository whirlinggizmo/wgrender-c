#include "sk_asset.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_fs.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with its path. Storage is delegated to sk_fs (sk_fs_exists / the web
 * idbfs store); host fetch for missing files lands in a later phase. The public
 * surface (ensure_async + add_task) is stable across both. */

#define MAX_ASSET_TASKS 64

typedef struct {
    char path[512];
    sk_asset_callback_fn on_success;
    sk_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via sk_asset_add_task */
} sk_asset_task_t;

static sk_asset_task_t sk_asset_tasks[MAX_ASSET_TASKS];
static sk_handle_pool_t sk_asset_pool;
static uint16_t sk_asset_free_indices[MAX_ASSET_TASKS];
static uint16_t sk_asset_generations[MAX_ASSET_TASKS];
static unsigned char sk_asset_occupied[MAX_ASSET_TASKS];
static bool sk_asset_ready = false;

static sk_asset_task_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_asset_pool, handle, &index)) {
        return NULL;
    }
    return &sk_asset_tasks[index];
}

SK_KEEP
sk_handle_t sk_asset_ensure_async(const char *path, const char *src)
{
    sk_handle_t handle;
    sk_asset_task_t *task_ptr;

    (void)src; /* host fetch is a later phase; desktop reads local files */
    if (!sk_asset_ready || path == NULL) {
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_asset_pool);
    if (handle == 0) {
        log_error("MAX_ASSET_TASKS reached (%d)", MAX_ASSET_TASKS);
        return 0;
    }
    task_ptr = resolve(handle);
    *task_ptr = (sk_asset_task_t){0};
    strncpy(task_ptr->path, path, sizeof(task_ptr->path) - 1);
    return handle;
}

SK_KEEP
sk_asset_add_task_result_t sk_asset_add_task(sk_handle_t handle,
                                             sk_asset_callback_fn on_success,
                                             sk_asset_callback_fn on_failure,
                                             void *user_data)
{
    sk_asset_task_t *task_ptr = resolve(handle);
    if (task_ptr == NULL) {
        return SK_ASSET_ADD_TASK_ERR_INVALID;
    }
    task_ptr->on_success = on_success;
    task_ptr->on_failure = on_failure;
    task_ptr->user_data = user_data;
    task_ptr->armed = true;
    return SK_ASSET_ADD_TASK_OK;
}

void sk_asset_init(void)
{
    memset(sk_asset_tasks, 0, sizeof(sk_asset_tasks));
    sk_handle_pool_init(&sk_asset_pool, SK_HANDLE_KIND_ASSET_TASK, MAX_ASSET_TASKS,
                        sk_asset_free_indices, MAX_ASSET_TASKS,
                        sk_asset_generations, sk_asset_occupied);
    sk_asset_ready = true;
}

void sk_asset_tick(void)
{
    /* Nothing can be ensured until storage is up (web: after the idbfs restore
     * barrier; desktop: immediately). Tasks stay queued until then. */
    if (!sk_asset_ready || !sk_fs_is_ready()) {
        return;
    }
    for (uint16_t i = 1; i < MAX_ASSET_TASKS; i++) {
        sk_asset_task_t *task = &sk_asset_tasks[i];
        sk_asset_callback_fn on_success, on_failure;
        void *user_data;
        char path[512];
        bool exists;

        if (!sk_asset_occupied[i] || !task->armed) {
            continue;
        }
        on_success = task->on_success;
        on_failure = task->on_failure;
        user_data = task->user_data;
        memcpy(path, task->path, sizeof(path));
        exists = sk_fs_exists(path);

        /* free the slot before firing — the callback may queue more work */
        {
            sk_handle_t handle = sk_handle_pool_handle_from_index(&sk_asset_pool, i);
            *task = (sk_asset_task_t){0};
            sk_handle_pool_free(&sk_asset_pool, handle);
        }

        if (exists) {
            if (on_success) on_success(path, user_data);
        } else {
            log_error("Asset not found: %s", path);
            if (on_failure) on_failure(path, user_data);
        }
    }
}

void sk_asset_deinit(void)
{
    sk_asset_ready = false;
    sk_handle_pool_reset(&sk_asset_pool);
}
