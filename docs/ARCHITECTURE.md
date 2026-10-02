# libwgrender resource architecture

Status: design locked, implementation in progress (see **Status** at the bottom).

This document captures the resource model libwgrender is converging on: the
**Asset → Resource → Object** layering, reference counting and deduplication, the
CPU-side vs GPU-side split, and how picking (including transparent/alpha-mask
picking, the problem that kicked this off) sits on top of it.

---

## 1. Three layers: Asset, Resource, Object

Everything loadable in libwgrender is described by three distinct layers. Keeping them
distinct is the whole point — it's what lets us preload, share, refcount, and
pick efficiently.

### Asset — the *source*
The bytes on disk (or in a bundle, or embedded). An asset is **not a runtime
type**; it's just where data comes from — a path today, a bundle entry later.

```
logo.png
woman_casual.glb
a_hero_is_born.mp3
JetBrainsMono
```

### Resource — *loaded runtime data*, built from an asset (or generated)
The decoded/uploaded runtime form of an asset. Resources are:

- **reference counted** — live while anything refers to them,
- **deduplicated** — loading the same asset twice returns the *same* resource,
- the natural home for **CPU-side data needed by the game** (e.g. picking).

A resource can also be **generated**: `wgr_mesh_create_plane`, `_cube`, `_sphere`,
`_cylinder`, `_cone`, `_capsule` and `_torus` make meshes with no file asset. They
follow the same rules: deduplicated (by their parameters instead of a path),
reference counted, and never changed once made (a differently sized placement scales
its model, or makes another mesh).

```c
wgr_handle_t tex   = wgr_texture_create("logo.png");      // Texture resource
wgr_handle_t mesh  = wgr_mesh_create("woman_casual.glb");      // Mesh resource
wgr_handle_t audio = wgr_audio_create("a_hero_is_born.mp3");  // Audio resource
wgr_handle_t font  = wgr_font_create("JetBrainsMono");    // Font resource
```

### Object — a *runtime instance* that references a resource
The lightweight, per-placement thing that lives in a scene. It carries its own
transform / tint / volume / playback state and points at a shared resource via
`set_<resource>()`.

### The full vocabulary

