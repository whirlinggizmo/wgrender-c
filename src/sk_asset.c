#include "sk_asset.h"

#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_fs.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with a directly-openable local path. Storage is delegated to sk_fs.
 *
 * Desktop: the host is a local base dir (set as the sk_fs root); a missing file
 * is a failure. Web: the host is a fetch origin — a cache miss downloads the
 * asset, writes it into the idbfs-backed store, then resolves. Either way the
 * callback receives a path the sync sk_*_create(path) creators can fopen. */

#define MAX_ASSET_TASKS 64

enum { TASK_NEW = 0, TASK_FETCHING };

typedef struct {
    char path[512];
    sk_asset_callback_fn on_success;
    sk_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via sk_asset_add_task */
    int state;
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
/* Per-slot fetch state, polled by sk_asset_tick: 0 pending / 1 ok / 2 failed. */
static volatile int sk_asset_fetch_state[MAX_ASSET_TASKS];

/* Called from JS when bytes arrive: hand them to sk_fs (which writes + flushes),
 * then flag the slot so tick can fire the callback with the local path. */
SK_KEEP
void sk_asset_on_fetched(int slot, const unsigned char *data, int size)
{
    if (slot < 0 || slot >= MAX_ASSET_TASKS) return;
    if (data != NULL && size >= 0 && sk_fs_write(sk_asset_tasks[slot].path, data, size)) {
        sk_asset_fetch_state[slot] = 1;
    } else {
        sk_asset_fetch_state[slot] = 2;
    }
}

SK_KEEP
void sk_asset_on_fetch_failed(int slot)
{
    if (slot >= 0 && slot < MAX_ASSET_TASKS) sk_asset_fetch_state[slot] = 2;
}

/* fetch(url) -> bytes -> sk_asset_on_fetched(slot, ...). No JSPI: this is a
 * fire-and-forget Promise; tick polls sk_asset_fetch_state[slot]. */
EM_JS(void, sk_asset_fetch_begin, (int slot, const char *url_c), {
    const url = UTF8ToString(url_c);
    fetch(url).then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.arrayBuffer();
    }).then(function (buf) {
        const bytes = new Uint8Array(buf);
        const ptr = Module._malloc(bytes.length || 1);
        Module.HEAPU8.set(bytes, ptr);
        Module.ccall("sk_asset_on_fetched", "void",
                     ["number", "number", "number"], [slot, ptr, bytes.length]);
        Module._free(ptr);
    }).catch(function (e) {
        console.error("sk_asset: fetch failed", url, e);
        Module.ccall("sk_asset_on_fetch_failed", "void", ["number"], [slot]);
    });
});
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
    for (uint16_t i = 1; i < MAX_ASSET_TASKS; i++) {
        sk_asset_task_t *task = &sk_asset_tasks[i];
        char local[512];

        if (!sk_asset_occupied[i] || !task->armed) {
            continue;
        }

#ifdef __EMSCRIPTEN__
        if (task->state == TASK_FETCHING) {
            int st = sk_asset_fetch_state[i];
            if (st == 0) continue; /* still downloading */
            sk_fs_resolve(task->path, local, sizeof(local));
            finish(i, st == 1, local, task->on_success, task->on_failure, task->user_data);
            continue;
        }
#endif

        if (sk_fs_exists(task->path)) {
            sk_fs_resolve(task->path, local, sizeof(local));
            finish(i, true, local, task->on_success, task->on_failure, task->user_data);
            continue;
        }

#ifdef __EMSCRIPTEN__
        /* Cache miss: fetch from the host, then cache + resolve next ticks. */
        {
            char url[768];
            snprintf(url, sizeof(url), "%s/%s", sk_asset_host, task->path);
            sk_asset_fetch_state[i] = 0;
            task->state = TASK_FETCHING;
            sk_asset_fetch_begin((int)i, url);
        }
#else
        sk_fs_resolve(task->path, local, sizeof(local));
        finish(i, false, local, task->on_success, task->on_failure, task->user_data);
#endif
    }
}

void sk_asset_deinit(void)
{
    sk_asset_ready = false;
    sk_handle_pool_reset(&sk_asset_pool);
}
