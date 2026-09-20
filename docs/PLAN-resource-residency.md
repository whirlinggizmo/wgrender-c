# Plan: GPU resource residency (decoupled upload + optional eviction)

Status: **proposed — awaiting approval.** No code changed yet.
Builds on the Asset→Resource→Object model ([ARCHITECTURE.md](ARCHITECTURE.md)) and
the `wgr_fs` local cache ([PLAN-wgr_fs.md](PLAN-wgr_fs.md)).

## Reality check — how much of this do we actually need?

Be honest about scope before building machinery:

- **Most games (including shipped raylib games) never need residency management.**
  Their entire texture+mesh set fits in VRAM (modern GPUs: 4–24 GB), so you load
  it all up front (or per level) and it stays resident. Where it doesn't fit,
  games do **coarse per-level load/unload** — and that's enough.
- **Streaming/eviction earns its keep only when the working set can't fit:** open
  worlds, hundreds of 4K textures, low-VRAM targets (mobile / integrated / web),
  or unbounded user content.

So this splits into a cheap, high-value part and an advanced, deferrable part:

- **Cheap 80% (worth doing):** *decouple upload from create* so loading/caching
  an asset doesn't pin VRAM, plus explicit `unload`/`evict` and the refcounting we
  already have. Removes the "create pins VRAM forever" footgun with almost no
  machinery.
- **Advanced 20% (defer until needed):** automatic **LRU + VRAM budget +
  lazy paging**. Only pays off when a frame's working set genuinely exceeds VRAM.
  Many projects never reach this.

The phasing below is ordered so we get the 80% first and only build the LRU
budget when a real case (a scene that won't fit) forces it.

## Why (the ceiling)

Resource creation today **fuses** decode-CPU-data with upload-to-GPU
(`sg_make_image`/`sg_make_buffer`), so `wgr_texture_create`/`wgr_mesh_create`
occupies VRAM whether or not it's drawn. You can't pre-cache a large library
without exhausting GPU memory — the structural ceiling that makes eager-upload
engines "toy-like" at scale. sokol_gfx is retained/explicit (unlike its immediate
*helper* layers), so we control when residency happens; `wgr_model` already proves
the pattern (mesh uploaded once, redrawn for free).

## What librl actually did (prior art)

librl had an LRU — but it was a **CPU cache of encoded file bytes** in `rl_fs`
(`lru_cache`, 32 MB / 256 entries / 8 MB max-entry), read path
*in-memory bytes → local storage → network*. It never paged GPU resources:
raylib uploaded on load and only freed at destroy/deinit. So:

- librl's byte-LRU role is **already covered by our `wgr_fs` cache** (encoded bytes
  in MEMFS/idbfs on web, disk on desktop).
- **GPU residency is genuinely new work** — librl never did it. The `lru_cache`
  data structure is reusable, but keyed on resources and accounting `sg_image`/
  `sg_buffer` *bytes*, evicting via `sg_destroy_*`.

## The model: two tiers, per resource type

Source of truth lives in the small **encoded** tier; VRAM holds only what's drawn.

```
encoded backing  ─decode/build─▶  GPU-resident (sg_image / sg_buffer)
(wgr_fs cache,                          │
 or retained CPU geometry)  ◀─evict────┘   (free VRAM, stay referenced)
```

- **Texture** — near-pure GPU resource. Backing = the **encoded bytes in the
  `wgr_fs` cache** (tiny vs decoded pixels). Evict VRAM → reload by decoding from
  the cache. No persistent decoded-CPU copy needed (that middle tier is a later
  optimization, only if decode-on-page-in proves costly).
- **Mesh** — hybrid: its Resource keeps CPU data needed *even when not drawn*
  (pick geometry, skeleton, animation clips, AABB). Only the vertex/index
  `sg_buffer`s are the pageable VRAM part. Backing = **retained CPU geometry**, so
  evict → re-upload with no re-parse/disk. (Today we retain only pick *positions*;
  to re-upload we'd retain the full interleaved vertex stream — and can unify it so
  one CPU buffer serves both picking and re-upload.)
- **Generated resources** (`create_cube`, procedural textures) have **no encoded
  backing to reload from**, so they must either keep their CPU data (to stay
  evictable) or be **pinned** (non-evictable). For meshes this falls out of
  keeping CPU geometry anyway.

