# libsk resource architecture

Status: design locked, implementation in progress (see **Status** at the bottom).

This document captures the resource model libsk is converging on: the
**Asset → Resource → Object** layering, reference counting and deduplication, the
CPU-side vs GPU-side split, and how picking (including transparent/alpha-mask
picking, the problem that kicked this off) sits on top of it.

---

## 1. Three layers: Asset, Resource, Object

Everything loadable in libsk is described by three distinct layers. Keeping them
distinct is the whole point — it's what lets us preload, share, refcount, and
pick efficiently.

### Asset — the *source*
The bytes on disk (or in a bundle, or embedded). An asset is **not a runtime
type**; it's just where data comes from — a path today, a bundle entry later.

```
logo.png
gumshoe.glb
ethernight_club.mp3
JetBrainsMono
```

### Resource — *loaded runtime data*, built from an asset (or generated)
The decoded/uploaded runtime form of an asset. Resources are:

- **reference counted** — live while anything refers to them,
- **deduplicated** — loading the same asset twice returns the *same* resource,
- the natural home for **CPU-side data needed by the game** (e.g. picking).

A resource can also be **generated** (a procedural cube mesh has no file asset).

```c
sk_handle_t tex   = sk_texture_create("logo.png");      // Texture resource
sk_handle_t mesh  = sk_mesh_create("gumshoe.glb");      // Mesh resource
sk_handle_t audio = sk_audio_create("ethernight.mp3");  // Audio resource
sk_handle_t font  = sk_font_create("JetBrainsMono");    // Font resource
```

### Object — a *runtime instance* that references a resource
The lightweight, per-placement thing that lives in a scene. It carries its own
transform / tint / volume / playback state and points at a shared resource via
`set_<resource>()`.

### The full vocabulary

| Asset (source)        | Resource (loaded · refcounted · deduped) | Object(s) (`set_…`)              |
|-----------------------|------------------------------------------|----------------------------------|
| `logo.png`            | **Texture**                              | Sprite2d / Sprite3d (`set_texture`) |
| `gumshoe.glb` / *gen* | **Mesh** (primitives + skin + clips)     | **Model** (`set_mesh`)           |
| `ethernight_club.mp3` | **Audio** (decoded \| streamed)          | **Sound** (`set_audio`)          |
| `JetBrainsMono`       | **Font**                                 | Text2d / Text3d (`set_font`)     |
| *(code)* / `gumshoe.glb` | **Material** (shading + params + textures) | Model (`set_material`, per slot) |
| `venice_sunset_1k.hdr` | **Environment** (irradiance + prefiltered cubemap) | Scene (`set_environment`, `set_background`) |
| *(none)*              | *(none)*                                 | **Light** (added to a Scene)     |

Rule that disambiguates every row: **resource = the data noun, object = the
concrete placed/heard noun.** `Texture→Sprite`, `Mesh→Model`, `Audio→Sound`,
`Font→Text`.

Naming notes / decisions:

- **Mesh, not Model, is the resource.** This dissolves the old "instance is a
  model and the asset is a model" collision. A **Mesh** resource follows the
  glTF shape: a Mesh is a collection of **Primitives** (submeshes), and it also
  bundles the skeleton + animation clips that came with it. (If clip sharing
  ever matters, clips can be split into their own resource later — not now.)
- **Texture, not Image.** There is no separate public `Image` type; the Texture
  resource carries the optional CPU-side alpha mask used for picking.
- **A render target is a Texture.** `sk_texture_create_target(w, h)` makes a texture
  you can draw into (`sk_render_begin_texture`); everything that takes a texture
  accepts it. See [PLAN-render-target.md](PLAN-render-target.md).
