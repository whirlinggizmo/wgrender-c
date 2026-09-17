# Plan: Environment lighting (image-based lighting) and tone mapping

Status: **implemented (2026-09-16).** Decisions 1–5 accepted as recommended; see
"As built" for details and deviations. `include/sk_environment.h`,
`sk_scene_set_environment/background/tonemap`, `examples/environment.c`.

## Why

Materials follow glTF metallic-roughness (docs/PLAN-materials.md), but lighting
comes only from punctual lights plus a flat ambient term. glTF assets are authored
for, and previewed under, an environment (Blender, the Khronos sample viewer,
three.js, Godot). Without one:

- **Metals look dark.** A metal has no diffuse color; it only reflects. With
  nothing around it to reflect, it's black except for small highlights.
- **Rough and smooth look alike away from highlights.** Roughness shows mostly in
  how blurry reflections are.
- **Lit values clip.** Bright lights and HDR environments exceed 1.0, and today the
  shader just clamps, so highlights flatten to white and colors shift.

## Proposed design

### Environment resource

```c
/* include/sk_environment.h */
/* An environment map loaded from an equirectangular (latitude-longitude) image:
 * Radiance .hdr (recommended, true HDR) or PNG/JPEG (sRGB, low dynamic range).
 * Creation prepares the lighting data on the CPU (see below). */
sk_handle_t sk_environment_create(const char *path);
void        sk_environment_release(sk_handle_t environment);

/* include/sk_scene.h */
/* Light the scene's models with an environment. intensity scales it (1 = as
 * authored); rotation (radians) turns it around the world up (+y) axis. 0 = none. */
bool sk_scene_set_environment(sk_handle_t scene, sk_handle_t environment, float intensity, float rotation);
/* Draw the environment behind everything as the scene's background (skybox).
 * blur 0..1 shows it sharp to fully blurred (useful behind focused subjects). */
bool sk_scene_set_background(sk_handle_t scene, sk_handle_t environment, float blur);
```

- A resource like Texture or Mesh: from a path, deduped, reference counted; scenes
  hold references. Handle kind ENVIRONMENT.
