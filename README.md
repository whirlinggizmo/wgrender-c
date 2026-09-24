# wgrender

A small game/graphics runtime built on [sokol](https://github.com/floooh/sokol),
evolving the ideas from `librl` (the raylib-backed `rl_*` library) rather than
porting it 1:1. Public symbols use the `wgr_` prefix; the library it builds is
`libwgrender.a`.

Still early, but past the first vertical slice: Linux and Windows (GL) and web (WebGL2 /
WebGPU) builds, 2D shapes and text, TTF fonts, textures, 3D sprites, glTF
models with GPU skinning, a scene graph with picking, audio, and async assets.

**The examples run in a browser: https://whirlinggizmo.github.io/wgrender-c/** —
every example, published from `main` by `.github/workflows/pages.yml`. It is the
`web-webgl2-nothreads` build, because GitHub Pages can't send the COOP/COEP headers
a threaded build needs; nothing needs threads, but asset decoding runs on the main
thread there, which `loading` reports rather than hides. Locally, `tools/serve.py`
sends those headers, so the same examples load on worker threads.

## Where it comes from

wgrender evolved from librl, a raylib-backed library, and reached functional parity
with it on 2026-09-21 -- everything librl could do, designed fresh rather than copied.
The move was for direct control of the GPU pipeline (materials, batching, particles,
render targets, residency), which sokol gives and raylib hides behind rlgl, and for a
web target that is a first-class build rather than a JSPI special case. The price is an
engine we build rather than wrap: loaders, audio formats, gamepad mappings and platform
quirks are written here or pulled in as single-header libraries. What librl taught us,
and what was left out on purpose, is in [docs/ROADMAP.md](docs/ROADMAP.md).

## Build

```sh
cmake --preset linux-release && cmake --build --preset linux-release   # library + every example
build/linux/release/simple                                              # run from this directory
```

That's Linux; a Mac's preset is `macos-release`, and Windows' `windows-msvc`. Each builds
into `build/<platform>/<variant>/`. CMake and Python 3, on Windows (Visual Studio opens
this folder), Linux and macOS; the web builds (WebGL2, WebGPU) need Emscripten. Everything else — presets, tests, the web,
Windows from Linux, generated files, benchmarks — is in [BUILDING.md](BUILDING.md).

## Invariant: no backend leakage

sokol is an implementation detail. The public API (`include/*.h`) and example
code (`examples/*.c`) must **not** depend on sokol: no sokol `#include`s and no
sokol API identifiers (`sapp_`/`sgl_`/`sg_`/`sdtx_`/`saudio_`/`sfetch_`/
`SOKOL_`/`SAPP_`). Consumers see only `wgr_*` / `WGR_*`. All sokol usage lives in
`src/` and the vendored headers; all backend linkage lives in `build.json`.

`tools/check.py` (the `check` test) enforces this. The word
"sokol" in a prose comment is fine — only API symbols are flagged.

A note on key codes: `WGR_KEY_*` values currently mirror the GLFW/sokol layout
because input is indexed straight from sokol key codes. That is an internal
detail — code to the `WGR_KEY_*` names; a backend swap would remap in
`src/wgr_input.c`, not in consumer code.

## The loop model

sokol_app owns the frame loop (on the web the browser drives it with
requestAnimationFrame), so wgrender is callback-driven. Two callbacks, for two rates:

- **tick** (`wgr_set_tick(fn, user, hz)`): simulation at a fixed rate. Runs 0..N
  times before each frame with the same `dt` every time. Physics and gameplay go
  here. It never draws.
- **frame** (`wgr_set_frame(fn, user)`): once per rendered frame. Variable update
  and drawing. `dt` is the time since the previous frame; `tick_fraction` (0..1)
  is how far this frame is into the next tick, for drawing tick state smoothly.

```c
#include "wgr.h"

static float prev_x, x;

static void tick(float dt, void *user_data) {
    prev_x = x;
    x += 120.0f * dt;                      // deterministic: dt is always 1/60 here
}

static void frame(float dt, float tick_fraction, void *user_data) {
    float draw_x = prev_x + (x - prev_x) * tick_fraction;   // smooth at any frame rate
    wgr_render_begin_frame();
    wgr_render_clear_background(WGR_COLOR_RAYWHITE);
    wgr_shape2d_draw_rectangle((int)draw_x, 40, 200, 120, WGR_COLOR_SKYBLUE);
    wgr_render_end_frame();
}

int main(void) {
    wgr_init_values(800, 600, "libwgrender", 0);  // configure (does not open window)
    wgr_set_tick(tick, NULL, 60);           // optional: fixed-rate simulation
    wgr_set_frame(frame, NULL);             // required: per-frame update + draw
    return wgr_run();                       // open window + run loop (blocks on desktop)
}
```

- Frames are vsync-locked by default. `wgr_set_target_fps` is a power/heat cap on
  frames, not a simulation rate; use a tick for that.
- After a stall at most 5 ticks run per frame and the backlog is dropped.
- Input edges (pressed/released, mouse deltas) are relative to the callback
  reading them, so every key press is seen by exactly one tick.
- See `examples/tick.c` and [docs/PLAN-tick.md](docs/PLAN-tick.md).

The consumer owns `main()` and calls `wgr_run()`; the sokol implementation TU is
compiled with `SOKOL_NO_ENTRY` so sokol does not generate its own entry point.

## Layout

```
include/        public wgr_*.h headers
shaders/wgr.glsl what custom material shaders get from wgrender (tools/shaderpack.py)
src/            implementation (one TU per subsystem)
src/internal/   shared, non-public declarations (handle pool, lifecycle hooks)
src/wgr_sokol_impl.c   single TU that compiles the sokol headers (SOKOL_IMPL)
deps/sokol/     vendored sokol headers
examples/       example programs
tests/unit/     unit tests (`ctest --preset linux-headless`; no display or GPU)
tools/          build, check and generator scripts (Python), benchmarks (tools/bench), the web dev server
cmake/          the MinGW toolchain file (the windows-mingw presets)
build.json      the build as data: sources, and per target defines, flags and libraries
docs/           ARCHITECTURE.md, ROADMAP.md, TASKS.md and one PLAN-*.md per feature
```

## Rendering notes

- 2D primitives are recorded via `sokol_gl` between `wgr_render_begin_frame()` and
  `wgr_render_end_frame()`; the swapchain pass (and the background clear) is opened in
  `wgr_render_end_frame()`, so `wgr_render_clear_background()` works in librl's
  begin → clear → draw → end order even though sokol clears via the pass
  load-action.
- Draw order follows call order. sokol_gl content is recorded into sokol_gl
  layers and models into a queue; `wgr_render_end_frame()` replays both in the order
  they were submitted (see `src/internal/wgr_render.h`).
- `wgr_scene_draw()` draws each layer in two passes: opaque parts (depth writes
  on), then transparent parts (blended or faded model primitives, sprites,
  translucent shapes) sorted back to front with depth writes off. Direct
  `wgr_*_draw()` calls outside a scene are not sorted against each other.
- 2D uses logical pixels (top-left origin, y down): 2D drawing, sprites, mouse
  positions and `wgr_window_get_screen_size` all divide out the DPI scale, so layouts
  keep their size on high-DPI displays, which render at their full resolution
  (`WGR_WINDOW_FLAG_LOW_DPI` renders at one pixel per logical pixel instead). Sprites (`wgr_sprite2d_*`) in a scene draw
  after all 3D and are picked first; see `examples/sprite2d.c`.
- Lighting is explicit: models in a scene are lit only by lights added to that
  scene (`wgr_light_create`, `wgr_scene_add`) plus its ambient
  (`wgr_scene_set_ambient`); a new scene is dark. Models drawn outside a scene are
  unlit. A 3D sprite is unlit until it's given a material
  (`wgr_sprite3d_set_material`): with one it's shaded like a model (the sprite's
  texture is the base color, its tint the vertex color, plus the material's normal,
  metallic-roughness, occlusion and emissive maps), lit by the scene's lights and
  environment, which are chosen once per batch. Shapes are always unlit. See
  `examples/lights.c`.
