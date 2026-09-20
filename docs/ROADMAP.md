# libwgrender Roadmap

Ordering reflects a **recommendation**, not a commitment — reprioritize freely.
Rationale: a few of these share a foundation (a batched 2D draw path + a
material/shader layer), so they're sequenced so enablers land before the things
that lean on them. Items with a design doc link there.

## Now / next (recommended order)

1. **Materials & shaders** (phase 1 done 2026-09-16: built-in materials; phase 2
   2026-09-20: custom shaders; see
   [PLAN-materials.md](PLAN-materials.md)) — a **handle-only uniform/material API**
   (`wgr_material_set_float/vec4/texture(...)`, no struct/pointer across the public
   boundary) on top of the sokol-shdc pipeline we already have. *Enabler:*
   user shaders, particle looks, UI styling. The work is API shape, not plumbing
   (shdc solved the per-backend shader half).
2. **2D / UI layer** — screen-space coordinate system, `sprite2d` (the reserved-
   but-unimplemented object), 2D draw ordering, **2D picking** (mouse → rect/AABB
   hit-test; far cheaper than the existing 3D ray path in `wgr_pick`), and pickable
   UI primitives. `text2d` already exists and slots in here. Broadly useful —
   every game needs HUD/UI. sprite2d design: [PLAN-sprite2d.md](PLAN-sprite2d.md).

   **GUI direction (decided 2026-09-16): don't build a GUI toolkit.** Two jobs, two
   tools, both outside the core as optional modules (like scripting and bindings):
   - *Developer/debug UI* (inspectors, sliders, stats): **Dear ImGui** via sokol's
     `sokol_imgui.h` (and `sokol_gfx_imgui.h`). Its API isn't handle-only, so users
     call ImGui directly; libwgrender provides a small C extension hook (input events in,
     drawing inside the render pass). Optional, so wasm builds that don't use it
     don't pay its size.
   - *In-game UI/HUD* (styled, animated menus and bars): built from sprite2d +
     text2d + shapes, with layout from a small renderer-agnostic library such as
     **Clay** (C99, flexbox-like, emits rectangles/text/images to draw). Later.
     Refined in [PLAN-ui.md](PLAN-ui.md) (accepted): the glue uses only libwgrender's
     public API, so libwgrender gains the immediate drawing, text and input pieces a layout
     library needs, and no Clay code or types enter libwgrender.
   - *Widgets* (decided 2026-09-21): still not in the core. Buttons, sliders and lists
     are mostly policy — theming, focus order, keyboard navigation, text editing — and
     choosing that policy for everyone is the toolkit this decision rules out. The
     hand-built widgets in `examples/ui.c` move into a shared `examples/ui_widgets.h`
     instead: no API commitment, and it keeps proving the public API is enough. A real
     widget layer, if a game wants one, goes outside the core like the Clay glue.
     Whatever such a layer can't express through the public API (focus, text-field
     editing, clipboard) is a core gap and gets fixed in the core.
3. **Particle emitters** — emitter object + **batched/instanced** quad rendering
   (rides the 2D batch path + materials from 1–2). High visual payoff; doing it
   right is what finally justifies a real batched renderer over sokol_gl immediate.

## librl parity

Capabilities librl (`rl_*`) has that libwgrender doesn't yet, in suggested order. The
goal is **functional parity, not a 1:1 API**: each item starts with a short design
review (what librl did, what we learned from it, what libwgrender should do), and the
result may be fewer, different or merged functions. Everything must fit the
handle-only public API (see AGENTS.md). libwgrender is the primary library (see
"Direction" in the README), so port what future work needs first; items above may
jump ahead of these.

Tracking: `tools/parity.map` (`make parity`) accounts for every librl function;
[TASKS.md](TASKS.md) is the checklist.

Lessons from librl already applied: the scratch buffer (value returns instead),
synchronous asset fetch (needed JSPI on the web), `*_create_from_file` shortcuts
(blurred resource vs object), separate Music and Sound (one Sound with a loop
flag), a poll-driven loop (sokol callbacks), and a public filesystem API (internal
`wgr_fs`).

