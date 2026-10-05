# wgrender Tasks

> Carried into libwgt (17f3f18, cb5dee5), and kept here while wgrender is maintained:
> CONVENTIONS.md, "Docs".

Working checklist. Order and reasoning live in [ROADMAP.md](ROADMAP.md); this file
is what's left to do; what's done is in [HISTORY.md](HISTORY.md).

Workflow: pick the top unchecked item, outline a plan (AGENTS.md, "Agent practice"), implement with
tests, keep `python3 tools/verify_builds.py` passing, and move the item to
[HISTORY.md](HISTORY.md) ("Tasks done") in the same commit. A task dropped instead
moves to "Tasks dropped" with the reason. What's here is what's left.

## Bugs and measurements

- [ ] Ogg files go through stb_vorbis 1.22 (`deps/stb`), which has open memory-safety
      issues upstream on malformed files: nothings/stb #2005 (2026-09, a heap overflow in
      the comment header), #1947 and #1933 (codebook allocation overflows), #1949 (a
      codebook out-of-bounds read), #1934 (an incomplete fix of CVE-2019-13220), #1552
      (CVE-2023-45675), and fuzz crashes #1491, #1248 and #1168. A downloaded `.ogg` is
      untrusted input, as the dr_libs pin already assumes for WAV and MP3. libwgt
      (2026-10-02) drops stb_vorbis: on the web the browser decodes all audio, and
      natively it takes Xiph's decoder (libvorbis or Tremor, plus libogg). Here: the
      same, or at least a broken-file test of the Ogg decoder under asan.
- [ ] A sound's pitch has no stated range: `wgr_sound_set_pitch` stores any float and its
      header says nothing. The mixer steps by it (`mix_sound` in `src/wgr_audio.c`), so a
      negative pitch plays backwards from the current position and stops at the start,
      ignoring loop -- from `play`, which starts at 0, it plays one frame -- and 0 holds
      the position silently while `is_playing` stays true. Safe (a position below 0
      stops the sound) but not a promise anyone made. Decide and say it in the header:
      refuse a negative pitch, or play backwards from the end as libwgt's player does
      (2026-10-02: reverse for decoded audio, refused for streamed, 0 holds).
