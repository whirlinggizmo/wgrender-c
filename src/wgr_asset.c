#include "wgr_asset.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_MSC_VER) && !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR) /* MSVC has stat(), not S_IS* */
#endif

#include "internal/exports_internal.h"
#include "internal/wgr_asset_internal.h"
#include "internal/wgr_fs_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_loader_internal.h"
#include "internal/wgr_manifest_internal.h"
#include "internal/wgr_sha256_internal.h"
#include "internal/wgr_thread_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_handle.h"
#include "wgr_logger.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* One plain GET per asset, straight to the browser's fetch().
 *
 * Not a Range request, and no HEAD to size the buffer first, because a host may
 * compress: it then answers a HEAD with the *compressed* length, hands back the raw
 * gzip stream for a ranged GET, and JS cannot ask it not to -- Accept-Encoding is a
 * forbidden header the browser strips. GitHub Pages does exactly this for .glb and
 * .ttf (not .png, which is why only some assets broke). An unranged GET is decoded by
 * the browser, so the bytes are the file's and arrayBuffer's byteLength is its real
 * size -- nothing has to be known in advance and there is no per-file cap. */
#define MAX_FETCHES 256 /* downloads at once; more tasks wait */
static int wgr_asset_fetching;
#endif

/* Acquisition layer: "ensure" makes an asset locally available, then fires the
 * callback with a directly-openable local path. Storage is delegated to wgr_fs.
 *
 * Desktop: the host is a local base dir (set as the wgr_fs root); a missing file
 * is a failure, unless the host is a URL and the program supplied a fetcher. Web: the
 * host is a fetch origin — a cached file is read from the cache (IndexedDB) into the
 * local store, once the cache mode or the manifest says it is current (else the host
 * is asked first); a miss downloads the asset with fetch() and writes it into the
 * store, which keeps it with the response's metadata; then it resolves.
 * Either way the callback receives a path the sync wgr_*_create(path) creators
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
enum { FETCH_PENDING = 0, FETCH_OK, FETCH_FAILED, FETCH_USE_CACHE /* the cached copy is current */ };

typedef struct {
    char path[512];      /* logical key: cache path + default (host + path) source */
    char fetch_url[1024]; /* per-call source override (empty = use host + path) */
    bool caller_url;      /* fetch_url is the caller's (or next to it), not from a redirect */
    char origin[512];     /* the path as asked for, before redirects (dependencies: once each) */
    struct wgr_asset_candidate *candidates; /* tried in turn when `path` is missing (heap; NULL = none) */
    int candidate_count, candidate_next; /* candidates[candidate_next - 1] is `path` (0: the first) */
    int primary_count;    /* how many of the paths tried (first included) stand for `origin`; the rest for: */
    char fallback_origin[512];
    bool overlay;         /* `path` came from a redirect: missing is normal, not a warning */
    unsigned int flags;
    wgr_asset_callback_fn on_success;
    wgr_asset_callback_fn on_failure;
    void *user_data;
    bool armed; /* callbacks attached via wgr_asset_add_task */
    int state;
    int fetch_result;         /* web: FETCH_* set when the download finishes */
    int cache_read;           /* web: reading the file from the cache (wgri_fs_cache_read_begin), or 0 */
    bool revalidating;        /* web: the fetch in flight asks about a cached copy */
    /* the manifest (wgr_asset_set_manifest) */
    bool manifest_checked;    /* looked up: expect_hash is the manifest's word, or "" (not listed) */
    char expect_hash[WGRI_SHA256_TEXT]; /* what the file's bytes must hash to; "" = anything */
    int manifest_dir;         /* this task loads wgr_manifest_dirs[manifest_dir - 1]; 0 = none */
    unsigned manifest_generation;
    bool manifest_root;       /* ... and it is the root, asked about once per run */
    /* dependencies (files this file references; see internal/wgr_asset.h) */
    uint16_t parent;          /* slot of the task this one is a dependency of; 0 = none */
    int pending;              /* dependencies not finished yet */
    bool dependency_failed;
    bool dependencies_started;
    bool optional;            /* a dependency its parent can do without */
    /* loading (docs/PLAN-pipeline.md): prepare on a worker, finish on the main thread */
    char local[512];          /* the local path: the resource's name and the callback's path */
    const wgri_loader_t *loader;
    void *prepared;
    wgr_handle_t resource;     /* holds one reference until the callback has run */
    bool load_failed;
    bool refetched;           /* a cached copy was rejected once and fetched again */
    uint32_t finish_order;    /* finishes run in the order tasks were prepared */
    bool finish_started;      /* a resource being finished over several steps goes first */
    /* groups (wgr_asset_group_create): a task that completes when its members have */
    bool is_group;
    uint16_t group;           /* slot of the group this task is a member of; 0 = none */
    int dependency_count;     /* dependencies (or a group's members) added in total */
    int failed_members;
    struct wgr_asset_held *held; /* a group's members' resources, until its callbacks have run */
    int held_count, held_capacity;
} wgr_asset_task_t;

/* Where a task looks for its file (wgr_asset_add_redirect, path mappers). */
typedef struct wgr_asset_candidate {
    char path[512];
    char url[1024]; /* download source; "" = host + path */
    bool overlay;
} wgr_asset_candidate_t;

typedef struct wgr_asset_held {
    const wgri_loader_t *loader;
    wgr_handle_t resource;
} wgr_asset_held_t;

/* A prepare job for the workers, or its result. */
typedef struct {
    uint16_t slot;
    const wgri_loader_t *loader;
    char path[512];
    void *prepared;
} wgr_asset_job_t;

typedef struct {
    char extension[16];
    wgri_asset_dependencies_fn list;
} wgr_asset_format_t;

static wgr_asset_task_t *wgr_asset_tasks; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_asset_pool;
static bool wgr_asset_ready = false;
static char wgr_asset_host[256] = "";
static wgr_asset_cache_mode_t wgr_asset_cache_mode = WGR_ASSET_CACHE_REVALIDATE;

/* The manifest tree (wgr_asset_set_manifest): one record per directory whose
 * manifest.json was wanted this run, read or not. */
enum { MANIFEST_LOADING = 0, MANIFEST_READY, MANIFEST_FAILED };
typedef struct {
    char dir[512]; /* under the root manifest's directory: "" for its own, "textures", ... */
    int state;
    wgri_manifest_t manifest;
} wgr_manifest_dir_t;
static char wgr_manifest_path[512];  /* the root's logical path; "" = no manifest */
static size_t wgr_manifest_base_len; /* how much of it is its directory, with the "/" */
static unsigned wgr_manifest_generation; /* bumped when the manifest changes */
static wgr_manifest_dir_t *wgr_manifest_dirs;
static int wgr_manifest_dir_count, wgr_manifest_dir_capacity;
#ifndef __EMSCRIPTEN__
/* Desktop downloads: the host is a URL, the app supplies the downloader, and the cache
 * directory is both where a download lands and where the next run finds it -- the same
 * job the browser's cache does on web (docs/PLAN-asset-fetch.md). */
static wgr_asset_fetch_fn wgr_asset_fetcher;
static void *wgr_asset_fetcher_user;
static char wgr_asset_cache_dir[256] = ".wgr-cache";
static bool wgr_asset_host_is_url;
#endif
static wgr_asset_format_t wgr_asset_formats[MAX_DEPENDENCY_FORMATS];
static int wgr_asset_format_count;

typedef struct {
    char extension[16];
    const wgri_loader_t *loader;
} wgr_asset_loader_format_t;

static wgr_asset_loader_format_t wgr_asset_loaders[MAX_LOADER_FORMATS];
static int wgr_asset_loader_count;

/* A ring of jobs. Rings hold as many jobs as there are task slots (a task has at
 * most one job or result at a time), so they never fill up; they grow with the task
 * pool (alloc_task). */
typedef struct {
    wgr_asset_job_t *jobs;
    int capacity, head, count;
} wgr_asset_ring_t;

/* Workers and their queues. Guarded by wgr_asset_jobs.lock. The rings are never
 * freed: on web, workers detached at shutdown may still push to them. */
static struct {
    wgri_mutex_t lock;
    wgri_cond_t wake;
    wgri_thread_t threads[MAX_WORKERS];
    int worker_count;
    bool stop;
    bool lock_live;
    wgr_asset_ring_t queue;
    wgr_asset_ring_t done;
} wgr_asset_jobs;
static int wgr_asset_worker_request = -1; /* -1 = default */
static float wgr_asset_upload_budget_ms = DEFAULT_UPLOAD_BUDGET_MS;
static uint32_t wgr_asset_finish_counter;

static wgr_handle_t alloc_task(void);
#ifndef __EMSCRIPTEN__
static void resolved(uint16_t i, bool ok);            /* a task finished, well or badly */
static bool use_fallback(wgr_asset_task_t *task);     /* another candidate path to try */
#endif

static wgr_asset_task_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_asset_pool, handle, &index)) {
        return NULL;
    }
    return &wgr_asset_tasks[index];
}

void wgr_asset_set_host(const char *host)
{
    size_t n;
    if (host == NULL) host = "";
    snprintf(wgr_asset_host, sizeof(wgr_asset_host), "%s", host);
    n = strlen(wgr_asset_host);
    while (n > 1 && wgr_asset_host[n - 1] == '/') wgr_asset_host[--n] = '\0';
#ifndef __EMSCRIPTEN__
    wgr_asset_host_is_url = strncmp(wgr_asset_host, "http://", 7) == 0 ||
                            strncmp(wgr_asset_host, "https://", 8) == 0;
    /* A URL is a fetch origin, so reads resolve against the cache instead; anything
       else is the local directory it has always been. */
    wgri_fs_set_root(wgr_asset_host_is_url ? wgr_asset_cache_dir : wgr_asset_host);
#endif
}

