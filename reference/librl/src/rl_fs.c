#include "rl_fs.h"
#include "rl_asset.h"
#include "rl_logger.h"

#include <raylib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "fileio/fileio.h"
#include "fileio/fileio_common.h"
#include "fetch_url/fetch_url.h"
#include "lru_cache/lru_cache.h"
#include "path/path.h"
#include "json/json.h"
#include "rl_scratch.h"
#include "internal/exports.h"
#include "internal/rl_handle_pool.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define RL_FS_DEFAULT_ROOT_DIR "cache"
#ifndef RL_FILEIO_DEFAULT_ASSET_HOST
#define RL_FILEIO_DEFAULT_ASSET_HOST "https://localhost:4444"
#endif
#define RL_FILEIO_LRU_MAX_BYTES (32u * 1024u * 1024u)
#define RL_FILEIO_LRU_MAX_ENTRIES 256u
#define RL_FILEIO_LRU_MAX_ENTRY_BYTES (8u * 1024u * 1024u)
#define RL_ASSET_MAX_HOST_LENGTH 256u
#define RL_FILEIO_FETCH_TIMEOUT_MS 5000
#define RL_FILEIO_HOST_PROBE_TIMEOUT_MS 1000
#define RL_FILEIO_RESTORE_TIMEOUT_MS 5000
#define RL_FILEIO_FLUSH_TIMEOUT_MS 5000
#define RL_FILEIO_MAX_TASK_HANDLES 256

static bool rl_fs_initialized = false;
static lru_cache_t *rl_fs_memory_cache = NULL;
static char rl_asset_host_buf[RL_ASSET_MAX_HOST_LENGTH] = RL_FILEIO_DEFAULT_ASSET_HOST;
static char rl_fs_root_dir[FILEIO_MAX_PATH_LENGTH * 2] = RL_FS_DEFAULT_ROOT_DIR;
static fileio_sync_op_t *rl_fs_restore_barrier = NULL;
static bool rl_fs_restore_ready = false;
static bool rl_fs_restore_failed = false;
static clock_t rl_fs_restore_started_at = 0;

typedef struct rl_fileio_task rl_asset_task_t;

static rl_asset_task_t *rl_asset_task_entries[RL_FILEIO_MAX_TASK_HANDLES];
static rl_handle_pool_t rl_asset_task_pool;
static uint16_t rl_asset_task_free_indices[RL_FILEIO_MAX_TASK_HANDLES];
static uint16_t rl_asset_task_generations[RL_FILEIO_MAX_TASK_HANDLES];
static unsigned char rl_asset_task_occupied[RL_FILEIO_MAX_TASK_HANDLES];
static bool rl_asset_task_pool_ready = false;

#ifdef __EMSCRIPTEN__
EM_ASYNC_JS(int, rl_fs_wait_for_idbfs_sync_js, (int timeout_ms), {
    const start = (typeof performance !== "undefined" && performance.now)
        ? performance.now()
        : Date.now();

    while (Module && Module.fileio_idbfs_syncing) {
        const now = (typeof performance !== "undefined" && performance.now)
            ? performance.now()
            : Date.now();
        if ((now - start) >= timeout_ms) {
            return 1;
        }
        await new Promise((resolve) => setTimeout(resolve, 16));
    }

    return 0;
});
#endif

static void init_asset_host_from_env(void)
{
    const char *env_asset_host = getenv("RL_ASSET_HOST");

    if (env_asset_host == NULL || env_asset_host[0] == '\0') {
        return;
    }

    rl_asset_set_host(env_asset_host);
}

typedef enum
{
    RL_FILEIO_TASK_KIND_NONE = 0,
    RL_FILEIO_TASK_KIND_RESTORE_FS = 1,
    RL_FILEIO_TASK_KIND_IMPORT_ASSET = 2,
    RL_FILEIO_TASK_KIND_IMPORT_ASSETS = 3,
    RL_FILEIO_TASK_KIND_FLUSH = 4
} rl_fileio_task_kind_t;

typedef enum
{
    RL_FILEIO_PREPARE_STATE_INIT = 0,
    RL_FILEIO_PREPARE_STATE_FETCHING_ROOT = 1,
    RL_FILEIO_PREPARE_STATE_PARSING_ROOT = 2,
    RL_FILEIO_PREPARE_STATE_FETCHING_DEPENDENCY = 3,
    RL_FILEIO_PREPARE_STATE_DONE = 4
} rl_fileio_prepare_state_t;

struct rl_fileio_task
{
    rl_fileio_task_kind_t kind;
    int status;
    bool done;
    bool should_cache_in_memory;
    rl_fileio_prepare_state_t prepare_state;
    char fetch_host[RL_ASSET_MAX_HOST_LENGTH];
    char resolved_path[FILEIO_MAX_PATH_LENGTH * 2];
    char root_dir[FILEIO_MAX_PATH_LENGTH * 2];
    char pending_fetch_path[FILEIO_MAX_PATH_LENGTH * 2];
    /* when non-empty, fetched data is written here instead of pending_fetch_path
     * (used when ensure src URL differs from local_path) */
    char write_path[FILEIO_MAX_PATH_LENGTH * 2];
    char **dependency_paths;
    size_t dependency_count;
    size_t dependency_index;
    char **batch_paths;
    size_t batch_count;
    size_t batch_index;
    rl_asset_task_t *child_task;
    fileio_sync_op_t *fileio_op;
    fetch_url_op_t *fetch_op;
};

static void mark_task_complete_ptr(rl_asset_task_t *task, int status);
static int cache_local_file_if_needed(const char *resolved_path);
static void fill_parent_dir_path(const char *resolved_path, char *buffer, size_t buffer_size);
static int start_task_fetch(rl_asset_task_t *task, const char *path);
static rl_asset_task_t *restore_task_async_ptr(void);
static rl_asset_task_t *ensure_task_async_ptr(const char *local_path, const char *src);
static rl_asset_task_t *ensure_group_task_async_ptr(const char *const *filenames, size_t filename_count);
static rl_asset_task_t *ensure_many_from_scratch_task_async_ptr(size_t filename_count);
static bool poll_task_ptr(rl_asset_task_t *task);
static const char *lookup_task_path_ptr(rl_asset_task_t *task);
static void free_task_ptr(rl_asset_task_t *task);
static void flush_fileio_before_deinit(void);

static void init_asset_task_pool_once(void)
{
    if (rl_asset_task_pool_ready) {
        return;
    }

    rl_handle_pool_init(&rl_asset_task_pool,
                        RL_HANDLE_KIND_ASSET_TASK,
                        RL_FILEIO_MAX_TASK_HANDLES,
                        rl_asset_task_free_indices,
                        RL_FILEIO_MAX_TASK_HANDLES,
                        rl_asset_task_generations,
                        rl_asset_task_occupied);
    memset(rl_asset_task_entries, 0, sizeof(rl_asset_task_entries));
    rl_asset_task_pool_ready = true;
}

static rl_handle_t register_task_ptr(rl_asset_task_t *task)
{
    rl_handle_t handle = 0;
    uint16_t index = 0;

    if (task == NULL) {
        return 0;
    }

    init_asset_task_pool_once();
    handle = rl_handle_pool_alloc(&rl_asset_task_pool);
    if (handle == 0) {
        free_task_ptr(task);
        return 0;
    }

    if (!rl_handle_pool_resolve(&rl_asset_task_pool, handle, &index)) {
        rl_handle_pool_free(&rl_asset_task_pool, handle);
        free_task_ptr(task);
        return 0;
    }

    rl_asset_task_entries[index] = task;
    return handle;
}

