#include "sk_asset.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_asset.h"
#include "internal/sk_fs.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_loader.h"
#include "internal/sk_thread.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#ifdef __EMSCRIPTEN__
#include "sokol_fetch.h"
/* Stream the download in chunks (sokol_fetch issues HTTP Range GETs on web) and
 * accumulate into an exactly-sized buffer — no per-file cap, memory tracks the
 * actual asset size. The dev server (tools/serve.py) honours Range for this. */
#define ASSET_CHUNK_BYTES (1024 * 1024)
#define MAX_FETCHES 256 /* downloads at once (sokol_fetch's request pool); more tasks wait */
static int sk_asset_fetching;
#endif

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with a directly-openable local path. Storage is delegated to sk_fs.
 *
 * Desktop: the host is a local base dir (set as the sk_fs root); a missing file
 * is a failure. Web: the host is a fetch origin — a cached file is read from the
 * cache (IndexedDB) into the local store; a miss downloads the asset via
 * sokol_fetch and writes it into the store, which keeps it; then it resolves.
 * Either way the callback receives a path the sync sk_*_create(path) creators
 * can fopen. */

#define ASSET_TASKS_INITIAL 64 /* slots to start with; the pool doubles as needed */
#define MAX_DEPENDENCY_FORMATS 8
#define MAX_LOADER_FORMATS 16

enum {
    TASK_NEW = 0,
    TASK_FETCHING,
    TASK_WAITING,   /* on its dependencies */
    TASK_PREPARING, /* queued for or running on a worker */
    TASK_FINISHING, /* prepared; creating the resource on the main thread */
};
#define MAX_WORKERS 4
#define DEFAULT_UPLOAD_BUDGET_MS 4.0f
enum { FETCH_PENDING = 0, FETCH_OK, FETCH_FAILED };

typedef struct {
    char path[512];      /* logical key: cache path + default (host + path) source */
    char fetch_url[1024]; /* per-call source override (empty = use host + path) */
    char fallback[512];   /* ensured instead when `path` is missing ("" = none) */
    char fallback_url[1024]; /* its source, when fetch_url is set */
    unsigned int flags;
    sk_asset_callback_fn on_success;
    sk_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via sk_asset_add_task */
    int state;
    int fetch_result;         /* web: FETCH_* set by the sokol_fetch callback */
    int cache_read;           /* web: reading the file from the cache (sk_fs_cache_read_begin), or 0 */
    unsigned char *fetch_buf; /* web: chunk buffer bound to the in-flight fetch */
    unsigned char *acc;       /* web: accumulated file bytes across chunks */
    size_t acc_len;
    bool acc_error;           /* web: a chunk realloc failed mid-stream */
    /* dependencies (files this file references; see internal/sk_asset.h) */
    uint16_t parent;          /* slot of the task this one is a dependency of; 0 = none */
    int pending;              /* dependencies not finished yet */
    bool dependency_failed;
    bool dependencies_started;
    bool optional;            /* a dependency its parent can do without */
    /* loading (docs/PLAN-pipeline.md): prepare on a worker, finish on the main thread */
    char local[512];          /* the local path: the resource's name and the callback's path */
    const sk_loader_t *loader;
    void *prepared;
    sk_handle_t resource;     /* holds one reference until the callback has run */
    bool load_failed;
    uint32_t finish_order;    /* finishes run in the order tasks were prepared */
    bool finish_started;      /* a resource being finished over several steps goes first */
    /* groups (sk_asset_group_create): a task that completes when its members have */
    bool is_group;
    uint16_t group;           /* slot of the group this task is a member of; 0 = none */
    int dependency_count;     /* dependencies (or a group's members) added in total */
    int failed_members;
    struct sk_asset_held *held; /* a group's members' resources, until its callbacks have run */
    int held_count, held_capacity;
} sk_asset_task_t;

typedef struct sk_asset_held {
    const sk_loader_t *loader;
    sk_handle_t resource;
} sk_asset_held_t;

/* A prepare job for the workers, or its result. */
typedef struct {
    uint16_t slot;
    const sk_loader_t *loader;
    char path[512];
    void *prepared;
} sk_asset_job_t;

typedef struct {
    char extension[16];
    sk_asset_dependencies_fn list;
} sk_asset_format_t;