#ifndef __EMSCRIPTEN__
/* A download the manifest lists is hashed before it counts: a match is recorded with
 * the file, a mismatch deleted (the host still serving the old file, a broken
 * deploy). Anything else counts as it is. */
static bool download_matches(wgr_asset_task_t *task)
{
    wgri_fs_meta_t meta = {0};
    unsigned char *data = NULL;
    int size = 0;
    if (task->expect_hash[0] == '\0') return true;
    if (wgri_fs_read(task->path, &data, &size)) {
        wgri_sha256_text(data, (size_t)size, meta.hash);
        wgri_fs_read_free(data);
    }
    if (strcmp(meta.hash, task->expect_hash) != 0) {
        log_warn("asset: %s isn't what the manifest lists (%.19s..., not %.19s...); not kept", task->path, meta.hash,
                 task->expect_hash);
        wgri_fs_remove(task->path);
        return false;
    }
    wgri_fs_meta_set(task->path, &meta);
    return true;
}

bool wgr_asset_fetch_done(wgr_handle_t request, bool ok)
{
    uint16_t i = 0;
    wgr_asset_task_t *task;
    if (!wgri_handle_pool_resolve(&wgr_asset_pool, request, &i)) {
        return false; /* finished, cancelled, or never ours */
    }
    task = &wgr_asset_tasks[i];
    if (task->state != TASK_FETCHING) {
        return false; /* not a request we are waiting on */
    }
    task->state = TASK_NEW;
    if (ok && wgri_fs_exists(task->path) && download_matches(task)) {
        resolved(i, true);
        return true;
    }
    if (!ok && task->manifest_root && wgri_fs_exists(task->path)) {
        log_info("asset: couldn't fetch %s; using the one from before", task->path);
        resolved(i, true);
        return true;
    }
    log_warn("asset: fetching %s failed", task->path);
    if (use_fallback(task)) {
        return true; /* another candidate to try */
    }
    resolved(i, false);
    return true;
}

bool wgr_asset_set_cache_dir(const char *dir)
{
    if (dir == NULL || *dir == '\0') {
        log_warn("wgr_asset_set_cache_dir: a directory is needed");
        return false;
    }
    snprintf(wgr_asset_cache_dir, sizeof(wgr_asset_cache_dir), "%s", dir);
    if (wgr_asset_host_is_url) {
        wgri_fs_set_root(wgr_asset_cache_dir);
    }
    return true;
}

bool wgr_asset_set_fetcher(wgr_asset_fetch_fn fn, void *user_data)
{
    wgr_asset_fetcher = fn;
    wgr_asset_fetcher_user = user_data;
    return true;
}
#else
bool wgr_asset_set_cache_dir(const char *dir)
{
    (void)dir; /* the browser caches; there is no directory to choose */
    return false;
}

bool wgr_asset_set_fetcher(wgr_asset_fetch_fn fn, void *user_data)
{
    (void)fn;
    (void)user_data; /* sokol_fetch already downloads here */
    return false;
}

bool wgr_asset_fetch_done(wgr_handle_t request, bool ok)
{
    (void)request;
    (void)ok;
    return false;
}
#endif

WGRI_KEEP
bool wgr_asset_evict(const char *path)
{
    return path != NULL && *path != '\0' && wgri_fs_remove(path);
}

void wgr_asset_clear_cache(void)
{
    wgri_fs_clear();
}

WGRI_KEEP
bool wgr_asset_set_cache_mode(wgr_asset_cache_mode_t mode)
{
    if (mode != WGR_ASSET_CACHE_REVALIDATE && mode != WGR_ASSET_CACHE_TRUST && mode != WGR_ASSET_CACHE_OFF) {
        log_warn("wgr_asset_set_cache_mode: %d isn't a cache mode", (int)mode);
        return false;
    }
    wgr_asset_cache_mode = mode;
    wgri_fs_set_persistent(mode != WGR_ASSET_CACHE_OFF);
    return true;
}

WGRI_KEEP
wgr_asset_cache_mode_t wgr_asset_get_cache_mode(void)
{
    return wgr_asset_cache_mode;
}

/* A year: how long an immutable response without a max-age stays fresh. */
#define IMMUTABLE_SECONDS (365.0 * 24.0 * 3600.0)

double wgri_asset_fresh_until(const char *cache_control, const char *age, double now)
{
    const char *p = cache_control != NULL ? cache_control : "";
    double max_age = -1.0;
    double already = 0.0;
    bool immutable = false;

    while (*p != '\0') {
        char token[64];
        size_t n = 0;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        while (*p != '\0' && *p != ',') {
            if (n + 1 < sizeof(token)) token[n++] = (char)tolower((unsigned char)*p);
            p++;
        }
        while (n > 0 && (token[n - 1] == ' ' || token[n - 1] == '\t')) n--;
        token[n] = '\0';
        /* no-cache="field" is still no-cache, as far as a whole file goes */
        if ((strncmp(token, "no-cache", 8) == 0 && (token[8] == '\0' || token[8] == '=')) ||
            strcmp(token, "no-store") == 0) {
            return 0.0;
        }
        if (strcmp(token, "immutable") == 0) {
            immutable = true;
        } else if (strncmp(token, "max-age=", 8) == 0) {
            char *end;
            const double value = strtod(token + 8, &end);
            if (end != token + 8 && *end == '\0' && value >= 0.0) max_age = value;
        }
    }
    if (max_age < 0.0) {
        return immutable ? now + IMMUTABLE_SECONDS : 0.0;
    }
    if (age != NULL && age[0] != '\0') {
        char *end;
        const double value = strtod(age, &end);
        if (end != age && value > 0.0) already = value; /* how long a shared cache held it */
    }
    return max_age > already ? now + (max_age - already) : 0.0;
}

/* ------------------------------------------------------------ manifest */

static void forget_manifests(void)
{
    for (int i = 0; i < wgr_manifest_dir_count; i++) wgri_manifest_free(&wgr_manifest_dirs[i].manifest);
    free(wgr_manifest_dirs);
    wgr_manifest_dirs = NULL;
    wgr_manifest_dir_count = wgr_manifest_dir_capacity = 0;
    wgr_manifest_generation++; /* a manifest task still in flight is for the old one */
}

WGRI_KEEP
bool wgr_asset_set_manifest(const char *path)
{
    const char *slash;
    if (path == NULL) path = "";
    if (path[0] == '/' || strstr(path, "://") != NULL || strlen(path) >= sizeof(wgr_manifest_path)) {
        log_warn("wgr_asset_set_manifest: %s isn't a path under the host", path);
        return false;
    }
    forget_manifests();
    snprintf(wgr_manifest_path, sizeof(wgr_manifest_path), "%s", path);
    slash = strrchr(wgr_manifest_path, '/');
    wgr_manifest_base_len = slash != NULL ? (size_t)(slash - wgr_manifest_path) + 1 : 0;
    return true;
}

/* The record for directory `dir`, wanted now if it wasn't before: its manifest.json
 * is ensured by a task of its own, and (but for the root) has to hash to `hash`.
 * NULL when there is no room for it. Adding a task may move the tasks. */
static wgr_manifest_dir_t *want_manifest(const char *dir, const char *hash)
{
    wgr_manifest_dir_t *record;
    wgr_asset_task_t *task;
    wgr_handle_t handle;
    char path[512];
    int written;

    for (int i = 0; i < wgr_manifest_dir_count; i++) {
        if (strcmp(wgr_manifest_dirs[i].dir, dir) == 0) return &wgr_manifest_dirs[i];
    }
    if (wgr_manifest_dir_count == wgr_manifest_dir_capacity) {
        const int capacity = wgr_manifest_dir_capacity > 0 ? wgr_manifest_dir_capacity * 2 : 16;
        wgr_manifest_dir_t *dirs = realloc(wgr_manifest_dirs, sizeof(*dirs) * (size_t)capacity);
        if (dirs == NULL) return NULL;
        wgr_manifest_dirs = dirs;
        wgr_manifest_dir_capacity = capacity;
    }
    record = &wgr_manifest_dirs[wgr_manifest_dir_count++];
    memset(record, 0, sizeof(*record));
    snprintf(record->dir, sizeof(record->dir), "%s", dir);
    written = dir[0] == '\0' ? snprintf(path, sizeof(path), "%s", wgr_manifest_path)
                             : snprintf(path, sizeof(path), "%.*s%s/manifest.json", (int)wgr_manifest_base_len,
                                        wgr_manifest_path, dir);
    handle = written > 0 && (size_t)written < sizeof(path) ? alloc_task() : 0;
    if (handle == 0) {
        record->state = MANIFEST_FAILED;
        return record;
    }
    task = resolve(handle);
    *task = (wgr_asset_task_t){0};
    snprintf(task->path, sizeof(task->path), "%s", path);
    task->flags = WGR_ASSET_FILE_ONLY;
    task->manifest_checked = true;
    snprintf(task->expect_hash, sizeof(task->expect_hash), "%s", hash);
    task->manifest_dir = wgr_manifest_dir_count;
    task->manifest_generation = wgr_manifest_generation;
    task->manifest_root = dir[0] == '\0';
    task->armed = true;
    return record;
}

enum { NOT_LISTED = 0, LISTED, LOOKING };

/* What the manifest says about a task's file: LISTED, with the hash its bytes must
 * have in `hash`; NOT_LISTED; or LOOKING while a manifest on the way to it is still
 * loading (it has been asked for). May add tasks, which may move them. */
