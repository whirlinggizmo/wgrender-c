# Plan: sk_fs + web-capable ensure (Phase 2)

Status: **proposed — awaiting approval.** No code changed yet.
Builds on the Phase 1 ensure model (see [PLAN-handle-only-api.md](PLAN-handle-only-api.md))
and the proven librl `rl_fs` design (cribbed, not vendored — see "Decisions").

## Why

Phase 1 gave us `sk_asset_ensure_async(path) → on_ready(path)` → sync
`sk_*_create(path)`. On **desktop** that already works (files are on disk; ensure
is an existence check). On **web** there is no synchronous disk: a file must be
synced out of IndexedDB into the in-memory FS before any `fopen` sees it, and
writes must be synced back or they vanish on reload. `sk_fs` is the layer that
makes "the file is locally readable" true on both platforms so the rest of libsk
(and the sync `_create(path)` creators) stay platform-agnostic.

## The constraint (what rl_fs proves)

IDBFS is asynchronous; synchronous file I/O only sees data across explicit sync
barriers. rl_fs handles this with three, all polled like our asset tasks:

- **Restore (startup):** IDBFS → MEMFS sync; reads must block until it's ready.
  Polled each frame with a **timeout → fall back to network fetch**.
- **Flush (writes):** MEMFS → IDBFS sync; run after writes and before deinit, or
  cached data is lost on reload.
- **JS↔C coordination:** an `EM_ASYNC_JS` shim spins on a `Module.*_idbfs_syncing`
  flag. libsk leans on **JSPI** for this (like librl), **not ASYNCIFY**. The
  identifier is Emscripten-facing ABI — never rename it (AGENTS.md § C
  implementation naming).

Desktop has none of this: restore is instantly "ready," read/write hit the real
directory.

## Target `sk_fs` API (trimmed from rl_fs)

A jailed local filesystem under a root dir. Start with the minimum ensure needs;
add user-facing ops only when something needs them.

```c
/* lifecycle — IDBFS on wasm, a real directory on desktop */
int          sk_fs_init(const char *root_dir);        /* sync (desktop / JSPI) */
sk_handle_t  sk_fs_restore_async(void);                /* IDBFS→MEMFS; poll via tick */
bool         sk_fs_is_ready(void);                     /* restore barrier cleared? */
int          sk_fs_flush(void);                        /* MEMFS→IDBFS (persist)  */
void         sk_fs_deinit(void);                       /* flush + unmount         */
const char  *sk_fs_get_root_dir(void);

/* file ops — all paths jailed to root_dir (internal at first) */
bool sk_fs_exists(const char *path);
int  sk_fs_read(const char *path, unsigned char **out_data, size_t *out_size);
void sk_fs_read_free(unsigned char *data);
int  sk_fs_write(const char *path, const unsigned char *data, size_t size);
```

Defer rl_fs's richer surface (mkdir/rmdir/remove/clear/normalize, the LRU memory
cache, dependency prefetch) until a caller needs it — keep `sk_fs` small.

## How ensure uses sk_fs

`sk_asset_ensure_async` / `sk_asset_tick` (already task-based) gain a web path;
desktop is unchanged:

- **Desktop:** `sk_fs_is_ready()` is always true; ensure = `sk_fs_exists(path)`
  (today's fopen check, routed through sk_fs) → fire callback.
- **Web:** the ensure task advances through states in `sk_asset_tick`:
  1. wait for `sk_fs_is_ready()` (restore barrier),
  2. `sk_fs_exists(path)`? → ready,
  3. else fetch from the asset host (sokol_fetch) → `sk_fs_write(path)` →
     `sk_fs_flush()` → ready,
  4. fire `on_ready(path)`; the sync `sk_*_create(path)` now reads a local file.

The path-only callback contract is what makes this invisible to user code.

## What to crib vs. skip (from rl_fs / fileio / wgutils)

`rl_fs` sits on a `fileio_*` + `fetch_url_op_t` underlayer (the wgutils-derived
wasm/desktop abstraction). We can vendor it, but likely don't need to:

- **Crib:** the `EM_ASYNC_JS` idbfs-sync shim, the restore/flush
  barrier+timeout+fallback logic, and the IDBFS mount/setup sequence. These are
  small and the tricky, proven part.
- **Reuse what we already have:** sokol_fetch for HTTP (already a dep) instead of
  `fetch_url_op`; our asset-task pool + `sk_asset_tick` instead of rl_fs's task
  pool.
- **Skip (for now):** the LRU memory cache, dependency/batch prefetch state
  machine, host-ping. Not needed for basic ensure.

## Build prerequisite

libsk has **no wasm target yet** (the Makefile is desktop GL only). The web half
of `sk_fs` can't be built or tested until there's an Emscripten target with at
least: `-sFORCE_FILESYSTEM`, `-lidbfs.js`, and **JSPI** (libsk leans on JSPI like
librl, not ASYNCIFY) for the `EM_ASYNC_JS` sync shim. That target is a
prerequisite milestone of its own.

## Phasing

- **2a — sk_fs desktop + route ensure through it.** Add `sk_fs` with the real-dir
  backend; `sk_fs_is_ready()` always true; move ensure's existence check behind
  `sk_fs_exists`. Pure refactor, no behavior change, desktop stays green. All
  web/idbfs code gated under `#ifdef __EMSCRIPTEN__` (no-ops on desktop).
- **2b — Emscripten build target.** Add the wasm build (flags above) so there's
  something to run the web path in. (Could precede 2a if we want to stand up wasm
  first.)
- **2c — web idbfs + fetch.** Implement the IDBFS mount, restore/flush barriers
  (cribbed shim), and the ensure fetch→write→flush state in `sk_asset_tick`.
  Host config via `sk_asset_set_host` (port rl_asset's host API).

## Decisions locked
- **Crib, don't vendor.** Pull the idbfs sync shim + barrier logic into a small
  `sk_fs` (internal `fileio`-style helpers if useful); use sokol_fetch + our
  existing asset-task machinery rather than importing wgutils wholesale.
- **Desktop never regresses.** Web code is `__EMSCRIPTEN__`-gated; 2a is a
  behavior-preserving refactor.

## Open questions
1. **Order:** 2a (desktop refactor) first, or stand up the wasm target (2b) first
   so the web path is testable as it's written?
2. **`sk_fs` surface:** internal-only (just what ensure needs) to start, or expose
   a public user-facing fs API (read/write/save-games) now? Leaning internal-only.
3. **Asset host config:** port `sk_asset_set_host` / env-var (`SK_ASSET_HOST`) in
   2c, or assume page-relative URLs on web for now?
