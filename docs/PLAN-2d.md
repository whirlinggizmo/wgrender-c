# Plan: 2D / UI layer

Status: **in progress.** Steps 1 (enabled, pointer interaction, touch as pointer),
2 (retained 2D shapes) and 3 (UI essentials) implemented 2026-09-17; see "As built".
Step 4 (sprite3d for 2D worlds) to do.

## What exists

- **sprite2d** (source rect, pivot, flip, tint, alpha-tested picking) and **text2d**
  (position, size, color, pickable by rectangle), both scene members in screen space.
- **sprite3d** (texture, transform, uniform size, facing modes including FREE and
  Y_UP, tint, alpha-tested picking); **orthographic camera3d**.
- **Immediate 2D shapes:** rectangle (filled/lines), line, circle (filled/lines),
  triangle. No retained 2D shapes.
- **Scenes:** 2D members draw after all 3D layers, in layer order (then the order
  added); `sk_scene_pick` hits 2D members first, topmost first, then 3D.
- **Flags on every drawable** (model, shape, sprite2d, sprite3d, text2d, text3d):
  `set_visible`/`is_visible`, `set_pickable`/`is_pickable`. Picks skip invisible
  and non-pickable objects.
- **Input:** mouse position/delta/wheel, keys and buttons with edges
  (`SK_BUTTON_UP/PRESSED/DOWN/RELEASED`, relative to the running tick or frame),
  typed characters.