static int manifest_lookup(uint16_t slot, char hash[WGRI_SHA256_TEXT])
{
    const wgr_asset_task_t *task = &wgr_asset_tasks[slot];
    const wgr_manifest_dir_t *record;
    char rest[512], dir[512] = "";
    char *name = rest;

    if (wgr_manifest_path[0] == '\0' || task->fetch_url[0] != '\0' ||
        strncmp(task->path, wgr_manifest_path, wgr_manifest_base_len) != 0) {
        return NOT_LISTED;
    }
#ifndef __EMSCRIPTEN__
    if (!wgr_asset_host_is_url || wgr_asset_fetcher == NULL) {
        return NOT_LISTED; /* a local directory host: its files are simply there */
    }
#endif
    snprintf(rest, sizeof(rest), "%s", task->path + wgr_manifest_base_len);
    record = want_manifest("", "");
    while (record != NULL && record->state == MANIFEST_READY) {
        char *slash = strchr(name, '/');
        char want[WGRI_SHA256_TEXT];
        const char *listed;
        if (slash == NULL) {
            listed = wgri_manifest_find(&record->manifest, name, false);
            if (listed == NULL) return NOT_LISTED;
            memcpy(hash, listed, WGRI_SHA256_TEXT);
            return LISTED;
        }
        *slash = '\0';
        listed = wgri_manifest_find(&record->manifest, name, true);
        if (listed == NULL) return NOT_LISTED;
        memcpy(want, listed, sizeof(want));
        {
            const size_t used = strlen(dir), more = strlen(name) + (used > 0 ? 1 : 0);
            if (used + more >= sizeof(dir)) return NOT_LISTED;
            if (used > 0) dir[used] = '/';
            memcpy(dir + used + (used > 0 ? 1 : 0), name, strlen(name) + 1);
        }
        record = want_manifest(dir, want);
        name = slash + 1;
    }
    return record != NULL && record->state == MANIFEST_LOADING ? LOOKING : NOT_LISTED;
}

/* A manifest task finished: read what it made local, or give that directory up
 * (its files are then cached as the mode says). */
static void manifest_loaded(const wgr_asset_task_t *task, bool ok)
{
    wgr_manifest_dir_t *record;
    unsigned char *data = NULL;
    int size = 0;

    if (task->manifest_generation != wgr_manifest_generation || task->manifest_dir > wgr_manifest_dir_count) {
        return; /* the manifest was set again meanwhile */
    }
    record = &wgr_manifest_dirs[task->manifest_dir - 1];
    record->state = MANIFEST_FAILED;
    if (!ok && task->manifest_root) { /* a host without one, as tools/serve.py is */
        log_info("asset: no manifest at %s; files are cached as the cache mode says", task->path);
        return;
    }
    if (!ok) {
        log_warn("asset: no manifest %s; what it would list is cached as the cache mode says", task->path);
        return;
    }
    if (wgri_fs_read(task->path, &data, &size) &&
        wgri_manifest_parse((const char *)data, (size_t)size, &record->manifest)) {
        record->state = MANIFEST_READY;
    } else {
        log_warn("asset: %s isn't a manifest; what it would list is cached as the cache mode says", task->path);
    }
    wgri_fs_read_free(data);
}

const char *wgr_asset_get_host(void)
{
    return wgr_asset_host;
}

#ifdef __EMSCRIPTEN__
/* Fetch `url` and give the bytes to wgri_asset_fetch_finished, which owns them from
 * then on, with the HTTP status (0: no answer). Errors arrive there too, with a null
 * pointer. The response's validators and Cache-Control wait in JS for
 * wgri_asset_fetch_header.
 *
 * `mode` FETCH_PLAIN: a GET the browser's cache may answer.
 * FETCH_REVALIDATE: a cached copy exists, so this asks whether it is still current.
 * On the page's own origin that is a conditional GET, with the copy's validators,
 * past the browser's cache (a 304 is the server's). On another origin the conditional
 * headers would need a CORS preflight the host may refuse, which would look like no
 * answer and keep a stale copy for good; there the browser revalidates its own cache
 * instead (no-cache) and hands back a 200 either way.
 * FETCH_CURRENT: what the host has now (no-cache), for a file the manifest says
 * changed, and hashed here (header 4) when the page may use crypto.subtle (a secure
 * context; otherwise C hashes it). */
EM_JS(void, wgri_asset_fetch_js, (int slot, const char *url_cstr, int mode, const char *etag_c,
                                  const char *modified_c), {
    const url = UTF8ToString(url_cstr);
    const init = { credentials: "same-origin" };
    if (mode === 2) init.cache = "no-cache";
    if (mode === 1) {
        let same = false;
        try { same = new URL(url, location.href).origin === location.origin; } catch (e) {}
        const etag = UTF8ToString(etag_c);
        const modified = UTF8ToString(modified_c);
        if (same && (etag || modified)) {
            init.headers = {};
            if (etag) init.headers["If-None-Match"] = etag;
            if (modified) init.headers["If-Modified-Since"] = modified;
            init.cache = "no-store";
        } else {
            init.cache = "no-cache";
        }
    }
    if (!Module.wgr_asset_headers) Module.wgr_asset_headers = new Map();
    fetch(url, init)
        .then((response) => {
            const h = response.headers;
            const headers = [h.get("ETag") || "", h.get("Last-Modified") || "", h.get("Cache-Control") || "",
                             h.get("Age") || "", ""];
            Module.wgr_asset_headers.set(slot, headers);
            if (!response.ok) {
                if (response.status !== 304) console.warn("wgr_asset: " + url + ": HTTP " + response.status);
                _wgri_asset_fetch_finished(slot, 0, 0, response.status);
                return;
            }
            return response.arrayBuffer().then((buffer) => {
                const subtle = mode === 2 && globalThis.crypto && crypto.subtle;
                return (subtle ? subtle.digest("SHA-256", buffer) : Promise.resolve(null))
                    .catch(() => null)
                    .then((digest) => [buffer, digest]);
            }).then(([buffer, digest]) => {
                if (digest) {
                    headers[4] = "sha256:" + Array.from(new Uint8Array(digest),
                                                        (b) => b.toString(16).padStart(2, "0")).join("");
                }
                /* byteLength is the decoded size, whatever the host did on the wire */
                const bytes = new Uint8Array(buffer);
                const ptr = _wgri_asset_fetch_alloc(bytes.length); /* malloc lives in C */
                if (ptr === 0) {
                    _wgri_asset_fetch_finished(slot, 0, 0, 0);
                    return;
                }
                HEAPU8.set(bytes, ptr);
                _wgri_asset_fetch_finished(slot, ptr, bytes.length, response.status);
            });
        })
        .catch((err) => {
            console.warn("wgr_asset: " + url + ": " + err);
            Module.wgr_asset_headers.delete(slot);
            _wgri_asset_fetch_finished(slot, 0, 0, 0);
        });
})

/* One of the finished response's headers into `out` (0 ETag, 1 Last-Modified,
 * 2 Cache-Control, 3 Age; and 4, the body's sha256 when JS hashed it), or "" -- also
 * for one too long for `out`, never cut. */
EM_JS(void, wgri_asset_fetch_header, (int slot, int which, char *out, int out_size), {
    const headers = Module.wgr_asset_headers && Module.wgr_asset_headers.get(slot);
    const text = headers ? headers[which] : "";
    stringToUTF8(lengthBytesUTF8(text) < out_size ? text : "", out, out_size);
})

EM_JS(void, wgri_asset_fetch_headers_done, (int slot), {
    if (Module.wgr_asset_headers) Module.wgr_asset_headers.delete(slot);
})

/* Room for a download the browser has already decoded. In C so the JS side needs no
 * exported malloc, which the closure pass would have to be told about. */
WGRI_JS_CALLED
unsigned char *wgri_asset_fetch_alloc(int size)
{
    return (unsigned char *)malloc(size > 0 ? (size_t)size : 1);
}

/* What the response said about the file, for keeping with it. */
static wgri_fs_meta_t response_meta(int slot)
{
    wgri_fs_meta_t meta;
    char cache_control[256], age[32];
    memset(&meta, 0, sizeof(meta));
    wgri_asset_fetch_header(slot, 0, meta.etag, (int)sizeof(meta.etag));
    wgri_asset_fetch_header(slot, 1, meta.last_modified, (int)sizeof(meta.last_modified));
    wgri_asset_fetch_header(slot, 2, cache_control, (int)sizeof(cache_control));
    wgri_asset_fetch_header(slot, 3, age, (int)sizeof(age));
    wgri_asset_fetch_header(slot, 4, meta.hash, (int)sizeof(meta.hash));
    wgri_asset_fetch_headers_done(slot);
    meta.fresh_until = wgri_asset_fresh_until(cache_control, age, (double)time(NULL));
    return meta;
}

/* The download finished with HTTP `status` (0: no answer): `data` is malloc'd for us
 * (null without a body). The answer decides what becomes of a cached copy
 * (wgr_asset.h, WGR_ASSET_CACHE_REVALIDATE). */
