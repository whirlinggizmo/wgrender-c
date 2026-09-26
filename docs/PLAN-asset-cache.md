# Plan: a web asset cache that notices changed files

Status: **in progress.** Steps 1-3 landed 2026-09-25: the metadata store, the cache
mode, and revalidation on the web (`tools/cachecheck.py` shows the bug fixed). As
built, a cross-origin host is revalidated with `cache: "no-cache"` rather than
conditional headers, which would need a CORS preflight (`wgr_asset.h`). Next: the
manifest (step 4). The one-time fix for the bug that prompted this (a bumped
`WGR_FS_CACHE_EPOCH`) landed separately.

## The bug

On the web, `wgr_fs` keeps every fetched asset in IndexedDB, keyed by its path, and
serves it on every later visit without asking anyone whether the file on the server has
changed. The only invalidation is `WGR_FS_CACHE_EPOCH` in `src/wgr_fs.c`, a number a
person has to remember to raise, which throws the whole cache away for everyone.

On 2026-09-25 the tile sheet (`textures/tiles.png`) changed shape at the same path, and
the published `tilemap` example showed the new cell coordinates cut from the old sheet
for anyone who had visited before. The same happened silently to
`models/woman_casual/woman_casual.glb`, which changed three times in a day. A game
built on wgrender would hit this on its first patch.

## What the design has to give

1. **A default that cannot show a stale file**, with nothing to configure or build.
2. **No wasted downloads:** a game that changes one texture should re-fetch one texture.
3. **Offline still works:** a cached copy is used when the network is down.
4. **A pipeline-agnostic opt-in** for the cheap path: whatever carries versions must be
   a plain file any build tool can write, not something only `tools/site.py` produces.
5. **Scales to a large game:** no single file that lists every asset of an MMO.
6. **Control from the game:** a way to turn persistence off (development) and to clear
   the cache (a settings button) -- the second exists: `wgr_asset_clear_cache`.

## What others do (checked in their source, 2026-09-25)

- **Babylon.js** (`packages/dev/core/src/Offline/database.pure.ts`): a hand-edited
  `version` in a `<scene>.manifest` beside each scene; no manifest means the cache is
  neither used nor deleted; offline means no cache at all (the manifest fetch fails);
  and it records the new version **before** fetching the file, so a failed fetch leaves
  the old file served as current on the next visit. Nothing is ever deleted.
- **Emscripten `--use-preload-cache`** (`tools/file_packager.py`): one sha256 of the whole
  package baked into the loader; a mismatch re-fetches the whole package.
- **Unity WebGL**: revalidates each cached file with the server; a `cacheControl` hook
  per file says `must-revalidate`, `immutable` or `no-store`.
- **three.js**: an in-memory map, off by default. **Godot web**: a service worker with,
  per its docs, no cache busting.

Nobody has a per-file content hash written at build time. Emscripten has the hash for
one monolithic package; Unity has per-file but asks the server every time.

## Design

Two layers. The first is the default and needs nothing; the second is opt-in and
removes the round trips.

### 1. Revalidate by default (HTTP semantics)

Every cached file gets **metadata** stored beside it: the response's `ETag`,
`Last-Modified`, and a freshness deadline computed from `Cache-Control` (`max-age`,
`immutable`) at the time it was stored.

When an asset is ensured and a cached copy exists:

| The cached copy is... | Do |
|---|---|
| fresh (`immutable`, or `max-age` not yet passed) | use it, no request (this is what browsers do; `tools/serve.py --cache` and a well-configured host mark versioned files this way) |
| not fresh | conditional GET with `If-None-Match` / `If-Modified-Since` |

And the server's answer decides, nothing else:

