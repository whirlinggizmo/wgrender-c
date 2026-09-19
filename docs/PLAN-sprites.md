# Plan: a sprite renderer, and particle emitters

Status: built (2026-09-18), steps 1-4, and more for particles after (step 5).

## Why

Games built on libsk will put **thousands** of sprites on screen, in 3D (sprite3d:
camera-facing, upright, flat and free) and in 2D (sprite2d, UI), plus particles in
both. Today every sprite is drawn through sokol_gl, one at a time: the CPU works out
its quad (billboard math included), writes 6 vertices, and sokol_gl turns runs of
them into draw calls. That was the right first version; the benchmark says it's now
the limit on phones.

## Baseline (tools/bench/spritebench.c, 2026-09-18)

CPU per frame in ms, out of the 16.7 a 60 fps frame has. Pixel 9 Pro XL (Chrome
engine, WebGL2 / WebGPU) and desktop GL (for scale):

| scene                          | 1,000       | 4,000       | 16,000        | desktop, 16,000 |
|--------------------------------|-------------|-------------|---------------|-----------------|
| field, one atlas               | 1.8 / 3.0   | 5.0 / 6.3   | 9.6 / 9.6     | 2.9             |
| field, 4 textures              | 2.1 / 3.2   | 4.4 / 6.9   | 13.8 / 15.9   | 7.1             |
| 3D particles                   | 0.9 / 1.3   | 3.7 / 4.4   | 11.2 / 11.8   | 3.9             |
| 2D particles                   | 1.1 / 1.6   | 2.1 / 2.8   | 7.4 / 7.9     | 2.1             |

"field": sprite3d on the ground under an orbiting perspective camera, a third each
camera-facing, upright (turning about Y) and flat, blended and sorted every frame.
"particles": each moved with one API call per frame, 1/120 replaced per frame.

Where the time goes:

- **The scene step dominates** (about 80% with an atlas): per sprite, a handle
  lookup, a depth for sorting, the sort, then billboard math and 6 vertices on the
  CPU, recorded into sokol_gl.
- **Separate textures cost draw calls**: sorted back to front, 4 textures interleave,
  so 16,000 sprites become about 12,000 draws, and submit doubles.
- **Particles pay per particle in API calls** before anything is drawn: 3-4 ms for
  16,000 on the phone just moving them.
- WebGPU is no cheaper than WebGL2 on the CPU here.

## Design

### 1. An instanced sprite pipeline

One quad, drawn instanced. Each sprite is one small record in a per-frame instance
buffer, and the vertex shader builds the corners:

    position   vec3     where the pivot goes
    axis_x     vec3     the quad's right edge (world size included)   \  only for FREE and
    axis_y     vec3     the quad's up edge                           /  Y_UP; billboards
    extent     vec2     world width, height                            get theirs from the
    pivot      vec2                                                    camera, in the shader
    uv         vec4     source rectangle, normalized
    tint       ubyte4
    facing     float    camera, upright about Y, flat, free

About 64 bytes a sprite, instead of 6 vertices of 24 bytes plus sokol_gl's
bookkeeping. Billboarding moves to the GPU: the shader has the camera's right, up and
position, so camera-facing and upright sprites cost the CPU nothing to turn.

- A new shader (`src/shaders/sk_sprite.glsl`, sokol-shdc, GL / WebGL2 / WebGPU like
  `sk_model.glsl`) and pipelines per blend mode.
- One instance buffer per frame, appended to as sprites are drawn and grown between
  frames like sokol_gl's budgets. WebGL2 has no base-instance draws, so each batch
  binds the buffer at its offset (supported on every backend).
- **Order is kept**: a batch is a render command (`RENDER_CMD_SPRITES`, a range of
  instances, like `RENDER_CMD_MODELS`), so sprites still interleave correctly with
  sokol_gl layers (shapes, text), model draws and passes. Consecutive sprites with the
  same texture, blend mode and pass share one draw.
