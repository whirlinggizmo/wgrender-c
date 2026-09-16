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
make            # build lib/libsk.a
make examples   # build examples/build/*
make run        # build + run the hello example
make check      # enforce the "no backend leakage" invariant
make clean
```

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

## The loop model (important difference from librl)

librl was poll-driven: `while (rl_tick()) { ... }`. sokol_app owns the frame
loop and (on web) must drive it via requestAnimationFrame, so there is **no
`sk_tick()`**. libsk is callback-driven:

```c
#include "sk.h"

static void frame(void *user_data) {
    sk_render_begin();
    sk_render_clear_background(SK_COLOR_RAYWHITE);
    sk_shape_draw_rectangle(40, 40, 200, 120, SK_COLOR_SKYBLUE);
    sk_text_draw("hello", 40, 200, 24, SK_COLOR_DARKGRAY);
    sk_render_end();
}

int main(void) {
    sk_init_values(800, 600, "libsk", 0);  // configure (does not open window)
    sk_set_frame(frame, NULL);             // register per-frame callback
    return sk_run();                       // open window + run loop (blocks on desktop)
}
```

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
- `sk_text_draw` without a font uses `sokol_debugtext` (built-in 8x8 bitmap
  font). TTF fonts (`sk_font_create`, `sk_text_draw_ex`, `text2d`) go through
  fontstash.

## Not yet ported from librl

Main gaps: `sprite2d`, `text3d`, the remaining 3D shapes, per-object picking,
lighting controls, window/monitor control, language bindings and tests. See the
**librl parity** section of [docs/ROADMAP.md](docs/ROADMAP.md) for the full list,
the suggested order, and what was left out on purpose.