static sk_asset_task_t *sk_asset_tasks; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_asset_pool;
static bool sk_asset_ready = false;
static char sk_asset_host[256] = "";
static sk_asset_format_t sk_asset_formats[MAX_DEPENDENCY_FORMATS];
static int sk_asset_format_count;

typedef struct {
    char extension[16];
    const sk_loader_t *loader;
} sk_asset_loader_format_t;

static sk_asset_loader_format_t sk_asset_loaders[MAX_LOADER_FORMATS];
static int sk_asset_loader_count;

/* A ring of jobs. Rings hold as many jobs as there are task slots (a task has at
 * most one job or result at a time), so they never fill up; they grow with the task
 * pool (alloc_task). */
typedef struct {
    sk_asset_job_t *jobs;
    int capacity, head, count;
} sk_asset_ring_t;

/* Workers and their queues. Guarded by sk_asset_jobs.lock. The rings are never
 * freed: on web, workers detached at shutdown may still push to them. */
static struct {
    sk_mutex_t lock;
    sk_cond_t wake;
    sk_thread_t threads[MAX_WORKERS];
    int worker_count;
    bool stop;
    bool lock_live;
    sk_asset_ring_t queue;
    sk_asset_ring_t done;
} sk_asset_jobs;
static int sk_asset_worker_request = -1; /* -1 = default */
static float sk_asset_upload_budget_ms = DEFAULT_UPLOAD_BUDGET_MS;
static uint32_t sk_asset_finish_counter;

static sk_handle_t alloc_task(void);

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

SK_KEEP
const char *sk_asset_get_host(void)
{
    return sk_asset_host;
}