- sokol_gl stays for shapes, text and immediate texture drawing.
- Picking, bounds and interaction are unchanged: they already work from the sprite's
  data on the CPU.

### 2. Alpha modes: what to sort

Blending needs back-to-front order, and order is what breaks batching (the 4-texture
field). Most sprites don't need it:

- **blend** (today, the default): sorted with the scene's other transparent parts.
- **cutout**: alpha-tested (discard below a threshold), depth written, **not sorted**.
  Hard-edged sprites (pixel art, foliage, most game sprites) look the same and batch
  by texture: 16,000 sprites from 4 textures become 4 draws.
- **additive**: glows, sparks, most particles. Order-independent, not sorted, no depth
  write.

### 3. sprite2d on the same path

sprite2d draws in call / layer order, never sorted, so it batches runs of the same
texture. Rotation, scale, pivot, tint and the source rectangle map onto the same
record (with a screen-space projection); a nine-slice sprite is 9 instances; a
layer's clip rectangle breaks the batch (it's a scissor change). Blend and additive
apply; cutout means nothing in 2D.

### 4. Particle emitters, 3D and 2D

Particles as sprites is what the particle scenes measure: a handle, an API call and a
sort entry per particle. An **emitter** is one object that owns many particles:

    sk_handle_t sk_emitter3d_create(sk_handle_t texture);   /* object from a resource */
    sk_handle_t sk_emitter2d_create(sk_handle_t texture);
    sk_emitter3d_set_rate(e, particles_per_second);  sk_emitter3d_burst(e, count);
    sk_emitter3d_set_life(e, min, max);  _set_velocity(e, ...);  _set_gravity(e, ...);
    sk_emitter3d_set_size(e, start, end);  _set_color(e, start, end);  _set_source(e, ...);
    sk_emitter3d_set_position / _set_alpha_mode / ...;   sk_emitter3d_destroy(e);

(names illustrative; the real set is part of step 4's design, taking what the
examples need.)

**Recommended: stateless, simulated on the GPU.** A particle is written once, when
it's born (time, position, velocity, a random seed); the vertex shader computes where
it is now (`p0 + v0 t + g t^2 / 2`), its size, color and fade from its age, and drops
it when it's dead. The CPU only spawns; there's no per-particle work per frame at
all. It covers fountains, sparks, smoke, dust, confetti and UI flourishes; it can't do
particles that react to the world after they're born (collisions, attractors), which
would be a later, CPU-simulated mode if a game needs it.

An emitter is a scene member like any object: one sort entry (its position) in 3D,
drawn in layer order in 2D, particles within it unsorted (additive by default; blend
without per-particle sort is the usual trade-off).

## Expected gains

Estimates, to be checked by the benchmark at each step:

- field, one atlas: the scene step loses the CPU billboard and vertex writes and
  sokol_gl recording; left are the lookup, the depth and the sort. Roughly 2-3x
  cheaper; on the phone 4,000 sprites from about 5 ms to about 2.
- field, 4 textures, cutout: about 12,000 draws become 4.
- particles: per-particle CPU cost goes to zero; what's left is spawning.

## Order

1. **The instanced pipeline, with sprite3d on it** (blend only, same look). Gate: the
   field benchmark at least 2x cheaper on the phone, pixel-identical enough in
   webcheck screenshots, `make verify`, webcheck on both backends.
2. **Alpha modes** for sprite3d (cutout, additive). Gate: the 4-texture field in
   cutout draws once per texture.
3. **sprite2d on it**, nine-slice and clipping included.
4. **Emitters**, 3D and 2D, GPU-simulated; an `examples/particles.c`; the benchmark's
   particle scenes rebuilt on them.

Each step is its own commit, measured on desktop, headless Chrome and the phone.

## Decisions

1. **Instanced quads with billboarding in the shader** rather than a faster CPU path
   into sokol_gl. Recommend: yes; the CPU vertex work is the cost.