| Asset (source)        | Resource (loaded · refcounted · deduped) | Object(s) (`set_…`)              |
|-----------------------|------------------------------------------|----------------------------------|
| `logo.png`            | **Texture**                              | Sprite2d / Sprite3d (`set_texture`) |
| `woman_casual.glb` / *gen* | **Mesh** (primitives + skin + clips)     | **Model** (`set_mesh`)           |
| `a_hero_is_born.mp3` | **Audio** (decoded \| streamed)          | **Sound** (`set_audio`)          |
| `JetBrainsMono`       | **Font**                                 | Text2d / Text3d (`set_font`)     |
| *(code)* / `woman_casual.glb` | **Material** (shading + params + textures) | Model (`set_material`, per slot) |
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
- **A render target is a Texture.** `wgr_texture_create_target(w, h)` makes a texture
  you can draw into (`wgr_render_begin_texture`); everything that takes a texture
  accepts it. See [HISTORY.md: Render to texture](HISTORY.md#render-to-texture).
- **Material is a resource that objects use, not an object.** Meshes loaded from
  glTF create one material per glTF material (the mesh's slots); models draw with
  them unless they override a slot with `wgr_model_set_material`. Materials are
  created in code with `wgr_material_create(shading)`, not from a path. See
  [HISTORY.md: Materials and shaders](HISTORY.md#materials-and-shaders).
- **Light is an object with no resource.** Directional, point and spot lights are
  created with `wgr_light_create(type)` and added to scenes; nothing is loaded.
  See [HISTORY.md: Lighting (light objects, per-scene lighting)](HISTORY.md#lighting-light-objects-per-scene-lighting).
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
- What every resource has in common is the **resource section's**
  (`include/wgr_resource.h`, `src/wgr_resource.c`): a resource's record starts with
  a `wgri_resource_t` (reference count, status, the path it was created from, the
  file it was read from), and its module registers its pool with what is its own
  (its loader, a record's defaults, how to free what a record holds). The core does
  the reference counting, finding a resource by its path, load on create, the status,
  the path and release, once for every kind.
- A resource keeps a reference count. It is freed only when the count reaches zero.
- Two kinds of reference add to the count:
  1. **Instance references** — each object using a resource retains it (`+1`), and
     releases it when the object is destroyed or given another (`-1`).
  2. **Ownership references** — creating a resource adds a caller-owned `+1`,
     dropped by `wgr_resource_release`.
- **The names say which layer you're on:** resources are released with
  `wgr_resource_release` (drop *a* reference; the resource goes when the last one
  does, and one still loading stops loading), objects have `*_destroy` (the object is
  gone when you say so). Retaining is internal: one `create` is one reference, so
  callers never need a matching `retain`.
- **Dedup by path:** `*_create(path)` first looks for a resource created from the
  same asset path; if found it retains and returns it, whatever its status. A
  generated mesh is found by its parameters the same way.

This gives the behaviors we wanted:

- **Create once, spawn many.** Create a resource up front; create N objects that
  all share it. The resource lives until the last object *and* the creator's
  reference are gone.
- **Hand over and let go.** Give a resource to an object and release yours at once:
  the object keeps it alive, and when it's destroyed and nothing else holds the
  resource, the resource frees itself.

### Why this differs from librl
In librl, a model/texture is pushed to the GPU as soon as it's loaded, and the
only way to reclaim it is to destroy the resource; instances are lightweight
"draw that buffer again with this transform". libwgrender keeps the lightweight
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
callbacks; `wgr_scene_pick` casts a ray and dispatches.

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
} wgri_ray_hit_t;
```

Resolvers (`wgri_pick_result_from_local` / `_from_world`) convert that into the
public result, which carries **both** spaces plus a single world-space distance:

```c
typedef struct {
    bool        hit;
    wgr_handle_t handle;
    float       distance;     /* world-space distance from ray origin */
    vec3_t      point_local;
    vec3_t      point_world;
    vec3_t      normal_local;
    vec3_t      normal_world;
} wgr_pick_result_t;
```

### Transparent / alpha-mask picking (the problem that started this)
A Sprite3d is a textured quad. Ray↔quad alone reports a hit anywhere on the
quad, including fully transparent texels — clicking the empty corner of a logo
registered a hit. Fix:

1. The sprite's texture must retain a CPU **alpha mask** (created *pickable*).
2. On a quad hit, the barycentric `u,v` map to texture UVs; we sample the mask
   (`wgri_texture_sample_alpha`).
3. If alpha `< threshold`, the hit is **rejected** (the ray passes through).

The mask lives on the **Texture resource** (CPU-side), and the test is enabled
per **object** (`wgr_sprite3d_set_pick_alpha_test`, with a threshold). This is the
canonical example of the layering paying off: shared CPU data on the resource,
per-instance policy on the object.

Current surface (pre-rename): `wgr_texture_create_pickable` /
`wgr_texture_create_from_memory_pickable` opt a texture into keeping the mask.
Target: the mask is generated from the texture's alpha when a sprite enables
alpha-test picking, so a separate "pickable" constructor isn't required.

---

## 5. The Shape decision

**Since 2026-09-17 shapes are two types**, `wgr_shape2d` (screen space) and
`wgr_shape3d` (world), matching sprite2d/sprite3d and text2d/text3d: hat (1) below
is now `wgr_shape2d_draw_*` plus retained 2D shapes, hats (2) and (3) are
`wgr_shape3d_*`. The reasoning below stands as written.

`wgr_shape` wore **three hats**; only one overlaps Model:

1. **Immediate 2D primitives** (`draw_rectangle/circle/line/triangle`) — the 2D
   drawing API. **Keep.** Not a mesh, not an object.
2. **Immediate 3D debug draw** (`draw_line_3d`, `draw_cube_wires`, `draw_grid`,
   `draw_sphere`) — bufferless gizmo/debug drawing, re-emitted each frame.
   **Keep.** You don't want a GPU mesh for a debug grid.
3. **Retained cube/sphere *objects*** (`shape_create` + `set_cube/set_sphere`,
   with transform/color/visible/pickable) — **retire.** A retained cube/sphere
   is just `Model + generated Mesh + tint`. With mesh generators
   (`wgr_mesh_create_cube/sphere/plane`), `wgr_model_create_cube()` fully replaces
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
future rename `wgr_shape3d_draw_*` → `wgr_draw_*`); fold hat (3) into Model.

---

## 6. Handle kinds

Resources and objects are separate handle kinds (`include/wgr_handle.h`).

| Concept        | Layer    | Handle kind (current → target)                |
|----------------|----------|-----------------------------------------------|
| Texture        | resource | `WGR_HANDLE_KIND_TEXTURE`                       |
| Sprite2d/3d    | object   | `WGR_HANDLE_KIND_SPRITE2D` / `…SPRITE3D`       |
| Mesh           | resource | `WGR_HANDLE_KIND_MESH`                          |
| Model          | object   | `WGR_HANDLE_KIND_MODEL`                         |
| Primitive      | submesh  | (internal; not a handle)                       |
| Audio          | resource | `WGR_HANDLE_KIND_AUDIO`                          |
| Sound          | object   | `WGR_HANDLE_KIND_SOUND`                          |
| Font           | resource | `WGR_HANDLE_KIND_FONT`                          |
| Text2d/3d      | object   | `WGR_HANDLE_KIND_TEXT2D` / `…TEXT3D`           |

---

## 6b. Coordinates and anchors

One screen space: **logical pixels, top-left origin, y down**, used by input
positions, picks, clip rectangles and every 2D draw. The world is right-handed with
**+y up**, and all angles everywhere are radians. High-DPI is invisible to callers —
the scissor rectangle is the only place framebuffer pixels appear, and
`wgr_render_push_clip` converts for you. Texture source rectangles are in texture
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
code — enforced by `tools/check_rules.py`. See AGENTS.md § "Public API shape".

**One creation rule, no exceptions:** a *resource* is created from a path (or a
generator); an *object* is created from a resource handle. Bare `_create` for
both — the noun says which. No `_create_from_memory`, no "create object from
file" shortcut.

```c
/* Resource — from a path (loaded on create: deduped, refcounted) or a generator. */
wgr_handle_t wgr_mesh_create(const char *path);
wgr_handle_t wgr_mesh_create_cube(float w, float h, float l);   /* generated: READY at once */

/* Object — from a resource handle (adds its own reference). */
wgr_handle_t wgr_model_create(wgr_handle_t mesh);

/* Any resource. */
bool wgr_resource_release(wgr_handle_t resource);
wgr_resource_status_t wgr_resource_get_status(wgr_handle_t resource);
```

The same pattern applies to texture/sprite, audio/sound, font/text.

**A resource loads on create** (`include/wgr_resource.h`): `wgr_mesh_create(path)`
returns the handle at once, PENDING, and the asset layer makes the file local (from
disk, the cache, or a download), prepares it on a worker thread and fills it in on
the main thread within a per-frame upload budget; the handle is READY or FAILED in a
later frame, at the start of it. Nothing is called back: objects take the handle at
once and do the right thing until it's READY (a model isn't drawn, a texture draws
nothing in a sprite and its default in a material, a font draws as the built-in
one, a sound waits), and a program reads the status only for what it wants to show.
Each resource type gives a loader (`src/internal/wgr_loader_internal.h`: prepare on
any thread, fill on the main thread in steps). See
[HISTORY.md: Loading pipeline (background preparation, budgeted GPU upload)](HISTORY.md#loading-pipeline-background-preparation-budgeted-gpu-upload)
and [PLAN-tasks.md](PLAN-tasks.md).

```c
wgr_handle_t mesh = wgr_mesh_create("models/character.glb"); /* PENDING */
g_model           = wgr_model_create(mesh);                  /* drawn once it's READY */
wgr_resource_release(mesh);                                  /* the model keeps its own reference */
```

*Ensuring* a file (`wgr_asset_ensure`) only makes it local, without loading it: to
fetch ahead (a level's files during a menu), from an explicit source (a `fetch_url`),
or to read a file yourself. An ensure is a task, read as a resource is: PENDING, then
DONE (with the local path) or FAILED, kept until `wgr_asset_task_destroy`; a group of
them is one task for a loading screen. A key ensured from an explicit source is then
what a create of that key loads.

**The path is logical; the asset layer decides which file it is.** It stays under the
asset root: `.` and `..` are resolved, and a path that is absolute, names a drive or
climbs out is refused (the rule wgutils' fileio has), for what a program names and for
what a file references alike. Creating `textures/rock.png` may load another file, tried
in order until one exists:

1. **Redirects** (`wgr_asset_add_redirect`): path rules stack, newest first, so a mod
   or a translation overrides only the files it has; the file's own path comes last.
2. **Device variants** (path mappers, `src/internal/wgr_loader.h`): `rock.ktx` becomes
   the compressed file this GPU can sample, falling back to `rock.png`
   ([HISTORY.md: compressed textures](HISTORY.md#compressed-textures)).
3. **Where it downloads from** (web): the asset host, or a redirect's URL (a CDN); the
   file is still cached and named by its path.

**The web cache never shows a stale file by default.** A cached copy is kept with its
response's validators and freshness; one that isn't fresh is checked with the host
before it's used (304 keeps it, 200 replaces it, 4xx forgets it, no answer uses it, so
an offline start works). With a manifest (`wgr_asset_set_manifest`, one per directory,
hashes of the files' contents; `tools/gen_manifest.py`) the host is asked only about
the root once per run, and a file is fetched only when its hash changed, and kept only
when its bytes match ([HISTORY.md: a web asset cache that notices changed files](HISTORY.md#a-web-asset-cache-that-notices-changed-files)).

A resource is read from the file actually found (`wgr_resource_get_path` names it),
and the files it references (a glTF's buffers and images) resolve the same way: the
loader reads them from where the asset layer found them (`wgri_asset_found_path`). On
desktop a miss is a download when the host is a URL and the program
supplied a fetcher (`wgr_asset_set_fetcher`): libwgrender names a URL and a destination
file, the fetcher writes it, and bytes never cross the boundary — so the core carries no
HTTP client and no TLS. Networking beyond this (WebSockets, HTTP APIs) is outside libwgrender
([ROADMAP.md](ROADMAP.md)).

---

## 7b. Core and optional subsystems

A program links only the subsystems it uses. The **core** is always there: the
runtime (`wgr.c`), platform and window, input, rendering (sokol_gl), cameras, scenes,
picking math, files and assets, fonts and text, 2D/3D shapes, events and debug. The
rest are **optional modules** (`src/internal/wgri_module.h`): textures, lights,
materials, environments, models (with glTF), sprites and their batcher, particles,
2D/3D text objects, audio and sounds (with their decoders), and gamepads.

- An optional subsystem registers itself from a constructor in its own source file
  (`WGRI_MODULE`): a static library links that file only when the program references
  something in it (`wgr_model_create`, `wgr_sound_play`, ...). The registration gives
  the runtime its init order, init / deinit, and per-frame work (input: begin the
  frame before the ticks, end each tick, finish the frame after its callback; update
  after the ticks; flush before the render passes; end of frame after them). The runtime starts
  the linked modules after the core, in order, and stops them before it, in reverse.
- The core never calls an optional subsystem by name. What it needs from one goes
  through hooks the subsystem sets in its init: `wgri_render_hooks` (draw models and
  sprite batches, render-target textures) and `wgri_scene_hooks` (environments,
  lighting, sprite grouping). A hook that isn't set means the subsystem isn't linked,
  and the core does without (no lighting, no background).
- Asset loaders register in the subsystem's init, at startup, so assets still
  decode in the background before the program asks for them.
- `tools/check_rules.py` (the `check` test) fails when a core object references an
  optional one's symbols.

Effect on the web (gzipped): `hello` 134 KB, a sprite program ~170 KB, a program
drawing models ~240 KB, one using everything ~300 KB (all ~311 KB before).

---
