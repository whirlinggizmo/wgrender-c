# Plan: Shadows

Status: **phases 1 and 2 implemented (2026-09-21)** on desktop GL, WebGL2 and WebGPU:
one directional light, then spot lights and up to four casting at once.
Decisions 1–7 were answered as recommended, with one addition during implementation:
a shadow's strength and tint. See "Phase 1 as built", and "The WebGPU bug" for the one
that took longest. Roadmap: the largest remaining gap against three.js, which ships
shadow maps. Builds on lighting ([PLAN-lighting.md](PLAN-lighting.md)), materials
([PLAN-materials.md](PLAN-materials.md)) and render targets
([PLAN-render-target.md](PLAN-render-target.md)).

## Why

Without shadows, lit scenes read as objects floating in space: nothing tells the eye
where a model meets the ground. Every screenshot in this repo shows it. Shadows are
also what makes a directional light look like a sun rather than a flat wash.

## Where we are

- Lights are scene objects (directional, point, spot), resolved per scene draw into a
  `sk_light_env_t` and selected per model (8 at a time, by contribution).
- Model shading lives in `src/shaders/sk_pbr.glsl`, shared by models and lit sprites;
  custom shaders get the same lights through `sk_frame` (`shaders/sk.glsl`).
- Render targets exist (`sk_texture_create_target`), the frame runs target passes
  before the screen pass, and screen effects already add their own passes after it
  (`src/sk_effect.c`) — so "more passes before the screen" is a solved shape.
- Nothing writes or reads a depth map; no light has any notion of casting.

## Proposed design

### API

```c
/* include/sk_light.h */
/* Cast shadows from this light (off by default: a shadow map costs a pass and
 * memory). Directional lights first; spot and point come later. */
bool sk_light_set_casts_shadows(sk_handle_t light, bool casts);
bool sk_light_get_casts_shadows(sk_handle_t light);

/* Depth offsets that stop a surface shadowing itself, in shadow-map depth units:
 * a constant, and one scaled by how steeply the surface faces the light.
 * Defaults suit a scene a few tens of units across. */
bool sk_light_set_shadow_bias(sk_handle_t light, float constant, float slope);

/* Pixels each way of this light's shadow map (rounded to a power of two, 256 to
 * 4096; default 2048). Bigger is sharper and slower. */
bool sk_light_set_shadow_map_size(sk_handle_t light, int size);

/* How far from the camera the light's shadows reach (world units; default 50).
 * The map covers that much, so a smaller distance is a sharper shadow. */
bool sk_light_set_shadow_distance(sk_handle_t light, float distance);

/* include/sk_model.h */
/* Whether this model is drawn into shadow maps (default: yes) and whether shadows
 * darken it (default: yes). A character casts; a ground plane usually only
 * receives; a glow or a skybox does neither. */
bool sk_model_set_casts_shadow(sk_handle_t model, bool casts);
bool sk_model_set_receives_shadow(sk_handle_t model, bool receives);
```

Nothing else changes: a program that enables shadows on its sun gets them everywhere
lit shading runs — models, lit 3D sprites, and custom shaders that call the new
`sk_shadow()` helper.

### Frame shape

A new optional module, `src/sk_shadow.c`, registers a render hook that runs before
the frame's other passes:

1. While the frame is recorded, each scene draw already pushes a `sk_light_env_t` and
   queues model items against it. The shadow module notes which envs have a casting
   light.
2. Before the target and screen passes, for each casting light: fit the light's
   projection, open a pass into that light's depth map, and replay the env's model
   items with a depth-only pipeline (no fragment work beyond alpha cutout).
3. The lit shaders then sample the map. `sk_frame` gains the light's view-projection
   matrix and its parameters, so models, sprites and custom shaders read it the same
   way (`.skshader` format 6 — custom shaders need rebuilding).

The core stays as it is: this is another `sk_render_hooks` entry, like screen effects,
so a program with no shadows doesn't link the module (`make check`).

### Fitting