static rl_asset_task_t *resolve_task_ptr(rl_handle_t handle)
{
    uint16_t index = 0;

    if (handle == 0 || !rl_asset_task_pool_ready) {
        return NULL;
    }
    if (!rl_handle_pool_resolve(&rl_asset_task_pool, handle, &index)) {
        return NULL;
    }
    return rl_asset_task_entries[index];
}

static rl_asset_task_t *take_task_ptr(rl_handle_t handle)
{
    rl_asset_task_t *task = NULL;
    uint16_t index = 0;

    if (handle == 0 || !rl_asset_task_pool_ready) {
        return NULL;
    }
    if (!rl_handle_pool_resolve(&rl_asset_task_pool, handle, &index)) {
        return NULL;
    }

    task = rl_asset_task_entries[index];
    rl_asset_task_entries[index] = NULL;
    rl_handle_pool_free(&rl_asset_task_pool, handle);
    return task;
}

static void free_all_task_handles(void)
{
    uint16_t i = 0;

    if (!rl_asset_task_pool_ready) {
        return;
    }

    for (i = 1; i < RL_FILEIO_MAX_TASK_HANDLES; i++) {
        if (rl_asset_task_entries[i] != NULL) {
            free_task_ptr(rl_asset_task_entries[i]);
            rl_asset_task_entries[i] = NULL;
        }
    }
    rl_handle_pool_reset(&rl_asset_task_pool);
}

static bool is_http_url(const char *path)
{
    if (!path) {
        return false;
    }
    return (strncmp(path, "http://", 7) == 0) || (strncmp(path, "https://", 8) == 0);
}

static bool split_url_host_and_path(const char *url, char *host, size_t host_size, char *path, size_t path_size)
{
    const char *scheme = strstr(url, "://");
    const char *path_start = NULL;
    const char *query_start = NULL;
    size_t host_len = 0;
    size_t path_len = 0;

    if (!scheme) {
        return false;
    }

    path_start = strchr(scheme + 3, '/');
    if (!path_start) {
        return false;
    }

    host_len = (size_t)(path_start - url);
    if (host_len + 1 > host_size) {
        return false;
    }
    memcpy(host, url, host_len);
    host[host_len] = '\0';

    path_start++;
    query_start = strpbrk(path_start, "?#");
    if (query_start) {
        path_len = (size_t)(query_start - path_start);
    } else {
        path_len = strlen(path_start);
    }

    if (path_len == 0 || path_len + 1 > path_size) {
        return false;
    }
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';

    return true;
}

static const char *strip_leading_slash(const char *path)
{
    if (path && path[0] == '/') {
        return path + 1;
    }
    return path;
}

static void trim_trailing_slashes(const char *input, char *output, size_t output_size)
{
    size_t len = 0;

    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL || input[0] == '\0') {
        return;
    }

    if (snprintf(output, output_size, "%s", input) >= (int)output_size) {
        output[0] = '\0';
        return;
    }

    len = strlen(output);
    while (len > 1 && output[len - 1] == '/') {
        output[len - 1] = '\0';
        len--;
    }
}

static const char *find_path_extension(const char *path)
{
    const char *dot = NULL;
    const char *slash = NULL;
    if (!path) {
        return NULL;
    }

    dot = strrchr(path, '.');
    if (!dot) {
        return NULL;
    }

    slash = strrchr(path, '/');
    if (slash && dot < slash) {
        return NULL;
    }

    return dot;
}

static bool is_extension_equal(const char *ext, const char *target)
{
    size_t i = 0;
    if (!ext || !target) {
        return false;
    }

    while (ext[i] != '\0' && target[i] != '\0')
    {
        if ((char)tolower((unsigned char)ext[i]) != (char)tolower((unsigned char)target[i])) {
            return false;
        }
        i++;
    }

    return ext[i] == '\0' && target[i] == '\0';
}

static bool is_memory_cache_path(const char *resolved_path)
{
    const char *ext = find_path_extension(resolved_path);
    if (!ext) {
        return false;
    }

    // Keep cache selective so RAM usage is predictable.
    return is_extension_equal(ext, ".glb") ||
           is_extension_equal(ext, ".gltf") ||
           is_extension_equal(ext, ".ttf") ||
           is_extension_equal(ext, ".otf") ||
           is_extension_equal(ext, ".png");
}

static bool is_dependency_bearing_asset_path(const char *path)
{
    const char *ext = find_path_extension(path);

    return ext != NULL && is_extension_equal(ext, ".gltf");
}

static bool poll_restore_barrier(void)
{
    int restore_rc = 0;
    clock_t now = 0;
    double elapsed_ms = 0.0;

    if (rl_fs_restore_ready) {
        return true;
    }

    if (rl_fs_restore_barrier == NULL) {
        rl_fs_restore_ready = true;
        return true;
    }

    now = clock();
    if (rl_fs_restore_started_at != 0 && now != (clock_t)-1) {
        elapsed_ms = ((double)(now - rl_fs_restore_started_at) * 1000.0) / (double)CLOCKS_PER_SEC;
        if (elapsed_ms >= (double)RL_FILEIO_RESTORE_TIMEOUT_MS) {
            fileio_sync_op_free(rl_fs_restore_barrier);
            rl_fs_restore_barrier = NULL;
            rl_fs_restore_ready = true;
            rl_fs_restore_failed = true;
            log_warn("rl_fileio: cache restore timed out after %d ms; falling back to network fetch",
                     RL_FILEIO_RESTORE_TIMEOUT_MS);
            return true;
        }
    }

    if (!fileio_sync_poll(rl_fs_restore_barrier)) {
        return false;
    }

    restore_rc = fileio_sync_finish(rl_fs_restore_barrier);
    fileio_sync_op_free(rl_fs_restore_barrier);
    rl_fs_restore_barrier = NULL;
    rl_fs_restore_ready = true;
    rl_fs_restore_failed = (restore_rc != 0);
    if (rl_fs_restore_failed) {
        log_warn("rl_fileio: cache restore failed; falling back to network fetch");
    }

    return true;
}

#ifdef __EMSCRIPTEN__
static void expire_restore_barrier(void)
{
    if (rl_fs_restore_barrier != NULL) {
        fileio_sync_op_free(rl_fs_restore_barrier);
        rl_fs_restore_barrier = NULL;
    }
    rl_fs_restore_ready = true;
    rl_fs_restore_failed = true;
    log_warn("rl_fileio: cache restore timed out after %d ms; falling back to network fetch",
             RL_FILEIO_RESTORE_TIMEOUT_MS);
}
#endif

static int wait_until_fileio_ready(void)
{
    if (rl_fs_restore_ready || rl_fs_restore_barrier == NULL) {
        rl_fs_restore_ready = true;
        return 0;
    }

#ifdef __EMSCRIPTEN__
    if (rl_fs_wait_for_idbfs_sync_js(RL_FILEIO_RESTORE_TIMEOUT_MS) != 0) {
        expire_restore_barrier();
        return 0;
    }
    (void)poll_restore_barrier();
    return 0;
#else
    while (!poll_restore_barrier()) {
    }
    return 0;
#endif
}