Lessons from librl not yet addressed (design for these; don't repeat them):

- **Networking and async vs sync got convoluted.** librl grew sync and async
  variants side by side, with fetching mixed into asset loading. For libwgrender: one
  async model (callbacks through the managed task queue, no sync twins). Decided
  (2026-09-20): libwgrender's networking is **assets** only: downloads inside `ensure`,
  asset redirects, host ping. WebSockets and general networking live **outside
  libwgrender** (see Future).
- **No way to leave out subsystems to shrink the wasm build.** libwgrender has the same
  problem today: `src/wgr.c` calls every subsystem's init/tick/deinit directly, and
  the web build compiles every `src/*.c` into each bundle, so audio, models
  (cgltf), fonts (fontstash) and friends are always linked. Needs a build-time way
  to exclude modules (e.g. `WGR_WITH_AUDIO=0`) plus lifecycle registration instead
  of hard-coded calls, and a size report per example to keep it honest.
- **Scripting and language bindings got mixed into the core.** librl carried
  script hosts, hot-reload plumbing (`rt_boot` / `rt_tick` hosts, reload counters)
  and four bindings in its own repo. For libwgrender: the core stays a plain C library;
  scripting is a separate module/repo on top of the public API, and each language
  bridge (Haxe, Nim, Lua, JS, and maybe Beef) is its own module/repo. The
  handle-only API is what makes that cheap; core changes shouldn't need binding
  changes in the same repo.

1. ~~**`sprite2d` + screen-space texture draw**~~ — done. See
   [PLAN-sprite2d.md](PLAN-sprite2d.md).
2. **`text3d`** — world-space text object (font/size/content/transform/color,
   facing, visible, pickable, bounds) plus a one-shot draw. Mirrors `text2d` +
   `sprite3d` facing.
3. **Remaining 3D shapes** — `rectangle_3d`, `circle_3d`, `line_strip_3d` (immediate
   and retained). `line_strip_3d` takes a point array in librl, so it needs a
   handle-only shape (e.g. a builder: `add_point`).
4. **Per-object picking** — `pick_model` / `pick_sprite3d` / `pick_shape` /
   `pick_text3d` alongside `wgr_scene_pick`; `set_pickable` / `is_pickable` on
   model, sprite3d, sprite2d, text2d, text3d (only shape has it today); pick stats
   (broadphase/narrowphase counters) under `wgr_debug`.
5. ~~**Lighting controls**~~ — done, redesigned as light objects (directional,
   point, spot) with per-scene lights and ambient. See
   [PLAN-lighting.md](PLAN-lighting.md).
6. **Window / monitor** — `set_size`, `set_position`, monitor count / current /
   set / width / height / position. Check what sokol_app exposes per platform;
   some may be desktop-only no-ops on web.
7. **Small leftovers** — `wgr_sound_set_pan`, `wgr_model_get_animation_frame_count`,
   FPS / `text_draw_fps` with a custom font, `texture_draw_ground`,
   `wgr_asset_ensure_many` (batch ensure). Maybe `*_is_valid` handle checks.

Not a code gap, but part of parity: **gamepad and touch input** (librl only
exposed these through scratch), **language bindings** (librl has Haxe, JS, Lua
and Nim; each becomes its own module/repo outside libwgrender, and Beef is a candidate),
**scripting** (also its own module/repo), and a **test suite** (librl has unit, smoke, regression,
headless and bindings tests; pairs with the headless renderer below).

**Left out on purpose** (not gaps): the scratch buffer and `_to_scratch`
functions (value returns instead), public `fs_*` (internal; see
[PLAN-wgr_fs.md](PLAN-wgr_fs.md)), `music_*` (a looping Sound), `*_create_from_file`
(objects come from handles), `window_open` / `input_poll_events` /
`init_values_async` (sokol owns the loop; see `wgr_run`), and
`model_set_asset` / `load_asset` (Mesh resource + `wgr_model_set_mesh`).

## Supporting / cross-cutting (slot in when an item above needs it)

- ~~**Loading pipeline: decode in the background, upload within a frame budget**~~ —
  done (2026-09-17): files ensured through `wgr_asset` are prepared on worker
  threads (web too, with cross-origin isolation) and finished within a per-frame
  upload budget before their callback; asset groups and progress replace librl's
  `ensure_many`. See [PLAN-pipeline.md](PLAN-pipeline.md). Next: compressed
  textures (KTX2 / Basis), since one large texture is still one upload.

- ~~**Offscreen / render-to-texture**~~ — done (2026-09-16): render targets are
  textures (`wgr_texture_create_target`, `wgr_render_begin_texture`); see
  [PLAN-render-target.md](PLAN-render-target.md). Still to come: persistent
  (uncleared) targets, HDR formats and full-screen passes for post-effects.
- **Mouse / pointer input + 2D hit-testing** — prerequisite for pickable UI;
  lands together with the 2D layer (confirm how much pointer input is exposed
  today).

## Deferred (real value; build when forced or as lower priority)

- **Desktop asset downloads** — on web a local cache miss downloads over HTTP
  (sokol_fetch, which reads only local files on native platforms and is compiled only
  into web builds); on **desktop** a local miss just fails (TODO in
  `wgri_asset_tick`). Plan: the OS's HTTP clients (WinHTTP on Windows, NSURLSession on
  macOS, libcurl on Linux, where it comes with the system), so HTTPS needs no bundled
  TLS library, plus a hook to fetch a missing file some other way. Deferred until
  desktop downloads are wanted. (Asset redirects and host ping are done, in the core;
  their download rules and URL pings start working on desktop with this.)