- [ ] The normal-mapped sphere in `examples/loading.c` (and environment, materials)
      shows a straight vertical cut on its left edge, where its outline is round.
      On main too, and gone without the normal map: either a tile groove at a grazing
      angle (expected: a normal map can't change the outline) or the sphere's UV seam
      with tangent frames that don't match across it (a bug). Turning the sphere or
      offsetting the texture tells them apart: a groove moves with the texture, a
      seam stays on the mesh.
- [ ] Bug (platform, XWayland): vsync doesn't hold under COSMIC/XWayland on NVIDIA
      (RTX 4080 laptop, driver 580) with sokol's GL backend. Not the driver: on the same
      machine, GPU and driver under XFCE on X11 (2026-09-21, a 300-frame probe with vsync
      on) frames land every 16.7 ms -- 60.3 fps on a 59.93 Hz display, 294 of 299
      intervals within 14-19 ms, one long/short alternation. So it is the compositor's
      presentation path accepting a swap without waiting for the vblank, which is also
      why a raw GLX program showed it. Real for anyone on a Wayland desktop; as observed
      there, swaps blocked only every other frame:
      ~120 frames/s on a 59.88 Hz display, intervals alternating ~16.7 ms and <4 ms,
      half the frames never shown. Not a libwgrender/sokol timing bug: a raw GLX program
      (no sokol) reproduces it with GLX_SWAP_INTERVAL=1 confirmed, the interval set
      before or after mapping, with or without glFinish, and with
      `__GL_SYNC_TO_VBLANK=1` or `__GL_MaxFramesAllowed=1`. Target caps
      are unaffected. Options: sokol's Vulkan backend on Linux (FIFO present), pacing
      to the display refresh ourselves (refresh via XRandR), native Wayland (sokol
      has none). Checking vsync needs a monitor that is on
- [ ] Light selection uses rest-pose bounds for animated models, so a limb far
      outside the rest pose can miss a nearby point light's range check. Minor;
      could reuse the posed bounds when they're already cached
- [ ] Loading follow-up: on the phone, frames reach 25-30 ms while a model's large
      textures upload. Tried (2026-09-20) and dropped: capping uploads by size per frame
      (4, 8, 16 MB): no better (at 4 MB, one 2K ASTC texture a frame, frames still reach
      ~30 ms) and slower to load; the cost is each large upload itself. Left: smaller
      files (ASTC 6x6 blocks, about half the bytes, some quality), or one mip level per
      frame (needs a change to libwgrender's sokol fork)
- [ ] Web performance on a low-end phone (Adreno 610, WEB_THREADS=0 build). Frame
      rates measured 2026-09-20: 60 FPS for most examples; instancing 13, shadows 13,
      postprocess 21, meshes 26, materials 30, shaders 31, environment 41. The
      measurement pass that entry asked for is done (2026-09-21, same phone, served
      over `adb reverse`), and it says the floor is CPU submit, not the GPU:
      - `make webstart`: startup is a flat ~400 ms of JS+wasm fetch and compile that
        caching barely moves (warm and hot land within noise of cold -- the wasm is
        105-286 KB gzipped and it is not the download), then ~150 ms to the first
        frame. What separates the examples is `ready`, the first frame with nothing
        pending: loading 4.0 s, environment 4.0 s, shaders 2.9 s, instancing 1.5 s,
        shadows 1.4 s, and everything else under 1.1 s. So startup work is asset and
        shader work, not load time
      - `make shadowbench-web`: one casting light costs ~16-20 ms of CPU at any model
        count (100 models: 1.9 ms off, 18.2 ms with a sun), and the shadow map's size
        is free within noise -- 1024, 2048 and 4096 all land at 18-24 ms. Two lights
        add ~9 ms. Most of it is on the *receiving* side, not the caster pass: with
        nothing receiving, 5.7 ms instead of 18.2, and the difference is all submit
        (3.8 vs 16.5), i.e. the pipeline and binding changes a receiving draw needs
      - the two levers are both about draw count. Instancing (`shared`) at 4000 models:
        44.7 ms against 75.0 ms per-model, and 44.9 vs 113.2 in the wide case. Frustum
        culling at 4000: 11.0 ms facing away with it on, 67.7 ms with it off
      - `make spritebench-web`: emitters are about ten times cheaper than sprite
        objects for the same particle count -- 16000 particles is 2.0 ms through an
        emitter and 23.9 ms as sprite3d objects moved by the CPU, nearly all of it
        scene-build time. The one cliff is texture switching: "field, 4 textures" at
        16000 spends 17.2 ms in submit where the atlas version spends 0.8
      So: optimise draw submission (fewer pipeline changes on shadow receivers, wider
      instancing, batching across textures), not fill rate or map sizes. Worth
      re-measuring `ready` for loading/environment/shaders before anything else, since
      seconds there dwarf the frame costs
- [ ] Web audio stutters through a slow frame (2026-09-21, seen in `audio` with the S
      stall; desktop plays through). sokol_audio's Emscripten backend feeds WebAudio from
      a ScriptProcessorNode callback on the main thread, so a frame that blocks for
      longer than the ~46 ms device buffer starves it, threads or no threads. The fix is
      an AudioWorklet backend (the callback on the audio rendering thread, fed from a
      SharedArrayBuffer ring), which sokol's own comment calls out as the eventual
      replacement; it would live in the sokol fork. Until then a game that stalls the
      main thread (a big synchronous load) will hear it on the web

## From libwgt

libwgt (`whirlinggizmo/libwgt`) is a layered port of wgrender; where it found something
better, wgrender adopts it, and where wgrender's is better it stays. Branch
`from-libwgt` first, then the API items, each on a branch of its own.

- [ ] The org's CONVENTIONS.md takes the same names (another repo: the user's call).
- [ ] Hidden symbol visibility: only `wgr_*` exported.
- [ ] A spot light's shadow bias (a bug; libwgt 0c9ea22): `src/wgr_shadow.c` converts
      the bias to depth units as `texel_world / depth_range` for every light, which
      is right only for an orthographic (directional) map. A spot's map stores depth
      non-linearly, so a thin caster's shadow starts late (the character's leg shadow
      at the knee). libwgt takes the bias in world units at the fragment's own
      distance, with a WebGL test of a thin pillar's shadow at its foot. Own branch
      off main; it touches the model shader, so link-check on the Adreno phone.