A directional light has no position, so its map covers a box around what the camera
can see, out to the light's shadow distance: fit an orthographic frustum to that
slice of the view frustum, expanded to include casters behind it (so an object off
screen still casts into view). Snap the fit to whole texels so the shadow doesn't
crawl when the camera moves.

### Sampling

The shader transforms the surface point into the light's clip space, compares its
depth to the map, and averages a small kernel (3x3 by default) for a soft edge.

Two constraints found while planning:

- **Sampler slots are full.** libsk owns sampler slots 8–11 (the environment cube,
  the BRDF table, a sprite's texture, sprite data / joints), and 12 is sokol's limit.
  The environment cube and the BRDF table are both sampled linear-clamp, so they can
  share one slot, freeing one for the shadow map.
- **Texture slots**: the map goes at 13, alongside the joints at 12.

### Custom shaders

`shaders/sk.glsl` gains, beside `sk_light()` and the environment helpers:

```glsl
float sk_shadow(int light, vec3 world_pos, vec3 normal);  /* 1 lit, 0 fully shadowed */
```

so a custom shader lights a surface the way built-in materials do, shadows included.

## Phasing

1. **One directional light**: casting flag, depth pass, fitted orthographic map,
   PCF, models cast and receive, lit sprites receive, `sk_shadow()` for custom
   shaders, an example. Everything above.
2. **Spot lights** (a perspective map, the same machinery) and **several casting
   lights** at once (a map each, capped). Done; see "Phase 2 as built".
3. **Cascades** for large outdoor scenes, **point lights** (six faces or none), and
   **sprite casters** (alpha-tested quads in the depth pass). Also worth doing then:
   skip a light's pass when nothing it can see has moved.

## Decisions

1. **Opt in per light** (`sk_light_set_casts_shadows`, off by default), rather than
   shadows on for every light automatically. Recommend: opt in — a shadow map is a
   pass and 16 MB at the default size, and most lights in a scene shouldn't pay it.
2. **Phase 1 is one casting light** (the first casting directional light in the
   scene; later ones warn once and are ignored), rather than N maps from the start.
   Recommend: one — it's the common case (a sun), and it keeps the fit, the uniforms
   and the slot pressure simple until the shape is proven.
3. **Depth texture or color map.** Sample a real depth texture (with a comparison
   sampler), or render depth into an R32F color target and compare by hand?
   Recommend: depth texture — sokol supports `SG_IMAGESAMPLETYPE_DEPTH` with
   `SG_SAMPLERTYPE_COMPARISON` on all three backends, it halves the memory, and
   hardware comparison gives smoother PCF. Fall back to R32F only if a backend
   refuses it.
4. **Per-model receive flag** (`sk_model_set_receives_shadow`) as well as the cast
   flag. Recommend: yes — it costs one bit and one branch, and it's how you keep a
   skybox, a glowing object or a UI-ish model out of the shading.
5. **The map size lives on the light** (`sk_light_set_shadow_map_size`) rather than a
   global quality setting. Recommend: on the light — a flashlight and a sun want very
   different sizes, and a global knob can come later as a multiplier.
6. **Shadow distance on the light** (`sk_light_set_shadow_distance`, default 50)
   rather than fitting the whole scene's bounds automatically. Recommend: on the
   light — automatic fitting over a big scene gives uselessly blurry shadows, and
   this is the one number that trades sharpness for range. Cascades (phase 3) remove
   the trade.
7. **`.skshader` format 6**: `sk_frame` grows the shadow matrix and parameters, so
   custom shaders must be rebuilt (as they were for formats 4 and 5). Recommend: yes,
   and keep refusing older files rather than carrying two layouts.

## Phase 1 as built

- **Opt in twice over.** `src/sk_shadow.c` is an optional module, so a program links
  the depth pass only if it calls one of the shadow functions — which is why they live
  there rather than beside the other light setters. (They started in `sk_light.c`; the
  linker dropped the whole module and the first render came out with no shadows at
  all.) At runtime a light casts only when asked, and a model says whether it casts
  and whether it receives.
- **One casting light**, the first the scene finds (`sk_light_env_t.shadow_light`),
  and only a directional one: spot and point lights refuse with a warning.
- **The fit** covers the slice of the camera's view out to the light's shadow
  distance, as a square so its texel size doesn't change as the camera turns, snapped
  to whole texels so shadows don't crawl, with the near plane pulled back 50 units so
  casters behind the camera still cast. `sk_shadow_fit_directional` is pure and
  tested.
- **The bias is measured in shadow texels**, not in depth units. It started in depth
  units, which read as "0.0015" but meant 20 cm along the light over the map's 155-unit
  range: shadows lifted off their casters' feet and thin parts (a hat brim) vanished.
  Texels hold at any map size or distance.
- **Most of the acne is dealt with by offsetting the lookup along the surface normal**
  (about a texel, more on surfaces facing the light edge-on) rather than by pushing
  the depth back, which is what lifts shadows off their feet.
- **A shadow fades out over the last tenth of the map** instead of being cut through
  where the light's coverage ends.
- **Strength and tint** (`sk_light_set_shadow_strength`, `_set_shadow_color`, added
  during implementation): how much of the light a shadow takes away, and a colour
  mixed into what it leaves. Shadows are really coloured by the ambient and
  environment light that still reaches them; the tint is the stylised knob.
- **The sampling runs for every pixel of a draw**, with the "outside the map" cases
  folded in as weights rather than early returns: a texture comparison in branchy code
  is undefined where the GPU needs neighbouring pixels to filter.
- **Sampler slots were full** (libsk owns 8–11, sokol allows 12), so the environment
  cube and the BRDF table now share one sampler — both are linear and clamped.
- `.skshader` format 6: `sk_frame` carries the light's matrix and parameters, and
  `sk_shadow(i, pos, n)` gives custom shaders the same answer built-in materials use.

## Phase 2 as built

- **One depth array, a layer per casting light** (`SK_MAX_SHADOW_LIGHTS` = 4), rather
  than a texture each. Custom shaders have almost no sampler slots left (libsk owns 8
  and 9 of the twelve), and an array costs one slot however many lights cast. Each
  layer gets its own attachment view (`sg_view_desc.depth_stencil_attachment.slice`)
  and its own pass.
- **Layers share one size**, which is what an array is: the largest `shadow_map_size`
  any casting light asked for wins, and `sk_light_set_shadow_map_size` says so. Keep
  them equal unless you mean it — a 2048 sun drags a torch's layer up with it. The
  alternative, an atlas with per-light tiles, buys memory back at the cost of uv rects
  and PCF that must not bleed across tile borders; not worth it yet.
- **A light's slot rides in `u_light_spot[i].z`** (-1: it casts nothing), so the
  per-light arrays don't grow at all. Everything else is per slot: the matrix, the
  texel sizes, the bias, the tint and the strength — about 460 bytes added to the
  frame block. `.skshader` format 7.
- **Spot lights fit their own cone**: a perspective frustum from the light, with the
  field of view taken from the outer cone angle plus a tenth so the edge isn't on the
  last texel, reaching the nearer of the light's range and its shadow distance. The
  near plane is a hundredth of that reach. `sk_shadow_fit_spot` is pure and tested,
  like the directional fit.
- **Point lights still refuse**, and say why: they want six maps, one each way.
- The scene hands out slots in the order it finds casting lights; past four, a light
  lights the scene without shadowing it.

## The WebGPU bug

On WebGPU every surface came back fully shadowed while the same code was right on
desktop GL and WebGL2. It looked like a WebGPU problem; it wasn't. The depth pass
never said its depth buffer should be kept, and sokol's default store action for depth
is `DONTCARE` (`sokol_gfx.h`, `_sg_resolve_default_pass_action`): the sensible default
for a depth buffer that only orders a pass's own draws, and exactly wrong for a shadow
map, which *is* the depth buffer. WebGPU took the discard at its word and the map read
as zero everywhere (zero is "in front of everything", so everything was in shadow);
GL treats the discard as a hint and happened to keep the data, which is why it passed
there — a latent bug on GL too, on any driver that honours the hint. The fix is one
explicit `store_action = SG_STOREACTION_STORE`.

What found it, in order, for next time: `webcheck` gained `--verbose` and the
browser's own log entries (`Log.entryAdded`, where Dawn's validation messages go;
`Runtime.consoleAPICalled` never sees them), which showed the pass was valid; then a
fixed comparison reference (0.001) with the sampler's function flipped to
`GREATER_EQUAL` lit everything, proving the sampler worked and the map held zeros; and
only then was it worth reading how the pass's depth was stored. The things ruled out
first — the clip-space depth range, the map's orientation, branchy sampling, an R32F
colour map instead of a depth texture — all cost cycles because each was a guess made
before there was a way to see what the map held.