- Shadows: directional and spot lights cast when asked
  (`wgr_light_set_casts_shadows`), drawing what casts into a depth map once a frame;
  models, lit 3D sprites and custom shaders are darkened by it. Up to four lights cast
  at once, sharing one map (a layer each). Per light: how
  far its shadows reach, the map size, the depth bias (in texels), how much light a
  shadow takes away and what colour it leaves. Per model: whether it casts, and
  whether it receives. See `docs/PLAN-shadows.md` and `examples/shadows.c`.
- Model instancing: models that agree on everything but where they stand — the same
  mesh and material, a forest or a crowd — are drawn together, however they were added
  to the scene. Nothing to ask for: what differs per model (its transform, its tint)
  travels in a per-frame data texture, and wgrender batches what it can. 4000 such models
  cost 0.6 ms to submit instead of 4.7; the shadow pass and custom material shaders
  batch them the same way.
  See `docs/PLAN-instancing.md` and `examples/instancing.c`.
- Assets come from the same place on both platforms: give `wgr_asset_set_host` a URL
  and a logical path resolves against it everywhere. The browser downloads and caches
  it on web; on desktop wgrender asks the program's fetcher
  (`wgr_asset_set_fetcher`) to write the file, then caches it in a directory, so the
  library carries no HTTP client and no TLS. See `examples/fetch.c`, which wires one up
  with `curl` in twenty lines and downloads this repo's own assets over HTTPS — none of
  the TLS being wgrender's.
