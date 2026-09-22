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

## What librl taught us

wgrender reached functional parity with librl on 2026-09-21: everything librl could do,
each feature designed fresh from what librl's version got wrong rather than copied.
The parity tooling is gone with the port; this is what it left behind.

Applied: the scratch buffer (value returns instead), synchronous asset fetch (needed
JSPI on the web; one async model through the managed task queue, no sync twins),
`*_create_from_file` shortcuts (blurred resource vs object; objects come from handles),
separate Music and Sound (one Sound with a loop flag), a poll-driven loop (sokol
callbacks), a public filesystem API (internal `wgr_fs`), and networking mixed into
asset loading (wgrender's networking is assets only -- downloads inside `ensure`,
redirects, host ping; anything else lives outside the library, see Future).

Structural: **subsystems that can't be left out** -- librl linked everything into every
build, and so did wgrender until `WGRI_MODULE` (ARCHITECTURE.md §7b): a program now links
only the subsystems it uses, and `make websize` keeps that honest. **Scripting and
bindings mixed into the core** -- librl carried script hosts, hot-reload plumbing and
four bindings; here the core stays a plain C library and each binding is its own repo
on the handle-only API (see the README's Bindings section).

Left out on purpose, not gaps: the scratch buffer and `_to_scratch` functions, public
`fs_*` ([PLAN-wgr_fs.md](PLAN-wgr_fs.md)), `music_*`, `*_create_from_file`,
`window_open` / `input_poll_events` / `init_values_async` (sokol owns the loop; see
`wgr_run`), and `model_set_asset` / `load_asset` (Mesh resource + `wgr_model_set_mesh`).

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

- **Desktop asset downloads: a built-in HTTP client** — the *hook* is done
  (2026-09-20): a URL asset host plus `wgr_asset_set_fetcher` turns a desktop cache
  miss into a download, the program supplies the downloader, and `examples/fetch.c`
  wires one up in twenty lines. libwgrender still ships no HTTP and no TLS, which is
  the point.
  What is deferred is a *built-in* fetcher so nothing has to be supplied: the OS's own
  clients (WinHTTP on Windows, NSURLSession on macOS, libcurl on Linux where it comes
  with the system), so HTTPS needs no bundled TLS library. Worth doing when shipping a
  game means "it just works with no glue"; until then the hook covers it, and wgnet's
  `fetch_url` is the obvious thing to plug in.
  Still stubbed on desktop: `wgr_asset_ping_host` (the hook has no ping) and URL
  redirect rules.
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
- **Test suite** — built, in layers: **unit tests** (`make test`, plain C against the
  headless library: handle pool, `wgr_fs` and asset bookkeeping, scene order and picking
  math, animation sampling, object state, shadows, culling, instancing; also under
  the sanitizers, `SANITIZE=thread|address|undefined`); **smoke** (`make smoke`, every
  example headless for 180 frames, and `make windows-smoke` the same under Wine);
  **web** (`make webcheck`, every example in a Chromium-based browser over the DevTools
  protocol, WebGL2 and WebGPU). The parity layers that were planned here -- an API
  report and scenarios run against both librl and wgrender -- were retired once parity
  was reached (see "What librl taught us"). Still optional: **image comparison**,
  rendering fixed scenes to render targets (which exist now) and comparing with a
  per-pixel tolerance; worth it the first time a rendering regression gets past smoke.
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