#ifdef __EMSCRIPTEN__
/* sokol_fetch delivers chunks on the main thread when sfetch_dowork() (called in
 * sk_asset_tick) pumps it. We grow `acc` chunk by chunk; on the final chunk we
 * hand the whole file to sk_fs (which writes it and keeps it in the cache) and
 * flag the slot, then tick resolves the task. */
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
        sk_asset_fetching--;
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
    } else {
        sk_asset_fetching++;
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

#define MAX_PATH_MAPPERS 4
static struct {
    char extension[16];
    sk_asset_path_mapper_fn map;
} sk_asset_mappers[MAX_PATH_MAPPERS];
static int sk_asset_mapper_count;

void sk_asset_register_path_mapper(const char *extension, sk_asset_path_mapper_fn map)
{
    for (int i = 0; i < sk_asset_mapper_count; i++) {
        if (strcmp(sk_asset_mappers[i].extension, extension) == 0) {
            sk_asset_mappers[i].map = map;
            return;
        }
    }
    if (sk_asset_mapper_count >= MAX_PATH_MAPPERS || strlen(extension) >= sizeof(sk_asset_mappers[0].extension)) {
        log_error("Can't register a path mapper for %s", extension);
        return;
    }
    snprintf(sk_asset_mappers[sk_asset_mapper_count].extension, sizeof(sk_asset_mappers[0].extension), "%s",
             extension);
    sk_asset_mappers[sk_asset_mapper_count++].map = map;
}

void sk_asset_register_loader(const char *extension, const sk_loader_t *loader)
{
    for (int i = 0; i < sk_asset_loader_count; i++) {
        if (strcmp(sk_asset_loaders[i].extension, extension) == 0) {
            sk_asset_loaders[i].loader = loader;
            return;
        }
    }
    if (sk_asset_loader_count >= MAX_LOADER_FORMATS || strlen(extension) >= sizeof(sk_asset_loaders[0].extension)) {
        log_error("Can't register a loader for %s", extension);
        return;
    }
    snprintf(sk_asset_loaders[sk_asset_loader_count].extension, sizeof(sk_asset_loaders[0].extension), "%s", extension);
    sk_asset_loaders[sk_asset_loader_count++].loader = loader;
}

sk_handle_t sk_loader_create(const sk_loader_t *loader, const char *path)
{
    sk_handle_t resource = loader->find(path);
    void *prepared;
    sk_loader_step_t step = SK_LOADER_MORE;

    if (resource != 0) {
        return resource;
    }
    prepared = loader->prepare(path);
    if (prepared == NULL) {
        return 0;
    }
    while (step == SK_LOADER_MORE) {
        step = loader->finish(prepared, path, &resource);
    }
    loader->discard(prepared);
    return step == SK_LOADER_DONE ? resource : 0;
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

static bool has_extension(const char *path, const char *extension)
{
    const size_t path_len = strlen(path), ext_len = strlen(extension);
    if (path_len < ext_len) return false;
    for (size_t k = 0; k < ext_len; k++) {
        if (tolower((unsigned char)path[path_len - ext_len + k]) != tolower((unsigned char)extension[k])) return false;
    }
    return true;
}

static const sk_loader_t *lookup_loader(const char *path)
{
    for (int i = 0; i < sk_asset_loader_count; i++) {
        if (has_extension(path, sk_asset_loaders[i].extension)) return sk_asset_loaders[i].loader;
    }
    return NULL;
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

/* A file fetched from an explicit URL finds its dependencies next to that URL (the
 * browser resolves any ".."); otherwise `out` stays empty (host + path). */
static void dependency_url(const sk_asset_task_t *parent_task, const char *uri, char *out, size_t out_size)
{
    char url[1024];
    out[0] = '\0';
    if (parent_task->fetch_url[0] != '\0') {
        const char *slash = strrchr(parent_task->fetch_url, '/');
        const int dir_len = slash != NULL ? (int)(slash - parent_task->fetch_url) + 1 : 0;
        if (snprintf(url, sizeof(url), "%.*s%s", dir_len, parent_task->fetch_url, uri) < (int)sizeof(url)) {
            snprintf(out, out_size, "%s", url);
        }
    }
}

/* Queue one dependency of the task in `context` (a uint16_t slot). */
static void add_dependency(const char *uri, const char *fallback_uri, bool required, void *context)
{
    const uint16_t parent = *(const uint16_t *)context;
    sk_asset_task_t *parent_task = &sk_asset_tasks[parent];
    char path[512];
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
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) { /* referenced twice: ensure once */
        if (sk_asset_pool.occupied[i] && sk_asset_tasks[i].parent == parent &&
            strcmp(sk_asset_tasks[i].path, path) == 0) {
            return;
        }
    }
    handle = alloc_task();
    parent_task = &sk_asset_tasks[parent]; /* the allocation may have moved the tasks */
    if (handle == 0) {
        log_error("Asset %s: can't queue its dependency %s", parent_task->path, path);
        parent_task->dependency_failed = true;
        return;
    }
    task = resolve(handle);
    *task = (sk_asset_task_t){0};
    snprintf(task->path, sizeof(task->path), "%s", path);
    dependency_url(parent_task, uri, task->fetch_url, sizeof(task->fetch_url));
    if (fallback_uri != NULL && sk_asset_is_relative_uri(fallback_uri) &&
        sk_asset_join_relative(parent_task->path, fallback_uri, task->fallback, sizeof(task->fallback))) {
        dependency_url(parent_task, fallback_uri, task->fallback_url, sizeof(task->fallback_url));
    }
    task->flags = parent_task->flags;
    task->parent = parent;
    task->optional = !required;
    task->armed = true;
    parent_task->pending++;
    parent_task->dependency_count++;
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
    task = &sk_asset_tasks[slot]; /* queueing dependencies may have moved the tasks */
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
    handle = alloc_task();
    if (handle == 0) {
        return 0;
    }
    task_ptr = resolve(handle);
    *task_ptr = (sk_asset_task_t){0};
    strncpy(task_ptr->path, path, sizeof(task_ptr->path) - 1);
    if (fetch_url == NULL) { /* a variant chosen for this device, say (sk_asset_register_path_mapper) */
        for (int i = 0; i < sk_asset_mapper_count; i++) {
            char mapped[sizeof(task_ptr->path)];
            if (has_extension(path, sk_asset_mappers[i].extension) &&
                sk_asset_mappers[i].map(path, mapped, sizeof(mapped), task_ptr->fallback, sizeof(task_ptr->fallback))) {
                snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", mapped);
                break;
            }
            task_ptr->fallback[0] = '\0';
        }
    }
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

/* ------------------------------------------------------------- workers ---- */

static void push_job(sk_asset_ring_t *ring, const sk_asset_job_t *job)
{
    ring->jobs[(ring->head + ring->count) % ring->capacity] = *job; /* never full: one entry per task */
    ring->count++;
}

static bool pop_job(sk_asset_ring_t *ring, sk_asset_job_t *job)
{
    if (ring->count == 0) return false;
    *job = ring->jobs[ring->head];
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count--;
    return true;
}

/* Grow a ring to `capacity` jobs, keeping their order. Under sk_asset_jobs.lock. */
static bool grow_ring(sk_asset_ring_t *ring, int capacity)
{
    sk_asset_job_t *jobs;
    if (ring->capacity >= capacity) return true;
    jobs = (sk_asset_job_t *)malloc(sizeof(sk_asset_job_t) * (size_t)capacity);
    if (jobs == NULL) return false;
    for (int i = 0; i < ring->count; i++) {
        jobs[i] = ring->jobs[(ring->head + i) % ring->capacity];
    }
    free(ring->jobs);
    ring->jobs = jobs;
    ring->capacity = capacity;
    ring->head = 0;
    return true;
}

/* A new task slot, with the job rings grown to match the pool; 0 when there's none. */
static sk_handle_t alloc_task(void)
{
    const sk_handle_t handle = sk_handle_pool_alloc(&sk_asset_pool);
    bool ok;
    if (handle == 0) {
        log_error("asset: too many tasks (%u)", (unsigned)sk_asset_pool.max - 1u);
        return 0;
    }
    sk_mutex_lock(&sk_asset_jobs.lock);
    ok = grow_ring(&sk_asset_jobs.queue, sk_asset_pool.capacity) &&
         grow_ring(&sk_asset_jobs.done, sk_asset_pool.capacity);
    sk_mutex_unlock(&sk_asset_jobs.lock);
    if (!ok) {
        sk_handle_pool_free(&sk_asset_pool, handle);
        log_error("asset: out of memory");
        return 0;
    }
    return handle;
}

static void worker_main(void *arg)
{
    sk_asset_job_t job;
    (void)arg;
    sk_mutex_lock(&sk_asset_jobs.lock);
    for (;;) {
        while (!sk_asset_jobs.stop && sk_asset_jobs.queue.count == 0) {
            sk_cond_wait(&sk_asset_jobs.wake, &sk_asset_jobs.lock);
        }
        if (sk_asset_jobs.stop) break;
        pop_job(&sk_asset_jobs.queue, &job);
        sk_mutex_unlock(&sk_asset_jobs.lock);
        job.prepared = job.loader->prepare(job.path);
        sk_mutex_lock(&sk_asset_jobs.lock);
        push_job(&sk_asset_jobs.done, &job);
    }
    sk_mutex_unlock(&sk_asset_jobs.lock);
}

static int default_worker_count(void)
{
    const int count = sk_thread_cpu_count() - 1;
    if (!sk_thread_available()) return 0;
    return count < 1 ? 1 : (count > MAX_WORKERS ? MAX_WORKERS : count);
}

static void start_workers(int count)
{
    sk_asset_jobs.stop = false;
    sk_asset_jobs.worker_count = 0;
    for (int i = 0; i < count && i < MAX_WORKERS; i++) {
        if (!sk_thread_create(&sk_asset_jobs.threads[i], worker_main, NULL)) {
            log_warn("Asset workers: started %d of %d; the rest of loading runs on the main thread", i, count);
            break;
        }
        sk_asset_jobs.worker_count++;
    }
}

/* `wait`: join the workers (running prepares finish first). Otherwise they're
 * detached and end on their own; the job lock must then stay alive. */
static void stop_workers(bool wait)
{
    sk_mutex_lock(&sk_asset_jobs.lock);
    sk_asset_jobs.stop = true;
    sk_cond_broadcast(&sk_asset_jobs.wake);
    sk_mutex_unlock(&sk_asset_jobs.lock);
    for (int i = 0; i < sk_asset_jobs.worker_count; i++) {
        if (wait) {
            sk_thread_join(&sk_asset_jobs.threads[i]);
        } else {
            sk_thread_detach(&sk_asset_jobs.threads[i]);
        }
    }
    sk_asset_jobs.worker_count = 0;
}

void sk_asset_set_worker_count(int count)
{
    sk_asset_worker_request = count;
    if (sk_asset_ready) {
        stop_workers(true);
        start_workers(count >= 0 ? count : default_worker_count());
    }
}

int sk_asset_get_worker_count(void)
{
    return sk_asset_jobs.worker_count;
}

SK_KEEP
void sk_asset_set_upload_budget(float milliseconds)
{
    sk_asset_upload_budget_ms = milliseconds > 0.0f ? milliseconds : 0.0f;
}

/* ------------------------------------------------------ groups, progress */

SK_KEEP
sk_handle_t sk_asset_group_create(void)
{
    sk_handle_t handle;
    sk_asset_task_t *task_ptr;

    if (!sk_asset_ready) {
        return 0;
    }
    handle = alloc_task();
    if (handle == 0) {
        return 0;
    }
    task_ptr = resolve(handle);
    *task_ptr = (sk_asset_task_t){0};
    task_ptr->is_group = true;
    task_ptr->state = TASK_WAITING;
    return handle;
}

SK_KEEP
bool sk_asset_group_add(sk_handle_t group, sk_handle_t task)
{
    sk_asset_task_t *group_ptr = resolve(group), *task_ptr = resolve(task);
    uint16_t group_index = 0;

    if (group_ptr == NULL || task_ptr == NULL || !group_ptr->is_group || task_ptr->is_group || group == task ||
        task_ptr->group != 0 || task_ptr->parent != 0) {
        log_warn("sk_asset_group_add: needs a group and a file task that isn't in a group");
        return false;
    }
    sk_handle_pool_resolve(&sk_asset_pool, group, &group_index);
    task_ptr->group = group_index;
    task_ptr->armed = true; /* loads even without callbacks of its own */
    group_ptr->pending++;
    group_ptr->dependency_count++;
    return true;
}

/* Rough progress of one file task: fetched, prepared, finished. */
static float task_progress(const sk_asset_task_t *task)
{
    switch (task->state) {
        case TASK_WAITING:
            return 0.25f + 0.25f * (task->dependency_count > 0
                                        ? (float)(task->dependency_count - task->pending) / (float)task->dependency_count
                                        : 1.0f);
        case TASK_PREPARING: return 0.5f;
        case TASK_FINISHING: return 0.75f;
        default: return 0.0f; /* queued or downloading */
    }
}

SK_KEEP
float sk_asset_get_progress(sk_handle_t task)
{
    uint16_t index = 0;
    const sk_asset_task_t *task_ptr;
    float sum;

    if (sk_handle_get_kind(task) != SK_HANDLE_KIND_ASSET_TASK) {
        return 0.0f;
    }
    if (!sk_handle_pool_resolve(&sk_asset_pool, task, &index)) {
        return 1.0f; /* finished: its callbacks have run */
    }
    task_ptr = &sk_asset_tasks[index];
    if (!task_ptr->is_group) {
        return task_progress(task_ptr);
    }
    if (task_ptr->dependency_count == 0) {
        return 0.0f;
    }
    sum = (float)(task_ptr->dependency_count - task_ptr->pending); /* finished members */
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) {
        if (sk_asset_pool.occupied[i] && sk_asset_tasks[i].group == index) {
            sum += task_progress(&sk_asset_tasks[i]);
        }
    }
    return sum / (float)task_ptr->dependency_count;
}

void sk_asset_init(void)
{
    if (!sk_handle_pool_init(&sk_asset_pool, SK_HANDLE_KIND_ASSET_TASK, "asset", (void **)&sk_asset_tasks,
                             sizeof(sk_asset_task_t), ASSET_TASKS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("asset: out of memory");
    }
#ifdef __EMSCRIPTEN__
    sk_asset_fetching = 0;
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = MAX_FETCHES,
        .num_channels = 1,
        .num_lanes = 4,
    });
#endif
    if (!sk_asset_jobs.lock_live) { /* still alive after a web shutdown (workers detached) */
        sk_mutex_init(&sk_asset_jobs.lock);
        sk_cond_init(&sk_asset_jobs.wake);
        sk_asset_jobs.lock_live = true;
    }
    sk_mutex_lock(&sk_asset_jobs.lock);
    sk_asset_jobs.queue.head = sk_asset_jobs.queue.count = 0;
    sk_asset_jobs.done.head = sk_asset_jobs.done.count = 0;
    sk_mutex_unlock(&sk_asset_jobs.lock);
    start_workers(sk_asset_worker_request >= 0 ? sk_asset_worker_request : default_worker_count());
    log_info("sk_asset: %d loading worker(s)%s", sk_asset_jobs.worker_count,
             sk_asset_jobs.worker_count == 0 ? " (loading on the main thread)" : "");
    sk_asset_ready = true;
}

static void ready(uint16_t i, bool ok);

static bool hold(sk_asset_task_t *group, const sk_loader_t *loader, sk_handle_t resource)
{
    if (group->held_count == group->held_capacity) {
        const int capacity = group->held_capacity > 0 ? group->held_capacity * 2 : 8;
        sk_asset_held_t *held = (sk_asset_held_t *)realloc(group->held, (size_t)capacity * sizeof(sk_asset_held_t));
        if (held == NULL) return false;
        group->held = held;
        group->held_capacity = capacity;
    }
    group->held[group->held_count++] = (sk_asset_held_t){loader, resource};
    return true;
}

/* Free a finished task slot before firing its callback (which may queue more),
 * then tell the task it's a dependency of, if any. */
static void complete(uint16_t i, bool ok)
{
    sk_handle_t handle = sk_handle_pool_handle_from_index(&sk_asset_pool, i);
    const sk_asset_task_t task = sk_asset_tasks[i];
    char local[512];

    if (task.is_group) {
        local[0] = '\0';
    } else if (task.local[0] != '\0') {
        snprintf(local, sizeof(local), "%s", task.local);
    } else {
        sk_fs_resolve(task.path, local, sizeof(local));
    }
    sk_asset_tasks[i] = (sk_asset_task_t){0};
    sk_handle_pool_free(&sk_asset_pool, handle);
    if (ok) {
        if (task.on_success) task.on_success(local, task.user_data);
    } else {
        if (task.is_group) {
            log_error("Asset group: some files failed (%d of %d)", task.failed_members, task.dependency_count);
        } else if (task.load_failed) {
            log_error("Asset couldn't be loaded: %s", local);
        } else if (task.dependency_failed) {
            log_error("Asset dependencies missing: %s", local);
        } else if (task.optional) {
            log_warn("Asset not found (optional, dependency of %s): %s", sk_asset_tasks[task.parent].path, local);
        } else {
            log_error("Asset not found: %s", local);
        }
        if (task.on_failure) task.on_failure(local, task.user_data);
    }
    for (int h = 0; h < task.held_count; h++) {
        task.held[h].loader->release(task.held[h].resource); /* members' resources nobody created */
    }
    free(task.held);
    if (task.resource != 0 && task.group != 0 && hold(&sk_asset_tasks[task.group], task.loader, task.resource)) {
        /* the group keeps it until its own callbacks have run */
    } else if (task.resource != 0) {
        task.loader->release(task.resource); /* freed unless the callback created it */
    }
    if (task.parent != 0) {
        sk_asset_task_t *parent = &sk_asset_tasks[task.parent];
        parent->pending--;
        parent->dependency_failed = parent->dependency_failed || (!ok && !task.optional);
        if (parent->pending <= 0) {
            ready(task.parent, !parent->dependency_failed);
        }
    }
    if (task.group != 0) {
        sk_asset_task_t *group = &sk_asset_tasks[task.group];
        group->pending--;
        group->failed_members += ok ? 0 : 1;
        if (group->pending <= 0 && group->armed) {
            complete(task.group, group->failed_members == 0);
        }
    }
}

/* A task's files are all local (ok) or not: load its resource, or complete. */
static void ready(uint16_t i, bool ok)
{
    sk_asset_task_t *task = &sk_asset_tasks[i];
    sk_asset_job_t job = {.slot = i};

    task->loader = ok && task->parent == 0 && !(task->flags & SK_ASSET_FILE_ONLY) ? lookup_loader(task->path) : NULL;
    if (task->loader == NULL) {
        complete(i, ok);
        return;
    }
    sk_fs_resolve(task->path, task->local, sizeof(task->local));
    task->resource = task->loader->find(task->local);
    if (task->resource != 0) {
        complete(i, true); /* already loaded */
        return;
    }
    task->state = TASK_PREPARING;
    job.loader = task->loader;
    snprintf(job.path, sizeof(job.path), "%s", task->local);
    sk_mutex_lock(&sk_asset_jobs.lock);
    push_job(&sk_asset_jobs.queue, &job);
    sk_cond_broadcast(&sk_asset_jobs.wake);
    sk_mutex_unlock(&sk_asset_jobs.lock);
}

/* A task's own file is local (ok) or unavailable: ensure its dependencies, or finish. */
static void resolved(uint16_t i, bool ok)
{
    sk_asset_task_t *task = &sk_asset_tasks[i];
    if (ok && !task->dependencies_started) {
        start_dependencies(i);
        task = &sk_asset_tasks[i]; /* it may have moved the tasks */
        if (task->state == TASK_WAITING) {
            return; /* finishes when its last dependency does */
        }
    }
    ready(i, ok && !task->dependency_failed);
}

/* Prepared jobs back from the workers (or, without workers, one prepared here). */
static void collect_prepared(void)
{
    sk_asset_job_t job;
    bool have;

    if (sk_asset_jobs.worker_count == 0) {
        sk_mutex_lock(&sk_asset_jobs.lock);
        have = pop_job(&sk_asset_jobs.queue, &job);
        sk_mutex_unlock(&sk_asset_jobs.lock);
        if (have) { /* one per frame, so loads don't stack into one stall */
            job.prepared = job.loader->prepare(job.path);
            sk_mutex_lock(&sk_asset_jobs.lock);
            push_job(&sk_asset_jobs.done, &job);
            sk_mutex_unlock(&sk_asset_jobs.lock);
        }
    }
    for (;;) {
        sk_mutex_lock(&sk_asset_jobs.lock);
        have = pop_job(&sk_asset_jobs.done, &job);
        sk_mutex_unlock(&sk_asset_jobs.lock);
        if (!have) break;
        sk_asset_task_t *task = &sk_asset_tasks[job.slot];
        if (job.prepared == NULL) {
            task->load_failed = true;
            complete(job.slot, false);
            continue;
        }
        task->prepared = job.prepared;
        task->state = TASK_FINISHING;
        task->finish_order = sk_asset_finish_counter++;
    }
}

/* The finishing task prepared first, or 0. */
static uint16_t next_finishing(void)
{
    uint16_t next = 0;
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) {
        const sk_asset_task_t *task = &sk_asset_tasks[i];
        if (!sk_asset_pool.occupied[i] || task->state != TASK_FINISHING) continue;
        if (task->finish_started) return i;
        if (next == 0 || task->finish_order < sk_asset_tasks[next].finish_order) next = i;
    }
    return next;
}

