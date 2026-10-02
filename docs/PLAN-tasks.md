# Plan: Load on create, and polled tasks instead of callbacks

Status: **approved; phase 1 next.** No code changed yet. Decided: the four loop setters
stay (`wgr_set_cleanup` renamed `wgr_set_shutdown`); the event bus goes; making a file
local and loading it are separate (libwgt's split), and a resource loads on create; and
the four decisions below, as recommended.

## Why

A C callback is the hardest thing in the public API for a binding: a function pointer
and a `void *` it hands back. A JS guest can't pass one at all (the Haxe binding's guest
ABI works around six), and every binding writes a trampoline for each. And most of
wgrender's callbacks exist for one reason: a resource can only be created once its file
is local and loaded, so the program ensures the file, waits for a callback, and creates
the resource in it.

libwgt (`gfx/include/wgt_texture.h`, `core/src/wgt_core_load_priv.h`) shows the way out,
built on a pipeline it says it cribbed from wgrender's own: **a resource loads on
create.** `create(path)` returns a handle at once, PENDING; the file is made local,
prepared on a worker and finished on the main thread over the frames that follow; the
handle becomes READY or FAILED, and nothing is ever called back. Making a file local
*without* loading it is a separate thing, a task (libwgt's `wgt_asset_ensure`, designed
there and not yet built). Whatever may wait is read, never called back: a status that
changes only at the start of a frame, so a frame callback that checks it sees each
change once, in order.

## What we have

- **`wgr_*_create(path)` is synchronous** (`wgri_loader_create`, `src/wgr_asset.c`): it
  returns the resource the asset layer already loaded for that path, or else reads,
  decodes and finishes it on the spot, on the main thread, holding up the frame, and
  returns **0 for any failure** -- a missing file, a broken one -- with nothing to ask
  why. It never fetches: the file must be local.
- **`wgr_asset_ensure_async` does two things**: makes the file local (fetching and
  caching it if it's missing) *and* loads the resource its extension names, unless
  `WGR_ASSET_FILE_ONLY`. That loaded resource is held out of sight until the program
  creates it in `wgr_asset_add_task`'s callback (or a group's).
- **Callbacks in the public API (11 calls):** the loop setters; `wgr_asset_add_task`,
  `wgr_asset_ping_host`, `wgr_asset_set_fetcher`; the event bus.
- **Readiness:** `wgr_model_is_ready` only; nothing says FAILED.

The machinery for the rest is there: the asset layer's queue already fetches, caches,
prepares on workers and finishes on the main thread within a budget. What changes is who
starts it (create, not ensure) and how its outcome is read (a status, not a callback).

## Design

### 1. A resource loads on create

```c
typedef enum {
    WGR_RESOURCE_NONE    = 0,  /* not a resource of this kind */
    WGR_RESOURCE_PENDING = 1,  /* its file is being made local, prepared or finished */
    WGR_RESOURCE_READY   = 2,
    WGR_RESOURCE_FAILED  = 3,  /* the fetch, the file or the decode failed (logged why) */
} wgr_resource_status_t;      /* wgr_types.h */

wgr_handle_t          wgr_texture_create(const char *path);       /* as now, but at once, PENDING */
wgr_resource_status_t wgr_texture_get_status(wgr_handle_t texture);
/* ... the same for mesh, audio, font, environment, shader */
```