2. **Alpha modes** as a new sprite3d/sprite2d setting (`blend`, `cutout`, `additive`;
   blend stays the default). This adds public API. Recommend: yes.
3. **Stateless GPU particles** for emitters, with a CPU-simulated mode only if a game
   needs particles that react after birth. Recommend: yes.
4. **Emitter as its own object noun** (`sk_emitter3d_*`, `sk_emitter2d_*`, created
   from a texture handle, like sprites), not a sprite flag. Recommend: yes.
5. **Particles within an emitter aren't sorted** (additive by default). Recommend: yes;
   revisit if a blended effect looks wrong.

## As built

### Step 1: the instanced pipeline, with sprite3d on it (2026-09-18)

- `src/shaders/sk_sprite.glsl` (`@module sprite`) and `src/sk_sprite_batch.c`: one
  76-byte record per sprite (position, facing, size, pivot, source rectangle, axes for
  flat and free sprites, tint), a 6-corner quad drawn instanced. Billboard axes are
  per camera, so they're uniforms, worked out by `sk_sprite3d_facing_basis` like
  picking's; flat and free sprites carry their own. Pipelines match sokol_gl's 3D ones
  (blended, depth-tested, depth writes on for direct draws, off in a scene's sorted
  pass). The frame's records go up in one transient buffer write before the passes.
- Batches are `RENDER_CMD_SPRITES` render commands: order with sokol_gl layers, model
  draws, passes and clips is unchanged. A batch records its camera, pass and scissor;
  consecutive batches only re-apply what differs.
- Looks the same: webcheck screenshots of `2d`, `pick` and `ui` are pixel-identical to
  the sokol_gl path (the rest differ only where they animate), on WebGL2 and WebGPU.

Measuring showed the per-sprite CPU work around the draw mattered as much as the
draw itself, so this step also fixed:

- **Scene membership**: a hash index per scene (handle -> member), with removals left
  as holes that are closed, layer-sorted and re-indexed once before the members are
  walked. Adding, removing, destroying (`sk_scene_forget`) and relayering were linear
  scans, quadratic under churn.
- **The transparent sort**: a stable radix sort on depth (ties keep submission order)
  above 64 parts, instead of `qsort`.
- **Per-sprite state lookups**: the batch's camera, pass and scissor are cached behind
  `sk_render_state_revision` and `sk_camera3d_revision` instead of being re-read and
  compared for every sprite.

CPU ms per frame, before -> after:

| 16,000 sprites          | desktop GL   | Chrome, WebGL2 | Pixel, WebGL2 | Pixel, WebGPU |
|-------------------------|--------------|----------------|---------------|---------------|
| field, one atlas        | 2.89 -> 0.92 | 5.40 -> 1.28   | 9.61 -> 3.58  | 9.61 -> 5.16  |
| particles 3d            | 3.91 -> 0.98 | 6.12 -> 1.44   | 11.15 -> 4.47 | 11.82 -> 5.34 |

**Correction** (found in step 2): the "field, 4 textures" numbers first reported for
this step (desktop 7.07 -> 1.35 and similar) were wrong. They were measured while the
render command list still stopped at 1,024, so most of that scene's ~12,000 batches
were dropped, not drawn, and the benchmark didn't notice (dropped sprites aren't a
sokol_gl error). Drawn in full, blended sprites from 4 interleaved textures cost about
what they did on sokol_gl on desktop GL (7.1 ms), and more on WebGL2 (see step 2).

- **The render command list grows** (up to 1M a frame) instead of stopping at 1,024:
  sprites are commands now, so a frame alternating sprites and sokol_gl shapes adds
  two per switch, where they used to share one sokol_gl stream.

