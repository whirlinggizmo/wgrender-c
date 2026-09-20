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
- [x] Unit tests for the gaps (2026-09-21): `sk_fs` paths and files (root joining,
      absolute paths, reading, writing, the directories a write makes), animation
      sampling (the posed joint matrices: interpolation, wrap, clamp, speed),
      sprite alpha-test picking (and the CPU alpha mask behind it), text2d state
      (font, color, visible / pickable / enabled, an invalid handle), scene layer
      order (which member a pick finds), and the render command list (model runs
      merging, what stops them merging, per-pass isolation, sprite batches, callbacks).
      120 unit tests. Found on the way: a texture made from pixels keeps an alpha mask
      when it has any transparency, so alpha-test picking works on it too
- [ ] Unit tests still missing: the web half of `sk_fs` (MEMFS + IndexedDB) needs a
      browser, so it wants the wasm-side tests below; `sk_asset` loader/mapper
      registration is only exercised through real loaders
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
- [x] Colors are values (2026-09-17): `sk_color_t` is packed 0xRRGGBBAA, so a tint
      can be computed per frame (`sk_color_rgba`, `sk_color_with_alpha`,
      `sk_color_lerp`) instead of pre-creating a palette, and the 256-slot pool,
      the handle kind and the color lifecycle are gone ([PLAN-color.md](PLAN-color.md))
- [x] Window flags accepted but ignored (2026-09-19): honored now (below)
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
- [x] Compressed textures (2026-09-19, [PLAN-textures.md](PLAN-textures.md)): a program
      loads `name.ktx` and libsk picks `name.bc7.ktx` / `.astc.ktx` / `.etc2.ktx` (made
      by `tools/compress_textures.sh`) or `name.png` for the GPU; a 2K texture loads in
      ~1 ms instead of 60-90 ms (desktop) and 1.5 ms instead of 125-200 ms (phone), at a
      quarter of the GPU memory
- [x] Compressed textures in glTF models (2026-09-19): `compress_textures.sh --gltf`
      writes `model.ktx.gltf` with the `SK_texture_ktx` extension (portable: other viewers
      use the original images); only the variant this GPU can use downloads. FlightHelmet
      on the phone: 2.0 -> 0.45 s in the background, 1.4 -> 0.1 s synchronously
- [x] Web file cache per file (2026-09-20, [PLAN-sk_fs.md](PLAN-sk_fs.md)): the phone's
      ~100 ms frame while a model loaded was IDBFS restoring the whole cache (56.5 MB)
      at startup, not loading or shaders (first draws of loaded models cost nothing
      extra). Now only the cache's list of files is read at startup and a file is read
      when it's ensured: FlightHelmet's worst frame 60-85 -> 27-30 ms