- **Not there:** hover/press/click state, a way to disable an object's interaction,
  retained 2D shapes, clipping, nine-slice, text alignment or wrapping, touch as a
  pointer. `sk_render_begin_mode_2d(camera)` ignores its camera (so did librl's).

Direction already decided (ROADMAP): **no GUI toolkit in the core.** In-game UI is
built from sprites, text and shapes; layout can come later from a small library
such as Clay; developer UI (Dear ImGui) is an optional module outside the core.

## Goals

1. **In-game UI and HUD:** panels, buttons, bars, labels on screen, reacting to the
   pointer, over a 2D or 3D world.
2. **2D games:** a world that scrolls and zooms, with the same scene, layer and
   picking model as 3D.

## Proposed design

### 1. Screen space for UI, orthographic 3D for 2D worlds (no camera2d)

librl had one camera (3D) and drew 2D in screen pixels; libsk does the same. A
separate 2D camera would only serve 2D worlds, and libsk can already draw those with
what it has: an **orthographic camera3d looking down -Z at sprite3d objects in the XY
plane** (FREE facing), with the same scenes, depth order, transparency sorting and
ray picking as 3D, and one world unit per pixel when pixel-exact placement matters.

- sprite2d, text2d and 2D shapes stay **screen space** (UI, HUD).
- sprite3d gains what 2D worlds need from sprite2d:

```c
bool sk_sprite3d_set_source(sk_handle_t sprite, float x, float y, float width, float height);
     /* texture pixels (sprite sheets, atlases); default: whole texture */
bool sk_sprite3d_set_pivot(sk_handle_t sprite, float x, float y);     /* 0..1, default center */
bool sk_sprite3d_set_extent(sk_handle_t sprite, float width, float height);
     /* world size; sk_sprite3d_set_size(s) stays as the square/aspect-kept shorthand */
```

- `sk_render_begin_mode_2d(camera)` loses its unused camera parameter (screen space
  only). Revisit a camera2d only if 2D worlds on orthographic 3D prove awkward.

### 2. Retained 2D shapes (screen space)

Shapes already have handles (3D). Add 2D variants as scene 2D members:

```c
bool sk_shape_set_rectangle_2d(sk_handle_t shape, float width, float height, float corner_radius);
bool sk_shape_set_circle_2d(sk_handle_t shape, float radius);
bool sk_shape_set_line_2d(sk_handle_t shape, float x0, float y0, float x1, float y1, float thickness);
bool sk_shape_set_transform_2d(sk_handle_t shape, float x, float y, float rotation, float scale_x, float scale_y);
bool sk_shape_set_outline(sk_handle_t shape, float thickness);         /* 0 = filled (default) */
```

Rounded rectangles and thick lines are what UI panels and bars need. Picked by
their exact shape.

### 3. `enabled` on every drawable

Following `visible` and `pickable`, on model, shape, sprite2d, sprite3d, text2d and
text3d (lights already have it):

```c
bool sk_<kind>_set_enabled(sk_handle_t object, bool enabled);   /* default true */
bool sk_<kind>_is_enabled(sk_handle_t object);
```

- **visible:** drawn or not.
- **pickable:** hit by picks or not; not pickable lets the pointer through to what's
  behind.
- **enabled:** whether a hit reacts. A disabled object is drawn, still blocks the
  pointer and is still reported by `sk_scene_pick` and `sk_scene_get_hovered` (a UI
  can show a "disabled" tooltip), but its hover, press and click stay `UP`/false.

### 4. Pointer interaction, polled per object with edges

A scene opts in, then updates its interaction state once per frame from one pick of
the pointer, against the positions objects had when last drawn (one frame of
latency, as usual for retained UI):

```c
bool sk_scene_set_interactive(sk_handle_t scene, bool interactive);   /* default false */

sk_handle_t       sk_scene_get_hovered(sk_handle_t scene);                   /* topmost under the pointer, or 0 */
sk_button_state_t sk_scene_get_hover(sk_handle_t scene, sk_handle_t object);
    /* UP: not under the pointer, PRESSED: entered this frame, DOWN: under it,
       RELEASED: left this frame */
sk_button_state_t sk_scene_get_press(sk_handle_t scene, sk_handle_t object);
    /* the primary button, for a press that started on this object: PRESSED this
       frame, DOWN while held (also when dragged off), RELEASED this frame */
bool              sk_scene_is_clicked(sk_handle_t scene, sk_handle_t object);  /* released while still over it */
bool              sk_input_is_pointer_captured(void);
    /* the current press started on a 2D member of an interactive scene: game
       controls (camera drag, 3D selection) should ignore it */
```

- **Members:** any pickable member, 2D and 3D (models, shapes, sprites, text), in
  the scene's pick order (2D topmost first, then nearest 3D). `sk_scene_pick` stays
  for other points and one-off queries.
- **Edges** use the input enum and the same frame semantics as keys and buttons.
- **Capture only for 2D hits**, so hovering and clicking 3D objects still works
  while UI blocks presses that land on it.
- **Pointer = mouse + primary touch** (touch input lands with this step), so UI works
  in mobile browsers without separate code.
- Handle and enum returns only: cheap per call, including through bindings.

### 5. UI drawing essentials

```c
bool sk_sprite2d_set_nine_slice(sk_handle_t sprite, float left, float top, float right, float bottom);
     /* borders in source pixels; the middle stretches, corners don't */
bool sk_text2d_set_align(sk_handle_t text, sk_text_align_t horizontal, sk_text_align_t vertical);
bool sk_text2d_set_max_width(sk_handle_t text, float width);   /* wrap at words; 0 = no wrap */
bool sk_scene_set_clip(sk_handle_t scene, int layer, float x, float y, float width, float height);
     /* clip a layer's 2D members to a screen rectangle (scroll areas, panels); 0 size = none */
void sk_render_begin_clip(float x, float y, float width, float height);  /* immediate drawing */
void sk_render_end_clip(void);
```

### Not in this plan

- **A batched 2D renderer.** sprites and shapes draw through sokol_gl, fine for UI and
  small 2D games; build it with particles, measured against a sprite-heavy example.
- **Layout (Clay), widgets, themes, Dear ImGui:** outside the core, later.
- **Sprite animation (frame sequences):** set the source rect per frame; revisit with
  a real use.

## Decisions

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
   text, a clipped scrolling list, a clickable 3D model) and `2d` (a scrolling, zooming
   2D world on an orthographic camera with sprite sheets and picking). Recommend: yes.

## As built

### Step 1: enabled, pointer interaction, touch

- `sk_<kind>_set_enabled` / `is_enabled` on model, shape, sprite2d, sprite3d, text2d,
  text3d; kinds register their getter with the scene (`sk_scene_register_enabled`).