WGRI_JS_CALLED
void wgri_asset_fetch_finished(int slot, unsigned char *data, int size, int status)
{
    wgr_asset_task_t *task = &wgr_asset_tasks[slot];
    wgri_fs_meta_t meta;
    if (slot <= 0 || slot >= wgr_asset_pool.capacity || task->state != TASK_FETCHING) {
        wgri_asset_fetch_headers_done(slot);
        free(data); /* cancelled, or the task went away while it was in flight */
        return;
    }
    meta = response_meta(slot);
    if (data != NULL && status / 100 == 2 && task->expect_hash[0] != '\0') {
        /* hash before store: the copy's hash is always that of the bytes it holds */
        if (meta.hash[0] == '\0') wgri_sha256_text(data, (size_t)size, meta.hash);
        if (strcmp(meta.hash, task->expect_hash) != 0) {
            log_warn("asset: %s isn't what the manifest lists (%.19s..., not %.19s...); not kept", task->path,
                     meta.hash, task->expect_hash);
            task->fetch_result = FETCH_FAILED;
        } else {
            task->fetch_result = wgri_fs_write_meta(task->path, data, size, &meta) ? FETCH_OK : FETCH_FAILED;
        }
    } else if (data != NULL && status / 100 == 2) {
        meta.hash[0] = '\0'; /* hashed only for the manifest's files */
        task->fetch_result = wgri_fs_write_meta(task->path, data, size, &meta) ? FETCH_OK : FETCH_FAILED;
    } else if (task->revalidating && status == 304) {
        wgri_fs_meta_t kept;
        wgri_fs_meta_get(task->path, &kept);
        /* a 304 need not repeat the validators; the bytes, and so their hash, are the same */
        if (meta.etag[0] == '\0') memcpy(meta.etag, kept.etag, sizeof(meta.etag));
        if (meta.last_modified[0] == '\0') memcpy(meta.last_modified, kept.last_modified, sizeof(meta.last_modified));
        memcpy(meta.hash, kept.hash, sizeof(meta.hash));
        wgri_fs_meta_set(task->path, &meta);
        task->fetch_result = FETCH_USE_CACHE;
    } else if (task->revalidating && status / 100 == 4) {
        log_info("asset: %s is gone from the host (HTTP %d); forgetting the cached copy", task->path, status);
        wgri_fs_remove(task->path);
        task->fetch_result = FETCH_FAILED;
    } else if (task->revalidating) {
        log_debug("asset: no answer about %s (HTTP %d); using the cached copy", task->path, status);
        task->fetch_result = FETCH_USE_CACHE;
    } else {
        task->fetch_result = FETCH_FAILED;
    }
    free(data);
    if (wgr_asset_fetching > 0) wgr_asset_fetching--;
}

enum { FETCH_PLAIN = 0, FETCH_REVALIDATE, FETCH_CURRENT }; /* wgri_asset_fetch_js's modes */

/* Download the task's file; `cached` (or NULL) is the metadata of a cached copy this
 * asks about (wgri_asset_fetch_js). A file the manifest lists, and the root manifest,
 * come from the host as it is now. */
static void start_fetch(uint16_t slot, const wgri_fs_meta_t *cached)
{
    wgr_asset_task_t *task = &wgr_asset_tasks[slot];
    char joined[1024];
    const char *url;

    task->state = TASK_FETCHING;
    task->fetch_result = FETCH_PENDING;
    task->revalidating = cached != NULL;
    /* per-call override wins; otherwise the default host + key */
    if (task->fetch_url[0] != '\0') {
        url = task->fetch_url;
    } else {
        snprintf(joined, sizeof(joined), "%s/%s", wgr_asset_host, task->path);
        url = joined;
    }
    wgr_asset_fetching++;
    wgri_asset_fetch_js((int)slot, url,
                        cached != NULL                                             ? FETCH_REVALIDATE
                        : task->expect_hash[0] != '\0' || task->manifest_root ? FETCH_CURRENT
                                                                                   : FETCH_PLAIN,
                        cached != NULL ? cached->etag : "", cached != NULL ? cached->last_modified : "");
}
#endif

/* ------------------------------------------------------------ dependencies */

void wgri_asset_register_dependencies(const char *extension, wgri_asset_dependencies_fn list)
{
    if (extension == NULL || list == NULL || wgr_asset_format_count >= MAX_DEPENDENCY_FORMATS) {
        return;
    }
    snprintf(wgr_asset_formats[wgr_asset_format_count].extension, sizeof(wgr_asset_formats[0].extension), "%s",
             extension);
    wgr_asset_formats[wgr_asset_format_count++].list = list;
}

#define MAX_PATH_MAPPERS 4
static struct {
    char extension[16];
    wgri_asset_path_mapper_fn map;
} wgr_asset_mappers[MAX_PATH_MAPPERS];
static int wgr_asset_mapper_count;

void wgri_asset_register_path_mapper(const char *extension, wgri_asset_path_mapper_fn map)
{
    for (int i = 0; i < wgr_asset_mapper_count; i++) {
        if (strcmp(wgr_asset_mappers[i].extension, extension) == 0) {
            wgr_asset_mappers[i].map = map;
            return;
        }
    }
    if (wgr_asset_mapper_count >= MAX_PATH_MAPPERS || strlen(extension) >= sizeof(wgr_asset_mappers[0].extension)) {
        log_error("Can't register a path mapper for %s", extension);
        return;
    }
    snprintf(wgr_asset_mappers[wgr_asset_mapper_count].extension, sizeof(wgr_asset_mappers[0].extension), "%s",
             extension);
    wgr_asset_mappers[wgr_asset_mapper_count++].map = map;
}

/* ------------------------------------------------------------ redirects */

#define MAX_REDIRECTS 32
static struct {
    char prefix[256];
    char target[512];
    bool url; /* a download source ("scheme://..."), not another path */
} wgr_asset_redirects[MAX_REDIRECTS];
static int wgr_asset_redirect_count;

WGRI_KEEP
bool wgr_asset_add_redirect(const char *prefix, const char *target)
{
    if (prefix == NULL || target == NULL || prefix[0] == '\0' || target[0] == '\0') {
        log_warn("wgr_asset_add_redirect: needs a prefix and a target");
        return false;
    }
    if (wgr_asset_redirect_count >= MAX_REDIRECTS || strlen(prefix) >= sizeof(wgr_asset_redirects[0].prefix) ||
        strlen(target) >= sizeof(wgr_asset_redirects[0].target)) {
        log_warn("wgr_asset_add_redirect: too many redirects (%d), or too long", MAX_REDIRECTS);
        return false;
    }
    snprintf(wgr_asset_redirects[wgr_asset_redirect_count].prefix, sizeof(wgr_asset_redirects[0].prefix), "%s", prefix);
    snprintf(wgr_asset_redirects[wgr_asset_redirect_count].target, sizeof(wgr_asset_redirects[0].target), "%s", target);
    wgr_asset_redirects[wgr_asset_redirect_count].url = strstr(target, "://") != NULL;
    wgr_asset_redirect_count++;
    return true;
}

WGRI_KEEP
void wgr_asset_clear_redirects(void)
{
    wgr_asset_redirect_count = 0;
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Add `path` to `list` as the redirect rules see it: each path rule matching it,
 * newest first, then the path itself; each with the download URL of the newest URL
 * rule matching it. */
static int expand(const char *path, wgr_asset_candidate_t *list, int count, int max)
{
    for (int pass = 0; pass < 2; pass++) { /* 0: the rules' paths, 1: the path itself */
        for (int r = pass == 0 ? wgr_asset_redirect_count - 1 : -1; r >= -1 && count < max; r--) {
            wgr_asset_candidate_t *c = &list[count];
            if (pass == 0 && (r < 0 || wgr_asset_redirects[r].url || !starts_with(path, wgr_asset_redirects[r].prefix))) {
                continue;
            }
            if (pass == 0) {
                if (snprintf(c->path, sizeof(c->path), "%s%s", wgr_asset_redirects[r].target,
                             path + strlen(wgr_asset_redirects[r].prefix)) >= (int)sizeof(c->path)) {
                    continue;
                }
            } else {
                snprintf(c->path, sizeof(c->path), "%s", path);
            }
            c->overlay = pass == 0;
            c->url[0] = '\0';
            for (int u = wgr_asset_redirect_count - 1; u >= 0; u--) {
                if (wgr_asset_redirects[u].url && starts_with(c->path, wgr_asset_redirects[u].prefix)) {
                    snprintf(c->url, sizeof(c->url), "%s%s", wgr_asset_redirects[u].target,
                             c->path + strlen(wgr_asset_redirects[u].prefix));
                    break;
                }
            }
            count++;
            if (pass == 1) break;
        }
    }
    return count;
}

/* Where a task looks: `primary`, else `fallback` (or none), each through the
 * redirects. The task starts at the first; the rest wait in its candidates. */
static void plan(wgr_asset_task_t *task, const char *primary, const char *fallback)
{
    const int max = 2 * (wgr_asset_redirect_count + 1);
    wgr_asset_candidate_t *list = malloc(sizeof(wgr_asset_candidate_t) * (size_t)max); /* too big for the stack */
    int count;

    if (list == NULL) { /* out of memory: the path as it is */
        snprintf(task->path, sizeof(task->path), "%s", primary);
        return;
    }
    count = expand(primary, list, 0, max);
    task->primary_count = count;
    if (fallback != NULL && fallback[0] != '\0') {
        count = expand(fallback, list, count, max);
        snprintf(task->fallback_origin, sizeof(task->fallback_origin), "%s", fallback);
    }
    snprintf(task->path, sizeof(task->path), "%s", list[0].path);
    snprintf(task->fetch_url, sizeof(task->fetch_url), "%s", list[0].url);
    task->overlay = list[0].overlay;
    if (count > 1) { /* the rest wait, in the same buffer */
        memmove(list, &list[1], sizeof(wgr_asset_candidate_t) * (size_t)(count - 1));
        task->candidates = list;
        task->candidate_count = count - 1;
    } else {
        free(list);
    }
}

/* ----------------------------------------------------------------- ping */

#define MAX_PINGS 8
#define PING_PENDING (-2.0f)
typedef struct {
    bool active;
    int id;             /* web: the browser's request */
    float result;       /* milliseconds, -1 unreachable, PING_PENDING */
    char host[256];
    wgr_asset_ping_fn on_done;
    void *user_data;
} wgr_asset_ping_t;
static wgr_asset_ping_t wgr_asset_pings[MAX_PINGS];

#ifdef __EMSCRIPTEN__
/* A HEAD request to `url`, timed; any response counts (no-cors: a CDN needs no CORS
 * headers for this). Returns an id for wgr_asset_ping_poll. */
EM_JS(int, wgr_asset_ping_begin, (const char *url_c, int timeout_ms), {
    if (!Module.wgr_asset_pings) {
        Module.wgr_asset_pings = new Map();
        Module.wgr_asset_next_ping = 1;
    }
    const id = Module.wgr_asset_next_ping++;
    const start = performance.now();
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout_ms);
    Module.wgr_asset_pings.set(id, -2);
    fetch(UTF8ToString(url_c), {method: "HEAD", mode: "no-cors", cache: "no-store", signal: controller.signal})
        .then(() => Module.wgr_asset_pings.set(id, performance.now() - start))
        .catch(() => Module.wgr_asset_pings.set(id, -1))
        .finally(() => clearTimeout(timer));
    return id;
});

