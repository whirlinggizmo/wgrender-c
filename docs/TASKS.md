# libsk Tasks

Working checklist. Order and reasoning live in [ROADMAP.md](ROADMAP.md); this file
is what's done and what's next. Per-function librl parity is tracked in
`tools/parity.map` (run `make parity`), not duplicated here.

Workflow: pick the top unchecked item, outline a plan (AGENTS.md), implement with
tests, keep `make`, `make examples`, `make check` and `make parity` passing, and
tick the box in the same commit.

## Infrastructure

- [x] `make parity`: librl → libsk API parity report (`tools/parity.sh`, `tools/parity.map`)
- [ ] Unit test setup: `make test`, `tests/unit/` layout mirroring librl, first
      tests for the handle pool and ray-vs-cube/sphere picking math
- [ ] Unit tests: `sk_fs` / asset bookkeeping, scene layers and ordering, sprite
      alpha-test picking, animation sampling, text2d state
- [ ] Null / headless renderer (dummy sokol backend + own tick loop)
- [ ] Headless smoke: run every example for N frames, require exit 0 and no errors
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
- [ ] Bug: `sk_set_target_fps` does nothing (swap interval is always 1; see `sk_run`)
- [ ] Decide: frame `dt` comes from `sapp_frame_duration()`, which is smoothed and
      capped at 0.1s, so time accumulated from `dt` runs slow when frames stall
      (e.g. a hidden window). Document it, or also expose unsmoothed time
- [ ] Decide: asset callbacks run on the main thread, so creating a resource
      blocks the frame (the MP3 decode takes ~0.22s). Related to audio streaming
- [ ] Remove the `PARITY:` notes in `examples/simple.c` as lighting control and
      FPS drawing in a custom font land

## librl parity (functional, not 1:1; see `make parity` for function-level status)

Each item starts with a short design review: what librl did, what went wrong or
felt awkward, and the libsk design. Update `tools/parity.map` with the outcome.

- [ ] Small leftovers: sound pan, read back the asset host, animation frame
      count and seeking to a frame, FPS readout with a custom font, a handle for
      the built-in font
- [ ] 2D sprites and screen-space texture drawing (with the 2D/UI roadmap item)
- [ ] 3D text
- [ ] 3D shapes: rectangles and circles (immediate + retained), retained lines
- [ ] 3D line strips: needs a handle-only way to pass points
- [ ] Per-object picking and pickable flags on every pickable object; read back
      a sprite's transform
- [ ] Pick statistics for debugging
- [ ] Lighting controls: on/off, direction, ambient
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

- [ ] Materials & shaders (handle-only uniform/material API)
- [ ] 2D / UI layer: screen space, draw ordering, 2D picking, pickable UI primitives
- [ ] Particle emitters (batched/instanced)
- [ ] Offscreen / render-to-texture (when the first consumer needs it)

## Lessons from librl to design for

- [ ] Networking: one async model and a single `sk_net` module under `sk_asset`
- [ ] Optional subsystems: exclude modules at build time to shrink wasm, with a
      per-example size report
- [x] Scripting and language bindings stay out of the core repo (decided; see ROADMAP)

## Open decisions

- [ ] Handle-only API for point lists (`line_strip_3d`, batch asset ensure)
- [ ] Which language binding comes first
