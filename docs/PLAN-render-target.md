# Plan: Render to texture

Status: **implemented (2026-09-16).** Decisions 1–5 accepted as recommended. See
`wgr_texture_create_target`, `wgr_render_begin_texture` and `examples/render_target.c`;
"As built" below records details found during implementation. **Screen effects
(post-processing) followed on 2026-09-21**: see "Screen effects as built" at the end.

## Why

Draw into a texture, then use that texture like any other: on a sprite, a model
material, or a 2D quad.

- Minimaps, rear-view mirrors, security cameras, portals
- Low-resolution rendering scaled up (pixel-art look, virtual resolution for 2D)
- Rendering UI or text once into a texture
- Later: post-processing (needs full-screen shader passes, materials phase 2)

Environment lighting doesn't depend on this. It can prefilter its maps on the CPU
at load, which works everywhere, including WebGL2, where float render targets
need an extension.

## Where we are

- Everything draws to the screen. `wgr_render_begin/end` records a frame command
  list (sokol_gl layers + model draws, in call order) and replays it in one
  swapchain pass at `wgr_render_end`.
- Every pipeline (sokol_gl 2D/3D, model pipelines, the fontstash text pipeline) is
  built for the screen's color format, depth format and MSAA sample count.
- Aspect ratio and 2D coordinates come from the window size.

## Proposed design

### API

```c
/* include/wgr_texture.h */
/* A texture you can draw into (a render target), width x height pixels, cleared to
 * transparent black. Use it anywhere a texture goes. */
wgr_handle_t wgr_texture_create_target(int width, int height);

/* How a texture is sampled where it's drawn directly (sprites, wgr_texture_draw).
 * Materials have their own per-texture sampling. Default: clamp, linear. */
bool wgr_texture_set_sampling(wgr_handle_t texture, wgr_texture_wrap_t wrap_u,
                             wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter);

/* include/wgr_render.h */
/* Between wgr_render_begin and wgr_render_end: draw into `texture` (a target)
 * instead of the screen until wgr_render_end_texture. Everything works inside:
 * clear, 2D, 3D mode, scenes, models, sprites, text. */
bool wgr_render_begin_texture(wgr_handle_t texture);
void wgr_render_end_texture(void);
```

A render target is a texture resource (made by a generator, like
`wgr_mesh_create_cube`), not a new kind: every API that takes a texture accepts it.

### Frame semantics

- Drawing is still recorded during the frame and replayed at `wgr_render_end`: each
  target's pass first, in the order they were begun, then the screen.
- Inside a target: 2D coordinates are target pixels (top-left origin); 3D uses the
  active camera with the target's aspect ratio. `wgr_render_clear_background` sets
  that target's clear color (default transparent black).
