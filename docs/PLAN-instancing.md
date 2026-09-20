# Plan: Model instancing

Status: **phases 1 and 2 built** (2026-09-21); phases 3–4 open. Decisions below are answered:
automatic grouping with no new API (an explicit instanced handle only if a measured case
ever needs one), opaque models may be reordered, and custom shaders follow in phase 4 of
the same release.
Builds on the model draw queue (`src/sk_model.c`), the joint texture it already uses for
skinning, and the sprite batch's instancing (`src/sk_sprite_batch.c`), which is the
closest thing libsk already has to this.

## Why

A model is one draw call, always. Measured with `make shadowbench DESKTOP=1` on an RTX
4080: 4000 lit models cost about 5 ms a frame and roughly 95% of it is CPU submission —
about 1.2 microseconds a model, whatever the model is. Sharing one mesh between them
saves 12% (1.13 against 1.30 microseconds), so the cost is not buffer churn: it is the
per-model uniform uploads, the bindings and the draw call itself.

Now that culling has taken the models nobody can see out of the frame, this is the
largest cost left in every row of that benchmark. For comparison, three.js draws 16k
instanced meshes in about 2.3 ms.

## Where we are

Per model placement, `draw_primitive` does all of:

- a pipeline check (skinned × blended × double-sided);
- `sg_apply_uniforms(vs_params)` — mvp, model, normal matrix (192 bytes);
- `sg_apply_uniforms(fs_params)` — material **and the placement's tint**, plus five UV
  matrices, about 200 bytes, uploaded for every model with no caching;
- `fs_scene` and `fs_lights` only when their contents change (a `memcmp` against the
  last one), so those already skip most of the time;
- `sg_apply_bindings` with ten views and ten samplers;
- `sg_draw(0, index_count, 1)`.

The queue entry (`sk_model_draw_t`) already holds everything an instance needs: `mvp`,
`model_mat`, `normal_mat`, `tint`, `light_env`, the selected `lights`, `joint_base` and
(since culling) the world bounds.

## Proposed design

### Per-instance data lives in a texture, not in attributes

The shape that fits libsk is the one skinning already uses: a per-frame RGBA32F data
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
texture costs one texture binding and one sampler (libsk currently owns 8–9 of the 12
sampler slots, so there is room) and keeps the custom-shader contract to a single
include block.

### Every model draw becomes an instanced draw

Rather than a second, parallel "instanced" path, a placement's per-model uniforms move
into the record and *every* stock model draw becomes `sg_draw(0, n, count)` with
`count >= 1`. That removes `vs_params` entirely, takes the tint out of `fs_params` (it
arrives as a varying from the vertex stage), and leaves `fs_params` holding nothing but
material state — so it can take the same `memcmp` cache `fs_scene` and `fs_lights`
already have. Consecutive models sharing a material stop re-uploading it even before
any grouping happens.

### Grouping, and what may be reordered

Two placements can share a draw when everything outside the record matches: pass,
pipeline (skinned / blended / double-sided), primitive buffers, material, lighting
environment, the selected light set, and the shadow binding. Those keys are hashed once
per item; a run of equal keys becomes one `sg_draw` with that many instances.

Runs only exist if equal items are adjacent. Opaque parts are already drawn in an
explicitly unordered region (`begin_unordered` / `end_unordered` in `sk_scene`, where
sprites group by texture), so the items inside one command's range get sorted by that
key first. Transparent parts keep their back-to-front order and only group where equal
items are already neighbours.

### What this does not cover yet

A placement's light selection is per placement, so two models under different lights
can't share a draw until the light indices move into the record and the fragment stage
reads the environment's full light array instead of the eight selected ones. That is a
later phase; in the common cases (one sun, or a few lights over a group of objects) the
selected sets are identical anyway.

## Decisions

1. **Automatic, or opt-in?** Grouping can be invisible — no public API, libsk batches
   what it can — or explicit, e.g. an instanced-model handle the caller fills. Automatic
   keeps the API surface and means existing programs get faster with no change; explicit
   would let a caller promise that N placements share everything and skip the per-frame
   key building. Recommend: automatic. libsk's job is to make the obvious code fast.
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
   `sk_color`-style varying instead. Recommend: yes, and phase the custom-shader side
   (5) so nothing breaks in between.