static void flush_fileio_before_deinit(void)
{
    fileio_sync_op_t *flush_op = NULL;
    int flush_rc = 0;

    if (rl_fs_restore_barrier != NULL) {
        (void)wait_until_fileio_ready();
    }

    flush_op = fileio_flush_async();
    if (flush_op == NULL) {
        log_warn("rl_fileio: failed to start fileio flush before deinit");
        return;
    }

#ifdef __EMSCRIPTEN__
    if (rl_fs_wait_for_idbfs_sync_js(RL_FILEIO_FLUSH_TIMEOUT_MS) != 0) {
        log_warn("rl_fileio: fileio flush timed out after %d ms during deinit",
                 RL_FILEIO_FLUSH_TIMEOUT_MS);
        fileio_sync_op_free(flush_op);
        return;
    }
#else
    while (!fileio_sync_poll(flush_op)) {
    }
#endif

    flush_rc = fileio_sync_finish(flush_op);
    fileio_sync_op_free(flush_op);
    if (flush_rc != 0) {
        log_warn("rl_fileio: fileio flush failed during deinit");
    }
}

static int prepare_single_asset(rl_asset_task_t *task)
{
    const char *local_check = NULL;

    if (!task) {
        return -1;
    }

    local_check = task->write_path[0] ? task->write_path : task->resolved_path;
    task->should_cache_in_memory = is_memory_cache_path(local_check);

    if (fileio_exists(local_check)) {
        mark_task_complete_ptr(task, cache_local_file_if_needed(local_check));
        return 0;
    }

    if (start_task_fetch(task, task->resolved_path) != 0) {
        return -1;
    }
    task->prepare_state = RL_FILEIO_PREPARE_STATE_FETCHING_ROOT;
    return 0;
}

static int prepare_import_asset(rl_asset_task_t *task)
{
    if (!task) {
        return -1;
    }

    task->should_cache_in_memory = is_memory_cache_path(task->resolved_path);

    if (!is_dependency_bearing_asset_path(task->resolved_path)) {
        return prepare_single_asset(task);
    }

    fill_parent_dir_path(task->resolved_path, task->root_dir, sizeof(task->root_dir));

    if (fileio_exists(task->resolved_path)) {
        task->prepare_state = RL_FILEIO_PREPARE_STATE_PARSING_ROOT;
        return 0;
    }

    if (start_task_fetch(task, task->resolved_path) != 0) {
        return -1;
    }
    task->prepare_state = RL_FILEIO_PREPARE_STATE_FETCHING_ROOT;
    return 0;
}

static void mark_task_complete_ptr(rl_asset_task_t *task, int status)
{
    if (!task) {
        return;
    }

    task->status = status;
    task->done = true;
    task->prepare_state = RL_FILEIO_PREPARE_STATE_DONE;
}

static void copy_memory_cache_if_needed(const char *resolved_path,
                                                  const unsigned char *data,
                                                  size_t size)
{
    if (!resolved_path || !data || size == 0) {
        return;
    }

    if (rl_fs_memory_cache == NULL) {
        return;
    }

    if (!is_memory_cache_path(resolved_path)) {
        return;
    }

    if (size > RL_FILEIO_LRU_MAX_ENTRY_BYTES) {
        return;
    }

    lru_cache_put_copy(rl_fs_memory_cache, resolved_path, data, size);
}

static int cache_local_file_if_needed(const char *resolved_path)
{
    fileio_read_result_t result = {0};

    if (!resolved_path || resolved_path[0] == '\0') {
        return -1;
    }

    if (!is_memory_cache_path(resolved_path) || rl_fs_memory_cache == NULL) {
        return fileio_exists(resolved_path) ? 0 : -1;
    }

    result = fileio_read(resolved_path);
    if (result.error != 0 || result.data == NULL || result.size == 0) {
        if (result.data) {
            free(result.data);
        }
        return -1;
    }

    copy_memory_cache_if_needed(resolved_path, result.data, result.size);
    free(result.data);
    return 0;
}

static void fill_parent_dir_path(const char *resolved_path, char *buffer, size_t buffer_size)
{
    const char *slash = NULL;
    size_t dir_len = 0;

    if (!buffer || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!resolved_path || resolved_path[0] == '\0') {
        return;
    }

    slash = strrchr(resolved_path, '/');
    if (!slash) {
        return;
    }

    dir_len = (size_t)(slash - resolved_path);
    if (dir_len >= buffer_size) {
        dir_len = buffer_size - 1;
    }

    memcpy(buffer, resolved_path, dir_len);
    buffer[dir_len] = '\0';
}

static int resolve_prepare_target(const char *filename,
                                            char *host,
                                            size_t host_size,
                                            char *resolved_path,
                                            size_t resolved_path_size)
{
    const char *relative_path = NULL;

    if (!filename || !host || !resolved_path || host_size == 0 || resolved_path_size == 0) {
        return -1;
    }

    if (is_http_url(filename)) {
        return split_url_host_and_path(filename, host, host_size, resolved_path, resolved_path_size) ? 0 : -1;
    }

    relative_path = strip_leading_slash(filename);
    if (!relative_path || relative_path[0] == '\0') {
        return -1;
    }

    if (snprintf(host, host_size, "%s", rl_asset_host_buf) >= (int)host_size) {
        return -1;
    }
    if (snprintf(resolved_path, resolved_path_size, "%s", relative_path) >= (int)resolved_path_size) {
        return -1;
    }

    return 0;
}

static int append_dependency_path(rl_asset_task_t *task, const char *dependency_path)
{
    char **next_paths = NULL;
    char *copy = NULL;

    if (!task || !dependency_path || dependency_path[0] == '\0') {
        return -1;
    }

    copy = (char *)malloc(strlen(dependency_path) + 1);
    if (!copy) {
        return -1;
    }
    strcpy(copy, dependency_path);

    next_paths = (char **)realloc(task->dependency_paths, sizeof(char *) * (task->dependency_count + 1));
    if (!next_paths) {
        free(copy);
        return -1;
    }

    task->dependency_paths = next_paths;
    task->dependency_paths[task->dependency_count++] = copy;
    return 0;
}

static bool is_skipped_dependency_uri(const char *uri)
{
    if (!uri || uri[0] == '\0') {
        return true;
    }

    return strncmp(uri, "data:", 5) == 0;
}

static int map_dependency_uri_to_local_path(const char *root_dir,
                                                  const char *uri,
                                                  char *resolved_path,
                                                  size_t resolved_path_size)
{
    char joined_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};

    if (!uri || !resolved_path || resolved_path_size == 0) {
        return -1;
    }

    if (is_skipped_dependency_uri(uri)) {
        return 1;
    }

    if (is_http_url(uri)) {
        char host[RL_ASSET_MAX_HOST_LENGTH] = {0};
        return split_url_host_and_path(uri, host, sizeof(host), resolved_path, resolved_path_size) ? 0 : -1;
    }

    if (root_dir && root_dir[0] != '\0') {
        path_join(root_dir, uri, joined_path, sizeof(joined_path));
    } else {
        snprintf(joined_path, sizeof(joined_path), "%s", uri);
    }
    path_normalize(joined_path, resolved_path, resolved_path_size);
    return resolved_path[0] != '\0' ? 0 : -1;
}

