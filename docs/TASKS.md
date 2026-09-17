# libsk Tasks

Working checklist. Order and reasoning live in [ROADMAP.md](ROADMAP.md); this file
is what's done and what's next. Per-function librl parity is tracked in
`tools/parity.map` (run `make parity`), not duplicated here.

Workflow: pick the top unchecked item, outline a plan (AGENTS.md), implement with
tests, keep `make`, `make examples`, `make check` and `make parity` passing, and
tick the box in the same commit.

## Infrastructure

- [x] `make parity`: librl → libsk API parity report (`tools/parity.sh`, `tools/parity.map`)
- [x] Unit test setup: `make test`, `tests/unit/` (no stubs, links the headless library);
      first tests cover the handle pool, matrix math, picking math and the
      transparent sort. They found two pick-normal bugs (fixed)
- [ ] Unit tests: `sk_fs` / asset bookkeeping, scene layers and ordering, sprite
      alpha-test picking, animation sampling, text2d state, render command-list
      merging
- [x] Sanitizer test builds: `make test SANITIZE=thread|address|undefined` (TSan in CI)
- [x] Faster checks (2026-09-16): `make verify` (~5 s incremental); smoke runs examples
      in parallel (46 s → 4 s); web library compiles once per backend, examples link
      against it (92 s → 2 s cold); webcheck checks 4 examples at a time in isolated
      browser contexts and waits for loading to finish instead of a fixed 5 s
      (78 s → 12 s WebGL2, ~100 s → 29 s WebGPU); CI caches emsdk and skips
      docs-only changes. Negative-tested: crashes, hangs, panics, error logs,
      missing assets, stale backend builds and unfinished loads all still fail.
      webcheck now also fails on libsk [ERROR]/[FATAL] logs (it missed them before)
- [ ] Later: wasm-side unit tests when web-only code needs them
- [x] CI (GitHub Actions, `.github/workflows/ci.yml`): desktop build, `make check`,
      `make test`, `make smoke`; web build + `make webcheck` (WebGL2, headless Chrome)
      with screenshots as an artifact
- [x] Null / headless renderer: `make HEADLESS=1` builds `build/headless/libsk.a`
      (sokol dummy GPU backend, no window or audio device, no GL/X11/ALSA link
      deps) behind an internal `sk_platform` layer; frames run paced at 60/s
      until `sk_request_quit` or `SK_HEADLESS_FRAMES`. Unit tests link it
- [x] Web smoke: `make webcheck` loads every example in a browser (WebGL2 headless,
      WebGPU headed), fails on console errors/exceptions/panics/wrong backend,
      saves screenshots
- [x] Desktop headless smoke: `make smoke` runs every example headless for 180
      frames and fails on a non-zero exit, a timeout, or error-level logs
      (tools/smoke.sh). Needs no display, so it works with monitors asleep
- [ ] Shared behavior tests: scenarios run against librl and libsk via an adapter
      header, compared with tolerances
- [ ] Gate on parity: add `make parity` to `make check` once librl is no longer needed
      locally, or run `--strict` in CI once todos reach zero

## Found by porting librl's c-simple (`examples/simple.c`)

- [x] Bug: models ignore glTF `alphaMode` (BLEND/MASK). gumshoe's `blobShadow`
      (BLEND, alpha 0.2) drew as a solid black quad. Fixed: MASK discards below
      the cutoff, BLEND and faded models (tint alpha < 1) draw in a sorted blended
      pass, `doubleSided` disables culling
