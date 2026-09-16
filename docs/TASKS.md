# libsk Tasks

Working checklist. Order and reasoning live in [ROADMAP.md](ROADMAP.md); this file
is what's done and what's next. Per-function librl parity is tracked in
`tools/parity.map` (run `make parity`), not duplicated here.

Workflow: pick the top unchecked item, outline a plan (AGENTS.md), implement with
tests, keep `make`, `make examples`, `make check` and `make parity` passing, and
tick the box in the same commit.

## Infrastructure

- [x] `make parity`: librl → libsk API parity report (`tools/parity.sh`, `tools/parity.map`)
- [x] Unit test setup: `make test`, `tests/unit/` (no stubs, links lib/libsk.a);
      first tests cover the handle pool, matrix math, picking math and the
      transparent sort. They found two pick-normal bugs (fixed)
- [ ] Unit tests: `sk_fs` / asset bookkeeping, scene layers and ordering, sprite
      alpha-test picking, animation sampling, text2d state, render command-list
      merging
- [ ] Later: `SANITIZE=1` (ASan/UBSan) test build; wasm-side unit tests when
      web-only code needs them
- [x] Null / headless renderer: `make HEADLESS=1` builds `lib/libsk_headless.a`
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
- [ ] 2D scene drawables (sprite2d, text2d in a scene) draw after all 3D layers
      once they exist
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
- [ ] Decide: asset callbacks run on the main thread, so creating a resource
      blocks the frame (the MP3 decode takes ~0.22s). Related to audio streaming
- [x] Lighting: light objects (directional, point, spot), per-scene lights and
      ambient, up to 8 lights per model by contribution, nothing lit implicitly
      (docs/PLAN-lighting.md, examples/lights.c). `simple.c` lighting PARITY note
      removed
- [ ] Remove the remaining `PARITY:` note in `examples/simple.c` when FPS drawing
      in a custom font lands

## librl parity (functional, not 1:1; see `make parity` for function-level status)

Each item starts with a short design review: what librl did, what went wrong or
felt awkward, and the libsk design. Update `tools/parity.map` with the outcome.

- [ ] Small leftovers: sound pan, read back the asset host, animation frame
      count and seeking to a frame, FPS readout with a custom font, a handle for
      the built-in font
- [x] 2D sprites and screen-space texture drawing: `sk_sprite2d_*` (source rect,
      pivot, rotation, x/y scale with flip, size, alpha-tested picking) and
      `sk_texture_draw`; scenes draw 2D after 3D and pick it first; 2D, mouse and
      screen size are in logical pixels (docs/PLAN-sprite2d.md, examples/sprite2d.c)
- [ ] 3D text
- [ ] 3D shapes: rectangles and circles (immediate + retained), retained lines
- [ ] 3D line strips: needs a handle-only way to pass points
- [ ] Per-object picking and pickable flags on every pickable object; read back
      a sprite's transform
- [ ] Pick statistics for debugging
- [x] Lighting controls: redesigned as light objects in scenes (see above)
- [ ] Window and monitor control: size, position, monitor queries (check web)
- [ ] Assets: ensure many files at once (handle-only), host reachability check
- [ ] Models: handle validity checks, a default placeholder mesh
- [ ] Ground-plane texture drawing

## Parity outside the API

- [ ] Gamepad input (`sk_input_*`)
- [ ] Touch input (`sk_input_*`)
- [ ] First language binding, as its own module/repo (decide which; Beef is a candidate)
- [ ] Scripting, as its own module/repo on top of the public API

## Roadmap features

- [x] Materials, phase 1: material resource, glTF metallic-roughness and unlit
      shading, normal/occlusion/emissive maps, sRGB-correct lighting, per-model
      slot overrides ([PLAN-materials.md](PLAN-materials.md), `examples/materials.c`)
- [ ] Materials, phase 2: custom shaders (`.skshader` packages from sokol-shdc)
- [ ] Materials, phase 3: shapes and sprites on materials (with the batched renderer)
- [ ] 2D / UI layer: screen space, draw ordering, 2D picking, pickable UI primitives
- [ ] Particle emitters (batched/instanced)
- [ ] Offscreen / render-to-texture (when the first consumer needs it)

## Materials: not supported yet

Wanted next:

- [ ] Second texture coordinate set (TEXCOORD_1): glTF textures that use
      `texCoord: 1` (common for occlusion/lightmaps) currently fall back to set 0
      with a warning. Needs a second UV vertex attribute, a per-texture set index on
      the material, and the shader choosing per texture.
- [ ] glTF images in separate files (`.gltf` + `.png`/`.jpg`, and external `.bin`
      buffers): only images and buffers embedded in `.glb` load today; others are
      skipped with a warning. Needs the asset layer to ensure the referenced files
      (relative to the `.gltf`) before `sk_mesh_create`, on web too.
- [ ] Environment lighting (image-based lighting): without it metals only show
      direct highlights and look dark. Scene environment map (prefiltered
      specular + irradiance, BRDF lookup table), probably with HDR input and tone
      mapping (below).
- [ ] Texture transforms (tiling, offset, rotation) per material texture:
      loaded from glTF `KHR_texture_transform`, and settable in code by name (e.g.
      `base_color_texture_scale` / `_offset` / `_rotation`; needs
      `sk_material_set_vec2`). Today UVs outside 0..1 tile (textures repeat), but
      the extension is ignored and code-created materials can't tile. Alpha-tested
      picking must apply the same transform. Also enables scrolling textures and
      flipbooks.

Later:

- [ ] glTF vertex colors (COLOR_0) multiplying base color
- [ ] glTF sampler modes (wrap, filter); today always linear + repeat
- [ ] Mipmaps for material textures (distant textures shimmer)
- [ ] Tone mapping / exposure (lit values above 1 clip)
- [ ] Other glTF material extensions (clearcoat, transmission, sheen, specular, ior, ...)
- [ ] Morph targets (animation weights are skipped)

## Lessons from librl to design for

- [ ] Networking: one async model and a single `sk_net` module under `sk_asset`
- [ ] Optional subsystems: exclude modules at build time to shrink wasm, with a
      per-example size report
- [x] Scripting and language bindings stay out of the core repo (decided; see ROADMAP)

## Open decisions

- [ ] Handle-only API for point lists (`line_strip_3d`, batch asset ensure)
- [ ] Which language binding comes first