static int collect_gltf_dependency_uris(rl_asset_task_t *task,
                                                  const unsigned char *json_bytes,
                                                  size_t json_size)
{
    json_value_t *root = NULL;
    const json_value_t *buffers = NULL;
    const json_value_t *images = NULL;
    const json_value_t *entry = NULL;
    int index = 0;

    if (!task || !json_bytes || json_size == 0) {
        return -1;
    }

    root = json_parse_with_length((const char *)json_bytes, json_size);
    if (!root) {
        return -1;
    }

    buffers = json_object_get(root, "buffers");
    if (json_is_array(buffers)) {
        for (index = 0; index < json_array_size(buffers); ++index) {
            const json_value_t *uri = NULL;
            char resolved_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};
            int rc = 0;

            entry = json_array_get(buffers, index);
            uri = json_object_get(entry, "uri");
            if (!json_is_string(uri) || json_get_string(uri) == NULL) {
                continue;
            }

            rc = map_dependency_uri_to_local_path(task->root_dir,
                                                        json_get_string(uri),
                                                        resolved_path,
                                                        sizeof(resolved_path));
            if (rc == 1) {
                continue;
            }
            if (rc != 0 || append_dependency_path(task, resolved_path) != 0) {
                json_delete(root);
                return -1;
            }
        }
    }

    images = json_object_get(root, "images");
    if (json_is_array(images)) {
        for (index = 0; index < json_array_size(images); ++index) {
            const json_value_t *uri = NULL;
            char resolved_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};
            int rc = 0;

            entry = json_array_get(images, index);
            uri = json_object_get(entry, "uri");
            if (!json_is_string(uri) || json_get_string(uri) == NULL) {
                continue;
            }

            rc = map_dependency_uri_to_local_path(task->root_dir,
                                                        json_get_string(uri),
                                                        resolved_path,
                                                        sizeof(resolved_path));
            if (rc == 1) {
                continue;
            }
            if (rc != 0 || append_dependency_path(task, resolved_path) != 0) {
                json_delete(root);
                return -1;
            }
        }
    }

    json_delete(root);
    return 0;
}

static int start_task_fetch(rl_asset_task_t *task, const char *path)
{
    char fetch_host[RL_ASSET_MAX_HOST_LENGTH] = {0};
    char full_url[512];

    if (!task || !path || path[0] == '\0') {
        return -1;
    }

    if (task->fetch_op != NULL) {
        fetch_url_op_free(task->fetch_op);
        task->fetch_op = NULL;
    }

    if (path[0] == '/') {
        return -1;
    }

    trim_trailing_slashes(task->fetch_host, fetch_host, sizeof(fetch_host));
    if (fetch_host[0] == '\0') {
        return -1;
    }

    snprintf(full_url, sizeof(full_url), "%s/%s", fetch_host, path);
    if (fetch_url_head(full_url, RL_FILEIO_FETCH_TIMEOUT_MS) != 0) {
        return -1;
    }

    snprintf(task->pending_fetch_path, sizeof(task->pending_fetch_path), "%s", path);
    task->fetch_op = fetch_url_with_path_async(fetch_host, path, RL_FILEIO_FETCH_TIMEOUT_MS);
    return task->fetch_op != NULL ? 0 : -1;
}

static int apply_fetch_completion(rl_asset_task_t *task, fetch_url_result_t *fetch_result)
{
    if (!task || !fetch_result) {
        return -1;
    }

    if (fetch_result->code != 200 || fetch_result->data == NULL || fetch_result->size == 0) {
        free(fetch_result->data);
        fetch_result->data = NULL;
        return -1;
    }

    {
        const char *write_dest = task->write_path[0] ? task->write_path : task->pending_fetch_path;
        if (fileio_write(write_dest, fetch_result->data, fetch_result->size) != 0) {
            free(fetch_result->data);
            fetch_result->data = NULL;
            return -1;
        }
        copy_memory_cache_if_needed(write_dest,
                                              (const unsigned char *)fetch_result->data,
                                              fetch_result->size);
    }
    return 0;
}

static rl_asset_task_t *import_asset_auto(const char *filename)
{
    return ensure_task_async_ptr(filename, NULL);
}

