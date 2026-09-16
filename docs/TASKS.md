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
- [ ] First language binding (decide which one)

## Roadmap features

- [ ] Materials & shaders (handle-only uniform/material API)
- [ ] 2D / UI layer: screen space, draw ordering, 2D picking, pickable UI primitives
- [ ] Particle emitters (batched/instanced)
- [ ] Offscreen / render-to-texture (when the first consumer needs it)

## Open decisions

- [ ] Handle-only API for point lists (`line_strip_3d`, batch asset ensure)
- [ ] Which language binding comes first