- **Material is a resource that objects use, not an object.** Meshes loaded from
  glTF create one material per glTF material (the mesh's slots); models draw with
  them unless they override a slot with `sk_model_set_material`. Materials are
  created in code with `sk_material_create(shading)`, not from a path. See
  [PLAN-materials.md](PLAN-materials.md).
- **Light is an object with no resource.** Directional, point and spot lights are
  created with `sk_light_create(type)` and added to scenes; nothing is loaded.
  See [PLAN-lighting.md](PLAN-lighting.md).
- **Audio (resource) → Sound (object).** "audio" is the loaded data ("load the
  audio"); "a sound" is the concrete thing you play and position ("play a
  sound", `sound_set_volume`). `play_sfx()` / `play_music()` are thin
  convenience wrappers; *music vs sfx is not a type distinction* — it's the
  Audio's load mode (streamed vs decoded).

---

## 2. Reference counting & deduplication

Resources are shared; objects are cheap and private.

- Resources are stored in their own handle pool (kind `*_…` resource kind),
  separate from the object pool.
- A resource keeps a `ref_count`. It is freed only when the count reaches zero.
- Two kinds of reference add to the count:
  1. **Instance references** — each object created from a resource retains it
     (`+1`), and releases it when the object is destroyed (`-1`).
  2. **Ownership references** — explicitly loading a resource
     (`*_create`/preload) adds a caller-owned `+1`, dropped by `*_release`.
- **The names say which layer you're on:** resources have `*_release` (drop *a*
  reference; the resource goes when the last one does), objects have `*_destroy`
  (the object is gone when you say so). Retaining is internal: one `create` is one
  reference, so callers never need a matching `retain`.
- **Dedup by source path:** `*_create(path)` first looks for an existing
  resource with the same path; if found it just retains and returns it (and
  skips the file read entirely). Generated resources are not deduped (no path).

This gives the behaviors we wanted:

- **Preload once, spawn many.** Load a resource up front; create N objects that
  all share it. The resource lives until the last object *and* the owning load
  are gone.
- **Implicit lifetime for simple callers.** `model = sk_model_create("x.glb")`
  loads-or-shares a Mesh and returns a Model; when the Model is destroyed and
  nothing else references the Mesh, the Mesh frees itself. Simple callers never
  touch resources.

### Why this differs from librl
In librl, a model/texture is pushed to the GPU as soon as it's loaded, and the
only way to reclaim it is to destroy the resource; instances are lightweight
"draw that buffer again with this transform". libsk keeps the lightweight
instance idea but makes the shared thing a **first-class, refcounted, deduped
resource** with an explicit lifecycle, instead of an internal side effect of
the first load.

---

## 3. CPU-side vs GPU-side

A resource has two representations:

- **GPU resource** — what the renderer binds: textures/images, vertex/index
  buffers, samplers, views.
- **CPU resource** — data the *game* needs on the CPU at runtime, independent of
  drawing.

The word **"resource"** elsewhere in this doc is the umbrella; **CPU/GPU** are
just *which side of the bus* a given piece of the resource lives on.

### The constraint
With sokol, `sg_make_image` / `sg_make_buffer` upload immediately — creating the
GPU resource also consumes GPU memory right then. So "preload to CPU without
filling the GPU" is **not** free with the current creation path; today, creating
a resource uploads it. Deferred upload (CPU-resident resource that uploads
lazily on first draw) is a known future option, but **not** required for the
current milestone — preload currently means "load + upload, shared".

### What we deliberately keep CPU-side (for picking)
- **Mesh:** retained bind-pose vertex **positions** and **indices** per
  primitive (`pick_positions`, `pick_indices`). These drive exact ray↔triangle
  picking without reading back from the GPU. Freed with the Mesh resource.
- **Texture:** an optional CPU **alpha mask** (`~w*h` bytes), retained only when
  the texture is created *pickable*. Drives alpha-tested ("don't pick
  transparent pixels") sprite picking. Freed with the Texture resource.

These are exactly the "CPU-side data the game needs" that the resource layer is
the natural home for.

---

## 4. Picking

Picking is a scene service: each drawable kind registers `bounds` and `pick`
callbacks; `sk_scene_pick` casts a ray and dispatches.

### Two phases
1. **Broadphase** — ray vs the object's world-space AABB (cheap reject /
   ordering). Meshes precompute a merged local AABB at load; the world AABB comes
   from the object transform.
2. **Narrowphase** — exact test in the object's *local* space (the ray is
   transformed into local space via the inverse model matrix), per object kind:
   - **Mesh/Model:** ray↔triangle (Möller–Trumbore, double-sided) against the
     retained bind-pose geometry. Skinned meshes are tested against the **bind
     pose** (matches raylib).
   - **Retained primitive objects (cube/sphere):** *analytic* ray↔AABB and
     ray↔sphere — exact, no tessellation. (See the Shape decision below.)
   - **Sprite3d:** ray↔quad (the billboard), then the alpha test below.

