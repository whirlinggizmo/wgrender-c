# Plan: Remaining librl parity

Status: **implemented (2026-09-16).** Decisions 1–7 accepted as recommended.
`make parity`: 95%, 10 todos left, all deferred on purpose (window and monitors,
batch ensure, host ping). See "As built".

`make parity` lists 58 librl functions still marked todo (see tools/parity.map).
Parity is functional, not 1:1 (AGENTS.md): each group below says what librl did,
what libsk already has, and the proposed libsk design. Several librl functions
collapse into one, and a few are proposed as dropped with a reason.

## 1. Picking (13 todos)

librl: `rl_pick_model/shape/sprite3d/text3d(camera, object, x, y)` (one per kind),
pickable flags on models, sprites, texts and shapes, and pick statistics.
libsk: `sk_scene_pick` over a scene; per-kind pick functions exist internally
(a registry by handle kind); pickable flags only on shapes and sprite2d.

```c
/* include/sk_pick.h (new public header) */
/* Pick one object at screen point (x, y), logical pixels, through `camera` (0 =
 * active). Works for every pickable kind: model, shape, sprite2d, sprite3d, text3d. */
sk_pick_result_t sk_pick_object(sk_handle_t object, sk_handle_t camera, float x, float y);

typedef struct { int broadphase_tests, broadphase_rejects, narrowphase_tests, narrowphase_hits; } sk_pick_stats_t;
sk_pick_stats_t sk_pick_get_stats(void);   /* since the last reset */
void            sk_pick_reset_stats(void);

/* per kind, default true; non-pickable objects are skipped by every pick */
bool sk_model_set_pickable(sk_handle_t model, bool pickable);    bool sk_model_is_pickable(sk_handle_t model);
bool sk_sprite3d_set_pickable(...);  bool sk_sprite3d_is_pickable(...);
bool sk_text2d_set_pickable(...);    bool sk_text2d_is_pickable(...);   /* makes text2d pickable at all */
bool sk_text3d_set_pickable(...);    bool sk_text3d_is_pickable(...);
```

Four kind-specific pick functions become one (`sk_pick_object`), dispatching on the
handle's kind like scenes do. Stats return a small value struct, like
`sk_pick_result_t`.

## 2. Text in 3D (15 todos)

librl: a text3d object (font, size, content, transform, color, facing, visible,
pickable, bounds, draw) plus an immediate `rl_text3d_draw_text`.

```c
/* include/sk_text3d.h */
sk_handle_t sk_text3d_create(sk_handle_t font);          /* font may be 0 (built-in, attach later) */
bool   sk_text3d_set_font(sk_handle_t text, sk_handle_t font);
bool   sk_text3d_set_text(sk_handle_t text, const char *text);
bool   sk_text3d_set_size(sk_handle_t text, float size);  /* world units: height of a line */
bool   sk_text3d_set_transform(sk_handle_t text, float x, float y, float z, float rx, float ry, float rz); /* radians */
bool   sk_text3d_set_facing(sk_handle_t text, sk_sprite3d_facing_t facing);  /* same modes as sprite3d */
bool   sk_text3d_set_color(sk_handle_t text, sk_handle_t color);
bool   sk_text3d_set_visible(...);  bool sk_text3d_is_visible(...);
vec2_t sk_text3d_get_size(sk_handle_t text);              /* world-space width, height */
void   sk_text3d_draw(sk_handle_t text);
void   sk_text3d_destroy(sk_handle_t text);
void   sk_text_draw_3d(sk_handle_t font, const char *text, float x, float y, float z, float size, sk_handle_t color);
```

- Drawn with fontstash through sokol_gl in 3D mode; a scene member, sorted with
  transparent parts (glyph edges blend). Pickable as its world-space quad.
