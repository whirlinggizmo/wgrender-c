# Plan: Materials and shaders

Status: **phase 1 implemented (2026-09-16)** (built-in materials for models; see
`include/sk_material.h` and `examples/materials.c`) and **phase 2 (2026-09-20)**
(custom shaders; see "Phase 2 as built", `include/sk_shader.h`, `shaders/sk.glsl` and
`examples/shaders.c`). Phase 3 is not started.
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
void        sk_material_release(sk_handle_t material);

/* Parameters by name. Built-in models define their names (below); custom shaders
 * expose their own uniform names. Unknown names return false. */
bool sk_material_set_float(sk_handle_t material, const char *name, float value);
bool sk_material_set_vec2(sk_handle_t material, const char *name, float x, float y);
bool sk_material_set_vec3(sk_handle_t material, const char *name, float x, float y, float z);
bool sk_material_set_vec4(sk_handle_t material, const char *name, float x, float y, float z, float w);
bool sk_material_set_color(sk_handle_t material, const char *name, sk_handle_t color);
bool sk_material_set_texture(sk_handle_t material, const char *name, sk_handle_t texture);

/* Blending and culling, per material (from glTF when loaded from a mesh). */
bool sk_material_set_alpha_mode(sk_handle_t material, sk_alpha_mode_t mode, float cutoff);
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
- **glTF coverage (follow-up the same day):** texture coordinate set 1, texture
  transforms (KHR_texture_transform, also settable by name), vertex colors,
  per-texture samplers (wrap, filter), mipmaps for all textures, and `.gltf` files
  with separate buffers/images or `data:` URIs. The asset layer ensures a glTF
  file's referenced files through a per-extension dependency lister (registered by
  sk_model), so the public ensure-then-create flow is unchanged and works on web.
  Verified against the Khronos TextureTransformTest, MultiUVTest, VertexColorTest,
  TextureSettingsTest and BoxTextured models.
- **Missing images:** a glTF image that is missing (optional dependency), broken or
  in an unsupported format doesn't fail the model. Base color and emissive slots get
  the placeholder texture (`sk_texture_get/set_placeholder`, default a magenta
  checker); normal, metallic-roughness and occlusion slots stay empty so lighting
  isn't distorted. Missing buffers still fail the ensure.
- **Not yet:** environment lighting (wanted next), tone mapping, and others tracked
  in TASKS.md under "Materials: glTF coverage".

## Phase 2 as built

- **Fragment shaders plus an optional vertex hook**, not whole vertex shaders.
  libsk keeps its vertex shaders (static and skinned), so custom shaders work on
  animated models without writing skinning. The hook,
  `void sk_vertex(inout vec3 position, inout vec3 normal)`, moves vertices in object
  space before skinning (waves, wind) and has its own parameters.
- **The interface is one file, `shaders/sk.glsl`.** A fragment shader includes
  `sk_surface`: world position, normal, tangent, both texture coordinate sets and
  vertex color; `sk_time()`, `sk_camera_position()`, `sk_ambient()`, the scene's
  lights (`sk_light_count()`, `sk_light(i, pos, out to_light)`, with the same falloff
  as built-in materials), sRGB helpers, and `sk_output(color, alpha)`, which applies
  the model's tint, the MASK cutoff, exposure and tone mapping and encodes sRGB, so a
  custom material fades, masks and tone-maps like a built-in one. The scene's
  environment (2026-09-20): `sk_environment_diffuse(n)`, `sk_environment_specular(n, v,
  roughness)` and `sk_environment_brdf(n_dot_v, roughness)`, the same split-sum pieces
  built-in materials use, with `sk_environment_intensity()` 0 (and the functions
  black) without one.
- **Bindings:** uniform block 0 is libsk's per-object block (matrices, time, joints),
  1 its per-draw fragment block (`sk_frame`: camera, time, tint, ambient, lights,
  output settings, the environment's intensity, rotation and irradiance), 2 the
  shader's fragment parameters, 3 the vertex hook's. The shader's textures are
  texture2D in the fragment shader, bindings 0-7, each paired with a sampler; 8 and 9
  are libsk's (the environment cubemap and BRDF table), bound only if used. The file
  format has a version: a change to `sk_frame` (version 2 added the environment) makes
  older files refused ("rebuild it") rather than drawn wrongly. Vertex inputs have fixed locations matching libsk's vertex buffers (without
  them sokol-shdc numbered the skinned shader's inputs in declaration order).
- **`tools/shaderpack.py`** puts `shaders/sk.glsl` in front of the file, adds the two
  vertex shaders (with the hook or an empty one), compiles with sokol-shdc
  (`-f bare_yaml`: sources plus a reflection file) for glsl410, glsl300es and wgsl,
  and writes one text `.skshader`: parameters (name, type, block, std140 offset; the
  tool computes the offsets, which sokol-shdc's reflection doesn't give, and checks
  them against its block sizes), texture names, then per backend and program the
  vertex attributes, uniform blocks, views, samplers and texture-sampler pairs, and
  the sources. Errors in the user's file are reported at its own line numbers. The
  output is deterministic. About 50 KB per shader (three backends, two programs).
- **Runtime (`src/sk_shader.c`, an optional module):** a resource like textures:
  deduplicated by path, reference counted, loaded through `sk_asset` (read on a
  worker; parsed and made on the main thread), so it downloads on the web.
  Finishing picks the running backend's sources (the dummy backend takes the GL
  description) and builds `sg_shader_desc` from the file. Materials and models reach
  it through hooks (`sk_shader_hooks`), so programs that never load a shader don't
  link it.
- **Materials:** `sk_material_create_custom(shader)`; shading reads
  `SK_MATERIAL_CUSTOM`, which create and set_shading refuse. The existing setters
  find the shader's parameters by name and type (float, int, vec2, vec3, vec4;
  `set_color` converts sRGB to linear) and write them into the material's copy of
  the two blocks; textures and their sampling by the shader's texture names (a
  texture not set is white). Built-in names don't apply. Picking treats custom
  surfaces as solid everywhere: libsk can't know where a shader discards.
- **Drawing:** per shader, pipelines for (static or skinned, blended, double-sided)
  made on first use and freed with the shader; blocks the compiler dropped because
  the shader doesn't use them aren't applied.
- **Not yet:** arrays and matrices as parameters, shaders for sprites and shapes
  (phase 3), D3D11/Metal sources.
- Checked on desktop GL, WebGL2 and WebGPU (`examples/shaders.c`: toon on the
  animated gumshoe, a dissolve with a noise texture, waves from a vertex hook), and
  headless (the dummy backend validates every uniform size and binding).

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
