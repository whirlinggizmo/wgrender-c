#include "wgr_material.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/wgr_color.h"
#include "internal/wgr_handle_pool.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_material.h"
#include "internal/wgr_math.h"
#include "internal/wgr_texture.h"
#include "internal/wgr_module.h"
#include "internal/wgr_shader.h"
#include "wgr_logger.h"

#define MATERIALS_INITIAL 64 /* slots to start with; the pool doubles as needed */

static wgr_material_t *wgr_materials; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_material_pool;

wgr_shader_hooks_t wgr_shader_hooks; /* internal/wgr_shader.h */

/* ------------------------------------------------------------ parameters ---- */

typedef enum {
    PARAM_INT,
    PARAM_FLOAT,
    PARAM_VEC2,
    PARAM_VEC3,
    PARAM_VEC4,
    PARAM_TEXTURE,
} param_kind_t;

typedef struct {
    const char *name;
    param_kind_t kind;
    size_t offset; /* into wgr_material_t, or the texture slot for PARAM_TEXTURE */
} param_t;

#define TEXTURE_PARAMS(prefix, slot)                                                                    \
    {prefix, PARAM_TEXTURE, slot},                                                                      \
    {prefix "_texcoord", PARAM_INT, offsetof(wgr_material_t, textures[slot].texcoord)},                 \
    {prefix "_offset", PARAM_VEC2, offsetof(wgr_material_t, textures[slot].offset)},                    \
    {prefix "_rotation", PARAM_FLOAT, offsetof(wgr_material_t, textures[slot].rotation)},               \
    {prefix "_scale", PARAM_VEC2, offsetof(wgr_material_t, textures[slot].scale)}

static const param_t PARAMS[] = {
    {"base_color", PARAM_VEC4, offsetof(wgr_material_t, base_color)},
    TEXTURE_PARAMS("base_color_texture", WGR_MATERIAL_TEXTURE_BASE_COLOR),
    {"metallic", PARAM_FLOAT, offsetof(wgr_material_t, metallic)},
    {"roughness", PARAM_FLOAT, offsetof(wgr_material_t, roughness)},
    TEXTURE_PARAMS("metallic_roughness_texture", WGR_MATERIAL_TEXTURE_METALLIC_ROUGHNESS),
    TEXTURE_PARAMS("normal_texture", WGR_MATERIAL_TEXTURE_NORMAL),
    {"normal_scale", PARAM_FLOAT, offsetof(wgr_material_t, normal_scale)},
    TEXTURE_PARAMS("occlusion_texture", WGR_MATERIAL_TEXTURE_OCCLUSION),
    {"occlusion_strength", PARAM_FLOAT, offsetof(wgr_material_t, occlusion_strength)},
    {"emissive", PARAM_VEC3, offsetof(wgr_material_t, emissive)},
    TEXTURE_PARAMS("emissive_texture", WGR_MATERIAL_TEXTURE_EMISSIVE),
};

static wgr_material_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_material_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid material handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &wgr_materials[index];
}

static const param_t *lookup_param(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(PARAMS) / sizeof(PARAMS[0]); i++) {
        if (strcmp(PARAMS[i].name, name) == 0) {
            return &PARAMS[i];
        }
    }
    return NULL;
}

/* Resolve the material and its parameter. Logs and returns NULL when either
 * doesn't exist, or when the parameter's kind isn't `kind` (or `alt_kind`). */
static const param_t *lookup(wgr_handle_t material, const char *name, param_kind_t kind, param_kind_t alt_kind,
                             wgr_material_t **material_out)
{
    wgr_material_t *material_ptr = resolve(material);
    const param_t *param = lookup_param(name);

    if (material_ptr == NULL) {
        return NULL;
    }
    if (param == NULL) {
        log_warn("material: unknown parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    if (param->kind != kind && param->kind != alt_kind) {
        log_warn("material: parameter '%s' has a different type", name);
        return NULL;
    }
    *material_out = material_ptr;
    return param;
}

static float *lookup_values(wgr_handle_t material, const char *name, param_kind_t kind)
{
    wgr_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, kind, kind, &material_ptr);
    return param != NULL ? (float *)((char *)material_ptr + param->offset) : NULL;
}

