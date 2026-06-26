# libsk Roadmap

Ordering reflects a **recommendation**, not a commitment — reprioritize freely.
Rationale: a few of these share a foundation (a batched 2D draw path + a
material/shader layer), so they're sequenced so enablers land before the things
that lean on them. Items with a design doc link there.

## Now / next (recommended order)

1. **Materials & shaders** — a **handle-only uniform/material API**
   (`sk_material_set_float/vec4/texture(...)`, no struct/pointer across the public
   boundary) on top of the sokol-shdc pipeline we already have. *Enabler:*
   user shaders, particle looks, UI styling. The work is API shape, not plumbing
   (shdc solved the per-backend shader half).
2. **2D / UI layer** — screen-space coordinate system, `sprite2d` (the reserved-
   but-unimplemented object), 2D draw ordering, **2D picking** (mouse → rect/AABB
   hit-test; far cheaper than the existing 3D ray path in `sk_pick`), and pickable
   UI primitives. `text2d` already exists and slots in here. Broadly useful —
   every game needs HUD/UI.
3. **Particle emitters** — emitter object + **batched/instanced** quad rendering
   (rides the 2D batch path + materials from 1–2). High visual payoff; doing it
   right is what finally justifies a real batched renderer over sokol_gl immediate.

## Supporting / cross-cutting (slot in when an item above needs it)

- **Offscreen / render-to-texture** — `sk_render` has no render targets yet.
  Foundational for post-effects, UI compositing, the text-bake idea, and
  particle-to-buffer. Build when the first consumer needs it.
- **Mouse / pointer input + 2D hit-testing** — prerequisite for pickable UI;
  lands together with the 2D layer (confirm how much pointer input is exposed
  today).

## Deferred (real value; build when forced or as lower priority)

- **Desktop network-fetch fallback** — on web a local cache miss fetches over
  HTTP (sokol_fetch/XHR); on **desktop** sokol_fetch is file-I/O only, so a local
  miss currently just fails (TODO in `sk_asset_tick`). Adding HTTP on desktop
  needs a real client (libcurl or similar). Deferred until desktop-fetch is
  actually wanted.
- **GPU resource residency** — decouple upload from create + optional LRU/budget.
  See [PLAN-resource-residency.md](PLAN-resource-residency.md). Phase 1 (decouple
  upload, `warm`/`evict`) is cheap and useful; the LRU/VRAM-budget machinery is
  deferred until a scene genuinely won't fit in VRAM.
- **Retained 2D geometry** (text2d / sprite3d cached vertices) — a per-frame
  CPU/upload micro-cost, *not* a capacity ceiling; only matters at very large
  text/sprite counts. `text2d` ships as retained-*state* today.

## Dev ergonomics / nice-to-have

- **Hot reload (reload-on-change)** — watch source assets and re-`ensure`; we
  already have `sk_fs` + `ensure`, so this is mostly a watcher. Big dev-loop win.
- **Audio streaming** — ARCHITECTURE.md treats streamed-vs-decoded as an Audio
  property; verify large music streams rather than fully decoding into RAM.

## Future

- **`sk_net`** (HTTP fetch, later websockets) — the web fetch inside `ensure` is
  really a network primitive; eventually `sk_asset` would call `sk_net` instead of
  sokol_fetch directly. Revisit when networking features land. (Also the natural
  home for the desktop-fetch fallback above.)
