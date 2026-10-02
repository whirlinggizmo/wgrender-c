# Plan: Load on create, and polled tasks instead of callbacks

Status: **phase 1 built** (every resource loads on create, the resource section; its
design, decisions and what was built are in [HISTORY.md](HISTORY.md#load-on-create-and-polled-tasks-instead-of-callbacks)).
**Phase 2 next**, then phase 3, both approved as below: the four loop setters stay
(`wgr_set_shutdown` among them), the event bus goes, the fetcher is polled, and
`wgr_fs.h` comes last.

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

## Design (phases 2 and 3)

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
  nothing (`WGR_ASSET_FILE_ONLY` already went with phase 1); `WGR_ASSET_FORCE_FETCH` and
  `fetch_url` stay.
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

1. **The fetcher polled** (section 4) rather than kept as the one other callback: a
   download is what a binding wants to do in its own language, and polling is what lets
   a JS guest or a cppia script supply one. Recommended.
2. **`wgr_fs.h` in this branch, last.** It's the same model over the same machinery, but
   a new public surface with its own tests (the root jail, the web store). Recommended:
   phase 3, once load on create has settled.

## Phasing

1. **Load on create.** Built.
2. **Ensure as a task; callbacks out.** `wgr_asset_ensure` (files only), task status,
   path, progress and destroy; groups over tasks; `add_task` out; the event bus out;
   ping as a task; the polled fetcher. check_rules.py holds the callback rule.
3. **`wgr_fs.h` and byte spans.**

## Verification

Every phase: `tools/verify_builds.py --web --windows HOST` (fetching and storage differ
by platform), `tools/check_asset_cache.py` with and without `--manifest` (the asset fetch
moves behind create), the binding's suite and its web examples, and `tools/bench/run_benchmark.py
loadbench`: loading on create with an upload budget against without (nothing loads
synchronously any more, so that's the comparison left).
