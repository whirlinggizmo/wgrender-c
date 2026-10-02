#ifndef WGR_ASSET_H
#define WGR_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* The asset layer makes files local: from disk, from the cache, or downloaded from the
 * asset host, and on the web it checks a cached copy as the cache mode says
 * (wgr_asset_set_cache_mode, wgr_asset_set_manifest). A resource does this itself when
 * it's created (wgr_resource.h: wgr_texture_create(path) loads on create), so a
 * program only ENSURES a file to have it local without loading it: to fetch ahead (a
 * level's files during a menu), from an explicit source (a fetch_url), or to read it
 * itself. An ensure is a task: a handle whose status, local path and progress the
 * program reads, nothing called back, and which it destroys when done with it. A key
 * ensured from an explicit source is what a later create of that key loads, wherever
 * the file was found.
 *
 * Files that reference other files are ensured together: ensuring a .gltf (or .glb)
 * also ensures the buffers and images it references, relative to it, and the task is
 * DONE once all of them are local. A missing buffer fails it; a missing image only
 * warns. */

typedef enum {
    WGR_ASSET_TASK_NONE    = 0, /* not a task (or one destroyed) */
    WGR_ASSET_TASK_PENDING = 1,
    WGR_ASSET_TASK_DONE    = 2, /* the file is local, and every file it names */
    WGR_ASSET_TASK_FAILED  = 3,
} wgr_asset_task_status_t;

/* Flags for wgr_asset_ensure (bitmask). */
enum {
    WGR_ASSET_NONE        = 0,
    WGR_ASSET_FORCE_FETCH = 1 << 0, /* re-download even if cached; no-op where nothing
                                      can download (desktop without a fetcher) */
};

/* Set the asset base that logical paths resolve against. A URL ("https://host/assets")
 * is a fetch origin on both platforms: a missing file is downloaded from it and cached,
 * on the web in the browser's storage (IndexedDB, checked as wgr_asset_set_cache_mode
 * says) and on desktop in the cache directory, by the fetcher below. Anything else is a
 * local directory ("examples/assets"), as it has always been on desktop, and a file:
 * URL ("file:///opt/game/assets") names one too -- on desktop only, since a browser
 * reads no file: URLs. A local host is only ever read, as a browser only reads its
 * host: what is downloaded under one -- a fetch_url's file, or one a "://" redirect
 * finds missing -- goes in the cache directory, so a shipped file is never
 * overwritten, and it can sit where the program can't write (Program Files, an app
 * bundle). Pass the same logical paths everywhere; only the base differs. */
void wgr_asset_set_host(const char *host);
/* The asset base set with wgr_asset_set_host (without a trailing slash), or "". */
const char *wgr_asset_get_host(void);

/* Where downloads land on desktop, and where later runs find them: a local directory,
 * created as needed. By default the user's cache directory for this program,
 * <cache>/<company>/<app> (wgr_set_app_company, wgr_set_app_name): ~/.cache/... or
 * $XDG_CACHE_HOME/... on Linux, ~/Library/Caches/... on macOS, %LOCALAPPDATA%\...\cache
 * on Windows; ".wgr-cache" where there is none. Ignored on the web, which caches in the
 * browser. Set it before the first wgr_asset_set_host with a URL. */
bool wgr_asset_set_cache_dir(const char *dir);
/* The directory downloads go in: set, or the default above. "" on the web. */
const char *wgr_asset_get_cache_dir(void);

/* Download a missing asset. libwgrender calls this when the host is a URL, the file
 * isn't local yet, and there is no built-in fetcher for this platform (desktop):
 * fetch `url` into `dest_path`, then call wgr_asset_fetch_done(request, ok).
 *
 * It is called on the main thread, and should start the download and return: do the
 * work on a thread of your own and call wgr_asset_fetch_done from there, from any
 * thread. The answer is taken at the next wgr_asset_tick, on the main thread, as a
 * browser's fetch reports back. A fetcher is handed at most 6 downloads at once, a
 * browser's limit per server; the rest wait their turn. One that downloads before
 * returning still works, but holds up the frame it runs in.
 *
 * Bytes never cross this boundary; a downloader deals in files, which is what curl,
 * WinHTTP and NSURLSession all hand you anyway. The directories above `dest_path`
 * already exist. `dest_path` is where the download is written until it is whole, not
 * where the file is read: libwgrender moves it into place when you report success,
 * and deletes it when you don't, so a failed or interrupted download never leaves half
 * a file to be read and never costs the copy that was there. Reporting success with
 * nothing written is a failure.
 *
 * Without a fetcher, a miss on desktop fails as it always has. With one, a miss
 * downloads whenever there is a source: the host if it's a URL, or whatever the task
 * was told to use (a fetch_url, or a "://" redirect target). */
typedef void (*wgr_asset_fetch_fn)(wgr_handle_t request, const char *url,
                                   const char *dest_path, void *user_data);
bool wgr_asset_set_fetcher(wgr_asset_fetch_fn fn, void *user_data);
/* What became of a download the fetcher was handed; any thread. False when it can't be
 * taken: libwgrender isn't running (shut down while the download ran, say). */
bool wgr_asset_fetch_done(wgr_handle_t request, bool ok);

/* Forget a cached asset, so the next ensure fetches it again: the file and what was
 * kept about it, on the web from the browser's storage and from this visit, on
 * desktop from the cache directory -- never a local host's own file. False when there
 * was no such file, or for a path that isn't under the host (as
 * wgr_asset_ensure reads one). A cache can hold a file that is wrong rather
 * than old (a host that compresses once served gzip bytes under an asset's name):
 * the host says it hasn't changed, so revalidation keeps it, and only something that
 * drops it helps.
 *
 * libwgrender also drops an entry by itself when a loader rejects a cached file and
 * fetches it once more, so this is for a program that knows better -- a new version of
 * an asset, or a user asking to free the space. */
bool wgr_asset_evict(const char *path);

/* Forget every cached asset, so the next ensure of any file fetches it again, and
 * what was read of the manifest (wgr_asset_set_manifest), so the root is asked about
 * again. On the web: the browser's storage and this visit's copies. On desktop: every
 * file libwgrender downloaded into the cache directory (wgr_asset_set_cache_dir) is
 * deleted, with its metadata and the directories that leaves empty, and a warning
 * says how many; it keeps a list of its downloads there, ".wgr-downloads", and never
 * deletes a file it didn't download. Resources already created stay as they are.
 * Call it while nothing is loading: a load in flight may fail. */
void wgr_asset_clear_cache(void);

/* How a cached asset is treated on a later visit. On the web the cache keeps each
 * file with what its response said about it (ETag, Last-Modified, Cache-Control);
 * on desktop, downloads in the cache directory are used as they are in every mode
 * (asking the server there is not built yet).
 *
 * WGR_ASSET_CACHE_REVALIDATE, the default: a copy still fresh by its Cache-Control
 * (max-age not passed, or immutable) is used without a request. Any other copy is
 * checked with the server first, and its answer decides: 304, the copy is used (and
 * is fresh again for the new max-age); 200, the new file replaces it; 4xx, the copy
 * is deleted and the load fails as it would without one; no answer (offline, DNS, a
 * timeout) or a 5xx, the copy is used. no-cache and no-store make a copy never
 * fresh, but it is still kept, for the next check and for starting offline. For a
 * host on the page's own origin the check is a conditional GET; on another origin it
 * is a GET that the browser revalidates from its own cache (a conditional header
 * there needs the host's CORS consent), so a changed file is always noticed, but an
 * unchanged one is stored again.
 *
 * WGR_ASSET_CACHE_TRUST: a cached copy is used without asking, however old: for a
 * program that must start without the network, or evicts by itself
 * (wgr_asset_evict).
 *
 * WGR_ASSET_CACHE_OFF: nothing is kept between visits, and what earlier visits kept
 * is neither used nor deleted (development).
 *
 * WGR_ASSET_FORCE_FETCH is a plain GET in every mode, and fails without an answer.
 * A mode applies to every file checked after it is set. */
typedef enum {
    WGR_ASSET_CACHE_REVALIDATE = 0,
    WGR_ASSET_CACHE_TRUST,
    WGR_ASSET_CACHE_OFF,
} wgr_asset_cache_mode_t;
/* False for a value that isn't one of the modes. */
bool wgr_asset_set_cache_mode(wgr_asset_cache_mode_t mode);
wgr_asset_cache_mode_t wgr_asset_get_cache_mode(void);

/* An asset manifest: a hash of each file's contents, so a cached copy whose hash
 * still matches is used with no request at all, and one that changed is fetched once
 * (docs/HISTORY.md, "a web asset cache that notices changed files"; tools/gen_manifest.py writes them). `path` is the root
 * manifest's logical path under the host ("manifest.json"). A manifest lists the
 * files beside it and, for each directory, the hash of that directory's own
 * manifest.json, which is fetched only when a file under it is first ensured, and
 * then only if its hash changed.
 *
 * The root is asked about once per run, as WGR_ASSET_CACHE_REVALIDATE asks whatever
 * the mode is; without an answer the cached root is used, and without either, nothing
 * is listed. A listed file is fetched past the browser's cache and its bytes are
 * hashed before they are kept: bytes that don't match (a host still serving the old
 * file, a broken deploy) are not kept, and the load fails. A file no manifest lists,
 * a file ensured with a fetch_url, and every file under a manifest that couldn't be
 * read or didn't match its hash are cached as the cache mode says. On desktop a
 * manifest needs a URL host and a fetcher (wgr_asset_set_fetcher), and a download is
 * hashed once the fetcher reports it.
 *
 * NULL or "" for none (the default). False for a path that isn't relative (one
 * starting with "/" or holding "://"), or is 512 bytes or longer. Set it before the
 * ensures it should cover;
 * setting it again forgets what was read of the last one. */
bool wgr_asset_set_manifest(const char *path);

/* Ensure a file is local: a task that is PENDING, then DONE or FAILED at the start of
 * a later frame (never inside this call), with a directly openable local path.
 *
 *   path      logical key: the cache path on web, the read path under the
 *             configured host on desktop, and (host + path) the default
 *             download location when fetched. It stays under the host: "\\" is
 *             read as "/", and "." and ".." segments are resolved; a path that is
 *             absolute, names a drive (any ":"), or climbs above the host with ".."
 *             is refused (0).
 *   fetch_url optional per-call override of the SOURCE only — a mirror, a signed
 *             link, a versioned name; bytes are still cached and resolved under
 *             `path`. NULL = the default host + path. It is read against the host
 *             as a browser reads a URL against a directory, on every platform:
 *             "music/v2/a.mp3" is under the host, "../x" beside it, "/x" at its
 *             origin's root, and an absolute URL is used as it is.
 *             On desktop an absolute one has to be http or https, and needs a
 *             fetcher but not a URL host. Under a local host a relative one is a
 *             file under it, read where it is (nothing is copied); it is held to
 *             `path`'s rules, so it can't climb out of the host. Anything else --
 *             a file: URL, one leaving a local host -- is refused (0).
 *   flags     bitmask of WGR_ASSET_* (e.g. WGR_ASSET_FORCE_FETCH).
 *
 * Returns a task handle (kind ASSET_TASK), kept until wgr_asset_task_destroy, or 0
 * (refused as above, or no room). */
wgr_handle_t wgr_asset_ensure(const char *path, const char *fetch_url, unsigned int flags);

/* A task's status: a file's, a group's or a ping's. NONE for anything that isn't a
 * task. It changes only at the start of a frame, so a frame that reads it sees each change
 * once. */
wgr_asset_task_status_t wgr_asset_task_get_status(wgr_handle_t task);

/* The local path of a DONE file task, directly openable (where the file was found:
 * a redirect's, a fetch_url's). "" until then, for a group, and for anything that
 * isn't a task. Borrowed: valid while the task is, until the next ensure. */
const char *wgr_asset_task_get_path(wgr_handle_t task);

/* Rough progress of a task or group, 0..1: a file counts half for being made local
 * and half for the files it names; a group, its members' average. 1 once it is DONE
 * or FAILED, 0 for anything that isn't a task. For resources loading, read their
 * statuses (wgr_resource.h). */
float wgr_asset_task_get_progress(wgr_handle_t task);

/* Free a task. One still PENDING runs on and its result is dropped (a file still
 * lands in the cache). Destroying a group destroys its members. False for anything
 * that isn't a task. */
bool wgr_asset_task_destroy(wgr_handle_t task);

/* Groups: one task for many files (fetching a level's files ahead, say). A group is
 * DONE once every member is, FAILED once every member has finished and any failed
 * (an empty one is DONE at the next frame). Members keep their own statuses and
 * paths. */
wgr_handle_t wgr_asset_group_create(void);
/* Add a file task (wgr_asset_ensure) to a group, finished or not. False for anything
 * else, a task already in a group, or a group that has finished. */
bool wgr_asset_group_add(wgr_handle_t group, wgr_handle_t task);

/* Redirects: load files from somewhere else, for mods, translations or a CDN.
 * Files whose path starts with `prefix` are looked for under `target` instead:
 *
 *   wgr_asset_add_redirect("textures/", "mods/hd/textures/");
 *       textures/rock.png loads mods/hd/textures/rock.png if it exists, else
 *       textures/rock.png
 *   wgr_asset_add_redirect("models/", "https://cdn.example.com/game/models/");
 *       a target with "://" is where the file downloads from -- the browser on the
 *       web, your fetcher on desktop (wgr_asset_set_fetcher): it's still cached and
 *       loaded as models/...
 *
 * Rules stack: every path rule matching a file is tried, the one added last first,
 * then the file's own path, so later rules sit on top (a mod over a mod, fr-CA over
 * fr). A missing file under a path rule isn't an error; the next one is tried (on
 * the web that costs a request). A download rule doesn't stack: the newest one
 * matching a path is where it downloads from. Prefixes are plain text, matched at
 * the start of the path ("textures/", not "*.png").
 *
 * Redirects apply to every file a create loads or an ensure makes local (unless it
 * gave an explicit fetch_url), and to the files they reference (a model's buffers and
 * images, found next to wherever the model came from); wgr_resource_get_path and
 * wgr_asset_task_get_path say which file was found. Up to 32 rules; false
 * when full, given an empty prefix or target, or a prefix or path target that isn't
 * under the host (as wgr_asset_ensure reads a path; a trailing "/" is kept). */
bool wgr_asset_add_redirect(const char *prefix, const char *target);
void wgr_asset_clear_redirects(void);

/* Ping an asset host: a task (wgr_asset_task_get_status, _destroy) that is DONE on a
 * later frame when the host answered within `timeout_ms` (<= 0: 5000), FAILED when it
 * didn't. `host` NULL pings the current one (wgr_asset_set_host). On the web it's a
 * HEAD request to the host (any response counts, even a 404; another origin needs no
 * CORS headers). On desktop the host is a local directory: DONE if it exists, FAILED
 * if not (or a URL: no host ping on desktop, whose fetcher hands files, not round
 * trips). Returns 0 before the asset layer is up, or when there's no room. */
wgr_handle_t wgr_asset_ping_host(const char *host, int timeout_ms);
/* The round trip of a DONE ping, in milliseconds (0 on desktop); 0 for one that isn't
 * DONE, and for anything that isn't a ping. */
float wgr_asset_ping_get_milliseconds(wgr_handle_t ping);

/* Milliseconds per frame spent finishing loads on the main thread (GPU uploads),
 * default 4. At least one step runs each frame, so one large texture can exceed
 * it: a 4096x4096 texture is one upload of ~45 ms. */
void wgr_asset_set_upload_budget(float milliseconds);

#ifdef __cplusplus
}
#endif

#endif // WGR_ASSET_H