| Answer | Cached copy exists | Do |
|---|---|---|
| **304** | yes | use it; refresh the freshness deadline from the new headers |
| **200** | any | store the bytes and their headers, replacing the copy; use them |
| **404** (or any 4xx) | yes | **delete the copy**, fail the load |
| 404 | no | fail the load |
| **network error** (offline, DNS, timeout) | yes | **use the copy** -- a non-answer is not "gone" |
| network error | no | fail the load |

`WGR_ASSET_FORCE_FETCH` keeps its meaning: an unconditional GET, always.

Revalidation is web-only in this plan: the built-in web fetch is a `fetch()` in
`wgri_asset_fetch_js` (`src/wgr_asset.c`), where headers are in hand. Desktop
downloads go through the program's `wgr_asset_fetch_fn`, which reports only success;
desktop keeps trusting its cache (`.wgr-cache`) in this plan, and gets the manifest
path below, which works on both platforms. Extending `wgr_asset_fetch_done` to carry an
ETag is a later phase.

### 2. A manifest tree, when the build can write one

A **manifest** names files with a hash of their contents. It is a tree, one per
directory, so a large game never fetches one file that lists everything:

```
assets/manifest.json            (the root; small)
{
  "wgr_manifest": 1,
  "files": { "README.md": "sha256:9f86d081..." },
  "dirs":  { "textures": "sha256:3a7bd3e2...", "models": "sha256:..." }
}
assets/textures/manifest.json   (one per directory, same shape)
{
  "wgr_manifest": 1,
  "files": { "tiles.png": "sha256:...", "tiles_normal.png": "sha256:..." },
  "dirs":  {}
}
```

A directory's entry in its parent is the hash of that directory's `manifest.json`,
so an unchanged directory is known unchanged from the parent alone (a Merkle tree, as
git does it). Hashes are `sha256:` + 64 hex; the prefix names the algorithm so another
can be added later.

With a manifest set (`wgr_asset_set_manifest`, below), ensuring `textures/tiles.png`:

1. The **root** is fetched once per session with `cache: "no-cache"` (it is small).
   If the fetch fails and a cached root exists, use the cached root (offline); if none,
   fall back to layer 1 for everything.
2. `textures/manifest.json` is fetched only if the root's hash for `textures` differs
   from the cached directory manifest's hash (or none is cached); then it is stored
   under that hash. Directories the program never touches are never fetched.
3. The file's manifest hash is compared with the cached copy's stored hash:
   **equal: use the copy, no request.** Different or no copy: plain GET.
4. A fetched file's bytes are **hashed before they are stored** (`crypto.subtle.digest`
   on the web, it is async and off the main thread; a small sha256 in C on desktop).
   Match: store bytes + hash + headers. Mismatch (a CDN edge still serving the old
   file, a broken deploy): **do not store, fail the load**; the next session tries
   again. This is the rule Babylon gets wrong: never record a version until the file
   that has it has landed.
5. A path the manifest does not list falls back to layer 1 (or, on desktop, to trust).

The manifest is just JSON: `tools/gen_manifest.py` writes it for our sites, and any
build tool can write the same thing. Adding an asset means regenerating its
directory's manifest (and the parents'), which the generator does for a whole tree.

### Public API (`include/wgr_asset.h`)

Every parameter stays a handle, a number, an enum or a `const char *`
(`tools/check.py` enforces it).

```c
/* How a cached asset is treated on later visits. */
typedef enum {
    WGR_ASSET_CACHE_REVALIDATE = 0, /* default: fresh copies are used, others are checked
                                       with the server (304 keeps, 200 replaces, 404
                                       forgets, no answer keeps); a manifest, when set,
                                       answers instead of the server for the files it
                                       lists */
    WGR_ASSET_CACHE_TRUST,          /* what the cache has is used without asking: for a
                                       program that evicts by itself, or must start
                                       without the network */
    WGR_ASSET_CACHE_OFF,            /* nothing is kept between visits (development) */
} wgr_asset_cache_mode_t;
void wgr_asset_set_cache_mode(wgr_asset_cache_mode_t mode);
wgr_asset_cache_mode_t wgr_asset_get_cache_mode(void);

/* The root manifest's logical path under the host ("manifest.json"), or NULL for
 * none. With one, a listed file is fetched only when its hash changed. False for a
 * path that isn't relative. */
bool wgr_asset_set_manifest(const char *path);
```