/* Milliseconds, -1 (failed), or -2 (still waiting). */
EM_JS(double, wgr_asset_ping_poll, (int id), {
    const result = Module.wgr_asset_pings ? Module.wgr_asset_pings.get(id) : undefined;
    if (result === undefined) return -1;
    if (result !== -2) Module.wgr_asset_pings.delete(id);
    return result;
});
#endif

WGRI_KEEP
bool wgr_asset_ping_host(const char *host, int timeout_ms, wgr_asset_ping_fn on_done, void *user_data)
{
    int slot = -1;
    if (!wgr_asset_ready || on_done == NULL) return false;
    for (int i = 0; i < MAX_PINGS && slot < 0; i++) {
        if (!wgr_asset_pings[i].active) slot = i;
    }
    if (slot < 0) {
        log_warn("wgr_asset_ping_host: %d pings already waiting", MAX_PINGS);
        return false;
    }
    if (host == NULL) host = wgr_asset_host;
    if (timeout_ms <= 0) timeout_ms = 5000;
    wgr_asset_pings[slot] = (wgr_asset_ping_t){.active = true, .on_done = on_done, .user_data = user_data};
    snprintf(wgr_asset_pings[slot].host, sizeof(wgr_asset_pings[slot].host), "%s", host);
#ifdef __EMSCRIPTEN__
    char url[300];
    snprintf(url, sizeof(url), "%s/", host); /* the host's root; a 404 still answers */
    wgr_asset_pings[slot].id = wgr_asset_ping_begin(url, timeout_ms);
    wgr_asset_pings[slot].result = PING_PENDING;
#else
    /* desktop: a local directory is there or it isn't. A URL would need a request of
     * its own, which the fetcher hook (files, not round trips) can't make. */
    struct stat st;
    if (strstr(host, "://") != NULL) {
        log_warn("wgr_asset_ping_host: %s: no host ping on desktop; set a fetcher and time an ensure", host);
        wgr_asset_pings[slot].result = -1.0f;
    } else {
        wgr_asset_pings[slot].result = stat(host[0] != '\0' ? host : ".", &st) == 0 && S_ISDIR(st.st_mode) ? 0.0f : -1.0f;
    }
#endif
    return true;
}

/* Report finished pings (their callbacks may start more). */
static void deliver_pings(void)
{
    for (int i = 0; i < MAX_PINGS; i++) {
        if (!wgr_asset_pings[i].active) continue;
#ifdef __EMSCRIPTEN__
        if (wgr_asset_pings[i].result == PING_PENDING) {
            const double result = wgr_asset_ping_poll(wgr_asset_pings[i].id);
            if (result == PING_PENDING) continue;
            wgr_asset_pings[i].result = (float)result;
        }
#endif
        char host[sizeof(wgr_asset_pings[i].host)];
        const wgr_asset_ping_fn on_done = wgr_asset_pings[i].on_done;
        void *user_data = wgr_asset_pings[i].user_data;
        const float result = wgr_asset_pings[i].result;
        snprintf(host, sizeof(host), "%s", wgr_asset_pings[i].host);
        wgr_asset_pings[i].active = false; /* free before the callback: it may ping again */
        on_done(host, result, user_data);
    }
}

/* Files found somewhere other than their own path (a redirect, a fallback), by local
 * path: a model reads the files it references from where they were found
 * (wgri_asset_found_path, from loading workers). Under wgr_asset_jobs.lock. */
typedef struct {
    char from[512];
    char to[512];
} wgr_asset_found_t;
static wgr_asset_found_t *wgr_asset_found;
static int wgr_asset_found_count, wgr_asset_found_capacity;

/* `origin` was found at `path` (the same path: forget any earlier redirect). */
static void record_found(const char *origin, const char *path)
{
    char from[512], to[512];
    int i;
    wgri_fs_resolve(origin, from, sizeof(from));
    wgri_fs_resolve(path, to, sizeof(to));
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    for (i = 0; i < wgr_asset_found_count && strcmp(wgr_asset_found[i].from, from) != 0; i++) {
    }
    if (strcmp(from, to) == 0) {
        if (i < wgr_asset_found_count) wgr_asset_found[i] = wgr_asset_found[--wgr_asset_found_count];
    } else {
        if (i == wgr_asset_found_count && wgr_asset_found_count == wgr_asset_found_capacity) {
            const int capacity = wgr_asset_found_capacity > 0 ? wgr_asset_found_capacity * 2 : 16;
            wgr_asset_found_t *grown = realloc(wgr_asset_found, sizeof(wgr_asset_found_t) * (size_t)capacity);
            if (grown != NULL) {
                wgr_asset_found = grown;
                wgr_asset_found_capacity = capacity;
            }
        }
        if (i < wgr_asset_found_capacity) {
            snprintf(wgr_asset_found[i].from, sizeof(wgr_asset_found[i].from), "%s", from);
            snprintf(wgr_asset_found[i].to, sizeof(wgr_asset_found[i].to), "%s", to);
            if (i == wgr_asset_found_count) wgr_asset_found_count++;
        }
    }
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
}

bool wgri_asset_found_path(const char *local, char *out, size_t out_size)
{
    bool found = false;
    snprintf(out, out_size, "%s", local != NULL ? local : "");
    if (local == NULL || !wgr_asset_jobs.lock_live) return false;
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    for (int i = 0; i < wgr_asset_found_count && !found; i++) {
        if (strcmp(wgr_asset_found[i].from, local) == 0) {
            snprintf(out, out_size, "%s", wgr_asset_found[i].to);
            found = true;
        }
    }
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
    return found;
}

void wgri_asset_register_loader(const char *extension, const wgri_loader_t *loader)
{
    for (int i = 0; i < wgr_asset_loader_count; i++) {
        if (strcmp(wgr_asset_loaders[i].extension, extension) == 0) {
            wgr_asset_loaders[i].loader = loader;
            return;
        }
    }
    if (wgr_asset_loader_count >= MAX_LOADER_FORMATS || strlen(extension) >= sizeof(wgr_asset_loaders[0].extension)) {
        log_error("Can't register a loader for %s", extension);
        return;
    }
    snprintf(wgr_asset_loaders[wgr_asset_loader_count].extension, sizeof(wgr_asset_loaders[0].extension), "%s", extension);
    wgr_asset_loaders[wgr_asset_loader_count++].loader = loader;
}

wgr_handle_t wgri_loader_create(const wgri_loader_t *loader, const char *path)
{
    wgr_handle_t resource = loader->find(path);
    void *prepared;
    wgri_loader_step_t step = WGRI_LOADER_MORE;

    if (resource != 0) {
        return resource;
    }
    prepared = loader->prepare(path);
    if (prepared == NULL) {
        return 0;
    }
    while (step == WGRI_LOADER_MORE) {
        step = loader->finish(prepared, path, &resource);
    }
    loader->discard(prepared);
    return step == WGRI_LOADER_DONE ? resource : 0;
}

bool wgri_asset_is_relative_uri(const char *uri)
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

bool wgri_asset_join_relative(const char *base_path, const char *uri, char *out, size_t out_size)
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

static const wgri_loader_t *lookup_loader(const char *path)
{
    for (int i = 0; i < wgr_asset_loader_count; i++) {
        if (has_extension(path, wgr_asset_loaders[i].extension)) return wgr_asset_loaders[i].loader;
    }
    return NULL;
}

static wgri_asset_dependencies_fn lookup_format(const char *path)
{
    const size_t path_len = strlen(path);
    for (int f = 0; f < wgr_asset_format_count; f++) {
        const size_t ext_len = strlen(wgr_asset_formats[f].extension);
        if (path_len < ext_len) continue;
        bool match = true;
        for (size_t k = 0; k < ext_len && match; k++) {
            match = tolower((unsigned char)path[path_len - ext_len + k]) ==
                    tolower((unsigned char)wgr_asset_formats[f].extension[k]);
        }
        if (match) return wgr_asset_formats[f].list;
    }
    return NULL;
}

/* A file fetched from an explicit URL finds its dependencies next to that URL (the
 * browser resolves any ".."); otherwise `out` stays empty (host + path). */