- Naming follows text2d (`set_text`, not librl's `set_content`); the immediate draw
  joins the text module (`sk_text_draw_3d`, like `sk_text_draw_ex`).

## 3. 3D shapes (7 todos)

librl: 3D rectangle and circle (center, size, axis-angle rotation), line and line
strip, immediate and retained. libsk: retained shapes are local geometry plus
`sk_shape_set_transform`; immediate 3D draws take a center and size.

```c
/* retained: local geometry, placed with sk_shape_set_transform */
bool sk_shape_set_rectangle(sk_handle_t shape, float width, float height);  /* in the XY plane, filled */
bool sk_shape_set_circle(sk_handle_t shape, float radius);                  /* in the XY plane, outline */
bool sk_shape_set_line(sk_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1);
bool sk_shape_set_line_strip(sk_handle_t shape);                            /* empty strip */
bool sk_shape_add_point(sk_handle_t shape, float x, float y, float z);      /* append to the strip */

/* immediate (3D mode); rotation is euler radians like every transform */
void sk_shape_draw_rectangle_3d(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, sk_handle_t color);
void sk_shape_draw_circle_3d(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, sk_handle_t color);
```

- **Line strips without a point array** (the public API can't take pointers):
  build a retained strip point by point. An immediate strip is a retained shape
  drawn once. Decision below.
- Axis-angle rotation becomes euler radians, matching every other libsk transform.

## 4. Window and monitors (8 todos)

librl: set window size and position, count monitors, get a monitor's size and
position, move to a monitor. **sokol_app has none of these** (only title, fullscreen
toggle and mouse lock), and the related window flags (resizable, undecorated,
hidden, ...) are accepted but ignored today (docs/TASKS.md).

Options:

- **A. Native calls in `sk_platform`** per window system: X11 (`XResizeWindow`,
  `XMoveWindow`, XRandR for monitors, adds `libXrandr`), Win32, Cocoa; web: the
  canvas size only (position and monitors don't apply). Also makes the ignored
  window flags work. Only Linux/X11 and web can be tested here; Windows and macOS
  would be written blind.
- **B. Defer:** mark these as todo until a game needs them, or until a sokol_app
  release adds them.

## 5. Model animation and validity (5 todos)

librl (raylib): animations as frame counts and "set frame N"; `is_valid` and a
strict variant; a default placeholder model.

```c
float sk_model_get_animation_duration(sk_handle_t model, int animation);  /* seconds */
bool  sk_model_set_animation_time(sk_handle_t model, float seconds);      /* pose at a time */
bool  sk_model_is_ready(sk_handle_t model);   /* has a loaded mesh (handles are already checked) */
```

- glTF animations are keyframed in seconds, not uniform frames, so duration and time
  replace frame count and frame index.
- `is_valid`/`is_valid_strict` checked raylib model data; libsk handles are
  generation-checked, so the useful question is "does it have a mesh yet":
  `sk_model_is_ready`.
- **Drop** the placeholder model: a model without a mesh draws nothing and logs
  why; missing images already get the placeholder texture.

## 6. Assets (3 todos)

- `rl_asset_get_host` → `const char *sk_asset_get_host(void)`.
- `rl_asset_ensure_many_async` → belongs to the loading pipeline (next roadmap item):
  a task group with a handle-only API. Move there.
- `rl_asset_ping_host` (latency to the asset host) → belongs to the `sk_net` redesign.
  Move there.

## 7. Small leftovers (5 todos)

- `rl_sound_set_pan` → `sk_sound_set_pan(sound, pan)`: -1 left .. 1 right,
  constant-power panning in the mixer.
- `rl_sprite3d_get_transform` (out-pointers) → `sk_sprite3d_get_position` and
  `get_rotation`, returning `vec3_t`.
- `rl_text_draw_fps_ex(font, x, y, size, color)` → `sk_text_draw_fps_ex` with the
  same parameters (also removes the last `PARITY:` note in `examples/simple.c`).
- `rl_font_get_default` → **drop:** font handle 0 already means the built-in font
  everywhere.
- `rl_texture_draw_ground` (a textured quad on the ground) → **drop:** a sprite3d
  with `SK_SPRITE3D_FACING_Y_UP` does this, and can be placed, picked and added to
  scenes.

## Decisions

1. **Picking:** one `sk_pick_object` for every kind, pickable flags on every
   pickable object, stats as a value struct. Recommend: yes.
2. **Text3d:** as above, sharing sprite3d's facing modes. Recommend: yes.
3. **Line strips:** built point by point on a retained shape
   (`sk_shape_set_line_strip` + `sk_shape_add_point`), with no immediate strip
   function. Alternatives: an immediate `begin/point/end` sequence, or strips from
   a "point list" resource handle. Recommend: retained, point by point.
4. **Window and monitors:** A (native, Linux/X11 + web now, Windows/macOS written
   but untested, plus the ignored window flags) or B (defer). Recommend: B for now.
   It's the biggest item, can't be tested on two of three desktop platforms, and
   no current work needs it; revisit with the first game that does.
5. **Model animation:** durations and times in seconds instead of frames;
   `sk_model_is_ready`; drop the placeholder model. Recommend: yes.
6. **Assets:** add `sk_asset_get_host`; move batch ensure to the loading pipeline
   and ping to `sk_net`. Recommend: yes.
7. **Leftovers:** pan, sprite3d getters, FPS text with a font; drop
   `font_get_default` and `texture_draw_ground`. Recommend: yes.

## As built

- **Bug fixed:** scene picking treated a non-pickable 3D object as "no exact test"
  and fell back to its bounding box, so `sk_shape_set_pickable(false)` still hit.
  Picking now goes through one internal path (`pick_2d`/`pick_3d` in sk_scene.c),
  shared by `sk_scene_pick` and `sk_pick_object`, which also counts the stats.
- **text2d joined scenes** as a 2D member (drawn over 3D, picked by its text
  rectangle), which its pickable flag needed.
- **FREE facing** (`SK_SPRITE3D_FACING_FREE`, oriented by the rotation) was added
  for sprite3d and text3d: librl's default sprite facing, noted as a gap in
  `examples/simple.c`. `sk_sprite3d_set_facing` now takes the enum.
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

## Order and verification

Order: 7 and 6 (small) → 5 → 1 → 3 → 2 (text3d uses picking and the shape work).

Verification: unit tests for each (pick dispatch and flags, stats counting, strip
building, animation time sampling, panning gains, text3d bounds); a `text3d`
example and additions to `pick` and `hello3d` examples; parity map updated, with
`make parity` showing only deferred items as todo; `make verify` and `make
webcheck` on both backends.
