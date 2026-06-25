#include "sk_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_fs.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#ifdef __EMSCRIPTEN__
#include "sokol_fetch.h"
/* Whole-file fetch into one preallocated buffer (no chunk streaming, so no HTTP
 * Range — our dev server doesn't serve partial content). Files larger than this
 * fail with SFETCH_ERROR_BUFFER_TOO_SMALL; bump if an asset outgrows it. */
#define ASSET_FETCH_MAX_BYTES (16 * 1024 * 1024)
#endif

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with a directly-openable local path. Storage is delegated to sk_fs.
 *
 * Desktop: the host is a local base dir (set as the sk_fs root); a missing file
 * is a failure. Web: the host is a fetch origin — a cache miss downloads the
 * asset via sokol_fetch, writes it into the idbfs-backed store, then resolves.
 * Either way the callback receives a path the sync sk_*_create(path) creators
 * can fopen. */

#define MAX_ASSET_TASKS 64

enum { TASK_NEW = 0, TASK_FETCHING };
enum { FETCH_PENDING = 0, FETCH_OK, FETCH_FAILED };

typedef struct {
    char path[512];
    sk_asset_callback_fn on_success;
    sk_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via sk_asset_add_task */
    int state;
    unsigned char *fetch_buf; /* web: buffer bound to the in-flight fetch */
    int fetch_result;         /* web: FETCH_* set by the sokol_fetch callback */
} sk_asset_task_t;

static sk_asset_task_t sk_asset_tasks[MAX_ASSET_TASKS];
static sk_handle_pool_t sk_asset_pool;
static uint16_t sk_asset_free_indices[MAX_ASSET_TASKS];
static uint16_t sk_asset_generations[MAX_ASSET_TASKS];
static unsigned char sk_asset_occupied[MAX_ASSET_TASKS];
static bool sk_asset_ready = false;
static char sk_asset_host[256] = "";

static sk_asset_task_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_asset_pool, handle, &index)) {
        return NULL;
    }
    return &sk_asset_tasks[index];
}

void sk_asset_set_host(const char *host)
{
    size_t n;
    if (host == NULL) host = "";
    snprintf(sk_asset_host, sizeof(sk_asset_host), "%s", host);
    n = strlen(sk_asset_host);
    while (n > 1 && sk_asset_host[n - 1] == '/') sk_asset_host[--n] = '\0';
#ifndef __EMSCRIPTEN__
    /* Desktop: the asset base IS the local root reads resolve against. */
    sk_fs_set_root(sk_asset_host);
#endif
}

#ifdef __EMSCRIPTEN__
/* sokol_fetch delivers bytes on the main thread when sfetch_dowork() (called in
 * sk_asset_tick) pumps it. On finish we hand the whole file to sk_fs (which
 * writes + flushes to idbfs) and flag the slot; tick then resolves the task. */
static void on_fetch(const sfetch_response_t *r)
{
    uint16_t slot = *(const uint16_t *)r->user_data;
    sk_asset_task_t *task = &sk_asset_tasks[slot];
    if (!r->finished) {
        return;
    }
    if (!r->failed && sk_fs_write(task->path, (const unsigned char *)r->data.ptr,
                                  (int)r->data.size)) {
        task->fetch_result = FETCH_OK;
    } else {
        task->fetch_result = FETCH_FAILED;
    }
    free(task->fetch_buf);
    task->fetch_buf = NULL;
}

static void start_fetch(uint16_t slot)
{
    sk_asset_task_t *task = &sk_asset_tasks[slot];
    char url[768];
    sfetch_handle_t h;

    task->state = TASK_FETCHING;
    task->fetch_result = FETCH_PENDING;
    task->fetch_buf = (unsigned char *)malloc(ASSET_FETCH_MAX_BYTES);
    if (task->fetch_buf == NULL) {
        task->fetch_result = FETCH_FAILED;
        return;
    }
    snprintf(url, sizeof(url), "%s/%s", sk_asset_host, task->path);
    h = sfetch_send(&(sfetch_request_t){
        .path = url,
        .callback = on_fetch,
        .buffer = { .ptr = task->fetch_buf, .size = ASSET_FETCH_MAX_BYTES },
        .user_data = { .ptr = &slot, .size = sizeof(slot) },
    });
    if (!sfetch_handle_valid(h)) {
        free(task->fetch_buf);
        task->fetch_buf = NULL;
        task->fetch_result = FETCH_FAILED;
    }
}
#endif

SK_KEEP
sk_handle_t sk_asset_ensure_async(const char *path, const char *src)
{
    sk_handle_t handle;
    sk_asset_task_t *task_ptr;

    (void)src; /* per-call override unused for now; uses the configured host */
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
#ifdef __EMSCRIPTEN__
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = MAX_ASSET_TASKS,
        .num_channels = 1,
        .num_lanes = 4,
    });
#endif
    sk_asset_ready = true;
}

/* Free a finished task slot before firing its callback (which may queue more). */
static void finish(uint16_t i, bool ok, const char *local_path,
                   sk_asset_callback_fn on_success, sk_asset_callback_fn on_failure,
                   void *user_data)
{
    sk_handle_t handle = sk_handle_pool_handle_from_index(&sk_asset_pool, i);
    sk_asset_tasks[i] = (sk_asset_task_t){0};
    sk_handle_pool_free(&sk_asset_pool, handle);
    if (ok) {
        if (on_success) on_success(local_path, user_data);
    } else {
        log_error("Asset not found: %s", local_path);
        if (on_failure) on_failure(local_path, user_data);
    }
}

void sk_asset_tick(void)
{
    /* Nothing can be ensured until storage is up (web: after the idbfs restore
     * barrier; desktop: immediately). Tasks stay queued until then. */
    if (!sk_asset_ready || !sk_fs_is_ready()) {
        return;
    }
#ifdef __EMSCRIPTEN__
    sfetch_dowork(); /* fires on_fetch for any completed downloads */
#endif
    for (uint16_t i = 1; i < MAX_ASSET_TASKS; i++) {
        sk_asset_task_t *task = &sk_asset_tasks[i];
        char local[512];

        if (!sk_asset_occupied[i] || !task->armed) {
            continue;
        }

#ifdef __EMSCRIPTEN__
        if (task->state == TASK_FETCHING) {
            if (task->fetch_result == FETCH_PENDING) continue; /* still downloading */
            sk_fs_resolve(task->path, local, sizeof(local));
            finish(i, task->fetch_result == FETCH_OK, local,
                   task->on_success, task->on_failure, task->user_data);
            continue;
        }
#endif

        if (sk_fs_exists(task->path)) {
            sk_fs_resolve(task->path, local, sizeof(local));
            finish(i, true, local, task->on_success, task->on_failure, task->user_data);
            continue;
        }

#ifdef __EMSCRIPTEN__
        start_fetch(i); /* cache miss: download, cache, resolve on later ticks */
#else
        sk_fs_resolve(task->path, local, sizeof(local));
        finish(i, false, local, task->on_success, task->on_failure, task->user_data);
#endif
    }
}

void sk_asset_deinit(void)
{
    sk_asset_ready = false;
#ifdef __EMSCRIPTEN__
    sfetch_shutdown();
#endif
    sk_handle_pool_reset(&sk_asset_pool);
}