static void dependency_url(const wgr_asset_task_t *parent_task, const char *uri, char *out, size_t out_size)
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
    wgr_asset_task_t *parent_task = &wgr_asset_tasks[parent];
    char path[512];
    wgr_handle_t handle;
    wgr_asset_task_t *task;

    if (!wgri_asset_is_relative_uri(uri)) {
        return;
    }
    if (!wgri_asset_join_relative(parent_task->path, uri, path, sizeof(path))) {
        log_warn("Asset %s: can't use dependency '%s' (outside the asset root or too long)", parent_task->path, uri);
        parent_task->dependency_failed = true;
        return;
    }
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) { /* referenced twice: ensure once */
        if (wgr_asset_pool.occupied[i] && wgr_asset_tasks[i].parent == parent &&
            strcmp(wgr_asset_tasks[i].origin, path) == 0) {
            return;
        }
    }
    handle = alloc_task();
    parent_task = &wgr_asset_tasks[parent]; /* the allocation may have moved the tasks */
    if (handle == 0) {
        log_error("Asset %s: can't queue its dependency %s", parent_task->path, path);
        parent_task->dependency_failed = true;
        return;
    }
    task = resolve(handle);
    *task = (wgr_asset_task_t){0};
    snprintf(task->origin, sizeof(task->origin), "%s", path);
    char fallback[512] = "";
    if (fallback_uri != NULL && wgri_asset_is_relative_uri(fallback_uri)) {
        wgri_asset_join_relative(parent_task->path, fallback_uri, fallback, sizeof(fallback));
    }
    if (parent_task->caller_url) {
        /* a file fetched from the caller's URL: its dependencies come from next to it */
        task->caller_url = true;
        snprintf(task->path, sizeof(task->path), "%s", path);
        dependency_url(parent_task, uri, task->fetch_url, sizeof(task->fetch_url));
        task->primary_count = 1;
        snprintf(task->fallback_origin, sizeof(task->fallback_origin), "%s", fallback);
        if (fallback[0] != '\0' && (task->candidates = calloc(1, sizeof(wgr_asset_candidate_t))) != NULL) {
            snprintf(task->candidates[0].path, sizeof(task->candidates[0].path), "%s", fallback);
            dependency_url(parent_task, fallback_uri, task->candidates[0].url, sizeof(task->candidates[0].url));
            task->candidate_count = 1;
        }
    } else {
        plan(task, path, fallback);
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
    wgr_asset_task_t *task = &wgr_asset_tasks[slot];
    const wgri_asset_dependencies_fn list = lookup_format(task->path);
    unsigned char *data = NULL;
    int size = 0;
    uint16_t context = slot;

    task->dependencies_started = true;
    if (list == NULL) {
        return;
    }
    if (!wgri_fs_read(task->path, &data, &size)) {
        return; /* the resource creator reports the unreadable file */
    }
    list(data, size, add_dependency, &context);
    wgri_fs_read_free(data);
    task = &wgr_asset_tasks[slot]; /* queueing dependencies may have moved the tasks */
    if (task->pending > 0) {
        task->state = TASK_WAITING;
    }
}

WGRI_KEEP
wgr_handle_t wgr_asset_ensure_async(const char *path, const char *fetch_url,
                                  unsigned int flags)
{
    wgr_handle_t handle;
    wgr_asset_task_t *task_ptr;

    if (!wgr_asset_ready || path == NULL) {
        return 0;
    }
    handle = alloc_task();
    if (handle == 0) {
        return 0;
    }
    task_ptr = resolve(handle);
    *task_ptr = (wgr_asset_task_t){0};
    snprintf(task_ptr->origin, sizeof(task_ptr->origin), "%s", path);
    if (fetch_url != NULL) { /* the caller chose the file: no redirects or variants */
        snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", path);
        snprintf(task_ptr->fetch_url, sizeof(task_ptr->fetch_url), "%s", fetch_url);
        task_ptr->caller_url = true;
    } else {
        char primary[512], fallback[512] = "";
        snprintf(primary, sizeof(primary), "%s", path);
        for (int i = 0; i < wgr_asset_mapper_count; i++) { /* a variant chosen for this device, say */
            char mapped[sizeof(primary)];
            if (has_extension(path, wgr_asset_mappers[i].extension) &&
                wgr_asset_mappers[i].map(path, mapped, sizeof(mapped), fallback, sizeof(fallback))) {
                snprintf(primary, sizeof(primary), "%s", mapped);
                break;
            }
            fallback[0] = '\0';
        }
        plan(task_ptr, primary, fallback);
    }
    task_ptr->flags = flags;
    return handle;
}

WGRI_KEEP
wgr_asset_add_task_result_t wgr_asset_add_task(wgr_handle_t handle,
                                             wgr_asset_callback_fn on_success,
                                             wgr_asset_callback_fn on_failure,
                                             void *user_data)
{
    wgr_asset_task_t *task_ptr = resolve(handle);
    if (task_ptr == NULL) {
        return WGR_ASSET_ADD_TASK_ERR_INVALID;
    }
    task_ptr->on_success = on_success;
    task_ptr->on_failure = on_failure;
    task_ptr->user_data = user_data;
    task_ptr->armed = true;
    return WGR_ASSET_ADD_TASK_OK;
}

/* ------------------------------------------------------------- workers ---- */

static void push_job(wgr_asset_ring_t *ring, const wgr_asset_job_t *job)
{
    ring->jobs[(ring->head + ring->count) % ring->capacity] = *job; /* never full: one entry per task */
    ring->count++;
}

static bool pop_job(wgr_asset_ring_t *ring, wgr_asset_job_t *job)
{
    if (ring->count == 0) return false;
    *job = ring->jobs[ring->head];
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count--;
    return true;
}

/* Grow a ring to `capacity` jobs, keeping their order. Under wgr_asset_jobs.lock. */
static bool grow_ring(wgr_asset_ring_t *ring, int capacity)
{
    wgr_asset_job_t *jobs;
    if (ring->capacity >= capacity) return true;
    jobs = (wgr_asset_job_t *)malloc(sizeof(wgr_asset_job_t) * (size_t)capacity);
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
static wgr_handle_t alloc_task(void)
{
    const wgr_handle_t handle = wgri_handle_pool_alloc(&wgr_asset_pool);
    bool ok;
    if (handle == 0) {
        log_error("asset: too many tasks (%u)", (unsigned)wgr_asset_pool.max - 1u);
        return 0;
    }
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    ok = grow_ring(&wgr_asset_jobs.queue, wgr_asset_pool.capacity) &&
         grow_ring(&wgr_asset_jobs.done, wgr_asset_pool.capacity);
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
    if (!ok) {
        wgri_handle_pool_free(&wgr_asset_pool, handle);
        log_error("asset: out of memory");
        return 0;
    }
    return handle;
}

static void worker_main(void *arg)
{
    wgr_asset_job_t job;
    (void)arg;
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    for (;;) {
        while (!wgr_asset_jobs.stop && wgr_asset_jobs.queue.count == 0) {
            wgri_cond_wait(&wgr_asset_jobs.wake, &wgr_asset_jobs.lock);
        }
        if (wgr_asset_jobs.stop) break;
        pop_job(&wgr_asset_jobs.queue, &job);
        wgri_mutex_unlock(&wgr_asset_jobs.lock);
        job.prepared = job.loader->prepare(job.path);
        wgri_mutex_lock(&wgr_asset_jobs.lock);
        push_job(&wgr_asset_jobs.done, &job);
    }
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
}

static int default_worker_count(void)
{
    const int count = wgri_thread_cpu_count() - 1;
    if (!wgri_thread_available()) return 0;
    return count < 1 ? 1 : (count > MAX_WORKERS ? MAX_WORKERS : count);
}

static void start_workers(int count)
{
    wgr_asset_jobs.stop = false;
    wgr_asset_jobs.worker_count = 0;
    for (int i = 0; i < count && i < MAX_WORKERS; i++) {
        if (!wgri_thread_create(&wgr_asset_jobs.threads[i], worker_main, NULL)) {
            log_warn("Asset workers: started %d of %d; the rest of loading runs on the main thread", i, count);
            break;
        }
        wgr_asset_jobs.worker_count++;
    }
}

/* `wait`: join the workers (running prepares finish first). Otherwise they're
 * detached and end on their own; the job lock must then stay alive. */
static void stop_workers(bool wait)
{
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    wgr_asset_jobs.stop = true;
    wgri_cond_broadcast(&wgr_asset_jobs.wake);
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
    for (int i = 0; i < wgr_asset_jobs.worker_count; i++) {
        if (wait) {
            wgri_thread_join(&wgr_asset_jobs.threads[i]);
        } else {
            wgri_thread_detach(&wgr_asset_jobs.threads[i]);
        }
    }
    wgr_asset_jobs.worker_count = 0;
}

void wgri_asset_set_worker_count(int count)
{
    wgr_asset_worker_request = count;
    if (wgr_asset_ready) {
        stop_workers(true);
        start_workers(count >= 0 ? count : default_worker_count());
    }
}

int wgri_asset_get_worker_count(void)
{
    return wgr_asset_jobs.worker_count;
}

WGRI_KEEP
void wgr_asset_set_upload_budget(float milliseconds)
{
    wgr_asset_upload_budget_ms = milliseconds > 0.0f ? milliseconds : 0.0f;
}

/* ------------------------------------------------------ groups, progress */

WGRI_KEEP
wgr_handle_t wgr_asset_group_create(void)
{
    wgr_handle_t handle;
    wgr_asset_task_t *task_ptr;

    if (!wgr_asset_ready) {
        return 0;
    }
    handle = alloc_task();
    if (handle == 0) {
        return 0;
    }
    task_ptr = resolve(handle);
    *task_ptr = (wgr_asset_task_t){0};
    task_ptr->is_group = true;
    task_ptr->state = TASK_WAITING;
    return handle;
}

WGRI_KEEP
bool wgr_asset_group_add(wgr_handle_t group, wgr_handle_t task)
{
    wgr_asset_task_t *group_ptr = resolve(group), *task_ptr = resolve(task);
    uint16_t group_index = 0;

    if (group_ptr == NULL || task_ptr == NULL || !group_ptr->is_group || task_ptr->is_group || group == task ||
        task_ptr->group != 0 || task_ptr->parent != 0) {
        log_warn("wgr_asset_group_add: needs a group and a file task that isn't in a group");
        return false;
    }
    wgri_handle_pool_resolve(&wgr_asset_pool, group, &group_index);
    task_ptr->group = group_index;
    task_ptr->armed = true; /* loads even without callbacks of its own */
    group_ptr->pending++;
    group_ptr->dependency_count++;
    return true;
}

/* Rough progress of one file task: fetched, prepared, finished. */
static float task_progress(const wgr_asset_task_t *task)
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

WGRI_KEEP
float wgr_asset_get_progress(wgr_handle_t task)
{
    uint16_t index = 0;
    const wgr_asset_task_t *task_ptr;
    float sum;

    if (wgr_handle_get_kind(task) != WGR_HANDLE_KIND_ASSET_TASK) {
        return 0.0f;
    }
    if (!wgri_handle_pool_resolve(&wgr_asset_pool, task, &index)) {
        return 1.0f; /* finished: its callbacks have run */
    }
    task_ptr = &wgr_asset_tasks[index];
    if (!task_ptr->is_group) {
        return task_progress(task_ptr);
    }
    if (task_ptr->dependency_count == 0) {
        return 0.0f;
    }
    sum = (float)(task_ptr->dependency_count - task_ptr->pending); /* finished members */
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        if (wgr_asset_pool.occupied[i] && wgr_asset_tasks[i].group == index) {
            sum += task_progress(&wgr_asset_tasks[i]);
        }
    }
    return sum / (float)task_ptr->dependency_count;
}