5. **When do custom shaders follow?** Stock PBR can instance without touching
   `shaders/sk.glsl`; custom material shaders need the same include block, which bumps
   the `.skshader` format (7 → 8) and repacks `examples/assets/shaders/`. Recommend: in
   the same release, one phase later, so a custom shader is never the slow path for long.

## Phase 1 as built

`sk_model` keeps a second frame data texture beside the joint one: RGBA32F, eight texels
a placement, 128 records a row, written once in `sk_model_flush` before any pass. A
record holds three rows of the model matrix, three of its inverse transpose, the tint
(converted to linear on the way in) and the skin base. `vs_params` is now the camera's
view-projection and the draw's first record; `vs_skin_params` is the same, since the
joint base moved into the record. Every stock model draw is `sg_draw(0, n, 1)` with its
own record — one instance each, until phase 2 groups them.

Two things fell out of it. The tint left `fs_params`: the vertex stage multiplies it
into `v_color`, and because the fragment stage already computed
`base_sample * u_base_color * v_color`, that is the same arithmetic in a different
place — no fragment change, and custom shaders that read `sk_color` keep working. What
is left in `fs_params` is material state only, which is what lets phase 2 group.

Measured: nothing moved. 4000 models with no shadows 5.28 ms against a 4.96–5.57 band
over five runs, a casting sun 5.15, two lights 5.33 — the per-placement uniform upload
(192 bytes a model) is gone and the vertex shader's texture fetch replaced it, which is
a wash. Phase 1 is not meant to be faster; it is meant to put the data where phase 2 can
draw thousands of placements from one call.

## Phase 2 as built

An item remembers which *unordered region* it was submitted in — `sk_scene` already
declares those around the opaque part of a layer, for sprites, and now tells `sk_model`
too through two scene hooks. Inside one region the items may be drawn in any order, so
`sk_model_flush` sorts each region by a hash of everything a draw has to set outside the
instance record: material, primitive buffers, pipeline (skinned / blended / double
sided), pass, lighting environment, the selected light set, whether the model receives
shadows, and the camera. See-through parts are marked region −1 and never move, so the
back-to-front order stands.

Records are written per item in that sorted order, so a run of equal items occupies
consecutive records. `sk_model_draw_items` then walks the run, comparing each item to
the first *exactly* (the hash only decides the sort; a collision costs a split, never a
wrong batch), and issues one `sg_draw` with that many instances. A material with a
custom shader never joins a run — that path has its own uniforms per placement until
phase 4.

### Measured

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

### Reviewed (2026-09-21)

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
- Native GL was exercised by `make smoke` and the desktop build; a screen capture of the
  real display came back black (capture-side), so the pixels were checked on the two
  browser backends, which share the same sokol GL path.

## Phasing

1. The instance texture and the record; every stock model draw becomes an instanced draw
   of one; `fs_params` loses the tint. No grouping yet — this phase must not be slower,
   and proves the record is right. **Built.**
2. The group key, sorting a command's opaque items by it, and runs of equal keys drawn
   as one `sg_draw`. This is the phase the numbers come from. **Built.**
3. The depth pass (shadows) instanced the same way — it is the same queue, and it is
   already the second-largest consumer of it.
4. Custom material shaders: the `sk.glsl` include block, format bump, repack.
5. Later, if wanted: per-instance light sets, so grouping isn't constrained by light
   selection; transparent runs; a persistent instance buffer for placements that don't
   move.

## Verification

- Unit tests: a record's layout round-trips (build the queue, read back what the texture
  would hold); two placements sharing everything produce one draw and two instances,
  while a difference in each key (material, mesh, light set, pipeline, pass) splits them;
  tint arrives per instance.
- `shadowbench` and `spritebench`: 4000 models sharing a mesh and material should fall
  from ~5 ms toward the cost of a few draws; nothing should get slower, in particular the
  4000-distinct-materials case, which cannot group at all.
- A new bench case or example with many copies of one model (the forest), since that is
  what this is for.
- Visual: `examples/instancing.c` — a batched field, skinned copies, see-through copies —
  on both browser backends; `model.c`, `lights.c`, `shadows.c`, `materials.c` unchanged;
  tints and per-model materials still right.
- `make verify`, `make webcheck` (both backends), Wine.