### Ray/hit types
Low-level tests report a raw hit in the ray's space:

```c
typedef struct {
    bool  hit;
    float t;        /* distance along ray.dir (unit dir ⇒ length) */
    vec3_t point;   /* in the ray's space */
    vec3_t normal;  /* in the ray's space, oriented against the ray */
    float u, v;     /* barycentric weights of v1/v2 (triangle tests only) */
} sk_ray_hit_t;
```

Resolvers (`sk_pick_result_from_local` / `_from_world`) convert that into the
public result, which carries **both** spaces plus a single world-space distance:

```c
typedef struct {
    bool        hit;
    sk_handle_t handle;
    float       distance;     /* world-space distance from ray origin */
    vec3_t      point_local;
    vec3_t      point_world;
    vec3_t      normal_local;
    vec3_t      normal_world;
} sk_pick_result_t;
```

### Transparent / alpha-mask picking (the problem that started this)
A Sprite3d is a textured quad. Ray↔quad alone reports a hit anywhere on the
quad, including fully transparent texels — clicking the empty corner of a logo
registered a hit. Fix:

1. The sprite's texture must retain a CPU **alpha mask** (created *pickable*).
2. On a quad hit, the barycentric `u,v` map to texture UVs; we sample the mask
   (`sk_texture_sample_alpha`).
3. If alpha `< threshold`, the hit is **rejected** (the ray passes through).

The mask lives on the **Texture resource** (CPU-side), and the test is enabled
per **object** (`sk_sprite3d_set_pick_alpha_test`, with a threshold). This is the
canonical example of the layering paying off: shared CPU data on the resource,
per-instance policy on the object.

Current surface (pre-rename): `sk_texture_create_pickable` /
`sk_texture_create_from_memory_pickable` opt a texture into keeping the mask.
Target: the mask is generated from the texture's alpha when a sprite enables
alpha-test picking, so a separate "pickable" constructor isn't required.

---

## 5. The Shape decision

**Since 2026-09-17 shapes are two types**, `sk_shape2d` (screen space) and
`sk_shape3d` (world), matching sprite2d/sprite3d and text2d/text3d: hat (1) below
is now `sk_shape2d_draw_*` plus retained 2D shapes, hats (2) and (3) are
`sk_shape3d_*`. The reasoning below stands as written.

`sk_shape` wore **three hats**; only one overlaps Model:

1. **Immediate 2D primitives** (`draw_rectangle/circle/line/triangle`) — the 2D
   drawing API. **Keep.** Not a mesh, not an object.
2. **Immediate 3D debug draw** (`draw_line_3d`, `draw_cube_wires`, `draw_grid`,
   `draw_sphere`) — bufferless gizmo/debug drawing, re-emitted each frame.
   **Keep.** You don't want a GPU mesh for a debug grid.
3. **Retained cube/sphere *objects*** (`shape_create` + `set_cube/set_sphere`,
   with transform/color/visible/pickable) — **retire.** A retained cube/sphere
   is just `Model + generated Mesh + tint`. With mesh generators
   (`sk_mesh_create_cube/sphere/plane`), `sk_model_create_cube()` fully replaces
   it, giving one 3D scene-object type, one pick path, and materials/lighting/
   sharing for free.

Costs accepted by retiring (3):
- Generated primitives go through the buffered lit/textured pipeline instead of
  immediate `sokol_gl` (a per-object GPU buffer; fine for normal counts, heavier
  for "500 wireframe boxes" — use the debug-draw API for that).
- Sphere picking becomes faceted (triangle-accurate) instead of analytic
  ray↔sphere. Acceptable; reintroduce an analytic "primitive object" later only
  if needed.

Net: keep hats (1) and (2) as a standalone **draw/gizmo** utility (candidate
future rename `sk_shape3d_draw_*` → `sk_draw_*`); fold hat (3) into Model.

---

## 6. Handle kinds

Resources and objects are separate handle kinds (`include/sk_handle.h`).

