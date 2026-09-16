#include "sk_asset.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_asset.h"
#include "internal/sk_fs.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#ifdef __EMSCRIPTEN__
#include "sokol_fetch.h"
/* Stream the download in chunks (sokol_fetch issues HTTP Range GETs on web) and
 * accumulate into an exactly-sized buffer — no per-file cap, memory tracks the
 * actual asset size. The dev server (tools/serve.py) honours Range for this. */
#define ASSET_CHUNK_BYTES (1024 * 1024)
#endif

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with a directly-openable local path. Storage is delegated to sk_fs.
 *
 * Desktop: the host is a local base dir (set as the sk_fs root); a missing file
 * is a failure. Web: the host is a fetch origin — a cache miss downloads the
 * asset via sokol_fetch, writes it into the idbfs-backed store, then resolves.
 * Either way the callback receives a path the sync sk_*_create(path) creators
 * can fopen. */

#define MAX_ASSET_TASKS 256
#define MAX_DEPENDENCY_FORMATS 8

enum { TASK_NEW = 0, TASK_FETCHING, TASK_WAITING /* on its dependencies */ };
enum { FETCH_PENDING = 0, FETCH_OK, FETCH_FAILED };

typedef struct {
    char path[512];      /* logical key: cache path + default (host + path) source */
    char fetch_url[1024]; /* per-call source override (empty = use host + path) */
    unsigned int flags;
    sk_asset_callback_fn on_success;
    sk_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via sk_asset_add_task */
    int state;
    int fetch_result;         /* web: FETCH_* set by the sokol_fetch callback */
    unsigned char *fetch_buf; /* web: chunk buffer bound to the in-flight fetch */
    unsigned char *acc;       /* web: accumulated file bytes across chunks */
    size_t acc_len;
    bool acc_error;           /* web: a chunk realloc failed mid-stream */
    /* dependencies (files this file references; see internal/sk_asset.h) */
    uint16_t parent;          /* slot of the task this one is a dependency of; 0 = none */
    int pending;              /* dependencies not finished yet */
    bool dependency_failed;
    bool dependencies_started;
} sk_asset_task_t;

typedef struct {
    char extension[16];
    sk_asset_dependencies_fn list;
} sk_asset_format_t;

static sk_asset_task_t sk_asset_tasks[MAX_ASSET_TASKS];
static sk_handle_pool_t sk_asset_pool;
static uint16_t sk_asset_free_indices[MAX_ASSET_TASKS];
static uint16_t sk_asset_generations[MAX_ASSET_TASKS];
static unsigned char sk_asset_occupied[MAX_ASSET_TASKS];
static bool sk_asset_ready = false;
static char sk_asset_host[256] = "";
static sk_asset_format_t sk_asset_formats[MAX_DEPENDENCY_FORMATS];
static int sk_asset_format_count;

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
/* sokol_fetch delivers chunks on the main thread when sfetch_dowork() (called in
 * sk_asset_tick) pumps it. We grow `acc` chunk by chunk; on the final chunk we
 * hand the whole file to sk_fs (which writes + flushes to idbfs) and flag the
 * slot, then tick resolves the task. */
static void on_fetch(const sfetch_response_t *r)
{
    uint16_t slot = *(const uint16_t *)r->user_data;
    sk_asset_task_t *task = &sk_asset_tasks[slot];

    if (r->fetched && r->data.size > 0 && !task->acc_error) {
        unsigned char *grown = (unsigned char *)realloc(task->acc, task->acc_len + r->data.size);
        if (grown == NULL) {
            task->acc_error = true; /* keep draining the stream, fail at finish */
        } else {
            task->acc = grown;
            memcpy(task->acc + task->acc_len, r->data.ptr, r->data.size);
            task->acc_len += r->data.size;
        }
    }
    if (r->finished) {
        bool ok = !r->failed && !task->acc_error &&
                  sk_fs_write(task->path, task->acc, (int)task->acc_len);
        task->fetch_result = ok ? FETCH_OK : FETCH_FAILED;
        free(task->acc);
        task->acc = NULL;
        task->acc_len = 0;
        free(task->fetch_buf);
        task->fetch_buf = NULL;
    }
}

static void start_fetch(uint16_t slot)
{
    sk_asset_task_t *task = &sk_asset_tasks[slot];
    char joined[1024];
    const char *url;
    sfetch_handle_t h;

    task->state = TASK_FETCHING;
    task->fetch_result = FETCH_PENDING;
    task->acc = NULL;
    task->acc_len = 0;
    task->acc_error = false;
    task->fetch_buf = (unsigned char *)malloc(ASSET_CHUNK_BYTES);
    if (task->fetch_buf == NULL) {
        task->fetch_result = FETCH_FAILED;
        return;
    }
    /* per-call override wins; otherwise the default host + key */
    if (task->fetch_url[0] != '\0') {
        url = task->fetch_url;
    } else {
        snprintf(joined, sizeof(joined), "%s/%s", sk_asset_host, task->path);
        url = joined;
    }
    h = sfetch_send(&(sfetch_request_t){
        .path = url,
        .callback = on_fetch,
        .chunk_size = ASSET_CHUNK_BYTES,
        .buffer = { .ptr = task->fetch_buf, .size = ASSET_CHUNK_BYTES },
        .user_data = { .ptr = &slot, .size = sizeof(slot) },
    });
    if (!sfetch_handle_valid(h)) {
        free(task->fetch_buf);
        task->fetch_buf = NULL;
        task->fetch_result = FETCH_FAILED;
    }
}
#endif