/* Finish prepared resources on the main thread within the upload budget, at least
 * one step per frame. */
static void load(void)
{
    const double start = sk_thread_now();
    uint16_t i;

    collect_prepared();
    while ((i = next_finishing()) != 0) {
        sk_asset_task_t *task = &sk_asset_tasks[i];
        sk_loader_step_t step = SK_LOADER_DONE;

        if (!task->finish_started) {
            /* created meanwhile, e.g. by a sync create of the same file: use that one */
            task->resource = task->loader->find(task->local);
            task->finish_started = true;
        }
        if (task->resource == 0) {
            step = task->loader->finish(task->prepared, task->local, &task->resource);
        }
        if (step != SK_LOADER_MORE) {
            task->loader->discard(task->prepared);
            task->prepared = NULL;
            task->load_failed = step == SK_LOADER_FAILED;
            complete(i, step == SK_LOADER_DONE);
        }
        if ((sk_thread_now() - start) * 1000.0 >= (double)sk_asset_upload_budget_ms) {
            break;
        }
    }
}

/* A task's file is missing: ensure its fallback instead, if it has one (a compressed
 * texture's PNG, say). The task starts over on the next tick. */
static bool use_fallback(sk_asset_task_t *task)
{
    if (task->fallback[0] == '\0') return false;
    log_warn("Asset not found: %s; using %s instead", task->path, task->fallback);
    snprintf(task->path, sizeof(task->path), "%s", task->fallback);
    snprintf(task->fetch_url, sizeof(task->fetch_url), "%s", task->fallback_url);
    task->fallback[0] = task->fallback_url[0] = '\0';
    task->state = TASK_NEW;
    task->fetch_result = FETCH_PENDING;
    return true;
}

