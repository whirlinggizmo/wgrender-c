# wgrender history

What was planned, decided, built and measured, kept for *why* things are the way they
are and for what was already tried. Nothing here describes the code as it is now: for
that, read the headers (`include/*.h`) and the code, then [ARCHITECTURE.md](ARCHITECTURE.md).
What's left to do is in [TASKS.md](TASKS.md), and the open plans in `docs/PLAN-*.md`.
Text here is kept as it was written, so a name or a path in it may since have changed.

## Contents


- [2D / UI layer](#2d--ui-layer)
- [a sprite renderer, and particle emitters](#a-sprite-renderer-and-particle-emitters)
- [a web asset cache that notices changed files](#a-web-asset-cache-that-notices-changed-files)
- [Audio — streamed music and mixing off the main thread](#audio--streamed-music-and-mixing-off-the-main-thread)
- [colors are values, not handles](#colors-are-values-not-handles)
- [compressed textures](#compressed-textures)
- [Environment lighting (image-based lighting) and tone mapping](#environment-lighting-image-based-lighting-and-tone-mapping)
- [Fixed-rate tick + frame callback timing arguments](#fixed-rate-tick--frame-callback-timing-arguments)
- [Frustum culling](#frustum-culling) (open part: [PLAN-culling.md](PLAN-culling.md))
- [handle-only public API (librl-style asset/resource split)](#handle-only-public-api-librl-style-assetresource-split)
- [Lighting (light objects, per-scene lighting)](#lighting-light-objects-per-scene-lighting)
- [Load on create, and polled tasks instead of callbacks](#load-on-create-and-polled-tasks-instead-of-callbacks) (open part: [PLAN-tasks.md](PLAN-tasks.md))
- [Loading pipeline (background preparation, budgeted GPU upload)](#loading-pipeline-background-preparation-budgeted-gpu-upload)
- [Materials and shaders](#materials-and-shaders)
- [Model instancing](#model-instancing) (open part: [PLAN-instancing.md](PLAN-instancing.md))
- [Remaining librl parity](#remaining-librl-parity)
- [Render to texture](#render-to-texture)
- [Shadows](#shadows) (open part: [PLAN-shadows.md](PLAN-shadows.md))
- [sprite2d (screen-space sprites)](#sprite2d-screen-space-sprites)
- [UI through libwgrender's public API](#ui-through-libwgrenders-public-api) (open part: [PLAN-ui.md](PLAN-ui.md))
- [wgr_fs + web-capable ensure (Phase 2)](#wgr_fs--web-capable-ensure-phase-2)
- [Window and monitor control](#window-and-monitor-control)
- [Tasks done](#tasks-done)
- [ARCHITECTURE.md status log](#architecturemd-status-log)

## 2D / UI layer

Status: **done** (2026-09-17). All four steps are implemented — see "As built" for
what shipped and how it differs from the proposal. Examples: `ui` (HUD over a 3D
model) and `tilemap` (then `2d`: a scrolling, zooming 2D world).

### What exists

- **sprite2d** (source rect, pivot, flip, tint, alpha-tested picking) and **text2d**
  (position, size, color, pickable by rectangle), both scene members in screen space.
- **sprite3d** (texture, transform, uniform size, facing modes including FREE and
  Y_UP, tint, alpha-tested picking); **orthographic camera3d**.
- **Immediate 2D shapes:** rectangle (filled/lines), line, circle (filled/lines),
  triangle. No retained 2D shapes.
- **Scenes:** 2D members draw after all 3D layers, in layer order (then the order
  added); `wgr_scene_pick` hits 2D members first, topmost first, then 3D.
- **Flags on every drawable** (model, shape, sprite2d, sprite3d, text2d, text3d):
  `set_visible`/`is_visible`, `set_pickable`/`is_pickable`. Picks skip invisible
  and non-pickable objects.
- **Input:** mouse position/delta/wheel, keys and buttons with edges
  (`WGR_BUTTON_UP/PRESSED/DOWN/RELEASED`, relative to the running tick or frame),
  typed characters.
- **Not there:** hover/press/click state, a way to disable an object's interaction,
  retained 2D shapes, clipping, nine-slice, text alignment or wrapping, touch as a
  pointer. `wgr_render_begin_mode_2d(camera)` ignores its camera (so did librl's).

Direction already decided (ROADMAP): **no GUI toolkit in the core.** In-game UI is
built from sprites, text and shapes; layout can come later from a small library
such as Clay; developer UI (Dear ImGui) is an optional module outside the core.

### Goals

1. **In-game UI and HUD:** panels, buttons, bars, labels on screen, reacting to the
   pointer, over a 2D or 3D world.
2. **2D games:** a world that scrolls and zooms, with the same scene, layer and
   picking model as 3D.

### Proposed design

#### 1. Screen space for UI, orthographic 3D for 2D worlds (no camera2d)

librl had one camera (3D) and drew 2D in screen pixels; libwgrender does the same. A
separate 2D camera would only serve 2D worlds, and libwgrender can already draw those with
what it has: an **orthographic camera3d looking down -Z at sprite3d objects in the XY
plane** (FREE facing), with the same scenes, depth order, transparency sorting and
ray picking as 3D, and one world unit per pixel when pixel-exact placement matters.

- sprite2d, text2d and 2D shapes stay **screen space** (UI, HUD).
- sprite3d gains what 2D worlds need from sprite2d:

```c
bool wgr_sprite3d_set_source(wgr_handle_t sprite, float x, float y, float width, float height);
     /* texture pixels (sprite sheets, atlases); default: whole texture */
bool wgr_sprite3d_set_pivot(wgr_handle_t sprite, float x, float y);     /* 0..1, default center */
bool wgr_sprite3d_set_extent(wgr_handle_t sprite, float width, float height);
     /* world size; wgr_sprite3d_set_size(s) stays as the square/aspect-kept shorthand */
```

- `wgr_render_begin_mode_2d(camera)` loses its unused camera parameter (screen space
  only). Revisit a camera2d only if 2D worlds on orthographic 3D prove awkward.

#### 2. Retained 2D shapes (screen space)

Shapes already have handles (3D). Add 2D variants as scene 2D members:

```c
bool wgr_shape_set_rectangle_2d(wgr_handle_t shape, float width, float height, float corner_radius);
bool wgr_shape_set_circle_2d(wgr_handle_t shape, float radius);
bool wgr_shape_set_line_2d(wgr_handle_t shape, float x0, float y0, float x1, float y1, float thickness);
bool wgr_shape_set_transform_2d(wgr_handle_t shape, float x, float y, float rotation, float scale_x, float scale_y);
bool wgr_shape_set_outline(wgr_handle_t shape, float thickness);         /* 0 = filled (default) */
```

Rounded rectangles and thick lines are what UI panels and bars need. Picked by
their exact shape.

#### 3. `enabled` on every drawable

Following `visible` and `pickable`, on model, shape, sprite2d, sprite3d, text2d and
text3d (lights already have it):

```c
bool wgr_<kind>_set_enabled(wgr_handle_t object, bool enabled);   /* default true */
bool wgr_<kind>_is_enabled(wgr_handle_t object);
```

- **visible:** drawn or not.
- **pickable:** hit by picks or not; not pickable lets the pointer through to what's
  behind.
- **enabled:** whether a hit reacts. A disabled object is drawn, still blocks the
  pointer and is still reported by `wgr_scene_pick` and `wgr_scene_get_hovered` (a UI
  can show a "disabled" tooltip), but its hover, press and click stay `UP`/false.

#### 4. Pointer interaction, polled per object with edges

A scene opts in, then updates its interaction state once per frame from one pick of
the pointer, against the positions objects had when last drawn (one frame of
latency, as usual for retained UI):

```c
bool wgr_scene_set_interactive(wgr_handle_t scene, bool interactive);   /* default false */

wgr_handle_t       wgr_scene_get_hovered(wgr_handle_t scene);                   /* topmost under the pointer, or 0 */
wgr_button_state_t wgr_scene_get_hover(wgr_handle_t scene, wgr_handle_t object);
    /* UP: not under the pointer, PRESSED: entered this frame, DOWN: under it,
       RELEASED: left this frame */
wgr_button_state_t wgr_scene_get_press(wgr_handle_t scene, wgr_handle_t object);
    /* the primary button, for a press that started on this object: PRESSED this
       frame, DOWN while held (also when dragged off), RELEASED this frame */
bool              wgr_scene_is_clicked(wgr_handle_t scene, wgr_handle_t object);  /* released while still over it */
bool              wgr_input_is_pointer_captured(void);
    /* the current press started on a 2D member of an interactive scene: game
       controls (camera drag, 3D selection) should ignore it */
```

- **Members:** any pickable member, 2D and 3D (models, shapes, sprites, text), in
  the scene's pick order (2D topmost first, then nearest 3D). `wgr_scene_pick` stays
  for other points and one-off queries.
- **Edges** use the input enum and the same frame semantics as keys and buttons.
- **Capture only for 2D hits**, so hovering and clicking 3D objects still works
  while UI blocks presses that land on it.
- **Pointer = mouse + primary touch** (touch input lands with this step), so UI works
  in mobile browsers without separate code.
- Handle and enum returns only: cheap per call, including through bindings.

#### 5. UI drawing essentials

```c
bool wgr_sprite2d_set_nine_slice(wgr_handle_t sprite, float left, float top, float right, float bottom);
     /* borders in source pixels; the middle stretches, corners don't */
bool wgr_text2d_set_align(wgr_handle_t text, wgr_text_align_t horizontal, wgr_text_align_t vertical);
bool wgr_text2d_set_max_width(wgr_handle_t text, float width);   /* wrap at words; 0 = no wrap */
bool wgr_scene_set_clip(wgr_handle_t scene, int layer, float x, float y, float width, float height);
     /* clip a layer's 2D members to a screen rectangle (scroll areas, panels); 0 size = none */
void wgr_render_begin_clip(float x, float y, float width, float height);  /* immediate drawing */
void wgr_render_end_clip(void);
```

#### Not in this plan

- **A batched 2D renderer.** sprites and shapes draw through sokol_gl, fine for UI and
  small 2D games; build it with particles, measured against a sprite-heavy example.
- **Layout (Clay), widgets, themes, Dear ImGui:** outside the core, later.
- **Sprite animation (frame sequences):** set the source rect per frame; revisit with
  a real use.

### Decisions

1. **No camera2d:** UI in screen space; 2D worlds with an orthographic camera3d and
   sprite3d, which gains source rect, pivot and extent; drop begin_mode_2d's unused
   camera parameter. Recommend: yes.
2. **Retained 2D shapes** (rounded rectangles, thick lines, outlines). Recommend: yes.
3. **`enabled` on every drawable**, next to visible and pickable. Recommend: yes.
4. **Pointer interaction:** opt-in per scene; per-object hover and press as
   UP/PRESSED/DOWN/RELEASED, clicked, hovered handle; 2D and 3D members; capture for
   2D hits; mouse + primary touch. Recommend: yes.
5. **UI essentials:** nine-slice, text alignment and wrap, clipping. Recommend: yes.
6. **Order:** (1) enabled + pointer interaction + touch, (2) retained 2D shapes,
   (3) UI essentials, (4) sprite3d source/pivot/extent for 2D worlds. Examples: `ui`
   (HUD over the 3D gumshoe: panel, buttons with hover/press/disabled, a bar, wrapped
   text, a clipped scrolling list, a clickable 3D model) and `tilemap` (then `2d`: a scrolling, zooming
   2D world on an orthographic camera with sprite sheets and picking). Recommend: yes.

### As built

#### Step 1: enabled, pointer interaction, touch

- `wgr_<kind>_set_enabled` / `is_enabled` on model, shape, sprite2d, sprite3d, text2d,
  text3d; kinds register their getter with the scene (`wgri_scene_register_enabled`).
- `wgr_scene_set_interactive`, `wgr_scene_is_interactive`, `wgr_scene_get_hovered`,
  `wgr_scene_get_hover`, `wgr_scene_get_press`, `wgr_scene_is_clicked`, and
  `wgr_input_is_pointer_captured`, as designed.
- The runtime updates interaction once per frame before the ticks
  (`wgri_scene_update_interaction`), from the frame's pointer edges; edges are kept per
  context like input (frame edges cleared after the frame callback, tick edges after
  each tick and carried over frames without ticks, up to 8 hover changes).
- The interaction pick reads the scene's camera without changing the active camera
  (unlike `wgr_scene_pick`) and doesn't count pick statistics.
- Capture lasts from the press frame through the release frame.
- Touch: the first touch drives the pointer (move + left button); other fingers are
  ignored. A multi-touch API is still open (TASKS).
- `examples/ui.c`: buttons with hover/press colors, a click counter, a button toggled
  enabled/disabled, the gumshoe as a 3D member (hover highlight, click to animate), and
  a camera orbit that ignores drags starting on UI. Checked in the browser with real
  (CDP) mouse events: hover, press and capture.

#### Step 2: retained 2D shapes

- Retained 2D shapes: a rounded rectangle (radius clamped to half the shorter side),
  a circle, a thick line (butt ends), a 2D transform and an outline for rectangles and
  circles. **A 2D rectangle's origin is its top-left corner**, like the immediate
  `wgr_shape2d_draw_rectangle(x, y, w, h)` and how UI is laid out; circles are centered.
  (Named `wgr_shape_set_*_2d` when step 2 landed; they're `wgr_shape2d_set_rectangle`,
  `_circle`, `_line`, `_transform` and `_outline` since the split below.)
  Picked by exact area (rounded corners, outline rings, distance to the line).
- Step 2 put 2D and 3D shapes in one handle kind, routed at runtime by
  `wgr_scene_register_is_2d`. **That was undone right after step 3: shapes are now two
  types**, `wgr_shape2d` and `wgr_shape3d` (see "Shape types" below).
- `examples/ui.c` is built from 2D shapes: a rounded translucent panel (pickable, so
  presses on it don't orbit), rounded buttons, a divider line, an outlined progress
  bar with a filled part and a circle tip. Checked in the browser with CDP clicks
  (counting, disabling, hover).

#### Step 3: UI essentials

- `wgr_sprite2d_set_nine_slice(left, top, right, bottom)`: borders in source pixels
  that keep their size at any drawn size. An axis with no borders stays one span, so
  a sprite sliced on one axis draws three patches, not nine; borders wider than the
  source region share it, and borders that don't fit the destination shrink to fill
  it (a patch never flips). Picks hit the whole rectangle — the alpha test is skipped
  while a sprite is sliced, because u/v don't map linearly into the source any more.
- `wgr_text2d_set_align(horizontal, vertical)` with `wgr_text_align_t`
  (LEFT/CENTER/RIGHT, TOP/MIDDLE/BOTTOM; the axes have distinct values, so a value
  from the wrong axis is refused) and `wgr_text2d_set_max_width` (wraps between words;
  a word wider than the box keeps its own line; newlines always break). The position
  is the block's edge or center per its alignment, and wrapped lines line up the same
  way inside the block. Layout lives in the text layer
  (`wgri_text_block_size` / `wgri_text_block_draw`, internal), so text3d can use it later.
  **`wgr_text2d_measure_width`/`_height` now measure the laid-out block** — the widest
  line and whole lines of the font's line height, not one line's glyph extents — and
  picks use that rectangle.
- **One layout path for all text** (cleaned up after step 4): the immediate
  `wgr_text_draw_ex` / `wgr_text_measure_ex` go through the same block layout, so
  newlines break lines there too and the measured height is whole lines rather than
  one line's glyph extents; text3d gained `set_align` and `set_max_width` and shares
  the line splitting (`wgri_text_split_lines`) while still drawing its own glyphs in
  world space. **`wgr_text3d_get_size().y` now reports the font's line height** for a
  single line (about 1.2x the size) instead of the ink's height.
- Clipping: `wgr_scene_set_clip(scene, layer, x, y, width, height)` clips a layer's 2D
  members (at most 8 layers per scene; a 0 size removes it), and
  `wgr_render_begin_clip` / `wgr_render_end_clip` do the same for immediate drawing
  (since [PLAN-ui.md](PLAN-ui.md): a nesting stack, `wgr_render_push_clip` / `pop_clip`).
  Rectangles are logical pixels, top-left origin; the scissor rect is framebuffer
  pixels, so screen rectangles scale by the DPI scale and render targets don't.
  **A clipped-away member isn't picked either**, so a scrolled-out row can't be
  hovered or clicked and the pointer falls through to what's behind.
- `examples/ui.c`: the panel is a nine-slice sprite
  (`examples/assets/textures/ui_panel.png`, 383 bytes, generated by
  `tools/gen_ui_panel.py`), the button labels are centered text2d, the note under the
  bar is wrapped, and the list at the bottom is a clipped, wheel-scrolled layer with
  selectable rows. Checked in the browser with CDP events on both backends: hover,
  click to select, wheel scroll, and a row scrolled out of the box that no longer
  takes the pointer ("hovered: the panel").

#### Shape types: wgr_shape2d and wgr_shape3d

Asked during step 3 review: why one shape type for both layers, when every other
drawable splits (`sprite2d`/`sprite3d`, `text2d`/`text3d`)? It had no good answer —
one type meant setters that silently don't apply (`set_outline` is 2D-only,
`set_transform` vs `set_transform_2d`), an object whose category changed with the
setter you called, runtime routing, and no type check for bindings. So:

- `wgr_shape2d_*` — screen space: the immediate primitives (`wgr_shape2d_draw_rectangle`,
  `_rectangle_lines`, `_line`, `_circle`, `_circle_lines`, `_triangle`) and retained
  shapes (`set_rectangle`, `set_circle`, `set_line`, `set_transform`, `set_pivot`,
  `set_outline`), handle kind `WGR_HANDLE_KIND_SHAPE2D`.
- `wgr_shape3d_*` — the world: the immediate debug draws (the `_3d` suffixes are gone:
  `wgr_shape3d_draw_line`, `_cube`, `_cube_wires`, `_sphere`, `_grid`, `_rectangle`,
  `_circle`) and retained 3D shapes, handle kind `WGR_HANDLE_KIND_SHAPE3D`.
- The scene drops `wgr_scene_register_is_2d` and its registry: a kind draws and picks
  either in 2D or in 3D, so the handle says which.

**`wgr_shape2d_set_pivot(x, y)`** comes with the split: a normalized point on the
shape's bounds that the position refers to and that rotation and scale turn around.
Default: the shape's own origin — a rectangle's top-left, a circle's center — so
nothing moves until it's set. Lines have explicit endpoints and ignore it. This is the
same idea as `wgr_sprite2d_set_pivot`, and text2d's alignment is its 9-point form; the
three mechanisms now line up, with each noun's default documented where it belongs.

#### Step 4: sprite3d for 2D worlds

- `wgr_sprite3d_set_extent(width, height)` — the quad's world size; `set_size(s)` is now
  the square shorthand for `set_extent(s, s)`, and a width or height <= 0 is refused.
- `wgr_sprite3d_set_source(x, y, width, height)` — the region of the texture to show, in
  texture pixels, like sprite2d's; width or height <= 0 means the whole texture. The
  pick's alpha test samples through the same region.
- `wgr_sprite3d_set_pivot(x, y)` — the point of the quad that sits on the position and
  that it turns around; (0.5, 0.5) center by default, y running down the texture, so
  (0.5, 1) stands a sprite on the ground. Bounds grow by the pivot offset, so a moved
  quad still passes the broadphase.
- `wgr_render_begin_mode_2d()` lost its unused camera parameter.
- `examples/tilemap.c`: 24x16 ground tiles and props from one 64x48 sheet
  (`tools/gen_tiles.py`, 705 bytes) under an orthographic camera3d, with drag/arrow
  scrolling, wheel zoom (the camera's ortho height), and coins that hover and collect
  through the scene's interaction state with alpha-tested picks. Checked in the browser
  on both backends with CDP events: hover, click to collect (coins 0 -> 1), wheel zoom
  (12 -> 7.5 units) and a drag (center 12.0, 8.0 -> 13.2, 7.3).
- Two practical notes the example records: **no MSAA** and **ground tiles a hair over
  one unit**, because neighbouring sprites are blended separately and an edge landing
  exactly on a pixel boundary otherwise lets the background through as a hairline seam.

### Verification

Unit tests on the headless build: the interaction state machine (enter/leave,
press/hold/drag-off/release, click vs cancel, disabled, topmost 2D over 3D, capture,
non-interactive scenes), 2D shape picks (rounded corners, thick lines), nine-slice
geometry, text wrap and alignment, clip rectangles, sprite3d source and pivot.
Examples checked with webcheck on both backends; `make websize` before and after.

*From docs/PLAN-2d.md.*

## a sprite renderer, and particle emitters

Status: built (2026-09-18), steps 1-4, and more for particles after (step 5).

### Why

Games built on libwgrender will put **thousands** of sprites on screen, in 3D (sprite3d:
camera-facing, upright, flat and free) and in 2D (sprite2d, UI), plus particles in
both. Today every sprite is drawn through sokol_gl, one at a time: the CPU works out
its quad (billboard math included), writes 6 vertices, and sokol_gl turns runs of
them into draw calls. That was the right first version; the benchmark says it's now
the limit on phones.

### Baseline (tools/bench/spritebench.c, 2026-09-18)

CPU per frame in ms, out of the 16.7 a 60 fps frame has. Pixel 9 Pro XL (Chrome
engine, WebGL2 / WebGPU) and desktop GL (for scale):

| scene                          | 1,000       | 4,000       | 16,000        | desktop, 16,000 |
|--------------------------------|-------------|-------------|---------------|-----------------|
| field, one atlas               | 1.8 / 3.0   | 5.0 / 6.3   | 9.6 / 9.6     | 2.9             |
| field, 4 textures              | 2.1 / 3.2   | 4.4 / 6.9   | 13.8 / 15.9   | 7.1             |
| 3D particles                   | 0.9 / 1.3   | 3.7 / 4.4   | 11.2 / 11.8   | 3.9             |
| 2D particles                   | 1.1 / 1.6   | 2.1 / 2.8   | 7.4 / 7.9     | 2.1             |

"field": sprite3d on the ground under an orbiting perspective camera, a third each
camera-facing, upright (turning about Y) and flat, blended and sorted every frame.
"particles": each moved with one API call per frame, 1/120 replaced per frame.

Where the time goes:

- **The scene step dominates** (about 80% with an atlas): per sprite, a handle
  lookup, a depth for sorting, the sort, then billboard math and 6 vertices on the
  CPU, recorded into sokol_gl.
- **Separate textures cost draw calls**: sorted back to front, 4 textures interleave,
  so 16,000 sprites become about 12,000 draws, and submit doubles.
- **Particles pay per particle in API calls** before anything is drawn: 3-4 ms for
  16,000 on the phone just moving them.
- WebGPU is no cheaper than WebGL2 on the CPU here.

### Design

#### 1. An instanced sprite pipeline

One quad, drawn instanced. Each sprite is one small record in a per-frame instance
buffer, and the vertex shader builds the corners:

    position   vec3     where the pivot goes
    axis_x     vec3     the quad's right edge (world size included)   \  only for FREE and
    axis_y     vec3     the quad's up edge                           /  Y_UP; billboards
    extent     vec2     world width, height                            get theirs from the
    pivot      vec2                                                    camera, in the shader
    uv         vec4     source rectangle, normalized
    tint       ubyte4
    facing     float    camera, upright about Y, flat, free

About 64 bytes a sprite, instead of 6 vertices of 24 bytes plus sokol_gl's
bookkeeping. Billboarding moves to the GPU: the shader has the camera's right, up and
position, so camera-facing and upright sprites cost the CPU nothing to turn.

- A new shader (`src/shaders/wgr_sprite.glsl`, sokol-shdc, GL / WebGL2 / WebGPU like
  `wgr_model.glsl`) and pipelines per blend mode.
- One instance buffer per frame, appended to as sprites are drawn and grown between
  frames like sokol_gl's budgets. WebGL2 has no base-instance draws, so each batch
  binds the buffer at its offset (supported on every backend).
- **Order is kept**: a batch is a render command (`RENDER_CMD_SPRITES`, a range of
  instances, like `RENDER_CMD_MODELS`), so sprites still interleave correctly with
  sokol_gl layers (shapes, text), model draws and passes. Consecutive sprites with the
  same texture, blend mode and pass share one draw.
- sokol_gl stays for shapes, text and immediate texture drawing.
- Picking, bounds and interaction are unchanged: they already work from the sprite's
  data on the CPU.

#### 2. Alpha modes: what to sort

Blending needs back-to-front order, and order is what breaks batching (the 4-texture
field). Most sprites don't need it:

- **blend** (today, the default): sorted with the scene's other transparent parts.
- **cutout**: alpha-tested (discard below a threshold), depth written, **not sorted**.
  Hard-edged sprites (pixel art, foliage, most game sprites) look the same and batch
  by texture: 16,000 sprites from 4 textures become 4 draws.
- **additive**: glows, sparks, most particles. Order-independent, not sorted, no depth
  write.

#### 3. sprite2d on the same path

sprite2d draws in call / layer order, never sorted, so it batches runs of the same
texture. Rotation, scale, pivot, tint and the source rectangle map onto the same
record (with a screen-space projection); a nine-slice sprite is 9 instances; a
layer's clip rectangle breaks the batch (it's a scissor change). Blend and additive
apply; cutout means nothing in 2D.

#### 4. Particle emitters, 3D and 2D

Particles as sprites is what the particle scenes measure: a handle, an API call and a
sort entry per particle. An **emitter** is one object that owns many particles:

    wgr_handle_t wgr_emitter3d_create(wgr_handle_t texture);   /* object from a resource */
    wgr_handle_t wgr_emitter2d_create(wgr_handle_t texture);
    wgr_emitter3d_set_rate(e, particles_per_second);  wgr_emitter3d_burst(e, count);
    wgr_emitter3d_set_life(e, min, max);  _set_velocity(e, ...);  _set_gravity(e, ...);
    wgr_emitter3d_set_size(e, start, end);  _set_color(e, start, end);  _set_source(e, ...);
    wgr_emitter3d_set_position / _set_alpha_mode / ...;   wgr_emitter3d_destroy(e);

(names illustrative; the real set is part of step 4's design, taking what the
examples need.)

**Recommended: stateless, simulated on the GPU.** A particle is written once, when
it's born (time, position, velocity, a random seed); the vertex shader computes where
it is now (`p0 + v0 t + g t^2 / 2`), its size, color and fade from its age, and drops
it when it's dead. The CPU only spawns; there's no per-particle work per frame at
all. It covers fountains, sparks, smoke, dust, confetti and UI flourishes; it can't do
particles that react to the world after they're born (collisions, attractors), which
would be a later, CPU-simulated mode if a game needs it.

An emitter is a scene member like any object: one sort entry (its position) in 3D,
drawn in layer order in 2D, particles within it unsorted (additive by default; blend
without per-particle sort is the usual trade-off).

### Expected gains

Estimates, to be checked by the benchmark at each step:

- field, one atlas: the scene step loses the CPU billboard and vertex writes and
  sokol_gl recording; left are the lookup, the depth and the sort. Roughly 2-3x
  cheaper; on the phone 4,000 sprites from about 5 ms to about 2.
- field, 4 textures, cutout: about 12,000 draws become 4.
- particles: per-particle CPU cost goes to zero; what's left is spawning.

### Order

1. **The instanced pipeline, with sprite3d on it** (blend only, same look). Gate: the
   field benchmark at least 2x cheaper on the phone, pixel-identical enough in
   webcheck screenshots, `make verify`, webcheck on both backends.
2. **Alpha modes** for sprite3d (cutout, additive). Gate: the 4-texture field in
   cutout draws once per texture.
3. **sprite2d on it**, nine-slice and clipping included.
4. **Emitters**, 3D and 2D, GPU-simulated; an `examples/particles.c`; the benchmark's
   particle scenes rebuilt on them.

Each step is its own commit, measured on desktop, headless Chrome and the phone.

### Decisions

1. **Instanced quads with billboarding in the shader** rather than a faster CPU path
   into sokol_gl. Recommend: yes; the CPU vertex work is the cost.
2. **Alpha modes** as a new sprite3d/sprite2d setting (`blend`, `cutout`, `additive`;
   blend stays the default). This adds public API. Recommend: yes.
3. **Stateless GPU particles** for emitters, with a CPU-simulated mode only if a game
   needs particles that react after birth. Recommend: yes.
4. **Emitter as its own object noun** (`wgr_emitter3d_*`, `wgr_emitter2d_*`, created
   from a texture handle, like sprites), not a sprite flag. Recommend: yes.
5. **Particles within an emitter aren't sorted** (additive by default). Recommend: yes;
   revisit if a blended effect looks wrong.

### As built

#### Step 1: the instanced pipeline, with sprite3d on it (2026-09-18)

- `src/shaders/wgr_sprite.glsl` (`@module sprite`) and `src/wgr_sprite_batch.c`: one
  76-byte record per sprite (position, facing, size, pivot, source rectangle, axes for
  flat and free sprites, tint), a 6-corner quad drawn instanced. Billboard axes are
  per camera, so they're uniforms, worked out by `wgri_sprite3d_facing_basis` like
  picking's; flat and free sprites carry their own. Pipelines match sokol_gl's 3D ones
  (blended, depth-tested, depth writes on for direct draws, off in a scene's sorted
  pass). The frame's records go up in one transient buffer write before the passes.
- Batches are `RENDER_CMD_SPRITES` render commands: order with sokol_gl layers, model
  draws, passes and clips is unchanged. A batch records its camera, pass and scissor;
  consecutive batches only re-apply what differs.
- Looks the same: webcheck screenshots of `tilemap` (then `2d`), `pick` and `ui` are pixel-identical to
  the sokol_gl path (the rest differ only where they animate), on WebGL2 and WebGPU.

Measuring showed the per-sprite CPU work around the draw mattered as much as the
draw itself, so this step also fixed:

- **Scene membership**: a hash index per scene (handle -> member), with removals left
  as holes that are closed, layer-sorted and re-indexed once before the members are
  walked. Adding, removing, destroying (`wgri_scene_forget`) and relayering were linear
  scans, quadratic under churn.
- **The transparent sort**: a stable radix sort on depth (ties keep submission order)
  above 64 parts, instead of `qsort`.
- **Per-sprite state lookups**: the batch's camera, pass and scissor are cached behind
  `wgri_render_state_revision` and `wgri_camera3d_revision` instead of being re-read and
  compared for every sprite.

CPU ms per frame, before -> after:

| 16,000 sprites          | desktop GL   | Chrome, WebGL2 | Pixel, WebGL2 | Pixel, WebGPU |
|-------------------------|--------------|----------------|---------------|---------------|
| field, one atlas        | 2.89 -> 0.92 | 5.40 -> 1.28   | 9.61 -> 3.58  | 9.61 -> 5.16  |
| particles 3d            | 3.91 -> 0.98 | 6.12 -> 1.44   | 11.15 -> 4.47 | 11.82 -> 5.34 |

**Correction** (found in step 2): the "field, 4 textures" numbers first reported for
this step (desktop 7.07 -> 1.35 and similar) were wrong. They were measured while the
render command list still stopped at 1,024, so most of that scene's ~12,000 batches
were dropped, not drawn, and the benchmark didn't notice (dropped sprites aren't a
sokol_gl error). Drawn in full, blended sprites from 4 interleaved textures cost about
what they did on sokol_gl on desktop GL (7.1 ms), and more on WebGL2 (see step 2).

- **The render command list grows** (up to 1M a frame) instead of stopping at 1,024:
  sprites are commands now, so a frame alternating sprites and sokol_gl shapes adds
  two per switch, where they used to share one sokol_gl stream.

Replaying many sokol_gl layers was quadratic (each `sgl_draw_layer` scanned the
frame's commands for its layer), which predated this step (models split layers too).
libwgrender's sokol fork now has `sgl_draw_layer_range`, and each layer draws its own
command range: 3,000 sprite/shape switches in one frame replay in 0.6 ms, not 5.4.

On the phone at 4,000 (the "thousands" games will have): the field from 5.0 to 1.9 ms
(WebGL2) and 6.3 to 2.7 (WebGPU). 1,000 sprites from 4 textures stay where they were
(about 2 ms): sorted back to front they make ~780 small batches, and WebGL2 has no
base-instance draws, so each rebinds the instance buffer. Step 2's cutout mode
removes the interleaving.

#### Step 2: alpha modes (2026-09-18)

- `wgr_alpha_mode_t` in `wgr_types.h` (`WGR_ALPHA_OPAQUE`, `_MASK`, `_BLEND`, `_ADD`), one
  vocabulary for sprites and materials (it replaces `wgr_material_alpha_t`; materials
  refuse `ADD` for now). `wgr_sprite3d_set_alpha_mode(sprite, mode, cutoff)`; blend
  stays the default.
- In a scene, opaque and masked sprites draw in the opaque pass and additive ones in a
  new additive pass after the blended parts (`wgri_scene_register_additive`). Neither
  is sorted: the batcher groups them by texture and mode (a counting sort over the
  few groups, stable, so overlapping sprites at one depth keep member order), so 400
  masked sprites alternating 4 textures draw in 4 batches (unit test).
- The shader gets the mode per sprite (the up axis's `w`): a mask cutoff discards,
  opaque and masked write alpha 1. Pipelines: opaque (no blending, depth written),
  blended with or without depth writes, added.
- Batches draw from a base instance where the backend can (GL 4.2+, WebGPU, Metal,
  D3D11): the instance buffer stays bound and consecutive batches only change the
  texture. WebGL2 can't, so it rebinds the instances per batch.
- `examples/tilemap.c` uses it: opaque ground tiles, masked props. Its pixel art has no soft
  edges, so it looks the same (pixel-identical screenshots) and needs no sorting.

CPU ms per frame, 16,000 sprites, 4 textures interleaved (field):

| mode                 | desktop GL          | Chrome, WebGL2      | Pixel, WebGL2 | Pixel, WebGPU |
|----------------------|---------------------|---------------------|---------------|---------------|
| blended, sokol_gl    | 7.1                 | 9.2                 | 13.8          | 15.9          |
| blended, instanced   | 5.7                 | 12.2                | 20.5          | 10.0          |
| masked, instanced    | **1.4** (4 batches) | **1.2** (4 batches) | **3.3**       | **4.6**       |

On the phone at 4,000 sprites, blended from 4 textures is a little cheaper than on
sokol_gl (WebGL2 3.8 vs 4.4 ms); it's at 16,000 that WebGL2's rebinding dominates.
The phone's WebGPU masked number (9.8) was noise. Traced and re-measured, the masked
16,000 scene takes 3.1 ms of scene time on WebGPU (4.6 ms in all), like WebGL2's.
Sampling the phone's clocks during a run showed no thermal throttling but the
governor moving the big cores between 0.7 and 3.1 GHz from second to second, so a
2-second benchmark step can land on a slow stretch: single phone runs vary 2-3x, and
a surprising phone number needs a second run before it means anything.

Additive particles cost the same as blended ones here (their one texture already made
few batches); the gain is that they need no sorting.

**Blended sprites from several textures on WebGL2** (fixed after step 2). Sorted back
to front they make thousands of tiny batches, and WebGL2 has no base-instance draws,
so each batch rebound six instance attributes, where sokol_gl only switched the
texture: slower than before. Now, without base-instance draws (WebGL2, GL before
4.2), the sprites go into an RGBA32F texture, 6 texels a sprite and 256 a row, and a
second vertex shader (`quad_pulled`) reads them by index (first + instance); a batch
sets one small uniform. The frame writes only the rows in use. `-DWGR_SPRITES_PULLED`
forces this path on any backend (the unit tests pass on both).

| 16,000, 4 textures, blended | sokol_gl | rebinding | read by index |
|-----------------------------|----------|-----------|---------------|
| Chrome, WebGL2              | 9.2      | 12.2      | **7.1**       |
| Pixel, WebGL2 (two runs)    | 13.8     | 20.5      | **11.0, 11.2**|

The cost: packing and uploading the texels, about 1 ms more at 16,000 on the phone
when batches were few anyway (one atlas: 4.6 -> 5.5-6.3 ms). Desktop GL keeps
base-instance draws (reading by index is no faster there: native GL calls are cheap).

#### Step 3: sprite2d on the instanced path (2026-09-18)

- A 2D sprite's quads (one, or up to nine when nine-sliced) go to the batcher as
  instances: position the top-left corner, axes the top and left edges, so rotation,
  pivot, scale, flips and nine-slices are worked out as before and come out the same.
  `wgri_sprite_batch_add_2d` records them in order (2D is never regrouped) under a 2D
  projection matching sokol_gl's (`wgri_mat4_ortho` over the target's logical pixels),
  with 2D pipelines (no depth test): blended, added, opaque/masked.
- `wgr_sprite2d_set_alpha_mode` / `get_alpha_mode`, like sprite3d's; blend the default.
- The immediate `wgr_texture_draw*` calls stay on sokol_gl (UI draws few, interleaved
  with shapes and text).
- Screenshots of `sprite2d`, `touch`, `ui` and `clay` match the sokol_gl build (the
  differences are the page's dropdown and animation). Unit test: a run of one texture
  (a nine-slice included) is one batch; alternating textures stay in order.

2D particles, 16,000, CPU ms: desktop GL 1.43 -> 1.07, Chrome 2.39 -> 1.63, Pixel
WebGL2 5.55 -> 3.89.

#### Step 4: particle emitters (2026-09-18)

- `wgr_emitter3d_*` and `wgr_emitter2d_*` (include/wgr_emitter3d.h, wgr_emitter2d.h):
  objects created from a texture. Emission: `set_rate`, `burst`, `set_emitting`,
  `set_max` (default 1024, at most 65,536; new particles replace the oldest),
  `set_life(min, max)`. Birth: `set_spawn_box`, `set_velocity(dir, spread,
  speed_variance)` (a cone in 3D, ± an angle in 2D), `set_gravity`. Over a life:
  `set_size(start, end, variance)`, `set_color(start, end)` (alpha included: fades),
  `set_spin(min, max)`. Plus `set_source` (an atlas cell), `set_alpha_mode` (additive
  by default), `set_seed`, `get_count`, `clear`, `set_visible`, `draw`. Configured in
  code; editor formats are for later (see below).
- Stateless on the GPU: a particle is a 48-byte record written once, at birth (where
  and when, velocity and life, size scale, spin and starting angle). `vs_particle`
  (src/shaders/wgr_sprite.glsl) works out its position (`p0 + v t + g t² / 2`), size,
  color and angle from its age, and moves dead or unborn ones off screen. The CPU only
  spawns (spread evenly through the frame, so a steady rate doesn't clump) and drops
  the dead from the front of each emitter's ring.
- libwgrender advances every emitter once a frame, after the ticks and before the frame
  callback. The emitter's clock is rebased every 4,096 s to keep the shader's float time
  precise.
- Drawing: one instanced draw per emitter. Its live particles are copied into the
  frame's particle buffer (uploaded once, before the passes) and drawn with the
  emitter's uniforms: the camera's right and up in 3D, the 2D projection in 2D. Emitters
  go through render commands like sprite batches, so they keep their place among sokol_gl
  drawing and their scissor.
- In scenes: a 3D emitter is one member. Opaque and masked emitters draw in the
  opaque pass; blended ones are sorted as a whole with the other transparent members;
  additive ones draw with the additive sprites. A 2D emitter draws in layer and
  member order and isn't pickable. Particles within an emitter aren't sorted.
- `examples/particles.c`: a fountain (blended), sparks from a moving source
  (additive, trailing), smoke (blended, growing, turning) and 2D confetti bursts where
  you click or tap (squares cut from the particle texture with `set_source`, so their
  spin shows). Unit tests (tests/unit/emitter_test.c) with fixed seeds: rates, bursts,
  the ring's max, retiring the dead, the clock rebase, birth records, and scene
  membership.

spritebench's particle scenes, the same particles from one emitter (the sprite
versions stay as the baseline), 16,000 alive, CPU ms per frame:

| scene             | desktop GL   | Chrome (WebGL2) | Pixel (WebGL2) |
|-------------------|--------------|-----------------|----------------|
| 3D, blended       | 1.13 -> 0.12 | 2.19 -> 0.30    | 6.20 -> 0.63   |
| 3D, additive      | 1.00 -> 0.11 | 1.93 -> 0.27    | 5.61 -> 0.59   |
| 2D                | 1.08 -> 0.10 | 1.83 -> 0.27    | 3.98 -> 0.76   |

Desktop frame time (vsync off) 1.25 -> 0.26 ms for 3D. What's left is copying and
uploading each emitter's live particles every frame (48 bytes each); spawning happens
in the runtime before the frame callback, outside these CPU columns but inside the
frame time. The example runs at 60 FPS on the phone. If uploads show up, an emitter's
ring could live in a GPU buffer and upload only its new particles.

#### Step 5: more for particles (2026-09-18)

Still stateless: everything is decided at birth or worked out from a particle's age
in `vs_particle`; the record stays 48 bytes (its spare float now a random 0..1).

- Motion:
  - `set_drag`: slowing in proportion to speed, with gravity, in closed form: towards
    gravity / drag.
  - `set_stretch(seconds)`: the quad's top follows the velocity across the screen, as
    long as the distance moved in that time, trailing behind.
  - `set_inherit_velocity(fraction)`: a share of the emitter's own movement at birth,
    measured over the frame the game moved it in (the previous update's time).
  - A moving emitter's steady spawns are spread along the way it moved, so a fast
    source leaves a smooth trail. The first position isn't a move; `jump` moves without
    a trail.
- Over life:
  - Curves: up to 8 size keys and 8 color keys (`add_size_key`, `add_color_key`, and
    `clear_*`), linear between keys, held outside them, a step where two share a time.
    `set_size` and `set_color` make the two-key curve they always did.
  - Palette: up to 8 colors; each particle picks one at birth, tinting its color.
- Look and start:
  - `set_frames(columns, rows, count, per_second)`: a flipbook within the source,
    played once over the life (0) or looped from a random frame.
  - `prewarm(seconds)`: start over as if the rate had been running that long.
  - `set_spawn_sphere` (2D: `set_spawn_circle`): evenly within a radius, instead of the
    box.
- `examples/particles.c`: sparks with drag, stretch and inherited velocity; a campfire
  (flipbook flames colored by a curve, under smoke with size and color curves); the
  steady emitters prewarmed; confetti from one emitter with a palette.
  `tools/gen_particles.py` makes the particle textures (the dot, and the flame's 4x4
  flipbook).
- Cost: the same CPU as step 4 on the phone (16,000 particles: 0.61-0.79 ms, within
  noise). The example (about 3,000 particles, plus 300 confetti) holds 60 FPS there.

### Not in this plan

- Lit sprites / sprites on materials (TASKS: materials phase 3).
- Texture arrays or bindless textures to batch blended sprites across textures;
  atlases do that today.
- Moving shapes and text off sokol_gl.
- Particles: particles that react after birth (collisions, attractors: a CPU-simulated
  mode), sorting within an emitter, and effects saved to files as resources (a format
  of our own, later ones made in editors, e.g. Cocos/particle-designer plists,
  Effekseer).

*From docs/PLAN-sprites.md.*

## a web asset cache that notices changed files

Status: **landed (2026-09-25).** All seven steps: the metadata store, the cache mode,
revalidation on the web, the manifest on both platforms, the tooling
(`tools/gen_manifest.py`, `site.py` writing manifests, the examples setting one,
`tools/cachecheck.py [--manifest]`), the bindings, and the docs sweep. As built,
beyond the design below: a cross-origin host is revalidated with `cache: "no-cache"`
rather than conditional headers, which would need a CORS preflight; a 5xx counts as no
answer, so the copy is used; `immutable` without `max-age` is fresh for a year, and
`Expires` is not read; the root manifest is asked about once a run whatever the mode;
a missing root is an ordinary setup (serve.py has none) and logs at info; the manifest
reader is written for its one shape (`src/wgr_manifest.c`) instead of vendoring jsmn.
`wgr_asset.h` is the contract. Bindings: wgrender-hx and wgrender-nim wrap the cache
mode and the manifest and publish manifests with their sites; wgrender-beef takes the
new library only (its binding covers what `simple` needs, and it publishes no site).
Not wired in: `tools/cachecheck.py` runs by hand, not from `tools/verify.py` or CI.
The one-time fix for the bug that prompted this (a bumped `WGR_FS_CACHE_EPOCH`) landed
separately, and the epoch is now the last resort.

### The bug

On the web, `wgr_fs` keeps every fetched asset in IndexedDB, keyed by its path, and
serves it on every later visit without asking anyone whether the file on the server has
changed. The only invalidation is `WGR_FS_CACHE_EPOCH` in `src/wgr_fs.c`, a number a
person has to remember to raise, which throws the whole cache away for everyone.

On 2026-09-25 the tile sheet (`textures/tiles.png`) changed shape at the same path, and
the published `tilemap` example showed the new cell coordinates cut from the old sheet
for anyone who had visited before. The same happened silently to
`models/woman_casual/woman_casual.glb`, which changed three times in a day. A game
built on wgrender would hit this on its first patch.

### What the design has to give

1. **A default that cannot show a stale file**, with nothing to configure or build.
2. **No wasted downloads:** a game that changes one texture should re-fetch one texture.
3. **Offline still works:** a cached copy is used when the network is down.
4. **A pipeline-agnostic opt-in** for the cheap path: whatever carries versions must be
   a plain file any build tool can write, not something only `tools/site.py` produces.
5. **Scales to a large game:** no single file that lists every asset of an MMO.
6. **Control from the game:** a way to turn persistence off (development) and to clear
   the cache (a settings button) -- the second exists: `wgr_asset_clear_cache`.

### What others do (checked in their source, 2026-09-25)

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

### Design

Two layers. The first is the default and needs nothing; the second is opt-in and
removes the round trips.

#### 1. Revalidate by default (HTTP semantics)

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

#### 2. A manifest tree, when the build can write one

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

#### Public API (`include/wgr_asset.h`)

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

### Where it goes

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

### Steps (each one a commit, each verified before the next)

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

### Verification

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

### Decisions

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

### Out of scope

- Desktop revalidation through the fetcher (needs `wgr_asset_fetch_done` to carry
  headers): a later phase.
- Quota handling beyond what `wgr_fs` does today.
- Signing or verifying manifests against tampering; the hash guards against staleness
  and broken deploys, not attackers.

*From docs/PLAN-asset-cache.md.*

## Audio — streamed music and mixing off the main thread

Status: **implemented (2026-09-16).** Decisions 1–3 accepted as recommended; see
"As built".

Restores two things librl had (through raylib's audio) that libwgrender lost. The
general "decode resources in the background" work is a separate roadmap item
(docs/ROADMAP.md, loading pipeline).

### Where we are

| | librl (raylib / miniaudio) | libwgrender today |
|---|---|---|
| Music | streamed: decoded while playing | decoded fully at `wgr_audio_create` |
| Mixing | on the audio device's thread | on the main thread (`wgr_audio_tick` pushes each frame and while pacing) |

Measured: `wgr_audio_create` on the 6 MB example MP3 takes **215 ms** and holds
**~108 MB** of float PCM (13.5 million stereo frames). And because mixing is on the main thread, any slow frame
(for any reason) leaves the audio device without samples: an audible gap.

### Proposed design

#### 1. Stream long audio

- An Audio whose file is larger than **1 MB** (roughly a minute of MP3) is
  streamed: it keeps the encoded file in memory and decodes while playing. Smaller
  files decode fully at create, as now, so sound effects start instantly.
- Each playing Sound of a streamed Audio has its own decoder (dr_mp3, dr_wav and
  stb_vorbis all decode incrementally from memory), so one Audio can play on
  several Sounds at once.
- `wgr_audio_create(path)` and the Sound API don't change. Play (rewind), pause,
  resume, stop, loop, volume and pitch behave the same for both kinds.
- Expected: create becomes a file read (a few ms); memory drops ~10x; decoding
  costs a little CPU during playback, on the mixing thread.

#### 2. Mix on the audio device's thread

- Switch sokol_audio to callback mode: the device asks for samples and the mixer
  fills them, on the audio thread on desktop. A slow frame no longer causes a gap.
- The game thread and the mixer share Sound state (playing, position, volume,
  pitch, loop, audio) and Audio resources. A mutex protects them: the mixer holds
  it while filling a buffer (short, a few ms at most); API calls hold it while
  changing a Sound or releasing an Audio, so PCM is never freed while being mixed.
- Web: sokol_audio also runs the callback there (from the browser's audio
  callback on the main thread), so the same code works; the mutex is a no-op
  without threads.
- Frame pacing no longer needs to feed audio while sleeping (`wgr.c` pacing loop
  and the per-frame `wgr_audio_tick` go away).
- Headless builds keep no audio device; a test hook pulls mixed samples directly.

### Decisions

1. **Stream automatically above 1 MB**, versus an explicit choice (a flag, or a
   separate noun). Recommend: automatic. It matches the file to the behavior
   without the game deciding; revisit if a game needs to force either mode.
2. **Mix on the device thread with a mutex**, versus a lock-free command queue.
   Recommend: mutex. Critical sections are tiny and rare on the game side; a queue
   adds complexity that measurements don't justify yet.
3. **Callback mode on web too** (one code path), versus keeping the push model
   there. Recommend: callback mode everywhere.

### As built

- `wgr_audio_create` on the example music: **215 ms → 5 ms**; memory **~108 MB → 6 MB**
  (the MP3 file). Streamed Audio knows its length at create (dr_mp3 counts frames
  from headers in ~1 ms), so looping works exactly like decoded Audio.
- Streamed and decoded Audio produce identical samples (unit test, block by block,
  WAV/OGG/MP3 with loop, pitch and resampling), including rewinds.
- One recursive mutex (`wgri_audio_lock`) guards Sounds and Audio; decoding a file at
  create happens outside it. Seeking a streamed MP3 far forward takes up to ~80 ms
  on the mixer thread, which only happens when a sound's position jumps (rewinding
  to the start is cheap).
- `make test SANITIZE=thread` (also `address`, `undefined`) builds the library into
  the tests with a sanitizer; ThreadSanitizer runs with ASLR off (`setarch -R`),
  which newer kernels need. A concurrency test mixes on a second thread while the
  game thread plays, stops, retargets, creates and destroys; TSan is clean, and was
  checked to flag an unlocked setter.
- Frame pacing sleeps in one go instead of slices (nothing to feed).
- `examples/audio.c`: press S for a 300 ms stall; music should keep playing.

### Verification

- Unit tests (headless, pulling samples from the mixer):
  - streamed decode matches full decode sample for sample for MP3, OGG and WAV,
    including across buffer boundaries and at loop points
  - play, pause, resume, stop, loop and pitch give the same output for streamed
    and fully decoded Audio
  - two Sounds playing one streamed Audio at different positions
  - releasing an Audio while its Sound is playing is safe
- Timing and memory before/after for `wgr_audio_create` on the example music.
- `examples/audio.c`: music keeps playing through a deliberately slow frame
  (e.g. a key that sleeps 300 ms), checked by ear on desktop and web.
- A `SANITIZE=thread` (TSan) build of the audio tests; `make test`, `make smoke`,
  `make check`, `make webcheck`; CI.

*From docs/PLAN-audio.md.*

## colors are values, not handles

Status: **done** (2026-09-17). Implemented as proposed, with the `color_t` ->
`wgri_colorf_t` move folded in; see "As built".

### What exists

- `wgr_color_create(r, g, b, a)` allocates a slot in a 256-entry pool and returns a
  handle; `wgr_color_destroy` frees the slot. 27 built-ins are `extern const
  wgr_handle_t` (raylib's palette), handle kind `WGR_HANDLE_KIND_COLOR = 1`.
- **Colors are not reference counted and not deduped by value.** librl says why, in
  `src/rl_color.c`: "colors are tiny value objects and do not use refcounted
  shared-asset semantics". libwgrender inherited the pool unchanged.
- Handle `0` means white (`wgr_color_get(0)`), an unresolvable handle draws magenta,
  and `WGR_COLOR_DEFAULT` is magenta and unused outside `wgr_color.c`.
- 29 public parameters across 13 headers take a color; 12 `src/*.c` files unpack one
  with the internal `wgr_color_get`.
- Colors are immutable (no public `wgri_color_set`), so animating a tint means
  pre-creating a palette — `examples/sprite2d.c` does exactly that — and with 256
  slots and no dedupe, a color created per frame exhausts the pool in about four
  seconds at 60 fps.

### Goal

A color is a value: nothing to create, destroy, run out of, or resolve. The
handle-only rule in AGENTS.md exists to keep pointers and structs out of the public
API; integers were always allowed, and a color is an integer.

### Design

```c
typedef uint32_t wgr_color_t;          /* 0xRRGGBBAA */

#define WGR_COLOR_WHITE  0xFFFFFFFFu   /* the 27 built-ins keep their names and values */
#define WGR_COLOR_RED    0xE62937FFu
/* ... */

wgr_color_t wgr_color_rgba(int r, int g, int b, int a);          /* 0..255 */
wgr_color_t wgr_color_with_alpha(wgr_color_t color, int a);       /* fades */
wgr_color_t wgr_color_lerp(wgr_color_t a, wgr_color_t b, float t); /* animation */
```

- Every public `wgr_handle_t color` / `tint` parameter becomes `wgr_color_t`.
- The internal `wgr_color_get(handle)` becomes `wgri_color_unpack(wgr_color_t)`, still
  returning the float struct the renderer already uses. Nothing else in the drawing
  path changes.
- `wgr_color_create`, `wgr_color_destroy`, the pool and `WGR_HANDLE_KIND_COLOR` go away
  (the kind is retired with a comment, like kind 10 for music).
- The helpers are real exported functions, not macros, so bindings and the wasm
  build get them for free.

#### Two color types, and which is which

There are two today, and they stay two — the renderer works in floats (`sgl_c4f`
takes them, and the sRGB -> linear conversion needs them), so the float struct does
**not** become a `uint32`:

| | today | proposed |
|---|---|---|
| in the public API | `wgr_handle_t` (a pool handle) | `wgr_color_t` = `uint32_t`, packed `0xRRGGBBAA` |
| inside the renderer | `color_t` — `{float r, g, b, a}` — via `wgr_color_get(handle)` | `wgri_colorf_t`, same struct, via `wgri_color_unpack(wgr_color_t)` |

`color_t` is declared in the **public** `include/wgr_types.h` today, but no public
function uses it (only 12 `src/*.c` files do, through `wgr_color_get`), and it is the
one type in the repo with no `wgr_` prefix although it crosses `.c` files, which
AGENTS.md requires. So it moves to `src/internal/wgr_color.h` next to
`wgri_color_unpack` and becomes `wgri_colorf_t` — one struct fewer on the surface every
binding generator has to read, and `wgr_color_t` (packed, public) vs `wgri_colorf_t`
(float, internal) can't be misread for each other the way `wgr_color_t` vs `color_t`
could.

#### The one behavior change: `0` stops meaning white

Today every color parameter treats handle `0` as white. Packed, `0x00000000` is
transparent black — which is `WGR_COLOR_BLANK`, a real value in the palette — so `0`
cannot keep meaning white without stealing it.

**Colors become explicit: pass `WGR_COLOR_WHITE`.** White is the identity for a tint,
so the internal `tint != 0 ? tint : white` branches collapse instead of growing, and
"no tint" and "white tint" stop being two spellings of one thing. `examples/ui.c` and
`examples/tilemap.c` pass `0` for "no tint" through ternaries; they become
`WGR_COLOR_WHITE`. This follows "correct over compatible" in AGENTS.md: no implicit
fallback kept alive just to avoid touching callers.

#### Migration risk

`wgr_color_t` and `wgr_handle_t` are both 32-bit unsigned, so a stale color *handle*
passed to a color parameter still compiles and silently reads as packed RGBA. Inside
the repo nothing survives the change — `wgr_color_create` is gone, so every creation
site is a compile error — and nothing depends on libwgrender yet. Worth stating in the
commit message.

### Cost

29 signatures in 13 public headers, 12 `src/*.c` files, roughly 180 call sites in
examples and tests, plus docs and `tools/parity.map`. Comparable to the shape split
(31 files), mostly mechanical, one pass with the full verify rig behind it. It
removes a pool, a handle kind, two public functions, the 256-color ceiling and the
"which colors do I own?" question.

### Decisions

1. **Packing `0xRRGGBBAA`** — hex literals read the way they look (`0xFF0000FF` is
   opaque red), and alpha last matches how the built-ins are written. Recommend: yes.
2. **Explicit colors, no `0` sentinel;** `WGR_COLOR_WHITE` is the identity tint.
   Recommend: yes.
3. **Helpers:** `wgr_color_rgba`, `wgr_color_with_alpha`, `wgr_color_lerp`. The last two
   are the cases that motivated this (fading and animating a tint without a palette).
   Recommend: yes, all three.
4. **Drop `WGR_COLOR_DEFAULT`** (magenta, unused); `WGR_COLOR_MAGENTA` already exists.
   Recommend: yes.
5. **Keep the built-in palette names and values** (raylib's): familiar, and free as
   `#define`s. Recommend: yes.

### As built

- `wgr_color_t` (packed `0xRRGGBBAA`) in `wgr_types.h`; the 26 built-ins are `#define`s
  with their raylib values in `wgr_color.h`; `wgr_color_rgba`, `wgr_color_with_alpha` and
  `wgr_color_lerp` are exported functions. `WGR_COLOR_DEFAULT` (magenta, unused) is gone.
- The float struct moved to `src/internal/wgr_color.h` as `wgri_colorf_t`, with
  `wgri_color_unpack`. `src/wgr_color.c` went from a 190-line pool to 50 lines of
  arithmetic; `wgr_color_init` / `wgr_color_deinit` and their calls in `wgr.c` are gone,
  and handle kind 1 is retired in `wgr_handle.h`.
- **Defaults had to move with the meaning of 0.** Every object that defaulted its
  tint or color to handle 0 (shape2d, shape3d, sprite2d, sprite3d, model, text2d,
  text3d, light, scene ambient) now defaults to `WGR_COLOR_WHITE`, because 0 is
  transparent black. The compiler cannot catch this — `wgr_color_t` and `wgr_handle_t`
  are both 32-bit unsigned — so it was done by reading every initializer, and the
  examples (which draw every kind) are the check.
- Building and reading components: `wgr_color_rgba` (0..255) and `wgr_color_rgbaf`
  (0..1, rounded to the nearest step), both clamping — out-of-range components
  saturate and never wrap into the neighbouring channel — plus `wgr_color_get_red`,
  `_green`, `_blue`, `_alpha`.
- **No constant-expression macro.** It was considered so a palette could be declared
  at file scope (`wgr_color_rgba(...)` is a function call, so it cannot initialize
  anything with static storage duration). Dropped, because a macro can't clamp
  without evaluating its arguments more than once, and clamping everywhere matters
  more than that convenience. The built-ins carry their components as comments
  instead, and the test below checks the literal against the function, so the two
  stay independent rather than one deriving from the other.
- `tests/unit/color_test.c` covers all 26 built-ins against their documented
  components, packing and clamping for both constructors, `with_alpha`, `lerp`
  endpoints and clamping, the component getters, unpacking, and round trips.
- **What the `0` change actually broke, and how it was caught.** Seven examples called
  `wgr_scene_set_ambient(scene, 0, ...)` — handle 0 meaning white — which silently
  became transparent black, so their 3D models lost all ambient light. The compiler
  can't see it (`wgr_color_t` and `wgr_handle_t` are the same width), the unit tests
  don't render, and `make smoke` only checks that frames run without errors. It showed
  up in the webcheck screenshot of `ui`, as a gumshoe several shades too dark.
  Afterwards every example's screenshot was compared with its pre-change version by
  mean brightness: all 22 matched within 0.2 (backend noise), which is the check that
  actually covers this class of change.
- Sizes barely move, as expected for removing a small pool: `hello` 323.3 -> 322.9 KB
  gzip, 265.4 -> 264.6 KB brotli.

### Verification

`make verify`, ASan, `make parity` (librl's `rl_color_create` / `rl_color_destroy`
need map entries: created values, no lifecycle), webcheck on both backends, and
`make websize` before and after — the pool and its handle plumbing should come out of
the wasm. Unit tests: packing round-trips through `wgr_color_rgba` / `wgri_color_unpack`,
`with_alpha` and `lerp` endpoints, and a drawing path that takes a literal color.

*From docs/PLAN-color.md.*

## compressed textures

Status: built (2026-09-19), for textures and for glTF models' textures.

### Why

A texture loaded from a PNG costs, every time it loads: decoding the PNG, building
its mipmaps on the CPU, and 4 bytes a pixel of GPU memory (a third more with
mipmaps). Phones feel all three: less memory, slower CPUs. GPUs sample compressed
formats directly at 1 byte a pixel, with the mipmaps made ahead of time.

### The choice: files per GPU family, not one file transcoded

GPUs don't share a compressed format: desktops have BC7, phones ASTC and ETC2.
Basis Universal stores one file and transcodes it on the device, but its transcoder
(C++) weighed, compiled for the web with only the formats libwgrender needs:

| transcoder (web)                     | wasm   | gzipped |
|--------------------------------------|--------|---------|
| current release (with HDR)           | 883 KB | 393 KB  |
| 1.16.4 (before HDR), our formats     | 404 KB | 197 KB  |
| 1.16.4, only BC7 + ASTC + RGBA       | 268 KB | 126 KB  |

For comparison, all of `hello` is 134 KB gzipped. So libwgrender ships files already in
each family's format instead and loads the one the GPU can use: no transcoder, a few
KB of C to read the files, and no work at load but reading and uploading.

### How it's built

- **Files:** `tools/compress_textures.sh name.png` writes `name.bc7.ktx` (BC7),
  `name.astc.ktx` (ASTC 4x4) and `name.etc2.ktx` (ETC2 RGBA) beside it: KTX 1 files,
  16 bytes a 4x4 block, with the full mipmap chain. It encodes with Basis Universal
  (UASTC level 2, then transcoded to each format), built the first time from a pinned
  release (1.16.4) into the per-user cache (`tools/hostcache.py`): a tool for making assets, never linked in.
  `--linear` for data textures (normal maps, roughness).
- **Names:** a program loads `name.ktx`, through `wgr_asset` or `wgr_texture_create`.
  The texture module maps it to the first variant the GPU can sample, in order BC7,
  ASTC, ETC2, else `name.png`: the asset layer maps it before fetching
  (`wgri_asset_register_path_mapper`), so the web downloads and caches only that file
  and the callback gets its path, and `wgr_texture_create` maps it the same way. A
  variant named outright (`name.astc.ktx`) is used as is.
- **Missing files:** when this GPU's variant is missing (compressed for some formats,
  or not at all), `name.png` loads instead, with a warning naming the missing file:
  the asset layer retries the PNG once the variant's fetch fails (a 404 on the web; a
  task's fallback path, from the path mapper), and `wgr_texture_create` checks for the
  variant before loading. A variant named outright has no fallback.
- **Loading:** a second loader in the texture module (`.ktx`) reads and checks the file
  on the asset workers (`src/wgr_ktx.c`: KTX 1, little-endian, 2D, one of the three
  formats, every level's size matching its dimensions) and uploads the levels as they
  are. The formats are the plain (not sRGB) ones, like RGBA8 textures: the shaders
  treat texture colors as sRGB values either way. The texture module links it: a
  program without textures pays nothing.
- **Picking:** pixel-accurate sprite picking (`set_pick_alpha_test`) reads the PNG
  beside a compressed texture, as it reads a PNG texture's own file.

### Measured

A 2048x2048 texture (FlightHelmet's leather base color: a 5.5 MB PNG; each compressed
file 5.6 MB with its mipmaps), created synchronously, three times each:

|                           | PNG           | compressed          |
|---------------------------|---------------|---------------------|
| desktop GL (NVIDIA), BC7  | 59-89 ms      | 0.8-1.0 ms          |
| Pixel 9 (WebGL2), ASTC    | 125-203 ms    | 1.4-1.9 ms          |
| GPU memory with mipmaps   | 21.3 MB       | 5.3 MB              |

Download size depends on the image: a detailed 2K texture's compressed file was about
its PNG's size; small, simple images compress far better as PNGs (a 256x256 logo:
13.5 KB PNG, 87.5 KB each variant), though HTTP compression brings the variants down
(the 256x256 flame: 28 KB PNG, 22 KB for BC7 gzipped).

Checked on desktop GL, WebGL2 (SwiftShader), WebGPU (NVIDIA; BC7 through its
`texture-compression-bc` feature), the Pixel's WebGL2 (ASTC), and the Windows build
under Wine (BC7). `examples/textures.c` shows each texture as PNG and compressed.

### glTF models

- `tools/compress_textures.sh --gltf model.gltf` (`tools/compress_gltf.py`) compresses
  every image the model's textures use and writes `model.ktx.gltf` beside the model,
  leaving it as it was. Data textures (normal, metallic-roughness and occlusion maps)
  are compressed as linear, colors (base color, emissive) as sRGB. Each texture gets
  the `WGR_texture_ktx` extension, `{"source": <image>}`, pointing at an added
  `name.ktx` image; the texture keeps its own image as `source`. The extension is in
  `extensionsUsed`, not `extensionsRequired`: other viewers ignore it and use the
  original images, so the file stays a portable glTF. Only `.gltf` files with images
  in separate PNG or JPEG files: images inside the file (a `.glb`) are left as they are.
- libwgrender: for a texture with the extension, the dependency list names the variant this
  GPU can use instead of the texture's own image (only that file downloads), the
  worker reads it instead of decoding an image, and the texture is uploaded as it is.
  Without a usable variant (or if the file can't be read) the texture's own image is
  used as before. A missing variant is a dependency with a fallback: the texture's
  own image is fetched instead (with a warning), so the web gets it too.
- Checked by eye: FlightHelmet with its PNGs and compressed, side by side, match
  (normal maps included).

Loading Sponza and FlightHelmet (`make loadbench DESKTOP=1 [KTX=1]`), desktop GL
(NVIDIA), files local:

|                                   | PNG / JPEG textures | compressed   |
|-----------------------------------|---------------------|--------------|
| in the background: total          | 0.85-0.92 s         | 0.13 s       |
| in the background: worst frame    | 31-35 ms            | 21-22 ms     |
| synchronously                     | 1.35-1.37 s         | 0.11-0.12 s  |

FlightHelmet alone on the Pixel 9 (WebGL2, ASTC), files in its cache:

|                                   | PNG textures        | compressed   |
|-----------------------------------|---------------------|--------------|
| in the background: total          | 1.98-2.03 s         | 0.45 s       |
| in the background: worst frame    | 73-113 ms           | 97-102 ms    |
| synchronously                     | 1.36-1.41 s         | 0.09-0.10 s  |

The phone's worst frame while loading in the background stays about 100 ms either way:
the first frame drawing the loaded model compiles its shaders (~220 ms seen before on
WebGL2), and uploads of several large textures can land in one frame (TASKS).

### Not in this plan

- Smaller ASTC blocks (6x6, 8x8: 3.6 and 2 bits a pixel) for smaller downloads, at
  some quality.
- Models in a `.glb` (images inside the file): the tool would have to take the images
  out; KHR_texture_basisu.
- KTX 2 and its zstd supercompression; Basis transcoding as a second, optional module
  if single-file assets turn out to matter.
- sRGB formats (with sRGB-correct filtering), when the renderer moves to linear
  sampling.

*From docs/PLAN-textures.md.*

## Environment lighting (image-based lighting) and tone mapping

Status: **implemented (2026-09-16).** Decisions 1–5 accepted as recommended; see
"As built" for details and deviations. `include/wgr_environment.h`,
`wgr_scene_set_environment/background/tonemap`, `examples/environment.c`.

### Why

Materials follow glTF metallic-roughness (docs/PLAN-materials.md), but lighting
comes only from punctual lights plus a flat ambient term. glTF assets are authored
for, and previewed under, an environment (Blender, the Khronos sample viewer,
three.js, Godot). Without one:

- **Metals look dark.** A metal has no diffuse color; it only reflects. With
  nothing around it to reflect, it's black except for small highlights.
- **Rough and smooth look alike away from highlights.** Roughness shows mostly in
  how blurry reflections are.
- **Lit values clip.** Bright lights and HDR environments exceed 1.0, and today the
  shader just clamps, so highlights flatten to white and colors shift.

### Proposed design

#### Environment resource

```c
/* include/wgr_environment.h */
/* An environment map loaded from an equirectangular (latitude-longitude) image:
 * Radiance .hdr (recommended, true HDR) or PNG/JPEG (sRGB, low dynamic range).
 * Creation prepares the lighting data on the CPU (see below). */
wgr_handle_t wgr_environment_create(const char *path);
void        wgr_environment_release(wgr_handle_t environment);

/* include/wgr_scene.h */
/* Light the scene's models with an environment. intensity scales it (1 = as
 * authored); rotation (radians) turns it around the world up (+y) axis. 0 = none. */
bool wgr_scene_set_environment(wgr_handle_t scene, wgr_handle_t environment, float intensity, float rotation);
/* Draw the environment behind everything as the scene's background (skybox).
 * blur 0..1 shows it sharp to fully blurred (useful behind focused subjects). */
bool wgr_scene_set_background(wgr_handle_t scene, wgr_handle_t environment, float blur);
```

- A resource like Texture or Mesh: from a path, deduped, reference counted; scenes
  hold references. Handle kind ENVIRONMENT.
- Works with the asset layer as today: ensure the file, then create in the callback.
- The flat ambient term stays and adds on top (it's also the cheap option when a
  game doesn't want an environment).

#### What gets computed (at create, on the CPU)

Standard "split-sum" image-based lighting, as in the glTF sample viewer, Filament
and Unreal:

1. **Diffuse:** the environment's irradiance as 9 spherical-harmonic coefficients
   (a few numbers, sent as uniforms). Accurate for diffuse lighting and tiny.
2. **Specular:** a prefiltered cubemap: face size 128 with a mip chain, where mip 0
   is the sharp reflection and each further mip is the environment blurred for a
   higher roughness (GGX importance sampling, filtered from the source's mips to
   avoid noise). The shader picks the mip from the material's roughness.
3. **BRDF term:** a small 2D lookup table (64x64) computed once at startup, per
   the split-sum approximation. Shared by all environments.

Formats: RGBA16F (half float) cubemaps where the backend can filter them (desktop
GL, WebGL2, WebGPU all can; checked at runtime), otherwise RGBA8 with RGBM encoding.

Cost: a 2K .hdr decodes in roughly 50–100 ms and prefilters in a few hundred ms
single-threaded (to be measured). That's a load-time cost, like large textures
today; the loading-pipeline roadmap item would move it to a worker.

#### Shading

- Model shader, PBR materials: add environment diffuse (SH irradiance × diffuse
  color) and specular (prefiltered radiance × (F0 × A + B) from the LUT), both
  scaled by intensity and multiplied by the occlusion map. Specular also uses a
  simple horizon/occlusion term so crevices don't glow.
- Unlit materials ignore it, like lights. Models outside a scene (wgr_model_draw)
  stay unlit.
- Background: a full-screen pass drawn before a scene's 3D layers, sampling the
  cubemap by view direction at a mip chosen from `blur`.

#### Tone mapping and exposure

```c
typedef enum {
    WGR_TONEMAP_NONE = 0,   /* clamp (today's behavior) */
    WGR_TONEMAP_NEUTRAL,    /* Khronos PBR Neutral: colors unchanged until highlights roll off */
    WGR_TONEMAP_ACES,       /* filmic, more contrast and hue shift */
} wgr_tonemap_t;

bool wgr_scene_set_tonemap(wgr_handle_t scene, wgr_tonemap_t tonemap, float exposure); /* exposure in stops (EV) */
```

- Applied in the model shader (and the background), before the sRGB encode. There
  is no HDR framebuffer, so sprites, shapes and text are unaffected: they're
  display colors, not lighting.
- Per scene, like ambient and the environment.

#### Example and assets

- `examples/environment.c`: the material spheres (plastic, gold, roughness steps)
  and the gumshoe under an environment with a visible background; keys to switch
  environment, blur, tone mapping and exposure, and to toggle the environment off
  for comparison.
- Assets: one small CC0 HDR from Poly Haven at 1K resolution (about 1–2 MB) in
  `examples/assets/environments/`, with its source and license noted.

### Decisions

1. **Environment is a resource from an equirectangular image** (`.hdr`, or
   PNG/JPEG), set per scene with intensity and rotation. Recommend: yes. Cubemap
   face images (6 files) and KTX2 prefiltered cubemaps can come later.
2. **Prefilter on the CPU at create** (SH irradiance + GGX mip chain), versus on the
   GPU with render targets. Recommend CPU: identical results on every backend, no
   float render targets needed (WebGL2 needs an extension for those), and easy to
   unit test. Revisit if load times hurt.
3. **Tone mapping default: Khronos PBR Neutral** for scenes, versus NONE (keeps
   today's clamping). Neutral leaves colors in the normal range untouched and only
   rolls off highlights, which is what glTF viewers use by default; it changes how
   bright highlights look in existing scenes. Recommend: Neutral by default.
4. **Background (skybox) included now**, drawn from the same environment with a
   blur setting. Recommend: yes; without it, reflections look disconnected from
   the scene.
5. **Ship a CC0 Poly Haven HDR** (~1–2 MB) as an example asset, versus a small
   procedurally generated sky. Recommend: Poly Haven, since realistic reflections
   are the point; a generated sky can be a second, tiny option.

### As built

- **Timing:** `wgr_environment_create` takes ~330 ms for a 1K `.hdr` (1024x512) on a
  desktop CPU, single-threaded: decode, SH projection, a source cubemap, and the GGX
  prefilter (96 samples per texel, filtered importance sampling). The loading
  pipeline roadmap item would move it off the main thread.
- **Background at the image's own resolution:** the background samples the source
  cubemap (face size = image width / 4, rounded down to a power of two: 256 for 1K)
  with box-filtered mips for blur, rather than the 128-pixel lighting cubemap, which
  was visibly soft. A 1K HDR still looks soft full screen; use 2K/4K for sharper
  backgrounds.
- **No RGBM fallback:** every target backend (desktop GL, WebGL2, WebGPU, and the
  dummy backend) filters RGBA16F, so a backend that can't disables environments with
  a warning instead. Tracked in TASKS.md.
- **Orientation verified:** the background rendered on WebGL2 and WebGPU from three
  camera directions matches a CPU reference made directly from the equirectangular
  image (no mirrored or flipped cube faces). MetalRoughSpheres under the studio HDR
  looks as expected (smooth metals mirror-like, rough ones blurred, non-metals keep
  their color).
- **Bug found by tests:** the half-float conversion rounded with the wrong bit
  (off by one step for some values); fixed and checked against Python's IEEE half.
- **BRDF table checks:** exact Schlick split for smooth surfaces, A + B ≤ 1,
  decreasing with roughness for n·v ≥ 0.25 (the table has its known bump at grazing
  angles), and (0.72, 0.02) at n·v 0.5, roughness 0.5 like Unreal's table.
- **No horizon occlusion term yet:** environment specular is scaled by the
  occlusion map only; the planned horizon term (so reflections don't show through
  the surface at grazing normal-mapped angles) is left for later.
- **Tone mapping:** new scenes default to Khronos PBR Neutral at 0 EV; models outside
  a scene and sprites/shapes/text are unchanged.
- **webcheck fixes found along the way:** requests to the page now time out (a page
  that never started could hang a headed run indefinitely), and the loading check no
  longer calls into the page before libwgrender has started (that aborted pages at random,
  about one run in two).

### Verification

- Unit tests (pure CPU parts):
  - equirectangular ↔ direction and cubemap face ↔ direction mappings agree
  - SH projection: a constant environment gives constant irradiance (π × radiance);
    a known directional pattern gives the expected coefficients
  - prefiltered mips conserve energy (average radiance stays within tolerance) and
    mip 0 reproduces the source
  - BRDF LUT against reference values (e.g. Karis's published curve)
  - half-float and RGBM encoding round trips; .hdr decode dimensions and values
- Visual: Khronos MetalRoughSpheres, EnvironmentTest and a glTF with metals, under
  the same HDR, compared side by side with the Khronos glTF Sample Viewer (same
  environment, Neutral tone mapping) on desktop GL, WebGL2 and WebGPU.
- Timing of create for the example HDR; `make verify`, `make webcheck` (both
  backends).

*From docs/PLAN-environment.md.*

## Fixed-rate tick + frame callback timing arguments

Status: **implemented (2026-09-16).**

### Problem

libwgrender has one callback, `wgr_set_frame(fn)`, called once per rendered frame, with
timing read from the global `wgr_get_delta_time()`. That leaves no good place for
simulation that must be deterministic:

- A variable `dt` makes physics and gameplay depend on the frame rate.
- When frames stall (a hidden or throttled window), `dt` is clamped to 100 ms, so
  game time silently runs slow.
- `wgr_set_target_fps` gets misused as a simulation-rate control. It should only
  be a power cap.

### Design

Two callbacks, named for what they are (frame rate vs tick rate):

```c
typedef void (*wgr_tick_fn)(float dt, void *user_data);                        /* dt = 1/hz, always */
typedef void (*wgr_frame_fn)(float dt, float tick_fraction, void *user_data);  /* once per rendered frame */

void wgr_set_tick(wgr_tick_fn tick_fn, void *user_data, int hz);  /* hz <= 0 or NULL fn: no tick */
void wgr_set_frame(wgr_frame_fn frame_fn, void *user_data);
```

- **tick:** simulation at a fixed rate. Runs 0..N times before each frame, always
  with the same `dt`. Never draws.
- **frame:** variable update plus drawing, once per rendered frame. `dt` is the
  time since the previous frame. `tick_fraction` (0..1) is how far this frame is
  into the next tick, for drawing tick state smoothly:
  `draw_pos = lerp(prev_tick_pos, tick_pos, tick_fraction)`. It is 0 when no tick
  is set.
- **Timing is passed as arguments, not read from globals.** `wgr_get_delta_time()`
  is removed. `wgr_get_time()` (absolute clock) stays.
- Name: `tick_fraction`, not the tutorial term "alpha" (which already means
  transparency in a graphics library). Godot calls it the physics interpolation
  fraction, Bevy the overstep fraction.

#### Scheduling

Per rendered frame (after frame pacing, before the frame callback):

```
accumulator += real elapsed time since the previous frame
steps = 0
while accumulator >= step and steps < MAX_TICKS_PER_FRAME (5):
    tick(step); accumulator -= step; steps++
if accumulator >= step:            # still behind after 5 ticks (a stall)
    accumulator = fmod(accumulator, step)   # drop the backlog, keep the phase
tick_fraction = accumulator / step
frame(dt, tick_fraction)
```

- The cap prevents the "spiral of death" (slow ticks causing more ticks). After a
  stall, simulation time falls behind wall time instead of freezing the app.
- The accumulator uses real elapsed time from the monotonic clock, not the
  smoothed or clamped frame `dt`.
- Changing the rate or callback resets the accumulator.
- Web: identical. Ticks run inside the browser's frame callback.
- The scheduling logic is pure (`src/wgr_tick_clock.c`) and unit tested, like
  `wgr_frame_pace`.

#### Input edges

Input is read through getters (`wgr_input_get_keyboard_state()`,
`wgr_input_get_mouse_state()`), so "pressed this frame" needs a defined meaning
inside a tick:

- **Edges (pressed / released, mouse and wheel deltas, typed keys and chars) are
  relative to the callback you're in.** In a tick: since the previous tick. In a
  frame: since the previous frame. Held state (`down`) and the mouse position are
  shared.
- So each press is seen **exactly once** by ticks, however many ticks run per
  frame: the first tick after the press sees it, later ticks in the same frame
  don't, and if a frame runs no ticks the press carries over to the next tick.
- Implementation: events update two edge sets. Tick edges clear after each tick;
  frame edges clear after the frame callback. The runtime tells the input module
  which callback is running.
- Precedent: Godot 4's `is_action_just_pressed()` is relative to the physics frame
  inside `_physics_process`. Unity's per-frame `GetKeyDown` inside `FixedUpdate`
  is the classic bug this avoids.

### Changes

- `include/wgr.h`: `wgr_tick_fn`, new `wgr_frame_fn` signature, `wgr_set_tick`; remove
  `wgr_get_delta_time`; document frame vs tick and the input edge rule.
- `src/wgr.c`: tick scheduling in `on_frame`, callback context for input.
- `src/wgr_tick_clock.c` + `src/internal/wgr_tick_clock.h`: pure scheduler.
- `src/wgr_input.c`: separate tick and frame edge sets; getters pick by context.
- Every example's frame callback gets the new signature; `model.c` and
  `simple.c` use the `dt` argument.
- New `examples/tick.c`: an object moving at a 10 Hz tick, drawn raw (visibly
  stepping) next to one drawn with `tick_fraction` (smooth), and a counter that
  increments once per key press inside the tick.
- README loop section, TASKS (resolves the frame-timing decision), parity map
  (`rl_get_delta_time` becomes dropped: timing is passed to callbacks).

### Verification

- Unit tests: tick clock (steps per frame, fraction, max-steps cap and backlog
  drop, rate change, no tick); input edges per context (press seen by exactly one
  tick with 0, 1 and 3 ticks per frame; frame edges unaffected by ticks).
- `examples/tick.c` screenshots on desktop and web; `make test`, `make check`,
  `make webcheck` (WebGL2 + WebGPU).

*From docs/PLAN-tick.md.*

## Frustum culling

Status: **phases 1 and 2 built** (2026-09-21). Phase 3 (a visibility mask, 2D members,
a spatial index) is open and not obviously needed yet.
Builds on the scene's bounds registry ([ARCHITECTURE.md](ARCHITECTURE.md)) and shadows
([PLAN-shadows.md](PLAN-shadows.md)).

### Why

Nothing is culled. A scene walks every member every frame and submits it; the only
visibility test is the manual `wgr_model_set_visible` flag. The GPU throws away what
lands off screen, but only after libwgrender has paid for the draw.

Measured with `make shadowbench DESKTOP=1` on an RTX 4080: **4000 models cost 4.89 ms a
frame with the camera pointed away from all of them, against 5.18 ms with every one in
view** — the same price for drawing nothing. A world larger than its view pays for all
of it, and most worlds are.

At about 1.2 microseconds to submit a model, every object rejected is 1.2 microseconds
back. The test that rejects it is a few dozen instructions.

### Where we are

- `wgr_scene` keeps members in layers and, for each layer, calls each drawable's
  `draw_opaque` and `collect_transparent` in turn (`draw_layer`).
- Every 3D kind already registers bounds with the scene for picking:
  `wgri_scene_register_bounds(kind, fn)` gives a local AABB plus the model matrix, and
  models, sprite3d, shape3d and text3d all provide one.
- `wgr_model`'s `begin_draw` already builds a world AABB (`wgri_pick_world_aabb`) to pick
  the placement's lights, so a model's world bounds are computed either way.
- The shadow pass redraws **every** caster in the lighting environment, including ones
  outside what the light's map covers (`shadow_distance`).

### Proposed design

#### Where the test goes

In `wgr_scene`, not in each drawable: that is where the camera is, and one test then
covers models, sprites, shapes and text alike through the bounds registry. Each scene
draw builds the camera's six frustum planes once; `draw_layer` tests each member's
world AABB and skips the ones outside.

Pure helpers, in `internal/wgr_math.h`, exposed for tests:

```c
/* The six planes of a view-projection, outward normals, for testing AABBs. */
void wgri_frustum_from_view_proj(wgri_mat4_t view_proj, wgri_plane_t out[6]);
/* False when the box is wholly outside any plane (a conservative test: a box that
 * straddles a corner may pass and be drawn). */
bool wgri_frustum_test_aabb(const wgri_plane_t planes[6], vec3_t min, vec3_t max);
```

#### Casters must not vanish

A model behind the camera can still throw a shadow into view, so culling to the
camera's frustum alone would make shadows pop at the screen edge. The volume tested for
a caster is therefore the camera frustum **extended along the light's direction** by the
light's shadow distance: if an object's swept box misses that, it can neither be seen
nor cast into view, and is skipped. Objects that only cast are still submitted to the
camera pass as they are today — correct, and no worse than now.

#### The shadow pass culls too

Separately, `wgri_model_draw_shadow_casters` tests each placement against the light's own
frustum (the fit it is already given) and skips casters outside it. A light's map covers
`shadow_distance`; today every caster in the scene is redrawn into it regardless. This
needs the placement to keep the world AABB it already computes for light selection.

#### What it costs

One AABB build and six plane tests per member per scene draw, plus one more test per
caster per casting light in the shadow pass. Against ~1.2 microseconds saved per
rejected model, the test pays for itself at any scene size.

### Decisions (answered; phase 1 took all three recommendations)

1. **Keeping casters correct.** Extend the tested volume along the light (above):
   simple, conservative, one place, but an off-screen caster is still submitted to the
   camera pass and clipped by the GPU. The alternative is a per-placement visibility
   mask — queue it, mark it camera-invisible, skip its items in the camera pass and
   draw them in the light's — which also removes that cost but needs the queue and the
   drawable interface to carry the flag. Recommend: the extended volume first, the mask
   later if the camera pass turns out to care.
2. **Skinned models.** Their bounds are the rest pose (the same limitation light
   selection has), so an animation that reaches outside it could be culled while a limb
   is still on screen. Pad a skinned model's bounds by a fraction of their size, or use
   the posed bounds when the pick cache already has them, or never cull skinned models.
   Recommend: pad — it is one multiply, and the posed bounds are only cached after a
   pick.
3. **A switch.** `wgr_scene_set_culling(scene, bool)`, default on, so a scene can turn it
   off when debugging what is drawn, or when a game knows everything is in view.
   Recommend: yes, it is two lines and it is the escape hatch if bounds are ever wrong.

### Phasing

1. The helpers, the scene's camera cull (all 3D kinds), the caster volume, the switch.
   **Built.**
2. The shadow pass's own cull against each light's frustum. **Built.**
3. Later, if wanted: the visibility mask from decision 1; 2D members against the screen
   rectangle; a spatial index so the per-member test itself stops scaling with the
   scene.

### Phase 1 as built

Three pure helpers in `src/internal/wgr_math.h`, so the tests can reach them without a
GPU: `wgri_frustum_from_view_proj` (Gribb-Hartmann, planes normalized so a test gives a
real distance), `wgri_frustum_test_aabb` (the corner furthest along each normal — out
only when the box is wholly behind one plane) and `wgri_aabb_sweep` (a box pushed along
a direction: where its shadow could land).

`wgr_scene` builds the planes once per draw, in `begin_culling`, from the camera it is
about to draw with and the lighting environment it just pushed — the index comes from
`push_lighting`'s return, not from the light module, so the core still reaches lights
only through hooks. For each casting light it keeps the direction and the reach its map
covers (`shadow_distance`, or `range` for a spot whose range is shorter). `draw_layer`
then asks `visible()` per member, for the opaque pass and the transparent one alike.

`visible()` resolves the member's bounds through the scene's existing bounds registry,
so every 3D kind is covered at once — model, sprite3d, shape3d, text3d — and a member
with no bounds (the 2D kinds) is always drawn. The world AABB is padded by 15%
(`WGRI_CULL_PAD`) because a skinned model's bounds are its rest pose; better to draw a
little too much than to cull a raised arm. If the box misses the view, and the member
casts, the box is swept along each casting light and tested again: a caster off screen
whose shadow falls on screen is kept.

Two pieces moved to make that cheap. `wgr_model` now caches the world matrix it builds
(`model_world`) instead of composing it for the cull and again for the draw, and it
answers `cull_bounds` with the posed bounds only when a pick already computed them —
culling never re-skins a mesh. `wgr_scene_set_culling` / `wgr_scene_is_culling` are the
switch, on by default.

#### Measured

`make shadowbench DESKTOP=1` on an RTX 4080 gained two cases, "look away" and "away, no
cull", which point the camera outward from the grid so every model is behind it:

| models | away, no cull | look away | scene (cull test) |
|-------:|--------------:|----------:|------------------:|
|    100 |       0.31 ms |   0.11 ms |           0.02 ms |
|   1000 |       2.23 ms |   0.28 ms |           0.19 ms |
|   4000 |       6.78 ms |   0.50 ms |           0.42 ms |

So 4000 models nobody can see cost 6.78 ms before and 0.50 ms now, and what is left is
almost entirely the test itself — about 0.1 microseconds a member against the 1.2 it
saves. Both rows come from the same run, so the gap between them is the measurement;
the shadow pass is skipped along with the models, since with nothing queued nothing
receives. With everything in view the bench doesn't move outside its ±0.3 ms run-to-run
noise (4000 models, no shadows: 4.96 ms against 5.18 before), the cached world matrix
paying for the test.

### Phase 2 as built

The depth pass had been redrawing every caster in the lighting environment, whatever
the light's map actually covered. It now builds that light's six planes from the fit it
is already given (`wgri_frustum_from_view_proj(fit.view_proj)`) and tests each placement
against them once, remembering the answer for the rest of that placement's primitives.

The test is exact here, not just conservative. A directional fit is an ortho box whose
side planes are parallel to the light, so a caster outside one projects along the light
to points that stay outside it — it cannot shadow anything in the box. A spot's planes
all pass through the light's own position, so the same holds for its cone. The near
plane is pulled back (`WGR_SHADOW_PULLBACK`) precisely so a caster between the light and
the box is kept, and the unit test checks that a body overhead still makes the cut.

For this, `wgr_model` stores the world AABB it already builds at submit time to pick a
placement's lights (padded once, there, by the same `WGRI_CULL_PAD`), so the depth pass
costs one box test per placement per casting light and no bounds work of its own. The
pad constant and `wgri_aabb_pad` moved to `internal/wgr_math.h`, which is where the
frustum helpers live, so the scene and the depth pass grow boxes the same way.

#### Measured

`make shadowbench DESKTOP=1`, RTX 4080, 4000 models — the sun's `shadow_distance` is
40 units and the grid is 140 across, so most casters were being drawn into a map that
could never hold them.

Read this bench as **differences inside one run**, never as one run against another:
the same "off" row measured 4.96, 5.25, 5.27, 5.40 and 5.57 ms over five runs, so a
number carries about ±0.3 ms of run-to-run noise. What a casting light costs is the gap
between its row and the "off" row in the same run:

| over "off", 4000 models | before phase 2 | after |
|-------------------------|---------------:|------:|
| sun casting, 1024       |        +1.6 ms | +0.1 ms, and ±0.3 across runs |
| sun and spot, 1024      |        +3.2 ms | +0.1 ms, and ±0.3 across runs |

So the pass has gone from the largest cost in the frame to something this bench can no
longer separate from noise: in three repeat runs the casting rows landed above and
below their own "off" row. At 100 models nothing moves either, because the fit covers
the whole scene and nothing is culled — which is the check that it isn't culling what
the map needs.

### Verification

- Unit tests: the planes of a known projection; AABBs inside, outside and straddling;
  a scene where a member outside the view isn't submitted (the model queue's counts
  say so) and one inside is; a caster behind the camera still queued while a light
  casts, and not when none does; the switch.
- `shadowbench` with the camera pointed away should fall from ~4.9 ms to near nothing,
  and with everything in view should not get slower.
- Visual: `examples/shadows.c` and `examples/lights.c` unchanged as the camera turns —
  nothing pops at the edges, and shadows from off-screen casters stay.
- `make verify`, `make webcheck` (both backends), Wine.

*From docs/PLAN-culling.md.*

## handle-only public API (librl-style asset/resource split)

Status: **implemented.** The handle-only public surface, the asset/resource
split, and `wgr_asset_ensure_async(path, fetch_url, flags)` are in and enforced by
`make check` (`tools/check_naming.sh`). Kept as the design record.
Supersedes an earlier draft of this file that invented a generic
`wgr_asset_load`/`wgr_destroy`; this version follows librl's proven model instead.

### The rule

The public API (`include/*.h`) takes and returns only:

- **handles** (`wgr_handle_t`),
- **integral / float types and enums**,
- **`const char *`** for paths and text.

**No other pointers in user code** — no `unsigned char *data`, no struct
pointers. Bindings (JS/Nim/Haxe/Lua) stay mechanical mirrors.

No backwards compatibility: anything breaking the rule is **removed**, not aliased.

### The model (from librl)

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

### Target public surface

#### Create (sync)

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

#### Asset (fetch/ensure) — rework wgr_asset to librl's shape
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
void        wgri_asset_tick(void); /* pump the queue each frame (runtime-driven) */
/* + poll/finish/get_task_path/free_task as in librl */
```

`WGR_HANDLE_KIND_ASSET_TASK` already exists for the task handle.

#### Removed (public)
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

#### Not doing (corrections to the earlier draft)
- **No generic `wgr_asset_load(path) → handle`.** Creation is typed and sync; the
  asset layer ensures files, it doesn't create resources.
- **No generic `wgr_destroy(handle)`.** librl keeps typed `wgr_*_destroy`; we will
  too. (The 6-bit kind field *could* support a generic destroy later, but it's
  out of scope and not the established model.)

### New dependency: `wgr_fs`

Ensure needs a local-file layer (librl's `rl_fs`): a root dir on desktop, idbfs
on web, with fetch-from-host on miss. libwgrender has **no fs module yet**, so this is
net-new and is the substantive part of the work — `wgr_asset` ensure sits on top
of it.

### Phasing

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

### Enforcement (`make check`)
- Extend `tools/check_naming.sh` (or a sibling) to **fail on**:
  - any `_create_from_memory` in `include/`,
  - raw pointer params in `include/*.h` other than `const char *`.
- Add the handle-only rule to AGENTS.md § Naming/API.
- Update ARCHITECTURE.md §7 (public API shape) + the Asset→Resource→Object tables.

### Decisions locked
- **Creator naming:** bare `_create` everywhere; resource noun → path, object noun
  → handle. No `_create_from_file`/`_from_memory`. (Resolved by the principle above.)
- **`wgr_audio` is public.** Objects come from resource handles, so a sound needs an
  audio handle — `wgr_audio_create(path)` joins the public resource creators.

### Open questions
1. **Scope now:** Phase 1 only (desktop; removes the pointers and reshapes the
   API) and leave `wgr_fs`/web fetch for a follow-up, or build Phase 1+2 together?
2. **Music vs Sound:** both are now objects over an Audio handle, differing only
   by default loop. Keep both public surfaces, or collapse to `wgr_sound_create`
   + a loop setter and make `wgr_music_*` go away? (Separate from this plan, but
   adjacent.)

*From docs/PLAN-handle-only-api.md.*

## Lighting (light objects, per-scene lighting)

Status: **implemented (2026-09-16).** See `include/wgr_light.h` and `examples/lights.c`.
Builds on the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)) and the
scene render passes (opaque, then sorted transparent). Leaves room for the
materials work in [ROADMAP.md](ROADMAP.md).

### Where we are

- **libwgrender today:** one hardcoded directional light plus ambient, baked into
  `apply_fs` in `src/wgr_model.c` (direction -0.6, -1, -0.5; ambient 0.3). Models
  are lit; shapes and sprites are not. There is no API.
- **librl:** one global directional light, `rl_enable_lighting` /
  `rl_disable_lighting` / `rl_is_lighting_enabled` / `rl_set_light_direction` /
  `rl_set_light_ambient`, living in the camera module, off by default, models only.

What librl taught us: a single global light can't express lamps, torches,
spotlights or colored light; global state in an unrelated module is hard to find;
and names that don't follow `wgr_<section>_<action>` fragment the API.

### Goals

- Lights are **objects** with handles, placed in scenes like drawables, with the
  usual create / set / destroy lifecycle. Handle-only API.
- **Directional, point and spot** lights from the start, so the API never has to
  change to add a type.
- **Per-scene** lights and ambient. Two scenes (e.g. world and a model preview)
  can be lit differently.
- **Efficient:** bounded per-draw cost, no per-frame allocation, one uniform
  upload per primitive as today.
- Light parameters follow **glTF `KHR_lights_punctual`** (color, intensity,
  range, inner/outer cone), so lights can later be imported from glTF files and
  artists' values mean the same thing.
- Correct, explicit behavior over compatibility: nothing is lit unless you light it.

### Non-goals (this plan)

- Shadows, PBR specular, image-based lighting, light probes, lightmaps.
- Lighting sprites, shapes or 2D. They stay unlit until materials land.
- Clustered/tiled light culling. Per-draw selection (below) is enough for tens of
  lights; clustering is a later optimization with the same API.
- Importing lights from glTF files (the parameters are compatible; import later).

### Public API

New header `include/wgr_light.h`, new handle kind `WGR_HANDLE_KIND_LIGHT = 16`.

```c
typedef enum {
    WGR_LIGHT_DIRECTIONAL = 0, /* infinitely far: direction only (sun, moon) */
    WGR_LIGHT_POINT = 1,       /* position + range (lamp, torch) */
    WGR_LIGHT_SPOT = 2,        /* position + direction + range + cone (flashlight) */
} wgr_light_type_t;

wgr_handle_t wgr_light_create(wgr_light_type_t type);
void        wgr_light_destroy(wgr_handle_t light);

bool wgr_light_set_color(wgr_handle_t light, wgr_handle_t color);   /* default white */
bool wgr_light_set_intensity(wgr_handle_t light, float intensity);  /* default 1 */
bool wgr_light_set_position(wgr_handle_t light, float x, float y, float z);  /* point, spot */
bool wgr_light_set_direction(wgr_handle_t light, float x, float y, float z); /* directional, spot; normalized */
bool wgr_light_set_range(wgr_handle_t light, float range);          /* point, spot; 0 = infinite */
bool wgr_light_set_spot_cone(wgr_handle_t light, float inner_angle, float outer_angle); /* spot, radians */
bool wgr_light_set_enabled(wgr_handle_t light, bool enabled);
bool wgr_light_is_enabled(wgr_handle_t light);
```

Scene integration reuses the existing membership API; the scene recognizes the
light handle kind:

```c
wgr_scene_add(scene, light, layer);      /* layer is ignored for lights */
wgr_scene_remove(scene, light);
wgr_scene_set_ambient(scene, color, intensity);  /* default: intensity 0 (no ambient) */
```

A light can be in several scenes. Destroying a light removes it from lighting
everywhere (scenes resolve handles each frame, so a stale handle is skipped).

#### Defaults

Decided: correct behavior over compatibility (see AGENTS.md).

- **A new scene has no lights and no ambient.** Its models render black until you
  add a light or set ambient. Nothing is lit implicitly.
- **Models drawn outside a scene** (`wgr_model_draw`) have no lighting
  environment, so they render **unlit**: base color x tint. Lighting is something
  a scene provides.
- The built-in hardcoded light in `src/wgr_model.c` is removed. Examples that draw
  models (`model`, `pick`, `simple`) add explicit lights.
- `wgr_light_set_*` on a property that doesn't apply to the type (e.g. range on a
  directional light) stores it and returns true, so switching types later isn't
  lossy. Logging a warning would be noise.

### Shading

Same model as today, extended to several colored lights, in world space:

```
lit = ambient_color * ambient_intensity
    + sum over selected lights: light_color * intensity * max(dot(N, -L), 0) * attenuation * spot
final_rgb = base_color_rgb * lit
```

- **Point/spot attenuation** (glTF `KHR_lights_punctual` recommendation): smooth
  window to zero at `range`, inverse-square inside it:
  `clamp(1 - (d / range)^4, 0, 1)^2 / max(d^2, 0.01)`; `range = 0` means no cutoff.
  Intensity is a unitless multiplier in this plan (not candela/lux); documented as
  such so a physical-units mode can come later without an API change.
- **Spot cone:** smoothstep between `cos(outer)` and `cos(inner)`.
- **Diffuse only** (Lambert), like today. Specular belongs to materials.
- The vertex shaders gain a world-space position output (static and skinned);
  point and spot lights need it.

### Efficiency: per-draw light selection

- Uniform block holds **`WGRI_MAX_DRAW_LIGHTS = 8`** lights, packed std140 as four
  `vec4[8]` arrays (position+range, direction+type, color*intensity, spot cosines)
  plus a count and the ambient term. About 136 floats, uploaded with the existing
  per-primitive `fs_params` call.
- Each scene layer draw gathers the scene's enabled lights once (cap
  `MAX_SCENE_LIGHTS = 64`, fixed arrays, no allocation).
- Per model placement (not per primitive), score every enabled light by its
  estimated contribution to the model and keep the top 8 (decided: 8):
  - **directional:** `luminance(color) * intensity`; they reach everything.
  - **point:** distance `d` from the light to the *nearest point* of the model's
    world AABB (0 if inside). If `range > 0` and `d > range`, the light is culled.
    Otherwise `luminance(color) * intensity * attenuation(d)`.
  - **spot:** as point, times the cone factor toward the AABB center.
  - Ties keep scene insertion order, so selection is deterministic.
  The selection is stored on the queued draw and reused for all its primitives.
- Known limits of per-object selection: a large model (terrain) touched by more
  than 8 lights only gets the strongest 8, so a light can visibly drop out; and
  near-equal scores can flip as things move. Later fixes that don't change the
  API: hysteresis (prefer last frame's picks) and clustered light culling.
- Cost is O(models x scene lights) per frame on the CPU, which is fine for tens of
  lights. The selection function is pure C and unit-tested.
- Transparent primitives use their placement's selection, same as opaque.

### Internals

- `src/wgr_light.c`: handle pool, light storage, public API, and a pure
  `wgri_light_select(...)` used by `wgr_model`.
- `src/internal/wgr_light.h`: packed per-draw light data and the selection API.
- `wgr_scene`: `wgr_scene_set_ambient`; while drawing a layer, collect enabled lights
  (scene members with the light handle kind) and hand them to `wgr_model` for the
  frame. Lights don't register draw passes, so the render passes ignore them.
- `wgr_model`: `begin_draw` selects lights for the placement; `apply_fs` uploads
  them. The hardcoded light is removed; draws outside a scene upload an "unlit"
  flag so the shader outputs base color x tint.
- `wgr_model.glsl`: world position varying, light loop, regenerated with `make shaders`.
- Parity map: `rl_enable_lighting`, `rl_disable_lighting`, `rl_is_lighting_enabled`,
  `rl_set_light_direction`, `rl_set_light_ambient` become `ported` to the light
  API (functional parity: a scene with one directional light and ambient).

### Verification

- **Unit tests:** light API defaults and setters; selection (scoring, range
  culling against the AABB, ranking, stable ties, cap of 8, disabled lights
  skipped); attenuation and cone helpers match the shader formulas.
- **Visual test scene:** gumshoe under a colored sun, a point light that falls off
  with range, a spot cone on the floor, a scene with no lights (black models), and
  a model drawn outside a scene (unlit).
- `examples/simple.c`: sun + ambient 0.25 via the API; its lighting `PARITY:` note
  goes away. `model.c` and `pick.c` get explicit lights.
- New `examples/lights.c` demonstrating all three types (also exercised by
  `make webcheck`).
- `make test`, `make check`, `make parity`, `make webcheck` (WebGL2 + WebGPU).

### Phasing

1. Light objects + API + scene membership + ambient; shader with the 8-light loop;
   per-draw selection; default light. Unit tests and the visual scene.
2. `examples/lights.c`, update `simple.c`, parity map, docs.

Later (separate plans): specular via materials, shadows for directional and spot
lights, glTF light import, clustered culling if scenes need hundreds of lights.

### Decisions (2026-09-16)

1. **8 lights per draw**, chosen by estimated contribution (above), not distance alone.
2. **No implicit lighting:** scenes start with no lights and no ambient; models
   drawn outside a scene are unlit.
3. **Shapes and sprites stay unlit** until the materials work.

*From docs/PLAN-lighting.md.*

## Load on create, and polled tasks instead of callbacks

Phases 1 and 2, as planned and as built. The rest of the plan is open: [PLAN-tasks.md](PLAN-tasks.md).

### Why

A C callback is the hardest thing in the public API for a binding: a function pointer
and a `void *` it hands back. A JS guest can't pass one at all (the Haxe binding's guest
ABI works around six), and every binding writes a trampoline for each. And most of
wgrender's callbacks exist for one reason: a resource can only be created once its file
is local and loaded, so the program ensures the file, waits for a callback, and creates
the resource in it.

libwgt (`gfx/include/wgt_texture.h`, `core/src/wgt_core_load_priv.h`) shows the way out,
built on a pipeline it says it cribbed from wgrender's own: **a resource loads on
create.** `create(path)` returns a handle at once, PENDING; the file is made local,
prepared on a worker and finished on the main thread over the frames that follow; the
handle becomes READY or FAILED, and nothing is ever called back. Making a file local
*without* loading it is a separate thing, a task (libwgt's `wgt_asset_ensure`, designed
there and not yet built). Whatever may wait is read, never called back: a status that
changes only at the start of a frame, so a frame callback that checks it sees each
change once, in order.

### What we have

- **`wgr_*_create(path)` is synchronous** (`wgri_loader_create`, `src/wgr_asset.c`): it
  returns the resource the asset layer already loaded for that path, or else reads,
  decodes and finishes it on the spot, on the main thread, holding up the frame, and
  returns **0 for any failure** -- a missing file, a broken one -- with nothing to ask
  why. It never fetches: the file must be local.
- **`wgr_asset_ensure_async` does two things**: makes the file local (fetching and
  caching it if it's missing) *and* loads the resource its extension names, unless
  `WGR_ASSET_FILE_ONLY`. That loaded resource is held out of sight until the program
  creates it in `wgr_asset_add_task`'s callback (or a group's).
- **Callbacks in the public API (11 calls):** the loop setters; `wgr_asset_add_task`,
  `wgr_asset_ping_host`, `wgr_asset_set_fetcher`; the event bus.
- **Readiness:** `wgr_model_is_ready` only; nothing says FAILED.

The machinery for the rest is there: the asset layer's queue already fetches, caches,
prepares on workers and finishes on the main thread within a budget. What changes is who
starts it (create, not ensure) and how its outcome is read (a status, not a callback).

### Design: a resource loads on create


```c
typedef enum {
    WGR_RESOURCE_NONE    = 0,  /* not a resource of this kind */
    WGR_RESOURCE_PENDING = 1,  /* its file is being made local, prepared or finished */
    WGR_RESOURCE_READY   = 2,
    WGR_RESOURCE_FAILED  = 3,  /* the fetch, the file or the decode failed (logged why) */
} wgr_resource_status_t;      /* wgr_resource.h */

wgr_handle_t          wgr_texture_create(const char *path);       /* as now, but at once, PENDING */
/* ... the same for mesh, audio, font, environment, shader */

/* wgr_resource.h: one call for any resource, by its handle's kind (each module
   registers its getter); NONE for anything that isn't one */
wgr_resource_status_t wgr_resource_get_status(wgr_handle_t resource);
```

- `create` returns a handle at once, PENDING, and queues the load: make the file local
  (from the cache, or **fetched** if it's missing, exactly as ensure fetches today), then
  prepare on a worker, then finish on the main thread within the upload budget. On
  desktop with the file on disk that's typically the next frame.
- **The path is an asset path**, as `ensure` takes: relative to the asset root, which is
  the host directory, the desktop cache directory under a URL host, or `/wgr` on the
  web, so the same path names the same file everywhere. Normalized, and refused (a
  FAILED handle, logged) when it's absolute, names a drive or climbs out. A redirect, a
  `.ktx` variant or a fallback changes where the bytes come from, never the key: a
  resource is found by its asset path, not by the local path it was read from. A
  file-system path outside the root can't be created from.
- **A key an ensure took from an explicit source** (a `fetch_url`) names that file:
  creating the key loads it from wherever the ensure found it (on desktop a local
  source is read in place, not copied under the key). A redirect's or a `.ktx`
  variant's answer isn't kept that way; a create plans those afresh.
- **0 only when there's no room** for another resource of that kind. A bad path, a
  missing file, a failed fetch or a file that won't decode gives a handle that's
  FAILED. Creating the same path again gives the same handle, with one more reference,
  whatever its status.
- **Drawing a resource that isn't READY is always safe.** PENDING is "not there yet":
  a sprite or texture draw using a PENDING texture draws nothing (and isn't picked), a
  material slot draws as if unset (its own default), a model whose mesh isn't READY
  isn't drawn, a scene whose environment isn't READY is lit as if it had none and
  draws no background (FAILED too: there's no sensible placeholder sky), a font draws
  as the default font, a sound whose audio isn't READY plays when it is. FAILED is "visibly broken": a texture draws the placeholder
  (`wgr_texture_set_placeholder`). Decided over the placeholder while PENDING too
  (libwgt's choice): on a slow first visit to the web build, every texture would show
  the magenta checker for seconds. Each header says so.
- **A custom material follows the same rule as an object**, its shader being the
  resource it uses: `wgr_material_create_custom(shader)` takes a shader in any status.
  While the shader is PENDING the material isn't drawn (an effect is skipped in the
  chain), and its setters keep values by name, applied once the shader is READY; a
  name the shader doesn't declare is logged then, so a setter can only refuse an
  unknown name once the shader is READY (the header says so). Getters read the kept
  values. FAILED, it draws as a fallback that's visibly broken: flat magenta, unlit,
  the shader's version of the texture placeholder.
- `wgr_model_is_ready` goes: a model is ready when its mesh is
  (`wgr_mesh_get_status(wgr_model_get_mesh(model))`).
- **Files that name other files** (a glTF's buffers and images) load together, as ensure
  loads them now: a missing buffer fails the mesh, a missing image warns and uses the
  placeholder.
- Every header with a `create(path)` says what it refuses and when it's FAILED (AGENTS.md:
  "false for ..." names every refusal; for a handle, "0 only when ...").

### Decisions (phase 1)

1. **One `wgr_resource_status_t` for every resource**, where libwgt has one enum per kind
   (`wgt_texture_status_t`, `wgt_font_status_t`, each NONE / PENDING / READY / FAILED).
   The values are the same for every resource, so one type says so, and a binding maps
   it once. Recommended: one.
2. **0 only when there's no room; a bad path is a FAILED handle** (libwgt's rule), where
   create returns 0 for any failure now. A handle can say why (it was logged) and be
   checked like any other. Recommended.

### Phase 1 as built (2026-10-02, branch polled-tasks)

Every resource kind loads on create, one commit each: textures, environments, audio,
fonts, shaders, meshes, then materials and the cleanup. What changed on the way, against
the design above:

- **A resource section, and a resource core.** Status, path and release turned out to be
  the same for every kind, so they're one call each in a new section, `wgr_resource.h`:
  `wgr_resource_get_status`, `wgr_resource_get_path` (the file actually read: a `.ktx`'s
  variant, a fallback, where a redirect found it; libwgt has this per kind) and
  `wgr_resource_release`, replacing seven `wgr_<kind>_release`. Inside, `src/wgr_resource.c`
  does the reference counting, finding a resource by its path, load on create and the
  status for every kind: a record starts with a `wgri_resource_t`, and a module registers
  its pool with its loader and how to free a record. About 60 to 80 lines of near
  copies per module went. A kind whose records another thread reads (audio: the mixer)
  gives its lock, taken around pool changes and statuses. The binding has
  `Resource.getStatus/getPath/release`, and `texture.getStatus()` etc. through `@:using`.
- **The path is an asset path**, as ensure takes, the same file everywhere; a path outside
  the root, or a create before the asset layer runs, is FAILED at once.
- **PENDING is "not there yet", FAILED is "visibly broken"**, rather than the placeholder
  for both (libwgt's choice): a pending texture draws nothing in a sprite and its default
  in a material, a pending environment lights nothing, a pending font draws as the
  built-in one, a sound waits; a failed texture draws the placeholder. On a slow first
  web visit the checker would otherwise have shown everywhere for seconds.
- **A custom material follows the object rule**, its shader being the resource it uses:
  made at once, settings kept by name until the shader is READY (an unknown name logged
  then), not drawn while it loads, flat magenta if it failed. The effect chain is decided
  at the start of each frame from the effects whose shader is ready.
- **A key ensured from an explicit source** (a `fetch_url`) names that file for a later
  create, on desktop too, where a local source is read in place.
- **Getter gaps closed on the way:** `wgr_sound_get_audio`, `wgr_model_get_mesh` (which
  replaces `wgr_model_is_ready`).
- **Ensure loads nothing now**, so its loading by extension, the resources a group held
  for its callback, and `WGR_ASSET_FILE_ONLY` went at the end of phase 1 rather than in
  phase 2.
- **Nothing loads synchronously**, so `examples/loading.c` (reworked around statuses, a
  row per file) and `loadbench` compare loading with the upload budget against without.
- **Tests and tooling:** `tests/unit/test_assets.c` brings the asset layer up and waits;
  hxcpp builds of the binding depend on wgrender's headers (a stale object had called
  the old variadic logger); `check_asset_cache.py`'s replacement sheet is cyan, told
  apart from the placeholder it now expects when a load fails.

### Design: phase 2, callbacks out

### 2. Making a file local is a task of its own

```c
wgr_handle_t wgr_asset_ensure(const char *path, const char *fetch_url, unsigned int flags);

typedef enum {
    WGR_ASSET_TASK_NONE    = 0,  /* not a task */
    WGR_ASSET_TASK_PENDING = 1,
    WGR_ASSET_TASK_DONE    = 2,  /* the file is local (and every file it names) */
    WGR_ASSET_TASK_FAILED  = 3,
} wgr_asset_task_status_t;

wgr_asset_task_status_t wgr_asset_task_get_status(wgr_handle_t task);  /* a file's, or a group's */
const char *wgr_asset_task_get_path(wgr_handle_t task);   /* the local path, once DONE */
float       wgr_asset_task_get_progress(wgr_handle_t task);   /* 0..1, as wgr_asset_get_progress */
bool        wgr_asset_task_destroy(wgr_handle_t task);
```

- `wgr_asset_ensure` (renamed from `ensure_async`: everything is async now) only makes
  files local: prefetching a level, warming the cache, a loading screen. It loads
  nothing (`WGR_ASSET_FILE_ONLY` already went with phase 1); `WGR_ASSET_FORCE_FETCH` and
  `fetch_url` stay.
- A task lives until it's destroyed, so its status and path can be read any number of
  times. Destroying one still pending lets it finish and discards the result (the
  file still lands in the cache).
- Groups stay (`wgr_asset_group_create`, `_add`), over ensure tasks: DONE once every
  member is, FAILED if any failed. Destroying a group destroys its members.
- `wgr_asset_add_task`, `wgr_asset_callback_fn` and `wgr_asset_add_task_result_t` go,
  and with them the resources held out of sight: every loaded resource is one the
  program created.
- A program that wants a loading screen for resources (not just files) reads their
  statuses; one that wants to fetch everything first and load later ensures a group,
  then creates once it's DONE.

### 3. The event bus goes

`wgr_event.h` and `wgr_event.c`: nothing in wgrender emits an event, no example or test
uses it, only the Haxe binding wraps it (`wgr.Event`, which only its own test calls).
Publish/subscribe with untyped payloads is a utility (the org's conventions list `event`
with path, logger and json: wgutils' kind of module), and every language a binding
serves has its own. libwgt has none either.

### 4. The asset layer's two other callbacks

- **`wgr_asset_ping_host(host, timeout_ms)`** returns a task: DONE or FAILED, and
  `wgr_asset_ping_get_milliseconds(task)` for the round trip.
- **The fetcher** is the reverse direction (wgrender asks the *program* to download), so
  polled, the program asks for work:

  ```c
  bool         wgr_asset_set_fetching(bool enabled);  /* a program downloads (desktop) */
  wgr_handle_t wgr_asset_fetch_next(void);            /* a download to do, or 0 */
  const char  *wgr_asset_fetch_get_url(wgr_handle_t request);
  const char  *wgr_asset_fetch_get_dest(wgr_handle_t request);
  bool         wgr_asset_fetch_done(wgr_handle_t request, bool ok);  /* as today, any thread */
  ```

  The contract holds as it is: at most 6 out at once, a download written apart and moved
  into place only on success, the answer taken at the next frame. On the web
  `set_fetching` answers false: the browser is the downloader.

### 5. The loop setters stay, and are the only callbacks

`wgr_set_init`, `wgr_set_tick`, `wgr_set_frame`, `wgr_set_shutdown`: the platform owns the
loop (sokol_app, or the browser), so something must call in; libwgt keeps the same
exception. They are `tools/check_rules.py`'s `TYPES_EXEMPT`, the type rule's one
exemption, with the reason beside them.

### What changes beside the library (phases 1 and 2, as planned)

- **Examples** get simpler: a callback that only created a resource becomes the create
  itself, in init (audio, clay, environment, and the binding's loading, touch and ui).
  `loading.c` shows both ways a loading screen can wait: a group of ensures, and
  resources' statuses. `fetch.c` polls `wgr_asset_fetch_next`.
- **The Haxe binding**: `wgr.Event` goes; resource classes gain `status`; `Asset` keeps a
  callback helper as sugar over polling its open tasks (plumbing, so one name per C call
  holds); five of its six JS omissions go (`wgr_set_*` stay C-only on the guest ABI).
  `Asset.setFetcher(Asset.httpFetcher)` reads as it does now: it stores the Haxe
  function and turns fetching on, and the binding's frame wrapper drains
  `wgr_asset_fetch_next` before the program's frame, handing each request to it.
  `httpFetcher` is unchanged (a thread per download, `fetchDone` from it), the
  `fetchTrampoline` goes, and a cppia script can supply a fetcher, which it can't
  through a C callback.
- **Tests**: load on create (PENDING, then READY or FAILED, never called back inside the
  call; the same handle for the same path while pending; 0 only when full), task
  lifetime, groups, the polled fetcher, each resource's status. `check_asset_cache.py`
  with and without `--manifest`.
- **check_rules.py**: `TYPES_TODO` empties, leaving the loop setters as the only calls
  the type rule exempts.

### Decisions (phase 2)

1. **The fetcher polled** (section 4) rather than kept as the one other callback: a
   download is what a binding wants to do in its own language, and polling is what lets
   a JS guest or a cppia script supply one. Recommended.

### Phase 2 as built (2026-10-02, branch polled-tasks)

One commit a step: the event bus, ensure as a task, ping as a task, the polled fetcher.
What changed on the way, against the design above:

- **A task's status changes only at a tick**, as a resource's does: an ensure, a group or
  a ping is PENDING when the call returns, even when the answer is already known (a
  desktop ping of a directory). A finished task is kept, DONE or FAILED, and no longer
  counts as pending (`wgri_asset_pending_count`, which the web check waits on).
- **Groups:** FAILED once every member has finished and any failed, not at the first
  failure, so a loading screen's progress runs to the end. A member that already
  finished can join (it counts as it finished); a group that has finished takes no
  more; destroying a member while it's pending takes it out of the group's count. An
  empty group is DONE at the next tick. A ping can't join: a group is of files.
- **A ping is a task in the same pool** (`is_ping`), so the eight fixed ping slots went,
  and with them the limit; `wgr_asset_ping_get_milliseconds` reads 0 unless DONE.
- **The fetcher's getter:** `wgr_asset_is_fetching` (the getter rule). A request is the
  task's own handle; `fetch_next` hands them out oldest first, once each, and
  `_get_url` / `_get_dest` are worked out when asked rather than stored, so a task
  record didn't grow. Turning fetching off fails the requests not yet taken, at the next
  tick.
- **The guest ABI lost its `asset` op**, so a guest registers three ops, not four:
  `wgr_guest_asset_load` existed only to stand in for the callback, and a guest now
  polls a task like any program. Every Haxe example's `GuestAbi.register` lost its
  third argument.
- **The binding:** `AssetTask` gained `getStatus`, `getPath`, `getProgress`, `destroy`
  and `getPingMilliseconds`; `wgr.impl.Trampoline` and the user-pointer helpers went
  with the ping, the last callback that crossed. `Asset.setFetcher` is sugar over
  `setFetching`, and the binding's frame op hands each request to the Haxe function
  before the program's frame (`Asset.takeFetches`), on both targets. The callback
  helper the plan kept as sugar (`AssetTask.then`) wasn't rebuilt: nothing used it.
- **Examples:** `loading.c` (and its Haxe port) gained E, fetch first: a group of
  ensures, then the creates, the second way of waiting the plan asked it to show.
  `force_fetch` and `fetch` poll. On js the binding reaches all but the four loop
  setters now.
- **Tests:** `test_assets_ensure` (ensure, tick until finished, read, destroy) and
  `test_assets_tick` / `test_assets_set_fetcher` (a tick, then each request handed to a
  test's downloader, as a program's frame does). `TYPES_TODO` is empty.

*From docs/PLAN-tasks.md.*

## Loading pipeline (background preparation, budgeted GPU upload)

Status: **implemented (2026-09-17).** Decisions 1–5 as recommended; decision 6
changed (see "As built").

### Problem

`wgr_*_create(path)` reads, decodes and uploads on the main thread, so a load during
gameplay stalls a frame for as long as the load takes. The asset layer's callback
already runs on the main thread, so running the create from a callback doesn't
help.

### Measurements (2026-09-16)

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

### Design

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

### Web

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

### Public API additions

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

### Decisions

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

### As built

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

### Order and verification

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

*From docs/PLAN-pipeline.md.*

## Materials and shaders

Status: **phase 1 implemented (2026-09-16)** (built-in materials for models; see
`include/wgr_material.h` and `examples/materials.c`) and **phase 2 (2026-09-20)**
(custom shaders; see "Phase 2 as built", `include/wgr_shader.h`, `shaders/wgr.glsl` and
`examples/shaders.c`). Phase 3a (2026-09-20): custom shaders on sprites; see "Phase 3a
as built". Phase 3b (2026-09-21): lit 3D sprites; see "Phase 3b as built". That
finishes the plan; what's left is recorded in [TASKS.md](TASKS.md) (particles and 2D
shapes on custom shaders, parameter arrays and matrices, lightmaps, shadows).
Decisions 1–5 below were accepted as proposed; "Phase 1 as built" records where the
implementation refined the proposal.
Roadmap item 1. Builds on lighting ([PLAN-lighting.md](#lighting-light-objects-per-scene-lighting)), render
passes, and the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)).

### Where we are

- **Models:** one built-in lit shader (static + skinned). From glTF materials it
  uses only the base color factor and texture, `alphaMode`/`alphaCutoff` and
  `doubleSided`. Normal, metallic-roughness, occlusion and emissive maps are
  ignored. Lighting is diffuse only.
- **Shapes and sprites:** drawn with sokol_gl's fixed shader, unlit, color x
  texture.
- **Users can't change how anything is shaded:** no material API, no custom
  shaders. Anything beyond tint needs engine changes.

### Goals

- A **Material** resource: shared, reference-counted, created in code or loaded
  from glTF, and assigned to models (later shapes and sprites).
- **Built-in shading models** covering what glTF assets expect, with correct
  lighting from scene lights.
- **Custom shaders** without breaking the public API rules: handles, numbers and
  strings only, and no sokol or backend types.
- One material system for every drawable eventually; models first.

### Non-goals (this plan)

Shadows, image-based lighting / environment maps, post-processing, a node-based
shader editor, runtime shader compilation from GLSL source (sokol-shdc is an
offline tool), compute shaders.

### Proposed design

#### Material resource

```c
/* include/wgr_material.h */
typedef enum {
    WGR_MATERIAL_UNLIT = 0, /* base color x texture x tint; ignores lights */
    WGR_MATERIAL_PBR   = 1, /* glTF metallic-roughness, lit by scene lights */
} wgr_material_model_t;

wgr_handle_t wgr_material_create(wgr_material_model_t model);  /* or a custom shader, below */
void        wgr_material_release(wgr_handle_t material);

/* Parameters by name. Built-in models define their names (below); custom shaders
 * expose their own uniform names. Unknown names return false. */
bool wgr_material_set_float(wgr_handle_t material, const char *name, float value);
bool wgr_material_set_vec2(wgr_handle_t material, const char *name, float x, float y);
bool wgr_material_set_vec3(wgr_handle_t material, const char *name, float x, float y, float z);
bool wgr_material_set_vec4(wgr_handle_t material, const char *name, float x, float y, float z, float w);
bool wgr_material_set_color(wgr_handle_t material, const char *name, wgr_handle_t color);
bool wgr_material_set_texture(wgr_handle_t material, const char *name, wgr_handle_t texture);

/* Blending and culling, per material (from glTF when loaded from a mesh). */
bool wgr_material_set_alpha_mode(wgr_handle_t material, wgr_alpha_mode_t mode, float cutoff);
bool wgr_material_set_double_sided(wgr_handle_t material, bool double_sided);
```

Built-in PBR parameter names follow glTF: `base_color` (vec4/color),
`base_color_texture`, `metallic`, `roughness`, `metallic_roughness_texture`,
`normal_texture`, `normal_scale`, `occlusion_texture`, `occlusion_strength`,
`emissive` (vec3), `emissive_texture`.

#### Models and materials

- A Mesh loaded from glTF creates one Material per glTF material (shared by the
  primitives that use it). `wgr_mesh_get_material(mesh, index)` exposes them.
- `wgr_model_set_material(model, primitive_index, material)` overrides a primitive
  on one model (`-1` = all primitives); the mesh's materials are the default.
  Overrides are per model, so two models can share a mesh and look different.
- Tint stays a per-model multiplier on top of the material.

#### Custom shaders

```c
wgr_handle_t wgr_shader_create(const char *path);  /* resource: a compiled shader package */
void        wgr_shader_destroy(wgr_handle_t shader);
wgr_handle_t wgr_material_create_custom(wgr_handle_t shader);
```

- Authors write annotated GLSL like `src/shaders/wgr_model.glsl`, against a small
  documented interface (vertex inputs, a `wgr_frame` block with camera matrices and
  time, a `wgr_object` block with model matrix and tint, and optionally the lights
  block).
- A build step, `tools/shaderpack` (wrapping `sokol-shdc -f bare_yaml`), compiles it
  for GL, WebGL2 and WebGPU and bundles sources plus reflection into one
  `.wgrshader` file.
- `wgr_shader_create(path)` loads that file like any other asset (so it works with
  `wgr_asset_ensure_async`), picks the running backend's source, and uses the
  reflection to map parameter names to uniform offsets and texture slots.
- Handle-only and backend-free: no `sg_shader_desc`, no pointers.

#### Pipelines and passes

- Pipelines are cached by (shader, vertex layout static/skinned, blend,
  double-sided). Materials pick the pass as today: opaque/mask first, blend sorted.
- Uniform data is packed per material once when parameters change, not per draw.

#### Shading (built-in PBR)

- glTF metallic-roughness BRDF (Lambert diffuse + GGX specular), normal mapping
  (needs vertex tangents: from glTF, or generated at load), occlusion, emissive.
- Uses the existing per-model light selection (8 lights).
- Without environment lighting, metals look dark apart from direct highlights;
  documented, and a reason ambient remains per scene.
- Color space: glTF colors and base color textures are sRGB, so decode to linear
  for lighting and encode at output. Today's shader skips this, so this is also a
  correctness fix; lit results will look different.

### Phasing

1. **Material resource + UNLIT/PBR built-ins for models**: glTF materials become
   Material resources; PBR shading with normal/metallic-roughness/occlusion/emissive
   maps; sRGB-correct; model overrides. Verify against Khronos glTF sample models.
2. **Custom shaders**: shader package tool, `wgr_shader_create`, custom materials,
   an example with an animated custom shader, web backends.
3. **Shapes and sprites on materials**: move them off sokol_gl's fixed shader onto
   material-driven pipelines. Ties into the batched renderer and particles.

### Phase 1 as built

- **Overrides are per material slot, not per primitive.** A mesh's material slots
  are its glTF materials (plus glTF's default material when a primitive has none).
  `wgr_model_set_material(model, slot, material)` replaces a slot on one model (-1 =
  every slot, 0 = back to the mesh's). Swapping "the body material" is what users
  want; primitive indices are an export detail. Up to 32 slots can be overridden.
  Overrides can be set before the mesh loads and stay when the mesh changes.
- **API names:** `wgr_material_shading_t` (`WGR_MATERIAL_PBR`, `WGR_MATERIAL_UNLIT`)
  instead of "model", to avoid confusion with `wgr_model`. Getters for shading, alpha
  mode and double-sided; `wgr_mesh_get_material_count/get_material`,
  `wgr_model_get_material`. glTF defaults for new materials (metallic 1, roughness 1).
- **Lighting follows glTF exactly**, including the 1/pi in the Lambert term. Light
  intensities that looked right before need about 3x (pi) now; the examples were
  updated. Light and ambient colors are sRGB and converted to linear, like material
  colors. Model tint is sRGB too.
- **Ambient** (no environment lighting yet): ambient x (diffuse color + F0) x
  occlusion, so metals aren't black away from direct highlights.
- **Tangents:** from glTF when present, otherwise generated per vertex from
  positions and texture coordinates (glTF convention: bitangent toward decreasing
  v). Verified against Khronos NormalTangentTest and NormalTangentMirrorTest, with
  and without the file's tangents (generated tangents matched the file's on all
  2770 vertices).
- **Fixed on the way:** normals now use the inverse transpose of the model matrix
  (non-uniform scale was wrong); double-sided back faces are lit from their side.
- **Textures:** glTF images become unnamed texture resources shared between the
  mesh's materials; materials hold references. Picking uses the material the model
  actually draws with (override included), and the base color texture's alpha.
- **glTF coverage (follow-up the same day):** texture coordinate set 1, texture
  transforms (KHR_texture_transform, also settable by name), vertex colors,
  per-texture samplers (wrap, filter), mipmaps for all textures, and `.gltf` files
  with separate buffers/images or `data:` URIs. The asset layer ensures a glTF
  file's referenced files through a per-extension dependency lister (registered by
  wgr_model), so the public ensure-then-create flow is unchanged and works on web.
  Verified against the Khronos TextureTransformTest, MultiUVTest, VertexColorTest,
  TextureSettingsTest and BoxTextured models.
- **Missing images:** a glTF image that is missing (optional dependency), broken or
  in an unsupported format doesn't fail the model. Base color and emissive slots get
  the placeholder texture (`wgr_texture_get/set_placeholder`, default a magenta
  checker); normal, metallic-roughness and occlusion slots stay empty so lighting
  isn't distorted. Missing buffers still fail the ensure.
- **Not yet:** environment lighting (wanted next), tone mapping, and others tracked
  in TASKS.md under "Materials: glTF coverage".

### Phase 2 as built

- **Fragment shaders plus an optional vertex hook**, not whole vertex shaders.
  libwgrender keeps its vertex shaders (static and skinned), so custom shaders work on
  animated models without writing skinning. The hook,
  `void wgr_vertex(inout vec3 position, inout vec3 normal)`, moves vertices in object
  space before skinning (waves, wind) and has its own parameters.
- **The interface is one file, `shaders/wgr.glsl`.** A fragment shader includes
  `wgr_surface`: world position, normal, tangent, both texture coordinate sets and
  vertex color; `wgr_time()`, `wgr_camera_position()`, `wgr_ambient()`, the scene's
  lights (`wgr_light_count()`, `wgr_light(i, pos, out to_light)`, with the same falloff
  as built-in materials), sRGB helpers, and `wgr_output(color, alpha)`, which applies
  the model's tint, the MASK cutoff, exposure and tone mapping and encodes sRGB, so a
  custom material fades, masks and tone-maps like a built-in one. The scene's
  environment (2026-09-20): `wgr_environment_diffuse(n)`, `wgr_environment_specular(n, v,
  roughness)` and `wgr_environment_brdf(n_dot_v, roughness)`, the same split-sum pieces
  built-in materials use, with `wgr_environment_intensity()` 0 (and the functions
  black) without one.
- **Skinning:** a skinned model's joint matrices come from the frame's joint texture
  (libwgrender's slot 12, uploaded once a frame by the model module); the object block
  carries where this model's start. Before that they were 128 matrices in the object
  block, per draw (docs/TASKS.md).
- **Bindings:** uniform block 0 is libwgrender's per-object block (matrices, time, joint base),
  1 its per-draw fragment block (`wgr_frame`: camera, time, tint, ambient, lights,
  output settings, the environment's intensity, rotation and irradiance), 2 the
  shader's fragment parameters, 3 the vertex hook's. The shader's textures are
  texture2D in the fragment shader, bindings 0-7, each paired with a sampler; 8 and 9
  are libwgrender's (the environment cubemap and BRDF table), bound only if used. The file
  format has a version: a change to `wgr_frame` (version 2 added the environment) makes
  older files refused ("rebuild it") rather than drawn wrongly. Vertex inputs have fixed locations matching libwgrender's vertex buffers (without
  them sokol-shdc numbered the skinned shader's inputs in declaration order).
- **`tools/shaderpack.py`** puts `shaders/wgr.glsl` in front of the file, adds the two
  vertex shaders (with the hook or an empty one), compiles with sokol-shdc
  (`-f bare_yaml`: sources plus a reflection file) for glsl410, glsl300es and wgsl,
  and writes one text `.wgrshader`: parameters (name, type, block, std140 offset; the
  tool computes the offsets, which sokol-shdc's reflection doesn't give, and checks
  them against its block sizes), texture names, then per backend and program the
  vertex attributes, uniform blocks, views, samplers and texture-sampler pairs, and
  the sources. Errors in the user's file are reported at its own line numbers. The
  output is deterministic. About 50 KB per shader (three backends, two programs).
- **Runtime (`src/wgr_shader.c`, an optional module):** a resource like textures:
  deduplicated by path, reference counted, loaded through `wgr_asset` (read on a
  worker; parsed and made on the main thread), so it downloads on the web.
  Finishing picks the running backend's sources (the dummy backend takes the GL
  description) and builds `sg_shader_desc` from the file. Materials and models reach
  it through hooks (`wgri_shader_hooks`), so programs that never load a shader don't
  link it.
- **Materials:** `wgr_material_create_custom(shader)`; shading reads
  `WGR_MATERIAL_CUSTOM`, which create and set_shading refuse. The existing setters
  find the shader's parameters by name and type (float, int, vec2, vec3, vec4;
  `set_color` converts sRGB to linear) and write them into the material's copy of
  the two blocks; textures and their sampling by the shader's texture names (a
  texture not set is white). Built-in names don't apply. Picking treats custom
  surfaces as solid everywhere: libwgrender can't know where a shader discards.
- **Drawing:** per shader, pipelines for (static or skinned, blended, double-sided)
  made on first use and freed with the shader; blocks the compiler dropped because
  the shader doesn't use them aren't applied.
- **Not yet:** arrays and matrices as parameters, shaders for sprites and shapes
  (phase 3), D3D11/Metal sources.
- Checked on desktop GL, WebGL2 and WebGPU (`examples/shaders.c`: toon on the
  animated gumshoe, a dissolve with a noise texture, waves from a vertex hook), and
  headless (the dummy backend validates every uniform size and binding).

### Phase 3a as built: custom shaders on sprites

- **Scope decided:** sprites (2D and 3D) take materials; shapes stay unlit (debug,
  gizmos, UI): lit geometry is a generated mesh (`wgr_mesh_create_*`) on a model. Text
  and particles are unchanged for now.
- **One shader for models and sprites.** `tools/shaderpack.py` builds two more
  programs from libwgrender's sprite vertex shaders in `shaders/wgr.glsl`: per-instance
  attributes, and sprites read from a texture where the backend can't draw from a base
  instance (WebGL2), as libwgrender's own sprite shader does. `.wgrshader` format 3; older
  files are refused. The vertex hook is models only.
- **What a fragment shader sees on a sprite:** the quad's world position (a 2D
  sprite's: its pixel), normal and tangent (its facing and right), `wgr_uv0` its
  texture region, `wgr_uv1` 0..1 across the quad (each slice of a nine-slice sprite),
  `wgr_color` its tint in linear. `wgr_sprite_color()` is the sprite's texture at its
  region times its tint; the texture is also `wgr_sprite_tex` / `wgr_sprite_smp` (slot
  10) for shaders that sample around (an outline). On a model it's a white texture,
  so `wgr_sprite_color()` is the vertex color and a shader works on both.
- **Alpha:** the sprite's alpha mode picks the pipeline as before, and reaches the
  shader as `wgr_sprite_alpha` (a cutoff, opaque, or as is), applied in `wgr_output`.
  No tone mapping on sprites, as today.
- **API:** `wgr_sprite3d_set_material` / `wgr_sprite2d_set_material` (+ `_get_material`):
  a custom material, or 0 for libwgrender's sprite shader; built-in materials are refused
  until 3b. The sprite holds a reference.
- **Batching:** the material joins texture, alpha mode, camera and clip in what a batch
  shares, and in how a scene's unordered sprites are grouped. A custom batch applies
  its whole state (the next batch too); libwgrender's blocks: 0 the sprite view (camera
  axes, time), 1 `wgr_frame` (camera, time; no lights yet), 4 the batch's first sprite
  (read from a texture); the shader module gives fallback textures (white, a black
  cube) for libwgrender's slots with nothing to show.
- **On the way:** the material texture samplers moved from the model module into the
  texture module (`wgri_texture_sampler`), shared by models and sprites; `wgr_frame`'s C
  layout is shared (`wgri_shader_frame_t`); GLSL names in a `.wgrshader` can be longer
  than parameter names (a texture-sampler pair joins two).
- Checked: desktop GL, WebGL2 (sprites read from a texture), WebGPU; unit tests on
  both sprite paths (`-DWGR_SPRITES_PULLED`); `examples/shaders.c` outlines and flashes a
  logo sprite in the world and on screen with one material.

### Phase 3b as built: lit 3D sprites

- **One shading path for models and sprites.** The surface shading moved out of
  `src/shaders/wgr_model.glsl` into `src/shaders/wgr_pbr.glsl` (`wgr_pbr_color`,
  `wgr_pbr_surface`, `wgr_pbr_main`), included by both. The sprite shader gained two
  programs, `quad_lit` and `quad_lit_pulled` (the WebGL2 path that reads sprites from
  a texture), whose vertex stage builds the quad as before and then writes the
  varyings the shared fragment stage expects.
- **What a built-in material does to a sprite:** the sprite's texture is the base
  color and its tint the vertex color, multiplied by the material's `base_color`; the
  material's normal, metallic-roughness, occlusion and emissive maps, their texture
  transforms, and `metallic` / `roughness` / `occlusion_strength` / `emissive` apply
  as on a model. `WGR_MATERIAL_UNLIT` takes the shader's unlit branch (base color,
  tone mapped), so an unlit material is still useful for the material's own maps and
  tint. The quad's normal is its facing and its tangent its right edge, so a normal
  map on a billboard lights as a flat card turned to the camera.
- **Alpha:** unchanged from 3a — the sprite's alpha mode picks the pipeline and
  reaches the shader as a cutoff/opaque/as-is flag, now combined with the material's
  own `alphaCutoff` (`max` of the two) in the shared fragment stage.
- **Lights are chosen once per batch, not per sprite.** A batch tracks the bounds of
  the sprites in it and calls `wgri_light_select` on them, as a model draw does for its
  own bounds. The lighting environment joins texture, material, alpha mode, camera
  and clip in what a batch shares, so sprites drawn under different scene lighting
  don't merge. Cost, measured in Chromium (sprites in a grid, CPU per frame): 4,000
  lit 1.79 ms vs 1.77 unlit; 16,000 lit 2.52 vs 1.89 — the shading is on the GPU and
  the extra CPU is the per-batch uniform blocks.
- **The sprite module doesn't link the environment module.** `wgri_environment_hooks`
  (one `get_binding`, defined in `wgr_render.c`, set by the environment module when
  it's linked) hands out the prefiltered cube, BRDF table and SH coefficients;
  without it the sprite batch binds a black cube and zero intensity, so a program
  that never touches environments doesn't pull one in (`make check`).
- **Custom sprite shaders get the same lighting.** A custom sprite draw now fills
  `wgr_frame` with the batch's lights and environment, so `wgr_environment_diffuse` /
  `_specular` / `_brdf` and the light list work the same in a sprite shader as in a
  model shader. `.wgrshader` format is unchanged by 3b (format 4 came from the
  skinning work in the same series).
- **API:** `wgr_sprite3d_set_material` now accepts a built-in material as well as a
  custom one; `wgr_sprite2d_set_material` stays custom-only (screen-space sprites have
  no place in a lit scene). No new functions.
- Checked: desktop GL, WebGL2 (both sprite paths) and WebGPU; unit tests cover a
  built-in material on a sprite forming its own batch; `examples/lights.c` puts four
  lit billboards in the scene (verified lit by zeroing the sun and ambient: far
  sprites go black, the ones by the point light stay cyan).

### Decisions

1. **Built-in shading:** PBR metallic-roughness (glTF-native, what assets expect)
   plus unlit, as proposed. Or a simpler Blinn-Phong first?
2. **Material = resource** (shared, from glTF or code) with **per-model,
   per-primitive overrides**, as proposed?
3. **Custom shaders via a precompiled shader package** (`.wgrshader` from
   sokol-shdc, loaded by path, parameters by name), as proposed? The alternative,
   runtime GLSL compilation, isn't available cross-backend.
4. **sRGB-correct lighting** now, accepting that lit scenes will look different
   (more correct)?
5. **Phasing:** models first (phases 1–2), shapes/sprites later with the batched
   renderer?

### Verification (per phase)

- Unit tests: parameter packing by name (reflection offsets, types, unknown names),
  material refcounting and per-model overrides, pipeline cache keys, sRGB
  conversions, BRDF helper values against reference numbers.
- Visual: Khronos glTF sample models (e.g. MetalRoughSpheres, NormalTangentTest,
  AlphaBlendModeTest) screenshots on desktop and web, compared with the Khronos
  reference renders.
- `make test`, `make smoke`, `make check`, `make webcheck` (WebGL2 + WebGPU).

*From docs/PLAN-materials.md.*

## Model instancing

Status: **built** (2026-09-21), phases 1–4. Phase 5 (per-instance light sets,
transparent runs, a persistent buffer) is open and not obviously needed yet. Decisions below are answered:
automatic grouping with no new API (an explicit instanced handle only if a measured case
ever needs one), opaque models may be reordered, and custom shaders follow in phase 4 of
the same release.
Builds on the model draw queue (`src/wgr_model.c`), the joint texture it already uses for
skinning, and the sprite batch's instancing (`src/wgr_sprite_batch.c`), which is the
closest thing libwgrender already has to this.

### Why

A model is one draw call, always. Measured with `make shadowbench DESKTOP=1` on an RTX
4080: 4000 lit models cost about 5 ms a frame and roughly 95% of it is CPU submission —
about 1.2 microseconds a model, whatever the model is. Sharing one mesh between them
saves 12% (1.13 against 1.30 microseconds), so the cost is not buffer churn: it is the
per-model uniform uploads, the bindings and the draw call itself.

Now that culling has taken the models nobody can see out of the frame, this is the
largest cost left in every row of that benchmark. For comparison, three.js draws 16k
instanced meshes in about 2.3 ms.

### Where we are

Per model placement, `draw_primitive` does all of:

- a pipeline check (skinned × blended × double-sided);
- `sg_apply_uniforms(vs_params)` — mvp, model, normal matrix (192 bytes);
- `sg_apply_uniforms(fs_params)` — material **and the placement's tint**, plus five UV
  matrices, about 200 bytes, uploaded for every model with no caching;
- `fs_scene` and `fs_lights` only when their contents change (a `memcmp` against the
  last one), so those already skip most of the time;
- `sg_apply_bindings` with ten views and ten samplers;
- `sg_draw(0, index_count, 1)`.

The queue entry (`wgr_model_draw_t`) already holds everything an instance needs: `mvp`,
`model_mat`, `normal_mat`, `tint`, `light_env`, the selected `lights`, `joint_base` and
(since culling) the world bounds.

### Proposed design

#### Per-instance data lives in a texture, not in attributes

The shape that fits libwgrender is the one skinning already uses: a per-frame RGBA32F data
texture, written once a frame, read in the vertex shader with `texelFetch` at
`gl_InstanceIndex + base`. One record per placement:

```
0..2   model matrix, three affine rows
3..5   normal matrix, three rows
6      tint (rgba, sRGB like every color handle)
7      extras: joint_base, light_count, receives_shadow, spare
```

Eight texels, 128 bytes a placement — 500 KB a frame at 4000 models, written into a
mapped buffer exactly as the joint texture is today.

The alternative is per-instance vertex attributes, which is what the sprite batch uses.
It costs no texture fetch, but a skinned model already spends 8 of sokol's 16 attribute
slots, and the record above needs 8 more: exactly at the limit, with nothing left for
anything later, and every custom vertex shader would have to declare all of them. The
texture costs one texture binding and one sampler (libwgrender currently owns 8–9 of the 12
sampler slots, so there is room) and keeps the custom-shader contract to a single
include block.

#### Every model draw becomes an instanced draw

Rather than a second, parallel "instanced" path, a placement's per-model uniforms move
into the record and *every* stock model draw becomes `sg_draw(0, n, count)` with
`count >= 1`. That removes `vs_params` entirely, takes the tint out of `fs_params` (it
arrives as a varying from the vertex stage), and leaves `fs_params` holding nothing but
material state — so it can take the same `memcmp` cache `fs_scene` and `fs_lights`
already have. Consecutive models sharing a material stop re-uploading it even before
any grouping happens.

#### Grouping, and what may be reordered

Two placements can share a draw when everything outside the record matches: pass,
pipeline (skinned / blended / double-sided), primitive buffers, material, lighting
environment, the selected light set, and the shadow binding. Those keys are hashed once
per item; a run of equal keys becomes one `sg_draw` with that many instances.

Runs only exist if equal items are adjacent. Opaque parts are already drawn in an
explicitly unordered region (`begin_unordered` / `end_unordered` in `wgr_scene`, where
sprites group by texture), so the items inside one command's range get sorted by that
key first. Transparent parts keep their back-to-front order and only group where equal
items are already neighbours.

#### What this does not cover yet

A placement's light selection is per placement, so two models under different lights
can't share a draw until the light indices move into the record and the fragment stage
reads the environment's full light array instead of the eight selected ones. That is a
later phase; in the common cases (one sun, or a few lights over a group of objects) the
selected sets are identical anyway.

### Decisions

1. **Automatic, or opt-in?** Grouping can be invisible — no public API, libwgrender batches
   what it can — or explicit, e.g. an instanced-model handle the caller fills. Automatic
   keeps the API surface and means existing programs get faster with no change; explicit
   would let a caller promise that N placements share everything and skip the per-frame
   key building. Recommend: automatic. libwgrender's job is to make the obvious code fast.
2. **Per-instance data: texture or attributes?** As above. Recommend: texture, for the
   attribute budget and the custom-shader contract.
3. **May opaque models be reordered?** Sorting a command's opaque items by pipeline,
   material and mesh is what makes runs exist. The region is already declared unordered
   and opaque parts are depth-tested, so nothing observable changes — but it is a change
   in what the renderer is allowed to do, so it is a decision, not an assumption.
   Recommend: yes.
4. **Does tint stop being a material uniform?** Moving the tint into the record changes
   `fs_params` to material-only and makes the tint a varying. Custom shaders currently
   see the tint folded into `u_base_color`; after this they would read it from
   `wgr_color`-style varying instead. Recommend: yes, and phase the custom-shader side
   (5) so nothing breaks in between.
5. **When do custom shaders follow?** Stock PBR can instance without touching
   `shaders/wgr.glsl`; custom material shaders need the same include block, which bumps
   the `.wgrshader` format (7 → 8) and repacks `examples/assets/shaders/`. Recommend: in
   the same release, one phase later, so a custom shader is never the slow path for long.

### Phase 1 as built

`wgr_model` keeps a second frame data texture beside the joint one: RGBA32F, eight texels
a placement, 128 records a row, written once in `wgri_model_flush` before any pass. A
record holds three rows of the model matrix, three of its inverse transpose, the tint
(converted to linear on the way in) and the skin base. `vs_params` is now the camera's
view-projection and the draw's first record; `vs_skin_params` is the same, since the
joint base moved into the record. Every stock model draw is `sg_draw(0, n, 1)` with its
own record — one instance each, until phase 2 groups them.

Two things fell out of it. The tint left `fs_params`: the vertex stage multiplies it
into `v_color`, and because the fragment stage already computed
`base_sample * u_base_color * v_color`, that is the same arithmetic in a different
place — no fragment change, and custom shaders that read `wgr_color` keep working. What
is left in `fs_params` is material state only, which is what lets phase 2 group.

Measured: nothing moved. 4000 models with no shadows 5.28 ms against a 4.96–5.57 band
over five runs, a casting sun 5.15, two lights 5.33 — the per-placement uniform upload
(192 bytes a model) is gone and the vertex shader's texture fetch replaced it, which is
a wash. Phase 1 is not meant to be faster; it is meant to put the data where phase 2 can
draw thousands of placements from one call.

### Phase 2 as built

An item remembers which *unordered region* it was submitted in — `wgr_scene` already
declares those around the opaque part of a layer, for sprites, and now tells `wgr_model`
too through two scene hooks. Inside one region the items may be drawn in any order, so
`wgri_model_flush` sorts each region by a hash of everything a draw has to set outside the
instance record: material, primitive buffers, pipeline (skinned / blended / double
sided), pass, lighting environment, the selected light set, whether the model receives
shadows, and the camera. See-through parts are marked region −1 and never move, so the
back-to-front order stands.

Records are written per item in that sorted order, so a run of equal items occupies
consecutive records. `wgri_model_draw_items` then walks the run, comparing each item to
the first *exactly* (the hash only decides the sort; a collision costs a split, never a
wrong batch), and issues one `sg_draw` with that many instances. A material with a
custom shader never joins a run — that path has its own uniforms per placement until
phase 4.

#### Measured

`make shadowbench DESKTOP=1` gained a "shared" case: one mesh, one material, N
placements — a forest. Against "sun 1024", the same scene with a material per model,
both from the same run, at 4000 models:

| 4000 models   | a material each | one shared material |
|---------------|----------------:|--------------------:|
| submit        |         4.71 ms |             0.60 ms |
| CPU total     |         5.89 ms |             1.76 ms |
| frame         |         6.12 ms |             3.20 ms |

Submission is what collapsed: 4000 draws became a handful. A scene where every model
has its own material cannot batch and measures exactly as it did. The scene walk itself
— 1.15 ms for 4000 members, culling included — is now the largest CPU cost in the frame,
and the frame is GPU-bound again.

#### Reviewed (2026-09-21)

A second pass over phases 1 and 2, looking for what the unit tests (dummy backend: no
pixels) could not show.

- **A draw with more than one instance had never been seen.** Every example had unique
  materials, so no group larger than one had rendered anywhere. `examples/instancing.c`
  now exists for that: 400 cubes sharing a mesh and a material with a tint and transform
  each, six gumshoes sharing one skinned mesh at different points of the walk, and five
  see-through copies over the field. Screenshotted on WebGL2 and WebGPU: per-instance
  tint, transform and joints all right, the translucent ones blended in order, and the
  two backends identical. It stays in `make smoke` and `make webcheck` for good.
- **sokol's instanced path.** The GL backend switches to `glDrawElementsInstanced` when
  `num_instances > 1` whatever the pipeline's vertex layout, and WebGPU always draws
  instanced, so a plain layout with `gl_InstanceIndex` is enough.
- **Sorting never crosses a pass.** A region is declared inside one scene draw, so it
  can't span passes; the sort now also breaks a run where the pass changes, because the
  commands that replay an item range are per pass and a stray item across one would be
  drawn in the wrong pass.
- **Transparent parts.** They are never sorted, and when equal ones happen to be
  neighbours they go up as one draw whose instances are rasterised in record order —
  which is the back-to-front order they were submitted in, so blending is unchanged.
- **Custom shaders** keep their own per-placement tint uniform (`frame.tint`), untouched
  by the tint moving out of `fs_params` on the stock path.
- **Found on the way, pre-existing:** a model with both an opaque and a see-through part
  gets two placements a frame (one per pass it appears in) and a skinned one uploads its
  joint matrices twice. Harmless, and cheap, but it is in TASKS.
- **Native GL and a phone**, once the machine was awake. The desktop build (GL core,
  RTX 4080, 122 FPS) captured through the COSMIC portal — `scrot` on the Xwayland root
  returns black under Wayland, and desktop examples must be run from the repo root or the
  asset base isn't found — and the WebGL2 and WebGPU builds on a Pixel 9 Pro XL (Mali,
  Chrome, 60 FPS) over `adb reverse`. All four pictures match the two desktop browsers:
  six walkers in six poses, the tinted field, the glass in order.

### Phase 3 as built

The depth pass now reads each caster's placement from the same records the shading pass
does, so it batches the same way. The instance block moved out of `wgr_model.glsl` into
`src/shaders/wgr_instance.glsl`, which both shaders `@include`; `vs_depth_params` became
the light's view-projection and the draw's first record, and the joint base comes out of
the record, exactly as on the shading side.

It batches on less than the camera pass, because it sets less: the buffers it binds, the
material it alpha-tests with, and the pipeline. Everything else about a caster is in its
record. The items are already sorted by the camera pass's finer key, so casters that
group there are adjacent here too; a run is cut wherever an item isn't drawn into this
map — a non-caster, a see-through part, one outside the light's fit — which keeps every
run's records contiguous.

#### Measured

`shadowbench` gained "wide, each" and "wide, shared": the sun's shadows reach the whole
grid instead of 40 units, so every model is drawn into the map as well as to the screen.
That is the case this phase is for — with the default 40, culling has already taken most
casters out of the map, which is why the existing rows don't move.

At 4000 models, one run, both passes batching against neither:

| 4000 models, everything casts | a material each | one shared material |
|-------------------------------|----------------:|--------------------:|
| submit                        |         6.38 ms |             0.60 ms |
| frame                         |         8.01 ms |             3.20 ms |

To separate this phase from phase 2, the same build with `same_depth_group` forced to
false: "wide, shared" at 4000 measured 2.10 ms of submission and a 5.24 ms frame, against
0.60 and 3.20 with it on. So the depth pass's own batching is worth about 1.5 ms there —
roughly what the camera pass was worth, which stands to reason: it is the same 4000
placements drawn a second time.

### Phase 4 as built

Custom material shaders read their placement the same way. `shaders/wgr.glsl` gained an
`wgr_vs_instance` block — the same record layout, at view slot 14 — and the two model
vertex stages use it: `wgr_object` is now the camera's view-projection plus the draw's
first record, and the joint base comes out of the record, so `wgr_skinned_object` is the
same block. Sampler slots stop at 11, so the instance texture and the joint texture
share one nonfiltering sampler; both are nearest and clamped, and sokol pairs one
sampler with as many textures as it likes.

The tint needed care. On the stock path it folds into `v_color`, because the fragment
stage multiplies by it anyway — but a custom shader's `wgr_output()` applies `wgr_tint`
from the frame block, and a shader that ignores `wgr_color` would have silently lost its
tint. So `wgr_tint` became a **varying** instead: the model stages write it from the
record, sprites write white (their tint has always been in `wgr_color`), and every
existing shader behaves exactly as it did. `wgr_frame` loses the field, which is part of
why the format moves.

`.wgrshader` format 7 → 8; `make example-shaders` repacks the six in
`examples/assets/shaders/`. An older pack is refused with a clear message rather than
drawn wrongly, as it was for format 7.

With that, `same_group` no longer excludes custom materials: two placements sharing a
custom material share its shader and its parameters, which is all a custom draw sets
besides the record.

### Phasing

1. The instance texture and the record; every stock model draw becomes an instanced draw
   of one; `fs_params` loses the tint. No grouping yet — this phase must not be slower,
   and proves the record is right. **Built.**
2. The group key, sorting a command's opaque items by it, and runs of equal keys drawn
   as one `sg_draw`. This is the phase the numbers come from. **Built.**
3. The depth pass (shadows) instanced the same way — it is the same queue, and it is
   already the second-largest consumer of it. **Built.**
4. Custom material shaders: the `wgr.glsl` include block, format bump, repack. **Built.**
5. Open: per-instance light sets, so grouping isn't constrained by light
   selection; transparent runs; a persistent instance buffer for placements that don't
   move.

### Verification

- Unit tests: a record's layout round-trips (build the queue, read back what the texture
  would hold); two placements sharing everything produce one draw and two instances,
  while a difference in each key (material, mesh, light set, pipeline, pass) splits them;
  tint arrives per instance.
- `shadowbench` and `spritebench`: 4000 models sharing a mesh and material should fall
  from ~5 ms toward the cost of a few draws; nothing should get slower, in particular the
  4000-distinct-materials case, which cannot group at all.
- A new bench case or example with many copies of one model (the forest), since that is
  what this is for.
- Visual: `examples/instancing.c` — a batched field, skinned copies, see-through copies,
  and every cube's shadow under it from a single depth draw —
  on both browser backends; `model.c`, `lights.c`, `shadows.c`, `materials.c` unchanged;
  tints and per-model materials still right.
- `make verify`, `make webcheck` (both backends), Wine.

*From docs/PLAN-instancing.md.*

## Remaining librl parity

Status: **implemented (2026-09-16).** Decisions 1–7 accepted as recommended.
`make parity`: 95%, 10 todos left, all deferred on purpose (window and monitors,
batch ensure, host ping). See "As built".

`make parity` lists 58 librl functions still marked todo (see tools/parity.map).
Parity is functional, not 1:1 (AGENTS.md): each group below says what librl did,
what libwgrender already has, and the proposed libwgrender design. Several librl functions
collapse into one, and a few are proposed as dropped with a reason.

### 1. Picking (13 todos)

librl: `rl_pick_model/shape/sprite3d/text3d(camera, object, x, y)` (one per kind),
pickable flags on models, sprites, texts and shapes, and pick statistics.
libwgrender: `wgr_scene_pick` over a scene; per-kind pick functions exist internally
(a registry by handle kind); pickable flags only on shapes and sprite2d.

```c
/* include/wgr_pick.h (new public header) */
/* Pick one object at screen point (x, y), logical pixels, through `camera` (0 =
 * active). Works for every pickable kind: model, shape, sprite2d, sprite3d, text3d. */
wgr_pick_result_t wgr_pick_object(wgr_handle_t object, wgr_handle_t camera, float x, float y);

typedef struct { int broadphase_tests, broadphase_rejects, narrowphase_tests, narrowphase_hits; } wgr_pick_stats_t;
wgr_pick_stats_t wgr_pick_get_stats(void);   /* since the last reset */
void            wgr_pick_reset_stats(void);

/* per kind, default true; non-pickable objects are skipped by every pick */
bool wgr_model_set_pickable(wgr_handle_t model, bool pickable);    bool wgr_model_is_pickable(wgr_handle_t model);
bool wgr_sprite3d_set_pickable(...);  bool wgr_sprite3d_is_pickable(...);
bool wgr_text2d_set_pickable(...);    bool wgr_text2d_is_pickable(...);   /* makes text2d pickable at all */
bool wgr_text3d_set_pickable(...);    bool wgr_text3d_is_pickable(...);
```

Four kind-specific pick functions become one (`wgr_pick_object`), dispatching on the
handle's kind like scenes do. Stats return a small value struct, like
`wgr_pick_result_t`.

### 2. Text in 3D (15 todos)

librl: a text3d object (font, size, content, transform, color, facing, visible,
pickable, bounds, draw) plus an immediate `rl_text3d_draw_text`.

```c
/* include/wgr_text3d.h */
wgr_handle_t wgr_text3d_create(wgr_handle_t font);          /* font may be 0 (built-in, attach later) */
bool   wgr_text3d_set_font(wgr_handle_t text, wgr_handle_t font);
bool   wgr_text3d_set_text(wgr_handle_t text, const char *text);
bool   wgr_text3d_set_size(wgr_handle_t text, float size);  /* world units: height of a line */
bool   wgr_text3d_set_transform(wgr_handle_t text, float x, float y, float z, float rx, float ry, float rz); /* radians */
bool   wgr_text3d_set_facing(wgr_handle_t text, wgr_sprite3d_facing_t facing);  /* same modes as sprite3d */
bool   wgr_text3d_set_color(wgr_handle_t text, wgr_handle_t color);
bool   wgr_text3d_set_visible(...);  bool wgr_text3d_is_visible(...);
vec2_t wgr_text3d_get_size(wgr_handle_t text);              /* world-space width, height */
void   wgr_text3d_draw(wgr_handle_t text);
void   wgr_text3d_destroy(wgr_handle_t text);
void   wgr_text_draw_3d(wgr_handle_t font, const char *text, float x, float y, float z, float size, wgr_handle_t color);
```

- Drawn with fontstash through sokol_gl in 3D mode; a scene member, sorted with
  transparent parts (glyph edges blend). Pickable as its world-space quad.
- Naming follows text2d (`set_text`, not librl's `set_content`); the immediate draw
  joins the text module (`wgr_text_draw_3d`, like `wgr_text_draw_ex`).

### 3. 3D shapes (7 todos)

librl: 3D rectangle and circle (center, size, axis-angle rotation), line and line
strip, immediate and retained. libwgrender: retained shapes are local geometry plus
`wgr_shape3d_set_transform`; immediate 3D draws take a center and size.

```c
/* retained: local geometry, placed with wgr_shape3d_set_transform */
bool wgr_shape3d_set_rectangle(wgr_handle_t shape, float width, float height);  /* in the XY plane, filled */
bool wgr_shape3d_set_circle(wgr_handle_t shape, float radius);                  /* in the XY plane, outline */
bool wgr_shape3d_set_line(wgr_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1);
bool wgr_shape3d_set_line_strip(wgr_handle_t shape);                            /* empty strip */
bool wgr_shape3d_add_point(wgr_handle_t shape, float x, float y, float z);      /* append to the strip */

/* immediate (3D mode); rotation is euler radians like every transform */
void wgr_shape3d_draw_rectangle(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, wgr_handle_t color);
void wgr_shape3d_draw_circle(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, wgr_handle_t color);
```

- **Line strips without a point array** (the public API can't take pointers):
  build a retained strip point by point. An immediate strip is a retained shape
  drawn once. Decision below.
- Axis-angle rotation becomes euler radians, matching every other libwgrender transform.

### 4. Window and monitors (8 todos)

librl: set window size and position, count monitors, get a monitor's size and
position, move to a monitor. **sokol_app has none of these** (only title, fullscreen
toggle and mouse lock), and the related window flags (resizable, undecorated,
hidden, ...) are accepted but ignored today (docs/TASKS.md).

Options:

- **A. Native calls in `wgr_platform`** per window system: X11 (`XResizeWindow`,
  `XMoveWindow`, XRandR for monitors, adds `libXrandr`), Win32, Cocoa; web: the
  canvas size only (position and monitors don't apply). Also makes the ignored
  window flags work. Only Linux/X11 and web can be tested here; Windows and macOS
  would be written blind.
- **B. Defer:** mark these as todo until a game needs them, or until a sokol_app
  release adds them.

### 5. Model animation and validity (5 todos)

librl (raylib): animations as frame counts and "set frame N"; `is_valid` and a
strict variant; a default placeholder model.

```c
float wgr_model_get_animation_duration(wgr_handle_t model, int animation);  /* seconds */
bool  wgr_model_set_animation_time(wgr_handle_t model, float seconds);      /* pose at a time */
bool  wgr_model_is_ready(wgr_handle_t model);   /* has a loaded mesh (handles are already checked) */
```

- glTF animations are keyframed in seconds, not uniform frames, so duration and time
  replace frame count and frame index.
- `is_valid`/`is_valid_strict` checked raylib model data; libwgrender handles are
  generation-checked, so the useful question is "does it have a mesh yet":
  `wgr_model_is_ready`.
- **Drop** the placeholder model: a model without a mesh draws nothing and logs
  why; missing images already get the placeholder texture.

### 6. Assets (3 todos)

- `rl_asset_get_host` → `const char *wgr_asset_get_host(void)`.
- `rl_asset_ensure_many_async` → belongs to the loading pipeline (next roadmap item):
  a task group with a handle-only API. Move there.
- `rl_asset_ping_host` (latency to the asset host) → a core asset feature, made
  asynchronous (the `wgr_net` module was dropped: networking beyond assets lives
  outside libwgrender). Web first; desktop once it downloads.

### 7. Small leftovers (5 todos)

- `rl_sound_set_pan` → `wgr_sound_set_pan(sound, pan)`: -1 left .. 1 right,
  constant-power panning in the mixer.
- `rl_sprite3d_get_transform` (out-pointers) → `wgr_sprite3d_get_position` and
  `get_rotation`, returning `vec3_t`.
- `rl_text_draw_fps_ex(font, x, y, size, color)` → `wgr_text_draw_fps_ex` with the
  same parameters (also removes the last `PARITY:` note in `examples/simple.c`).
- `rl_font_get_default` → **drop:** font handle 0 already means the built-in font
  everywhere.
- `rl_texture_draw_ground` (a textured quad on the ground) → **drop:** a sprite3d
  with `WGR_SPRITE3D_FACING_Y_UP` does this, and can be placed, picked and added to
  scenes.

### Decisions

1. **Picking:** one `wgr_pick_object` for every kind, pickable flags on every
   pickable object, stats as a value struct. Recommend: yes.
2. **Text3d:** as above, sharing sprite3d's facing modes. Recommend: yes.
3. **Line strips:** built point by point on a retained shape
   (`wgr_shape3d_set_line_strip` + `wgr_shape3d_add_point`), with no immediate strip
   function. Alternatives: an immediate `begin/point/end` sequence, or strips from
   a "point list" resource handle. Recommend: retained, point by point.
4. **Window and monitors:** A (native, Linux/X11 + web now, Windows/macOS written
   but untested, plus the ignored window flags) or B (defer). Recommend: B for now.
   It's the biggest item, can't be tested on two of three desktop platforms, and
   no current work needs it; revisit with the first game that does.
5. **Model animation:** durations and times in seconds instead of frames;
   `wgr_model_is_ready`; drop the placeholder model. Recommend: yes.
6. **Assets:** add `wgr_asset_get_host`; move batch ensure to the loading pipeline
   and make ping asynchronous in the asset layer. Recommend: yes.
7. **Leftovers:** pan, sprite3d getters, FPS text with a font; drop
   `font_get_default` and `texture_draw_ground`. Recommend: yes.

### As built

- **Bug fixed:** scene picking treated a non-pickable 3D object as "no exact test"
  and fell back to its bounding box, so `wgr_shape3d_set_pickable(false)` still hit.
  Picking now goes through one internal path (`pick_2d`/`pick_3d` in wgr_scene.c),
  shared by `wgr_scene_pick` and `wgr_pick_object`, which also counts the stats.
- **text2d joined scenes** as a 2D member (drawn over 3D, picked by its text
  rectangle), which its pickable flag needed.
- **FREE facing** (`WGR_SPRITE3D_FACING_FREE`, oriented by the rotation) was added
  for sprite3d and text3d: librl's default sprite facing, noted as a gap in
  `examples/simple.c`. `wgr_sprite3d_set_facing` now takes the enum.
- **text3d is depth-tested:** sokol_fontstash's own pipeline has no depth test, so
  text3d draws fontstash's glyph quads (via its text iterator) with fontstash's
  shader and atlas through its own depth-tested, blended pipeline. `size` is the
  font size in world units (not the line height, as first written); the bitmap
  font isn't available in 3D.
- **Shapes:** circles are outlines picked inside the outline; lines and strips have
  no area and aren't hit; rays exactly in a rectangle's plane don't hit it.
- **Sound pan is balance, not constant power:** centered keeps full volume on both
  channels, and panning turns the far channel down, so a centered sound plays at
  the same level as before.
- **Pick distances** are measured from the camera's near plane, as scene picks were.
- **Examples:** a new `text3d` example covers labels, a FREE-facing sign, shapes,
  strips, hover picking, stats and FPS in a TrueType font; `simple.c`'s remaining
  PARITY notes are resolved (FREE facing, FPS in the debug font).

### Order and verification

Order: 7 and 6 (small) → 5 → 1 → 3 → 2 (text3d uses picking and the shape work).

Verification: unit tests for each (pick dispatch and flags, stats counting, strip
building, animation time sampling, panning gains, text3d bounds); a `text3d`
example and additions to `pick` and `hello3d` examples; parity map updated, with
`make parity` showing only deferred items as todo; `make verify` and `make
webcheck` on both backends.

*From docs/PLAN-parity.md.*

## Render to texture

Status: **implemented (2026-09-16).** Decisions 1–5 accepted as recommended. See
`wgr_texture_create_target`, `wgr_render_begin_texture` and `examples/render_target.c`;
"As built" below records details found during implementation. **Screen effects
(post-processing) followed on 2026-09-21**: see "Screen effects as built" at the end.

### Why

Draw into a texture, then use that texture like any other: on a sprite, a model
material, or a 2D quad.

- Minimaps, rear-view mirrors, security cameras, portals
- Low-resolution rendering scaled up (pixel-art look, virtual resolution for 2D)
- Rendering UI or text once into a texture
- Later: post-processing (needs full-screen shader passes, materials phase 2)

Environment lighting doesn't depend on this. It can prefilter its maps on the CPU
at load, which works everywhere, including WebGL2, where float render targets
need an extension.

### Where we are

- Everything draws to the screen. `wgr_render_begin_frame/end_frame` records a frame command
  list (sokol_gl layers + model draws, in call order) and replays it in one
  swapchain pass at `wgr_render_end_frame`.
- Every pipeline (sokol_gl 2D/3D, model pipelines, the fontstash text pipeline) is
  built for the screen's color format, depth format and MSAA sample count.
- Aspect ratio and 2D coordinates come from the window size.

### Proposed design

#### API

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
/* Between wgr_render_begin_frame and wgr_render_end_frame: draw into `texture` (a target)
 * instead of the screen until wgr_render_end_texture. Everything works inside:
 * clear, 2D, 3D mode, scenes, models, sprites, text. */
bool wgr_render_begin_texture(wgr_handle_t texture);
void wgr_render_end_texture(void);
```

A render target is a texture resource (made by a generator, like
`wgr_mesh_create_cube`), not a new kind: every API that takes a texture accepts it.

#### Frame semantics

- Drawing is still recorded during the frame and replayed at `wgr_render_end_frame`: each
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

#### Format

Targets match the screen: its color format (RGBA8, or BGRA8 on WebGPU), a depth
buffer, and its MSAA sample count, resolved into the texture. That way every
existing pipeline (sokol_gl, models, text) works in a target unchanged, and
edges are as smooth as on screen. Targets have no mipmaps.

### Decisions

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

### As built

- **Bottom-up targets on GL / WebGL2.** A texture rendered on a backend whose
  framebuffer origin is bottom-left stores its rows bottom-up. Rather than flip
  projections while rendering (which would also flip winding and can't reach the
  bitmap text shader), whoever samples a target maps v to 1 - v
  (`wgri_texture_is_flipped`): sprites, `wgr_texture_draw`, sprite3d and material UV
  transforms. Verified identical on WebGL2 and WebGPU.
- **Bitmap text (`wgr_text_draw`) works in targets:** each pass records into its own
  sokol_debugtext layer, drawn at the end of that pass.
- **Second pass into the same target in a frame** loads the earlier contents instead
  of clearing.
- **Self-use:** a target sampled inside its own pass binds the default white
  texture (warned once), rather than skipping the draw.
- Model placements are per pass, so a model drawn into several targets gets each
  target's aspect ratio. Up to 16 target passes per frame.

### Screen effects as built (2026-09-21)

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
  (`src/wgr_effect.c`), which registers `wgri_render_hooks.effects_begin` (where the
  screen's pass draws) and `effects_draw` (the chain, ending on the swapchain). A
  program that never adds an effect doesn't link it (`make check`).
- **GL's bottom-up targets** are handled as elsewhere: the shader gets a flag in its
  frame block and flips v when it samples, so `wgr_screen_uv` means the same thing on
  GL, WebGL2 and WebGPU. Checked on all three.
- **Not in this phase** (recorded in TASKS): HDR/float targets, so a chain can tone map
  after bloom; keeping contents between frames; targets without depth or MSAA (an
  effect chain allocates both today); the depth buffer as an input (fog, depth of
  field); effects on a render target's pass rather than the screen.

### Verification

- Unit tests: command-list recording per pass (pass order, target sizes, nested and
  self-use rejection), sampler selection.
- New example `examples/render_target.c`: a spinning model rendered into a
  low-resolution target shown enlarged with nearest filtering, a minimap from a
  top-down camera, and text rendered into a texture on a model material.
- Screenshots on desktop GL, WebGL2 and WebGPU; `make test`, `make smoke`,
  `make check`, `make webcheck` (both backends); CI.

*From docs/PLAN-render-target.md.*

## Shadows

Status: **phases 1 and 2 implemented (2026-09-21)** on desktop GL, WebGL2 and WebGPU:
one directional light, then spot lights and up to four casting at once.
Decisions 1–7 were answered as recommended, with one addition during implementation:
a shadow's strength and tint. See "Phase 1 as built", and "The WebGPU bug" for the one
that took longest. Roadmap: the largest remaining gap against three.js, which ships
shadow maps. Builds on lighting ([PLAN-lighting.md](#lighting-light-objects-per-scene-lighting)), materials
([PLAN-materials.md](#materials-and-shaders)) and render targets
([PLAN-render-target.md](#render-to-texture)).

### Why

Without shadows, lit scenes read as objects floating in space: nothing tells the eye
where a model meets the ground. Every screenshot in this repo shows it. Shadows are
also what makes a directional light look like a sun rather than a flat wash.

### Where we are

- Lights are scene objects (directional, point, spot), resolved per scene draw into a
  `wgri_light_env_t` and selected per model (8 at a time, by contribution).
- Model shading lives in `src/shaders/wgr_pbr.glsl`, shared by models and lit sprites;
  custom shaders get the same lights through `wgr_frame` (`shaders/wgr.glsl`).
- Render targets exist (`wgr_texture_create_target`), the frame runs target passes
  before the screen pass, and screen effects already add their own passes after it
  (`src/wgr_effect.c`) — so "more passes before the screen" is a solved shape.
- Nothing writes or reads a depth map; no light has any notion of casting.

### Proposed design

#### API

```c
/* include/wgr_light.h */
/* Cast shadows from this light (off by default: a shadow map costs a pass and
 * memory). Directional lights first; spot and point come later. */
bool wgr_light_set_casts_shadows(wgr_handle_t light, bool casts);
bool wgr_light_get_casts_shadows(wgr_handle_t light);

/* Depth offsets that stop a surface shadowing itself, in shadow-map depth units:
 * a constant, and one scaled by how steeply the surface faces the light.
 * Defaults suit a scene a few tens of units across. */
bool wgr_light_set_shadow_bias(wgr_handle_t light, float constant, float slope);

/* Pixels each way of this light's shadow map (rounded to a power of two, 256 to
 * 4096; default 2048). Bigger is sharper and slower. */
bool wgr_light_set_shadow_map_size(wgr_handle_t light, int size);

/* How far from the camera the light's shadows reach (world units; default 50).
 * The map covers that much, so a smaller distance is a sharper shadow. */
bool wgr_light_set_shadow_distance(wgr_handle_t light, float distance);

/* include/wgr_model.h */
/* Whether this model is drawn into shadow maps (default: yes) and whether shadows
 * darken it (default: yes). A character casts; a ground plane usually only
 * receives; a glow or a skybox does neither. */
bool wgr_model_set_casts_shadow(wgr_handle_t model, bool casts);
bool wgr_model_set_receives_shadow(wgr_handle_t model, bool receives);
```

Nothing else changes: a program that enables shadows on its sun gets them everywhere
lit shading runs — models, lit 3D sprites, and custom shaders that call the new
`wgr_shadow()` helper.

#### Frame shape

A new optional module, `src/wgr_shadow.c`, registers a render hook that runs before
the frame's other passes:

1. While the frame is recorded, each scene draw already pushes a `wgri_light_env_t` and
   queues model items against it. The shadow module notes which envs have a casting
   light.
2. Before the target and screen passes, for each casting light: fit the light's
   projection, open a pass into that light's depth map, and replay the env's model
   items with a depth-only pipeline (no fragment work beyond alpha cutout).
3. The lit shaders then sample the map. `wgr_frame` gains the light's view-projection
   matrix and its parameters, so models, sprites and custom shaders read it the same
   way (`.wgrshader` format 6 — custom shaders need rebuilding).

The core stays as it is: this is another `wgri_render_hooks` entry, like screen effects,
so a program with no shadows doesn't link the module (`make check`).

#### Fitting

A directional light has no position, so its map covers a box around what the camera
can see, out to the light's shadow distance: fit an orthographic frustum to that
slice of the view frustum, expanded to include casters behind it (so an object off
screen still casts into view). Snap the fit to whole texels so the shadow doesn't
crawl when the camera moves.

#### Sampling

The shader transforms the surface point into the light's clip space, compares its
depth to the map, and averages a small kernel (3x3 by default) for a soft edge.

Two constraints found while planning:

- **Sampler slots are full.** libwgrender owns sampler slots 8–11 (the environment cube,
  the BRDF table, a sprite's texture, sprite data / joints), and 12 is sokol's limit.
  The environment cube and the BRDF table are both sampled linear-clamp, so they can
  share one slot, freeing one for the shadow map.
- **Texture slots**: the map goes at 13, alongside the joints at 12.

#### Custom shaders

`shaders/wgr.glsl` gains, beside `wgr_light()` and the environment helpers:

```glsl
float wgr_shadow(int light, vec3 world_pos, vec3 normal);  /* 1 lit, 0 fully shadowed */
```

so a custom shader lights a surface the way built-in materials do, shadows included.

### Phasing

1. **One directional light**: casting flag, depth pass, fitted orthographic map,
   PCF, models cast and receive, lit sprites receive, `wgr_shadow()` for custom
   shaders, an example. Everything above.
2. **Spot lights** (a perspective map, the same machinery) and **several casting
   lights** at once (a map each, capped). Done; see "Phase 2 as built".
3. **Cascades** for large outdoor scenes, **point lights** (six faces or none), and
   **sprite casters** (alpha-tested quads in the depth pass). Also worth doing then:
   skip a light's pass when nothing it can see has moved.

### Decisions

1. **Opt in per light** (`wgr_light_set_casts_shadows`, off by default), rather than
   shadows on for every light automatically. Recommend: opt in — a shadow map is a
   pass and 16 MB at the default size, and most lights in a scene shouldn't pay it.
2. **Phase 1 is one casting light** (the first casting directional light in the
   scene; later ones warn once and are ignored), rather than N maps from the start.
   Recommend: one — it's the common case (a sun), and it keeps the fit, the uniforms
   and the slot pressure simple until the shape is proven.
3. **Depth texture or color map.** Sample a real depth texture (with a comparison
   sampler), or render depth into an R32F color target and compare by hand?
   Recommend: depth texture — sokol supports `SG_IMAGESAMPLETYPE_DEPTH` with
   `SG_SAMPLERTYPE_COMPARISON` on all three backends, it halves the memory, and
   hardware comparison gives smoother PCF. Fall back to R32F only if a backend
   refuses it.
4. **Per-model receive flag** (`wgr_model_set_receives_shadow`) as well as the cast
   flag. Recommend: yes — it costs one bit and one branch, and it's how you keep a
   skybox, a glowing object or a UI-ish model out of the shading.
5. **The map size lives on the light** (`wgr_light_set_shadow_map_size`) rather than a
   global quality setting. Recommend: on the light — a flashlight and a sun want very
   different sizes, and a global knob can come later as a multiplier.
6. **Shadow distance on the light** (`wgr_light_set_shadow_distance`, default 50)
   rather than fitting the whole scene's bounds automatically. Recommend: on the
   light — automatic fitting over a big scene gives uselessly blurry shadows, and
   this is the one number that trades sharpness for range. Cascades (phase 3) remove
   the trade.
7. **`.wgrshader` format 6**: `wgr_frame` grows the shadow matrix and parameters, so
   custom shaders must be rebuilt (as they were for formats 4 and 5). Recommend: yes,
   and keep refusing older files rather than carrying two layouts.

### Phase 1 as built

- **Opt in twice over.** `src/wgr_shadow.c` is an optional module, so a program links
  the depth pass only if it calls one of the shadow functions — which is why they live
  there rather than beside the other light setters. (They started in `wgr_light.c`; the
  linker dropped the whole module and the first render came out with no shadows at
  all.) At runtime a light casts only when asked, and a model says whether it casts
  and whether it receives.
- **One casting light**, the first the scene finds (`wgri_light_env_t.shadow_light`),
  and only a directional one: spot and point lights refuse with a warning.
- **The fit** covers the slice of the camera's view out to the light's shadow
  distance, as a square so its texel size doesn't change as the camera turns, snapped
  to whole texels so shadows don't crawl, with the near plane pulled back 50 units so
  casters behind the camera still cast. `wgri_shadow_fit_directional` is pure and
  tested.
- **The bias is measured in shadow texels**, not in depth units. It started in depth
  units, which read as "0.0015" but meant 20 cm along the light over the map's 155-unit
  range: shadows lifted off their casters' feet and thin parts (a hat brim) vanished.
  Texels hold at any map size or distance.
- **Most of the acne is dealt with by offsetting the lookup along the surface normal**
  (about a texel, more on surfaces facing the light edge-on) rather than by pushing
  the depth back, which is what lifts shadows off their feet.
- **A shadow fades out over the last tenth of the map** instead of being cut through
  where the light's coverage ends.
- **Strength and tint** (`wgr_light_set_shadow_strength`, `_set_shadow_color`, added
  during implementation): how much of the light a shadow takes away, and a colour
  mixed into what it leaves. Shadows are really coloured by the ambient and
  environment light that still reaches them; the tint is the stylised knob.
- **The sampling runs for every pixel of a draw**, with the "outside the map" cases
  folded in as weights rather than early returns: a texture comparison in branchy code
  is undefined where the GPU needs neighbouring pixels to filter.
- **Sampler slots were full** (libwgrender owns 8–11, sokol allows 12), so the environment
  cube and the BRDF table now share one sampler — both are linear and clamped.
- `.wgrshader` format 6: `wgr_frame` carries the light's matrix and parameters, and
  `wgr_shadow(i, pos, n)` gives custom shaders the same answer built-in materials use.

### Phase 2 as built

- **One depth array, a layer per casting light** (`WGRI_MAX_SHADOW_LIGHTS` = 4), rather
  than a texture each. Custom shaders have almost no sampler slots left (libwgrender owns 8
  and 9 of the twelve), and an array costs one slot however many lights cast. Each
  layer gets its own attachment view (`sg_view_desc.depth_stencil_attachment.slice`)
  and its own pass.
- **Layers share one size**, which is what an array is: the largest `shadow_map_size`
  any casting light asked for wins, and `wgr_light_set_shadow_map_size` says so. Keep
  them equal unless you mean it — a 2048 sun drags a torch's layer up with it. The
  alternative, an atlas with per-light tiles, buys memory back at the cost of uv rects
  and PCF that must not bleed across tile borders; not worth it yet.
- **A light's slot rides in `u_light_spot[i].z`** (-1: it casts nothing), so the
  per-light arrays don't grow at all. Everything else is per slot: the matrix, the
  texel sizes, the bias, the tint and the strength — about 460 bytes added to the
  frame block. `.wgrshader` format 7.
- **Spot lights fit their own cone**: a perspective frustum from the light, with the
  field of view taken from the outer cone angle plus a tenth so the edge isn't on the
  last texel, reaching the nearer of the light's range and its shadow distance. The
  near plane is a hundredth of that reach. `wgri_shadow_fit_spot` is pure and tested,
  like the directional fit.
- **Point lights still refuse**, and say why: they want six maps, one each way.
- The scene hands out slots in the order it finds casting lights; past four, a light
  lights the scene without shadowing it.

### The WebGPU bug

On WebGPU every surface came back fully shadowed while the same code was right on
desktop GL and WebGL2. It looked like a WebGPU problem; it wasn't. The depth pass
never said its depth buffer should be kept, and sokol's default store action for depth
is `DONTCARE` (`sokol_gfx.h`, `_sg_resolve_default_pass_action`): the sensible default
for a depth buffer that only orders a pass's own draws, and exactly wrong for a shadow
map, which *is* the depth buffer. WebGPU took the discard at its word and the map read
as zero everywhere (zero is "in front of everything", so everything was in shadow);
GL treats the discard as a hint and happened to keep the data, which is why it passed
there — a latent bug on GL too, on any driver that honours the hint. The fix is one
explicit `store_action = SG_STOREACTION_STORE`.

What found it, in order, for next time: `webcheck` gained `--verbose` and the
browser's own log entries (`Log.entryAdded`, where Dawn's validation messages go;
`Runtime.consoleAPICalled` never sees them), which showed the pass was valid; then a
fixed comparison reference (0.001) with the sampler's function flipped to
`GREATER_EQUAL` lit everything, proving the sampler worked and the map held zeros; and
only then was it worth reading how the pass's depth was stored. The things ruled out
first — the clip-space depth range, the map's orientation, branchy sampling, an R32F
colour map instead of a depth texture — all cost cycles because each was a guess made
before there was a way to see what the map held.

### Verification

- Unit tests: the light's new state and its defaults; the fit (pure math: a frustum
  slice in, an orthographic matrix out, texel snapping) against known values; a
  casting light adding exactly one depth pass to the frame, and none when nothing
  casts; per-model cast/receive flags reaching the draw.
- Visual: a scene with a ground plane and a few models, shadows on and off,
  screenshots on desktop GL, WebGL2 and WebGPU (`examples/shadows.c`); the existing
  `lights`, `shaders` and `postprocess` examples still look right.
- `make verify`, `make webcheck` (both backends), `make test SANITIZE=address`,
  `make windows-test` / `windows-smoke` under Wine.
- Cost, measured with `make shadowbench DESKTOP=1` (tools/bench/shadowbench.c) on an
  RTX 4080 laptop GPU, vsync off — the same scene of generated shapes with a floor,
  half of them turning, the camera moving so the fit re-snaps. Frame ms, and the CPU
  ms inside it (one run; runs vary by about 0.05 ms):

  | case             |  100 |  400 | 1000 | 4000 |
  |------------------|-----:|-----:|-----:|-----:|
  | no shadows       | 0.23 | 0.82 | 1.34 | 5.18 |
  | sun, 1024        | 0.32 | 0.90 | 1.99 | 6.99 |
  | sun, 2048        | 0.33 | 0.90 | 1.90 | 6.44 |
  | sun, 4096        | 0.38 | 1.01 | 2.06 | 6.59 |
  | sun + spot, 1024 | 0.45 | 1.11 | 2.36 | 8.57 |
  | sun, no receive  | 0.30 | 0.79 | 1.60 | 5.02 |

  Nearly all of that is CPU: at 4000 models the "no shadows" frame is 5.18 ms with
  4.93 of it on the CPU, almost all in submission.

  Read the two columns against each other. **The CPU number barely moves across 1024,
  2048 and 4096** — the pass draws the same casters whatever the map's size — so what
  separates those rows is the map's fill, and it is real: sixteen times the pixels
  costs about +0.05 ms at 100 models and +0.12 at 400. What separates "no shadows" from
  "sun, 1024" is the other thing, submitting every caster a second time: +0.15 ms of
  CPU at 400 models, +0.40 at 1000. A second casting light adds another pass and
  roughly repeats it. Receiving is the cheapest part: turning it off saves ~0.02.

  So a casting light costs one pass over its casters plus its map's fill, and which
  dominates is a property of the scene rather than of shadows. Both point at the same
  phase 3 work — skip a light's pass when nothing it sees has moved — and say cascades
  should be budgeted as passes *and* pixels.

  An earlier run of this benchmark stopped at 1000 models, because libwgrender dropped model
  placements past 1024 a frame: asking for 1600 and 4096 measured the same 1024 twice,
  which showed up as two rows with identical submission times. The queue grows now, so
  4000 is a real 4000 — about 1.2 microseconds a mesh to light, and a shadow pass adds
  roughly half that again.

- Checked on desktop GL, WebGL2 and WebGPU: every caster throws a shadow, the no-cast
  sphere throws none, the no-receive sphere stays lit, shadows touch their casters.

*From docs/PLAN-shadows.md.*

## sprite2d (screen-space sprites)

Status: **implemented (2026-09-16).** See `include/wgr_sprite2d.h` and `examples/sprite2d.c`.
Builds on the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)), scene render
passes, and picking. Related: GUI direction in [ROADMAP.md](ROADMAP.md).

### Why

HUD icons, 2D games, and in-game UI all need textured quads in screen space. The
`sprite2d` handle kind is reserved but unimplemented, and `rl_texture_draw_ex`
(one-off screen-space texture draws) has no libwgrender equivalent.

### What librl had, and what it taught us

`rl_sprite2d_create(texture)`, `set_texture`, `set_transform(x, y, scale, rotation)`,
`set_tint`, `set_visible`, `set_pickable`, `draw`, `destroy` (plus
`create_from_file` and a default texture, both already dropped on purpose).

Gaps that forced workarounds:

- **No source rectangle.** Sprite sheets and atlases needed one texture per frame.
- **No pivot/origin.** Rotation and positioning were always around one corner.
- **Uniform scale only, no flip.** Mirroring a character meant a second texture.
- Picking was rectangle-only, so transparent corners of icons were clickable.

### Proposed API

```c
/* include/wgr_sprite2d.h */
wgr_handle_t wgr_sprite2d_create(wgr_handle_t texture);          /* texture may be 0, set later */
void        wgr_sprite2d_destroy(wgr_handle_t sprite);

bool wgr_sprite2d_set_texture(wgr_handle_t sprite, wgr_handle_t texture);
bool wgr_sprite2d_set_source(wgr_handle_t sprite, float x, float y, float width, float height);
                                   /* texture pixels; default: whole texture */
bool wgr_sprite2d_set_position(wgr_handle_t sprite, float x, float y);
bool wgr_sprite2d_set_rotation(wgr_handle_t sprite, float angle);   /* radians */
bool wgr_sprite2d_set_scale(wgr_handle_t sprite, float x, float y); /* negative = flip */
bool wgr_sprite2d_set_size(wgr_handle_t sprite, float width, float height);
                                   /* on-screen size in pixels; default: source size */
bool wgr_sprite2d_set_pivot(wgr_handle_t sprite, float x, float y);
                                   /* 0..1 within the sprite; default (0.5, 0.5) */
bool wgr_sprite2d_set_tint(wgr_handle_t sprite, wgr_handle_t color);
bool wgr_sprite2d_set_visible(wgr_handle_t sprite, bool visible);
bool wgr_sprite2d_is_visible(wgr_handle_t sprite);
bool wgr_sprite2d_set_pickable(wgr_handle_t sprite, bool pickable);
bool wgr_sprite2d_set_pick_alpha_test(wgr_handle_t sprite, bool enable, float threshold);

void wgr_sprite2d_draw(wgr_handle_t sprite);   /* immediate, outside a scene */

/* include/wgr_texture.h: one-off draw without an object (replaces rl_texture_draw_ex) */
void wgr_texture_draw(wgr_handle_t texture, float x, float y, float width, float height,
                     wgr_handle_t tint);
```

- **Flip** is negative scale, not a separate flag: one concept, no conflicting state.
- **Size vs scale:** size sets the base on-screen size in pixels (UI layouts think in
  pixels); scale multiplies it (animation, flipping). Both default to "texture as-is".
- Handle-only rules hold: handles, floats, bools.

### Decisions

Decisions 1–4 were accepted as recommended.

1. **Where 2D objects live.** *Recommend: in scenes.* A scene draws all 3D layers
   first, then its 2D members by layer (ascending) and insertion order, with no depth
   test. `wgr_scene_pick` tests 2D members first, topmost first, since they're drawn on
   top of 3D. One ordering system and one picking API. The alternative, a separate
   "canvas" object for 2D, duplicates layers, picking and membership for little gain.
   Scenes without 2D members are unaffected.
2. **Coordinates.** *Recommend: logical pixels, top-left origin, y down,* matching
   today's 2D drawing. On high-DPI displays, logical pixels = framebuffer pixels /
   DPI scale, so UI doesn't shrink on a 4K laptop. Virtual resolution (design at
   1920x1080, scale to the window) is a separate camera2d concern, left for the 2D/UI
   layer work, not sprite2d.
3. **Immediate drawing.** *Recommend both:* `wgr_sprite2d_draw(handle)` for a sprite
   outside a scene (like `wgr_model_draw`), and `wgr_texture_draw(...)` for one-off
   draws without creating an object. Both follow call order (frame command list).
4. **Batching.** *Recommend: sokol_gl textured quads for now,* consecutive sprites
   sharing a texture batched automatically by sokol_gl. The batched renderer on the
   roadmap replaces the internals later without an API change.
5. **Angle units across the public API. Decided (2026-09-16): radians everywhere.**
   Matches our internal math, sokol and glTF (camera `yfov`, `KHR_lights_punctual`
   cone angles), and what `atan2`/`sin`/`cos` and physics libraries produce, so game
   code needs no conversions. Apps that want degrees convert themselves; libwgrender
   exposes no degree helpers. Done ahead of sprite2d: camera `fov` and the spot cone
   are radians, and cameras have separate `fov` (perspective) and `ortho_height`
   (orthographic) settings instead of one overloaded `fovy`.

### Picking

- Rotated rectangle test in the sprite's local space (inverse of pivot, scale,
  rotation, position), then UV from the local point for the optional alpha test,
  reusing the texture alpha mask sprite3d already builds.
- 2D hits return `point_world` as screen pixels and the sprite handle; `distance` 0.
  (Documented: 2D hits have no depth.)

### Verification

- Unit tests: local transform (pivot, rotation, scale, flip), source-rect UVs,
  rotated-rect hit test, alpha test, pick order (2D before 3D, topmost 2D first).
- Visual test: an atlas-driven animated sprite, rotated/scaled/flipped sprites with
  pivots, a HUD over a 3D scene, click-picking with alpha test.
- `examples/sprite2d.c`; `make test`, `make check`, `make parity`, `make webcheck`.

### Out of scope

Text layout, 9-slice panels, UI widgets, virtual resolution / camera2d, render
targets. Those belong to the 2D/UI layer (and a Clay-style layout module, see ROADMAP).

*From docs/PLAN-sprite2d.md.*

## UI through libwgrender's public API

Status: **done for now** (2026-09-18). Steps 1 (drawing and input), 2 (text) and 3 (the Clay
example) are built; see "As built". Step 4 — clipboard, letter spacing, the ImGui hook —
waits until text fields, a design or developer UI need it.

### Why

libwgrender won't ship a GUI toolkit ([ROADMAP.md](ROADMAP.md), "GUI direction"). In-game UI
comes from a layout library — [Clay](https://github.com/nicbarker/clay) is the likely
one — drawing through libwgrender, and developer UI from Dear ImGui.

A layout library can reach the GPU two ways. It can bypass libwgrender: Clay ships
`sokol_clay.h`, which talks to sokol directly, and in libwgrender that means a second
fontstash context and a second copy of every font (with different text metrics), raw
`sapp_event` input that skips libwgrender's input edges and pointer capture, and a projection
that fights libwgrender's 2D mode. Or libwgrender can be its renderer: a small piece of glue turns
the library's draw commands into libwgrender calls.

This plan is the second way, done so the glue needs **nothing but libwgrender's public API**.
Then no third-party types or code enter libwgrender, the glue ports to any language binding
(Clay already has bindings for several), and every addition below is just as useful for
a hand-written HUD or another library (microui and Nuklear's command mode emit the same
kinds of commands).

### What the prototype showed

A throwaway `examples/clay.c` (2026-09-18, not committed; Clay pinned at `e6cc369`) drew
Clay's own demo layout — a header bar with a hover dropdown, a clickable sidebar, a
momentum-scrolled document of wrapped text — through the public API, checked in the
browser on both backends. It cost about +50 KB gzipped on web, only in the program that
used it. It worked, and it found the gaps this plan closes:

- no immediate rounded rectangle or border (the prototype built both from triangles);
- clips don't nest (Clay nests scroll areas; `wgr_render_end_clip` resets to the whole
  screen);
- immediate 2D positions are `int`, while layout produces fractions;
- text draw and measure need NUL-terminated strings; Clay passes slices of one string,
  so the prototype copied every word it measured;
- no way for a UI to claim the pointer: a game drawn behind the UI would also get its
  clicks;
- no letter spacing; images would need texture handles and source rectangles.

It also found a bug, fixed in `caa49dd`: measuring `" "` returned 0, so Clay wrapped
every line too late.

Checking the code for this plan found one more, the most important:

- **text is rasterized at its logical size.** Nothing in `wgr_text.c` or `wgr_font.c`
  looks at the DPI scale, so with high-DPI on, every glyph is drawn at half resolution
  and magnified — blurry text on phones and retina screens, for all libwgrender text, not
  just UI.

### Design

Every signature follows the public API rules: handles, scalars, enums, `const char *`.

#### 1. Drawing

**Float coordinates for immediate 2D.** `wgr_shape2d_draw_rectangle`, `_rectangle_lines`,
`_line`, `_circle` and `_circle_lines` take `int` positions and sizes today (triangle
already takes floats). They become `float`: layout produces fractional positions, and on
a high-DPI screen half a logical pixel is a real pixel. This breaks callers that pass
ints only in the sense that they convert implicitly; nothing needs rewriting.

**Rounded rectangles and borders**, per-corner radii and per-side widths, as Clay and CSS
have them:

```c
void wgr_shape2d_draw_rounded_rectangle(float x, float y, float width, float height,
                                       float r_top_left, float r_top_right,
                                       float r_bottom_right, float r_bottom_left, wgr_color_t color);
void wgr_shape2d_draw_border(float x, float y, float width, float height,
                            float left, float top, float right, float bottom,
                            float r_top_left, float r_top_right,
                            float r_bottom_right, float r_bottom_left, wgr_color_t color);
```

Radii are clamped to half the shorter side; a border's inner corners follow the outer
radius minus the adjoining width. The geometry already exists for retained 2D shapes
(`src/wgr_shape2d.c`), which move onto the same code.

**Images with a source rectangle**, and nine-slice, for skinned panels and icons from an
atlas:

```c
void wgr_texture_draw_ex(wgr_handle_t texture, float source_x, float source_y,
                        float source_width, float source_height,
                        float x, float y, float width, float height, wgr_color_t tint);
void wgr_texture_draw_nine_slice(wgr_handle_t texture, float source_x, float source_y,
                                float source_width, float source_height,
                                float left, float top, float right, float bottom,
                                float x, float y, float width, float height, wgr_color_t tint);
```

The nine-slice math is sprite2d's (`wgri_sprite2d_nine_slice_axis`).

**A clip stack** replaces `wgr_render_begin_clip` / `wgr_render_end_clip`:

```c
void wgr_render_push_clip(float x, float y, float width, float height); /* intersects the current clip */
void wgr_render_pop_clip(void);                                         /* back to the enclosing one */
```

Each push intersects with the clip it's pushed inside, so a scroll area inside a panel
stays inside the panel. Scene layer clips (`wgr_scene_set_clip`) use the same stack, so a
layer clip and a UI clip nest too. The stack is per render pass (render targets start
empty) and is emptied at the end of a frame, with a warning if pushes and pops didn't
match.

#### 2. Text

**Length-taking draw and measure**, so text can be a slice of a longer string:

```c
void   wgr_text_draw_n(wgr_handle_t font, const char *text, int length, float x, float y,
                      float size, wgr_color_t color);
vec2_t wgr_text_measure_n(wgr_handle_t font, const char *text, int length, float size);
```

A negative length means "up to the NUL", so the existing `_ex` functions become thin
wrappers.

**Crisp text at any DPI** (internal, no API change). Glyphs are rasterized at
`size × dpi scale` and drawn scaled back to logical pixels, so they land 1:1 on the
screen's pixels. Measurement stays in logical pixels. Render targets use their own
pixels (scale 1). Two costs to watch: glyphs take up to 4× the atlas area at 2× DPI (the
atlas may need to grow), and text laid out on a 1× and a 2× screen can differ by a pixel,
as with any DPI-aware text.

**Letter spacing** — deferred until a design needs it; when it does, it becomes a
parameter of the `_n` functions and a text2d setting.

#### 3. Input

**The UI can capture the pointer and the keyboard.** Game code already asks
`wgr_input_is_pointer_captured()` before acting on the mouse; today only scene members can
set that. The UI gets its own, sticky, switch:

```c
void wgr_input_set_pointer_captured(bool captured);   /* the UI holds the pointer */
void wgr_input_set_keyboard_captured(bool captured);  /* a UI text field has focus */
bool wgr_input_is_keyboard_captured(void);
```

`wgr_input_is_pointer_captured()` becomes true while either a scene press or the UI holds
the pointer. The capture is **sticky** — it stays until the UI changes it — because of
frame order: the runtime updates scene interaction, runs the ticks, then calls the frame
callback, and a UI lays out in the frame callback. A capture that lasted one frame would
never be seen by the ticks. So the glue sets it every frame from the library's hover
result, and ticks see the previous frame's value: one frame of latency, the same as
scene interaction. The glue also keeps the pointer captured while a press that started
over UI is held, even when the drag leaves the UI, as scene capture does (so dragging off
a slider doesn't start orbiting the camera). Captures are advisory, as today: libwgrender still
reports input; game code checks the flags.

**Fractional, two-axis wheel.** `wgr_mouse_state_t.wheel` is an `int`, which quantizes
trackpad and precision-wheel scrolling. It becomes a `float`, with a `wheel_x` beside it
(sokol reports both axes).

**Clipboard** — `wgr_clipboard_set_text(const char *)` and
`const char *wgr_clipboard_get_text(void)` — when text fields arrive. On web, sokol
delivers pastes as events, so "get" returns the last paste.

#### 4. The glue, outside the core

With the above, the Clay glue is about 150 lines of public-API code: a table from Clay
font ids to libwgrender fonts, the measure callback (`wgr_text_measure_n`), a switch from Clay's
render commands to the draw calls above, image data as a small `{texture, source rect}`
struct, and input fed from `wgr_input` with the pointer capture set from
`Clay_GetPointerOverIds()`. It starts as a rebuilt `examples/clay.c`, with Clay vendored
for the example only. If it proves reusable, it becomes an optional header-only
`ext/wgr_clay.h`; libwgrender itself never includes or links Clay.

#### Developer UI (Dear ImGui) — later

ImGui renders itself (its own pipeline and buffers, via `sokol_imgui.h`), so the public
API can't carry it. It needs a small extension hook instead: raw input events forwarded
in, and a callback that draws inside libwgrender's final swapchain pass, after libwgrender's own
drawing. That hook passes sokol types, so it would live in an extension header outside
the public API's rules. It's independent of everything above and can come later.

### Order

1. **Drawing and input** — float immediate coordinates, rounded rectangle and border,
   `wgr_texture_draw_ex` and nine-slice, the clip stack, pointer and keyboard capture,
   the float wheel. Mechanical, and each piece is useful on its own. The only caller of
   today's begin/end clip is scene layer clipping, which moves onto the stack
   unchanged, so `examples/ui.c`'s clipped list keeps working as it is.
2. **Text** — `_n` draw and measure, then DPI-correct glyphs (the one substantial
   piece: rasterization, measurement, atlas sizing).
3. **The Clay example**, rebuilt on the above as the end-to-end check.
4. Clipboard and letter spacing, when text fields or a design need them; the ImGui hook
   when developer UI is wanted.

### Decisions

1. **Glue on the public API only; no Clay code or types in libwgrender.** Recommend: yes.
2. **Immediate 2D takes floats** (a signature change for five functions). Recommend: yes.
3. **Clip stack replaces begin/end clip**, shared with scene layer clips. Recommend: yes.
4. **`_n` text functions**, negative length meaning NUL-terminated, rather than adding a
   length to every existing text function. Recommend: yes.
5. **Sticky pointer and keyboard capture** with one frame of latency for ticks.
   Recommend: yes.
6. **`wheel` becomes a float, plus `wheel_x`.** Recommend: yes.
7. **DPI-correct glyph rasterization** for all 2D text. Recommend: yes — without it UI
   text is blurry on every high-DPI screen.
8. **Deferred:** letter spacing, clipboard, the ImGui hook.

### As built

#### Step 1: drawing and input (2026-09-18)

- Immediate 2D takes floats (`rectangle`, `rectangle_lines`, `line`, `circle`,
  `circle_lines`); callers passing ints needed no changes.
- `wgr_shape2d_draw_rounded_rectangle` and `wgr_shape2d_draw_border` as designed. One
  outline builder (`wgri_shape2d_rounded_outline`, internal) now serves them and the
  retained rectangles; borders wider than the box fill it.
- `wgr_texture_draw_ex` and `wgr_texture_draw_nine_slice`; `wgr_texture_draw` became a
  wrapper, and the nine-slice helper no longer needs a sprite, so sprites and the
  immediate call share it.
- The clip stack, `wgr_render_push_clip` / `pop_clip`, replaced begin/end clip: 32 deep,
  each push intersected with its parent, a render target's pass starting from its own
  target, and pushes left open dropped (with a warning) at the end of a texture pass
  or the frame. Scene layer clips push and pop on it, so a layer clip intersects with a
  clip pushed around `wgr_scene_draw`.
- **Capture names differ from the draft:** `wgr_input_set_pointer_captured` /
  `wgr_input_set_keyboard_captured` / `wgr_input_is_keyboard_captured`, the house
  set/is pairing for the existing `wgr_input_is_pointer_captured`. The draft's
  `wgr_input_capture_pointer` would have sat next to the existing
  `wgr_input_capture_cursor`, which means something else (pointer lock). The scene's
  internal setter became `wgri_input_set_scene_pointer_captured`.
- The wheel is `float wheel, wheel_x` (and `wgr_input_get_mouse_wheel_x`). Beyond
  precision, this fixed a bug: each scroll event was truncated to `int` before being
  added up, so small trackpad steps (sokol reports a notch as about 1.0 and trackpad
  steps as fractions) were dropped entirely.
- `examples/ui.c` gained an immediate header — its nine-slice texture drawn directly,
  and a rounded, bordered status pill — above the retained panel; checked on both
  backends.
- Tests: `input_wheel`, `input_capture`, `render_clip_stack`, `shape2d_immediate`
  (outline geometry, and vertex counts proving each draw emits and skips what it
  should), `texture_draw_immediate`.

#### Step 2: text (2026-09-18)

- `wgr_text_draw_n` / `wgr_text_measure_n` as designed; the `_ex` functions, `wgr_text_draw`
  and the FPS counter all go through them. The internal layout (`wgri_text_block_size`,
  `wgri_text_block_draw`, `wgri_text_split_lines`) takes a length too.
- **Crisp high-DPI text.** Glyphs are rasterized at size × the drawing target's pixel
  scale (`wgri_render_pixel_scale`, internal: the screen's DPI scale, 1 inside a render
  target) and drawn under a matching `1/scale` matrix; fontstash emits each call's
  vertices before returning, so they get it. Measurement divides the scale back out.
  In the browser at a device pixel ratio of 2, the `font` example's text went from
  visibly smeared to sharp: edge contrast over the same text rose from 4.93 to 6.40.
  The `font` example now enables high-DPI.
- **Measured widths differ slightly between screens.** fontstash rounds every glyph's
  advance to a whole rasterized pixel, so a line's width is off by up to half a pixel
  per glyph at 1×, a quarter at 2×, a sixth at 3× — higher DPI is *closer* to the true
  width ("Hello, world" at 16 px: 84 px at 1×, 90 at 2×, 88 at 3×, about 88.8 exact).
  Measuring and drawing round the same way, so layout is consistent on any one screen.
- **The glyph atlas grows** instead of silently dropping glyphs that don't fit
  (1024² to start, the smaller side doubling up to 4096²) — but only **between
  frames**. Growing mid-frame recreates the atlas texture while draws recorded earlier
  in the frame still refer to it and to UVs for its old size: with sokol's validation
  on, the next frame's submit aborted. So a full atlas asks to grow, the glyphs that
  didn't fit skip one frame, and `wgri_font_end_frame` grows it after `sg_commit`.
- Tests: `text_slices_and_dpi` — slices, logical metrics at 1×/2×/3× within the
  rounding bound, scale 1 inside a render target, and the atlas growing across a frame
  then drawing from the bigger atlas. The headless platform gained a DPI scale for
  tests (`wgri_platform_set_headless_dpi_scale`).

#### Step 3: the Clay example (2026-09-18)

- `examples/clay.c` draws Clay's demo layout through the public API only: about 100
  lines of glue (measure callback on `wgr_text_measure_n`, a render-command switch onto
  the step 1 and 2 calls, pointer capture from `Clay_GetPointerOverIds`). Every GAP the
  prototype marked is gone: no string copies, no triangle-built rectangles, nested
  clips, images from a small `{texture, source rect}` struct in `imageData`.
- Clay is vendored for the example only (`deps/clay`, pinned at `e6cc369`); libwgrender
  doesn't include or link it. It costs about +51 KB gzipped on web, in that program
  only (`clay` 376.6 KB vs `hello` 325.9 KB).
- A strip of "game" beside the UI shows capture working: clicks there drop markers,
  and a press that starts on the UI and is released over the strip doesn't. Checked
  in the browser with CDP input on WebGL2 (dropdown on hover, sidebar switching, wheel
  scrolling, two markers from two strip clicks and none from the drag), plus webcheck
  on both backends and a 2× device-pixel-ratio capture (crisp text, smooth corners).
- A second page (Tab) exercises what the demo layout doesn't: images from source
  rectangles, tinted and nine-sliced; a floating tooltip per image and click to select;
  borders with per-side widths, per-corner radii and lines between children; a
  horizontal scroll area inside a vertical one; a custom element (a meter the game
  draws); an overlay color that darkens a card on hover; a width and color transition
  (`Clay_EndLayout(dt)`). The glue grew by about 40 lines: `clay_image_t` gains
  nine-slice borders and a tint (not the element's `backgroundColor`, which Clay also
  draws as a rectangle over the image), a `clay_custom_t` draw callback, and a stack of
  overlay colors mixed into every color drawn.
- It found a Clay bug, fixed on a branch of libwgrender's Clay fork (`github.com/robknopf/clay`,
  vendored with `tools/update_clay.sh`, like sokol): the wheel and drag
  went to whichever scroll container came last in Clay's internal list, not the
  innermost one under the pointer, and swap-back removal (after switching pages) put
  the outer area last, so the inner one never scrolled. Also, Clay multiplies the wheel
  delta by 10, so the example passes notches times 4 for 40 pixels per notch.

### Verification

Unit tests on the headless build: clip stack intersection, nesting, per-pass reset and
the unmatched-push warning; rounded rectangle and border radius clamping; source
rectangle and nine-slice mapping; `_n` measure against `_ex` for the same text; sticky
capture across the frame and tick order; logical text metrics unchanged between DPI
scales. Examples: `ui` on the clip stack, the rebuilt `clay` example. Browsers: webcheck
on both backends, plus a CDP run with `Emulation.setDeviceMetricsOverride` at a device
scale factor of 2 to compare text sharpness against the current build.

*From docs/PLAN-ui.md.*

## wgr_fs + web-capable ensure (Phase 2)

Status: **implemented (steps 1–3 done).** wasm target + serve loop, `wgr_fs`
desktop seam, and web idbfs + fetch all landed. Fetch uses **sokol_fetch**
(streaming via HTTP Range; `tools/serve.py` serves 206), not the hand-rolled
EM_JS first tried. `ensure` gained a per-call `fetch_url` source override and a
`WGR_ASSET_FORCE_FETCH` flag. Remaining: in-browser verification of the streaming
path, WebGPU backend (WGSL via sokol-shdc), desktop network-fetch fallback, and
the eventual `wgr_net` (see "Later"). Kept as the design record.
Builds on the Phase 1 ensure model (see [PLAN-handle-only-api.md](#handle-only-public-api-librl-style-assetresource-split))
and the proven librl `rl_fs` design (cribbed, not vendored — see "Decisions").

**Update (2026-09-20): IDBFS replaced by a per-file cache.** IDBFS restored the
whole cache into MEMFS at every start: on a Pixel 9 with a 56.5 MB cache that was
~80 ms of main-thread IndexedDB callbacks (one of 45 ms) before anything loaded,
and every cached file stayed in memory whether the program used it or not. Now
`wgr_fs` keeps its own IndexedDB store (`wgr_fs:<root>`, object store `files`, one
Blob per file, keyed by its full MEMFS path):

- **Startup** opens the store and reads only its keys (`wgr:fs-ready`, ~10 ms after
  libwgrender's init); `wgri_fs_is_ready()` polls that.
- **A cached file** is read when it's ensured: `wgr_asset` sees it isn't in MEMFS but
  is cached (`wgri_fs_is_cached`), starts `wgri_fs_cache_read_begin` and polls it, then
  resolves the task. A failed read drops the key and the file downloads instead.
  Blobs, because reading a stored `Uint8Array` unpacked it on the main thread
  (~6 ms per 5.6 MB file); a Blob's bytes are read off it (`blob.arrayBuffer()`) and
  handed to MEMFS without a copy.
- **A write** (a download) goes to MEMFS and is put into the store; there's no
  flush and no whole-tree sync.
- The old IDBFS database (named after the mount point, `/sk`) is deleted: its files
  download once more.

FlightHelmet (compressed) from the cache on the Pixel: loaded in 0.28 s (was 0.35),
worst frame while loading in the background 27-30 ms (was 60-85). The restore/flush
design below is kept as the record.

### Why

Phase 1 gave us `wgr_asset_ensure_async(path) → on_ready(path)` → sync
`wgr_*_create(path)`. On **desktop** that already works (files are on disk; ensure
is an existence check). On **web** there is no synchronous disk: a file must be
synced out of IndexedDB into the in-memory FS before any `fopen` sees it, and
writes must be synced back or they vanish on reload. `wgr_fs` is the layer that
makes "the file is locally readable" true on both platforms so the rest of libwgrender
(and the sync `_create(path)` creators) stay platform-agnostic.

### The constraint (what rl_fs proves)

IDBFS is asynchronous; synchronous file I/O only sees data across explicit sync
barriers. rl_fs handles this with three, all polled like our asset tasks:

- **Restore (startup):** IDBFS → MEMFS sync; reads must block until it's ready.
  Polled each frame with a **timeout → fall back to network fetch**.
- **Flush (writes):** MEMFS → IDBFS sync; run after writes and before deinit, or
  cached data is lost on reload.
- **JS↔C coordination:** plain `EM_JS` callbacks — `FS.syncfs` is kicked
  non-blocking and sets a `Module.wgr_fs_restore` flag that `wgri_fs_is_ready()`
  polls. **No JSPI suspension** (see the JSPI finding under "Decisions locked":
  `sapp_run` owns the loop, so callbacks can't suspend).

Desktop has none of this: restore is instantly "ready," read/write hit the real
directory.

### Target `wgr_fs` API (trimmed from rl_fs)

A jailed local filesystem under a root dir. Start with the minimum ensure needs;
add user-facing ops only when something needs them.

```c
/* lifecycle — IDBFS on wasm, a real directory on desktop */
int          wgri_fs_init(const char *root_dir);        /* sync (desktop / JSPI) */
wgr_handle_t  wgr_fs_restore_async(void);                /* IDBFS→MEMFS; poll via tick */
bool         wgri_fs_is_ready(void);                     /* restore barrier cleared? */
int          wgr_fs_flush(void);                        /* MEMFS→IDBFS (persist)  */
void         wgri_fs_deinit(void);                       /* flush + unmount         */
const char  *wgr_fs_get_root_dir(void);

/* file ops — all paths jailed to root_dir (internal at first) */
bool wgri_fs_exists(const char *path);
int  wgri_fs_read(const char *path, unsigned char **out_data, size_t *out_size);
void wgri_fs_read_free(unsigned char *data);
int  wgri_fs_write(const char *path, const unsigned char *data, size_t size);
```

Defer rl_fs's richer surface (mkdir/rmdir/remove/clear/normalize, the LRU memory
cache, dependency prefetch) until a caller needs it — keep `wgr_fs` small.

### How ensure uses wgr_fs

`wgr_asset_ensure_async` / `wgri_asset_tick` (already task-based) gain a web path;
desktop is unchanged:

- **Desktop:** `wgri_fs_is_ready()` is always true; ensure = `wgri_fs_exists(path)`
  (today's fopen check, routed through wgr_fs) → fire callback.
- **Web:** the ensure task advances through states in `wgri_asset_tick`:
  1. wait for `wgri_fs_is_ready()` (restore barrier),
  2. `wgri_fs_exists(path)`? → ready,
  3. else fetch from the asset host (sokol_fetch) → `wgri_fs_write(path)` →
     `wgr_fs_flush()` → ready,
  4. fire `on_ready(path)`; the sync `wgr_*_create(path)` now reads a local file.

The path-only callback contract is what makes this invisible to user code.

### What to crib vs. skip (from rl_fs / fileio / wgutils)

`rl_fs` sits on a `fileio_*` + `fetch_url_op_t` underlayer (the wgutils-derived
wasm/desktop abstraction). We can vendor it, but likely don't need to:

- **Crib:** the `EM_ASYNC_JS` idbfs-sync shim, the restore/flush
  barrier+timeout+fallback logic, and the IDBFS mount/setup sequence. These are
  small and the tricky, proven part.
- **Reuse what we already have:** sokol_fetch for HTTP (already a dep) instead of
  `fetch_url_op`; our asset-task pool + `wgri_asset_tick` instead of rl_fs's task
  pool.
- **Skip (for now):** the LRU memory cache, dependency/batch prefetch state
  machine, host-ping. Not needed for basic ensure.

### Build prerequisite (done — step 1/2)

The Emscripten target needs `-sFORCE_FILESYSTEM` and `-lidbfs.js` for the idbfs
store, plus `-sALLOW_MEMORY_GROWTH=1`. **No `-sASYNCIFY` and no `-sJSPI`** — the
restore is a polled barrier (callbacks), so no stack-unwinding mechanism is
linked. (sokol_app owns the loop; suspension isn't available in its callbacks.)

### Phasing (locked order: 2b → 2a → 2c) — all DONE

(Steps below are kept for the record; all three landed. Note step 1's `-sJSPI`
was later dropped — `sapp_run` owns the loop, so the idbfs restore is a polled
barrier, not a JSPI await. See "Decisions locked".)

Toolchain is the dominant risk and is independent of `wgr_fs`, so stand up wasm
first and get the in-browser feedback loop before adding fs complexity. Good news
already confirmed: `wgr_run` uses `sapp_run`, so sokol_app drives the web main loop
(`emscripten_set_main_loop`) for us — no run-loop port needed. emcc 5.0.7 is
installed (`~/toolchains/emsdk`).

- **Step 1 (was 2b) — wasm/JSPI target + serve loop.** Build a **no-asset** example
  (`hello`) with emcc: `-DSOKOL_GLES3`, `-sUSE_WEBGL2=1`, `-sJSPI`,
  `-sALLOW_MEMORY_GROWTH=1`, `-o examples/build/web/hello.js`. Serve it and confirm
  renders in a Chromium-class browser. Desktop build untouched. (No files yet, so
  no `FORCE_FILESYSTEM`/idbfs — that's step 3.)
- **Step 2 (was 2a) — `wgr_fs` desktop seam.** Add `wgr_fs` (real-dir backend), route
  ensure's existence check behind `wgri_fs_exists`; `wgri_fs_is_ready()`→true on
  desktop, web path a `__EMSCRIPTEN__`-gated stub. Behavior-preserving; both
  targets build.
- **Step 3 (was 2c) — web idbfs + fetch.** IDBFS mount, restore/flush barriers
  (cribbed JSPI `EM_ASYNC_JS` shim), and ensure's `wait-ready → exists? → fetch →
  write → flush` path. Adds `-sFORCE_FILESYSTEM -lidbfs.js`. Asset examples then
  run in-browser.

### Decisions locked
- **Order:** 2b → 2a → 2c (above).
- **Crib, don't vendor.** Pull the idbfs sync shim + barrier logic into a small
  `wgr_fs`; use sokol_fetch (XHR on web) + our existing asset-task machinery rather
  than importing wgutils wholesale.
- **Desktop never regresses.** Web code is `__EMSCRIPTEN__`-gated.
- **No JSPI / no ASYNCIFY — async polled barrier instead.** ⚠️ Supersedes the
  earlier "lean on JSPI" call. Finding (verified in-browser): JSPI can only
  suspend when the wasm export called from JS is promising-wrapped, but
  **`sapp_run` owns the emscripten RAF loop**, so init/frame callbacks aren't
  promising — suspending in them throws `SuspendError: trying to suspend without
  WebAssembly.promising`. librl could use JSPI because it drove its *own* tick
  loop; sokol_app doesn't expose that. So `wgr_fs` restore is a **polled barrier**
  (`FS.syncfs` kicked non-blocking; `wgri_fs_is_ready()` reflects a flag; `ensure`
  waits via the existing `wgri_asset_tick` gate). `-sJSPI` is dropped (it only
  narrowed browser support). Reconsider only if we ever drive our own loop
  instead of `sapp_run`.
- **Web backend is parameterized** (`make wasm BACKEND=gl|wgpu`). WebGPU
  (`SOKOL_WGPU`, via emscripten's `emdawnwebgpu` port) was a motivation for
  choosing sokol and is a first-class target — but the same libwgrender source compiles
  to either, so we get **first light on WebGL2** (`SOKOL_GLES3`, lowest risk,
  validates canvas/loop/JSPI/serve/fs) and bring up **WebGPU as a sibling target**
  right after, since it adds async device init + the Dawn port on top.
- **Host fetch is required on web** (not optional): first run has an empty IDBFS
  cache, so every asset is fetched from the serving origin then cached. Port a
  `wgr_asset_set_host` / `WGR_ASSET_HOST` config in step 3.
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

### Decided
- **`wgr_fs` is internal storage; `ensure` stays public in `wgr_asset`.** `wgr_fs`
  owns local storage only (exists/read/write + idbfs sync) with no network;
  `wgr_asset` owns acquisition (ensure + host + fetch) on top of it. Keeps the
  filesystem free of an HTTP/host dependency (layered, matches librl). Promote
  `wgr_fs` to a public VFS later only if user-facing read/write/save-games is wanted.

### Later
- **`wgr_net` (fetch / websockets).** The web fetch inside `ensure` is really a
  network primitive; eventually a small `wgr_net` subsystem (HTTP fetch, later
  websockets) should own it, and `wgr_asset` would call `wgr_net` rather than
  sokol_fetch directly. Out of scope for Phase 2; revisit when websockets/network
  features land.

*From docs/PLAN-wgr_fs.md.*

## Window and monitor control

Status: **phase 1 implemented (2026-09-17)** with decisions 1–3 and 5 as
recommended; window flags (decision 4) are phase 2. Both built: see "As built".

### Problem

librl had window size and position and monitor queries (through raylib's RGFW
backend). sokol_app has none of these, so they were the last deferred parity items
(`make parity`: 8 todos). libwgrender also has two quiet gaps in the window API today:

- `wgr_window_get_position` always returns (0, 0).
- The window flags `RESIZABLE`, `UNDECORATED`, `TRANSPARENT`, `HIDDEN` and
  `ALWAYS_RUN` are accepted and ignored.

### What we have

`deps/sokol_utils/sokol_app_utils.h` (squk/sokol_utils, zlib license, vendored with
two marked fixes; see its `VERSION`) adds to sokol_app, on Win32, macOS and X11:

```c
void sapp_set_window_position(int x, int y);   void sapp_get_window_position(int *x, int *y);
void sapp_set_window_size(int w, int h);       void sapp_get_window_size(int *w, int *h);
int  sapp_num_displays(void);                  int  sapp_current_display(void);
void sapp_set_display(int index);              const char *sapp_display_name(int index);
int  sapp_display_width(int index);            int  sapp_display_height(int index);
bool sapp_window_focused(void);                void sapp_set_fullscreen(bool enable);
void sapp_set_swap_interval(int interval);     int  sapp_get_swap_interval(void);
void sapp_set_mouse_position(float x, float y);
```

On web almost all of these do nothing (fullscreen works). There is no display
position; X11 (XRandR), Win32 and macOS each have one, so it's a small addition.

### Proposed API (`include/wgr_window.h`)

```c
/* Window, in logical pixels (like wgr_window_get_screen_size). False where the
 * platform can't do it (see "Web" and "Linux"); logged once. */
bool   wgr_window_set_size(int width, int height);
bool   wgr_window_set_position(int x, int y);      /* top-left, desktop coordinates */
vec2_t wgr_window_get_position(void);               /* now real; (0, 0) on web */
bool   wgr_window_set_fullscreen(bool fullscreen);
bool   wgr_window_is_fullscreen(void);
bool   wgr_window_is_focused(void);

/* Monitors: 0 .. count - 1. */
int         wgr_window_get_monitor_count(void);
int         wgr_window_get_monitor(void);                    /* the one the window is on */
bool        wgr_window_set_monitor(int monitor);             /* move the window there */
vec2_t      wgr_window_get_monitor_size(int monitor);        /* logical pixels */
vec2_t      wgr_window_get_monitor_position(int monitor);    /* desktop coordinates */
const char *wgr_window_get_monitor_name(int monitor);
```

- librl's `get_monitor_width` and `get_monitor_height` merge into
  `wgr_window_get_monitor_size`, like `wgr_window_get_screen_size`.
- Fullscreen, focus and monitor names are new (librl only had the fullscreen flag
  at startup). Runtime vsync (`sapp_set_swap_interval`) is left out for now; it
  interacts with frame pacing and `WGR_WINDOW_FLAG_VSYNC_OFF` and deserves its own
  small design.
- Mouse warping (`sapp_set_mouse_position`) is left out; it belongs with input.

#### Web

- Size: resizes the canvas (its CSS size; sokol follows it). The page layout can
  still override it, so it returns true only when the canvas took the size.
- Position, monitors other than 0: return false / (0, 0). Monitor 0 is the screen
  (`window.screen` size), the name is "".
- Fullscreen: sokol's fullscreen toggle (needs a user gesture in browsers).

#### Linux

sokol_app is X11-only, so on Wayland desktops libwgrender runs through XWayland, where
compositors ignore a program positioning its own window. Built first as "return true,
the window may not move"; changed (2026-09-19) so moves and `wgr_window_set_monitor`
return false there, and the position reads (0, 0): libwgrender finds XWayland by its X
extension (`sapp_can_move_window`, `[libwgrender]` in sokol_utils) and logs why once.
Resizing, fullscreen and hiding work.

### Window flags

| Flag | Proposal |
|---|---|
| `RESIZABLE` | Honor: without it, the window gets a fixed size (X11 size hints, Win32 style, macOS style mask) |
| `UNDECORATED` | Honor: X11 `_MOTIF_WM_HINTS`, Win32 `WS_POPUP`, macOS borderless style |
| `HIDDEN` | Honor: create unmapped / hidden; add `wgr_window_show(bool)` |
| `ALWAYS_RUN` | Remove: sokol_app never pauses on desktop, and browsers throttle background tabs regardless |
| `TRANSPARENT` | Web only (`composite_mode`); desktop needs an alpha framebuffer config from sokol's GL setup. Honor on web, document desktop as unsupported |

The honored flags need platform code next to sokol_app's window creation, in the
vendored utils header (marked `[libwgrender]`), and run after `sapp_run` has created the
window, so a visible window may flash in its default style first on some platforms.

### Decisions

1. **API as above** (merged monitor size, fullscreen/focus/name added, vsync and
   mouse warp later). Recommend: yes.
2. **Coordinates:** window and monitor sizes in logical pixels (DPI-scaled, like the
   rest of libwgrender); positions in the OS's desktop coordinates (unscaled on X11 and
   Win32, points on macOS). Recommend: yes; a fully DPI-consistent desktop space
   isn't possible across monitors with different scales.
3. **Unsupported platforms return false** (logged once) rather than pretending.
   Recommend: yes.
4. **Window flags:** honor `RESIZABLE`, `UNDECORATED`, `HIDDEN` (+ `wgr_window_show`);
   remove `ALWAYS_RUN`; `TRANSPARENT` web-only. As a second phase after the
   functions. Recommend: yes.
5. **Testing:** Linux (X11 via XWayland here: position may be ignored) and web in
   the browser; Windows and macOS use sokol_utils' code untested by us. A unit test
   on the headless build checks the fallbacks; an example (`examples/window.c`)
   lists monitors and moves/resizes the window with keys. Recommend: yes.

### As built (phase 1)

- `sapp_display_position` added to the vendored header (`[libwgrender]`; X11 XRandR, Win32,
  macOS with y flipped to top-down).
- Tested with a scratch program calling each function over frames:
  - **Desktop (XWayland on COSMIC, 2 monitors):** monitors, names and positions
    correct (DP-3 1920x1080 at (1920, 0), HDMI-A-1 at (0, 0)); resizing and
    fullscreen work; moving the window and `set_monitor` are ignored by the
    compositor, as expected.
  - **Xvfb (plain X11, no window manager):** moving works, `set_monitor` centers the
    window; fullscreen does nothing without a window manager.
  - **Web:** webcheck on WebGL2 and WebGPU (the `window` example starts cleanly);
    keys aren't exercised automatically.
  - Windows and macOS: sokol_utils' code, untested by us.
- The same test found a desktop quit abort in sokol_audio (the device callback
  called `saudio_sample_rate` during shutdown), fixed in `wgr_audio.c`.

### As built (phase 2: window flags)

- Applied from libwgrender after sokol_app made the window, through `[libwgrender]` functions in
  the vendored `deps/sokol_utils` header (`sapp_set_window_resizable`,
  `sapp_set_window_decorated`, `sapp_set_window_visible`, `sapp_window_visible`), not
  a sokol fork change: it keeps the fork small enough to upstream by hand. The cost is
  that a hidden window can show for a moment first.
  - X11: a fixed size is minimum = maximum in the size hints, moved along by
    `wgr_window_set_size`; no decorations is the `_MOTIF_WM_HINTS` property; hidden is
    unmapped.
  - Win32: the window style (no resize frame or maximize box; a popup when
    undecorated), keeping the client size; `ShowWindow`.
  - macOS: the window's style mask; `orderOut` / `makeKeyAndOrderFront`.
  - Web: only visibility (the canvas's CSS visibility); the page lays out the canvas.
  - The style is set again on leaving fullscreen (Win32's toggle resets it), and the
    size limit lifted on entering it.
- `RESIZABLE` has raylib's meaning (decided: without it the window keeps its size);
  every example sets it.
- `TRANSPARENT` is sokol_app's premultiplied composite mode: blended drawing already
  produces premultiplied output, and libwgrender premultiplies the screen's clear color, so
  clearing to any color with alpha 0 shows what's behind.
- `ALWAYS_RUN` removed.
- Window positions use static gravity in the size hints (sokol_app sets center), so a
  position set is read back the same under a window manager's frame: before, (120, 90)
  read back as (121, 116) under Openbox, and each move drifted.
- Tested: in Xephyr with Openbox (a real X11 window manager): a fixed window offers
  no resize or maximize, an undecorated one has no frame, a resizable one offers both,
  and positions round-trip; Xephyr reports one monitor even with Xinerama, so changing
  monitor isn't tested. On Xvfb with `xprop` / `xwininfo` (size hints fixed and
  following a resize, Motif hints, unmapped when hidden and mapped when shown); on the
  COSMIC desktop
  (XWayland) the hints reach the window manager, which publishes no frame or
  allowed-action properties to confirm what it does with them; web transparency by the
  page's pixel through a transparent window (and red through an opaque one). Windows
  and macOS: written, untested.

### Order

1. Functions (decisions 1–3) with the `window` example; parity map updated.
2. Window flags (decision 4).

*From docs/PLAN-window.md.*

## Tasks done

TASKS.md's ticked items, by the section they were in.

### Bugs and measurements

- [x] Bug: models ignore glTF `alphaMode` (BLEND/MASK). gumshoe's `blobShadow`
      (BLEND, alpha 0.2) drew as a solid black quad. Fixed: MASK discards below
      the cutoff, BLEND and faded models (tint alpha < 1) draw in a sorted blended
      pass, `doubleSided` disables culling
- [x] Bug: static (unskinned) glTF primitives ignored their node transform
      (gumshoe's `blobShadow` node is scaled 0.66 and offset). Fixed: node world
      transforms are baked into positions, normals, pick data and bounds at load
- [x] Bug: `wgr_set_target_fps` did nothing. Fixed: frames are vsync-locked by
      default; the target caps below that (desktop sleeps, web skips early browser
      frames); `WGR_WINDOW_FLAG_VSYNC_OFF` (was `_VSYNC_HINT`) unlocks on desktop.
      Measured: desktop vsync off at 144/20 fps, web at 30 and capped at 60
- [x] Frame `dt` came from sokol's smoothed frame duration, which drifted under the
      irregular swaps above (summed dt was 0.88 of wall time). Now measured from our
      own clock: summed dt matches wall time with vsync on/off, capped or not
- [x] Bug: animated models were picked against their rest (T) pose. Fixed: pick
      geometry is skinned on the CPU with the current joint matrices when a pick
      needs it (cached per pose), and the broadphase uses the posed bounds
- [x] Bug: invisible parts of transparent materials were pickable (gumshoe's blob
      shadow). Fixed: MASK/BLEND hits need material alpha (texture alpha at the
      hit UV x base color alpha, tint ignored) at or above the MASK cutoff, or 0.5
      for BLEND. The shadow (at most 20% opaque) is no longer pickable at all.
      Pick-grid check after the fix: no picked-but-not-drawn points on any object
- [x] Bug: orthographic cameras only affected sokol_gl content; models and
      picking always used perspective (fovy 6 world units became a 6 degree FOV,
      so models drew hugely magnified and picks missed). Fixed: one
      `wgri_camera3d_projection` / `wgri_camera3d_view` used by sokol_gl 3D mode,
      models and picking
- [x] Audio regressions from librl fixed: long audio streams (music create 215 ms →
      5 ms, ~108 MB → 6 MB) and mixing runs on the audio device's thread
      ([PLAN-audio.md](#audio--streamed-music-and-mixing-off-the-main-thread))
- [x] Bug: sokol's default pools (128 buffers, images) made Sponza fail to load;
      pools raised, and a failed GPU buffer or image fails the load
- [x] Bug: `wgr_request_quit` on web aborted in sokol_audio when the main thread
      had been busy: audioprocess events queued meanwhile ran after shutdown and
      asserted on the freed buffer. Fixed in libwgrender's sokol fork
      (github.com/robknopf/sokol: the handler is cleared on shutdown); sokol is now
      vendored from the fork with `tools/update_sokol.sh`. `examples/quit.c` (music,
      loads in flight, a busy frame, quit) keeps webcheck on this path
- [x] Bug: quitting a desktop program with audio could abort in
      `saudio_sample_rate` (asserts once `saudio_shutdown` has begun, while the device
      thread still asks for a buffer). The callback uses the rate cached at init
- [x] Shadows on WebGPU (2026-09-21): were fully shadowed everywhere. Not a WebGPU
      problem: the depth pass never asked for its depth buffer to be kept, sokol's
      default store action for depth is DONTCARE, WebGPU honours the discard and GL
      only treats it as a hint. One explicit store action. `make webcheck` now records
      the browser's own log entries (Dawn's validation messages live there, not in the
      console API) and `--verbose` prints them (PLAN-shadows.md, "The WebGPU bug")
- [x] Shadow cost measured (2026-09-21): `make shadowbench [DESKTOP=1]`
      (tools/bench/shadowbench.c, also a web page). RTX 4080, vsync off, frame ms (and
      the CPU ms in it) at 100 / 400 / 1000 models: none 0.25/0.62/1.41; sun at 1024
      0.42/0.82/1.94, at 2048 0.33/0.90/1.95, at 4096 0.38/1.03/2.06; sun + spot
      0.45/1.11/2.36; nothing receiving 0.30/0.79/1.60 (the pass is skipped), and at
      4000 models none 5.18, sun 6.99, two lights 8.57. The CPU cost barely moves with
      the map's size, so that axis is GPU fill (16x the pixels: ~+0.05 / +0.12 ms),
      while the extra pass over the casters is CPU and scales with them. Receiving
      costs ~0.02. Which dominates is a property of the scene
- [x] Bug (device): the emitter's shader didn't link on Adreno 610 (2026-09-20; moto g
      power 2021, GLES 3.2 V@0502.0, driver dated 2020-12-29). The driver's own compiler
      gave up -- `Assertion failed: GVI && "cannot compute gv size for oob (no global
      info)"` -- so sokol reported GL_SHADER_LINKING_FAILED and `particles` simulated
      (2001 fountain, 518 sparks, 60 FPS) while drawing nothing. Bisected on the device
      by linking variants of the generated glsl300es in a bare WebGL2 page: the one
      construct that trips it is the *array* index in `size_times[i / 4]`, where shdc
      has flattened the block to `uniform vec4 particle_params[33]`. The driver clamps a
      dynamic index into that array only when it can see the index is constant; a
      divided index it can't bound, and -- the part that cost a second round -- a branch
      around the read hides it just as well, so the obvious `i < 4 ? a : b` fails too
      (shdc turns a ternary into if/else + phi). Both halves are now read unconditionally
      and selected with `mix`/`step`, which shdc keeps branch-free. `params[i + 17]` and
      the dynamic component `q[i % 4]` were never the problem. All ten glsl300es programs
      link on that phone and `particles` draws at 60 FPS

### From libwgt

- [x] WebGPU clips depth 0..1: the 3D camera's projection goes through
      `wgri_render_clip_depth` where it reaches the GPU; CPU math keeps GL's -1..1.
- [x] Shadows on the same rule: light matrices -1..1 on the CPU (caster culling was
      wrong on WebGPU), converted only for the depth pass.
- [x] Tools named verb-first (`run_`, `check_`, `build_`, `measure_`...); modules that
      tools import keep nouns. Every tool takes `--help` (prints its usage and does
      nothing else) and stops on an argument it doesn't take, through the Haxe
      binding's `cli.py`, now `tools/cli.py` (`buildweb.py --help` ran a build).
      `check_rules.py` holds both: names, and every tool run both ways. The binding
      runs the root's web tools instead of its copies of them.
- [x] No tool reads source as text. One module reads the public headers with clang's
      JSON AST (functions, enums, structs, defines, doc comments), and every tool that
      needs to know what's in them uses it: `tools/check_rules.py` (with libwgt's
      self-test: a header that breaks every rule), the binding's `gen_raw.py` (its whole
      C surface comes from regexes over the headers today), `gen_keys.py` and
      `check_refusals.py` (its C half already uses clang). The binding's own members
      (`check_coverage.py`, `members.py`) from the Haxe compiler: a macro writes them as
      JSON. Backend-free headers and examples by compiling them without `deps/`;
      `tools/pack_shader.py` from sokol-shdc's reflection; `build_hxcpp_web.py` takes
      `--main` as data instead of reading the hxml. Regenerated binding files must come
      out the same. Where nothing parses it, raise it rather than scan.
      Done that way, with two choices of the user's: the _ptr rule covers wgr_/wgri_
      functions (clang's name filter keeps src/ to seconds; file-local helpers are
      outside it), and pack_shader.py reads SPIR-V for the parameters (shdc reports
      no block members) and shdc's --dump for the sections; if those prove flaky,
      fork sokol-tools to put both in its reflection. New check it brought: public
      macros carry the prefix (wgr_logger.h's log_* are MACROS_TODO).
- [x] Build names as libwgt's: platform is what a program links against, with the
      architecture (`linux-x64`, `macos-arm64` only, `windows-x64-msvc`,
      `windows-x64-mingw`, `wasm32`); the variant names its config always, then what it
      adds (`debug-headless`, `release-threads`, `release-webgpu`), never `-nothreads`:
      the web default is now no threads. Tests run on debug builds.
      `out/<platform>/<variant>/` holds `lib/` and `bin/` (or `site/` on the web),
      written by the build itself -- no install step, no copies of the headers -- so
      deleting `out/` is a clean; work in `build/<preset>/`. `webdeploy` is a tracked
      custom command. The Haxe binding's builds follow it (`-hxcpp` for its hxcpp web
      build).
- [x] `tools/run_remote_windows.py`: build and test the working tree on a Windows
      machine over ssh, MinGW or `--msvc`, leaving nothing there (ported from libwgt's;
      MinGW builds there use the pinned compiler below).
- [x] A pinned MinGW-w64 on Windows hosts (`tools/setup_mingw.py`, libwgt's): the
      toolchain file sets up a WinLibs GCC in the per-user cache and builds with it,
      instead of the `gcc` on PATH (which was choosenim's, Nim's); the build's
      workaround for that shim's missing `ar` is gone.
- [x] The Haxe binding moves in as `bindings/haxe` (history kept); its CI and Pages
      join the root's, `tools/verify_builds.py` runs its suite. wgrender-beef is dropped and
      wgrender-nim set aside.
- [x] `docs/HISTORY.md`: what's done moves out of TASKS.md and the PLAN files, so they
      show only what's current.

### Core runtime

- [x] Colors are values (2026-09-17): `wgr_color_t` is packed 0xRRGGBBAA, so a tint
      can be computed per frame (`wgr_color_rgba`, `wgr_color_with_alpha`,
      `wgr_color_lerp`) instead of pre-creating a palette, and the 256-slot pool,
      the handle kind and the color lifecycle are gone ([PLAN-color.md](#colors-are-values-not-handles))
- [x] Fixed-rate tick (`wgr_set_tick`) + timing passed to callbacks (`dt`,
      `tick_fraction`); `wgr_get_delta_time` removed; input edges relative to the
      running callback. Resolves the frame-timing decision below
      (docs/PLAN-tick.md, examples/tick.c)
- [x] Decided (by the tick design): time accumulated from frame `dt` runs slow
      when frames stall, because `dt` is capped at 0.1 s. Simulation belongs in a
      tick, which uses real elapsed time and catches up (up to 5 ticks per frame)
- [x] Web quit no longer blocks the page waiting for asset workers ("Blocking on the
      main thread"): they're detached and end on their own
- [x] Limits that grow (2026-09-18): the sprite3d and sprite2d pools start at 256
      and double up to 65,534 (the handle's 16-bit index); the scene's transparent
      list doubles as needed; sokol_gl's per-frame vertex and command budgets double
      after a frame that ran out (logged: that frame lost its draws past them), up to
      1M vertices and 256K commands. Freed handle slots are reused oldest first, so
      churn no longer wraps one slot's 10-bit generation. spritebench: 32,768 sprites
      in every scene with the default build (was 1,024 sprite3d)
- [x] Every handle pool grows (2026-09-18): textures, meshes, models, materials,
      fonts, text2d/3d, shapes, lights, cameras, scenes, environments, audio, sounds
      and asset tasks start small and double up to 65,534. Along the way: the mixer
      walks the sound pool under its lock instead of a 128-entry pointer list, so
      sounds past the 128th are no longer silent; asset job queues grow with the
      tasks; web downloads are capped at 256 at once (sokol_fetch's pool) and the
      rest wait instead of failing; stale task pointers after queueing dependencies
      are gone

### Rendering

- [x] Scene layers + render passes (librl had these; libwgrender kept only the API).
      Done: per layer an opaque pass, then one transparent pass sorted back to
      front across model primitives, sprites and translucent shapes (runs stay
      batched, unlike librl's per-item flush). Draw order follows call order
      across sokol_gl and model draws (frame command list in `wgr_render`)
- [x] 2D scene drawables (sprite2d, text2d in a scene) draw after all 3D layers
- [x] Verified picking against rendering under transforms (pick-grid vs screenshot:
      a 4 px grid over 960x540): rotated + non-uniformly scaled box and ellipsoid,
      a scaled sprite with alpha test, and a rotated/scaled static model all match
      except anti-aliased edges
- [x] Built-in font (2026-09-17): the 8x8 sokol_debugtext bitmap font (KC85/3, whose
      `[ ] \ { | } ~` were graphics and umlauts) is replaced by JetBrains Mono, an
      ASCII subset embedded in the library (`src/fonts/wgr_default_font.h`, generated
      by `tools/gen_default_font.py`, OFL). All text is TrueType now; sokol_debugtext
      is gone (web size about even: -12.5 KB code, +9 KB font).
      `wgr_text_set_default_font` sets another default (e.g. for UTF-8), used by
      `wgr_text_draw` and font handle 0 everywhere, including text3d
- [x] Lighting: light objects (directional, point, spot), per-scene lights and
      ambient, up to 8 lights per model by contribution, nothing lit implicitly
      (docs/PLAN-lighting.md, examples/lights.c). `simple.c` lighting PARITY note
      removed
- [x] Parity batch (2026-09-16, [PLAN-parity.md](#remaining-librl-parity), `examples/text3d.c`):
      `wgr_pick_object` + pick stats + pickable flags everywhere; `wgr_text3d_*` and
      `wgr_text_draw_3d`; 3D rectangles, circles, lines and point-by-point line
      strips; animation duration/time in seconds and `wgr_model_is_ready`;
      `wgr_sound_set_pan`; sprite3d getters and FREE facing; `wgr_text_draw_fps_ex`;
      `wgr_asset_get_host`. Dropped: built-in font handle, placeholder model, ground
      texture drawing. `make parity`: 95%, 10 todos, all deferred on purpose
- [x] 2D sprites and screen-space texture drawing: `wgr_sprite2d_*` (source rect,
      pivot, rotation, x/y scale with flip, size, alpha-tested picking) and
      `wgr_texture_draw`; scenes draw 2D after 3D and pick it first; 2D, mouse and
      screen size are in logical pixels (docs/PLAN-sprite2d.md, examples/sprite2d.c)
- [x] Lighting controls: redesigned as light objects in scenes (see above)
- [x] sprite3d / text3d camera facings (2026-09-18) now do what their names say, and
      differ from librl on purpose. `CAMERA` is spherical (parallel to the view plane,
      tilting with the camera's pitch); `CAMERA_FIXED_Y` is cylindrical (turns about
      world Y, stays upright). In librl, `CAMERA` built the quad from `camera.up` — the
      up *hint*, normally (0, 1, 0) — so both modes were upright there; in libwgrender before
      this fix, both tilted. `CAMERA_FIXED_Y` also no longer collapses to an invisible
      zero-width quad when the camera looks straight down
- [x] Destroying an object takes it out of every scene (2026-09-18): members, hover
      and press state, and a scene's camera. It used to stay as a stale handle that
      warned on every draw
- [x] Skinning and per-draw uniforms (2026-09-20): a crowd of animated models spent
      85% of its frame CPU in one call, uploading 128 joint matrices (8.4 KB) as
      uniforms per skinned draw, and would have overflowed the backends' per-frame
      uniform buffer past ~450 skinned draws. Now the frame's joint matrices (only the
      joints a mesh has) go into one float texture, uploaded once before the passes,
      and the skinned shaders read them with texelFetch; the fragment block is split
      into material (every draw), scene and lights (applied only when they change).
      Per draw: ~8.6 KB -> ~430 bytes. Measured in Chromium on the GPU (100 animated
      gumshoes: 15.2 -> 2.1 ms of frame CPU; 400: 19.4 -> 7.8 ms), with three.js 0.186
      at 2.0 and 6.1 ms. `.wgrshader` format 4 (rebuild custom shaders)
- [x] Shadows, phase 1 (2026-09-21, desktop GL, WebGL2 and WebGPU): a directional light casts
      into a depth map before the frame's passes, and models, lit sprites and custom
      shaders (`wgr_shadow`) are darkened by it. `wgr_light_set_casts_shadows` /
      `_shadow_distance` / `_shadow_map_size` / `_shadow_bias` (in texels) /
      `_shadow_strength` / `_shadow_color`, and `wgr_model_set_casts_shadow` /
      `_set_receives_shadow`. Opt in twice: the module links only when a program calls
      one of these, and a light casts only when asked. `.wgrshader` format 6
      ([PLAN-shadows.md](PLAN-shadows.md), `examples/shadows.c`)
- [x] Shadows, phase 2 (2026-09-21): spot lights cast through their own cone (a
      perspective fit), and up to four lights cast at once — one depth array with a
      layer each, since a texture per light would eat the sampler slots custom shaders
      need. Layers share the largest map size asked for; a light's slot rides in a
      spare component of its per-light data, so only the per-slot arrays grow.
      `.wgrshader` format 7 ([PLAN-shadows.md](PLAN-shadows.md), `examples/shadows.c`)
- [x] The model draw queue grows (2026-09-21): it was two fixed arrays, 1024
      placements and 8192 primitives, and a frame past either lost the rest of its
      models after one warning — found while benchmarking, where asking for 1600 and
      4096 models both measured the same 1024. They now double from 64 / 256 up to
      16384 / 131072, in the shape of wgr_scene's transparent list, and the warning is
      kept for the ceiling (about 23 ms of submission, far past playable). Small
      programs stop carrying the room as well: ~448 KB of always-resident memory gone
- [x] Frustum culling, phase 1 (2026-09-21, docs/PLAN-culling.md): a scene tests each
      member's world bounds against the camera's six planes before submitting it, for
      every 3D kind at once through the bounds registry it already keeps for picking.
      A caster the camera can't see is kept when its box, swept along a casting light
      for the reach of that light's map, still touches the view — so shadows from off
      screen stay. Skinned bounds are the rest pose, so a member's box is padded 15%.
      `wgr_scene_set_culling` turns it off (default on) when you want to see everything
      submitted. shadowbench gained "look away" / "away, no cull": 4000 models behind
      the camera cost 6.78 ms a frame before and 0.50 ms now, of which 0.42 is the test
      itself (~0.1 microseconds a member, against the 1.2 it saves); with everything in
      view nothing got slower, because the world matrix a model built twice is now
      cached. 2D members still aren't tested (nothing measured says they need to be)
- [x] Frustum culling, phase 2 (2026-09-21): the depth pass tests each caster against
      the casting light's own fit instead of redrawing every caster in the environment.
      Exact, not merely conservative — a directional fit's side planes are parallel to
      the light and a spot's all pass through it, so a caster outside one cannot shadow
      anything inside, and the fit's pull-back keeps the ones between the light and the
      box. The bounds come free: wgr_model already builds a placement's world AABB to
      pick its lights, and now keeps it. shadowbench at 4000 models, where the sun's
      40-unit reach covers a fraction of a 140-unit grid: a casting light cost ~1.6 ms
      over the same scene with no shadows and now costs ~0.1, two lights ~3.2 ms and now
      ~0.1 — read as the gap to the "off" row in the same run, because a row carries
      about +/-0.3 ms of run-to-run noise (the "off" row alone measured 4.96 to 5.57
      over five runs). At 100 models, where the fit covers everything, nothing moves
- [x] Models are instanced (2026-09-21, docs/PLAN-instancing.md): models that agree on
      everything but where they stand go up as one draw. What differs per placement --
      the matrices and the tint -- moved into a per-frame data texture, eight texels a
      placement, the same trick the joint matrices use; the tint left fs_params for the
      vertex color, which is the same arithmetic in a different place and leaves nothing
      per placement in the material block. Inside an unordered region (what wgr_scene
      already declares for sprites) the items are sorted by what a draw has to set, and
      a run of equal ones becomes one sg_draw. shadowbench gained a "shared" case -- one
      mesh, one material, N placements, the forest -- against "sun 1024", the same scene
      with a material each: at 4000 models, submission 4.71 -> 0.60 ms, CPU 5.89 -> 1.76,
      frame 6.12 -> 3.20, both rows from one run. A scene where every model has its own
      material cannot batch and is unchanged. Still per draw: custom material shaders
      (phase 4) and the shadow pass (phase 3); the scene walk itself, 1.15 ms at 4000
      members, is now the largest CPU cost left
- [x] The shadow depth pass is instanced too (2026-09-21, docs/PLAN-instancing.md phase
      3): casters read their placement and joint base from the same records the shading
      pass does, so a run that agrees on mesh and material goes into the map as one
      draw. The instance block is now src/shaders/wgr_instance.glsl, included by both
      shaders. shadowbench gained "wide, each" / "wide, shared", where the sun reaches
      the whole grid so nothing is culled out of the map: at 4000 models, submission
      6.38 -> 0.60 ms and the frame 8.01 -> 3.20. The depth pass's own share of that,
      measured by forcing same_depth_group false in the same build, is about 1.5 ms
- [x] Custom material shaders instance too (2026-09-21, docs/PLAN-instancing.md phase
      4): shaders/wgr.glsl grew an wgr_vs_instance block, so a custom shader's model
      stages read the placement from the same records; the instance and joint textures
      share the one nonfiltering sampler, since sampler slots stop at 11. The tint
      became a varying rather than folding into wgr_color: wgr_output() applies wgr_tint,
      and a shader that ignores wgr_color would otherwise have silently lost it. Sprites
      write white there, where their tint has always been in wgr_color. .wgrshader format
      7 -> 8, the six example shaders repacked; older packs are refused, not drawn
      wrongly. A custom material no longer blocks batching
- [x] Instanced sprite3d (2026-09-18, [PLAN-sprites.md](#a-sprite-renderer-and-particle-emitters) step 1): one
      record per sprite, quads and billboards built on the GPU, batches as render
      commands; with an indexed scene membership, a radix transparent sort and cached
      batch state, the benchmark field of 16,000 sprites went 2.9 -> 0.9 ms (desktop) and
      9.6 -> 3.6 ms (phone, WebGL2)
- [x] Linear sokol_gl replay (2026-09-18): `sgl_draw_layer` scanned all of the frame's
      commands for each layer, so many layers (sprites or models interleaved with
      shapes and text) replayed in quadratic time. libwgrender's sokol fork adds
      `sgl_draw_layer_range` (branch perf/sgl-draw-layer-range), and wgr_render draws
      each layer's own command range: 3,000 switches replay in 0.6 ms instead of 5.4
- [x] Sprite alpha modes (2026-09-18, PLAN-sprites step 2): `wgr_alpha_mode_t` shared with
      materials; opaque, masked and additive sprites aren't sorted and group by texture
      (16,000 masked sprites from 4 textures: 4 batches, 1.4 ms desktop / 1.2 ms Chrome)
- [x] Blended sprites from several textures on WebGL2 (2026-09-18): without
      base-instance draws, sprites are read by index from a float texture instead of
      rebinding per batch: 16,000 from 4 textures 20.5 -> 11 ms on the phone (sokol_gl:
      13.8), 12.2 -> 7.1 in Chrome
- [x] sprite2d on the instanced sprite path (2026-09-18, PLAN-sprites step 3), with
      `wgr_sprite2d_set_alpha_mode`; immediate `wgr_texture_draw*` stays on sokol_gl
- [x] Render to texture: `wgr_texture_create_target`, `wgr_render_begin_frame/end_texture`,
      `wgr_texture_set_sampling` ([PLAN-render-target.md](#render-to-texture),
      `examples/render_target.c`)
- [x] Screen effects (2026-09-21): post-processing as full-screen shader passes —
      `wgr_render_add_effect` / `_clear_effects` / `_effect_count`, a chain of up to 8
      custom materials whose shaders include `wgr_screen` (one program, `wgr_screen_color()`,
      `wgr_screen_uv`; `.wgrshader` format 5). The frame renders into a render target and
      the chain ping-pongs between two of them onto the screen
      ([PLAN-render-target.md](#render-to-texture), `examples/postprocess.c`)
- [x] Generated meshes (2026-09-20): `wgr_mesh_create_plane/cube/sphere/cylinder/cone/
      capsule/torus`, resources deduplicated by their parameters, with normals, texture
      coordinates and tangents (any material, normal maps and custom shaders included)
      and picking; one white, non-metallic material slot. Geometry in
      `src/wgr_mesh_shapes.c` (pure, unit tested: winding, normals, bounds);
      `examples/meshes.c`; the shaders example's floor and spheres use them
- [x] A render target in a custom shader reads the right way up (2026-10-02): GL and
      WebGL2 store a target bottom-up, and libwgrender turns the coordinate over where
      it samples one (sprites, particles, built-in materials, effects' frame), but a
      custom shader's own textures were never told, so a target given to one was upside
      down there and right on WebGPU. `wgr_texture_uv(binding, uv)` in `shaders/wgr.glsl`
      turns it over from flags in the frame block, by binding. libwgt flips at draw time
      instead (a target stored top-down on every backend, at the cost of reversed-winding
      pipelines in GL target passes), which is the better design: no reader can forget.
      Read-time stays here as the smaller fix for a library folding into libwgt.
      `examples/render_target.c` shows the label on a toon-shaded cube too

### Materials and glTF

- [x] Compressed textures (2026-09-19, [PLAN-textures.md](#compressed-textures)): a program
      loads `name.ktx` and libwgrender picks `name.bc7.ktx` / `.astc.ktx` / `.etc2.ktx` (made
      by `tools/compress_textures.sh`) or `name.png` for the GPU; a 2K texture loads in
      ~1 ms instead of 60-90 ms (desktop) and 1.5 ms instead of 125-200 ms (phone), at a
      quarter of the GPU memory
- [x] Compressed textures in glTF models (2026-09-19): `compress_textures.sh --gltf`
      writes `model.ktx.gltf` with the `WGR_texture_ktx` extension (portable: other viewers
      use the original images); only the variant this GPU can use downloads. FlightHelmet
      on the phone: 2.0 -> 0.45 s in the background, 1.4 -> 0.1 s synchronously
- [x] Materials, phase 1: material resource, glTF metallic-roughness and unlit
      shading, normal/occlusion/emissive maps, sRGB-correct lighting, per-model
      slot overrides ([PLAN-materials.md](#materials-and-shaders), `examples/materials.c`)
- [x] Materials, phase 2 (2026-09-20): custom shaders. A fragment shader (and an
      optional vertex hook) written against `shaders/wgr.glsl`, packed for GL, WebGL2 and
      WebGPU by `tools/shaderpack.py` into a `.wgrshader` file; `wgr_shader_create`,
      `wgr_material_create_custom`, parameters and textures by the shader's names.
      Static and skinned models, scene lights, tint, alpha modes, tone mapping
      ([PLAN-materials.md](#materials-and-shaders), `examples/shaders.c`)
- [x] Environment lighting in custom shaders (2026-09-20): `wgr_environment_diffuse`,
      `_specular`, `_brdf`, `_intensity` in `shaders/wgr.glsl` (`.wgrshader` format 2;
      older files are refused, rebuild them); the shaders example's water reflects a sunset
- [x] Materials, phase 3a (2026-09-20): custom shaders on sprites (2D and 3D):
      `wgr_sprite3d/2d_set_material`, one `.wgrshader` for models and sprites
      (`wgr_sprite_color()`, `wgr_sprite_tex`), batched by material; format 3. Shapes stay
      unlit (generated meshes for lit geometry) ([PLAN-materials.md](#materials-and-shaders))
- [x] Second texture coordinate set (TEXCOORD_1), chosen per texture
- [x] glTF files with separate buffers and images (`.gltf` + `.bin`/`.png`), and
      `data:` URIs. Ensuring a `.gltf`/`.glb` also ensures the files it references
      (wgr_asset dependency listers), so it works on web.
- [x] Texture transforms per texture (glTF `KHR_texture_transform`, or `<t>_offset`,
      `_rotation`, `_scale` by name); picking applies them
- [x] Vertex colors (COLOR_0) multiply base color (and alpha, for picking)
- [x] Sampler modes per texture: wrap (repeat, clamp, mirror) and filter, from glTF
      or `wgr_material_set_texture_sampling`
- [x] Mipmaps for all textures (generated at load)
- [x] Missing or broken glTF images: the model still loads (warning); color
      textures use the placeholder texture (built-in magenta checker,
      `wgr_texture_set_placeholder`), data textures stay empty. Missing buffers still
      fail. Ensured dependencies can be optional.
- [x] Environment lighting: `wgr_environment_create` (.hdr/PNG/JPEG equirect),
      `wgr_scene_set_environment/background/tonemap`, SH irradiance + GGX-prefiltered
      cubemap + BRDF table, background skybox, tone mapping (Neutral default, ACES)
      and exposure ([PLAN-environment.md](#environment-lighting-image-based-lighting-and-tone-mapping), `examples/environment.c`)

### Particles

- [x] Particle emitters, 3D and 2D, simulated on the GPU (2026-09-18, PLAN-sprites
      step 4): `wgr_emitter3d_*` / `wgr_emitter2d_*`, configured in code; particles written
      once at birth and moved by the GPU; `examples/particles.c`. 16,000 particles:
      CPU 1.1 -> 0.1 ms (desktop), 6.2 -> 0.6 ms (phone)
- [x] More for particles (2026-09-18, PLAN-sprites step 5), still stateless: drag,
      stretch along the motion, inherited velocity and spawning along a moving
      emitter's path; size and color curves (8 keys), a palette; flipbooks, prewarm, a
      spawn sphere / circle

### UI and input

- [x] Gamepad input (2026-09-19): `wgr_input_get_gamepad_button/axis`, up to 4 pads by
      slot, buttons by position with frame and tick edges, sticks with a dead zone,
      triggers as axes and buttons; an optional module (`src/wgr_gamepad.c`). Web: the
      Gamepad API; Linux: evdev (the `xpad` driver's X/Y codes swapped), rescanned for
      hot-plugging; Windows: XInput (compiles; untested with a pad). Checked on a
      wired Xbox 360 pad, native and in Chrome;
      `examples/gamepad.c`
- [x] Touch input (`wgr_input_*`, 2026-09-18): the first finger drives the pointer
      (since 2026-09-17), and a second one cancels its press (released off-screen,
      no click); every finger with ids, edges and deltas (`wgr_input_get_touch`), and
      the two-finger pan / pinch / twist (`wgr_input_get_touch_gesture`), per frame
      and per tick. `examples/touch.c`; checked with CDP touch events and on a Pixel
      9 Pro XL (`tools/serve.py --tls` for a secure page on the LAN)
- [x] 2D / UI layer: `enabled` and pointer interaction per scene member, touch as a
      pointer, retained 2D shapes, nine-slice sprites, text alignment and wrapping,
      per-layer clipping, and sprite3d source/extent/pivot for 2D worlds on an
      orthographic camera ([PLAN-2d.md](#2d--ui-layer), `examples/ui.c`, `examples/tilemap.c`)
- [x] UI through the public API ([PLAN-ui.md](PLAN-ui.md), 2026-09-18): float immediate
      2D, rounded rectangles and borders, source-rect and nine-slice images, a nesting
      clip stack, length-taking text, crisp high-DPI glyphs, a growing glyph atlas, UI
      pointer/keyboard capture, a float two-axis wheel; `examples/clay.c` lays out UI
      with Clay and draws it through ~100 lines of public-API glue
- [x] UI widgets (2026-09-21, decided as in ROADMAP "GUI direction"): the hand-built
      button, progress bar and scrolling list moved out of `examples/ui.c` into a shared
      `examples/ui_widgets.h` (a theme, `ui_button_*`, `ui_bar_*`, `ui_list_*`; each
      widget labels on the layer above the one it's given). No widget API in the core: a
      real widget layer belongs outside it, like the Clay glue, and anything it can't
      express through the public API (focus order, text-field editing, clipboard) is a
      core gap to fix there. `examples/ui.c` looks the same as before, bar an empty
      progress bar no longer showing its knob

### Assets and loading

- [x] Loading pipeline (2026-09-17, [PLAN-pipeline.md](#loading-pipeline-background-preparation-budgeted-gpu-upload),
      `examples/loading.c`, `make loadbench`): textures, meshes, environments and
      audio decode on worker threads and upload within a per-frame budget before
      the asset callback; asset groups and progress; threaded web build
      (`WEB_THREADS=0` without). Loading Sponza + FlightHelmet: worst frame 1.24 s
      → 17 ms (desktop, headless) and 1.98 s → 70 ms (WebGL2)
- [x] Web file cache per file (2026-09-20, [PLAN-wgr_fs.md](#wgr_fs--web-capable-ensure-phase-2)): the phone's
      ~100 ms frame while a model loaded was IDBFS restoring the whole cache (56.5 MB)
      at startup, not loading or shaders (first draws of loaded models cost nothing
      extra). Now only the cache's list of files is read at startup and a file is read
      when it's ensured: FlightHelmet's worst frame 60-85 -> 27-30 ms
- [x] Assets: ensure many files at once: asset groups (`wgr_asset_group_create`,
      `wgr_asset_group_add`) with `wgr_asset_get_progress`
- [x] Assets: host ping (2026-09-20): `wgr_asset_ping_host(host, timeout_ms, on_done,
      user)`, asynchronous (librl's blocked, and did nothing on the web): a timed HEAD
      request on the web (any response counts, no CORS needed); on desktop, whether the
      asset directory exists
- [x] Desktop downloads its assets (2026-09-20): a URL asset host is a fetch origin on
      desktop too, and `wgr_asset_set_fetcher` lets the program supply the downloader --
      libwgrender names a URL and a destination file, the fetcher writes it, bytes never
      cross, so the core still has no HTTP and no TLS. Downloads land in a cache dir
      (`wgr_asset_set_cache_dir`, default `.wgr-cache`) and the next run reads them
      there, which is what the browser's cache does on web. examples/fetch.c shells out
      to curl and pulls from the same `make serve` origin the web build uses; the unit
      test needs no network, since a fetcher that writes the file itself satisfies the
      whole contract. Still stubbed on desktop: ping and URL redirect rules
- [x] Web assets survive a host that compresses (2026-09-20): models and fonts loaded
      from GitHub Pages arrived as raw gzip, so every model in every example failed to
      parse -- on a new Pixel as readily as a 2021 moto g, because it was never the GPU.
      sokol_fetch streams with HTTP Range, sized from a HEAD; a compressing host answers
      the HEAD with the *compressed* length and a ranged GET with bytes the browser will
      not decode, and JS cannot ask for identity (Accept-Encoding is a forbidden header,
      measured: the browser drops it). PNGs were fine only because Pages does not bother
      compressing them. Web downloads are now one plain unranged GET through our own
      EM_JS shim: the browser decodes, arrayBuffer().byteLength is the true size, so
      nothing has to be known in advance and there is no per-file cap. sokol_fetch is
      gone from the web path, which also removes a HEAD round trip per asset
- [x] A cached asset can be dropped (2026-09-20): wgr_asset_evict(path) and
      wgr_asset_clear_cache(), and libwgrender drops a cached file by itself when a
      loader rejects it and fetches once more. Found the hard way: the corrupt files
      above were cached in IndexedDB, so the fix alone would not have healed a browser
      that had already visited. librl had rl_fs_remove/rl_fs_clear and tools/parity.map
      dropped both as "wgr_fs is internal" -- true of the filesystem, wrong about the
      capability, which is the failure mode "parity is functional, not 1:1" warns about.
      The map now points them at the asset-level functions
- [x] Asset redirects (2026-09-20): `wgr_asset_add_redirect(prefix, target)` /
      `wgr_asset_clear_redirects`. Path rules stack, newest first, then the file itself,
      so a file missing under a mod or a translation falls through (quietly; a 404 each
      on the web); a target with "://" is where files download from (web). They apply to
      ensured files and the files those reference: a model reads its buffers and images
      from where the asset layer found them (`wgri_asset_found_path`). Plain prefixes;
      wildcards if a game needs them
- [x] Networking decided (2026-09-20): libwgrender fetches assets only (desktop HTTP(S)
      through the OS's clients, deferred); WebSockets and general networking go in a
      separate library outside libwgrender (ROADMAP "Future")
- [x] Fonts want a .ttf/.otf loader (2026-10-02, phase 1 of
      [load on create](#load-on-create-and-polled-tasks-instead-of-callbacks)): a font
      loads on create through a registered loader, read on a worker; a file fontstash
      won't take fails the load, so the asset layer fetches it once more, as it heals
      any other file's bad cached copy

### Platform

- [x] Window flags accepted but ignored (2026-09-19): honored now (below)
- [x] Window and monitor control (2026-09-17, [PLAN-window.md](#window-and-monitor-control),
      `examples/window.c`): size, position, fullscreen, focus, monitors, through
      `deps/sokol_utils` (squk/sokol_utils, vendored with fixes)
- [x] Window flags (2026-09-19, PLAN-window.md phase 2): `RESIZABLE` (without it the
      window keeps its size, as in raylib; the examples set it), `UNDECORATED`,
      `HIDDEN` with `wgr_window_set_visible` / `wgr_window_is_visible`, `TRANSPARENT`
      (sokol's premultiplied compositing; the screen's clear color is premultiplied);
      `ALWAYS_RUN` removed. Applied through the vendored sokol_utils header after the
      window exists, so a hidden window can show for a moment first
- [x] Windows builds (2026-09-19): `make windows` cross-compiles the library and
      examples with MinGW (OpenGL), `make windows-test` / `windows-smoke` run the unit
      tests and headless examples under Wine or Steam's Proton (`tools/wine.sh`);
      `make verify` builds it when MinGW is installed. Two compile fixes (fontstash
      needs windows.h; `_mkdir`). All 100 tests and 26 examples pass under Proton 11,
      including the Windows threads (asset workers); `hello` and `model` also draw
      correctly windowed under Wine
- [x] High-DPI by default (2026-09-18): windows render at the display's full
      resolution; `WGR_WINDOW_FLAG_LOW_DPI` opts out (fewer pixels to fill).
      Replaces `WGR_WINDOW_FLAG_WINDOW_HIGHDPI`

### Infrastructure and tooling

- [x] `make parity`: librl → libwgrender API parity report (`tools/parity.sh`, `tools/parity.map`)
- [x] Unit test setup: `make test`, `tests/unit/` (no stubs, links the headless library);
      first tests cover the handle pool, matrix math, picking math and the
      transparent sort. They found two pick-normal bugs (fixed)
- [x] Unit tests for the gaps (2026-09-21): `wgr_fs` paths and files (root joining,
      absolute paths, reading, writing, the directories a write makes), animation
      sampling (the posed joint matrices: interpolation, wrap, clamp, speed),
      sprite alpha-test picking (and the CPU alpha mask behind it), text2d state
      (font, color, visible / pickable / enabled, an invalid handle), scene layer
      order (which member a pick finds), and the render command list (model runs
      merging, what stops them merging, per-pass isolation, sprite batches, callbacks).
      120 unit tests. Found on the way: a texture made from pixels keeps an alpha mask
      when it has any transparency, so alpha-test picking works on it too
- [x] Sanitizer test builds: `make test SANITIZE=thread|address|undefined` (TSan in CI)
- [x] Faster checks (2026-09-16): `make verify` (~5 s incremental); smoke runs examples
      in parallel (46 s → 4 s); web library compiles once per backend, examples link
      against it (92 s → 2 s cold); webcheck checks 4 examples at a time in isolated
      browser contexts and waits for loading to finish instead of a fixed 5 s
      (78 s → 12 s WebGL2, ~100 s → 29 s WebGPU); CI caches emsdk and skips
      docs-only changes. Negative-tested: crashes, hangs, panics, error logs,
      missing assets, stale backend builds and unfinished loads all still fail.
      webcheck now also fails on libwgrender [ERROR]/[FATAL] logs (it missed them before)
- [x] CI (GitHub Actions, `.github/workflows/ci.yml`): desktop build, `make check`,
      `make test`, `make smoke`; web build + `make webcheck` (WebGL2, headless Chrome)
      with screenshots as an artifact
- [x] Null / headless renderer: `make HEADLESS=1` builds `build/headless/libwgrender.a`
      (sokol dummy GPU backend, no window or audio device, no GL/X11/ALSA link
      deps) behind an internal `wgr_platform` layer; frames run paced at 60/s
      until `wgr_request_quit` or `WGR_HEADLESS_FRAMES`. Unit tests link it
- [x] Web smoke: `make webcheck` loads every example in a browser (WebGL2 headless,
      WebGPU headed), fails on console errors/exceptions/panics/wrong backend,
      saves screenshots
- [x] Desktop headless smoke: `make smoke` runs every example headless for 180
      frames and fails on a non-zero exit, a timeout, or error-level logs
      (tools/smoke.sh). Needs no display, so it works with monitors asleep
- [x] Shared behavior tests: won't (2026-09-21). The idea was scenarios run against
      librl and wgrender through an adapter header, compared with tolerances, to prove
      parity. Parity is reached and librl is frozen, so the comparison would only ever
      say what it says today; wgrender's own behavior is pinned by its unit tests and
      the smoke run of every example
- [x] Gate on parity: won't (2026-09-21). Parity is reached (0 todo) and librl is
      frozen, so none of the report's three checks can fire for a reason that matters:
      unmapped and stale need librl to change, and "ported target missing" is a removed
      public function, which the examples and unit tests already catch. The map stays as
      the record of what was dropped and why; `make parity` prints it, and
      tools/parity.sh now falls back to reference/librl so that works from a bare
      checkout.
- [x] Bug: the library builds had no header dependency tracking (desktop and
      headless not at all, web not for the vendored `-isystem` headers), so header
      changes left stale objects. Both now use `-MD -MP`
- [x] webcheck: WebGPU runs failed the first four examples (started after ~20 s or
      never) when the monitors were asleep: WebGPU ran in a visible browser window,
      and the pages waited for the compositor to wake the displays (cosmic-comp logs
      a modeset as they continue). An earlier fix wrongly blamed HDMI audio (the fake
      audio device stays: webcheck shouldn't play sound). Fixed: WebGPU runs on a
      private Xvfb display (ANGLE on Vulkan), rendering correctly, no window, no
      monitors involved; headless WebGPU loses its device immediately
- [x] Remove the remaining `PARITY:` note in `examples/simple.c` when FPS drawing
      in a custom font lands
- [x] Web size (2026-09-17): web builds weren't link-optimized (no -O: no wasm-opt,
      unminified JS, assertions) and carried all three backends' shader sources.
      Now -O3 (WEB_DEBUG=1 for debug builds) and sokol-shdc --ifdef: simple went
      from 874 KB wasm + 425 KB JS to 693 + 190 KB (323 KB gzipped; librl's c-simple
      is 653 + 264 KB, 342 KB gzipped). The rest of the gap: every program links
      every subsystem (below)
- [x] Web startup (2026-09-19): `make webstart` (`tools/webstart.mjs`) times cold,
      warm and hot visits from `wgr:*` performance marks, locally, on emulated 4G and on
      a phone. Fixed: worker threads no longer hold up main() (~500 ms on 4G);
      versioned code (`?v=<hash>`, `tools/webdeploy.py`) cached for good, so a warm
      visit fetches no code; the wasm downloads alongside the JS; the BRDF table is
      baked (35-40 ms of every start); sprites, particles, models and the audio device
      are set up on first use. 4G, `simple`, first frame: cold 1214 -> 744 ms, warm
      1112 -> 389 ms. Pixel over Wi-Fi: libwgrender's setup 80-186 -> 15-34 ms, first
      frame 422-816 -> 241-444 ms. README "Startup and hosting" lists the headers a
      host needs
- [x] Web size, flags (2026-09-19): release web builds define NDEBUG (no sokol
      validation layer or C asserts; desktop, headless and WEB_DEBUG builds keep them)
      and run Closure on the JS glue with `-sENVIRONMENT=web,worker`: hello 361 -> 311
      KB gzipped (wasm 316 -> 284, JS 45 -> 27). Measured and not worth it: `-Oz` at
      link (-5 KB, slower code), emmalloc (-2 KB)
- [x] Web size, structure (2026-09-19): programs link only the subsystems they use
      (ARCHITECTURE.md §7b: optional modules register themselves, the core reaches
      them through hooks; `make check` guards it). Gzipped: hello 311 -> 134 KB,
      sprite programs ~170, model programs ~240, everything ~300
- [x] Optional subsystems (2026-09-19): linked by use, no build flags (see web size,
      structure, above); `make websize` reports per example

### Outside the library

- [x] First language binding (2026-09-21): Haxe, as its own repo -- wgrender-hx
      (github.com/whirlinggizmo/wgrender-hx), hxcpp and JS targets generated from the
      headers, in development. Nim and Beef are the candidates after it
- [x] Scripting and language bindings stay out of the core repo (decided; see ROADMAP)

### Decisions

- [x] Which language binding comes first: Haxe (2026-09-21; wgrender-hx exists and is
      generating against the headers)

- [x] Light getters (2026-09-21): wgr_light.h had one getter (casts_shadows) against
      thirteen setters, which the Haxe binding pointed out makes a clamp unobservable
      -- set_shadow_map_size(64) said true and nothing outside could learn it became
      256. A getter per setter value now, plus get_type; the shadow ones go through
      wgr_shadow.c like their setters
- [x] Handle-only API for point lists: line strips are built point by point on a
      retained shape (`wgr_shape3d_set_line_strip` + `wgr_shape3d_add_point`); batch
      asset ensure is designed with the loading pipeline
- [x] Naming, part 1 (2026-09-17): resources now have `wgr_<resource>_release`
      instead of `destroy`, because that's what it does — drop this handle's
      reference — and objects keep `destroy`, so the name says which layer you're on
      (texture, mesh, audio, font, material, environment). The public wrappers
      collapsed onto the internal `release` functions that already existed; `retain`
      stays internal (one `create` is one reference). `create` stays `create` for
      both layers on purpose: the noun says whether it takes a path or a handle, and
      generators like `wgr_mesh_create_cube` load nothing
- [x] Naming, part 2 (2026-09-20): done, and for a better reason than the one written
      here — sk_ was never sokol's (sokol is sg_/sapp_/sgl_/saudio_/sfetch_/stm_), it
      was just libsk's own prefix. It named the library, and the library was renamed:
      libsk -> libwgrender, repo robknopf/libsk -> whirlinggizmo/wgrender-c, symbols
      sk_ -> wgr_ with internals wgri_, .skshader -> .wgrshader. Done before any
      binding existed to depend on the names, which was the point of the deadline
- [x] Fonts are resources like the rest (2026-09-17): refcounted and deduped by
      path; text objects and the default font hold references; a released font's
      fontstash data is kept by path and reused (fontstash can't remove fonts).
      Follow-up: a `.ttf/.otf` loader so reading big font files runs in the loading
      pipeline

## ARCHITECTURE.md status log

ARCHITECTURE.md's section 8, "Status", from the first phases.

- **Done — Mesh/Model split + vocabulary (Phase 1).** `wgr_model.c` separates a
  shared, refcounted, path-deduped resource from a lightweight instance, in the
  final vocabulary:
  - **Mesh** resource (`wgr_mesh_t`, kind `WGR_HANDLE_KIND_MESH`) owns primitives
    (`wgr_primitive_t`) + GPU buffers + retained pick geometry + skeleton + clips
    + merged AABB + `ref_count` + `path`;
  - **Model** object (`wgr_model_t`, kind `WGR_HANDLE_KIND_MODEL`) owns
    transform / tint / visibility / animation playback / joint matrices, and
    references a Mesh via its `mesh` handle;
  - two handle pools; `create_mesh`/`find_mesh_by_path`/`retain_mesh`/
    `release_mesh`/`create_model`;
  - public API: `wgr_mesh_create` / `wgr_mesh_create_from_memory` /
    `wgr_model_create_from_mesh` / `wgr_mesh_release`, plus backward-compatible
    `wgr_model_create` / `wgr_model_create_from_memory` sugar. Builds clean (lib +
    examples + `make check`).

- **Pending — procedural mesh generators** (`wgr_mesh_create_cube/sphere/plane`)
  and **retire the retained Shape object** into Model; keep the immediate
  draw/gizmo API.

- **Pending — Texture resource split** (Sprite objects share Texture resources;
  alpha mask on the resource, generated on demand for alpha-test picking).

- **Done — Audio/Sound split, Music folded in.** `wgr_audio.c` owns an **Audio**
  resource (`wgr_audio_t`, kind `WGR_HANDLE_KIND_AUDIO`) holding decoded PCM,
  refcounted and path-deduped; the mixer plays **Sound** objects (`wgri_sound_t`,
  kind SOUND) that carry playback state (`pos`/`volume`/`pitch`/`loop`/`playing`)
  and reference an Audio by handle. There is **no separate Music type** — a
  looping background track is just a Sound with `wgr_sound_set_loop(true)`
  (`wgr_music_*` and kind MUSIC are gone). `wgr_sound_play/pause/resume/stop`.

- **Pending — streamed Audio.** Today every Audio is fully decoded into PCM, so a
  long music track is decoded whole into RAM. The doc's *decoded | streamed* load
  mode (chosen at `wgr_audio_create`) is the proper fix: the Audio holds the shared
  compressed source, and a Sound playing a streamed Audio carries its own decoder
  + ring buffer (single-buffer sharing only works for decoded PCM). Pairs with
  the `wgr_fs`/host-fetch work. `play_sfx`/`play_music` sugar optional on top.