void wgri_asset_init(void)
{
    if (!wgri_handle_pool_init(&wgr_asset_pool, WGR_HANDLE_KIND_ASSET_TASK, "asset", (void **)&wgr_asset_tasks,
                             sizeof(wgr_asset_task_t), ASSET_TASKS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("asset: out of memory");
    }
#ifdef __EMSCRIPTEN__
    wgr_asset_fetching = 0;
#endif
    if (!wgr_asset_jobs.lock_live) { /* still alive after a web shutdown (workers detached) */
        wgri_mutex_init(&wgr_asset_jobs.lock);
        wgri_cond_init(&wgr_asset_jobs.wake);
        wgr_asset_jobs.lock_live = true;
    }
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    wgr_asset_jobs.queue.head = wgr_asset_jobs.queue.count = 0;
    wgr_asset_jobs.done.head = wgr_asset_jobs.done.count = 0;
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
    start_workers(wgr_asset_worker_request >= 0 ? wgr_asset_worker_request : default_worker_count());
    log_info("wgr_asset: %d loading worker(s)%s", wgr_asset_jobs.worker_count,
             wgr_asset_jobs.worker_count == 0 ? " (loading on the main thread)" : "");
    wgr_asset_ready = true;
}

static void ready(uint16_t i, bool ok);

static bool hold(wgr_asset_task_t *group, const wgri_loader_t *loader, wgr_handle_t resource)
{
    if (group->held_count == group->held_capacity) {
        const int capacity = group->held_capacity > 0 ? group->held_capacity * 2 : 8;
        wgr_asset_held_t *held = (wgr_asset_held_t *)realloc(group->held, (size_t)capacity * sizeof(wgr_asset_held_t));
        if (held == NULL) return false;
        group->held = held;
        group->held_capacity = capacity;
    }
    group->held[group->held_count++] = (wgr_asset_held_t){loader, resource};
    return true;
}

/* Free a finished task slot before firing its callback (which may queue more),
 * then tell the task it's a dependency of, if any. */
static void complete(uint16_t i, bool ok)
{
    wgr_handle_t handle = wgri_handle_pool_handle_from_index(&wgr_asset_pool, i);
    const wgr_asset_task_t task = wgr_asset_tasks[i];
    char local[512];

    if (task.is_group) {
        local[0] = '\0';
    } else if (task.local[0] != '\0') {
        snprintf(local, sizeof(local), "%s", task.local);
    } else {
        wgri_fs_resolve(task.path, local, sizeof(local));
    }
    free(task.candidates);
    wgr_asset_tasks[i] = (wgr_asset_task_t){0};
    wgri_handle_pool_free(&wgr_asset_pool, handle);
    if (task.manifest_dir != 0) {
        manifest_loaded(&task, ok); /* no callbacks, resource, parent or group */
        return;
    }
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
            log_warn("Asset not found (optional, dependency of %s): %s", wgr_asset_tasks[task.parent].path, local);
        } else {
            log_error("Asset not found: %s", local);
        }
        if (task.on_failure) task.on_failure(local, task.user_data);
    }
    for (int h = 0; h < task.held_count; h++) {
        task.held[h].loader->release(task.held[h].resource); /* members' resources nobody created */
    }
    free(task.held);
    if (task.resource != 0 && task.group != 0 && hold(&wgr_asset_tasks[task.group], task.loader, task.resource)) {
        /* the group keeps it until its own callbacks have run */
    } else if (task.resource != 0) {
        task.loader->release(task.resource); /* freed unless the callback created it */
    }
    if (task.parent != 0) {
        wgr_asset_task_t *parent = &wgr_asset_tasks[task.parent];
        parent->pending--;
        parent->dependency_failed = parent->dependency_failed || (!ok && !task.optional);
        if (parent->pending <= 0) {
            ready(task.parent, !parent->dependency_failed);
        }
    }
    if (task.group != 0) {
        wgr_asset_task_t *group = &wgr_asset_tasks[task.group];
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
    wgr_asset_task_t *task = &wgr_asset_tasks[i];
    wgr_asset_job_t job = {.slot = i};

    task->loader = ok && task->parent == 0 && !(task->flags & WGR_ASSET_FILE_ONLY) ? lookup_loader(task->path) : NULL;
    if (task->loader == NULL) {
        complete(i, ok);
        return;
    }
    wgri_fs_resolve(task->path, task->local, sizeof(task->local));
    task->resource = task->loader->find(task->local);
    if (task->resource != 0) {
        complete(i, true); /* already loaded */
        return;
    }
    task->state = TASK_PREPARING;
    job.loader = task->loader;
    snprintf(job.path, sizeof(job.path), "%s", task->local);
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    push_job(&wgr_asset_jobs.queue, &job);
    wgri_cond_broadcast(&wgr_asset_jobs.wake);
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
}

/* A task's own file is local (ok) or unavailable: ensure its dependencies, or finish. */
static void resolved(uint16_t i, bool ok)
{
    wgr_asset_task_t *task = &wgr_asset_tasks[i];
    if (ok && task->origin[0] != '\0') {
        record_found(task->origin, task->path);
        if (task->candidate_next >= task->primary_count && task->fallback_origin[0] != '\0') {
            record_found(task->fallback_origin, task->path);
        }
    }
    if (ok && !task->dependencies_started) {
        start_dependencies(i);
        task = &wgr_asset_tasks[i]; /* it may have moved the tasks */
        if (task->state == TASK_WAITING) {
            return; /* finishes when its last dependency does */
        }
    }
    ready(i, ok && !task->dependency_failed);
}

/* Prepared jobs back from the workers (or, without workers, one prepared here). */
#ifdef __EMSCRIPTEN__
/* A file that won't load may be a bad copy rather than a bad asset: a host that
 * compresses served gzip bytes under an asset's name until 2026-09-20, and the cache
 * keeps whatever it was given. Forget it and fetch once more before calling it a
 * failure -- a second refusal is the asset's own fault. */
static bool refetch_once(uint16_t i)
{
    wgr_asset_task_t *task = &wgr_asset_tasks[i];
    if (task->refetched || task->is_group || task->path[0] == '\0') {
        return false;
    }
    task->refetched = true;
    task->load_failed = false;
    task->resource = 0;
    task->state = TASK_NEW;
    task->fetch_result = FETCH_PENDING;
    wgri_fs_remove(task->path);
    log_warn("asset: %s didn't load; forgetting the cached copy and fetching it again", task->path);
    return true;
}
#else
static bool refetch_once(uint16_t i)
{
    (void)i; /* desktop reads the file the program put there; nothing to re-fetch */
    return false;
}
#endif

static void collect_prepared(void)
{
    wgr_asset_job_t job;
    bool have;

    if (wgr_asset_jobs.worker_count == 0) {
        wgri_mutex_lock(&wgr_asset_jobs.lock);
        have = pop_job(&wgr_asset_jobs.queue, &job);
        wgri_mutex_unlock(&wgr_asset_jobs.lock);
        if (have) { /* one per frame, so loads don't stack into one stall */
            job.prepared = job.loader->prepare(job.path);
            wgri_mutex_lock(&wgr_asset_jobs.lock);
            push_job(&wgr_asset_jobs.done, &job);
            wgri_mutex_unlock(&wgr_asset_jobs.lock);
        }
    }
    for (;;) {
        wgri_mutex_lock(&wgr_asset_jobs.lock);
        have = pop_job(&wgr_asset_jobs.done, &job);
        wgri_mutex_unlock(&wgr_asset_jobs.lock);
        if (!have) break;
        wgr_asset_task_t *task = &wgr_asset_tasks[job.slot];
        if (job.prepared == NULL) {
            task->load_failed = true;
            if (!refetch_once(job.slot)) {
                complete(job.slot, false);
            }
            continue;
        }
        task->prepared = job.prepared;
        task->state = TASK_FINISHING;
        task->finish_order = wgr_asset_finish_counter++;
    }
}

/* The finishing task prepared first, or 0. */
static uint16_t next_finishing(void)
{
    uint16_t next = 0;
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        const wgr_asset_task_t *task = &wgr_asset_tasks[i];
        if (!wgr_asset_pool.occupied[i] || task->state != TASK_FINISHING) continue;
        if (task->finish_started) return i;
        if (next == 0 || task->finish_order < wgr_asset_tasks[next].finish_order) next = i;
    }
    return next;
}

/* Finish prepared resources on the main thread within the upload budget, at least
 * one step per frame. */