void sk_asset_tick(void)
{
    /* Nothing can be ensured until storage is up (web: once the cache's list of
     * files is read; desktop: immediately). Tasks stay queued until then. */
    if (!sk_asset_ready || !sk_fs_is_ready()) {
        return;
    }
#ifdef __EMSCRIPTEN__
    sfetch_dowork(); /* fires on_fetch for any completed downloads */
#endif
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) {
        sk_asset_task_t *task = &sk_asset_tasks[i];

        if (!sk_asset_pool.occupied[i] || !task->armed || task->state == TASK_PREPARING ||
            task->state == TASK_FINISHING) {
            continue;
        }
        if (task->is_group) {
            if (task->pending <= 0) complete(i, task->failed_members == 0); /* all done before it was armed */
            continue;
        }
        if (task->state == TASK_WAITING) {
            continue;
        }

#ifdef __EMSCRIPTEN__
        if (task->cache_read != 0) {
            const int read = sk_fs_cache_read_poll(task->cache_read);
            if (read == 0) continue; /* still reading */
            task->cache_read = 0;
            if (read > 0) {
                resolved(i, true);
            } else {
                task->state = TASK_NEW; /* dropped from the cache: download it */
            }
            continue;
        }
        if (task->state == TASK_FETCHING) {
            if (task->fetch_result == FETCH_PENDING) continue; /* still downloading */
            if (task->fetch_result == FETCH_FAILED && use_fallback(task)) continue;
            resolved(i, task->fetch_result == FETCH_OK);
            continue;
        }
        /* FORCE_FETCH re-downloads; otherwise serve the cache when present. */
        if (!(task->flags & SK_ASSET_FORCE_FETCH) && sk_fs_exists(task->path)) {
            resolved(i, true);
            continue;
        }
        if (!(task->flags & SK_ASSET_FORCE_FETCH) && sk_fs_is_cached(task->path)) {
            task->state = TASK_FETCHING;
            task->cache_read = sk_fs_cache_read_begin(task->path); /* resolves on a later tick */
            continue;
        }
        if (sk_asset_fetching >= MAX_FETCHES) {
            continue; /* waits for a download to finish */
        }
        start_fetch(i); /* miss (or forced): download, cache, resolve on later ticks */
#else
        /* Desktop has no network fetcher yet, so FORCE_FETCH is a no-op: resolve
         * from the jailed local fs (miss = failure). Network fallback is TODO. */
        if (!sk_fs_exists(task->path) && use_fallback(task)) continue;
        resolved(i, sk_fs_exists(task->path));
#endif
    }
    load();
}
/* Tasks not finished yet (queued, downloading or waiting on dependencies).
 * Exported on web so tools/webcheck.mjs can tell when an example is done loading. */