- **GPU resource residency** — decouple upload from create + optional LRU/budget.
  See [PLAN-resource-residency.md](PLAN-resource-residency.md). Phase 1 (decouple
  upload, `warm`/`evict`) is cheap and useful; the LRU/VRAM-budget machinery is
  deferred until a scene genuinely won't fit in VRAM.
- **Retained 2D geometry** (text2d / sprite3d cached vertices) — a per-frame
  CPU/upload micro-cost, *not* a capacity ceiling; only matters at very large
  text/sprite counts. `text2d` ships as retained-*state* today.

## Infrastructure / testing

- ~~**Null / headless renderer**~~ — done: `make HEADLESS=1` (sokol dummy GPU
  backend, a headless run loop behind the internal `wgr_platform` layer, no audio
  device) and `make smoke`. Unit tests link the headless library, so they need no
  GL/X11/ALSA. CI runs `make test`, `make smoke` and the WebGL2 webcheck. Next, when needed:
  benchmarks / asset-validation tools on the headless build.
- **Test suite (features + librl parity)** — build it in layers, cheapest first:
  1. **API parity report** (`make parity`): diff librl's public `rl_*` symbols
     against `wgr_*` using a checked-in map file that marks each librl function as
     *ported* (with its new name), *dropped on purpose* (with the reason), or
     *todo*. Unknown symbols fail, so new librl API can't be missed. Starts as a
     report, then gates once parity is reached. No GPU needed.
  2. **Unit tests** (plain C, no window): handle pool, `wgr_fs` / asset
     bookkeeping, scene layers and ordering, ray-vs-cube/sphere/sprite math
     (including alpha test), animation sampling, text2d state. Follow librl's
     `tests/unit` layout so the two suites look alike.
  3. **Shared behavior tests** (the real parity check): each scenario (build a
     scene, pick at pixel X, measure text, count animation frames, sample a joint)
     is written once against a small adapter header with one version for librl
     and one for libwgrender, then run on both and compared with tolerances. Anything
     that depends on rasterization (e.g. text metrics from raylib vs fontstash)
     gets a loose tolerance or is marked as expected to differ.
  4. **Smoke tests**: both halves exist. Desktop: `make smoke` (headless build,
     every example for 180 frames, exit 0 and no error logs). Web: `make webcheck`
     (`tools/webcheck.mjs`, every example in a Chromium-based browser over the
     DevTools protocol, no npm dependencies). Move to Playwright if Firefox/WebKit
     (Safari) coverage becomes important.
  5. **Image comparison** (later, optional): render fixed scenes to offscreen
     targets and compare with a per-pixel tolerance. Needs render-to-texture;
     prone to flakiness across GPUs, so keep it out of the default `make test`.

## Dev ergonomics / nice-to-have

- **Hot reload (reload-on-change)** — watch source assets and re-`ensure`; we
  already have `wgr_fs` + `ensure`, so this is mostly a watcher. Big dev-loop win.
- **Audio streaming** — ARCHITECTURE.md treats streamed-vs-decoded as an Audio
  property; verify large music streams rather than fully decoding into RAM.

## Future

- **Networking outside libwgrender** (decided 2026-09-20): WebSockets and general
  networking (HTTP APIs, multiplayer) as a separate library and repo built on libwgrender's
  public API, like scripting and bindings. Why: they're game-specific; secure
  WebSockets on desktop need a bundled TLS library (mbedTLS or similar) with its own
  security updates; message payloads (binary data) don't fit libwgrender's handle-only API
  rules; and they need nothing from libwgrender's internals (poll from the frame callback,
  deliver on the main thread). librl's WebSocket code (`deps/wgutils/src/websocket`:
  the browser's WebSocket on web, a socket thread on desktop, no TLS) is the starting
  point. It can also supply libwgrender's missing-file hook (above).