- `sk_scene_set_interactive`, `sk_scene_is_interactive`, `sk_scene_get_hovered`,
  `sk_scene_get_hover`, `sk_scene_get_press`, `sk_scene_is_clicked`, and
  `sk_input_is_pointer_captured`, as designed.
- The runtime updates interaction once per frame before the ticks
  (`sk_scene_update_interaction`), from the frame's pointer edges; edges are kept per
  context like input (frame edges cleared after the frame callback, tick edges after
  each tick and carried over frames without ticks, up to 8 hover changes).
- The interaction pick reads the scene's camera without changing the active camera
  (unlike `sk_scene_pick`) and doesn't count pick statistics.
- Capture lasts from the press frame through the release frame.
- Touch: the first touch drives the pointer (move + left button); other fingers are
  ignored. A multi-touch API is still open (TASKS).
- `examples/ui.c`: buttons with hover/press colors, a click counter, a button toggled
  enabled/disabled, the gumshoe as a 3D member (hover highlight, click to animate), and
  a camera orbit that ignores drags starting on UI. Checked in the browser with real
  (CDP) mouse events: hover, press and capture.

### Step 2: retained 2D shapes

- New shape kinds with `sk_shape_set_rectangle_2d` (rounded corners, radius clamped to
  half the shorter side), `sk_shape_set_circle_2d`, `sk_shape_set_line_2d` (thickness,
  butt ends), `sk_shape_set_transform_2d` and `sk_shape_set_outline` (rectangles and
  circles). **A 2D rectangle's origin is its top-left corner**, like the immediate
  `sk_shape_draw_rectangle(x, y, w, h)` and how UI is laid out; circles are centered.
  Picked by exact area (rounded corners, outline rings, distance to the line).
- One shape handle kind now holds 2D and 3D shapes, so kinds can register
  `sk_scene_register_is_2d`: scenes draw and pick each member through exactly one of
  the 2D or 3D paths, `sk_pick_object` routes the same way (it previously used the 2D
  pick for any kind that had one), and pick statistics are unchanged for 3D shapes.
- `examples/ui.c` is built from 2D shapes: a rounded translucent panel (pickable, so
  presses on it don't orbit), rounded buttons, a divider line, an outlined progress
  bar with a filled part and a circle tip. Checked in the browser with CDP clicks
  (counting, disabling, hover).

### Step 3: UI essentials

- `sk_sprite2d_set_nine_slice(left, top, right, bottom)`: borders in source pixels
  that keep their size at any drawn size. An axis with no borders stays one span, so
  a sprite sliced on one axis draws three patches, not nine; borders wider than the
  source region share it, and borders that don't fit the destination shrink to fill
  it (a patch never flips). Picks hit the whole rectangle — the alpha test is skipped
  while a sprite is sliced, because u/v don't map linearly into the source any more.
- `sk_text2d_set_align(horizontal, vertical)` with `sk_text_align_t`
  (LEFT/CENTER/RIGHT, TOP/MIDDLE/BOTTOM; the axes have distinct values, so a value
  from the wrong axis is refused) and `sk_text2d_set_max_width` (wraps between words;
  a word wider than the box keeps its own line; newlines always break). The position
  is the block's edge or center per its alignment, and wrapped lines line up the same
  way inside the block. Layout lives in the text layer
  (`sk_text_block_size` / `sk_text_block_draw`, internal), so text3d can use it later.
  **`sk_text2d_measure_width`/`_height` now measure the laid-out block** — the widest
  line and whole lines of the font's line height, not one line's glyph extents — and
  picks use that rectangle.
- Clipping: `sk_scene_set_clip(scene, layer, x, y, width, height)` clips a layer's 2D
  members (at most 8 layers per scene; a 0 size removes it), and
  `sk_render_begin_clip` / `sk_render_end_clip` do the same for immediate drawing.
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

## Verification

Unit tests on the headless build: the interaction state machine (enter/leave,
press/hold/drag-off/release, click vs cancel, disabled, topmost 2D over 3D, capture,
non-interactive scenes), 2D shape picks (rounded corners, thick lines), nine-slice
geometry, text wrap and alignment, clip rectangles, sprite3d source and pivot.
Examples checked with webcheck on both backends; `make websize` before and after.