- [ ] Loading follow-up: on the phone, frames reach 25-30 ms while a model's large
      textures upload. Tried (2026-09-20) and dropped: capping uploads by size per frame
      (4, 8, 16 MB): no better (at 4 MB, one 2K ASTC texture a frame, frames still reach
      ~30 ms) and slower to load; the cost is each large upload itself. Left: smaller
      files (ASTC 6x6 blocks, about half the bytes, some quality), or one mip level per
      frame (needs a change to libsk's sokol fork)
- [ ] Loading follow-ups: shader warm-up (the first frame drawing loaded PBR
      models stalled ~220 ms on WebGL2 while programs compiled; not seen on the Pixel 9
      with FlightHelmet in a lit scene: recheck with an environment); the zero-worker mode
      prepares a whole glTF in one frame (~1 s for FlightHelmet); `.glb` dependency
      listing reads the whole file on the main thread
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
- [x] Window flags (2026-09-19, PLAN-window.md phase 2): `RESIZABLE` (without it the
      window keeps its size, as in raylib; the examples set it), `UNDECORATED`,
      `HIDDEN` with `sk_window_set_visible` / `sk_window_is_visible`, `TRANSPARENT`
      (sokol's premultiplied compositing; the screen's clear color is premultiplied);
      `ALWAYS_RUN` removed. Applied through the vendored sokol_utils header after the
      window exists, so a hidden window can show for a moment first
- [x] Assets: ensure many files at once: asset groups (`sk_asset_group_create`,
      `sk_asset_group_add`) with `sk_asset_get_progress`
- [x] Assets: host ping (2026-09-20): `sk_asset_ping_host(host, timeout_ms, on_done,
      user)`, asynchronous (librl's blocked, and did nothing on the web): a timed HEAD
      request on the web (any response counts, no CORS needed); on desktop, whether the
      asset directory exists

## Parity outside the API

- [x] sprite3d / text3d camera facings (2026-09-18) now do what their names say, and
      differ from librl on purpose. `CAMERA` is spherical (parallel to the view plane,
      tilting with the camera's pitch); `CAMERA_FIXED_Y` is cylindrical (turns about
      world Y, stays upright). In librl, `CAMERA` built the quad from `camera.up` — the
      up *hint*, normally (0, 1, 0) — so both modes were upright there; in libsk before
      this fix, both tilted. `CAMERA_FIXED_Y` also no longer collapses to an invisible
      zero-width quad when the camera looks straight down
- [x] Gamepad input (2026-09-19): `sk_input_get_gamepad_button/axis`, up to 4 pads by
      slot, buttons by position with frame and tick edges, sticks with a dead zone,
      triggers as axes and buttons; an optional module (`src/sk_gamepad.c`). Web: the
      Gamepad API; Linux: evdev (the `xpad` driver's X/Y codes swapped), rescanned for
      hot-plugging; Windows: XInput (compiles; untested with a pad). Checked on a
      wired Xbox 360 pad, native and in Chrome;
      `examples/gamepad.c`
- [x] Windows builds (2026-09-19): `make windows` cross-compiles the library and
      examples with MinGW (OpenGL), `make windows-test` / `windows-smoke` run the unit
      tests and headless examples under Wine or Steam's Proton (`tools/wine.sh`);
      `make verify` builds it when MinGW is installed. Two compile fixes (fontstash
      needs windows.h; `_mkdir`). All 100 tests and 26 examples pass under Proton 11,
      including the Windows threads (asset workers); `hello` and `model` also draw
      correctly windowed under Wine
- [ ] Windows on real Windows: windows (the window flags), WASAPI audio, XInput
      gamepads; Direct3D 11 (sokol-shdc HLSL output) instead of OpenGL
- [ ] Gamepads later: macOS (GameController framework), rumble, connect/disconnect
      events, a mapping database for pads the kernel doesn't name by position
- [x] Touch input (`sk_input_*`, 2026-09-18): the first finger drives the pointer
      (since 2026-09-17), and a second one cancels its press (released off-screen,
      no click); every finger with ids, edges and deltas (`sk_input_get_touch`), and
      the two-finger pan / pinch / twist (`sk_input_get_touch_gesture`), per frame
      and per tick. `examples/touch.c`; checked with CDP touch events and on a Pixel
      9 Pro XL (`tools/serve.py --tls` for a secure page on the LAN)
- [x] High-DPI by default (2026-09-18): windows render at the display's full
      resolution; `SK_WINDOW_FLAG_LOW_DPI` opts out (fewer pixels to fill).
      Replaces `SK_WINDOW_FLAG_WINDOW_HIGHDPI`
- [x] Destroying an object takes it out of every scene (2026-09-18): members, hover
      and press state, and a scene's camera. It used to stay as a stale handle that
      warned on every draw
- [ ] Touch later: long-press and swipe/fling recognizers if a game wants them;
      pinch from desktop trackpads (browsers send it as ctrl + wheel)
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
- [x] Materials, phase 2 (2026-09-20): custom shaders. A fragment shader (and an
      optional vertex hook) written against `shaders/sk.glsl`, packed for GL, WebGL2 and
      WebGPU by `tools/shaderpack.py` into a `.skshader` file; `sk_shader_create`,
      `sk_material_create_custom`, parameters and textures by the shader's names.
      Static and skinned models, scene lights, tint, alpha modes, tone mapping
      ([PLAN-materials.md](PLAN-materials.md), `examples/shaders.c`)
- [x] Environment lighting in custom shaders (2026-09-20): `sk_environment_diffuse`,
      `_specular`, `_brdf`, `_intensity` in `shaders/sk.glsl` (`.skshader` format 2;
      older files are refused, rebuild them); the shaders example's water reflects a sunset
- [ ] Custom shaders later: arrays and matrices as parameters; D3D11/Metal sources when
      those backends come
- [x] Materials, phase 3a (2026-09-20): custom shaders on sprites (2D and 3D):
      `sk_sprite3d/2d_set_material`, one `.skshader` for models and sprites
      (`sk_sprite_color()`, `sk_sprite_tex`), batched by material; format 3. Shapes stay
      unlit (generated meshes for lit geometry) ([PLAN-materials.md](PLAN-materials.md))
- [x] Skinning and per-draw uniforms (2026-09-20): a crowd of animated models spent
      85% of its frame CPU in one call, uploading 128 joint matrices (8.4 KB) as
      uniforms per skinned draw, and would have overflowed the backends' per-frame
      uniform buffer past ~450 skinned draws. Now the frame's joint matrices (only the
      joints a mesh has) go into one float texture, uploaded once before the passes,
      and the skinned shaders read them with texelFetch; the fragment block is split
      into material (every draw), scene and lights (applied only when they change).
      Per draw: ~8.6 KB -> ~430 bytes. Measured in Chromium on the GPU (100 animated
      gumshoes: 15.2 -> 2.1 ms of frame CPU; 400: 19.4 -> 7.8 ms), with three.js 0.186
      at 2.0 and 6.1 ms. `.skshader` format 4 (rebuild custom shaders)
- [ ] Lightmaps: baked lighting as a material texture (its own texture coordinate set,
      which materials already support), multiplied into the surface. No new passes;
      bake them in Blender
- [x] Shadows, phase 1 (2026-09-21, desktop GL, WebGL2 and WebGPU): a directional light casts
      into a depth map before the frame's passes, and models, lit sprites and custom
      shaders (`sk_shadow`) are darkened by it. `sk_light_set_casts_shadows` /
      `_shadow_distance` / `_shadow_map_size` / `_shadow_bias` (in texels) /
      `_shadow_strength` / `_shadow_color`, and `sk_model_set_casts_shadow` /
      `_set_receives_shadow`. Opt in twice: the module links only when a program calls
      one of these, and a light casts only when asked. `.skshader` format 6
      ([PLAN-shadows.md](PLAN-shadows.md), `examples/shadows.c`)
- [x] Shadows on WebGPU (2026-09-21): were fully shadowed everywhere. Not a WebGPU
      problem: the depth pass never asked for its depth buffer to be kept, sokol's
      default store action for depth is DONTCARE, WebGPU honours the discard and GL
      only treats it as a hint. One explicit store action. `make webcheck` now records
      the browser's own log entries (Dawn's validation messages live there, not in the
      console API) and `--verbose` prints them (PLAN-shadows.md, "The WebGPU bug")
- [x] Shadows, phase 2 (2026-09-21): spot lights cast through their own cone (a
      perspective fit), and up to four lights cast at once — one depth array with a
      layer each, since a texture per light would eat the sampler slots custom shaders
      need. Layers share the largest map size asked for; a light's slot rides in a
      spare component of its per-light data, so only the per-slot arrays grow.
      `.skshader` format 7 ([PLAN-shadows.md](PLAN-shadows.md), `examples/shadows.c`)
- [x] Shadow cost measured (2026-09-21): `make shadowbench [DESKTOP=1]`
      (tools/bench/shadowbench.c, also a web page). RTX 4080, vsync off, frame ms (and
      the CPU ms in it) at 100 / 400 / 1000 models: none 0.25/0.62/1.41; sun at 1024
      0.42/0.82/1.94, at 2048 0.33/0.90/1.95, at 4096 0.38/1.03/2.06; sun + spot
      0.45/1.11/2.36; nothing receiving 0.30/0.79/1.60 (the pass is skipped), and at
      4000 models none 5.18, sun 6.99, two lights 8.57. The CPU cost barely moves with
      the map's size, so that axis is GPU fill (16x the pixels: ~+0.05 / +0.12 ms),
      while the extra pass over the casters is CPU and scales with them. Receiving
      costs ~0.02. Which dominates is a property of the scene
- [x] The model draw queue grows (2026-09-21): it was two fixed arrays, 1024
      placements and 8192 primitives, and a frame past either lost the rest of its
      models after one warning — found while benchmarking, where asking for 1600 and
      4096 models both measured the same 1024. They now double from 64 / 256 up to
      16384 / 131072, in the shape of sk_scene's transparent list, and the warning is
      kept for the ceiling (about 23 ms of submission, far past playable). Small
      programs stop carrying the room as well: ~448 KB of always-resident memory gone
- [ ] Models are one draw call each: 4000 lit models cost about 5 ms a frame on an
      RTX 4080 (shadowbench), and ~95% of that is CPU submission — roughly 1.2
      microseconds a model, whatever the model is. Sprites already avoid this (a run
      sharing texture, material, camera and clip is one instanced draw); models have no
      equivalent, so a forest of identical trees pays per tree. Sharing the mesh barely
      helps: 4000 models built from three shared meshes cost 4.53 ms against 5.18 ms
      with a mesh each (1.13 vs 1.30 microseconds a model), because sokol's bindings
      cache skips re-binding the same buffers but the uniform uploads and the draw call
      itself are per model regardless. So instancing's win is collapsing N draws into
      one, not avoiding buffer churn. It would be the same shape as the sprite batch:
      collect per-instance transforms into a buffer, one draw per group. The queue growing (above)
      raised the ceiling on how many can be queued; this is the ceiling on how many are
      worth queuing
- [ ] Lit particles: emitter particles are unlit — emitters have their own program
      (`particle` = vs_particle + the unlit `fs` in src/shaders/sk_sprite.glsl) and no
      material API, so the only lit "particles" today are sprite3d objects moved by the
      CPU. A `particle_lit` program is mostly wiring now that `fs_lit` and the per-batch
      light block exist; the design question is where an emitter's lights come from,
      since its particles aren't in a sprite batch with bounds — probably one selection
      from the emitter's own bounds, with the same caveat as sprites (a particle far
      from the rest can miss a light near it)