SK_KEEP
int sk_asset_pending_count(void)
{
    int count = 0;
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) {
        count += sk_asset_pool.occupied[i] ? 1 : 0;
    }
    return count;
}

void sk_asset_deinit(void)
{
    sk_asset_job_t job;

    sk_asset_ready = false;
    /* Loads still in progress are dropped: queued jobs, prepared data, and resources
     * partly finished (their loader's discard releases what it created).
     * On web this runs on the browser's main thread when the app quits, where
     * waiting for a worker blocks the page (for as long as a prepare takes), so the
     * workers are detached instead: they end on their own, and whatever they finish
     * preparing afterwards is dropped with the page. */
#ifdef __EMSCRIPTEN__
    const bool wait = false;
#else
    const bool wait = true;
#endif
    stop_workers(wait);
    sk_mutex_lock(&sk_asset_jobs.lock);
    sk_asset_jobs.queue.count = 0;
    while (pop_job(&sk_asset_jobs.done, &job)) {
        if (job.prepared != NULL) job.loader->discard(job.prepared);
    }
    sk_mutex_unlock(&sk_asset_jobs.lock);
    for (uint16_t i = 1; i < sk_asset_pool.capacity; i++) {
        sk_asset_task_t *task = &sk_asset_tasks[i];
        if (!sk_asset_pool.occupied[i]) continue;
        if (task->prepared != NULL) {
            task->loader->discard(task->prepared);
            task->prepared = NULL;
        }
        for (int h = 0; h < task->held_count; h++) {
            task->held[h].loader->release(task->held[h].resource);
        }
        free(task->held);
        task->held = NULL;
        task->held_count = 0;
    }
    if (wait) {
        sk_cond_destroy(&sk_asset_jobs.wake);
        sk_mutex_destroy(&sk_asset_jobs.lock);
        sk_asset_jobs.lock_live = false;
    }
#ifdef __EMSCRIPTEN__
    sfetch_shutdown();
#endif
    sk_handle_pool_destroy(&sk_asset_pool);
}