static void clear_textures(wgr_material_t *material_ptr)
{
    for (int i = 0; i < WGR_MATERIAL_MAX_TEXTURES; i++) {
        wgr_texture_release(material_ptr->textures[i].texture); /* no-op for 0 */
        material_ptr->textures[i].texture = 0;
    }
}

/* Everything a material holds: textures, and a custom material's shader and values. */
static void clear(wgr_material_t *material_ptr)
{
    clear_textures(material_ptr);
    free(material_ptr->custom_params);
    if (material_ptr->shader != 0 && wgr_shader_hooks.release != NULL) {
        wgr_shader_hooks.release(material_ptr->shader);
    }
    memset(material_ptr, 0, sizeof(*material_ptr));
}

/* ---------------------------------------------------- custom materials ---- */

/* The material when it has a custom shader (without logging); NULL otherwise. */
static wgr_material_t *resolve_custom(wgr_handle_t material)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_material_pool, material, &index) || wgr_materials[index].shader == 0) {
        return NULL;
    }
    return &wgr_materials[index];
}

/* Where a custom material keeps parameter `name`, when its type is one of `types`
 * (a mask of 1 << wgr_shader_param_type_t); NULL (logged) otherwise. */
static unsigned char *custom_param(wgr_material_t *material_ptr, const char *name, int types,
                                   wgr_shader_param_type_t *type_out)
{
    const wgr_shader_t *shader = wgr_shader_hooks.get != NULL ? wgr_shader_hooks.get(material_ptr->shader) : NULL;
    const int i = shader != NULL ? wgr_shader_hooks.find_param(shader, name) : -1;
    const wgr_shader_param_t *param;

    if (i < 0) {
        log_warn("material: its shader has no parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    param = &shader->params[i];
    if (!(types & (1 << param->type))) {
        log_warn("material: parameter '%s' has a different type", name);
        return NULL;
    }
    if (type_out != NULL) *type_out = param->type;
    return material_ptr->custom_params + param->offset +
           (param->block == WGR_SHADER_BLOCK_VS_PARAMS ? shader->block_size[WGR_SHADER_BLOCK_FS_PARAMS] : 0);
}

static wgr_material_texture_t *custom_texture(wgr_material_t *material_ptr, const char *name)
{
    const wgr_shader_t *shader = wgr_shader_hooks.get != NULL ? wgr_shader_hooks.get(material_ptr->shader) : NULL;
    const int i = shader != NULL ? wgr_shader_hooks.find_texture(shader, name) : -1;
    if (i < 0) {
        log_warn("material: its shader has no texture '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    return &material_ptr->textures[i];
}

static bool set_custom_floats(wgr_material_t *material_ptr, const char *name, wgr_shader_param_type_t type,
                              const float *values, int count)
{
    unsigned char *at = custom_param(material_ptr, name, 1 << type, NULL);
    if (at == NULL) return false;
    memcpy(at, values, sizeof(float) * (size_t)count);
    return true;
}

/* -------------------------------------------------------------- internal ---- */

void wgr_material_uv_matrix(const wgr_material_texture_t *texture, float m[6])
{
    const float c = cosf(texture->rotation), s = sinf(texture->rotation);
    m[0] = c * texture->scale[0];
    m[1] = s * texture->scale[1];
    m[2] = texture->offset[0];
    m[3] = -s * texture->scale[0];
    m[4] = c * texture->scale[1];
    m[5] = texture->offset[1];
}

const wgr_material_t *wgr_material_get(wgr_handle_t material)
{
    uint16_t index = 0;
    return wgr_handle_pool_resolve(&wgr_material_pool, material, &index) ? &wgr_materials[index] : NULL;
}

bool wgr_material_is_screen(wgr_handle_t material)
{
    const wgr_material_t *material_ptr = wgr_material_get(material);
    const wgr_shader_t *shader = material_ptr != NULL && material_ptr->shader != 0 && wgr_shader_hooks.get != NULL
                                    ? wgr_shader_hooks.get(material_ptr->shader)
                                    : NULL;
    return shader != NULL && shader->screen;
}

void wgr_material_retain(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    if (material_ptr != NULL) {
        material_ptr->ref_count++;
    }
}

WGR_KEEP
void wgr_material_release(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) {
        return;
    }
    if (material_ptr->ref_count > 0) {
        material_ptr->ref_count--;
    }
    if (material_ptr->ref_count == 0) {
        clear(material_ptr);
        wgr_handle_pool_free(&wgr_material_pool, material);
    }
}

void wgr_material_init(void)
{
    if (!wgr_handle_pool_init(&wgr_material_pool, WGR_HANDLE_KIND_MATERIAL, "material", (void **)&wgr_materials,
                             sizeof(wgr_material_t), MATERIALS_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("material: out of memory");
    }
}

void wgr_material_deinit(void)
{
    for (uint16_t i = 1; i < wgr_material_pool.capacity; i++) {
        if (wgr_material_pool.occupied[i]) {
            clear(&wgr_materials[i]);
        }
    }
    wgr_handle_pool_destroy(&wgr_material_pool);
}

bool wgr_material_set_texture_mipmaps(wgr_handle_t material, const char *name, bool mipmaps)
{
    wgr_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    if (param == NULL) {
        return false;
    }
    material_ptr->textures[param->offset].mipmaps = mipmaps;
    return true;
}

/* ------------------------------------------------------------ public API ---- */

WGR_KEEP
wgr_handle_t wgr_material_create(wgr_material_shading_t shading)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    if (shading != WGR_MATERIAL_PBR && shading != WGR_MATERIAL_UNLIT) {
        log_error("wgr_material_create: unknown shading %d", (int)shading);
        return 0;
    }
    handle = wgr_handle_pool_alloc(&wgr_material_pool);
    if (handle == 0) {
        log_error("material: pool full (%u)", (unsigned)wgr_material_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_material_pool, handle, &index);
    wgr_materials[index] = (wgr_material_t){
        .shading = shading,
        .alpha_mode = WGR_ALPHA_OPAQUE,
        .alpha_cutoff = 0.5f,
        .base_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .metallic = 1.0f,
        .roughness = 1.0f,
        .normal_scale = 1.0f,
        .occlusion_strength = 1.0f,
        .ref_count = 1,
    };
    for (int i = 0; i < WGR_MATERIAL_MAX_TEXTURES; i++) {
        wgr_materials[index].textures[i] = (wgr_material_texture_t){.scale = {1.0f, 1.0f}, .mipmaps = true};
    }
    return handle;
}

WGR_KEEP
wgr_handle_t wgr_material_create_custom(wgr_handle_t shader)
{
    const wgr_shader_t *shader_ptr = wgr_shader_hooks.get != NULL ? wgr_shader_hooks.get(shader) : NULL;
    wgr_material_t *material_ptr;
    unsigned char *params;
    wgr_handle_t handle;
    size_t size;

    if (shader_ptr == NULL) {
        log_error("wgr_material_create_custom: needs a shader (wgr_shader_create)");
        return 0;
    }
    size = (size_t)(shader_ptr->block_size[WGR_SHADER_BLOCK_FS_PARAMS] + shader_ptr->block_size[WGR_SHADER_BLOCK_VS_PARAMS]);
    params = calloc(1, size > 0 ? size : 1);
    handle = params != NULL ? wgr_material_create(WGR_MATERIAL_UNLIT) : 0;
    material_ptr = resolve(handle);
    if (material_ptr == NULL) {
        free(params);
        return 0;
    }
    material_ptr->shading = WGR_MATERIAL_CUSTOM;
    material_ptr->shader = shader;
    material_ptr->custom_params = params;
    wgr_shader_hooks.retain(shader);
    return handle;
}

WGR_KEEP
wgr_handle_t wgr_material_get_shader(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shader : 0;
}

WGR_KEEP
bool wgr_material_set_shading(wgr_handle_t material, wgr_material_shading_t shading)
{
    wgr_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || (shading != WGR_MATERIAL_PBR && shading != WGR_MATERIAL_UNLIT)) {
        return false;
    }
    if (material_ptr->shader != 0) {
        log_warn("wgr_material_set_shading: a custom material keeps its shader");
        return false;
    }
    material_ptr->shading = shading;
    return true;
}

WGR_KEEP
wgr_material_shading_t wgr_material_get_shading(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shading : WGR_MATERIAL_PBR;
}

WGR_KEEP
bool wgr_material_set_alpha_mode(wgr_handle_t material, wgr_alpha_mode_t mode, float cutoff)
{
    wgr_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || mode < WGR_ALPHA_OPAQUE || mode > WGR_ALPHA_BLEND) {
        return false;
    }
    material_ptr->alpha_mode = mode;
    material_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff;
    return true;
}

WGR_KEEP
wgr_alpha_mode_t wgr_material_get_alpha_mode(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->alpha_mode : WGR_ALPHA_OPAQUE;
}

WGR_KEEP
bool wgr_material_set_double_sided(wgr_handle_t material, bool double_sided)
{
    wgr_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) {
        return false;
    }
    material_ptr->double_sided = double_sided;
    return true;
}

WGR_KEEP
bool wgr_material_is_double_sided(wgr_handle_t material)
{
    wgr_material_t *material_ptr = resolve(material);
    return material_ptr != NULL && material_ptr->double_sided;
}

WGR_KEEP
bool wgr_material_set_int(wgr_handle_t material, const char *name, int value)
{
    wgr_material_t *material_ptr = resolve_custom(material);
    if (material_ptr != NULL) {
        unsigned char *at = custom_param(material_ptr, name, 1 << WGR_SHADER_PARAM_INT, NULL);
        if (at != NULL) memcpy(at, &value, sizeof(value));
        return at != NULL;
    }
    const param_t *param = lookup(material, name, PARAM_INT, PARAM_INT, &material_ptr);
    if (param == NULL) {
        return false;
    }
    if (value < 0 || value > 1) { /* the only int parameters are texture coordinate sets */
        log_warn("material: '%s' must be 0 or 1 (got %d)", name, value);
        return false;
    }
    *(int *)((char *)material_ptr + param->offset) = value;
    return true;
}

WGR_KEEP
bool wgr_material_set_vec2(wgr_handle_t material, const char *name, float x, float y)
{
    wgr_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) return set_custom_floats(custom_ptr, name, WGR_SHADER_PARAM_VEC2, (const float[]){x, y}, 2);
    float *values = lookup_values(material, name, PARAM_VEC2);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    return true;
}

