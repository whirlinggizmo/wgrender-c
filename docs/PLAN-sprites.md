# Plan: a sprite renderer, and particle emitters

Status: proposed (2026-09-18). Nothing built yet.

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

## Not in this plan

- Lit sprites / sprites on materials (TASKS: materials phase 3).
- Texture arrays or bindless textures to batch blended sprites across textures;
  atlases do that today.
- Moving shapes and text off sokol_gl.
