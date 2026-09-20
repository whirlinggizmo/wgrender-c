# Plan: sprite2d (screen-space sprites)

Status: **implemented (2026-09-16).** See `include/wgr_sprite2d.h` and `examples/sprite2d.c`.
Builds on the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)), scene render
passes, and picking. Related: GUI direction in [ROADMAP.md](ROADMAP.md).

## Why

HUD icons, 2D games, and in-game UI all need textured quads in screen space. The
`sprite2d` handle kind is reserved but unimplemented, and `rl_texture_draw_ex`
(one-off screen-space texture draws) has no libwgrender equivalent.

## What librl had, and what it taught us

`rl_sprite2d_create(texture)`, `set_texture`, `set_transform(x, y, scale, rotation)`,
`set_tint`, `set_visible`, `set_pickable`, `draw`, `destroy` (plus
`create_from_file` and a default texture, both already dropped on purpose).

Gaps that forced workarounds:

- **No source rectangle.** Sprite sheets and atlases needed one texture per frame.
- **No pivot/origin.** Rotation and positioning were always around one corner.
- **Uniform scale only, no flip.** Mirroring a character meant a second texture.
- Picking was rectangle-only, so transparent corners of icons were clickable.

## Proposed API

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

## Decisions

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

## Picking

- Rotated rectangle test in the sprite's local space (inverse of pivot, scale,
  rotation, position), then UV from the local point for the optional alpha test,
  reusing the texture alpha mask sprite3d already builds.
- 2D hits return `point_world` as screen pixels and the sprite handle; `distance` 0.
  (Documented: 2D hits have no depth.)

## Verification

- Unit tests: local transform (pivot, rotation, scale, flip), source-rect UVs,
  rotated-rect hit test, alpha test, pick order (2D before 3D, topmost 2D first).
- Visual test: an atlas-driven animated sprite, rotated/scaled/flipped sprites with
  pivots, a HUD over a 3D scene, click-picking with alpha test.
- `examples/sprite2d.c`; `make test`, `make check`, `make parity`, `make webcheck`.

## Out of scope

Text layout, 9-slice panels, UI widgets, virtual resolution / camera2d, render
targets. Those belong to the 2D/UI layer (and a Clay-style layout module, see ROADMAP).