WGR_KEEP
bool wgr_material_set_float(wgr_handle_t material, const char *name, float value)
{
    wgr_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) return set_custom_floats(custom_ptr, name, WGR_SHADER_PARAM_FLOAT, &value, 1);
    float *values = lookup_values(material, name, PARAM_FLOAT);
    if (values == NULL) {
        return false;
    }
    values[0] = value;
    return true;
}

WGR_KEEP
bool wgr_material_set_vec3(wgr_handle_t material, const char *name, float x, float y, float z)
{
    wgr_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) return set_custom_floats(custom_ptr, name, WGR_SHADER_PARAM_VEC3, (const float[]){x, y, z}, 3);
    float *values = lookup_values(material, name, PARAM_VEC3);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    values[2] = z;
    return true;
}

WGR_KEEP
bool wgr_material_set_vec4(wgr_handle_t material, const char *name, float x, float y, float z, float w)
{
    wgr_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) return set_custom_floats(custom_ptr, name, WGR_SHADER_PARAM_VEC4, (const float[]){x, y, z, w}, 4);
    float *values = lookup_values(material, name, PARAM_VEC4);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
    return true;
}

WGR_KEEP
bool wgr_material_set_color(wgr_handle_t material, const char *name, wgr_color_t color)
{
    wgr_material_t *material_ptr = resolve_custom(material);
    const wgr_colorf_t c = wgr_color_unpack(color);
    float *values;

    if (material_ptr != NULL) {
        wgr_shader_param_type_t type;
        unsigned char *at =
            custom_param(material_ptr, name, (1 << WGR_SHADER_PARAM_VEC3) | (1 << WGR_SHADER_PARAM_VEC4), &type);
        const float linear[4] = {wgr_srgb_to_linear(c.r), wgr_srgb_to_linear(c.g), wgr_srgb_to_linear(c.b), c.a};
        if (at != NULL) memcpy(at, linear, sizeof(float) * (type == WGR_SHADER_PARAM_VEC4 ? 4 : 3));
        return at != NULL;
    }
    const param_t *param = lookup(material, name, PARAM_VEC3, PARAM_VEC4, &material_ptr);

    if (param == NULL) {
        return false;
    }
    values = (float *)((char *)material_ptr + param->offset);
    values[0] = wgr_srgb_to_linear(c.r);
    values[1] = wgr_srgb_to_linear(c.g);
    values[2] = wgr_srgb_to_linear(c.b);
    if (param->kind == PARAM_VEC4) {
        values[3] = c.a; /* alpha is linear */
    }
    return true;
}

