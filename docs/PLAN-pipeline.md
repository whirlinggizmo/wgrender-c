# Plan: Loading pipeline (background preparation, budgeted GPU upload)

Status: **implemented (2026-09-17).** Decisions 1–5 as recommended; decision 6
changed (see "As built").

## Problem

`wgr_*_create(path)` reads, decodes and uploads on the main thread, so a load during
gameplay stalls a frame for as long as the load takes. The asset layer's callback
already runs on the main thread, so running the create from a callback doesn't
help.

## Measurements (2026-09-16)

CPU stages, from the real create code with temporary timers (headless build, one
core, `-O2`):

| Load | Total | Read | Decode | Mipmaps | glTF parse + primitives |
|---|---|---|---|---|---|
| 4096² JPEG texture | 118 ms | 2 | 97 | 18 | — |
| 4096² PNG texture | 243 ms | 2 | 222 | 15 | — |
| DamagedHelmet.glb (5 × 2048² JPEG) | 124 ms | 0 | 98 | 17 | 1 |
| FlightHelmet.gltf (15 × 2048² PNG) | 628 ms | 8 | 546 | 46 | 5 |
| Sponza.gltf (69 textures, 103 primitives) | 558 ms | 6 | 463 | 58 | 16 |
| Environment, 1K HDR (earlier) | 330 ms | | | | prefilter |

GPU upload, GL on the RTX 4080 (headless EGL, `glTexImage2D` for every mip level,
then `glFinish`):

| Upload | Time |
|---|---|
| 1024² + mipmaps | 2–9 ms |
| 2048² + mipmaps | 8–15 ms |
| 4096² + mipmaps | 42–51 ms |
| 64 MB vertex buffer | 43–54 ms |

Conclusions:

- **Decoding images is 80–90% of every load.** It is pure CPU work on bytes, so it
  can run on a worker. Mipmap generation (~10%) and glTF parsing and tangents go
  with it.
- **Uploads are the rest, and they must stay on the main thread** (sokol_gfx is
  single-threaded). Many uploads can be spread over frames, but sokol creates an
  image with all its mip levels in one call, so **one 4096² texture is a ~45 ms
  upload that can't be split**. The fix for that is compressed textures (KTX2 /
  Basis: about 4× less data and no mipmaps to generate), a separate roadmap item.
- **Found along the way:** Sponza fails to load because it needs 206 GPU buffers
  and sokol's default pool holds 128 (`BUFFER_POOL_EXHAUSTED`). Images and views
  also default to 128, so ~60 textured materials hit the same wall.

## Design

The asset task gets two more stages, and the public flow stays the same (ensure,
then create in the callback):

```
ensure (fetch; exists) ─▶ prepare (worker) ─▶ finish (main thread, budgeted) ─▶ callback
                           read, decode,        GPU upload, create the resource
                           mipmaps, parse,      under its path, hold a reference
                           tangents, prefilter
```

- **Each resource type registers a preparer** for the file extensions it loads,
  like formats already register dependency listers. A preparer has two halves: a
  `prepare(path) → CPU data` function that runs on a worker and touches no
  handles, sokol or globals, and a `finish(CPU data)` step that creates the
  resource on the main thread. The texture, mesh, environment and audio (decoded,
  not streamed) create functions are split along that line, and the sync
  `wgr_*_create(path)` runs both halves in a row, as today.
- **The task holds one reference** to the resource it finished. `wgr_*_create(path)`
  in the callback finds it by path (the existing dedupe) and adds its own
  reference. After the callback, the task drops its reference, so a resource
  nobody created in the callback is freed.
- **Finish is resumable and budgeted:** `wgri_asset_tick` runs finish steps until a
  per-frame time budget is used up, always doing at least one step so loading
  can't stall. A mesh finishes over several steps (buffers, then one texture per
  step), so Sponza spreads over frames instead of uploading 69 textures at once.
- **Workers:** a small pool (cores − 1, at most 4) started with the asset layer.
  Shutdown lets running jobs finish and drops queued ones. With zero workers,
  prepare runs on the main thread, one job per frame. Headless tests use that
  mode, so they stay deterministic.
