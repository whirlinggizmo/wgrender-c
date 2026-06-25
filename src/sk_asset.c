#include "sk_asset.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_internal.h"
#include "sk_logger.h"

#include "sokol_fetch.h"
#include "sokol_log.h"

/* Each request gets a fixed max buffer bound at dispatch (sokol_fetch's
 * canonical pattern when the size is unknown up front). Streaming / right-sized
 * buffers can come later. */
#define SK_ASSET_MAX_FILE_BYTES (24 * 1024 * 1024)

typedef struct {
    char path[512];
    unsigned char *buffer;
    sk_asset_loaded_fn on_loaded;
    sk_asset_failed_fn on_failed;
    void *user_data;
} sk_asset_task_t;

static bool sk_asset_ready = false;

static void on_response(const sfetch_response_t *res)
{
    sk_asset_task_t *task = *(sk_asset_task_t **)res->user_data;

    if (res->dispatched) {
        task->buffer = (unsigned char *)malloc(SK_ASSET_MAX_FILE_BYTES);
        sfetch_bind_buffer(res->handle,
                           (sfetch_range_t){.ptr = task->buffer, .size = SK_ASSET_MAX_FILE_BYTES});
    }
    if (res->fetched) {
        if (task->on_loaded) {
            task->on_loaded(task->path, (const unsigned char *)res->data.ptr,
                            (int)res->data.size, task->user_data);
        }
    }
    if (res->failed) {
        log_error("Asset load failed: %s (error %d)", task->path, (int)res->error_code);
        if (task->on_failed) {
            task->on_failed(task->path, task->user_data);
        }
    }
    if (res->finished) {
        free(task->buffer);
        free(task);
    }
}

SK_KEEP
bool sk_asset_load_async(const char *path,
                         sk_asset_loaded_fn on_loaded,
                         sk_asset_failed_fn on_failed,
                         void *user_data)
{
    sk_asset_task_t *task;

    if (!sk_asset_ready || path == NULL) {
        return false;
    }

    task = (sk_asset_task_t *)calloc(1, sizeof(*task));
    if (task == NULL) {
        return false;
    }
    strncpy(task->path, path, sizeof(task->path) - 1);
    task->on_loaded = on_loaded;
    task->on_failed = on_failed;
    task->user_data = user_data;

    sfetch_handle_t h = sfetch_send(&(sfetch_request_t){
        .path = task->path,
        .callback = on_response,
        .user_data = {.ptr = &task, .size = sizeof(task)},
    });
    if (!sfetch_handle_valid(h)) {
        free(task);
        return false;
    }
    return true;
}

void sk_asset_init(void)
{
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = 64,
        .num_channels = 1,
        .num_lanes = 4,
        .logger.func = slog_func,
    });
    sk_asset_ready = true;
}

void sk_asset_tick(void)
{
    if (sk_asset_ready) {
        sfetch_dowork();
    }
}

void sk_asset_deinit(void)
{
    if (sk_asset_ready) {
        sfetch_shutdown();
        sk_asset_ready = false;
    }
}