static int clear_dir_contents(const char *abs_dir, const char *rel_dir)
{
    struct stat st;
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    if (stat(abs_dir, &st) != 0)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        if (!rel_dir || rel_dir[0] == '\0')
        {
            return -1;
        }
        return fileio_rmfile(rel_dir);
    }

    dir = opendir(abs_dir);
    if (!dir)
    {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char abs_child[FILEIO_MAX_PATH_LENGTH * 2];
        char rel_child[FILEIO_MAX_PATH_LENGTH * 2];
        int child_rc = 0;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(abs_child, sizeof(abs_child), "%s/%s", abs_dir, entry->d_name);
        abs_child[sizeof(abs_child) - 1] = '\0';

        if (rel_dir && rel_dir[0] != '\0')
        {
            snprintf(rel_child, sizeof(rel_child), "%s/%s", rel_dir, entry->d_name);
        }
        else
        {
            snprintf(rel_child, sizeof(rel_child), "%s", entry->d_name);
        }
        rel_child[sizeof(rel_child) - 1] = '\0';

        child_rc = clear_dir_contents(abs_child, rel_child);
        if (child_rc != 0)
        {
            closedir(dir);
            return child_rc;
        }
    }

    closedir(dir);

    if (rel_dir && rel_dir[0] != '\0')
    {
        if (fileio_rmdir(rel_dir) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static unsigned char *load_file_data_cb(const char *file_name, int *data_size)
{
    fileio_read_result_t result = {0};
    const char *resolved_path = NULL;
    char parsed_asset_host[256] = {0};
    char parsed_path[512] = {0};
    bool should_cache_in_memory = false;
    size_t cached_size = 0;
    unsigned char *cached_data = NULL;

    if (data_size) {
        *data_size = 0;
    }

    if (!file_name) {
        return NULL;
    }

    // Step 1: Resolve host/path. Explicit URL overrides default asset host.
    if (is_http_url(file_name)) {
        if (!split_url_host_and_path(file_name, parsed_asset_host, sizeof(parsed_asset_host), parsed_path, sizeof(parsed_path))) {
            return NULL;
        }
        resolved_path = parsed_path;
    } else {
        resolved_path = strip_leading_slash(file_name);
    }

    should_cache_in_memory = is_memory_cache_path(resolved_path);

    // Step 2: Cache-first read from in-memory LRU for selected asset types.
    if (should_cache_in_memory && rl_fs_memory_cache != NULL) {
        if (lru_cache_get_copy(rl_fs_memory_cache, resolved_path, &cached_data, &cached_size)) {
            if (cached_size > (size_t)INT_MAX) {
                free(cached_data);
                return NULL;
            }
            if (data_size) {
                *data_size = (int)cached_size;
            }
            return cached_data;
        }
    }

    // Step 3: Cache-first read from local mounted storage.
    result = fileio_read(resolved_path);

    // Step 4: If load failed, signal raylib failure for this path.
    if (result.error != 0 || !result.data || result.size == 0) {
        if (result.data) {
            free(result.data);
        }
        return NULL;
    }

    if (should_cache_in_memory && rl_fs_memory_cache != NULL && result.size <= RL_FILEIO_LRU_MAX_ENTRY_BYTES) {
        lru_cache_put_copy(rl_fs_memory_cache, resolved_path, result.data, result.size);
    }

    if (data_size) {
        if (result.size > (size_t)INT_MAX) {
            free(result.data);
            return NULL;
        }
        *data_size = (int)result.size;
    }

    return result.data;
}

int rl_asset_set_host(const char *asset_host)
{
    const char *next_asset_host = asset_host;
    size_t asset_host_len = 0;

    if (!next_asset_host || next_asset_host[0] == '\0') {
        next_asset_host = RL_FILEIO_DEFAULT_ASSET_HOST;
    }

    asset_host_len = strlen(next_asset_host);
    if (asset_host_len + 1 > sizeof(rl_asset_host_buf)) {
        return -1;
    }

    memcpy(rl_asset_host_buf, next_asset_host, asset_host_len + 1);
    return 0;
}

const char *rl_asset_get_host(void)
{
    return rl_asset_host_buf;
}

float rl_asset_ping_host(const char *asset_host)
{
    const char *probe_host = asset_host;
    char fetch_host[RL_ASSET_MAX_HOST_LENGTH] = {0};

    if (probe_host == NULL || probe_host[0] == '\0') {
        probe_host = rl_asset_host_buf;
    }
    trim_trailing_slashes(probe_host, fetch_host, sizeof(fetch_host));
    if (fetch_host[0] == '\0') {
        return -1.0f;
    }

    return fetch_url_ping(fetch_host, RL_FILEIO_HOST_PROBE_TIMEOUT_MS);
}

const char *rl_fs_get_root_dir(void)
{
    return rl_fs_root_dir;
}

static rl_asset_task_t *restore_task_async_ptr(void)
{
    rl_asset_task_t *task = NULL;

    if (!rl_fs_initialized) {
        return NULL;
    }

    task = (rl_asset_task_t *)calloc(1, sizeof(rl_asset_task_t));
    if (!task) {
        return NULL;
    }

    task->kind = RL_FILEIO_TASK_KIND_RESTORE_FS;
    task->fileio_op = fileio_restore_async();
    if (!task->fileio_op) {
        free(task);
        return NULL;
    }

    return task;
}

static rl_asset_task_t *import_single_asset(const char *local_path, const char *src)
{
    rl_asset_task_t *task = NULL;
    const char *stripped = NULL;
    const char *fetch_src = NULL;

    if (!rl_fs_initialized || !local_path || local_path[0] == '\0') {
        return NULL;
    }

    task = (rl_asset_task_t *)calloc(1, sizeof(rl_asset_task_t));
    if (!task) {
        return NULL;
    }

    task->kind = RL_FILEIO_TASK_KIND_IMPORT_ASSET;
    task->prepare_state = RL_FILEIO_PREPARE_STATE_INIT;

    /* Resolve fetch URL from src (or local_path if src is NULL). */
    fetch_src = (src != NULL && src[0] != '\0') ? src : local_path;
    if (resolve_prepare_target(fetch_src,
                                         task->fetch_host,
                                         sizeof(task->fetch_host),
                                         task->resolved_path,
                                         sizeof(task->resolved_path)) != 0) {
        free(task);
        return NULL;
    }

    /* When src differs from local_path, store the local write destination. */
    if (src != NULL && src[0] != '\0') {
        stripped = strip_leading_slash(local_path);
        if (!stripped || stripped[0] == '\0') {
            free(task);
            return NULL;
        }
        if (snprintf(task->write_path, sizeof(task->write_path), "%s", stripped) >= (int)sizeof(task->write_path)) {
            free(task);
            return NULL;
        }
    }

    return task;
}

int rl_asset_ensure(const char *local_path, const char *src)
{
    char host[RL_ASSET_MAX_HOST_LENGTH] = {0};
    char fetch_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};
    char resolved_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};
    fetch_url_result_t result = {0};
    const char *stripped = NULL;
    const char *fetch_src = NULL;
    int rc = 0;

    if (!rl_fs_initialized) {
        return -1;
    }
    if (local_path == NULL || local_path[0] == '\0') {
        return -1;
    }

    /* Wait for any in-flight IDBFS restore to settle. */
    (void)wait_until_fileio_ready();

    /* Resolve local storage path from local_path. */
    stripped = strip_leading_slash(local_path);
    if (!stripped || stripped[0] == '\0') {
        return -1;
    }
    if (snprintf(resolved_path, sizeof(resolved_path), "%s", stripped) >= (int)sizeof(resolved_path)) {
        return -1;
    }

    if (fileio_exists(resolved_path)) {
        return cache_local_file_if_needed(resolved_path);
    }

    /* Resolve fetch host + URL path from src (or local_path if src is NULL). */
    fetch_src = (src != NULL && src[0] != '\0') ? src : local_path;
    if (resolve_prepare_target(fetch_src, host, sizeof(host), fetch_path, sizeof(fetch_path)) != 0) {
        return -1;
    }

    if (host[0] == '\0') {
        return -1;
    }

    rc = fetch_url_with_path_sync(host, fetch_path, RL_FILEIO_FETCH_TIMEOUT_MS, &result);
    if (rc != 200 || result.data == NULL || result.size == 0) {
        free(result.data);
        return rc != 0 ? rc : -1;
    }

    if (fileio_write(resolved_path, result.data, result.size) != 0) {
        free(result.data);
        return -1;
    }

    copy_memory_cache_if_needed(resolved_path,
                                          (const unsigned char *)result.data,
                                          result.size);
    free(result.data);
    return 0;
}

static rl_asset_task_t *ensure_task_async_ptr(const char *local_path, const char *src)
{
    rl_asset_task_t *task = NULL;
    const char *ext = NULL;
    const char *fetch_src = (src != NULL && src[0] != '\0') ? src : local_path;

    if (!rl_fs_initialized || !local_path || local_path[0] == '\0') {
        return NULL;
    }

    ext = find_path_extension(fetch_src);
    if (ext == NULL || !is_extension_equal(ext, ".gltf")) {
        return import_single_asset(local_path, src);
    }

    task = (rl_asset_task_t *)calloc(1, sizeof(rl_asset_task_t));
    if (!task) {
        return NULL;
    }

    task->kind = RL_FILEIO_TASK_KIND_IMPORT_ASSET;
    task->prepare_state = RL_FILEIO_PREPARE_STATE_INIT;

    if (resolve_prepare_target(fetch_src,
                                         task->fetch_host,
                                         sizeof(task->fetch_host),
                                         task->resolved_path,
                                         sizeof(task->resolved_path)) != 0) {
        free(task);
        return NULL;
    }

    return task;
}

static rl_asset_task_t *ensure_many_from_scratch_task_async_ptr(size_t filename_count)
{
    const char *filenames[RL_SCRATCH_MAX_STRING_TABLE_ENTRIES];
    rl_scratch_t *scratch = NULL;
    size_t i = 0;

    if (filename_count == 0 || filename_count > RL_SCRATCH_MAX_STRING_TABLE_ENTRIES) {
        return NULL;
    }

    scratch = (rl_scratch_t *)(uintptr_t)rl_scratch_get_base();
    if (scratch == NULL) {
        return NULL;
    }

    for (i = 0; i < filename_count; i++) {
        uint32_t offset = scratch->string_offsets[i];
        if (offset >= RL_SCRATCH_MAX_STRING_TABLE_BYTES) {
            return NULL;
        }
        filenames[i] = &scratch->string_bytes[offset];
    }

    return ensure_group_task_async_ptr(filenames, filename_count);
}

