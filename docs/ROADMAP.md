# libwgrender Roadmap

libwgrender is converging into libwgt, which carries every open item from this file,
TASKS.md and the plans (libwgt 17f3f18, cb5dee5); they are kept here while wgrender is
maintained (CONVENTIONS.md, "Docs").

## Now / next

Nothing queued: new work goes to libwgt. What follows is still open here; the items done
from this file are in HISTORY.md, "Roadmap items done".

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
only the subsystems it uses, and `tools/measure_example_sizes.py` keeps that honest. **Scripting and
bindings mixed into the core** -- librl carried script hosts, hot-reload plumbing and
four bindings; here the core stays a plain C library, and the bindings live beside it
under `bindings/`, built only on the handle-only API (see the README's Bindings
section; they were separate repos until 2026-10-02).

Left out on purpose, not gaps: the scratch buffer and `_to_scratch` functions, public
`fs_*` ([HISTORY.md: wgr_fs + web-capable ensure (Phase 2)](HISTORY.md#wgr_fs--web-capable-ensure-phase-2)), `music_*`, `*_create_from_file`,
`window_open` / `input_poll_events` / `init_values_async` (sokol owns the loop; see
`wgr_run`), and `model_set_asset` / `load_asset` (Mesh resource + `wgr_model_set_mesh`).

## Deferred (real value; build when forced or as lower priority)

- **Desktop asset downloads: a built-in HTTP client** — the *hook* is done
  (2026-09-20): a URL asset host plus `wgr_asset_set_fetching` turns a desktop cache
  miss into a download request, the program polls for them and downloads, and `examples/fetch.c`
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

- **Image comparison** (what remained of the test suite item): rendering fixed scenes
  to render targets and comparing with a per-pixel tolerance; worth it the first time a
  rendering regression gets past smoke.

- **Hot reload (reload-on-change)** — watch source assets and re-`ensure`; we
  already have `wgr_fs` + `ensure`, so this is mostly a watcher. Big dev-loop win.
- **Audio streaming from the network** — decoding streams already: an Audio over 1 MB
  keeps its encoded bytes and each Sound decodes it while it plays. What's left is the
  file itself: today it must be wholly local before `wgr_audio_create`, so a long track
  can't start until it has arrived. A streamed Audio would be ready once enough has
  arrived to start (a task's PENDING -> READY), which needs the asset layer to deliver a
  file in pieces.

## Future

- **Networking outside libwgrender** (decided 2026-09-20): WebSockets and general
  networking (HTTP APIs, multiplayer) as a separate library and repo built on libwgrender's
  public API, like scripting. Why: they're game-specific; secure
  WebSockets on desktop need a bundled TLS library (mbedTLS or similar) with its own
  security updates; message payloads (binary data) don't fit libwgrender's handle-only API
  rules; and they need nothing from libwgrender's internals (poll from the frame callback,
  deliver on the main thread). librl's WebSocket code (`deps/wgutils/src/websocket`:
  the browser's WebSocket on web, a socket thread on desktop, no TLS) is the starting
  point. It can also supply libwgrender's missing-file hook (above).
