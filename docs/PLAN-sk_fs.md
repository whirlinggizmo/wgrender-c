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

## Phasing (locked order: 2b → 2a → 2c)

Toolchain is the dominant risk and is independent of `sk_fs`, so stand up wasm
first and get the in-browser feedback loop before adding fs complexity. Good news
already confirmed: `sk_run` uses `sapp_run`, so sokol_app drives the web main loop
(`emscripten_set_main_loop`) for us — no run-loop port needed. emcc 5.0.7 is
installed (`~/toolchains/emsdk`).

- **Step 1 (was 2b) — wasm/JSPI target + serve loop.** Build a **no-asset** example
  (`hello`) with emcc: `-DSOKOL_GLES3`, `-sUSE_WEBGL2=1`, `-sJSPI`,
  `-sALLOW_MEMORY_GROWTH=1`, `-o examples/build/web/hello.js`. Serve it and confirm
  renders in a Chromium-class browser. Desktop build untouched. (No files yet, so
  no `FORCE_FILESYSTEM`/idbfs — that's step 3.)
- **Step 2 (was 2a) — `sk_fs` desktop seam.** Add `sk_fs` (real-dir backend), route
  ensure's existence check behind `sk_fs_exists`; `sk_fs_is_ready()`→true on
  desktop, web path a `__EMSCRIPTEN__`-gated stub. Behavior-preserving; both
  targets build.
- **Step 3 (was 2c) — web idbfs + fetch.** IDBFS mount, restore/flush barriers
  (cribbed JSPI `EM_ASYNC_JS` shim), and ensure's `wait-ready → exists? → fetch →
  write → flush` path. Adds `-sFORCE_FILESYSTEM -lidbfs.js`. Asset examples then
  run in-browser.

## Decisions locked
- **Order:** 2b → 2a → 2c (above).
- **Crib, don't vendor.** Pull the idbfs sync shim + barrier logic into a small
  `sk_fs`; use sokol_fetch (XHR on web) + our existing asset-task machinery rather
  than importing wgutils wholesale.
- **Desktop never regresses.** Web code is `__EMSCRIPTEN__`-gated.
- **JSPI, not ASYNCIFY** — matches librl; dev target is Chromium-class browsers.
- **Web backend is parameterized** (`make wasm BACKEND=gl|wgpu`). WebGPU
  (`SOKOL_WGPU`, via emscripten's `emdawnwebgpu` port) was a motivation for
  choosing sokol and is a first-class target — but the same libsk source compiles
  to either, so we get **first light on WebGL2** (`SOKOL_GLES3`, lowest risk,
  validates canvas/loop/JSPI/serve/fs) and bring up **WebGPU as a sibling target**
  right after, since it adds async device init + the Dawn port on top.
- **Host fetch is required on web** (not optional): first run has an empty IDBFS
  cache, so every asset is fetched from the serving origin then cached. Port a
  `sk_asset_set_host` / `SK_ASSET_HOST` config in step 3.
- **Asset paths are logical; the base differs by platform.** Examples reference
  assets by logical relative path (e.g. `music/ethernight_club.mp3`, no
  `examples/assets/` prefix). The base that path resolves against is per-platform
  — the **asset host** (serving origin) on web for fetch, the **fs root_dir**
  locally for reads. (This is exactly why librl had both `assetHost` and
  `rl_fs_init(root_dir)`.) Normalize the example path `#define`s + set the bases
  when step 3 lands.
- **Assets: one source, mounted not duplicated.** `examples/assets/` stays the
  single tracked source of truth. For dev we **mount** it at the server (no
  symlink — Windows-hostile — and no copy): `tools/serve.py` serves the built site
  at `/` and maps `/assets/* → examples/assets/*`, so web `assetHost = "/assets/"`.
  A standalone deploy copies the tree into the bundle; dev never does.
  (`npx live-server --mount=/assets:examples/assets` or a vite alias work the same
  if you want live reload.)
- **Build layout.** Generated artifacts live under `examples/build/` —
  `examples/build/desktop/` (native example binaries) and `examples/build/web/`
  (the wasm site) — both gitignored, so `make clean` (`rm -rf examples/build`)
  nukes either/both and the project root stays clean. The library still builds to
  root `build/` + `lib/`. `examples/web/` is the only *tracked* web bit (the shell).
- **Serve loop:** `make wasm` emits to `examples/build/web/`; `make serve` runs
  `tools/serve.py` (stdlib, cross-platform) which serves it and mounts `/assets/`.
  Reload-on-change: rerun `make wasm` (manually or via a watcher) and a live server
  (`npx live-server --mount=/assets:examples/assets`, or a vite alias) if you want
  auto-refresh. Not hard-picked.

## Decided
- **`sk_fs` is internal storage; `ensure` stays public in `sk_asset`.** `sk_fs`
  owns local storage only (exists/read/write + idbfs sync) with no network;
  `sk_asset` owns acquisition (ensure + host + fetch) on top of it. Keeps the
  filesystem free of an HTTP/host dependency (layered, matches librl). Promote
  `sk_fs` to a public VFS later only if user-facing read/write/save-games is wanted.

## Later
- **`sk_net` (fetch / websockets).** The web fetch inside `ensure` is really a
  network primitive; eventually a small `sk_net` subsystem (HTTP fetch, later
  websockets) should own it, and `sk_asset` would call `sk_net` rather than
  sokol_fetch directly. Out of scope for Phase 2; revisit when websockets/network
  features land.