static rl_asset_task_t *ensure_group_task_async_ptr(const char *const *filenames, size_t filename_count)
{
    rl_asset_task_t *task = NULL;
    size_t i = 0;

    if (!rl_fs_initialized || filenames == NULL || filename_count == 0) {
        return NULL;
    }

    task = (rl_asset_task_t *)calloc(1, sizeof(rl_asset_task_t));
    if (!task) {
        return NULL;
    }

    task->kind = RL_FILEIO_TASK_KIND_IMPORT_ASSETS;
    task->batch_paths = (char **)calloc(filename_count, sizeof(char *));
    if (!task->batch_paths) {
        free(task);
        return NULL;
    }

    for (i = 0; i < filename_count; i++) {
        if (filenames[i] == NULL || filenames[i][0] == '\0') {
            free_task_ptr(task);
            return NULL;
        }

        task->batch_paths[i] = (char *)malloc(strlen(filenames[i]) + 1);
        if (!task->batch_paths[i]) {
            free_task_ptr(task);
            return NULL;
        }
        strcpy(task->batch_paths[i], filenames[i]);
    }

    task->batch_count = filename_count;
    return task;
}

static bool poll_task_ptr(rl_asset_task_t *task)
{
    fetch_url_result_t fetch_result = {0};
    fileio_read_result_t root_result = {0};
    int finish_rc = 0;

    if (!task) {
        return false;
    }

    if (task->done) {
        return true;
    }

    switch (task->kind) {
        case RL_FILEIO_TASK_KIND_RESTORE_FS:
        case RL_FILEIO_TASK_KIND_FLUSH:
            if (task->fileio_op == NULL) {
                mark_task_complete_ptr(task, -1);
                return true;
            }
            if (!fileio_sync_poll(task->fileio_op)) {
                return false;
            }
            mark_task_complete_ptr(task, fileio_sync_finish(task->fileio_op));
            return true;
        case RL_FILEIO_TASK_KIND_IMPORT_ASSET:
            if (task->prepare_state == RL_FILEIO_PREPARE_STATE_INIT) {
                if (!poll_restore_barrier()) {
                    return false;
                }
                if (prepare_import_asset(task) != 0) {
                    mark_task_complete_ptr(task, -1);
                    return true;
                }
                if (task->done) {
                    return true;
                }
            }
            if (task->prepare_state == RL_FILEIO_PREPARE_STATE_FETCHING_ROOT ||
                task->prepare_state == RL_FILEIO_PREPARE_STATE_FETCHING_DEPENDENCY) {
                if (task->fetch_op == NULL) {
                    mark_task_complete_ptr(task, -1);
                    return true;
                }
                if (!fetch_url_poll(task->fetch_op)) {
                    return false;
                }
                finish_rc = fetch_url_finish(task->fetch_op, &fetch_result);
                if (finish_rc != 0) {
                    mark_task_complete_ptr(task, -1);
                    return true;
                }
                if (apply_fetch_completion(task, &fetch_result) != 0) {
                    free(fetch_result.data);
                    mark_task_complete_ptr(task, -1);
                    return true;
                }
                free(fetch_result.data);
                if (task->prepare_state == RL_FILEIO_PREPARE_STATE_FETCHING_ROOT) {
                    task->prepare_state = RL_FILEIO_PREPARE_STATE_PARSING_ROOT;
                } else {
                    task->dependency_index++;
                    task->prepare_state = RL_FILEIO_PREPARE_STATE_PARSING_ROOT;
                }
            }

            if (task->prepare_state == RL_FILEIO_PREPARE_STATE_PARSING_ROOT) {
                if (!is_dependency_bearing_asset_path(task->resolved_path)) {
                    mark_task_complete_ptr(task, 0);
                    return true;
                }

                if (task->dependency_count == 0 && task->dependency_index == 0) {
                    root_result = fileio_read(task->resolved_path);
                    if (root_result.error != 0 || root_result.data == NULL || root_result.size == 0) {
                        free(root_result.data);
                        mark_task_complete_ptr(task, -1);
                        return true;
                    }
                    if (collect_gltf_dependency_uris(task, root_result.data, root_result.size) != 0) {
                        free(root_result.data);
                        mark_task_complete_ptr(task, -1);
                        return true;
                    }
                    free(root_result.data);
                }

                while (task->dependency_index < task->dependency_count &&
                       fileio_exists(task->dependency_paths[task->dependency_index])) {
                    cache_local_file_if_needed(task->dependency_paths[task->dependency_index]);
                    task->dependency_index++;
                }

                if (task->dependency_index >= task->dependency_count) {
                    mark_task_complete_ptr(task, 0);
                    return true;
                }

                if (start_task_fetch(task, task->dependency_paths[task->dependency_index]) != 0) {
                    mark_task_complete_ptr(task, -1);
                    return true;
                }
                task->prepare_state = RL_FILEIO_PREPARE_STATE_FETCHING_DEPENDENCY;
                return false;
            }

            if (task->prepare_state != RL_FILEIO_PREPARE_STATE_FETCHING_ROOT || task->fetch_op == NULL) {
                mark_task_complete_ptr(task, -1);
                return true;
            }
            if (!fetch_url_poll(task->fetch_op)) {
                return false;
            }
            finish_rc = fetch_url_finish(task->fetch_op, &fetch_result);
            if (finish_rc != 0) {
                mark_task_complete_ptr(task, -1);
                return true;
            }
            if (apply_fetch_completion(task, &fetch_result) != 0) {
                free(fetch_result.data);
                mark_task_complete_ptr(task, -1);
                return true;
            }
            free(fetch_result.data);
            mark_task_complete_ptr(task, 0);
            return true;
        case RL_FILEIO_TASK_KIND_IMPORT_ASSETS:
            while (task->batch_index < task->batch_count) {
                if (task->child_task == NULL) {
                    task->child_task = import_asset_auto(task->batch_paths[task->batch_index]);
                    if (task->child_task == NULL) {
                        mark_task_complete_ptr(task, -1);
                        return true;
                    }
                }

                if (!poll_task_ptr(task->child_task)) {
                    return false;
                }

                finish_rc = task->child_task->status;
                if (finish_rc != 0) {
                    mark_task_complete_ptr(task, finish_rc);
                    return true;
                }

                free_task_ptr(task->child_task);
                task->child_task = NULL;
                task->batch_index++;
            }

            mark_task_complete_ptr(task, 0);
            return true;
        default:
            mark_task_complete_ptr(task, -1);
            return true;
    }
}

static const char *lookup_task_path_ptr(rl_asset_task_t *task)
{
    if (task == NULL) {
        return NULL;
    }

    switch (task->kind) {
        case RL_FILEIO_TASK_KIND_IMPORT_ASSET:
            if (task->write_path[0] != '\0') {
                return task->write_path;
            }
            if (task->pending_fetch_path[0] != '\0') {
                return task->pending_fetch_path;
            }
            if (task->resolved_path[0] != '\0') {
                return task->resolved_path;
            }
            return NULL;
        case RL_FILEIO_TASK_KIND_IMPORT_ASSETS:
            if (task->child_task != NULL) {
                return lookup_task_path_ptr(task->child_task);
            }
            if (task->batch_index < task->batch_count) {
                return task->batch_paths[task->batch_index];
            }
            return NULL;
        default:
            return NULL;
    }
}