- **Dependencies:** a glTF's images are prepared by the mesh preparer (it decodes
  them on the worker), not separately as textures.
- **Already loaded:** a task whose path already has a live resource of that type
  skips prepare and finish.
- **Logging from workers:** messages are queued and logged on the main thread (or
  the logger gets a lock; to check during implementation).

## Web

Emscripten threads (`-pthread`) need `SharedArrayBuffer`, which browsers only
allow on cross-origin isolated pages: the server must send
`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy:
require-corp`. A threaded build doesn't start on a page without those headers.

- `tools/serve.py` adds the headers. Hosts that can't set headers (GitHub Pages,
  for example) need the `coi-serviceworker` shim or a different host.
- The zero-worker mode stays available as a build option (`WEB_THREADS=0`). It
  runs the same pipeline on the main thread, one prepare per frame: no parallel
  decoding, but loads no longer stack into one long stall.
- Browser-native decoders (`createImageBitmap`, `decodeAudioData`) would work
  without isolation but cover only images and audio, add colour and alpha
  conversion pitfalls, and still copy pixels back on the main thread. Not
  proposed now.

## Public API additions

```c
/* Milliseconds per frame for finishing loads (GPU uploads). Default 4. At least one
 * step runs each frame, so a single large upload can exceed it. */
void wgr_asset_set_upload_budget(float milliseconds);

/* Ensure without preparing: the file is only made local (for files used as
 * something other than their extension's default resource). */
WGR_ASSET_FILE_ONLY = 1 << 1,

/* Groups (librl's ensure_many, handle-only): a task that finishes when all of its
 * members have, and fails if any does. Attach callbacks with wgr_asset_add_task. */
wgr_handle_t wgr_asset_group_create(void);
bool        wgr_asset_group_add(wgr_handle_t group, wgr_handle_t task);

/* 0..1 for a task or group (files fetched, prepared, finished), for loading screens. */
float wgr_asset_get_progress(wgr_handle_t task);
```

## Decisions

1. **How a task knows what to prepare:** by file extension (`.png/.jpg` texture,
   `.gltf/.glb` mesh, `.hdr` environment, `.wav/.ogg/.mp3` audio), with
   `WGR_ASSET_FILE_ONLY` to opt out. Existing code gets background loading with no
   changes. The alternative is a per-type ensure (`wgr_texture_load_async(path)`),
   which is explicit but doubles the loading API. A PNG used as an environment
   would be prepared as a texture for nothing unless the caller passes
   `FILE_ONLY`. Recommend: by extension.
2. **Web threads:** threaded web build requiring cross-origin isolation, with
   `WEB_THREADS=0` as the fallback. Recommend: yes. The alternative is no web
   threads (main thread, one job per frame), which avoids stacking stalls but keeps
   every decode stall.
3. **Upload budget:** 4 ms by default, settable, at least one step per frame.
   Recommend: yes.
4. **Groups and progress** (librl parity, loading screens): as above. Recommend: yes.
5. **sokol pool sizes:** raise the buffer, image and view pools (to 1024 each, a few
   MB of bookkeeping) and say which pool ran out in the error, as a separate small
   fix first. Recommend: yes.
6. **Measurement assets:** add DamagedHelmet (3.7 MB, CC BY 4.0) to
   `examples/assets` for a `loading` example that loads during gameplay and shows
   the worst frame time. Sponza and FlightHelmet (~50 MB each) stay out of the
   repo: `tools/fetch_bench_assets.sh` downloads them for a local benchmark.
   Recommend: yes.

## As built

