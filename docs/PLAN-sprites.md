# Plan: a sprite renderer, and particle emitters

Status: steps 1 and 2 built (2026-09-18); steps 3-4 to come.

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

**Open: blended sprites from several textures on WebGL2.** Sorted back to front they
make thousands of tiny batches, and without base-instance draws each one rebinds six
instance attributes, where sokol_gl only switched the texture: 30% slower than before
in Chrome. Atlases and masking avoid it. The fix would be reading instances from a
data texture by index on WebGL2 (bind once, a uniform per batch).

## Not in this plan

- Lit sprites / sprites on materials (TASKS: materials phase 3).
- Texture arrays or bindless textures to batch blended sprites across textures;
  atlases do that today.
- Moving shapes and text off sokol_gl.