- Using a target's texture while drawing into that same target isn't allowed
  (the GPU can't read and write it at once): logged once, skipped. Using a target in
  a target that renders earlier in the frame shows its previous frame's contents.
- Nesting (`begin_texture` inside `begin_texture`) isn't allowed: logged, ignored.
  A target can be drawn into again in the same frame (a second pass that keeps the
  first pass's contents).

### Format

Targets match the screen: its color format (RGBA8, or BGRA8 on WebGPU), a depth
buffer, and its MSAA sample count, resolved into the texture. That way every
existing pipeline (sokol_gl, models, text) works in a target unchanged, and
edges are as smooth as on screen. Targets have no mipmaps.

## Decisions

1. **A target is a texture** (`wgr_texture_create_target`), not a separate
   RenderTarget object with its own handle kind. Recommend: texture.
2. **Targets match the screen's format and MSAA** (above), instead of choosing
   format and sample count per target. Recommend: match the screen; per-target
   formats (e.g. float for HDR) come with post-processing.
3. **Clear on every pass by default**, keeping contents between frames only as a
   later option (`wgr_render_begin_texture` flags, e.g. for trails or painting).
   Recommend: clear by default.
4. **Add `wgr_texture_set_sampling`** now, since low-resolution upscaling needs
   nearest filtering on sprites. Recommend: yes.
5. **Screen readback / screenshots** (`wgr_texture_save`, reading pixels): out of
   scope; tracked separately.

## As built

- **Bottom-up targets on GL / WebGL2.** A texture rendered on a backend whose
  framebuffer origin is bottom-left stores its rows bottom-up. Rather than flip
  projections while rendering (which would also flip winding and can't reach the
  bitmap text shader), whoever samples a target maps v to 1 - v
  (`wgr_texture_is_flipped`): sprites, `wgr_texture_draw`, sprite3d and material UV
  transforms. Verified identical on WebGL2 and WebGPU.
- **Bitmap text (`wgr_text_draw`) works in targets:** each pass records into its own
  sokol_debugtext layer, drawn at the end of that pass.
- **Second pass into the same target in a frame** loads the earlier contents instead
  of clearing.
- **Self-use:** a target sampled inside its own pass binds the default white
  texture (warned once), rather than skipping the draw.
- Model placements are per pass, so a model drawn into several targets gets each
  target's aspect ratio. Up to 16 target passes per frame.

## Screen effects as built (2026-09-21)

Post-processing, the "later" in "Why" above. The frame draws into a render target and
each effect redraws it, the last one onto the screen.

- **An effect is a material**, so it needs no new resource kind and its parameters and
  textures are set exactly as a surface material's are, any frame:
  `wgr_render_add_effect(material)`, `wgr_render_clear_effects()`,
  `wgr_render_effect_count()`. Up to 8, applied in the order added, each holding a
  reference. There is no "remove one": a program that toggles effects clears the chain
  and adds what it wants (`examples/postprocess.c`), which keeps the order explicit.
- **A shader says which it is.** A fragment shader that includes `wgr_screen` instead of
  `wgr_surface` is a screen effect: `tools/shaderpack.py` then builds one program with a
  full-screen-triangle vertex stage (no vertex buffer, no vertex hook) instead of the
  four surface programs, and writes the kind into the file (`.wgrshader` format 5). Using
  a screen material on a model or sprite is refused, and a surface material as an
  effect, because the program simply isn't there.
- **What a screen shader sees:** `wgr_screen_uv` (0..1 from the top-left corner, the
  same on every backend), `wgr_screen_color()` and `wgr_screen_color_at(uv)` (the frame,
  decoded to linear), `wgr_screen_size()` / `wgr_screen_texel()`, `wgr_time()`, and
  `wgr_output(color, alpha)`, which encodes sRGB and nothing else — no tint, alpha mode
  or tone mapping, since an effect draws over the finished frame.
- **The buffers are ordinary render targets** (`wgr_texture_create_target`), so they
  match the screen's format, depth and MSAA and every pipeline that draws the frame
  works in them unchanged, including sokol_gl and text. Two are enough — effect i reads
  one and writes the other — and the second is only made when effects follow each
  other. They follow the framebuffer size and are rebuilt when it changes.
- **The core doesn't know about materials.** The chain lives in its own optional module
  (`src/wgr_effect.c`), which registers `wgr_render_hooks.effects_begin` (where the
  screen's pass draws) and `effects_draw` (the chain, ending on the swapchain). A
  program that never adds an effect doesn't link it (`make check`).
- **GL's bottom-up targets** are handled as elsewhere: the shader gets a flag in its
  frame block and flips v when it samples, so `wgr_screen_uv` means the same thing on
  GL, WebGL2 and WebGPU. Checked on all three.
- **Not in this phase** (recorded in TASKS): HDR/float targets, so a chain can tone map
  after bloom; keeping contents between frames; targets without depth or MSAA (an
  effect chain allocates both today); the depth buffer as an input (fog, depth of
  field); effects on a render target's pass rather than the screen.

## Verification

- Unit tests: command-list recording per pass (pass order, target sizes, nested and
  self-use rejection), sampler selection.
- New example `examples/render_target.c`: a spinning model rendered into a
  low-resolution target shown enlarged with nearest filtering, a minimap from a
  top-down camera, and text rendered into a texture on a model material.
- Screenshots on desktop GL, WebGL2 and WebGPU; `make test`, `make smoke`,
  `make check`, `make webcheck` (both backends); CI.
