# Plan: Materials and shaders

Status: **phase 1 implemented (2026-09-16)** (built-in materials for models). See
`include/sk_material.h` and `examples/materials.c`. Phases 2 and 3 are not started.
Decisions 1–5 below were accepted as proposed; "Phase 1 as built" records where the
implementation refined the proposal.
Roadmap item 1. Builds on lighting ([PLAN-lighting.md](PLAN-lighting.md)), render
passes, and the Resource/Object model ([ARCHITECTURE.md](ARCHITECTURE.md)).

## Where we are

- **Models:** one built-in lit shader (static + skinned). From glTF materials it
  uses only the base color factor and texture, `alphaMode`/`alphaCutoff` and
  `doubleSided`. Normal, metallic-roughness, occlusion and emissive maps are
  ignored. Lighting is diffuse only.
- **Shapes and sprites:** drawn with sokol_gl's fixed shader, unlit, color x
  texture.
- **Users can't change how anything is shaded:** no material API, no custom
  shaders. Anything beyond tint needs engine changes.

## Goals

- A **Material** resource: shared, reference-counted, created in code or loaded
  from glTF, and assigned to models (later shapes and sprites).
- **Built-in shading models** covering what glTF assets expect, with correct
  lighting from scene lights.
- **Custom shaders** without breaking the public API rules: handles, numbers and
  strings only, and no sokol or backend types.
- One material system for every drawable eventually; models first.

## Non-goals (this plan)

Shadows, image-based lighting / environment maps, post-processing, a node-based
shader editor, runtime shader compilation from GLSL source (sokol-shdc is an
offline tool), compute shaders.

## Proposed design

### Material resource

```c
/* include/sk_material.h */
typedef enum {
    SK_MATERIAL_UNLIT = 0, /* base color x texture x tint; ignores lights */
    SK_MATERIAL_PBR   = 1, /* glTF metallic-roughness, lit by scene lights */
} sk_material_model_t;

sk_handle_t sk_material_create(sk_material_model_t model);  /* or a custom shader, below */
void        sk_material_destroy(sk_handle_t material);

/* Parameters by name. Built-in models define their names (below); custom shaders
 * expose their own uniform names. Unknown names return false. */
bool sk_material_set_float(sk_handle_t material, const char *name, float value);
bool sk_material_set_vec2(sk_handle_t material, const char *name, float x, float y);
bool sk_material_set_vec3(sk_handle_t material, const char *name, float x, float y, float z);
bool sk_material_set_vec4(sk_handle_t material, const char *name, float x, float y, float z, float w);
bool sk_material_set_color(sk_handle_t material, const char *name, sk_handle_t color);
bool sk_material_set_texture(sk_handle_t material, const char *name, sk_handle_t texture);

/* Blending and culling, per material (from glTF when loaded from a mesh). */
bool sk_material_set_alpha_mode(sk_handle_t material, sk_material_alpha_t mode, float cutoff);
bool sk_material_set_double_sided(sk_handle_t material, bool double_sided);
```

Built-in PBR parameter names follow glTF: `base_color` (vec4/color),
`base_color_texture`, `metallic`, `roughness`, `metallic_roughness_texture`,
`normal_texture`, `normal_scale`, `occlusion_texture`, `occlusion_strength`,
`emissive` (vec3), `emissive_texture`.

### Models and materials

- A Mesh loaded from glTF creates one Material per glTF material (shared by the
  primitives that use it). `sk_mesh_get_material(mesh, index)` exposes them.
- `sk_model_set_material(model, primitive_index, material)` overrides a primitive
  on one model (`-1` = all primitives); the mesh's materials are the default.
  Overrides are per model, so two models can share a mesh and look different.
- Tint stays a per-model multiplier on top of the material.

### Custom shaders

```c
sk_handle_t sk_shader_create(const char *path);  /* resource: a compiled shader package */
void        sk_shader_destroy(sk_handle_t shader);
sk_handle_t sk_material_create_custom(sk_handle_t shader);
```

- Authors write annotated GLSL like `src/shaders/sk_model.glsl`, against a small
  documented interface (vertex inputs, a `sk_frame` block with camera matrices and
  time, a `sk_object` block with model matrix and tint, and optionally the lights
  block).
- A build step, `tools/shaderpack` (wrapping `sokol-shdc -f bare_yaml`), compiles it
  for GL, WebGL2 and WebGPU and bundles sources plus reflection into one
  `.skshader` file.
- `sk_shader_create(path)` loads that file like any other asset (so it works with
  `sk_asset_ensure_async`), picks the running backend's source, and uses the
  reflection to map parameter names to uniform offsets and texture slots.
- Handle-only and backend-free: no `sg_shader_desc`, no pointers.