- **Results** (`make loadbench`: Sponza + FlightHelmet loaded while frames run):

  | | Background | Synchronous |
  |---|---|---|
  | Desktop, headless build (CPU work only) | worst frame 17 ms (the loop's pacing), 0.77 s | 1.24 s stall |
  | WebGL2, threaded build | worst frame 70 ms, 2.96 s | 1.98 s stall |
  | WebGL2, `WEB_THREADS=0` | worst frame 1.11 s, 3.43 s | 1.92 s stall |

  The web's remaining 30–70 ms frames come from downloading and caching the files
  and from single 2048² texture uploads, which can't be split.
- **Loaders** (`src/internal/wgr_loader.h`): each resource type registers
  `prepare` (any thread), `finish` (main thread, one step per call), `discard`,
  `find` and `release` for its extensions; `wgri_loader_create` runs them inline for
  the sync creates, so both paths share one implementation. Registered: texture
  (`.png .jpg .jpeg`), mesh (`.gltf .glb`), environment (`.hdr`), audio
  (`.wav .ogg .mp3`). Fonts have none (a TTF load is cheap; glyphs rasterize on
  demand).
- **Mesh finish steps:** buffers, then one texture per step, then materials and
  the mesh itself. Only images that textures use are decoded. A `.glb`'s buffers
  point into the file's bytes, so the prepared mesh keeps them until discarded (a
  bug caught during implementation, now covered by `pipeline_mesh_textures`).
- **Groups hold their members' resources** until the group's own callbacks have
  run. Without that, a member's resource was freed after the member's callback and
  the group callback's creates loaded everything again, synchronously (found by
  the benchmark: a 1.2 s frame in the "background" load).
- **A sync create during a background load** of the same file: the pipeline uses
  the existing resource instead of creating a second one.
- **Failures:** a file that can't be decoded now fires the failure callback
  ("Asset couldn't be loaded"), where before the success callback's create failed.
- **Threads:** `src/wgr_thread.c` (POSIX, Win32, Emscripten pthreads). Workers:
  CPU cores − 1, at most 4 (internal `wgri_asset_set_worker_count` for tests).
  Windows is written but untested.
- **Web:** `-pthread -sPTHREAD_POOL_SIZE=4` by default; `WEB_THREADS=0` builds
  into `build/<backend>-nothreads`. `tools/serve.py` sends COOP/COEP. webcheck
  ignores the pthread workers' script requests (their completion is reported to
  the worker, so it waited for its deadline) and takes `--threads=0`. Object files
  now rebuild when the compile flags change. Neither Asyncify nor JSPI is used.
- **Decision 6 changed:** DamagedHelmet's model files are licensed CC BY 4.0 *and*
  CC BY-NC 4.0, so it isn't in the repo. `examples/loading.c` uses assets already
  there (two HDR environments at ~330 ms each, two models, textures) and shows a
  frame-time graph with background (A) and synchronous (S) loads.
  `tools/bench/fetch_assets.sh` downloads Sponza and FlightHelmet into the
  gitignored `examples/assets/bench/`.
- **Pools** are 4096 buffers, 2048 images and 4096 views (not 1024 each: a glTF
  primitive takes two buffers, and textures, targets and environments share the
  images). sokol's own error names the exhausted pool; libwgrender now fails the load
  instead of keeping a resource with an invalid buffer or image.
- **Logging from workers** needs nothing extra: the logger formats into a local
  buffer and writes one `fprintf` per message.
- **Tests** run the pipeline with zero and with two workers (`pipeline_*`, also
  under `SANITIZE=thread` and `address`).
- **Also fixed:** `wgri_fs_init` leaked its `getcwd` buffer (found by the new tests
  under ASan).
- **Found, not fixed** (docs/TASKS.md): the first frame drawing loaded PBR models
  stalls ~220 ms on WebGL2 (shader compile on first use); `wgr_request_quit` on web
  aborts in sokol_audio; the zero-worker mode prepares a whole glTF in one frame;
  `.glb` dependency listing reads the whole file on the main thread.

## Order and verification

1. Pool sizes (decision 5), with Sponza loading as the check.
2. Split texture, mesh, environment and audio create into prepare and finish; the
   sync create uses both. No behaviour change: existing tests and examples pass.
3. Worker pool, prepare and finish stages, budget, zero-worker mode. Unit tests:
   the callback's create returns the prepared resource, unclaimed resources are
   freed, failures, dependencies, shutdown with queued jobs, `SANITIZE=thread`.
4. Web threads (`-pthread`, serve.py headers, `WEB_THREADS=0`), webcheck on both
   backends in both modes.
5. Groups and progress; the `loading` example; benchmark before and after (worst
   frame while loading Sponza, desktop and WebGL2).
