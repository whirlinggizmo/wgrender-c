# libsk

A small game/graphics runtime built on [sokol](https://github.com/floooh/sokol),
evolving the ideas from `librl` (the raylib-backed `rl_*` library) rather than
porting it 1:1. Public symbols use the `sk_` prefix.

This is an early experiment. The first milestone is a **desktop GL vertical
slice**: window, clear, 2D shapes, text, and input.

## Build (desktop, Linux GL)

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
- Text uses `sokol_debugtext` (built-in 8x8 bitmap fonts) for now — no font
  handle yet. Real TTF/fontstash fonts come later.

## Not yet ported (next phases)

3D (camera3d, 3D shapes, scene, picking), textures/sprites, models + skeletal
animation, audio (sound/music), the scratch buffer + `_to_scratch` marshalling,
filesystem/asset loading, the web (wasm) build, and language bindings (trimmed
to a single binding until the API stabilizes).