- [ ] Materials later: particles (emitters) on custom shaders; custom shaders for 2D
      shapes
- [x] 2D / UI layer: `enabled` and pointer interaction per scene member, touch as a
      pointer, retained 2D shapes, nine-slice sprites, text alignment and wrapping,
      per-layer clipping, and sprite3d source/extent/pivot for 2D worlds on an
      orthographic camera ([PLAN-2d.md](PLAN-2d.md), `examples/ui.c`, `examples/2d.c`)
- [x] UI through the public API ([PLAN-ui.md](PLAN-ui.md), 2026-09-18): float immediate
      2D, rounded rectangles and borders, source-rect and nine-slice images, a nesting
      clip stack, length-taking text, crisp high-DPI glyphs, a growing glyph atlas, UI
      pointer/keyboard capture, a float two-axis wheel; `examples/clay.c` lays out UI
      with Clay and draws it through ~100 lines of public-API glue
- [ ] UI later (PLAN-ui step 4): clipboard and keyboard capture in use with text
      fields, letter spacing, the Dear ImGui extension hook
- [x] UI widgets (2026-09-21, decided as in ROADMAP "GUI direction"): the hand-built
      button, progress bar and scrolling list moved out of `examples/ui.c` into a shared
      `examples/ui_widgets.h` (a theme, `ui_button_*`, `ui_bar_*`, `ui_list_*`; each
      widget labels on the layer above the one it's given). No widget API in the core: a
      real widget layer belongs outside it, like the Clay glue, and anything it can't
      express through the public API (focus order, text-field editing, clipboard) is a
      core gap to fix there. `examples/ui.c` looks the same as before, bar an empty
      progress bar no longer showing its knob
- [ ] UI widgets later, if an example wants them: a slider that takes a value (the bar
      only shows one), a checkbox, a text field (needs the clipboard and keyboard
      capture above)
- [x] Limits that grow (2026-09-18): the sprite3d and sprite2d pools start at 256
      and double up to 65,534 (the handle's 16-bit index); the scene's transparent
      list doubles as needed; sokol_gl's per-frame vertex and command budgets double
      after a frame that ran out (logged: that frame lost its draws past them), up to
      1M vertices and 256K commands. Freed handle slots are reused oldest first, so
      churn no longer wraps one slot's 10-bit generation. spritebench: 32,768 sprites
      in every scene with the default build (was 1,024 sprite3d)
- [x] Every handle pool grows (2026-09-18): textures, meshes, models, materials,
      fonts, text2d/3d, shapes, lights, cameras, scenes, environments, audio, sounds
      and asset tasks start small and double up to 65,534. Along the way: the mixer
      walks the sound pool under its lock instead of a 128-entry pointer list, so
      sounds past the 128th are no longer silent; asset job queues grow with the
      tasks; web downloads are capped at 256 at once (sokol_fetch's pool) and the
      rest wait instead of failing; stale task pointers after queueing dependencies
      are gone
- [x] Instanced sprite3d (2026-09-18, [PLAN-sprites.md](PLAN-sprites.md) step 1): one
      record per sprite, quads and billboards built on the GPU, batches as render
      commands; with an indexed scene membership, a radix transparent sort and cached
      batch state, the benchmark field of 16,000 sprites went 2.9 -> 0.9 ms (desktop) and
      9.6 -> 3.6 ms (phone, WebGL2)
- [x] Linear sokol_gl replay (2026-09-18): `sgl_draw_layer` scanned all of the frame's
      commands for each layer, so many layers (sprites or models interleaved with
      shapes and text) replayed in quadratic time. libsk's sokol fork adds
      `sgl_draw_layer_range` (branch perf/sgl-draw-layer-range), and sk_render draws
      each layer's own command range: 3,000 switches replay in 0.6 ms instead of 5.4
- [x] Sprite alpha modes (2026-09-18, PLAN-sprites step 2): `sk_alpha_mode_t` shared with
      materials; opaque, masked and additive sprites aren't sorted and group by texture
      (16,000 masked sprites from 4 textures: 4 batches, 1.4 ms desktop / 1.2 ms Chrome)
- [x] Blended sprites from several textures on WebGL2 (2026-09-18): without
      base-instance draws, sprites are read by index from a float texture instead of
      rebinding per batch: 16,000 from 4 textures 20.5 -> 11 ms on the phone (sokol_gl:
      13.8), 12.2 -> 7.1 in Chrome
- [x] sprite2d on the instanced sprite path (2026-09-18, PLAN-sprites step 3), with
      `sk_sprite2d_set_alpha_mode`; immediate `sk_texture_draw*` stays on sokol_gl
- [x] Particle emitters, 3D and 2D, simulated on the GPU (2026-09-18, PLAN-sprites
      step 4): `sk_emitter3d_*` / `sk_emitter2d_*`, configured in code; particles written
      once at birth and moved by the GPU; `examples/particles.c`. 16,000 particles:
      CPU 1.1 -> 0.1 ms (desktop), 6.2 -> 0.6 ms (phone)
- [x] More for particles (2026-09-18, PLAN-sprites step 5), still stateless: drag,
      stretch along the motion, inherited velocity and spawning along a moving
      emitter's path; size and color curves (8 keys), a palette; flipbooks, prewarm, a
      spawn sphere / circle
- [ ] Particles later: a CPU-simulated mode for particles that react after birth
      (collisions, attractors), effects saved to files as resources
- [x] Render to texture: `sk_texture_create_target`, `sk_render_begin/end_texture`,
      `sk_texture_set_sampling` ([PLAN-render-target.md](PLAN-render-target.md),
      `examples/render_target.c`)
- [x] Screen effects (2026-09-21): post-processing as full-screen shader passes —
      `sk_render_add_effect` / `_clear_effects` / `_effect_count`, a chain of up to 8
      custom materials whose shaders include `sk_screen` (one program, `sk_screen_color()`,
      `sk_screen_uv`; `.skshader` format 5). The frame renders into a render target and
      the chain ping-pongs between two of them onto the screen
      ([PLAN-render-target.md](PLAN-render-target.md), `examples/postprocess.c`)
- [ ] Render targets later: per-target formats (HDR/float) so effects can tone map
      after bloom (WebGL2 needs EXT_color_buffer_float), keeping contents between
      frames (no clear) for trails, targets without depth or MSAA (effect chains
      allocate both today), reading pixels back / screenshots, and the depth buffer in
      a screen effect (fog, depth of field)

- [x] Generated meshes (2026-09-20): `sk_mesh_create_plane/cube/sphere/cylinder/cone/
      capsule/torus`, resources deduplicated by their parameters, with normals, texture
      coordinates and tangents (any material, normal maps and custom shaders included)
      and picking; one white, non-metallic material slot. Geometry in
      `src/sk_mesh_shapes.c` (pure, unit tested: winding, normals, bounds);
      `examples/meshes.c`; the shaders example's floor and spheres use them
- [ ] Generated meshes later: height maps (from an image: a path, so a resource like a
      loaded mesh), and other shapes when something needs them

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
- [x] Asset redirects (2026-09-20): `sk_asset_add_redirect(prefix, target)` /
      `sk_asset_clear_redirects`. Path rules stack, newest first, then the file itself,
      so a file missing under a mod or a translation falls through (quietly; a 404 each
      on the web); a target with "://" is where files download from (web). They apply to
      ensured files and the files those reference: a model reads its buffers and images
      from where the asset layer found them (`sk_asset_found_path`). Plain prefixes;
      wildcards if a game needs them

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
- [ ] Other image formats: WebP; compressed textures for `.glb` models and
      KHR_texture_basisu, KTX 2, smaller ASTC blocks (PLAN-textures "Not in this plan")

## Lessons from librl to design for

- [x] Networking decided (2026-09-20): libsk fetches assets only (desktop HTTP(S)
      through the OS's clients, deferred); WebSockets and general networking go in a
      separate library outside libsk (ROADMAP "Future")
- [x] Web size (2026-09-17): web builds weren't link-optimized (no -O: no wasm-opt,
      unminified JS, assertions) and carried all three backends' shader sources.
      Now -O3 (WEB_DEBUG=1 for debug builds) and sokol-shdc --ifdef: simple went
      from 874 KB wasm + 425 KB JS to 693 + 190 KB (323 KB gzipped; librl's c-simple
      is 653 + 264 KB, 342 KB gzipped). The rest of the gap: every program links
      every subsystem (below)
- [x] Web startup (2026-09-19): `make webstart` (`tools/webstart.mjs`) times cold,
      warm and hot visits from `sk:*` performance marks, locally, on emulated 4G and on
      a phone. Fixed: worker threads no longer hold up main() (~500 ms on 4G);
      versioned code (`?v=<hash>`, `tools/webdeploy.py`) cached for good, so a warm
      visit fetches no code; the wasm downloads alongside the JS; the BRDF table is
      baked (35-40 ms of every start); sprites, particles, models and the audio device
      are set up on first use. 4G, `simple`, first frame: cold 1214 -> 744 ms, warm
      1112 -> 389 ms. Pixel over Wi-Fi: libsk's setup 80-186 -> 15-34 ms, first
      frame 422-816 -> 241-444 ms. README "Startup and hosting" lists the headers a
      host needs
- [x] Web size, flags (2026-09-19): release web builds define NDEBUG (no sokol
      validation layer or C asserts; desktop, headless and WEB_DEBUG builds keep them)
      and run Closure on the JS glue with `-sENVIRONMENT=web,worker`: hello 361 -> 311
      KB gzipped (wasm 316 -> 284, JS 45 -> 27). Measured and not worth it: `-Oz` at
      link (-5 KB, slower code), emmalloc (-2 KB)
- [x] Web size, structure (2026-09-19): programs link only the subsystems they use
      (ARCHITECTURE.md §7b: optional modules register themselves, the core reaches
      them through hooks; `make check` guards it). Gzipped: hello 311 -> 134 KB,
      sprite programs ~170, model programs ~240, everything ~300
- [ ] Web size later: browser-native image/audio decoders on web (async decode
      through JS); the baked BRDF table costs ~14 KB gzipped (half floats barely
      compress). Measured (2026-09-20, wasm code by library, gzipped, each group on its
      own so approximate; `--profiling-funcs` builds): hello = libsk 42, C runtime 21,
      sokol 17, fonts (fontstash, stb_truetype: all text, the built-in font too) 13
      KB; model adds cgltf 16 and stb_image 16; audio programs add dr_mp3 + dr_wav +
      stb_vorbis 34. Browser decoders would save ~16 KB (images) and ~34 KB (audio):
      `simple` 308 -> ~260 KB. For comparison, three.js 0.186 bundled with esbuild:
      130 KB gzipped for a hello3d, 155 for glTF + animation + lights, 161 with an HDR
      environment, a shader material, sprites, picking and audio (it leans on the
      browser's decoders and has no text or worker loading). Speed, same machine and
      browser (CPU ms a frame, frame-rate cap off; build/perf in a work tree): 2D
      sprites libsk vs PixiJS 8.21 are level (16k: 2.6 vs 2.6; 64k: 9.6 vs 10.2);
      3D billboards libsk vs three.js `Sprite` 2.6 vs 26.9 at 16k (its hand-managed
      `InstancedMesh`: 2.3), and 9.7 vs 85.6 at 64k (`InstancedMesh` 5.1); animated
      crowds libsk vs three.js 2.1 vs 2.0 at 100 models, 7.8 vs 6.1 at 400. Audio caveat: the browser
      decodes a whole file at once (decodeAudioData), which undoes libsk's streamed
      music (PLAN-audio: ~108 MB -> 6 MB for a long track)
- [ ] Explore (later, own session): libsk's C API as the contract with other
      implementations, e.g. a JS backend (three.js/Babylon) for JS-target games, or
      another implementation language (Zig, Odin, D betterC, Beef; engines like
      Sedulous). Compare footprint and caching against the C + sokol build first
- [x] Optional subsystems (2026-09-19): linked by use, no build flags (see web size,
      structure, above); `make websize` reports per example
- [x] Scripting and language bindings stay out of the core repo (decided; see ROADMAP)

## Open decisions

- [x] Handle-only API for point lists: line strips are built point by point on a
      retained shape (`sk_shape3d_set_line_strip` + `sk_shape3d_add_point`); batch
      asset ensure is designed with the loading pipeline
- [ ] Which language binding comes first
- [x] Naming, part 1 (2026-09-17): resources now have `sk_<resource>_release`
      instead of `destroy`, because that's what it does — drop this handle's
      reference — and objects keep `destroy`, so the name says which layer you're on
      (texture, mesh, audio, font, material, environment). The public wrappers
      collapsed onto the internal `release` functions that already existed; `retain`
      stays internal (one `create` is one reference). `create` stays `create` for
      both layers on purpose: the noun says whether it takes a path or a handle, and
      generators like `sk_mesh_create_cube` load nothing
- [ ] Naming, part 2: the `sk_` prefix names the sokol implementation rather than the
      API. Only worth changing if the "C API as a contract" exploration goes ahead —
      then do it in the same sweep as any other rename, before bindings depend on the
      names
- [x] Fonts are resources like the rest (2026-09-17): refcounted and deduped by
      path; text objects and the default font hold references; a released font's
      fontstash data is kept by path and reused (fontstash can't remove fonts).
      Follow-up: a `.ttf/.otf` loader so reading big font files runs in the loading
      pipeline
