# Plan: Frustum culling

Status: **phase 1 built** (2026-09-21); phase 2 (the shadow pass's own cull) open.
Builds on the scene's bounds registry ([ARCHITECTURE.md](ARCHITECTURE.md)) and shadows
([PLAN-shadows.md](PLAN-shadows.md)).

## Why

Nothing is culled. A scene walks every member every frame and submits it; the only
visibility test is the manual `sk_model_set_visible` flag. The GPU throws away what
lands off screen, but only after libsk has paid for the draw.

Measured with `make shadowbench DESKTOP=1` on an RTX 4080: **4000 models cost 4.89 ms a
frame with the camera pointed away from all of them, against 5.18 ms with every one in
view** — the same price for drawing nothing. A world larger than its view pays for all
of it, and most worlds are.

At about 1.2 microseconds to submit a model, every object rejected is 1.2 microseconds
back. The test that rejects it is a few dozen instructions.

## Where we are

- `sk_scene` keeps members in layers and, for each layer, calls each drawable's
  `draw_opaque` and `collect_transparent` in turn (`draw_layer`).
- Every 3D kind already registers bounds with the scene for picking:
  `sk_scene_register_bounds(kind, fn)` gives a local AABB plus the model matrix, and
  models, sprite3d, shape3d and text3d all provide one.
- `sk_model`'s `begin_draw` already builds a world AABB (`sk_pick_world_aabb`) to pick
  the placement's lights, so a model's world bounds are computed either way.
- The shadow pass redraws **every** caster in the lighting environment, including ones
  outside what the light's map covers (`shadow_distance`).

## Proposed design

### Where the test goes

In `sk_scene`, not in each drawable: that is where the camera is, and one test then
covers models, sprites, shapes and text alike through the bounds registry. Each scene
draw builds the camera's six frustum planes once; `draw_layer` tests each member's
world AABB and skips the ones outside.

Pure helpers, in `internal/sk_math.h`, exposed for tests:

```c
/* The six planes of a view-projection, outward normals, for testing AABBs. */
void sk_frustum_from_view_proj(sk_mat4_t view_proj, sk_plane_t out[6]);
/* False when the box is wholly outside any plane (a conservative test: a box that
 * straddles a corner may pass and be drawn). */
bool sk_frustum_test_aabb(const sk_plane_t planes[6], vec3_t min, vec3_t max);
```

### Casters must not vanish

A model behind the camera can still throw a shadow into view, so culling to the
camera's frustum alone would make shadows pop at the screen edge. The volume tested for
a caster is therefore the camera frustum **extended along the light's direction** by the
light's shadow distance: if an object's swept box misses that, it can neither be seen
nor cast into view, and is skipped. Objects that only cast are still submitted to the
camera pass as they are today — correct, and no worse than now.

### The shadow pass culls too

Separately, `sk_model_draw_shadow_casters` tests each placement against the light's own
frustum (the fit it is already given) and skips casters outside it. A light's map covers
`shadow_distance`; today every caster in the scene is redrawn into it regardless. This
needs the placement to keep the world AABB it already computes for light selection.

### What it costs

One AABB build and six plane tests per member per scene draw, plus one more test per
caster per casting light in the shadow pass. Against ~1.2 microseconds saved per
rejected model, the test pays for itself at any scene size.

## Decisions (answered; phase 1 took all three recommendations)

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
3. **A switch.** `sk_scene_set_culling(scene, bool)`, default on, so a scene can turn it
   off when debugging what is drawn, or when a game knows everything is in view.
   Recommend: yes, it is two lines and it is the escape hatch if bounds are ever wrong.

## Phasing

1. The helpers, the scene's camera cull (all 3D kinds), the caster volume, the switch.
2. The shadow pass's own cull against each light's frustum.
3. Later, if wanted: the visibility mask from decision 1; 2D members against the screen
   rectangle; a spatial index so the per-member test itself stops scaling with the
   scene.

## Phase 1 as built

Three pure helpers in `src/internal/sk_math.h`, so the tests can reach them without a
GPU: `sk_frustum_from_view_proj` (Gribb-Hartmann, planes normalized so a test gives a
real distance), `sk_frustum_test_aabb` (the corner furthest along each normal — out
only when the box is wholly behind one plane) and `sk_aabb_sweep` (a box pushed along
a direction: where its shadow could land).

`sk_scene` builds the planes once per draw, in `begin_culling`, from the camera it is
about to draw with and the lighting environment it just pushed — the index comes from
`push_lighting`'s return, not from the light module, so the core still reaches lights
only through hooks. For each casting light it keeps the direction and the reach its map
covers (`shadow_distance`, or `range` for a spot whose range is shorter). `draw_layer`
then asks `visible()` per member, for the opaque pass and the transparent one alike.

`visible()` resolves the member's bounds through the scene's existing bounds registry,
so every 3D kind is covered at once — model, sprite3d, shape3d, text3d — and a member
with no bounds (the 2D kinds) is always drawn. The world AABB is padded by 15%
(`SK_CULL_PAD`) because a skinned model's bounds are its rest pose; better to draw a
little too much than to cull a raised arm. If the box misses the view, and the member
casts, the box is swept along each casting light and tested again: a caster off screen
whose shadow falls on screen is kept.

Two pieces moved to make that cheap. `sk_model` now caches the world matrix it builds
(`model_world`) instead of composing it for the cull and again for the draw, and it
answers `cull_bounds` with the posed bounds only when a pick already computed them —
culling never re-skins a mesh. `sk_scene_set_culling` / `sk_scene_is_culling` are the
switch, on by default.

### Measured

`make shadowbench DESKTOP=1` on an RTX 4080 gained two cases, "look away" and "away, no
cull", which point the camera outward from the grid so every model is behind it:

| models | away, no cull | look away | scene (cull test) |
|-------:|--------------:|----------:|------------------:|
|    100 |       0.31 ms |   0.11 ms |           0.02 ms |
|   1000 |       2.23 ms |   0.28 ms |           0.19 ms |
|   4000 |       6.78 ms |   0.50 ms |           0.42 ms |

So 4000 models nobody can see cost 6.78 ms before and 0.50 ms now, and what is left is
almost entirely the test itself — about 0.1 microseconds a member against the 1.2 it
saves. The shadow pass is skipped along with them: with nothing queued, nothing
receives. With everything in view the bench is unchanged (4000 models, no shadows: 4.96
against 5.18 before — the cached world matrix pays for the test).

## Verification

- Unit tests: the planes of a known projection; AABBs inside, outside and straddling;
  a scene where a member outside the view isn't submitted (the model queue's counts
  say so) and one inside is; a caster behind the camera still queued while a light
  casts, and not when none does; the switch.
- `shadowbench` with the camera pointed away should fall from ~4.9 ms to near nothing,
  and with everything in view should not get slower.
- Visual: `examples/shadows.c` and `examples/lights.c` unchanged as the camera turns —
  nothing pops at the edges, and shadows from off-screen casters stay.
- `make verify`, `make webcheck` (both backends), Wine.