WGR_KEEP
bool wgr_material_set_texture(wgr_handle_t material, const char *name, wgr_handle_t texture)
{
    wgr_material_t *material_ptr = resolve_custom(material);
    wgr_material_texture_t *custom = material_ptr != NULL ? custom_texture(material_ptr, name) : NULL;
    const param_t *param = NULL;
    wgr_handle_t *slot;

    if (material_ptr == NULL) {
        param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    }
    if (param == NULL && custom == NULL) {
        return false;
    }
    if (texture != 0 && wgr_handle_get_kind(texture) != WGR_HANDLE_KIND_TEXTURE) {
        log_warn("material: '%s' needs a texture handle", name);
        return false;
    }
    slot = custom != NULL ? &custom->texture : &material_ptr->textures[param->offset].texture;
    if (*slot != texture) {
        wgr_texture_retain(texture); /* before releasing, in case they're the same resource */
        wgr_texture_release(*slot);
        *slot = texture;
    }
    return true;
}

WGR_KEEP
bool wgr_material_set_texture_sampling(wgr_handle_t material, const char *name, wgr_texture_wrap_t wrap_u,
                                      wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter)
{
    wgr_material_t *material_ptr = resolve_custom(material);
    wgr_material_texture_t *texture = material_ptr != NULL ? custom_texture(material_ptr, name) : NULL;
    const param_t *param = NULL;

    if (material_ptr == NULL) {
        param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
        if (param != NULL) texture = &material_ptr->textures[param->offset];
    }
    if (texture == NULL) {
        return false;
    }
    if (wrap_u < WGR_TEXTURE_WRAP_REPEAT || wrap_u > WGR_TEXTURE_WRAP_MIRROR || wrap_v < WGR_TEXTURE_WRAP_REPEAT ||
        wrap_v > WGR_TEXTURE_WRAP_MIRROR || filter < WGR_TEXTURE_FILTER_LINEAR || filter > WGR_TEXTURE_FILTER_NEAREST) {
        log_warn("material: invalid sampling for '%s'", name);
        return false;
    }
    texture->wrap_u = wrap_u;
    texture->wrap_v = wrap_v;
    texture->filter = filter;
    return true;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_material_module = {.name = "material", .order = 30, .init = wgr_material_init, .deinit = wgr_material_deinit};
WGR_MODULE(wgr_material_module)