- Works with the asset layer as today: ensure the file, then create in the callback.
- The flat ambient term stays and adds on top (it's also the cheap option when a
  game doesn't want an environment).

### What gets computed (at create, on the CPU)

Standard "split-sum" image-based lighting, as in the glTF sample viewer, Filament
and Unreal:

1. **Diffuse:** the environment's irradiance as 9 spherical-harmonic coefficients
   (a few numbers, sent as uniforms). Accurate for diffuse lighting and tiny.
2. **Specular:** a prefiltered cubemap: face size 128 with a mip chain, where mip 0
   is the sharp reflection and each further mip is the environment blurred for a
   higher roughness (GGX importance sampling, filtered from the source's mips to
   avoid noise). The shader picks the mip from the material's roughness.
3. **BRDF term:** a small 2D lookup table (64x64) computed once at startup, per
   the split-sum approximation. Shared by all environments.

Formats: RGBA16F (half float) cubemaps where the backend can filter them (desktop
GL, WebGL2, WebGPU all can; checked at runtime), otherwise RGBA8 with RGBM encoding.

Cost: a 2K .hdr decodes in roughly 50–100 ms and prefilters in a few hundred ms
single-threaded (to be measured). That's a load-time cost, like large textures
today; the loading-pipeline roadmap item would move it to a worker.

### Shading

- Model shader, PBR materials: add environment diffuse (SH irradiance × diffuse
  color) and specular (prefiltered radiance × (F0 × A + B) from the LUT), both
  scaled by intensity and multiplied by the occlusion map. Specular also uses a
  simple horizon/occlusion term so crevices don't glow.
- Unlit materials ignore it, like lights. Models outside a scene (sk_model_draw)
  stay unlit.
- Background: a full-screen pass drawn before a scene's 3D layers, sampling the
  cubemap by view direction at a mip chosen from `blur`.

### Tone mapping and exposure

```c
typedef enum {
    SK_TONEMAP_NONE = 0,   /* clamp (today's behavior) */
    SK_TONEMAP_NEUTRAL,    /* Khronos PBR Neutral: colors unchanged until highlights roll off */
    SK_TONEMAP_ACES,       /* filmic, more contrast and hue shift */
} sk_tonemap_t;

bool sk_scene_set_tonemap(sk_handle_t scene, sk_tonemap_t tonemap, float exposure); /* exposure in stops (EV) */
```

- Applied in the model shader (and the background), before the sRGB encode. There
  is no HDR framebuffer, so sprites, shapes and text are unaffected: they're
  display colors, not lighting.
- Per scene, like ambient and the environment.

### Example and assets

- `examples/environment.c`: the material spheres (plastic, gold, roughness steps)
  and the gumshoe under an environment with a visible background; keys to switch
  environment, blur, tone mapping and exposure, and to toggle the environment off
  for comparison.
- Assets: one small CC0 HDR from Poly Haven at 1K resolution (about 1–2 MB) in
  `examples/assets/environments/`, with its source and license noted.

## Decisions

1. **Environment is a resource from an equirectangular image** (`.hdr`, or
   PNG/JPEG), set per scene with intensity and rotation. Recommend: yes. Cubemap
   face images (6 files) and KTX2 prefiltered cubemaps can come later.
2. **Prefilter on the CPU at create** (SH irradiance + GGX mip chain), versus on the
   GPU with render targets. Recommend CPU: identical results on every backend, no
   float render targets needed (WebGL2 needs an extension for those), and easy to
   unit test. Revisit if load times hurt.
3. **Tone mapping default: Khronos PBR Neutral** for scenes, versus NONE (keeps
   today's clamping). Neutral leaves colors in the normal range untouched and only
   rolls off highlights, which is what glTF viewers use by default; it changes how
   bright highlights look in existing scenes. Recommend: Neutral by default.
4. **Background (skybox) included now**, drawn from the same environment with a
   blur setting. Recommend: yes; without it, reflections look disconnected from
   the scene.
5. **Ship a CC0 Poly Haven HDR** (~1–2 MB) as an example asset, versus a small
   procedurally generated sky. Recommend: Poly Haven, since realistic reflections
   are the point; a generated sky can be a second, tiny option.

## As built

- **Timing:** `sk_environment_create` takes ~330 ms for a 1K `.hdr` (1024x512) on a
  desktop CPU, single-threaded: decode, SH projection, a source cubemap, and the GGX
  prefilter (96 samples per texel, filtered importance sampling). The loading
  pipeline roadmap item would move it off the main thread.
- **Background at the image's own resolution:** the background samples the source
  cubemap (face size = image width / 4, rounded down to a power of two: 256 for 1K)
  with box-filtered mips for blur, rather than the 128-pixel lighting cubemap, which
  was visibly soft. A 1K HDR still looks soft full screen; use 2K/4K for sharper
  backgrounds.
- **No RGBM fallback:** every target backend (desktop GL, WebGL2, WebGPU, and the
  dummy backend) filters RGBA16F, so a backend that can't disables environments with
  a warning instead. Tracked in TASKS.md.
- **Orientation verified:** the background rendered on WebGL2 and WebGPU from three
  camera directions matches a CPU reference made directly from the equirectangular
  image (no mirrored or flipped cube faces). MetalRoughSpheres under the studio HDR
  looks as expected (smooth metals mirror-like, rough ones blurred, non-metals keep
  their color).
- **Bug found by tests:** the half-float conversion rounded with the wrong bit
  (off by one step for some values); fixed and checked against Python's IEEE half.
- **BRDF table checks:** exact Schlick split for smooth surfaces, A + B ≤ 1,
  decreasing with roughness for n·v ≥ 0.25 (the table has its known bump at grazing
  angles), and (0.72, 0.02) at n·v 0.5, roughness 0.5 like Unreal's table.
- **No horizon occlusion term yet:** environment specular is scaled by the
  occlusion map only; the planned horizon term (so reflections don't show through
  the surface at grazing normal-mapped angles) is left for later.
- **Tone mapping:** new scenes default to Khronos PBR Neutral at 0 EV; models outside
  a scene and sprites/shapes/text are unchanged.
- **webcheck fixes found along the way:** requests to the page now time out (a page
  that never started could hang a headed run indefinitely), and the loading check no
  longer calls into the page before libsk has started (that aborted pages at random,
  about one run in two).

## Verification

- Unit tests (pure CPU parts):
  - equirectangular ↔ direction and cubemap face ↔ direction mappings agree
  - SH projection: a constant environment gives constant irradiance (π × radiance);
    a known directional pattern gives the expected coefficients
  - prefiltered mips conserve energy (average radiance stays within tolerance) and
    mip 0 reproduces the source
  - BRDF LUT against reference values (e.g. Karis's published curve)
  - half-float and RGBM encoding round trips; .hdr decode dimensions and values
- Visual: Khronos MetalRoughSpheres, EnvironmentTest and a glTF with metals, under
  the same HDR, compared side by side with the Khronos glTF Sample Viewer (same
  environment, Neutral tone mapping) on desktop GL, WebGL2 and WebGPU.
- Timing of create for the example HDR; `make verify`, `make webcheck` (both
  backends).