- [x] Scene layers + render passes (librl had these; libsk kept only the API).
      Done: per layer an opaque pass, then one transparent pass sorted back to
      front across model primitives, sprites and translucent shapes (runs stay
      batched, unlike librl's per-item flush). Draw order follows call order
      across sokol_gl and model draws (frame command list in `sk_render`)
- [x] 2D scene drawables (sprite2d, text2d in a scene) draw after all 3D layers
- [x] Bug: static (unskinned) glTF primitives ignored their node transform
      (gumshoe's `blobShadow` node is scaled 0.66 and offset). Fixed: node world
      transforms are baked into positions, normals, pick data and bounds at load
- [x] Bug: `sk_set_target_fps` did nothing. Fixed: frames are vsync-locked by
      default; the target caps below that (desktop sleeps, web skips early browser
      frames); `SK_WINDOW_FLAG_VSYNC_OFF` (was `_VSYNC_HINT`) unlocks on desktop.
      Measured: desktop vsync off at 144/20 fps, web at 30 and capped at 60
- [ ] Bug (platform): vsync doesn't hold on NVIDIA (RTX 4080 laptop, driver 580) +
      COSMIC/XWayland with sokol's GL backend. Swaps block only every other frame:
      ~120 frames/s on a 59.88 Hz display, intervals alternating ~16.7 ms and <4 ms,
      half the frames never shown. Not a libsk/sokol timing bug: a raw GLX program
      (no sokol) reproduces it with GLX_SWAP_INTERVAL=1 confirmed, the interval set
      before or after mapping, with or without glFinish, and with
      `__GL_SYNC_TO_VBLANK=1` or `__GL_MaxFramesAllowed=1`. Target caps
      are unaffected. Options: sokol's Vulkan backend on Linux (FIFO present), pacing
      to the display refresh ourselves (refresh via XRandR), native Wayland (sokol
      has none). Checking vsync needs a monitor that is on
- [x] Frame `dt` came from sokol's smoothed frame duration, which drifted under the
      irregular swaps above (summed dt was 0.88 of wall time). Now measured from our
      own clock: summed dt matches wall time with vsync on/off, capped or not
- [x] Verified picking against rendering under transforms (pick-grid vs screenshot:
      a 4 px grid over 960x540): rotated + non-uniformly scaled box and ellipsoid,
      a scaled sprite with alpha test, and a rotated/scaled static model all match
      except anti-aliased edges
- [x] Bug: animated models were picked against their rest (T) pose. Fixed: pick
      geometry is skinned on the CPU with the current joint matrices when a pick
      needs it (cached per pose), and the broadphase uses the posed bounds
- [x] Bug: invisible parts of transparent materials were pickable (gumshoe's blob
      shadow). Fixed: MASK/BLEND hits need material alpha (texture alpha at the
      hit UV x base color alpha, tint ignored) at or above the MASK cutoff, or 0.5
      for BLEND. The shadow (at most 20% opaque) is no longer pickable at all.
      Pick-grid check after the fix: no picked-but-not-drawn points on any object
- [ ] Light selection uses rest-pose bounds for animated models, so a limb far
      outside the rest pose can miss a nearby point light's range check. Minor;
      could reuse the posed bounds when they're already cached
- [ ] Colors are immutable (no public `sk_color_set`), so animating a tint means
      pre-creating a palette (see `examples/sprite2d.c`). Decide whether color
      handles should be settable
- [ ] Window flags accepted but ignored: `RESIZABLE`, `UNDECORATED`, `TRANSPARENT`,
      `HIDDEN`, `ALWAYS_RUN` (only fullscreen, high-DPI, MSAA and vsync-off work)
- [x] Bug: orthographic cameras only affected sokol_gl content; models and
      picking always used perspective (fovy 6 world units became a 6 degree FOV,
      so models drew hugely magnified and picks missed). Fixed: one
      `sk_camera3d_projection` / `sk_camera3d_view` used by sokol_gl 3D mode,
      models and picking
- [x] Fixed-rate tick (`sk_set_tick`) + timing passed to callbacks (`dt`,
      `tick_fraction`); `sk_get_delta_time` removed; input edges relative to the
      running callback. Resolves the frame-timing decision below
      (docs/PLAN-tick.md, examples/tick.c)
- [x] Decided (by the tick design): time accumulated from frame `dt` runs slow
      when frames stall, because `dt` is capped at 0.1 s. Simulation belongs in a
      tick, which uses real elapsed time and catches up (up to 5 ticks per frame)
- [x] Audio regressions from librl fixed: long audio streams (music create 215 ms →
      5 ms, ~108 MB → 6 MB) and mixing runs on the audio device's thread
      ([PLAN-audio.md](PLAN-audio.md))
- [x] Loading pipeline (2026-09-17, [PLAN-pipeline.md](PLAN-pipeline.md),
      `examples/loading.c`, `make loadbench`): textures, meshes, environments and
      audio decode on worker threads and upload within a per-frame budget before
      the asset callback; asset groups and progress; threaded web build
      (`WEB_THREADS=0` without). Loading Sponza + FlightHelmet: worst frame 1.24 s
      → 17 ms (desktop, headless) and 1.98 s → 70 ms (WebGL2)
- [x] Bug: sokol's default pools (128 buffers, images) made Sponza fail to load;
      pools raised, and a failed GPU buffer or image fails the load
- [ ] Loading follow-ups: compressed textures (KTX2 / Basis) so a large texture
      isn't one long upload; shader warm-up (the first frame drawing loaded PBR
      models stalls ~220 ms on WebGL2 while programs compile); the zero-worker mode
      prepares a whole glTF in one frame (~1 s for FlightHelmet); `.glb` dependency
      listing reads the whole file on the main thread; Windows threads are written
      but untested
- [x] Bug: `sk_request_quit` on web aborted in sokol_audio when the main thread
      had been busy: audioprocess events queued meanwhile ran after shutdown and
      asserted on the freed buffer. Fixed in libsk's sokol fork
      (github.com/robknopf/sokol: the handler is cleared on shutdown); sokol is now
      vendored from the fork with `tools/update_sokol.sh`. `examples/quit.c` (music,
      loads in flight, a busy frame, quit) keeps webcheck on this path
- [x] Web quit no longer blocks the page waiting for asset workers ("Blocking on the
      main thread"): they're detached and end on their own
- [x] Bug: the library builds had no header dependency tracking (desktop and
      headless not at all, web not for the vendored `-isystem` headers), so header
      changes left stale objects. Both now use `-MD -MP`
- [x] Built-in font (2026-09-17): the 8x8 sokol_debugtext bitmap font (KC85/3, whose
      `[ ] \ { | } ~` were graphics and umlauts) is replaced by JetBrains Mono, an
      ASCII subset embedded in the library (`src/fonts/sk_default_font.h`, generated
      by `tools/gen_default_font.py`, OFL). All text is TrueType now; sokol_debugtext
      is gone (web size about even: -12.5 KB code, +9 KB font).
      `sk_text_set_default_font` sets another default (e.g. for UTF-8), used by
      `sk_text_draw` and font handle 0 everywhere, including text3d
- [x] webcheck: WebGPU runs failed the first four examples (started after ~20 s or
      never) when the monitors were asleep: WebGPU ran in a visible browser window,
      and the pages waited for the compositor to wake the displays (cosmic-comp logs
      a modeset as they continue). An earlier fix wrongly blamed HDMI audio (the fake
      audio device stays: webcheck shouldn't play sound). Fixed: WebGPU runs on a
      private Xvfb display (ANGLE on Vulkan), rendering correctly, no window, no
      monitors involved; headless WebGPU loses its device immediately
- [x] Bug: quitting a desktop program with audio could abort in
      `saudio_sample_rate` (asserts once `saudio_shutdown` has begun, while the device
      thread still asks for a buffer). The callback uses the rate cached at init
- [x] Lighting: light objects (directional, point, spot), per-scene lights and
      ambient, up to 8 lights per model by contribution, nothing lit implicitly
      (docs/PLAN-lighting.md, examples/lights.c). `simple.c` lighting PARITY note
      removed
- [x] Remove the remaining `PARITY:` note in `examples/simple.c` when FPS drawing
      in a custom font lands

## librl parity (functional, not 1:1; see `make parity` for function-level status)

Each item starts with a short design review: what librl did, what went wrong or
felt awkward, and the libsk design. Update `tools/parity.map` with the outcome.

- [x] Parity batch (2026-09-16, [PLAN-parity.md](PLAN-parity.md), `examples/text3d.c`):
      `sk_pick_object` + pick stats + pickable flags everywhere; `sk_text3d_*` and
      `sk_text_draw_3d`; 3D rectangles, circles, lines and point-by-point line
      strips; animation duration/time in seconds and `sk_model_is_ready`;
      `sk_sound_set_pan`; sprite3d getters and FREE facing; `sk_text_draw_fps_ex`;
      `sk_asset_get_host`. Dropped: built-in font handle, placeholder model, ground
      texture drawing. `make parity`: 95%, 10 todos, all deferred on purpose
- [x] 2D sprites and screen-space texture drawing: `sk_sprite2d_*` (source rect,
      pivot, rotation, x/y scale with flip, size, alpha-tested picking) and
      `sk_texture_draw`; scenes draw 2D after 3D and pick it first; 2D, mouse and
      screen size are in logical pixels (docs/PLAN-sprite2d.md, examples/sprite2d.c)
- [x] Lighting controls: redesigned as light objects in scenes (see above)
- [x] Window and monitor control (2026-09-17, [PLAN-window.md](PLAN-window.md),
      `examples/window.c`): size, position, fullscreen, focus, monitors, through
      `deps/sokol_utils` (squk/sokol_utils, vendored with fixes)
- [ ] Window flags: honor `RESIZABLE`, `UNDECORATED`, `HIDDEN`; remove `ALWAYS_RUN`;
      `TRANSPARENT` web only (PLAN-window.md, phase 2)
- [x] Assets: ensure many files at once: asset groups (`sk_asset_group_create`,
      `sk_asset_group_add`) with `sk_asset_get_progress`
- [ ] Assets: host ping (with the `sk_net` rework)

## Parity outside the API

- [ ] Gamepad input (`sk_input_*`)
- [ ] Touch input (`sk_input_*`): the primary touch drives the pointer (buttons,
      scene interaction) since 2026-09-17; a multi-touch API (all touch points,
      gestures) is still open
- [ ] Native iOS / Android: long stretch goal. sokol supports both (Metal/GLES3,
      CoreAudio/AAudio, touch); libsk would need build targets, Metal shaders, app
      lifecycle and bundle/APK file access. Until then, mobile runs the wasm build
      (mobile browser, a wasm host app, or a shell like Electron/Tauri; hosts without
      cross-origin isolation need WEB_THREADS=0)
- [ ] First language binding, as its own module/repo (decide which; Beef is a candidate)
- [ ] Scripting, as its own module/repo on top of the public API

## Roadmap features

- [x] Materials, phase 1: material resource, glTF metallic-roughness and unlit
      shading, normal/occlusion/emissive maps, sRGB-correct lighting, per-model
      slot overrides ([PLAN-materials.md](PLAN-materials.md), `examples/materials.c`)
- [ ] Materials, phase 2: custom shaders (`.skshader` packages from sokol-shdc)
- [ ] Materials, phase 3: shapes and sprites on materials (with the batched renderer)
- [x] 2D / UI layer: `enabled` and pointer interaction per scene member, touch as a
      pointer, retained 2D shapes, nine-slice sprites, text alignment and wrapping,
      per-layer clipping, and sprite3d source/extent/pivot for 2D worlds on an
      orthographic camera ([PLAN-2d.md](PLAN-2d.md), `examples/ui.c`, `examples/2d.c`)
- [ ] Particle emitters (batched/instanced)
- [x] Render to texture: `sk_texture_create_target`, `sk_render_begin/end_texture`,
      `sk_texture_set_sampling` ([PLAN-render-target.md](PLAN-render-target.md),
      `examples/render_target.c`)
- [ ] Render targets later: keep contents between frames (no clear), per-target
      formats (HDR/float) for post-processing, full-screen shader passes, reading
      pixels back / screenshots

## Materials: glTF coverage

Done (2026-09-16, verified against Khronos TextureTransformTest, MultiUVTest,
VertexColorTest, TextureSettingsTest and BoxTextured on desktop, WebGL2, WebGPU):

- [x] Second texture coordinate set (TEXCOORD_1), chosen per texture
- [x] glTF files with separate buffers and images (`.gltf` + `.bin`/`.png`), and
      `data:` URIs. Ensuring a `.gltf`/`.glb` also ensures the files it references
      (sk_asset dependency listers), so it works on web.
- [x] Texture transforms per texture (glTF `KHR_texture_transform`, or `<t>_offset`,
      `_rotation`, `_scale` by name); picking applies them
- [x] Vertex colors (COLOR_0) multiply base color (and alpha, for picking)
- [x] Sampler modes per texture: wrap (repeat, clamp, mirror) and filter, from glTF
      or `sk_material_set_texture_sampling`
- [x] Mipmaps for all textures (generated at load)
- [x] Missing or broken glTF images: the model still loads (warning); color
      textures use the placeholder texture (built-in magenta checker,
      `sk_texture_set_placeholder`), data textures stay empty. Missing buffers still
      fail. Ensured dependencies can be optional.
- [ ] Redirect where assets and their dependencies load from (CDN, mods, localized
      files), e.g. `sk_asset_set_redirect(fn)`; design with the `sk_net` rework

Not supported yet:

- [x] Environment lighting: `sk_environment_create` (.hdr/PNG/JPEG equirect),
      `sk_scene_set_environment/background/tonemap`, SH irradiance + GGX-prefiltered
      cubemap + BRDF table, background skybox, tone mapping (Neutral default, ACES)
      and exposure ([PLAN-environment.md](PLAN-environment.md), `examples/environment.c`)
- [ ] Environment follow-ups: prefiltering runs on a loading worker when loaded
      through `sk_asset` (330 ms per 1K HDR; sync creates still block), other inputs (6 cube faces, KTX2
      prefiltered), RGBM fallback for backends that can't filter half-float textures,
      HDR framebuffer (bloom, tone mapping sprites together with models)
- [ ] Generated tangents come from texture coordinate set 0; normal maps on set 1
      need tangents in the file
- [ ] Mipmaps average in stored (sRGB) space and don't renormalize normal maps
- [ ] Other glTF material extensions (clearcoat, transmission, sheen, specular, ior, ...)
- [ ] Morph targets (animation weights are skipped)
- [ ] Compressed / other image formats (KTX2/Basis, WebP)

## Lessons from librl to design for

- [ ] Networking: one async model and a single `sk_net` module under `sk_asset`
- [x] Web size (2026-09-17): web builds weren't link-optimized (no -O: no wasm-opt,
      unminified JS, assertions) and carried all three backends' shader sources.
      Now -O3 (WEB_DEBUG=1 for debug builds) and sokol-shdc --ifdef: simple went
      from 874 KB wasm + 425 KB JS to 693 + 190 KB (323 KB gzipped; librl's c-simple
      is 653 + 264 KB, 342 KB gzipped). The rest of the gap: every program links
      every subsystem (below)
- [ ] Web startup goal: one cold start (download + compile, like an install), then
      warm starts with no redownload or recompile. Measure cold vs warm time to first
      frame per example plus wasm/JS sizes (raw, gzip, brotli); check compiled-code
      caching (streaming instantiation, `application/wasm`, cache headers; the dev
      server sends no-store on purpose). Then size levers: Closure on the JS glue,
      emmalloc, `-sENVIRONMENT=web,worker`, `-Oz`, browser-native image/audio
      decoders on web, optional subsystems (below)
- [ ] Explore (later, own session): libsk's C API as the contract with other
      implementations, e.g. a JS backend (three.js/Babylon) for JS-target games, or
      another implementation language (Zig, Odin, D betterC, Beef; engines like
      Sedulous). Compare footprint and caching against the C + sokol build first
- [ ] Optional subsystems: exclude modules at build time to shrink wasm, with a
      per-example size report
- [x] Scripting and language bindings stay out of the core repo (decided; see ROADMAP)

## Open decisions

- [x] Handle-only API for point lists: line strips are built point by point on a
      retained shape (`sk_shape3d_set_line_strip` + `sk_shape3d_add_point`); batch
      asset ensure is designed with the loading pipeline
- [ ] Which language binding comes first
- [ ] Naming, if libsk becomes an API with swappable implementations (see the
      exploration task): `destroy` on resources only drops a reference (`release`
      would say so), `create(path)` on resources loads or finds a shared one (`load`?),
      and the `sk_` prefix names the sokol implementation rather than the API. Decide
      together, before bindings depend on the names
- [x] Fonts are resources like the rest (2026-09-17): refcounted and deduped by
      path; text objects and the default font hold references; a released font's
      fontstash data is kept by path and reused (fontstash can't remove fonts).
      Follow-up: a `.ttf/.otf` loader so reading big font files runs in the loading
      pipeline