So **CPU retention is per-type, not one global knob**: textures lean fs-reload,
meshes keep CPU geometry, generated resources keep-CPU-or-pin.

## Eviction: two lifetimes, never conflated

The trap is tying eviction to instances/scene membership. It must not be:

- **`ref_count`** (already exists) answers *"referenced at all?"* → at 0, the
  resource is **destroyed**. This is the instance/scene-removal signal. It does
  **not** drive eviction (refcount may still be > 0).
- **`last_used` (frame/tick), new** answers *"in VRAM right now?"* → drives
  **eviction**. Eviction is about VRAM pressure + draw recency, not references.

Mechanism:

1. **Touch on draw.** Each frame an object draws, it stamps its (shared) resource
   `last_used = current_frame` and ensures residency (upload on miss). Many
   instances sharing one resource all touch the same entry — hot while *any* of
   them draws it.
2. **Evict lazily, under pressure.** When an upload would exceed the VRAM budget,
   evict the **coldest** resident resources (smallest `last_used`) via
   `sg_destroy_*` until there's room. Evicted resources stay *referenced* and
   reloadable; the next draw pages them back in.
3. **Protect the working set.** Only evict entries **not touched this frame**
   (`last_used < current_frame`); anything drawn this frame is untouchable. This
   is exactly why "someone else is still using it" is a non-issue — if anyone
   draws it this frame, it can't be evicted.
4. **Over-subscription is a budget problem, not an LRU one.** If a single frame's
   working set exceeds the budget, everything is touched-this-frame and LRU can't
   help — the honest responses are grow the budget or accept re-upload churn.

## sokol primitives this relies on

- `sg_alloc_image` + `sg_init_image` (+ buffer equivalents) — two-phase: reserve a
  handle now, fill GPU contents lazily.
- `sg_destroy_image` / `sg_destroy_buffer` — free VRAM, keep the resource.
- `sg_query_*_state` — residency checks.
- Verify: first upload via `sg_init_*` mid-frame is allowed (only per-frame
  `sg_update_*` has the once-per-frame rule); lazy-on-first-draw should be fine.

## Phasing (80% first, LRU only when forced)

1. **Decouple upload from create (the high-value, low-machinery step).**
   Create makes a resource *CPU/encoded-resident*; the `sg_image`/`sg_buffer` is
   created on first draw (object resolve hook) or via explicit `wgr_*_warm()`. Add
   `wgr_*_evict()` (drop GPU, keep resource). No budget, no automatic eviction.
   This alone removes the "create pins VRAM forever" ceiling and enables
   load-without-uploading.
2. **Coarse group/scene unload** (optional convenience): evict all GPU residency
   for a set of resources at a boundary — covers the common "per-level" pattern
   most games actually use.
3. **Automatic LRU + VRAM budget (advanced; build only when a real scene won't
   fit).** `last_used` touch-on-draw, byte accounting, `wgr_set_vram_budget(bytes)`,
   evict-coldest-on-pressure with working-set protection.
4. **Mesh residency** reusing the same layer (retain full CPU vertex stream;
   unify with pick data).
5. **Later:** persistent decoded-CPU tier for textures (skip re-decode on
   page-in) and mip/streaming refinements — only if profiling demands them.

## Scope

- VRAM-backed resources only: **texture** (`sg_image`) and **mesh** (`sg_buffer`),
  texture first. **Audio is out of scope** — decoded PCM is RAM, not VRAM.

## Open questions for sign-off

- Phase 1 trigger: pure **lazy-on-first-draw**, plus optional `warm()`? (friendly)
- Do we want phase-2 coarse group unload before (or instead of) the full LRU?
- Public knobs + names: `warm` / `evict` / `set_vram_budget`; per-resource vs
  global.
- Confirm the sokol mid-frame `sg_init_*` timing assumption before wiring the
  lazy-on-draw hook.

## Related (different axis, not this plan)

- **Retained 2D geometry** (text2d/sprite3d cached vertices) is a per-frame
  CPU/upload concern, *not* VRAM capacity, and was deliberately deferred — see the
  text2d scope note. Residency is the higher-value, separate item.
