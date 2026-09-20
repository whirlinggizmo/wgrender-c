# Plan: handle-only public API (librl-style asset/resource split)

Status: **implemented.** The handle-only public surface, the asset/resource
split, and `wgr_asset_ensure_async(path, fetch_url, flags)` are in and enforced by
`make check` (`tools/check_naming.sh`). Kept as the design record.
Supersedes an earlier draft of this file that invented a generic
`wgr_asset_load`/`wgr_destroy`; this version follows librl's proven model instead.

## The rule

The public API (`include/*.h`) takes and returns only:

- **handles** (`wgr_handle_t`),
- **integral / float types and enums**,
- **`const char *`** for paths and text.

**No other pointers in user code** — no `unsigned char *data`, no struct
pointers. Bindings (JS/Nim/Haxe/Lua) stay mechanical mirrors.

No backwards compatibility: anything breaking the rule is **removed**, not aliased.

## The model (from librl)

librl cleanly separates two phases — **fetch** and **create** — and *neither*
puts bytes in user code:

1. **Fetch / ensure (async, path-based).** The asset layer makes a file local
   (idbfs on web, disk on desktop), fetching from a host if absent. It never
   decodes anything. The ready callback hands back a **path**:

   ```c
   typedef void (*wgr_asset_callback_fn)(const char *path, void *user_data);
   wgr_handle_t task = wgr_asset_ensure_async(path, NULL);
   wgr_asset_add_task(task, on_ready, on_failed, ctx);
   ```

2. **Create (sync, path- or handle-based).** Inside the ready callback, build the
   resource/object from the now-local path. Returns a handle:

   ```c
   static void on_model_ready(const char *path, void *user) {
       ctx->model = wgr_model_create(path);       /* sync; reads local file */
   }
   ```

The decode-from-bytes step stays **internal** — it's never a public entry point.

## Target public surface

### Create (sync)

Single principle: **resources are created from a file (or a generator); objects
are created only from a resource handle — never from a path.** Bare `_create`
for both; the *noun* says which — a resource noun takes a path, an object noun
takes a handle. (No "create object from file" shortcuts; that two-in-one was the
shape the maintainer never liked.)

```c
/* resources — from a local path, or a generator */
wgr_handle_t wgr_texture_create(const char *path);
wgr_handle_t wgr_mesh_create(const char *path);
wgr_handle_t wgr_audio_create(const char *path);
wgr_handle_t wgr_font_create(const char *path, int size);
wgr_handle_t wgr_mesh_create_cube(float w, float h, float l); /* generated */

/* objects — only ever from their resource handle */
wgr_handle_t wgr_sprite3d_create(wgr_handle_t texture);
wgr_handle_t wgr_model_create(wgr_handle_t mesh);   /* was wgr_model_create_from_mesh */
wgr_handle_t wgr_sound_create(wgr_handle_t audio);  /* was wgr_sound_create(path)     */
wgr_handle_t wgr_music_create(wgr_handle_t audio);  /* was wgr_music_create(path)     */
wgr_handle_t wgr_text2d_create(wgr_handle_t font, float size);
```

### Asset (fetch/ensure) — rework wgr_asset to librl's shape
```c
typedef void (*wgr_asset_callback_fn)(const char *path, void *user_data);

int         wgr_asset_set_host(const char *host);
const char *wgr_asset_get_host(void);

int         wgr_asset_ensure(const char *path, const char *src);        /* sync */
wgr_handle_t wgr_asset_ensure_async(const char *path, const char *src);  /* → ASSET_TASK */
wgr_asset_add_task_result_t wgr_asset_add_task(wgr_handle_t task,
                                             wgr_asset_callback_fn on_success,
                                             wgr_asset_callback_fn on_failure,
                                             void *user_data);
void        wgr_asset_tick(void); /* pump the queue each frame (runtime-driven) */
/* + poll/finish/get_task_path/free_task as in librl */
```

`WGR_HANDLE_KIND_ASSET_TASK` already exists for the task handle.

### Removed (public)
- **Every `wgr_*_create_from_memory(...)`** — texture, mesh, model, sound, music
  (the only pointer-taking functions). Decode-from-bytes stays internal.
- **Object-from-file shortcuts** — `wgr_sprite3d_create_from_file`, and the
  bundled `wgr_model_create(path)` (mesh+model in one). Objects come from a
  resource handle; load the resource first.
- **Path-based sound/music create** — `wgr_sound_create(path)` /
  `wgr_music_create(path)` become handle-based (`…create(audio)`).
- `wgr_model_create_from_mesh` → renamed `wgr_model_create(mesh)`.
- The current byte-delivering `wgr_asset_load_async(path, on_loaded(data,size,…))`
  → replaced by the ensure API above (callbacks deliver a path).

### Not doing (corrections to the earlier draft)
- **No generic `wgr_asset_load(path) → handle`.** Creation is typed and sync; the
  asset layer ensures files, it doesn't create resources.
- **No generic `wgr_destroy(handle)`.** librl keeps typed `wgr_*_destroy`; we will
  too. (The 6-bit kind field *could* support a generic destroy later, but it's
  out of scope and not the established model.)

## New dependency: `wgr_fs`

Ensure needs a local-file layer (librl's `rl_fs`): a root dir on desktop, idbfs
on web, with fetch-from-host on miss. libwgrender has **no fs module yet**, so this is
net-new and is the substantive part of the work — `wgr_asset` ensure sits on top
of it.

## Phasing

**Phase 1 — kill pointers in user code (desktop-first).**
- Delete public `wgr_*_create_from_memory`; keep internal decoders.
- Add object-from-path creators where missing (`wgr_sprite3d_create_from_file`).
- Replace `wgr_asset`'s byte callback with the path-based ensure API. On desktop,
  "ensure" degenerates to "file exists on disk → fire callback"; the sync
  `wgr_*_create(path)` then reads it. (No host fetch yet.)
- Port `examples/*` to ensure-async → `_create(path)`; build clean; `make check`.

**Phase 2 — real ensure (web parity).**
- Add `wgr_fs` (root dir + idbfs) and host fetch (sokol_fetch) so `ensure`
  fetches missing files into the local store on web. Group-ensure variants.

**Phase 3 — embedded data (optional).**
- `wgr_fs_mount_memory(vpath, data, size)` for compiled-in blobs, so they load via
  `wgr_*_create("mem://…")` with no per-asset pointer in gameplay code.

## Enforcement (`make check`)
- Extend `tools/check_naming.sh` (or a sibling) to **fail on**:
  - any `_create_from_memory` in `include/`,
  - raw pointer params in `include/*.h` other than `const char *`.
- Add the handle-only rule to AGENTS.md § Naming/API.
- Update ARCHITECTURE.md §7 (public API shape) + the Asset→Resource→Object tables.

## Decisions locked
- **Creator naming:** bare `_create` everywhere; resource noun → path, object noun
  → handle. No `_create_from_file`/`_from_memory`. (Resolved by the principle above.)
- **`wgr_audio` is public.** Objects come from resource handles, so a sound needs an
  audio handle — `wgr_audio_create(path)` joins the public resource creators.

## Open questions
1. **Scope now:** Phase 1 only (desktop; removes the pointers and reshapes the
   API) and leave `wgr_fs`/web fetch for a follow-up, or build Phase 1+2 together?
2. **Music vs Sound:** both are now objects over an Audio handle, differing only
   by default loop. Keep both public surfaces, or collapse to `wgr_sound_create`
   + a loop setter and make `wgr_music_*` go away? (Separate from this plan, but
   adjacent.)