- `create` returns a handle at once, PENDING, and queues the load: make the file local
  (from the cache, or **fetched** if it's missing, exactly as ensure fetches today), then
  prepare on a worker, then finish on the main thread within the upload budget. On
  desktop with the file on disk that's typically the next frame.
- **0 only when there's no room** for another resource of that kind. A bad path, a
  missing file, a failed fetch or a file that won't decode gives a handle that's
  FAILED. Creating the same path again gives the same handle, with one more reference,
  whatever its status.
- **Drawing a resource that isn't READY is always safe**, as now: a texture draws the
  placeholder (`wgr_texture_set_placeholder`), a font draws as the default font, a model
  whose mesh isn't READY isn't drawn, a sound whose audio isn't READY plays when it is.
  Each header says so.
- `wgr_model_is_ready` goes: a model is ready when its mesh is
  (`wgr_mesh_get_status(wgr_model_get_mesh(model))`).
- **Files that name other files** (a glTF's buffers and images) load together, as ensure
  loads them now: a missing buffer fails the mesh, a missing image warns and uses the
  placeholder.
- Every header with a `create(path)` says what it refuses and when it's FAILED (AGENTS.md:
  "false for ..." names every refusal; for a handle, "0 only when ...").

### 2. Making a file local is a task of its own

```c
wgr_handle_t wgr_asset_ensure(const char *path, const char *fetch_url, unsigned int flags);

typedef enum {
    WGR_ASSET_TASK_NONE    = 0,  /* not a task */
    WGR_ASSET_TASK_PENDING = 1,
    WGR_ASSET_TASK_DONE    = 2,  /* the file is local (and every file it names) */
    WGR_ASSET_TASK_FAILED  = 3,
} wgr_asset_task_status_t;

wgr_asset_task_status_t wgr_asset_task_get_status(wgr_handle_t task);  /* a file's, or a group's */
const char *wgr_asset_task_get_path(wgr_handle_t task);   /* the local path, once DONE */
float       wgr_asset_task_get_progress(wgr_handle_t task);   /* 0..1, as wgr_asset_get_progress */
bool        wgr_asset_task_destroy(wgr_handle_t task);
```

- `wgr_asset_ensure` (renamed from `ensure_async`: everything is async now) only makes
  files local: prefetching a level, warming the cache, a loading screen. It loads
  nothing, so `WGR_ASSET_FILE_ONLY` goes; `WGR_ASSET_FORCE_FETCH` and `fetch_url` stay.
- A task lives until it's destroyed, so its status and path can be read any number of
  times. Destroying one still pending lets it finish and discards the result (the
  file still lands in the cache).
- Groups stay (`wgr_asset_group_create`, `_add`), over ensure tasks: DONE once every
  member is, FAILED if any failed. Destroying a group destroys its members.
- `wgr_asset_add_task`, `wgr_asset_callback_fn` and `wgr_asset_add_task_result_t` go,
  and with them the resources held out of sight: every loaded resource is one the
  program created.
- A program that wants a loading screen for resources (not just files) reads their
  statuses; one that wants to fetch everything first and load later ensures a group,
  then creates once it's DONE.

### 3. The event bus goes

`wgr_event.h` and `wgr_event.c`: nothing in wgrender emits an event, no example or test
uses it, only the Haxe binding wraps it (`wgr.Event`, which only its own test calls).
Publish/subscribe with untyped payloads is a utility (the org's conventions list `event`
with path, logger and json: wgutils' kind of module), and every language a binding
serves has its own. libwgt has none either.

### 4. The asset layer's two other callbacks

- **`wgr_asset_ping_host(host, timeout_ms)`** returns a task: DONE or FAILED, and
  `wgr_asset_ping_get_milliseconds(task)` for the round trip.
- **The fetcher** is the reverse direction (wgrender asks the *program* to download), so
  polled, the program asks for work:

  ```c
  bool         wgr_asset_set_fetching(bool enabled);  /* a program downloads (desktop) */
  wgr_handle_t wgr_asset_fetch_next(void);            /* a download to do, or 0 */
  const char  *wgr_asset_fetch_get_url(wgr_handle_t request);
  const char  *wgr_asset_fetch_get_dest(wgr_handle_t request);
  bool         wgr_asset_fetch_done(wgr_handle_t request, bool ok);  /* as today, any thread */
  ```

  The contract holds as it is: at most 6 out at once, a download written apart and moved
  into place only on success, the answer taken at the next frame. On the web
  `set_fetching` answers false: the browser is the downloader.

### 5. The loop setters stay, and are the only callbacks

`wgr_set_init`, `wgr_set_tick`, `wgr_set_frame`, `wgr_set_shutdown`: the platform owns the
loop (sokol_app, or the browser), so something must call in; libwgt keeps the same
exception. They are `tools/check_rules.py`'s `TYPES_EXEMPT`, the type rule's one
exemption, with the reason beside them.

### 6. A public `wgr_fs.h`, and byte spans

libwgt's `wgt_fs.h` over wgrender's storage layer, which stays as it is inside:
`wgr_fs_read`, `_write`, `_exists`, `_remove`, `_mkdir`, `_rmdir` each return a task, on
every platform (a desktop read too, answered the next frame, as a web read is);
`wgr_fs_task_get_status` (NONE, PENDING, DONE, NOT_FOUND, FAILED), `_get_path`,
`_get_data`, `_get_size`, `_get_text`, `_destroy`. Requests made before storage is ready
wait for it. Paths are confined under the root (normalized; an absolute one, a `:`, a
control character, or one climbing above the root is refused). `wgr_fs.c` splits into
`_native` and `_web`. AGENTS.md's hard rule takes libwgt's byte span: `const unsigned
char *data, int size`, in copied before the call returns, out owned by the task until
it's destroyed; `check_rules.py` allows one only as a parameter followed by `int size`,
or a `_get_data` beside a `_get_size`.

## What changes beside the library

- **Examples** get simpler: a callback that only created a resource becomes the create
  itself, in init (audio, clay, environment, and the binding's loading, touch and ui).
  `loading.c` shows both ways a loading screen can wait: a group of ensures, and
  resources' statuses. `fetch.c` polls `wgr_asset_fetch_next`.
- **The Haxe binding**: `wgr.Event` goes; resource classes gain `status`; `Asset` keeps a
  callback helper as sugar over polling its open tasks (plumbing, so one name per C call
  holds); five of its six JS omissions go (`wgr_set_*` stay C-only on the guest ABI).
  `Asset.setFetcher(Asset.httpFetcher)` reads as it does now: it stores the Haxe
  function and turns fetching on, and the binding's frame wrapper drains
  `wgr_asset_fetch_next` before the program's frame, handing each request to it.
  `httpFetcher` is unchanged (a thread per download, `fetchDone` from it), the
  `fetchTrampoline` goes, and a cppia script can supply a fetcher, which it can't
  through a C callback.
- **Tests**: load on create (PENDING, then READY or FAILED, never called back inside the
  call; the same handle for the same path while pending; 0 only when full), task
  lifetime, groups, the polled fetcher, each resource's status. `check_asset_cache.py`
  with and without `--manifest`.
- **check_rules.py**: `TYPES_TODO` empties, leaving the loop setters as the only calls
  the type rule exempts.

## Decisions

1. **One `wgr_resource_status_t` for every resource**, where libwgt has one enum per kind
   (`wgt_texture_status_t`, `wgt_font_status_t`, each NONE / PENDING / READY / FAILED).
   The values are the same for every resource, so one type says so, and a binding maps
   it once. Recommended: one.
2. **0 only when there's no room; a bad path is a FAILED handle** (libwgt's rule), where
   create returns 0 for any failure now. A handle can say why (it was logged) and be
   checked like any other. Recommended.
3. **The fetcher polled** (section 4) rather than kept as the one other callback: a
   download is what a binding wants to do in its own language, and polling is what lets
   a JS guest or a cppia script supply one. Recommended.
4. **`wgr_fs.h` in this branch, last.** It's the same model over the same machinery, but
   a new public surface with its own tests (the root jail, the web store). Recommended:
   phase 3, once load on create has settled.

## Phasing

1. **Load on create.** The acquire step in front of the pipeline (local, cached or
   fetched), create returning PENDING, `get_status` on every resource, 0 only when full,
   `wgr_model_is_ready` out, every header's create documented; examples, binding and
   tests follow.
2. **Ensure as a task; callbacks out.** `wgr_asset_ensure` (files only), task status,
   path, progress and destroy; groups over tasks; `add_task` out; the event bus out;
   ping as a task; the polled fetcher. check_rules.py holds the callback rule.
3. **`wgr_fs.h` and byte spans.**

## Verification

Every phase: `tools/verify_builds.py --web --windows HOST` (fetching and storage differ
by platform), `tools/check_asset_cache.py` with and without `--manifest` (the asset fetch
moves behind create), the binding's suite and its web examples, and `tools/bench/run_benchmark.py
loadbench`: loading in the background against synchronously, which is the number load
on create should improve.