static void load(void)
{
    const double start = wgri_thread_now();
    uint16_t i;

    collect_prepared();
    while ((i = next_finishing()) != 0) {
        wgr_asset_task_t *task = &wgr_asset_tasks[i];
        wgri_loader_step_t step = WGRI_LOADER_DONE;

        if (!task->finish_started) {
            /* created meanwhile, e.g. by a sync create of the same file: use that one */
            task->resource = task->loader->find(task->local);
            task->finish_started = true;
        }
        if (task->resource == 0) {
            step = task->loader->finish(task->prepared, task->local, &task->resource);
        }
        if (step != WGRI_LOADER_MORE) {
            task->loader->discard(task->prepared);
            task->prepared = NULL;
            task->load_failed = step == WGRI_LOADER_FAILED;
            if (step != WGRI_LOADER_FAILED || !refetch_once(i)) {
                complete(i, step == WGRI_LOADER_DONE);
            }
        }
        if ((wgri_thread_now() - start) * 1000.0 >= (double)wgr_asset_upload_budget_ms) {
            break;
        }
    }
}

/* A task's file is missing: ensure its fallback instead, if it has one (a compressed
 * texture's PNG, say). The task starts over on the next tick. */
static bool use_fallback(wgr_asset_task_t *task)
{
    const wgr_asset_candidate_t *next;
    if (task->candidates == NULL || task->candidate_next >= task->candidate_count) return false;
    next = &task->candidates[task->candidate_next++];
    if (task->overlay) { /* a redirect without this file: normal for mods and translations */
        log_debug("Asset %s not at %s; trying %s", task->origin, task->path, next->path);
    } else {
        log_warn("Asset not found: %s; using %s instead", task->path, next->path);
    }
    snprintf(task->path, sizeof(task->path), "%s", next->path);
    snprintf(task->fetch_url, sizeof(task->fetch_url), "%s", next->url);
    task->overlay = next->overlay;
    task->state = TASK_NEW;
    task->fetch_result = FETCH_PENDING;
    task->manifest_checked = false; /* another path: another entry */
    task->expect_hash[0] = '\0';
    return true;
}

void wgri_asset_tick(void)
{
    /* Nothing can be ensured until storage is up (web: once the cache's list of
     * files is read; desktop: immediately). Tasks stay queued until then. */
    if (!wgr_asset_ready) {
        return;
    }
    deliver_pings();
    if (!wgri_fs_is_ready()) {
        return;
    }
#ifdef __EMSCRIPTEN__
#endif
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        wgr_asset_task_t *task = &wgr_asset_tasks[i];

        if (!wgr_asset_pool.occupied[i] || !task->armed || task->state == TASK_PREPARING ||
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
        if (task->state == TASK_NEW && !task->manifest_checked) {
            char hash[WGRI_SHA256_TEXT];
            const int listed = manifest_lookup(i, hash);
            task = &wgr_asset_tasks[i]; /* the lookup may have added tasks */
            if (listed == LOOKING) continue;
            task->manifest_checked = true;
            if (listed == LISTED) memcpy(task->expect_hash, hash, sizeof(hash));
        }

#ifdef __EMSCRIPTEN__
        if (task->cache_read != 0) {
            const int read = wgri_fs_cache_read_poll(task->cache_read);
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
            if (task->fetch_result == FETCH_USE_CACHE) {
                task->revalidating = false;
                task->cache_read = wgri_fs_cache_read_begin(task->path); /* resolves on a later tick */
                if (task->cache_read == 0) task->state = TASK_NEW; /* gone meanwhile: download it */
                continue;
            }
            if (task->fetch_result == FETCH_FAILED && use_fallback(task)) continue;
            resolved(i, task->fetch_result == FETCH_OK);
            continue;
        }
        /* FORCE_FETCH re-downloads. Otherwise a file already loaded this visit is used.
           A cached one the manifest lists is used if its hash is the listed one, and
           fetched if not; any other is used if the mode trusts it or it is still fresh,
           and checked with the host if not (wgr_asset.h). The root manifest is always
           checked. */
        if (!(task->flags & WGR_ASSET_FORCE_FETCH) && wgri_fs_exists(task->path)) {
            resolved(i, true);
            continue;
        }
        if (!(task->flags & WGR_ASSET_FORCE_FETCH) && wgri_fs_is_cached(task->path)) {
            wgri_fs_meta_t meta;
            const bool has_meta = wgri_fs_meta_get(task->path, &meta);
            const bool fresh = has_meta && meta.fresh_until > (double)time(NULL);
            const bool listed = task->expect_hash[0] != '\0';
            if (listed ? strcmp(meta.hash, task->expect_hash) == 0
                       : !task->manifest_root && (wgr_asset_cache_mode == WGR_ASSET_CACHE_TRUST || fresh)) {
                task->state = TASK_FETCHING;
                task->cache_read = wgri_fs_cache_read_begin(task->path); /* resolves on a later tick */
                continue;
            }
            if (wgr_asset_fetching >= MAX_FETCHES) continue;
            start_fetch(i, listed ? NULL : &meta); /* asked about: unconditionally without metadata */
            continue;
        }
        if (wgr_asset_fetching >= MAX_FETCHES) {
            continue; /* waits for a download to finish */
        }
        start_fetch(i, NULL); /* miss (or forced): download, cache, resolve on later ticks */
#else
        /* Desktop: a hit resolves from the jailed local fs. A miss asks the app's
         * fetcher, if one is set and there is somewhere to download from -- a URL host,
         * or a source this task was given outright (a per-call fetch_url, or a URL
         * redirect). It answers on a later tick (wgr_asset_fetch_done). Without a
         * fetcher a miss fails, as it always has. */
        if (task->state == TASK_FETCHING) {
            continue; /* the fetcher answers with wgr_asset_fetch_done */
        }
        {
            const bool have = wgri_fs_exists(task->path);
            const bool forced = (task->flags & WGR_ASSET_FORCE_FETCH) != 0;
            /* a file the manifest lists is current when its recorded hash is the listed
               one; the root manifest is fetched once a run, whatever is there */
            bool current = have && !forced && !task->manifest_root;
            if (current && task->expect_hash[0] != '\0') {
                wgri_fs_meta_t meta;
                current = wgri_fs_meta_get(task->path, &meta) && strcmp(meta.hash, task->expect_hash) == 0;
            }
            if (current) {
                resolved(i, true);
                continue;
            }
            if (wgr_asset_fetcher != NULL && (wgr_asset_host_is_url || task->fetch_url[0] != '\0')) {
                char joined[1024], dest[1024];
                const char *url; /* per-call override wins, as on the web (start_fetch) */
                if (task->fetch_url[0] != '\0') {
                    url = task->fetch_url;
                } else {
                    snprintf(joined, sizeof(joined), "%s/%s", wgr_asset_host, task->path);
                    url = joined;
                }
                wgri_fs_resolve(task->path, dest, sizeof(dest));
                wgri_fs_make_parents(task->path); /* the fetcher only has to write */
                if (have) { /* the bytes are about to change under what described them */
                    const wgri_fs_meta_t none = {0};
                    wgri_fs_meta_set(task->path, &none);
                }
                task->state = TASK_FETCHING;
                wgr_asset_fetcher(wgri_handle_pool_handle_from_index(&wgr_asset_pool, i), url, dest,
                                  wgr_asset_fetcher_user);
                continue;
            }
            if (!have && use_fallback(task)) continue;
            resolved(i, have);
        }
#endif
    }
    load();
}
/* Tasks not finished yet (queued, downloading or waiting on dependencies).
 * Exported on web so tools/webcheck.py can tell when an example is done loading. */
WGRI_KEEP
int wgri_asset_pending_count(void)
{
    int count = 0;
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        count += wgr_asset_pool.occupied[i] ? 1 : 0;
    }
    return count;
}

/* One warning per task not finished yet, saying where it is stuck: the stage a
 * count can't tell apart. webcheck calls it when an example is still loading at its
 * deadline, so a slow runner's failure names the file and the stage. */
WGRI_KEEP
void wgri_asset_pending_log(void)
{
    static const char *const STAGE[] = {
        [TASK_NEW] = "queued", [TASK_FETCHING] = "downloading", [TASK_WAITING] = "waiting on dependencies",
        [TASK_PREPARING] = "decoding on a worker", [TASK_FINISHING] = "creating the resource on the main thread",
    };
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        const wgr_asset_task_t *task = &wgr_asset_tasks[i];
        if (!wgr_asset_pool.occupied[i]) continue;
        log_warn("wgr_asset: pending: %s (%s%s%s)", task->path, STAGE[task->state],
                 task->state == TASK_FETCHING ? (task->fetch_result == FETCH_PENDING ? ", in flight" : ", answered") : "",
                 task->finish_started ? ", part done" : "");
    }
}

void wgri_asset_deinit(void)
{
    wgr_asset_job_t job;

    wgr_asset_ready = false;
    memset(wgr_asset_pings, 0, sizeof(wgr_asset_pings)); /* unreported: dropped */
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    wgr_asset_found_count = 0;
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
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
    wgri_mutex_lock(&wgr_asset_jobs.lock);
    wgr_asset_jobs.queue.count = 0;
    while (pop_job(&wgr_asset_jobs.done, &job)) {
        if (job.prepared != NULL) job.loader->discard(job.prepared);
    }
    wgri_mutex_unlock(&wgr_asset_jobs.lock);
    for (uint16_t i = 1; i < wgr_asset_pool.capacity; i++) {
        wgr_asset_task_t *task = &wgr_asset_tasks[i];
        if (!wgr_asset_pool.occupied[i]) continue;
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
        free(task->candidates);
        task->candidates = NULL;
    }
    if (wait) {
        wgri_cond_destroy(&wgr_asset_jobs.wake);
        wgri_mutex_destroy(&wgr_asset_jobs.lock);
        wgr_asset_jobs.lock_live = false;
    }
#ifdef __EMSCRIPTEN__
#endif
    wgri_handle_pool_destroy(&wgr_asset_pool);
    forget_manifests(); /* read again, from the cache or the host, after the next init */
}