static void free_task_ptr(rl_asset_task_t *task)
{
    if (!task) {
        return;
    }

    if (task->fetch_op != NULL) {
        fetch_url_op_free(task->fetch_op);
    }
    if (task->child_task != NULL) {
        free_task_ptr(task->child_task);
    }
    if (task->fileio_op != NULL) {
        fileio_sync_op_free(task->fileio_op);
    }
    if (task->dependency_paths != NULL) {
        size_t i = 0;
        for (i = 0; i < task->dependency_count; i++) {
            free(task->dependency_paths[i]);
        }
        free(task->dependency_paths);
    }
    if (task->batch_paths != NULL) {
        size_t i = 0;
        for (i = 0; i < task->batch_count; i++) {
            free(task->batch_paths[i]);
        }
        free(task->batch_paths);
    }
    free(task);
}

RL_KEEP
rl_handle_t rl_fs_restore_async(void)
{
    return register_task_ptr(restore_task_async_ptr());
}

RL_KEEP
rl_handle_t rl_asset_ensure_async(const char *local_path, const char *src)
{
    return register_task_ptr(ensure_task_async_ptr(local_path, src));
}

RL_KEEP
rl_handle_t rl_asset_ensure_many_from_scratch_async(size_t filename_count)
{
    return register_task_ptr(ensure_many_from_scratch_task_async_ptr(filename_count));
}

RL_KEEP
rl_handle_t rl_asset_ensure_many_async(const char *const *filenames, size_t filename_count)
{
    return register_task_ptr(ensure_group_task_async_ptr(filenames, filename_count));
}

RL_KEEP
bool rl_asset_poll_task(rl_handle_t task)
{
    return poll_task_ptr(resolve_task_ptr(task));
}

RL_KEEP
int rl_asset_finish_task(rl_handle_t task)
{
    rl_asset_task_t *t = resolve_task_ptr(task);
    if (!t) {
        return -1;
    }
    if (!poll_task_ptr(t)) {
        return 1;
    }
    return t->status;
}

RL_KEEP
const char *rl_asset_get_task_path(rl_handle_t task)
{
    return lookup_task_path_ptr(resolve_task_ptr(task));
}

RL_KEEP
void rl_asset_free_task(rl_handle_t task)
{
    rl_asset_task_t *task_ptr = take_task_ptr(task);

    if (task_ptr != NULL) {
        free_task_ptr(task_ptr);
    }
}

bool rl_fs_exists(const char *filename)
{
    char host[RL_ASSET_MAX_HOST_LENGTH] = {0};
    char resolved_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};

    if (!rl_fs_initialized) {
        return false;
    }

    if (resolve_prepare_target(filename,
                                         host,
                                         sizeof(host),
                                         resolved_path,
                                         sizeof(resolved_path)) != 0) {
        return false;
    }

    return fileio_exists(resolved_path);
}

int rl_fs_read(const char *path, unsigned char **out_data, size_t *out_size)
{
    char resolved_path[FILEIO_MAX_PATH_LENGTH * 2] = {0};
    fileio_read_result_t result = {0};
    bool should_cache_in_memory = false;
    size_t cached_size = 0;
    unsigned char *cached_data = NULL;
    const char *stripped = NULL;

    if (!rl_fs_initialized || path == NULL || path[0] == '\0') {
        return -1;
    }
    if (out_data == NULL || out_size == NULL) {
        return -1;
    }

    stripped = strip_leading_slash(path);
    if (!stripped || stripped[0] == '\0') {
        return -1;
    }
    if (snprintf(resolved_path, sizeof(resolved_path), "%s", stripped) >= (int)sizeof(resolved_path)) {
        return -1;
    }

    should_cache_in_memory = is_memory_cache_path(resolved_path);
    if (should_cache_in_memory && rl_fs_memory_cache != NULL) {
        if (lru_cache_get_copy(rl_fs_memory_cache, resolved_path, &cached_data, &cached_size)) {
            *out_data = cached_data;
            *out_size = cached_size;
            return 0;
        }
    }

    result = fileio_read(resolved_path);
    if (result.error != 0 || result.data == NULL || result.size == 0) {
        free(result.data);
        return result.error != 0 ? result.error : -1;
    }

    *out_data = (unsigned char *)result.data;
    *out_size = result.size;
    return 0;
}

void rl_fs_read_free(unsigned char *data)
{
    free(data);
}

void rl_fs_normalize_path(const char *path, char *buffer, size_t buffer_size)
{
    if (path == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }
    path_normalize(path, buffer, buffer_size);
}

int rl_fs_remove(const char *filename)
{
    const char *resolved_path = strip_leading_slash(filename);
    int rc = 0;

    if (!resolved_path || resolved_path[0] == '\0') {
        return -1;
    }

    rc = fileio_rmfile(resolved_path);
    if (rc == 0 && rl_fs_memory_cache != NULL) {
        // Prevent stale reads from in-memory cache after on-disk removal.
        lru_cache_clear(rl_fs_memory_cache);
    }

    return rc;
}

int rl_fs_clear(void)
{
    if (!fileio_mount_point_initialized || fileio_mount_point[0] == '\0') {
        return -1;
    }

    if (clear_dir_contents(fileio_mount_point, "") != 0) {
        return -1;
    }

    if (rl_fs_memory_cache != NULL) {
        lru_cache_clear(rl_fs_memory_cache);
    }

    return 0;
}

bool rl_fs_is_ready(void)
{
    return poll_restore_barrier();
}

#define RL_FILEIO_MAX_MANAGED_TASKS 16
#define RL_FILEIO_MAX_TASK_PATH 256

typedef struct rl_fileio_managed_task_t {
    rl_asset_task_t *task;
    char path[RL_FILEIO_MAX_TASK_PATH];
    rl_asset_callback_fn on_success;
    rl_asset_callback_fn on_failure;
    void *user_data;
    bool in_use;
} rl_fileio_managed_task_t;

static rl_fileio_managed_task_t rl_fileio_managed_tasks[RL_FILEIO_MAX_MANAGED_TASKS] = {{0}};

RL_KEEP
rl_asset_add_task_result_t rl_asset_add_task(rl_handle_t task,
                                               rl_asset_callback_fn on_success,
                                               rl_asset_callback_fn on_failure,
                                               void *user_data)
{
    int i = 0;
    rl_fileio_managed_task_t *slot = NULL;
    const char *path = NULL;
    rl_asset_task_t *task_ptr = take_task_ptr(task);

    if (task_ptr == NULL) {
        if (on_failure != NULL) {
            on_failure(NULL, user_data);
        }
        return RL_ASSET_ADD_TASK_ERR_INVALID;
    }

    path = lookup_task_path_ptr(task_ptr);

    for (i = 0; i < RL_FILEIO_MAX_MANAGED_TASKS; i++) {
        if (!rl_fileio_managed_tasks[i].in_use) {
            slot = &rl_fileio_managed_tasks[i];
            break;
        }
    }

    if (slot == NULL) {
        if (on_failure != NULL) {
            on_failure(path, user_data);
        }
        free_task_ptr(task_ptr);
        return RL_ASSET_ADD_TASK_ERR_QUEUE_FULL;
    }

    slot->task = task_ptr;
    slot->path[0] = '\0';
    if (path != NULL) {
        size_t path_len = strnlen(path, sizeof(slot->path) - 1);
        memcpy(slot->path, path, path_len);
        slot->path[path_len] = '\0';
    }
    slot->on_success = on_success;
    slot->on_failure = on_failure;
    slot->user_data = user_data;
    slot->in_use = true;
    return RL_ASSET_ADD_TASK_OK;
}