- [ ] A model mirrored by its own scale (a bug; libwgt 26bf8c5, 5f230d0):
      `wgr_model_set_scale(m, -1, 1, 1)` draws with back-face culling and CCW front
      faces, so the model shows its inside, and a normal map is lit with the wrong
      handedness. A mirror inside a glTF file is fixed at load (`src/wgr_model.c`,
      the node's determinant), a model's own transform isn't. Draw a placement with a
      negative determinant through a CW-front pipeline variant, carry its handedness
      to the shader for the tangent's sign, and keep mirrored and unmirrored copies in
      separate instanced draws. With the spot bias fix above: one branch, one Adreno
      link check.
- [ ] Pixel tests: frames read back in headless Chrome and in a GL window on Xvfb; a
      desktop check of every example with screenshots. libwgt's way (78804dd,
      46f1bc6): each desktop example in a real window on a private Xvfb display with
      OpenGL in software, failing on an early exit or an error, the screen read from
      the file Xvfb keeps it in and written as PNG with the standard library; and one
      exact-pixel test drawn through the normal program path in such a window.
- [ ] ARCHITECTURE.md: the lifecycle of resources and objects in one place (libwgt
      0acd18a): resources shared and released, objects owned and destroyed, an object
      holds a reference to each resource it uses, destroying an object never destroys
      a resource, and the usual hand-over-then-release pattern.
- [ ] Public math (own branch): vec2/3/4, quat, mat4 operations `inline` in public
      headers with one exported copy each for bindings; types `wgr_vec3_t`,
      `wgr_quat_t`, `wgr_mat4_t` (column-major `float m[16]`; `matrix_t` goes); the
      internal `wgri_mat4_*` / `wgri_v3_*` replaced by it; CONVENTIONS.md's "Public API shape" adds mat4 to the math values a call may take.
- [ ] A relative asset host resolves against the program's location (Rob, 2026-10-05,
      libwgt first): the executable's directory natively, the page's directory on the
      web, and leaving the host unset means the same. Today wgrender resolves a
      relative host natively against the working directory, so the same host string
      means different things on the two platforms, and `examples/shared/example_assets.h`
      hides it behind an `#if` ("assets" on the web, "examples/assets" natively, valid
      only when run from the repo root). That `#if` is wgrender working around the rule
      and must not be ported into libwgt. When libwgt's commit lands, port it back with
      its header wording and test: the native examples set `"../assets"`, the build
      links `out/<platform>/<variant>/assets` to `examples/assets`, and the examples
      run from any directory. Dropping the `#if` entirely needs libwgt's per-example
      pages too: layout, later
- [ ] The load budget is a resource setting, not an asset one (Rob, 2026-10-03, as
      libwgt's `wgt_resource_set_load_budget`): it paces every resource load,
      bundled, cached or local, so `wgr_asset_set_upload_budget` becomes
      `wgr_resource_set_load_budget` / `_get_load_budget` in `wgr_resource.h`, its
      ~45 ms 4096² note kept; a bool setter, false for a negative or non-finite
      value (Rob: refuse, not clamp; 0 is one step a frame). Callers: `examples/loading.c`,
      `tools/bench/loadbench.c`, `tests/unit/pipeline_test.c`, `tools/check_rules.py`;
      regenerate both bindings.
- [ ] Maybe: a node tree (parenting, cached transforms, enabled / visible / pickable
      as separate flags). Biggest API change here; only if a hierarchy is wanted.

## Rendering

- [ ] A model with an opaque part and a see-through part gets two placements a frame,
      one per pass it appears in, because begin_draw only reuses the last placement
      when the same model is drawn twice in a row; a skinned one uploads its joint
      matrices twice as well. Cheap (a record and a few matrices), and instancing
      groups each pass's parts across models regardless, but it is wasted work: found
      while reviewing instancing
- [ ] Render targets later: per-target formats (HDR/float) so effects can tone map
      after bloom (WebGL2 needs EXT_color_buffer_float), keeping contents between
      frames (no clear) for trails, targets without depth or MSAA (effect chains
      allocate both today), reading pixels back / screenshots, and the depth buffer in
      a screen effect (fog, depth of field)
- [ ] Generated meshes later: height maps (from an image: a path, so a resource like a
      loaded mesh), and other shapes when something needs them

## Materials and glTF

- [ ] Custom shaders later: arrays and matrices as parameters; D3D11/Metal sources when
      those backends come
- [ ] Lightmaps: baked lighting as a material texture (its own texture coordinate set,
      which materials already support), multiplied into the surface. No new passes;
      bake them in Blender
- [ ] Environment follow-ups: prefiltering runs on a loading worker when loaded
      through `wgr_asset` (330 ms per 1K HDR; sync creates still block), other inputs (6 cube faces, KTX2
      prefiltered), RGBM fallback for backends that can't filter half-float textures,
      HDR framebuffer (bloom, tone mapping sprites together with models)
- [ ] Generated tangents come from texture coordinate set 0; normal maps on set 1
      need tangents in the file
- [ ] Mipmaps average in stored (sRGB) space and don't renormalize normal maps
- [ ] Other glTF material extensions (clearcoat, transmission, sheen, specular, ior, ...)
- [ ] Morph targets (animation weights are skipped)
- [ ] Other image formats: WebP; compressed textures for `.glb` models and
      KHR_texture_basisu, KTX 2, smaller ASTC blocks (PLAN-textures "Not in this plan")

Done (2026-09-16, verified against Khronos TextureTransformTest, MultiUVTest,

VertexColorTest, TextureSettingsTest and BoxTextured on desktop, WebGL2, WebGPU):

## Particles

- [ ] Lit particles: emitter particles are unlit — emitters have their own program
      (`particle` = vs_particle + the unlit `fs` in src/shaders/wgr_sprite.glsl) and no
      material API, so the only lit "particles" today are sprite3d objects moved by the
      CPU. A `particle_lit` program is mostly wiring now that `fs_lit` and the per-batch
      light block exist; the design question is where an emitter's lights come from,
      since its particles aren't in a sprite batch with bounds — probably one selection
      from the emitter's own bounds, with the same caveat as sprites (a particle far
      from the rest can miss a light near it)