| Concept        | Layer    | Handle kind (current → target)                |
|----------------|----------|-----------------------------------------------|
| Texture        | resource | `SK_HANDLE_KIND_TEXTURE`                       |
| Sprite2d/3d    | object   | `SK_HANDLE_KIND_SPRITE2D` / `…SPRITE3D`       |
| Mesh           | resource | `SK_HANDLE_KIND_MESH`                          |
| Model          | object   | `SK_HANDLE_KIND_MODEL`                         |
| Primitive      | submesh  | (internal; not a handle)                       |
| Audio          | resource | `SK_HANDLE_KIND_AUDIO`                          |
| Sound          | object   | `SK_HANDLE_KIND_SOUND`                          |
| Font           | resource | `SK_HANDLE_KIND_FONT`                          |
| Text2d/3d      | object   | `SK_HANDLE_KIND_TEXT2D` / `…TEXT3D`           |

---

## 6b. Coordinates and anchors

One screen space: **logical pixels, top-left origin, y down**, used by input
positions, picks, clip rectangles and every 2D draw. The world is right-handed with
**+y up**, and all angles everywhere are radians. High-DPI is invisible to callers —
the scissor rectangle is the only place framebuffer pixels appear, and
`sk_render_push_clip` converts for you. Texture source rectangles are in texture
pixels, also top-left.

What differs per noun is **where an object's position sits on it**, and each default
is the one that noun is usually placed by:

| object | anchor | default |
|---|---|---|
| sprite2d | `set_pivot`, a fraction of the quad | `(0.5, 0.5)`, its center |
| sprite3d | `set_pivot`, a fraction of the quad (y runs down the texture) | `(0.5, 0.5)`; `(0.5, 1)` stands it on the ground |
| shape2d rectangle | `set_pivot`, a fraction of the bounds | its top-left corner, like UI layout |
| shape2d circle | `set_pivot`, a fraction of the bounds | its center |
| shape2d line | its own endpoints (no pivot) | — |
| text2d | `set_align`, a 9-point grid | LEFT / TOP |
| text3d | `set_align`, the same enum | CENTER / MIDDLE |
| model, shape3d | the mesh's or shape's own origin | — |

Two mechanisms, not three: a pivot is continuous (any fraction, including outside
0..1), alignment is the 9-point form of the same idea and additionally lines up
wrapped lines inside the block. Text uses alignment because it needs that second job.

## 7. Public API shape

The public surface is **handle-only**: every parameter/return is a handle, an
integral/float/enum, or a `const char *` (path/text). No raw pointers in user
code — enforced by `tools/check_naming.sh`. See AGENTS.md § "Public API shape".

**One creation rule, no exceptions:** a *resource* is created from a path (or a
generator); an *object* is created from a resource handle. Bare `_create` for
both — the noun says which. No `_create_from_memory`, no "create object from
file" shortcut.

```c
/* Resource — from a path (load-or-share: deduped, refcounted) or a generator. */
sk_handle_t sk_mesh_create(const char *path);
sk_handle_t sk_mesh_create_cube(float w, float h, float l);   /* generated */
void        sk_mesh_release(sk_handle_t mesh);

/* Object — from a resource handle (adds its own reference). */
sk_handle_t sk_model_create(sk_handle_t mesh);
```

The same pattern applies to texture/sprite, audio/sound, font/text.

**Loading is split from creation** (`include/sk_asset.h`): *ensure* the file is
local (async), then *create* synchronously from the path in the ready callback —
which receives a path, never bytes. Before the callback fires, the asset pipeline
also loads the file as the resource its extension names (decoded on worker
threads, uploaded on the main thread within a per-frame budget), so the create in
the callback only finds it. Each resource type registers a loader
(`src/internal/sk_loader.h`: prepare on any thread, finish on the main thread in
steps); the sync create runs the same loader inline. See
[PLAN-pipeline.md](PLAN-pipeline.md).

```c
static void on_ready(const char *path, void *user) {
    sk_handle_t mesh  = sk_mesh_create(path);   /* resource ← file        */
    g_model           = sk_model_create(mesh);  /* object   ← resource    */
    sk_mesh_release(mesh);                       /* model keeps its own ref */
}
sk_asset_add_task(sk_asset_ensure_async(path, NULL, SK_ASSET_NONE), on_ready, on_failed, ctx);
```