Replaying many sokol_gl layers was quadratic (each `sgl_draw_layer` scanned the
frame's commands for its layer), which predated this step (models split layers too).
libsk's sokol fork now has `sgl_draw_layer_range`, and each layer draws its own
command range: 3,000 sprite/shape switches in one frame replay in 0.6 ms, not 5.4.

On the phone at 4,000 (the "thousands" games will have): the field from 5.0 to 1.9 ms
(WebGL2) and 6.3 to 2.7 (WebGPU). 1,000 sprites from 4 textures stay where they were
(about 2 ms): sorted back to front they make ~780 small batches, and WebGL2 has no
base-instance draws, so each rebinds the instance buffer. Step 2's cutout mode
removes the interleaving.

### Step 2: alpha modes (2026-09-18)

- `sk_alpha_mode_t` in `sk_types.h` (`SK_ALPHA_OPAQUE`, `_MASK`, `_BLEND`, `_ADD`), one
  vocabulary for sprites and materials (it replaces `sk_material_alpha_t`; materials
  refuse `ADD` for now). `sk_sprite3d_set_alpha_mode(sprite, mode, cutoff)`; blend
  stays the default.
- In a scene, opaque and masked sprites draw in the opaque pass and additive ones in a
  new additive pass after the blended parts (`sk_scene_register_additive`). Neither
  is sorted: the batcher groups them by texture and mode (a counting sort over the
  few groups, stable, so overlapping sprites at one depth keep member order), so 400
  masked sprites alternating 4 textures draw in 4 batches (unit test).
- The shader gets the mode per sprite (the up axis's `w`): a mask cutoff discards,
  opaque and masked write alpha 1. Pipelines: opaque (no blending, depth written),
  blended with or without depth writes, added.
- Batches draw from a base instance where the backend can (GL 4.2+, WebGPU, Metal,
  D3D11): the instance buffer stays bound and consecutive batches only change the
  texture. WebGL2 can't, so it rebinds the instances per batch.
- `examples/2d.c` uses it: opaque ground tiles, masked props. Its pixel art has no soft
  edges, so it looks the same (pixel-identical screenshots) and needs no sorting.

CPU ms per frame, 16,000 sprites, 4 textures interleaved (field):

| mode                 | desktop GL          | Chrome, WebGL2      | Pixel, WebGL2 | Pixel, WebGPU |
|----------------------|---------------------|---------------------|---------------|---------------|
| blended, sokol_gl    | 7.1                 | 9.2                 | 13.8          | 15.9          |
| blended, instanced   | 5.7                 | 12.2                | 20.5          | 10.0          |
| masked, instanced    | **1.4** (4 batches) | **1.2** (4 batches) | **3.3**       | **4.6**       |

On the phone at 4,000 sprites, blended from 4 textures is a little cheaper than on
sokol_gl (WebGL2 3.8 vs 4.4 ms); it's at 16,000 that WebGL2's rebinding dominates.
The phone's WebGPU masked number (9.8) was noise. Traced and re-measured, the masked
16,000 scene takes 3.1 ms of scene time on WebGPU (4.6 ms in all), like WebGL2's.
Sampling the phone's clocks during a run showed no thermal throttling but the
governor moving the big cores between 0.7 and 3.1 GHz from second to second, so a
2-second benchmark step can land on a slow stretch: single phone runs vary 2-3x, and
a surprising phone number needs a second run before it means anything.

Additive particles cost the same as blended ones here (their one texture already made
few batches); the gain is that they need no sorting.

**Blended sprites from several textures on WebGL2** (fixed after step 2). Sorted back
to front they make thousands of tiny batches, and WebGL2 has no base-instance draws,
so each batch rebound six instance attributes, where sokol_gl only switched the
texture: slower than before. Now, without base-instance draws (WebGL2, GL before
4.2), the sprites go into an RGBA32F texture, 6 texels a sprite and 256 a row, and a
second vertex shader (`quad_pulled`) reads them by index (first + instance); a batch
sets one small uniform. The frame writes only the rows in use. `-DSK_SPRITES_PULLED`
forces this path on any backend (the unit tests pass on both).

| 16,000, 4 textures, blended | sokol_gl | rebinding | read by index |
|-----------------------------|----------|-----------|---------------|
| Chrome, WebGL2              | 9.2      | 12.2      | **7.1**       |
| Pixel, WebGL2 (two runs)    | 13.8     | 20.5      | **11.0, 11.2**|

The cost: packing and uploading the texels, about 1 ms more at 16,000 on the phone
when batches were few anyway (one atlas: 4.6 -> 5.5-6.3 ms). Desktop GL keeps
base-instance draws (reading by index is no faster there: native GL calls are cheap).

### Step 3: sprite2d on the instanced path (2026-09-18)

- A 2D sprite's quads (one, or up to nine when nine-sliced) go to the batcher as
  instances: position the top-left corner, axes the top and left edges, so rotation,
  pivot, scale, flips and nine-slices are worked out as before and come out the same.
  `sk_sprite_batch_add_2d` records them in order (2D is never regrouped) under a 2D
  projection matching sokol_gl's (`sk_mat4_ortho` over the target's logical pixels),
  with 2D pipelines (no depth test): blended, added, opaque/masked.
- `sk_sprite2d_set_alpha_mode` / `get_alpha_mode`, like sprite3d's; blend the default.
- The immediate `sk_texture_draw*` calls stay on sokol_gl (UI draws few, interleaved
  with shapes and text).
- Screenshots of `sprite2d`, `touch`, `ui` and `clay` match the sokol_gl build (the
  differences are the page's dropdown and animation). Unit test: a run of one texture
  (a nine-slice included) is one batch; alternating textures stay in order.

2D particles, 16,000, CPU ms: desktop GL 1.43 -> 1.07, Chrome 2.39 -> 1.63, Pixel
WebGL2 5.55 -> 3.89.

### Step 4: particle emitters (2026-09-18)

- `sk_emitter3d_*` and `sk_emitter2d_*` (include/sk_emitter3d.h, sk_emitter2d.h):
  objects created from a texture. Emission: `set_rate`, `burst`, `set_emitting`,
  `set_max` (default 1024, at most 65,536; new particles replace the oldest),
  `set_life(min, max)`. Birth: `set_spawn_box`, `set_velocity(dir, spread,
  speed_variance)` (a cone in 3D, ± an angle in 2D), `set_gravity`. Over a life:
  `set_size(start, end, variance)`, `set_color(start, end)` (alpha included: fades),
  `set_spin(min, max)`. Plus `set_source` (an atlas cell), `set_alpha_mode` (additive
  by default), `set_seed`, `get_count`, `clear`, `set_visible`, `draw`. Configured in
  code; editor formats are for later (see below).
- Stateless on the GPU: a particle is a 48-byte record written once, at birth (where
  and when, velocity and life, size scale, spin and starting angle). `vs_particle`
  (src/shaders/sk_sprite.glsl) works out its position (`p0 + v t + g t² / 2`), size,
  color and angle from its age, and moves dead or unborn ones off screen. The CPU only
  spawns (spread evenly through the frame, so a steady rate doesn't clump) and drops
  the dead from the front of each emitter's ring.
- libsk advances every emitter once a frame, after the ticks and before the frame
  callback. The emitter's clock is rebased every 4,096 s to keep the shader's float time
  precise.
- Drawing: one instanced draw per emitter. Its live particles are copied into the
  frame's particle buffer (uploaded once, before the passes) and drawn with the
  emitter's uniforms: the camera's right and up in 3D, the 2D projection in 2D. Emitters
  go through render commands like sprite batches, so they keep their place among sokol_gl
  drawing and their scissor.
- In scenes: a 3D emitter is one member. Opaque and masked emitters draw in the
  opaque pass; blended ones are sorted as a whole with the other transparent members;
  additive ones draw with the additive sprites. A 2D emitter draws in layer and
  member order and isn't pickable. Particles within an emitter aren't sorted.
- `examples/particles.c`: a fountain (blended), sparks from a moving source
  (additive, trailing), smoke (blended, growing, turning) and 2D confetti bursts where
  you click or tap (squares cut from the particle texture with `set_source`, so their
  spin shows). Unit tests (tests/unit/emitter_test.c) with fixed seeds: rates, bursts,
  the ring's max, retiring the dead, the clock rebase, birth records, and scene
  membership.

spritebench's particle scenes, the same particles from one emitter (the sprite
versions stay as the baseline), 16,000 alive, CPU ms per frame:

| scene             | desktop GL   | Chrome (WebGL2) | Pixel (WebGL2) |
|-------------------|--------------|-----------------|----------------|
| 3D, blended       | 1.13 -> 0.12 | 2.19 -> 0.30    | 6.20 -> 0.63   |
| 3D, additive      | 1.00 -> 0.11 | 1.93 -> 0.27    | 5.61 -> 0.59   |
| 2D                | 1.08 -> 0.10 | 1.83 -> 0.27    | 3.98 -> 0.76   |

Desktop frame time (vsync off) 1.25 -> 0.26 ms for 3D. What's left is copying and
uploading each emitter's live particles every frame (48 bytes each); spawning happens
in the runtime before the frame callback, outside these CPU columns but inside the
frame time. The example runs at 60 FPS on the phone. If uploads show up, an emitter's
ring could live in a GPU buffer and upload only its new particles.

### Step 5: more for particles (2026-09-18)

Still stateless: everything is decided at birth or worked out from a particle's age
in `vs_particle`; the record stays 48 bytes (its spare float now a random 0..1).

- Motion:
  - `set_drag`: slowing in proportion to speed, with gravity, in closed form: towards
    gravity / drag.
  - `set_stretch(seconds)`: the quad's top follows the velocity across the screen, as
    long as the distance moved in that time, trailing behind.
  - `set_inherit_velocity(fraction)`: a share of the emitter's own movement at birth,
    measured over the frame the game moved it in (the previous update's time).
  - A moving emitter's steady spawns are spread along the way it moved, so a fast
    source leaves a smooth trail. The first position isn't a move; `jump` moves without
    a trail.
- Over life:
  - Curves: up to 8 size keys and 8 color keys (`add_size_key`, `add_color_key`, and
    `clear_*`), linear between keys, held outside them, a step where two share a time.
    `set_size` and `set_color` make the two-key curve they always did.
  - Palette: up to 8 colors; each particle picks one at birth, tinting its color.
- Look and start:
  - `set_frames(columns, rows, count, per_second)`: a flipbook within the source,
    played once over the life (0) or looped from a random frame.
  - `prewarm(seconds)`: start over as if the rate had been running that long.
  - `set_spawn_sphere` (2D: `set_spawn_circle`): evenly within a radius, instead of the
    box.
- `examples/particles.c`: sparks with drag, stretch and inherited velocity; a campfire
  (flipbook flames colored by a curve, under smoke with size and color curves); the
  steady emitters prewarmed; confetti from one emitter with a palette.
  `tools/gen_particles.py` makes the particle textures (the dot, and the flame's 4x4
  flipbook).
- Cost: the same CPU as step 4 on the phone (16,000 particles: 0.61-0.79 ms, within
  noise). The example (about 3,000 particles, plus 300 confetti) holds 60 FPS there.

## Not in this plan

- Lit sprites / sprites on materials (TASKS: materials phase 3).
- Texture arrays or bindless textures to batch blended sprites across textures;
  atlases do that today.
- Moving shapes and text off sokol_gl.
- Particles: particles that react after birth (collisions, attractors: a CPU-simulated
  mode), sorting within an emitter, and effects saved to files as resources (a format
  of our own, later ones made in editors, e.g. Cocos/particle-designer plists,
  Effekseer).
