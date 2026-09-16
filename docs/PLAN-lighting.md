# Plan: Lighting (light objects, per-scene lighting)

Status: **implemented (2026-09-16).** See `include/sk_light.h` and `examples/lights.c`.
Builds on the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)) and the
scene render passes (opaque, then sorted transparent). Leaves room for the
materials work in [ROADMAP.md](ROADMAP.md).

## Where we are

- **libsk today:** one hardcoded directional light plus ambient, baked into
  `apply_fs` in `src/sk_model.c` (direction -0.6, -1, -0.5; ambient 0.3). Models
  are lit; shapes and sprites are not. There is no API.
- **librl:** one global directional light, `rl_enable_lighting` /
  `rl_disable_lighting` / `rl_is_lighting_enabled` / `rl_set_light_direction` /
  `rl_set_light_ambient`, living in the camera module, off by default, models only.

What librl taught us: a single global light can't express lamps, torches,
spotlights or colored light; global state in an unrelated module is hard to find;
and names that don't follow `sk_<section>_<action>` fragment the API.

## Goals

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

## Non-goals (this plan)

- Shadows, PBR specular, image-based lighting, light probes, lightmaps.
- Lighting sprites, shapes or 2D. They stay unlit until materials land.
- Clustered/tiled light culling. Per-draw selection (below) is enough for tens of
  lights; clustering is a later optimization with the same API.
- Importing lights from glTF files (the parameters are compatible; import later).

## Public API

New header `include/sk_light.h`, new handle kind `SK_HANDLE_KIND_LIGHT = 16`.

```c
typedef enum {
    SK_LIGHT_DIRECTIONAL = 0, /* infinitely far: direction only (sun, moon) */
    SK_LIGHT_POINT = 1,       /* position + range (lamp, torch) */
    SK_LIGHT_SPOT = 2,        /* position + direction + range + cone (flashlight) */
} sk_light_type_t;

sk_handle_t sk_light_create(sk_light_type_t type);
void        sk_light_destroy(sk_handle_t light);

bool sk_light_set_color(sk_handle_t light, sk_handle_t color);   /* default white */
bool sk_light_set_intensity(sk_handle_t light, float intensity);  /* default 1 */
bool sk_light_set_position(sk_handle_t light, float x, float y, float z);  /* point, spot */
bool sk_light_set_direction(sk_handle_t light, float x, float y, float z); /* directional, spot; normalized */
bool sk_light_set_range(sk_handle_t light, float range);          /* point, spot; 0 = infinite */
bool sk_light_set_spot_cone(sk_handle_t light, float inner_angle, float outer_angle); /* spot, radians */
bool sk_light_set_enabled(sk_handle_t light, bool enabled);
bool sk_light_is_enabled(sk_handle_t light);
```

Scene integration reuses the existing membership API; the scene recognizes the
light handle kind:

```c
sk_scene_add(scene, light, layer);      /* layer is ignored for lights */
sk_scene_remove(scene, light);
sk_scene_set_ambient(scene, color, intensity);  /* default: intensity 0 (no ambient) */
```

A light can be in several scenes. Destroying a light removes it from lighting
everywhere (scenes resolve handles each frame, so a stale handle is skipped).

### Defaults

Decided: correct behavior over compatibility (see AGENTS.md).

- **A new scene has no lights and no ambient.** Its models render black until you
  add a light or set ambient. Nothing is lit implicitly.
- **Models drawn outside a scene** (`sk_model_draw`) have no lighting
  environment, so they render **unlit**: base color x tint. Lighting is something
  a scene provides.
- The built-in hardcoded light in `src/sk_model.c` is removed. Examples that draw
  models (`model`, `pick`, `simple`) add explicit lights.
- `sk_light_set_*` on a property that doesn't apply to the type (e.g. range on a
  directional light) stores it and returns true, so switching types later isn't
  lossy. Logging a warning would be noise.

## Shading

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

## Efficiency: per-draw light selection

- Uniform block holds **`SK_MAX_DRAW_LIGHTS = 8`** lights, packed std140 as four
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

## Internals

- `src/sk_light.c`: handle pool, light storage, public API, and a pure
  `sk_light_select(...)` used by `sk_model`.
- `src/internal/sk_light.h`: packed per-draw light data and the selection API.
- `sk_scene`: `sk_scene_set_ambient`; while drawing a layer, collect enabled lights
  (scene members with the light handle kind) and hand them to `sk_model` for the
  frame. Lights don't register draw passes, so the render passes ignore them.
- `sk_model`: `begin_draw` selects lights for the placement; `apply_fs` uploads
  them. The hardcoded light is removed; draws outside a scene upload an "unlit"
  flag so the shader outputs base color x tint.
- `sk_model.glsl`: world position varying, light loop, regenerated with `make shaders`.
- Parity map: `rl_enable_lighting`, `rl_disable_lighting`, `rl_is_lighting_enabled`,
  `rl_set_light_direction`, `rl_set_light_ambient` become `ported` to the light
  API (functional parity: a scene with one directional light and ambient).

## Verification

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

## Phasing

1. Light objects + API + scene membership + ambient; shader with the 8-light loop;
   per-draw selection; default light. Unit tests and the visual scene.
2. `examples/lights.c`, update `simple.c`, parity map, docs.

Later (separate plans): specular via materials, shadows for directional and spot
lights, glTF light import, clustered culling if scenes need hundreds of lights.

## Decisions (2026-09-16)

1. **8 lights per draw**, chosen by estimated contribution (above), not distance alone.
2. **No implicit lighting:** scenes start with no lights and no ambient; models
   drawn outside a scene are unlit.
3. **Shapes and sprites stay unlit** until the materials work.