---

## 7b. Core and optional subsystems

A program links only the subsystems it uses. The **core** is always there: the
runtime (`sk.c`), platform and window, input, rendering (sokol_gl), cameras, scenes,
picking math, files and assets, fonts and text, 2D/3D shapes, events and debug. The
rest are **optional modules** (`src/internal/sk_module.h`): textures, lights,
materials, environments, models (with glTF), sprites and their batcher, particles,
2D/3D text objects, audio and sounds (with their decoders).

- An optional subsystem registers itself from a constructor in its own source file
  (`SK_MODULE`): a static library links that file only when the program references
  something in it (`sk_model_create`, `sk_sound_play`, ...). The registration gives
  the runtime its init order, init / deinit, and per-frame work (update after the
  ticks; flush before the render passes; end of frame after them). The runtime starts
  the linked modules after the core, in order, and stops them before it, in reverse.
- The core never calls an optional subsystem by name. What it needs from one goes
  through hooks the subsystem sets in its init: `sk_render_hooks` (draw models and
  sprite batches, render-target textures) and `sk_scene_hooks` (environments,
  lighting, sprite grouping). A hook that isn't set means the subsystem isn't linked,
  and the core does without (no lighting, no background).
- Asset loaders register in the subsystem's init, at startup, so assets still
  decode in the background before the program asks for them.
- `make check` (`tools/check_modules.sh`) fails when a core object references an
  optional one's symbols.

Effect on the web (gzipped): `hello` 134 KB, a sprite program ~170 KB, a program
drawing models ~240 KB, one using everything ~300 KB (all ~311 KB before).

---

## 8. Status

- **Done — Mesh/Model split + vocabulary (Phase 1).** `sk_model.c` separates a
  shared, refcounted, path-deduped resource from a lightweight instance, in the
  final vocabulary:
  - **Mesh** resource (`sk_mesh_t`, kind `SK_HANDLE_KIND_MESH`) owns primitives
    (`sk_primitive_t`) + GPU buffers + retained pick geometry + skeleton + clips
    + merged AABB + `ref_count` + `path`;
  - **Model** object (`sk_model_t`, kind `SK_HANDLE_KIND_MODEL`) owns
    transform / tint / visibility / animation playback / joint matrices, and
    references a Mesh via its `mesh` handle;
  - two handle pools; `create_mesh`/`find_mesh_by_path`/`retain_mesh`/
    `release_mesh`/`create_model`;
  - public API: `sk_mesh_create` / `sk_mesh_create_from_memory` /
    `sk_model_create_from_mesh` / `sk_mesh_release`, plus backward-compatible
    `sk_model_create` / `sk_model_create_from_memory` sugar. Builds clean (lib +
    examples + `make check`).

- **Pending — procedural mesh generators** (`sk_mesh_create_cube/sphere/plane`)
  and **retire the retained Shape object** into Model; keep the immediate
  draw/gizmo API.

- **Pending — Texture resource split** (Sprite objects share Texture resources;
  alpha mask on the resource, generated on demand for alpha-test picking).

- **Done — Audio/Sound split, Music folded in.** `sk_audio.c` owns an **Audio**
  resource (`sk_audio_t`, kind `SK_HANDLE_KIND_AUDIO`) holding decoded PCM,
  refcounted and path-deduped; the mixer plays **Sound** objects (`sk_sound_t`,
  kind SOUND) that carry playback state (`pos`/`volume`/`pitch`/`loop`/`playing`)
  and reference an Audio by handle. There is **no separate Music type** — a
  looping background track is just a Sound with `sk_sound_set_loop(true)`
  (`sk_music_*` and kind MUSIC are gone). `sk_sound_play/pause/resume/stop`.

- **Pending — streamed Audio.** Today every Audio is fully decoded into PCM, so a
  long music track is decoded whole into RAM. The doc's *decoded | streamed* load
  mode (chosen at `sk_audio_create`) is the proper fix: the Audio holds the shared
  compressed source, and a Sound playing a streamed Audio carries its own decoder
  + ring buffer (single-buffer sharing only works for decoded PCM). Pairs with
  the `sk_fs`/host-fetch work. `play_sfx`/`play_music` sugar optional on top.