Existing and unchanged: `WGR_ASSET_FORCE_FETCH`, `wgr_asset_evict`,
`wgr_asset_clear_cache`, `wgr_asset_ensure_async`'s callback contract (a path, never
bytes).

Header comments are the contract (AGENTS.md "Docs: which one is true"): the tables
above go into `wgr_asset.h` in prose, in the same commit as the behaviour.

## Where it goes

- `src/wgr_fs.c` -- the IndexedDB store gains a **`meta` object store** (bump the DB
  version 1 -> 2; `onupgradeneeded` creates it; existing `files` records survive and,
  having no metadata, get a plain conditional-less GET once, which is correct). A meta
  record: `{ etag, lastModified, freshUntil, hash }`. New internals in
  `src/internal/wgr_fs_internal.h`: `wgri_fs_meta_get(path, out)`,
  `wgri_fs_meta_set(path, meta)`, removed together with the file by `wgri_fs_remove`.
  Desktop: a sidecar under the cache dir, `<cache>/.meta/<path>` (never beside the
  asset: `foo.png.meta` could be a real asset's name).
- `src/wgr_asset.c` -- `wgri_asset_fetch_js` sends the conditional headers and reports
  the status and headers back: extend `wgri_asset_fetch_finished(slot, data, size)` to
  carry `status`, and pass `etag`/`last-modified`/`cache-control` as strings through a
  sibling `wgri_asset_fetch_headers(slot, ...)` allocated the way `wgri_asset_fetch_alloc`
  is (malloc in C, so closure needs no new export). The decision tables live here, in
  `start_fetch` and the finished path. The manifest lookup is a small state machine on
  the task queue: an ensure waits for the root, then the directory, then decides.
- A JSON reader for the manifest: there is none in `deps/` (cgltf's is private). Vendor
  **jsmn** (MIT, one header) into `deps/jsmn/` and add it to README's license table, or
  write a reader for this one fixed shape; jsmn is the smaller risk.
- sha256 in C for desktop (and for hashing on the web if `crypto.subtle` is unavailable
  in an insecure context): a single-file public-domain implementation in `deps/`, or
  write the 100 lines; either is fine, license noted in README.
- `tools/gen_manifest.py DIR` -- writes `manifest.json` in DIR and every directory under
  it (skipping `manifest.json` itself); idempotent; Python only.
- `tools/site.py` runs it on the copied assets. The bindings' own `webdeploy` do the
  same (they already import wgrender's tools). `tools/serve.py` keeps `no-store` by
  default (layer 1 revalidates, so local edits show up); `--cache` sends what it does
  now, which layer 1 honours as fresh.
- `docs/ARCHITECTURE.md` -- the asset layer's paragraph mentions the cache's promise;
  README "Startup and hosting" says what a host should send and how to ship a manifest.

## Steps (each one a commit, each verified before the next)

1. **Metadata store.** `meta` store on the web, `.meta/` sidecars on desktop, the
   `wgri_fs_meta_*` internals, removed with the file. Unit test in
   `tests/unit/fs_test.c` (desktop path). Nothing observable changes yet.
2. **Cache mode.** The enum, setter, getter; `OFF` implemented (MEMFS only, no store
   writes); `TRUST` is today's behaviour; `REVALIDATE` behaves as `TRUST` until step 3.
   Header comments written now, saying step 3's behaviour is "not yet" -- no: per
   AGENTS.md a header never lags, so land steps 2 and 3 **together** if they can't be
   made true separately.
3. **Revalidation** (web). Conditional GET, status back to C, the answer table,
   freshness from `Cache-Control`. This changes observable behaviour (a round trip
   before a cached asset's callback when the copy isn't fresh), which is the point;
   it is already approved by this plan. Verify with `tools/verify.py --web` and the new
   `tools/cachecheck.py` (below).
4. **Manifest** (both platforms). jsmn, sha256, the tree lookup, hash-before-store,
   `wgr_asset_set_manifest`. Unit tests with the fetch hook (`test_asset_fetch_hook` is
   the model): a hook serving files from a directory; a manifest whose hash matches
   the cached copy -> no fetch; a changed hash -> fetch; a fetched file whose bytes
   don't match the manifest -> not stored, load fails; a listed file that 404s ->
   copy deleted.
5. **Tooling.** `gen_manifest.py`; `site.py` calls it; the examples' page sets the
   manifest (`wgr_asset_set_manifest("manifest.json")` in `example_assets.h`'s
   guidance, applied in the examples that fetch); `cachecheck.py`.
6. **Bindings.** wgrender-hx, -nim, -beef: regenerate the raw bindings
   (`tools/gen_raw.py` in each), their `webdeploy` writes the manifest, examples set it
   the way C's do. Separate commits per repo, submodule pinned to the wgrender commit.
7. **Docs sweep.** `wgr_asset.h` re-read whole; ARCHITECTURE.md; README hosting section;
   `WGR_FS_CACHE_EPOCH`'s comment says it is now the last resort, not the mechanism.

## Verification

- `ctest --preset linux-headless` after every step (unit, check, smoke).
- `python3 tools/verify.py --web` after steps 3 and 5 (EM_JS changes are unverified
  until a web example links -- AGENTS.md).
- **`tools/cachecheck.py`** (new, step 3): serve a site with `serve.py`, load `tilemap`
  in the headless browser (the harness `webcheck.py` uses), take a screenshot; change
  `textures/tiles.png` on disk; reload; the screenshot must differ and the console must
  show one 200 for the changed file and 304s (or no requests, with a manifest) for the
  rest; then take the file away; reload; the console must show the cached copy deleted
  and the load failing, not the old tiles. This is the bug, reproduced and then fixed.
- Sizes: `tools/websize.py` before and after; the manifest reader and sha256 should
  add well under 10 KB of wasm.

## Decisions

- **Revalidate, not trust, by default.** Trust-forever is the bug. Babylon's opt-in
  ("no manifest, no cache") avoids the bug by never caching, which gives up the offline
  start. Revalidation keeps the copy and only asks whether it is still right.
- **Honour `Cache-Control`.** Without it, every visit would revalidate every file: a
  round trip per asset. With it, a host that marks versioned files `immutable` (as
  `serve.py --cache` does) gets no requests at all, the same as browsers.
- **A tree, not one manifest.** One JSON listing 100,000 files is ~10 MB, re-fetched on
  every deploy before the first frame. Per-directory manifests fetched on demand cost
  what the program touches.
- **Hash before store.** The cached copy's recorded hash is the hash of the bytes that
  were stored, never the hash the manifest promised.
- **404 deletes, a network error keeps.** The server's answer is the only authority;
  the absence of an answer is not one.
- **Not per-file manifests** (Babylon): a request per asset per visit, and no way to
  know a directory is unchanged without asking about each file.
- **Not hashes in filenames** (bundler style): the examples and games ask for logical
  paths; mapping logical to hashed names is the manifest again.
- **Not the epoch.** It stays as the last resort for a cache that is wrong in a way
  nothing can detect (the gzip case of 2026-09-20), and should never be needed for a
  changed asset again.

## Out of scope

- Desktop revalidation through the fetcher (needs `wgr_asset_fetch_done` to carry
  headers): a later phase.
- Quota handling beyond what `wgr_fs` does today.
- Signing or verifying manifests against tampering; the hash guards against staleness
  and broken deploys, not attackers.