- [ ] Materials later: particles (emitters) on custom shaders; custom shaders for 2D
      shapes
- [ ] Particles later: a CPU-simulated mode for particles that react after birth
      (collisions, attractors), effects saved to files as resources

## UI and input

- [ ] Gamepads later: macOS (GameController framework), rumble, connect/disconnect
      events, a mapping database for pads the kernel doesn't name by position
- [ ] Touch later: long-press and swipe/fling recognizers if a game wants them;
      pinch from desktop trackpads (browsers send it as ctrl + wheel)
- [ ] UI later (PLAN-ui step 4): clipboard and keyboard capture in use with text
      fields, letter spacing, the Dear ImGui extension hook
- [ ] UI widgets later, if an example wants them: a slider that takes a value (the bar
      only shows one), a checkbox, a text field (needs the clipboard and keyboard
      capture above)

## Assets and loading

- [ ] Music that starts before its file has arrived: wgrender streams decoding (an
      Audio over 1 MB is decoded while it plays) but not the download -- the whole
      file must be local before `wgr_audio_create`. A streamed Audio, ready once
      enough has arrived, with seeking refusing what hasn't; it wants the asset layer
      to deliver a file in pieces, and the polled-task status (ROADMAP: audio
      streaming; libwgt's roadmap has the same `create_streamed`).

- [ ] Loading follow-ups: shader warm-up (the first frame drawing loaded PBR
      models stalled ~220 ms on WebGL2 while programs compiled; not seen on the Pixel 9
      with FlightHelmet in a lit scene: recheck with an environment); the zero-worker mode
      prepares a whole glTF in one frame (~1 s for FlightHelmet); `.glb` dependency
      listing reads the whole file on the main thread

- [ ] A large web download stalls a frame on arrival (measured in libwgt 605d64b,
      2026-10-03, whose fetch is ported from ours): a 64 MB file held one frame ~80 ms,
      hashed or not, which is the copy into the wasm heap plus the store write in one
      step. Measure ours the same way; the fix is writing in pieces, or JS writing
      the bytes to IndexedDB directly. libwgt's measurement also confirmed our
      manifest hashing in crypto.subtle is what keeps hashing off the frame (wasm
      SHA-256 is ~350 MB/s, so a 100 MB file hashed in one step holds a frame ~300 ms).
      Natively we have that stall: `download_matches` reads and hashes a listed
      download whole on the main thread; libwgt hashes it 64 KB at a time, at most
      2 ms an update

## Platform

- [ ] Windows on real Windows: windows (the window flags), WASAPI audio, XInput
      gamepads; Direct3D 11 (sokol-shdc HLSL output) instead of OpenGL
- [ ] Native iOS / Android: long stretch goal. sokol supports both (Metal/GLES3,
      CoreAudio/AAudio, touch); libwgrender would need build targets, Metal shaders, app
      lifecycle and bundle/APK file access. Until then, mobile runs the wasm build
      (mobile browser, a wasm host app, or a shell like Electron/Tauri; hosts without
      cross-origin isolation need WEB_THREADS=0)

## Infrastructure and tooling

- [ ] `tools/check_asset_cache.py` may wait out every visit on a GPU display
      (libwgt, 2026-10-03, on its port of the same judge): there DevTools never
      reported a 404's `Network.loadingFinished`, so each visit ran its full 20 s
      settle (126 s for 8). libwgt settles a request on a response of 300 or more.
      Time ours with `--backend=webgpu` on the desktop display; same fix if it bites
- [ ] Unit tests still missing: the web half of `wgr_fs` (MEMFS + IndexedDB) needs a
      browser, so it wants the wasm-side tests below; `wgr_asset` loader/mapper
      registration is only exercised through real loaders
- [ ] Later: wasm-side unit tests when web-only code needs them
- [ ] `tools/headers.py` still reads two things out of strings clang prints: a
      function's return type out of its type's spelling (`_returns`: `const char
      *(wgr_handle_t)`, since `-ast-dump=json` has no return-type field) and an array
      field's bound (`_split_array`: `int[256]`). libclang's Python bindings give both
      as values (`cursor.result_type`, `type.element_count`), but they're a pip
      package (`libclang`) that bundles a clang of its own, tens of MB a platform and
      behind (18.1 against emsdk's), so the checks would parse with a different clang
      than the one that builds; the bindings alone (pip `clang`) need a `libclang.so`,
      which emsdk doesn't ship. Every tool `verify_builds.py` runs is standard library
      only today (fontTools and brotli are needed only by `gen_default_font.py` and
      for a brotli size). Worth it if a binding's generator needs more than these two
- [ ] API reference docs, generated from the public headers. Doxygen is the one
      everyone knows and it looks its age; the modern options are Doxygen + Breathe +
      Sphinx (heavy), Doxygen + doxygen-awesome-css (one stylesheet, keeps the
      pipeline), or a small generator of our own over `include/*.h`, which is tempting
      because the surface is 468 functions of one shape and the comments are already
      the documentation. Wanted once the API has settled; the wgr_/wgri_ split now
      makes "what is public" a mechanical question a generator can answer
- [ ] Web size later: browser-native image/audio decoders on web (async decode
      through JS); the baked BRDF table costs ~14 KB gzipped (half floats barely
      compress). Measured (2026-09-20, wasm code by library, gzipped, each group on its
      own so approximate; `--profiling-funcs` builds): hello = libwgrender 42, C runtime 21,
      sokol 17, fonts (fontstash, stb_truetype: all text, the built-in font too) 13
      KB; model adds cgltf 16 and stb_image 16; audio programs add dr_mp3 + dr_wav +
      stb_vorbis 34. Browser decoders would save ~16 KB (images) and ~34 KB (audio):
      `simple` 308 -> ~260 KB. For comparison, three.js 0.186 bundled with esbuild:
      130 KB gzipped for a hello3d, 155 for glTF + animation + lights, 161 with an HDR
      environment, a shader material, sprites, picking and audio (it leans on the
      browser's decoders and has no text or worker loading). Speed, same machine and
      browser (CPU ms a frame, frame-rate cap off; build/perf in a work tree): 2D
      sprites libwgrender vs PixiJS 8.21 are level (16k: 2.6 vs 2.6; 64k: 9.6 vs 10.2);
      3D billboards libwgrender vs three.js `Sprite` 2.6 vs 26.9 at 16k (its hand-managed
      `InstancedMesh`: 2.3), and 9.7 vs 85.6 at 64k (`InstancedMesh` 5.1); animated
      crowds libwgrender vs three.js 2.1 vs 2.0 at 100 models, 7.8 vs 6.1 at 400. Audio caveat: the browser
      decodes a whole file at once (decodeAudioData), which undoes libwgrender's streamed
      music (PLAN-audio: ~108 MB -> 6 MB for a long track)

## Bindings

- [ ] A plain-JS program's trimmed listing (`build_host.py --trimmed`) derived from
      something that parses the program, such as a bundler that reports the exports it
      kept (Rollup's `renderedExports`). Written by hand today; Haxe derives its own.

## Outside the library

- [ ] Scripting, as its own module/repo on top of the public API
- [ ] Explore (later, own session): libwgrender's C API as the contract with other
      implementations, e.g. a JS backend (three.js/Babylon) for JS-target games, or
      another implementation language (Zig, Odin, D betterC, Beef; engines like
      Sedulous). Compare footprint and caching against the C + sokol build first
