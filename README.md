# libsk

A small game/graphics runtime built on [sokol](https://github.com/floooh/sokol),
evolving the ideas from `librl` (the raylib-backed `rl_*` library) rather than
porting it 1:1. Public symbols use the `sk_` prefix.

Still early, but past the first vertical slice: desktop GL and web (WebGL2 /
WebGPU) builds, 2D shapes and text, TTF fonts, textures, 3D sprites, glTF
models with GPU skinning, a scene graph with picking, audio, and async assets.

## Direction: libsk is the primary library

As of 2026-09-16, **libsk is where new work happens**; librl is in maintenance mode.

- **Why:** the roadmap (materials and shaders, batched 2D, particles, render
  targets, GPU residency) needs direct control of the GPU pipeline, which sokol
  gives and raylib hides behind rlgl. sokol's callback loop and WebGL2/WebGPU
  backends make the web a first-class target instead of a JSPI special case. The
  handle-only API (enforced by `make check`) keeps language bindings cheap.
- **Cost:** libsk is an engine we build, not one we wrap. Loaders, audio formats,
  gamepad mappings, gestures, collision helpers and platform quirks that raylib
  covers must be written or pulled in (preferably as single-header libraries).
- **librl's role:** fixes only when needed; a reference for behavior and the
  baseline for parity tests. Archive it once parity is reached.
- **Parity means functional parity, not a 1:1 API.** Anything you could build with
  librl should be buildable with libsk, but each feature gets a fresh design using
  what librl taught us, rather than a copy of its functions. Port what future work
  needs first; roadmap items may come before some parity items.

## Build (desktop, Linux GL)

sokol links against the system's audio, GL and X11 libraries, so their dev
packages must be installed (the build checks and names any that are missing):

```sh
make deps       # install them via apt / dnf / pacman (uses sudo)
# or manually, e.g. Debian/Ubuntu:
#   sudo apt install libasound2-dev libgl-dev libx11-dev libxi-dev libxcursor-dev
```

Then:

```sh
make            # build build/desktop/libsk.a
make examples   # build examples/build/desktop/*
make run        # build + run the hello example
make check      # enforce the "no backend leakage" invariant
make test       # unit tests (tests/unit/; links the headless library)
make smoke      # run every example headless (no window, GPU or audio) for ~3 s, in parallel
make verify     # build + check + test + smoke (a few seconds): run before calling a change done
make HEADLESS=1 # build build/headless/libsk.a: sokol dummy GPU backend, no window or audio
                # headless apps run frames at 60/s until sk_request_quit(), or for
                # SK_HEADLESS_FRAMES frames when that environment variable is set
make clean
```

## Build (web: WebGL2 / WebGPU)

Needs Emscripten on `PATH` (`source <emsdk>/emsdk_env.sh`).

```sh
make wasm WASM_EXAMPLE=simple      # one example, WebGL2 (BACKEND=webgpu for WebGPU)
make wasm-all                      # every example -> examples/build/webgl2/
make serve                         # http://localhost:8000/ (assets mounted at /assets/)
make webcheck                      # build all, load each in a browser, fail on errors
make webcheck BACKEND=webgpu       # same for WebGPU (opens visible browser windows)
make webstart                      # startup times per example: cold, warm and hot visits
```

`make webcheck` (`tools/webcheck.mjs`) needs Node >= 22 and a Chromium-based
browser (Brave, Chrome or Chromium; override with `WEBCHECK_BROWSER`). It checks
four examples at a time, each in its own browser context, waits until each has
finished loading its assets, and fails an example on console errors, libsk
`[ERROR]`/`[FATAL]` logs, exceptions, sokol panics, a wrong/missing backend, or
assets still loading after 20 s. It saves a screenshot of each to
`examples/build/<backend>/webcheck/`. WebGL2
runs headless; WebGPU needs a visible window because headless browsers have no
GPU adapter. It catches crashes, errors and unfinished loads, not wrong-looking
output, so glance at the screenshots. The browser and
server it starts are always stopped, even if Node crashes or is killed (process
groups, a sweep by the run's unique profile directory, and a watchdog).

### Startup and hosting

A built site (`examples/build/<backend>/`) loads each program as `name.js?v=<hash>`
and `name.wasm?v=<hash>`: `tools/webdeploy.py` writes every file's hash into
`index.html`, so a file's URL changes when its content does. The page starts
downloading the wasm alongside the JS, and it compiles as it streams. To start fast,
a host should send:

- `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` (threaded builds; `WEB_THREADS=0`
  builds don't need them)
- `Content-Type: application/wasm` for `.wasm` (else it can't compile while streaming)
- `Cache-Control: public, max-age=31536000, immutable` for versioned requests
  (`?v=`), and `no-cache` for the page: a returning visit then revalidates only the
  page and fetches no code (a CDN must keep the query string in its cache key)
- gzip or brotli for `.js`, `.wasm` and `.html`

`make serve` sends no-store (every reload gets the latest build);
`tools/serve.py --cache --gzip` serves as above. `make webstart`
(`tools/webstart.mjs`) opens each example three times in a fresh browser profile
(cold, warm, and hot: Chrome's compiled-code cache), locally and on emulated 4G, and
times the download, compile, libsk's init, the first frame and the end of asset
loading from `sk:*` performance marks. `--devtools` and `--url` measure another
device's browser, such as a phone through `adb forward`.

## Build (Windows, cross-compiled)

With MinGW-w64 (`sudo apt install mingw-w64`), Windows builds come from Linux:

```sh
make windows          # build/windows/libsk.a, examples/build/windows/*.exe (OpenGL)
make windows-test     # unit tests, under Wine
make windows-smoke    # every example headless, under Wine
```

The tests and smoke run go through `tools/wine.sh`: `$WINE`, else `wine64` / `wine`
on `PATH`, else the newest Proton in a Steam library (Library > Tools). Its prefix is
`build/wine`. The `.exe` files are linked statically (no MinGW DLLs to ship).
`make verify` builds `windows` when MinGW is installed, so Windows code keeps
compiling. Wine runs the windowed examples too (OpenGL through the host's driver),
but their windows, audio and gamepads on real Windows are untested.

## Invariant: no backend leakage

sokol is an implementation detail. The public API (`include/*.h`) and example
code (`examples/*.c`) must **not** depend on sokol: no sokol `#include`s and no
sokol API identifiers (`sapp_`/`sgl_`/`sg_`/`sdtx_`/`saudio_`/`sfetch_`/
`SOKOL_`/`SAPP_`). Consumers see only `sk_*` / `SK_*`. All sokol usage lives in
`src/` and the vendored headers; all backend linkage lives in the Makefile.

`make check` (script: `tools/check_no_backend_leak.sh`) enforces this. The word
"sokol" in a prose comment is fine — only API symbols are flagged.

A note on key codes: `SK_KEY_*` values currently mirror the GLFW/sokol layout
because input is indexed straight from sokol key codes. That is an internal
detail — code to the `SK_KEY_*` names; a backend swap would remap in
`src/sk_input.c`, not in consumer code.

## The loop model

sokol_app owns the frame loop (on the web the browser drives it with
requestAnimationFrame), so libsk is callback-driven. Two callbacks, for two rates:

- **tick** (`sk_set_tick(fn, user, hz)`): simulation at a fixed rate. Runs 0..N
  times before each frame with the same `dt` every time. Physics and gameplay go
  here. It never draws.
- **frame** (`sk_set_frame(fn, user)`): once per rendered frame. Variable update
  and drawing. `dt` is the time since the previous frame; `tick_fraction` (0..1)
  is how far this frame is into the next tick, for drawing tick state smoothly.

```c
#include "sk.h"

static float prev_x, x;

static void tick(float dt, void *user_data) {
    prev_x = x;
    x += 120.0f * dt;                      // deterministic: dt is always 1/60 here
}

static void frame(float dt, float tick_fraction, void *user_data) {
    float draw_x = prev_x + (x - prev_x) * tick_fraction;   // smooth at any frame rate
    sk_render_begin();
    sk_render_clear_background(SK_COLOR_RAYWHITE);
    sk_shape2d_draw_rectangle((int)draw_x, 40, 200, 120, SK_COLOR_SKYBLUE);
    sk_render_end();
}

int main(void) {
    sk_init_values(800, 600, "libsk", 0);  // configure (does not open window)
    sk_set_tick(tick, NULL, 60);           // optional: fixed-rate simulation
    sk_set_frame(frame, NULL);             // required: per-frame update + draw
    return sk_run();                       // open window + run loop (blocks on desktop)
}
```

- Frames are vsync-locked by default. `sk_set_target_fps` is a power/heat cap on
  frames, not a simulation rate; use a tick for that.
- After a stall at most 5 ticks run per frame and the backlog is dropped.
- Input edges (pressed/released, mouse deltas) are relative to the callback
  reading them, so every key press is seen by exactly one tick.
- See `examples/tick.c` and [docs/PLAN-tick.md](docs/PLAN-tick.md).

The consumer owns `main()` and calls `sk_run()`; the sokol implementation TU is
compiled with `SOKOL_NO_ENTRY` so sokol does not generate its own entry point.

## Layout

```
include/        public sk_*.h headers
src/            implementation (one TU per subsystem)
src/internal/   shared, non-public declarations (handle pool, lifecycle hooks)
src/sk_sokol_impl.c   single TU that compiles the sokol headers (SOKOL_IMPL)
deps/sokol/     vendored sokol headers
examples/       example programs
reference/librl the raylib library this evolves from (read-only reference)
```

## Rendering notes

- 2D primitives are recorded via `sokol_gl` between `sk_render_begin()` and
  `sk_render_end()`; the swapchain pass (and the background clear) is opened in
  `sk_render_end()`, so `sk_render_clear_background()` works in librl's
  begin → clear → draw → end order even though sokol clears via the pass
  load-action.
- Draw order follows call order. sokol_gl content is recorded into sokol_gl
  layers and models into a queue; `sk_render_end()` replays both in the order
  they were submitted (see `src/internal/sk_render.h`).
- `sk_scene_draw()` draws each layer in two passes: opaque parts (depth writes
  on), then transparent parts (blended or faded model primitives, sprites,
  translucent shapes) sorted back to front with depth writes off. Direct
  `sk_*_draw()` calls outside a scene are not sorted against each other.
- 2D uses logical pixels (top-left origin, y down): 2D drawing, sprites, mouse
  positions and `sk_window_get_screen_size` all divide out the DPI scale, so layouts
  keep their size on high-DPI displays, which render at their full resolution
  (`SK_WINDOW_FLAG_LOW_DPI` renders at one pixel per logical pixel instead). Sprites (`sk_sprite2d_*`) in a scene draw
  after all 3D and are picked first; see `examples/sprite2d.c`.
- Lighting is explicit: models in a scene are lit only by lights added to that
  scene (`sk_light_create`, `sk_scene_add`) plus its ambient
  (`sk_scene_set_ambient`); a new scene is dark. Models drawn outside a scene are
  unlit. Shapes and sprites are unlit. See `examples/lights.c`.
- `sk_text_draw` without a font uses `sokol_debugtext` (built-in 8x8 bitmap
  font). TTF fonts (`sk_font_create`, `sk_text_draw_ex`, `text2d`) go through
  fontstash.
- Compressed textures: load `name.ktx` and libsk picks the file the GPU can sample,
  `name.bc7.ktx` (desktops), `name.astc.ktx` (phones), `name.etc2.ktx` (older phones)
  or `name.png`; on the web only that file downloads. Make them with
  `tools/compress_textures.sh name.png`. A quarter of the GPU memory, and no decoding
  or mipmap building at load (a 2K texture: ~1 ms instead of 60-200 ms); see
  `docs/PLAN-textures.md` and `examples/textures.c`.

## Not yet ported from librl

Main gaps: `text3d`, the remaining 3D shapes, per-object picking,
window/monitor control, language bindings and tests. See the
**librl parity** section of [docs/ROADMAP.md](docs/ROADMAP.md) for the full list,
the suggested order, and what was left out on purpose.