void rl_asset_tick(void)
{
    int i = 0;

    poll_restore_barrier();

    for (i = 0; i < RL_FILEIO_MAX_MANAGED_TASKS; i++) {
        rl_fileio_managed_task_t *slot = &rl_fileio_managed_tasks[i];
        int rc = 0;

        if (!slot->in_use) {
            continue;
        }

        if (!poll_task_ptr(slot->task)) {
            continue;
        }

        rc = slot->task->status;

        if (rc == 0 && slot->on_success != NULL) {
            slot->on_success(slot->path, slot->user_data);
        } else if (rc != 0 && slot->on_failure != NULL) {
            slot->on_failure(slot->path, slot->user_data);
        }

        free_task_ptr(slot->task);
        slot->task = NULL;
        slot->in_use = false;
    }
}

bool rl_fs_is_initialized(void)
{
    return rl_fs_initialized;
}

int rl_fs_init_async(const char *base_dir)
{
    const char *resolved = base_dir;

    if (rl_fs_initialized) {
        return 0;
    }

    if (!resolved || resolved[0] == '\0') {
        resolved = RL_FS_DEFAULT_ROOT_DIR;
    }

    rl_fs_root_dir[0] = '\0';
    (void)snprintf(rl_fs_root_dir, sizeof(rl_fs_root_dir), "%s", resolved);

    if (fileio_init(resolved) != 0) {
        return -1;
    }

    init_asset_host_from_env();

    rl_fs_restore_barrier = fileio_restore_async();
    rl_fs_restore_ready = (rl_fs_restore_barrier == NULL);
    rl_fs_restore_failed = false;
    rl_fs_restore_started_at = clock();

    rl_fs_memory_cache = lru_cache_create(RL_FILEIO_LRU_MAX_BYTES, RL_FILEIO_LRU_MAX_ENTRIES);

    SetLoadFileDataCallback(load_file_data_cb);

    rl_fs_initialized = true;
    return 0;
}

int rl_fs_init(const char *base_dir)
{
    int rc = rl_fs_init_async(base_dir);

    if (rc != 0) {
        return rc;
    }

    return wait_until_fileio_ready();
}

void rl_fs_deinit(void)
{
    int i = 0;

    if (!rl_fs_initialized) {
        return;
    }

    for (i = 0; i < RL_FILEIO_MAX_MANAGED_TASKS; i++) {
        rl_fileio_managed_task_t *slot = &rl_fileio_managed_tasks[i];
        if (slot->in_use && slot->task != NULL) {
            free_task_ptr(slot->task);
        }
        memset(slot, 0, sizeof(*slot));
    }
    free_all_task_handles();

    SetLoadFileDataCallback(NULL);
    lru_cache_destroy(rl_fs_memory_cache);
    rl_fs_memory_cache = NULL;
    flush_fileio_before_deinit();
    rl_fs_restore_ready = false;
    rl_fs_restore_failed = false;
    rl_fs_restore_started_at = 0;
    rl_fs_root_dir[0] = '\0';
    fileio_deinit();
    rl_fs_initialized = false;
}

RL_KEEP
rl_handle_t rl_fs_deinit_async(void)
{
    rl_asset_task_t *task = NULL;
    fileio_sync_op_t *flush_op = NULL;
    int i = 0;

    if (!rl_fs_initialized) {
        return 0;
    }

    for (i = 0; i < RL_FILEIO_MAX_MANAGED_TASKS; i++) {
        rl_fileio_managed_task_t *slot = &rl_fileio_managed_tasks[i];
        if (slot->in_use && slot->task != NULL) {
            free_task_ptr(slot->task);
        }
        memset(slot, 0, sizeof(*slot));
    }
    free_all_task_handles();

    SetLoadFileDataCallback(NULL);
    lru_cache_destroy(rl_fs_memory_cache);
    rl_fs_memory_cache = NULL;

    flush_op = fileio_flush_async();
    if (flush_op == NULL) {
        rl_fs_restore_ready = false;
        rl_fs_restore_failed = false;
        rl_fs_restore_started_at = 0;
        rl_fs_root_dir[0] = '\0';
        fileio_deinit();
        rl_fs_initialized = false;
        return 0;
    }

    task = (rl_asset_task_t *)calloc(1, sizeof(rl_asset_task_t));
    if (!task) {
        fileio_sync_op_free(flush_op);
        rl_fs_restore_ready = false;
        rl_fs_restore_failed = false;
        rl_fs_restore_started_at = 0;
        rl_fs_root_dir[0] = '\0';
        fileio_deinit();
        rl_fs_initialized = false;
        return 0;
    }

    task->kind = RL_FILEIO_TASK_KIND_FLUSH;
    task->fileio_op = flush_op;

    rl_fs_restore_ready = false;
    rl_fs_restore_failed = false;
    rl_fs_restore_started_at = 0;
    rl_fs_root_dir[0] = '\0';
    rl_fs_initialized = false;

    return register_task_ptr(task);
}

RL_KEEP
int rl_fs_flush(void)
{
    fileio_sync_op_t *flush_op = NULL;
    int rc = 0;

    if (!rl_fs_initialized) {
        return -1;
    }

    (void)wait_until_fileio_ready();

    flush_op = fileio_flush_async();
    if (flush_op == NULL) {
        return -1;
    }

#ifdef __EMSCRIPTEN__
    if (rl_fs_wait_for_idbfs_sync_js(RL_FILEIO_FLUSH_TIMEOUT_MS) != 0) {
        fileio_sync_op_free(flush_op);
        return -1;
    }
#else
    while (!fileio_sync_poll(flush_op)) {
    }
#endif

    rc = fileio_sync_finish(flush_op);
    fileio_sync_op_free(flush_op);
    return rc;
}

RL_KEEP
int rl_fs_write(const char *path, const unsigned char *data, size_t size)
{
    const char *stripped = NULL;

    if (!rl_fs_initialized || path == NULL || path[0] == '\0') {
        return -1;
    }
    if (data == NULL || size == 0) {
        return -1;
    }

    stripped = strip_leading_slash(path);
    if (!stripped || stripped[0] == '\0') {
        return -1;
    }

    return fileio_write(stripped, (void *)data, size);
}

RL_KEEP
int rl_fs_mkdir(const char *path)
{
    const char *stripped = NULL;

    if (!rl_fs_initialized || path == NULL || path[0] == '\0') {
        return -1;
    }

    stripped = strip_leading_slash(path);
    if (!stripped || stripped[0] == '\0') {
        return -1;
    }

    return fileio_mkdir(stripped);
}

RL_KEEP
int rl_fs_rmdir(const char *path)
{
    const char *stripped = NULL;

    if (!rl_fs_initialized || path == NULL || path[0] == '\0') {
        return -1;
    }

    stripped = strip_leading_slash(path);
    if (!stripped || stripped[0] == '\0') {
        return -1;
    }

    return fileio_rmdir(stripped);
}