### Pipelines and passes

- Pipelines are cached by (shader, vertex layout static/skinned, blend,
  double-sided). Materials pick the pass as today: opaque/mask first, blend sorted.
- Uniform data is packed per material once when parameters change, not per draw.

### Shading (built-in PBR)

- glTF metallic-roughness BRDF (Lambert diffuse + GGX specular), normal mapping
  (needs vertex tangents: from glTF, or generated at load), occlusion, emissive.
- Uses the existing per-model light selection (8 lights).
- Without environment lighting, metals look dark apart from direct highlights;
  documented, and a reason ambient remains per scene.
- Color space: glTF colors and base color textures are sRGB, so decode to linear
  for lighting and encode at output. Today's shader skips this, so this is also a
  correctness fix; lit results will look different.

## Phasing

1. **Material resource + UNLIT/PBR built-ins for models**: glTF materials become
   Material resources; PBR shading with normal/metallic-roughness/occlusion/emissive
   maps; sRGB-correct; model overrides. Verify against Khronos glTF sample models.
2. **Custom shaders**: shader package tool, `sk_shader_create`, custom materials,
   an example with an animated custom shader, web backends.
3. **Shapes and sprites on materials**: move them off sokol_gl's fixed shader onto
   material-driven pipelines. Ties into the batched renderer and particles.

## Phase 1 as built

- **Overrides are per material slot, not per primitive.** A mesh's material slots
  are its glTF materials (plus glTF's default material when a primitive has none).
  `sk_model_set_material(model, slot, material)` replaces a slot on one model (-1 =
  every slot, 0 = back to the mesh's). Swapping "the body material" is what users
  want; primitive indices are an export detail. Up to 32 slots can be overridden.
  Overrides can be set before the mesh loads and stay when the mesh changes.
- **API names:** `sk_material_shading_t` (`SK_MATERIAL_PBR`, `SK_MATERIAL_UNLIT`)
  instead of "model", to avoid confusion with `sk_model`. Getters for shading, alpha
  mode and double-sided; `sk_mesh_get_material_count/get_material`,
  `sk_model_get_material`. glTF defaults for new materials (metallic 1, roughness 1).
- **Lighting follows glTF exactly**, including the 1/pi in the Lambert term. Light
  intensities that looked right before need about 3x (pi) now; the examples were
  updated. Light and ambient colors are sRGB and converted to linear, like material
  colors. Model tint is sRGB too.
- **Ambient** (no environment lighting yet): ambient x (diffuse color + F0) x
  occlusion, so metals aren't black away from direct highlights.
- **Tangents:** from glTF when present, otherwise generated per vertex from
  positions and texture coordinates (glTF convention: bitangent toward decreasing
  v). Verified against Khronos NormalTangentTest and NormalTangentMirrorTest, with
  and without the file's tangents (generated tangents matched the file's on all
  2770 vertices).
- **Fixed on the way:** normals now use the inverse transpose of the model matrix
  (non-uniform scale was wrong); double-sided back faces are lit from their side.
- **Textures:** glTF images become unnamed texture resources shared between the
  mesh's materials; materials hold references. Picking uses the material the model
  actually draws with (override included), and the base color texture's alpha.
- **Not yet:** texture coordinate set 1, images in external files, environment
  lighting and texture transforms (wanted next), then vertex colors, glTF sampler
  modes (always linear + repeat), mipmaps, tone mapping. Tracked in TASKS.md
  under "Materials: not supported yet".

## Decisions

1. **Built-in shading:** PBR metallic-roughness (glTF-native, what assets expect)
   plus unlit, as proposed. Or a simpler Blinn-Phong first?
2. **Material = resource** (shared, from glTF or code) with **per-model,
   per-primitive overrides**, as proposed?
3. **Custom shaders via a precompiled shader package** (`.skshader` from
   sokol-shdc, loaded by path, parameters by name), as proposed? The alternative,
   runtime GLSL compilation, isn't available cross-backend.
4. **sRGB-correct lighting** now, accepting that lit scenes will look different
   (more correct)?
5. **Phasing:** models first (phases 1–2), shapes/sprites later with the batched
   renderer?

## Verification (per phase)

- Unit tests: parameter packing by name (reflection offsets, types, unknown names),
  material refcounting and per-model overrides, pipeline cache keys, sRGB
  conversions, BRDF helper values against reference numbers.
- Visual: Khronos glTF sample models (e.g. MetalRoughSpheres, NormalTangentTest,
  AlphaBlendModeTest) screenshots on desktop and web, compared with the Khronos
  reference renders.
- `make test`, `make smoke`, `make check`, `make webcheck` (WebGL2 + WebGPU).
