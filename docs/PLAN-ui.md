# Plan: UI through libsk's public API

Status: **done for now** (2026-09-18). Steps 1 (drawing and input), 2 (text) and 3 (the Clay
example) are built; see "As built". Step 4 — clipboard, letter spacing, the ImGui hook —
waits until text fields, a design or developer UI need it.

## Why

libsk won't ship a GUI toolkit ([ROADMAP.md](ROADMAP.md), "GUI direction"). In-game UI
comes from a layout library — [Clay](https://github.com/nicbarker/clay) is the likely
one — drawing through libsk, and developer UI from Dear ImGui.

A layout library can reach the GPU two ways. It can bypass libsk: Clay ships
`sokol_clay.h`, which talks to sokol directly, and in libsk that means a second
fontstash context and a second copy of every font (with different text metrics), raw
`sapp_event` input that skips libsk's input edges and pointer capture, and a projection
that fights libsk's 2D mode. Or libsk can be its renderer: a small piece of glue turns
the library's draw commands into libsk calls.

This plan is the second way, done so the glue needs **nothing but libsk's public API**.
Then no third-party types or code enter libsk, the glue ports to any language binding
(Clay already has bindings for several), and every addition below is just as useful for
a hand-written HUD or another library (microui and Nuklear's command mode emit the same
kinds of commands).

## What the prototype showed

A throwaway `examples/clay.c` (2026-09-18, not committed; Clay pinned at `e6cc369`) drew
Clay's own demo layout — a header bar with a hover dropdown, a clickable sidebar, a
momentum-scrolled document of wrapped text — through the public API, checked in the
browser on both backends. It cost about +50 KB gzipped on web, only in the program that
used it. It worked, and it found the gaps this plan closes:

- no immediate rounded rectangle or border (the prototype built both from triangles);
- clips don't nest (Clay nests scroll areas; `sk_render_end_clip` resets to the whole
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

- **text is rasterized at its logical size.** Nothing in `sk_text.c` or `sk_font.c`
  looks at the DPI scale, so with high-DPI on, every glyph is drawn at half resolution
  and magnified — blurry text on phones and retina screens, for all libsk text, not
  just UI.

## Design

Every signature follows the public API rules: handles, scalars, enums, `const char *`.

### 1. Drawing

**Float coordinates for immediate 2D.** `sk_shape2d_draw_rectangle`, `_rectangle_lines`,
`_line`, `_circle` and `_circle_lines` take `int` positions and sizes today (triangle
already takes floats). They become `float`: layout produces fractional positions, and on
a high-DPI screen half a logical pixel is a real pixel. This breaks callers that pass
ints only in the sense that they convert implicitly; nothing needs rewriting.

**Rounded rectangles and borders**, per-corner radii and per-side widths, as Clay and CSS
have them:

```c
void sk_shape2d_draw_rounded_rectangle(float x, float y, float width, float height,
                                       float r_top_left, float r_top_right,
                                       float r_bottom_right, float r_bottom_left, sk_color_t color);
void sk_shape2d_draw_border(float x, float y, float width, float height,
                            float left, float top, float right, float bottom,
                            float r_top_left, float r_top_right,
                            float r_bottom_right, float r_bottom_left, sk_color_t color);
```

Radii are clamped to half the shorter side; a border's inner corners follow the outer
radius minus the adjoining width. The geometry already exists for retained 2D shapes
(`src/sk_shape2d.c`), which move onto the same code.

**Images with a source rectangle**, and nine-slice, for skinned panels and icons from an
atlas:

```c
void sk_texture_draw_ex(sk_handle_t texture, float source_x, float source_y,
                        float source_width, float source_height,
                        float x, float y, float width, float height, sk_color_t tint);
void sk_texture_draw_nine_slice(sk_handle_t texture, float source_x, float source_y,
                                float source_width, float source_height,
                                float left, float top, float right, float bottom,
                                float x, float y, float width, float height, sk_color_t tint);
```

The nine-slice math is sprite2d's (`sk_sprite2d_nine_slice_axis`).

**A clip stack** replaces `sk_render_begin_clip` / `sk_render_end_clip`:

```c
void sk_render_push_clip(float x, float y, float width, float height); /* intersects the current clip */
void sk_render_pop_clip(void);                                         /* back to the enclosing one */
```

Each push intersects with the clip it's pushed inside, so a scroll area inside a panel
stays inside the panel. Scene layer clips (`sk_scene_set_clip`) use the same stack, so a
layer clip and a UI clip nest too. The stack is per render pass (render targets start
empty) and is emptied at the end of a frame, with a warning if pushes and pops didn't
match.

### 2. Text

**Length-taking draw and measure**, so text can be a slice of a longer string:

```c
void   sk_text_draw_n(sk_handle_t font, const char *text, int length, float x, float y,
                      float size, sk_color_t color);
vec2_t sk_text_measure_n(sk_handle_t font, const char *text, int length, float size);
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

### 3. Input

**The UI can capture the pointer and the keyboard.** Game code already asks
`sk_input_is_pointer_captured()` before acting on the mouse; today only scene members can
set that. The UI gets its own, sticky, switch:

```c
void sk_input_set_pointer_captured(bool captured);   /* the UI holds the pointer */
void sk_input_set_keyboard_captured(bool captured);  /* a UI text field has focus */
bool sk_input_is_keyboard_captured(void);
```

`sk_input_is_pointer_captured()` becomes true while either a scene press or the UI holds
the pointer. The capture is **sticky** — it stays until the UI changes it — because of
frame order: the runtime updates scene interaction, runs the ticks, then calls the frame
callback, and a UI lays out in the frame callback. A capture that lasted one frame would
never be seen by the ticks. So the glue sets it every frame from the library's hover
result, and ticks see the previous frame's value: one frame of latency, the same as
scene interaction. The glue also keeps the pointer captured while a press that started
over UI is held, even when the drag leaves the UI, as scene capture does (so dragging off
a slider doesn't start orbiting the camera). Captures are advisory, as today: libsk still
reports input; game code checks the flags.

**Fractional, two-axis wheel.** `sk_mouse_state_t.wheel` is an `int`, which quantizes
trackpad and precision-wheel scrolling. It becomes a `float`, with a `wheel_x` beside it
(sokol reports both axes).

**Clipboard** — `sk_clipboard_set_text(const char *)` and
`const char *sk_clipboard_get_text(void)` — when text fields arrive. On web, sokol
delivers pastes as events, so "get" returns the last paste.

### 4. The glue, outside the core

With the above, the Clay glue is about 150 lines of public-API code: a table from Clay
font ids to libsk fonts, the measure callback (`sk_text_measure_n`), a switch from Clay's
render commands to the draw calls above, image data as a small `{texture, source rect}`
struct, and input fed from `sk_input` with the pointer capture set from
`Clay_GetPointerOverIds()`. It starts as a rebuilt `examples/clay.c`, with Clay vendored
for the example only. If it proves reusable, it becomes an optional header-only
`ext/sk_clay.h`; libsk itself never includes or links Clay.

### Developer UI (Dear ImGui) — later

ImGui renders itself (its own pipeline and buffers, via `sokol_imgui.h`), so the public
API can't carry it. It needs a small extension hook instead: raw input events forwarded
in, and a callback that draws inside libsk's final swapchain pass, after libsk's own
drawing. That hook passes sokol types, so it would live in an extension header outside
the public API's rules. It's independent of everything above and can come later.

## Order

1. **Drawing and input** — float immediate coordinates, rounded rectangle and border,
   `sk_texture_draw_ex` and nine-slice, the clip stack, pointer and keyboard capture,
   the float wheel. Mechanical, and each piece is useful on its own. The only caller of
   today's begin/end clip is scene layer clipping, which moves onto the stack
   unchanged, so `examples/ui.c`'s clipped list keeps working as it is.
2. **Text** — `_n` draw and measure, then DPI-correct glyphs (the one substantial
   piece: rasterization, measurement, atlas sizing).
3. **The Clay example**, rebuilt on the above as the end-to-end check.
4. Clipboard and letter spacing, when text fields or a design need them; the ImGui hook
   when developer UI is wanted.

## Decisions

1. **Glue on the public API only; no Clay code or types in libsk.** Recommend: yes.
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

## As built

### Step 1: drawing and input (2026-09-18)

- Immediate 2D takes floats (`rectangle`, `rectangle_lines`, `line`, `circle`,
  `circle_lines`); callers passing ints needed no changes.
- `sk_shape2d_draw_rounded_rectangle` and `sk_shape2d_draw_border` as designed. One
  outline builder (`sk_shape2d_rounded_outline`, internal) now serves them and the
  retained rectangles; borders wider than the box fill it.
- `sk_texture_draw_ex` and `sk_texture_draw_nine_slice`; `sk_texture_draw` became a
  wrapper, and the nine-slice helper no longer needs a sprite, so sprites and the
  immediate call share it.
- The clip stack, `sk_render_push_clip` / `pop_clip`, replaced begin/end clip: 32 deep,
  each push intersected with its parent, a render target's pass starting from its own
  target, and pushes left open dropped (with a warning) at the end of a texture pass
  or the frame. Scene layer clips push and pop on it, so a layer clip intersects with a
  clip pushed around `sk_scene_draw`.
- **Capture names differ from the draft:** `sk_input_set_pointer_captured` /
  `sk_input_set_keyboard_captured` / `sk_input_is_keyboard_captured`, the house
  set/is pairing for the existing `sk_input_is_pointer_captured`. The draft's
  `sk_input_capture_pointer` would have sat next to the existing
  `sk_input_capture_cursor`, which means something else (pointer lock). The scene's
  internal setter became `sk_input_set_scene_pointer_captured`.
- The wheel is `float wheel, wheel_x` (and `sk_input_get_mouse_wheel_x`). Beyond
  precision, this fixed a bug: each scroll event was truncated to `int` before being
  added up, so small trackpad steps (sokol reports a notch as about 1.0 and trackpad
  steps as fractions) were dropped entirely.
- `examples/ui.c` gained an immediate header — its nine-slice texture drawn directly,
  and a rounded, bordered status pill — above the retained panel; checked on both
  backends.
- Tests: `input_wheel`, `input_capture`, `render_clip_stack`, `shape2d_immediate`
  (outline geometry, and vertex counts proving each draw emits and skips what it
  should), `texture_draw_immediate`.

### Step 2: text (2026-09-18)

- `sk_text_draw_n` / `sk_text_measure_n` as designed; the `_ex` functions, `sk_text_draw`
  and the FPS counter all go through them. The internal layout (`sk_text_block_size`,
  `sk_text_block_draw`, `sk_text_split_lines`) takes a length too.
- **Crisp high-DPI text.** Glyphs are rasterized at size × the drawing target's pixel
  scale (`sk_render_pixel_scale`, internal: the screen's DPI scale, 1 inside a render
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
  didn't fit skip one frame, and `sk_font_end_frame` grows it after `sg_commit`.
- Tests: `text_slices_and_dpi` — slices, logical metrics at 1×/2×/3× within the
  rounding bound, scale 1 inside a render target, and the atlas growing across a frame
  then drawing from the bigger atlas. The headless platform gained a DPI scale for
  tests (`sk_platform_set_headless_dpi_scale`).

### Step 3: the Clay example (2026-09-18)

- `examples/clay.c` draws Clay's demo layout through the public API only: about 100
  lines of glue (measure callback on `sk_text_measure_n`, a render-command switch onto
  the step 1 and 2 calls, pointer capture from `Clay_GetPointerOverIds`). Every GAP the
  prototype marked is gone: no string copies, no triangle-built rectangles, nested
  clips, images from a small `{texture, source rect}` struct in `imageData`.
- Clay is vendored for the example only (`deps/clay`, pinned at `e6cc369`); libsk
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
- It found a Clay bug, fixed on a branch of libsk's Clay fork (`github.com/robknopf/clay`,
  vendored with `tools/update_clay.sh`, like sokol): the wheel and drag
  went to whichever scroll container came last in Clay's internal list, not the
  innermost one under the pointer, and swap-back removal (after switching pages) put
  the outer area last, so the inner one never scrolled. Also, Clay multiplies the wheel
  delta by 10, so the example passes notches times 4 for 40 pixels per notch.

## Verification

Unit tests on the headless build: clip stack intersection, nesting, per-pass reset and
the unmatched-push warning; rounded rectangle and border radius clamping; source
rectangle and nine-slice mapping; `_n` measure against `_ex` for the same text; sticky
capture across the frame and tick order; logical text metrics unchanged between DPI
scales. Examples: `ui` on the clip stack, the rebuilt `clay` example. Browsers: webcheck
on both backends, plus a CDP run with `Emulation.setDeviceMetricsOverride` at a device
scale factor of 2 to compare text sharpness against the current build.