## Verification

- Unit tests: the light's new state and its defaults; the fit (pure math: a frustum
  slice in, an orthographic matrix out, texel snapping) against known values; a
  casting light adding exactly one depth pass to the frame, and none when nothing
  casts; per-model cast/receive flags reaching the draw.
- Visual: a scene with a ground plane and a few models, shadows on and off,
  screenshots on desktop GL, WebGL2 and WebGPU (`examples/shadows.c`); the existing
  `lights`, `shaders` and `postprocess` examples still look right.
- `make verify`, `make webcheck` (both backends), `make test SANITIZE=address`,
  `make windows-test` / `windows-smoke` under Wine.
- Cost, measured with `make shadowbench DESKTOP=1` (tools/bench/shadowbench.c) on an
  RTX 4080 laptop GPU, vsync off — the same scene of generated shapes with a floor,
  half of them turning, the camera moving so the fit re-snaps. Frame ms, and the CPU
  ms inside it (one run; runs vary by about 0.05 ms):

  | case             |  100 | cpu  |  400 | cpu  | 1000 | cpu  |
  |------------------|-----:|-----:|-----:|-----:|-----:|-----:|
  | no shadows       | 0.25 | 0.15 | 0.62 | 0.47 | 1.41 | 1.20 |
  | sun, 1024        | 0.42 | 0.26 | 0.82 | 0.62 | 1.94 | 1.60 |
  | sun, 2048        | 0.33 | 0.18 | 0.90 | 0.66 | 1.95 | 1.63 |
  | sun, 4096        | 0.38 | 0.18 | 1.03 | 0.65 | 2.06 | 1.60 |
  | sun + spot, 1024 | 0.40 | 0.23 | 1.11 | 0.88 | 2.21 | 2.04 |
  | sun, no receive  | 0.30 | 0.18 | 0.84 | 0.66 | 1.91 | 1.62 |

  Read the two columns against each other. **The CPU number barely moves across 1024,
  2048 and 4096** — the pass draws the same casters whatever the map's size — so what
  separates those rows is the map's fill, and it is real: sixteen times the pixels
  costs about +0.05 ms at 100 models and +0.12 at 400. What separates "no shadows" from
  "sun, 1024" is the other thing, submitting every caster a second time: +0.15 ms of
  CPU at 400 models, +0.40 at 1000. A second casting light adds another pass and
  roughly repeats it. Receiving is the cheapest part: turning it off saves ~0.02.

  So a casting light costs one pass over its casters plus its map's fill, and which
  dominates is a property of the scene rather than of shadows. Both point at the same
  phase 3 work — skip a light's pass when nothing it sees has moved — and say cascades
  should be budgeted as passes *and* pixels.

  The model counts stop at 1000 because libsk drops model placements past 1024 a frame
  (`MAX_MODEL_DRAWS`): asking for more measures the same 1024. Lighting alone is about
  1.4 microseconds a mesh here, so a scene that could draw 4096 would spend roughly
  5-6 ms before shadows.

- Checked on desktop GL, WebGL2 and WebGPU: every caster throws a shadow, the no-cast
  sphere throws none, the no-receive sphere stays lit, shadows touch their casters.