/* ------------------------------------------------------------ dependencies */

void sk_asset_register_dependencies(const char *extension, sk_asset_dependencies_fn list)
{
    if (extension == NULL || list == NULL || sk_asset_format_count >= MAX_DEPENDENCY_FORMATS) {
        return;
    }
    snprintf(sk_asset_formats[sk_asset_format_count].extension, sizeof(sk_asset_formats[0].extension), "%s",
             extension);
    sk_asset_formats[sk_asset_format_count++].list = list;
}

bool sk_asset_is_relative_uri(const char *uri)
{
    if (uri == NULL || uri[0] == '\0' || uri[0] == '/' || strncmp(uri, "data:", 5) == 0) {
        return false;
    }
    for (const char *c = uri; *c != '\0' && *c != '/'; c++) {
        if (*c == ':') {
            return false; /* scheme (http:, file:, ...) */
        }
    }
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool sk_asset_join_relative(const char *base_path, const char *uri, char *out, size_t out_size)
{
    char buffer[1024];
    const char *segments[128];
    size_t lengths[128];
    int count = 0;
    size_t n = 0, pos = 0;
    const char *last_slash;
    const bool rooted = base_path != NULL && base_path[0] == '/';

    if (base_path == NULL || uri == NULL || out == NULL || out_size == 0) {
        return false;
    }
    /* base directory, then the decoded uri, as one '/'-separated string */
    last_slash = strrchr(base_path, '/');
    if (last_slash != NULL) {
        n = (size_t)(last_slash - base_path) + 1;
        if (n >= sizeof(buffer)) return false;
        memcpy(buffer, base_path, n);
    }
    for (const char *c = uri; *c != '\0'; c++) {
        char ch = *c;
        if (ch == '%' && hex_value(c[1]) >= 0 && hex_value(c[2]) >= 0) {
            ch = (char)(hex_value(c[1]) * 16 + hex_value(c[2]));
            c += 2;
        }
        if (n + 1 >= sizeof(buffer)) return false;
        buffer[n++] = ch;
    }
    buffer[n] = '\0';

    for (size_t start = 0; start <= n;) {
        size_t end = start;
        while (end < n && buffer[end] != '/') end++;
        const size_t len = end - start;
        if (len == 0 || (len == 1 && buffer[start] == '.')) {
            /* empty or "." */
        } else if (len == 2 && buffer[start] == '.' && buffer[start + 1] == '.') {
            if (count == 0) return false; /* above the top directory */
            count--;
        } else {
            if (count >= (int)(sizeof(segments) / sizeof(segments[0]))) return false;
            segments[count] = &buffer[start];
            lengths[count++] = len;
        }
        start = end + 1;
    }

    if (rooted) {
        if (pos + 1 >= out_size) return false;
        out[pos++] = '/';
    }
    for (int i = 0; i < count; i++) {
        if (pos + lengths[i] + (i > 0 ? 1 : 0) >= out_size) return false;
        if (i > 0) out[pos++] = '/';
        memcpy(out + pos, segments[i], lengths[i]);
        pos += lengths[i];
    }
    out[pos] = '\0';
    return count > 0;
}

static sk_asset_dependencies_fn lookup_format(const char *path)
{
    const size_t path_len = strlen(path);
    for (int f = 0; f < sk_asset_format_count; f++) {
        const size_t ext_len = strlen(sk_asset_formats[f].extension);
        if (path_len < ext_len) continue;
        bool match = true;
        for (size_t k = 0; k < ext_len && match; k++) {
            match = tolower((unsigned char)path[path_len - ext_len + k]) ==
                    tolower((unsigned char)sk_asset_formats[f].extension[k]);
        }
        if (match) return sk_asset_formats[f].list;
    }
    return NULL;
}

/* Queue one dependency of the task in `context` (a uint16_t slot). */
static void add_dependency(const char *uri, void *context)
{
    const uint16_t parent = *(const uint16_t *)context;
    sk_asset_task_t *parent_task = &sk_asset_tasks[parent];
    char path[512], url[1024];
    sk_handle_t handle;
    sk_asset_task_t *task;

    if (!sk_asset_is_relative_uri(uri)) {
        return;
    }
    if (!sk_asset_join_relative(parent_task->path, uri, path, sizeof(path))) {
        log_warn("Asset %s: can't use dependency '%s' (outside the asset root or too long)", parent_task->path, uri);
        parent_task->dependency_failed = true;
        return;
    }
    for (uint16_t i = 1; i < MAX_ASSET_TASKS; i++) { /* referenced twice: ensure once */
        if (sk_asset_occupied[i] && sk_asset_tasks[i].parent == parent && strcmp(sk_asset_tasks[i].path, path) == 0) {
            return;
        }
    }
    handle = sk_handle_pool_alloc(&sk_asset_pool);
    if (handle == 0) {
        log_error("MAX_ASSET_TASKS reached (%d) ensuring dependencies of %s", MAX_ASSET_TASKS, parent_task->path);
        parent_task->dependency_failed = true;
        return;
    }
    task = resolve(handle);
    *task = (sk_asset_task_t){0};
    snprintf(task->path, sizeof(task->path), "%s", path);
    /* a file fetched from an explicit URL finds its dependencies next to that URL
     * (the browser resolves any "..") */
    if (parent_task->fetch_url[0] != '\0') {
        const char *slash = strrchr(parent_task->fetch_url, '/');
        const int dir_len = slash != NULL ? (int)(slash - parent_task->fetch_url) + 1 : 0;
        if (snprintf(url, sizeof(url), "%.*s%s", dir_len, parent_task->fetch_url, uri) < (int)sizeof(url)) {
            snprintf(task->fetch_url, sizeof(task->fetch_url), "%s", url);
        }
    }
    task->flags = parent_task->flags;
    task->parent = parent;
    task->armed = true;
    parent_task->pending++;
}

/* Queue the dependencies of a task whose own file is now local. */
static void start_dependencies(uint16_t slot)
{
    sk_asset_task_t *task = &sk_asset_tasks[slot];
    const sk_asset_dependencies_fn list = lookup_format(task->path);
    unsigned char *data = NULL;
    int size = 0;
    uint16_t context = slot;

    task->dependencies_started = true;
    if (list == NULL) {
        return;
    }
    if (!sk_fs_read(task->path, &data, &size)) {
        return; /* the resource creator reports the unreadable file */
    }
    list(data, size, add_dependency, &context);
    sk_fs_read_free(data);
    if (task->pending > 0) {
        task->state = TASK_WAITING;
    }
}

SK_KEEP
sk_handle_t sk_asset_ensure_async(const char *path, const char *fetch_url,
                                  unsigned int flags)
{
    sk_handle_t handle;
    sk_asset_task_t *task_ptr;

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
    if (fetch_url != NULL) {
        strncpy(task_ptr->fetch_url, fetch_url, sizeof(task_ptr->fetch_url) - 1);
    }
    task_ptr->flags = flags;
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

/* Free a finished task slot before firing its callback (which may queue more),
 * then tell the task it's a dependency of, if any. */
static void finish(uint16_t i, bool ok)
{
    sk_handle_t handle = sk_handle_pool_handle_from_index(&sk_asset_pool, i);
    const sk_asset_task_t task = sk_asset_tasks[i];
    char local[512];

    sk_fs_resolve(task.path, local, sizeof(local));
    sk_asset_tasks[i] = (sk_asset_task_t){0};
    sk_handle_pool_free(&sk_asset_pool, handle);
    if (ok) {
        if (task.on_success) task.on_success(local, task.user_data);
    } else {
        if (task.dependency_failed) {
            log_error("Asset dependencies missing: %s", local);
        } else {
            log_error("Asset not found: %s", local);
        }
        if (task.on_failure) task.on_failure(local, task.user_data);
    }
    if (task.parent != 0) {
        sk_asset_task_t *parent = &sk_asset_tasks[task.parent];
        parent->pending--;
        parent->dependency_failed = parent->dependency_failed || !ok;
        if (parent->pending <= 0) {
            finish(task.parent, !parent->dependency_failed);
        }
    }
}

/* A task's own file is local (ok) or unavailable: ensure its dependencies, or finish. */
static void resolved(uint16_t i, bool ok)
{
    sk_asset_task_t *task = &sk_asset_tasks[i];
    if (ok && !task->dependencies_started) {
        start_dependencies(i);
        if (task->state == TASK_WAITING) {
            return; /* finishes when its last dependency does */
        }
    }
    finish(i, ok && !task->dependency_failed);
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

        if (!sk_asset_occupied[i] || !task->armed || task->state == TASK_WAITING) {
            continue;
        }

#ifdef __EMSCRIPTEN__
        if (task->state == TASK_FETCHING) {
            if (task->fetch_result == FETCH_PENDING) continue; /* still downloading */
            resolved(i, task->fetch_result == FETCH_OK);
            continue;
        }
        /* FORCE_FETCH re-downloads; otherwise serve the cache when present. */
        if (!(task->flags & SK_ASSET_FORCE_FETCH) && sk_fs_exists(task->path)) {
            resolved(i, true);
            continue;
        }
        start_fetch(i); /* miss (or forced): download, cache, resolve on later ticks */
#else
        /* Desktop has no network fetcher yet, so FORCE_FETCH is a no-op: resolve
         * from the jailed local fs (miss = failure). Network fallback is TODO. */
        resolved(i, sk_fs_exists(task->path));
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