- Assets survive a host that compresses: a web download is one plain GET, decoded by
  the browser, so a host that gzips a `.glb` or `.ttf` (GitHub Pages does) can't hand
  the loader a gzip stream. A cached file that won't load is dropped and fetched once
  more; `wgr_asset_evict` and `wgr_asset_clear_cache` are there for a program that
  knows better.
- Frustum culling: a scene skips members the camera can't see, testing their bounds
  against the view before anything is submitted — 4000 models behind the camera cost
  0.5 ms a frame instead of 6.8. A caster whose shadow could still fall into view is
  kept, and the shadow pass itself only redraws the casters that light's map can hold.
  `wgr_scene_set_culling(scene, false)` turns it off to see everything submitted.
  See `docs/PLAN-culling.md`.
- All text is TrueType, through fontstash. `wgr_text_draw` without a font uses the
  built-in font (an ASCII subset of JetBrains Mono embedded in the library,
  `src/fonts/wgr_default_font.h`); `wgr_font_create` loads others (`wgr_text_draw_ex`,
  `text2d`, `text3d`).
- Compressed textures: load `name.ktx` and wgrender picks the file the GPU can sample,
  `name.bc7.ktx` (desktops), `name.astc.ktx` (phones), `name.etc2.ktx` (older phones)
  or `name.png`; on the web only that file downloads. Make them with
  `tools/compress_textures.py name.png`. A quarter of the GPU memory, and no decoding
  or mipmap building at load (a 2K texture: ~1 ms instead of 60-200 ms); see
  `docs/PLAN-textures.md` and `examples/textures.c`. For a glTF model,
  `tools/compress_textures.py --gltf model.gltf` writes `model.ktx.gltf`, which loads
  its textures the same way (and stays a valid glTF for other viewers).
- Custom shaders: write a fragment shader (and optionally a vertex hook) against
  `shaders/wgr.glsl`, which gives it the surface, time, camera, the scene's lights and
  environment, and `wgr_output` (tint, alpha cutoff, tone mapping, sRGB). `tools/shaderpack.py name.glsl`
  compiles it for GL, WebGL2 and WebGPU into `name.wgrshader`; load that with
  `wgr_shader_create` (or through `wgr_asset`) and use it with
  `wgr_material_create_custom`. Its parameters and textures are set by the names in
  the shader. The same shader draws sprites (`wgr_sprite3d_set_material`,
  `wgr_sprite2d_set_material`; `wgr_sprite_color()` is the sprite's texture and tint),
  with the same lights and environment a model shader gets.
  See `docs/PLAN-materials.md` and `examples/shaders.c`.
- Screen effects (post-processing): a shader that includes `wgr_screen` instead of
  `wgr_surface` redraws the finished frame — `wgr_render_add_effect(material)` puts it in
  a chain (up to 8, in order; the frame goes into a texture and the last effect draws
  onto the screen). Its parameters are the material's, so an effect can change every
  frame. See `docs/PLAN-render-target.md` and `examples/postprocess.c`.

## Bindings

wgrender stays a plain C library; a binding is its own repo on the public API. The
handle-only surface (every parameter a handle, a number, an enum or a `const char *`;
`tools/check.py` enforces it) is what keeps one cheap to write and to keep in step.

| Language | Repo | State |
|---|---|---|
| Haxe | [wgrender-hx](https://github.com/whirlinggizmo/wgrender-hx) | in development: hxcpp (desktop) and JS (web) targets, generated from the headers |
| Nim | [wgrender-nim](https://github.com/whirlinggizmo/wgrender-nim) | in development: desktop and web, wgrender compiled in from `build.json` |
| Beef | [wgrender-beef](https://github.com/whirlinggizmo/wgrender-beef) | early: desktop and web examples |
