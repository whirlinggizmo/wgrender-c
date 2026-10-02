#include "wgr_material.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "internal/wgr_resource_internal.h"
#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_texture_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_shader_internal.h"
#include "wgr_logger.h"

#define MATERIALS_INITIAL 64 /* slots to start with; the pool doubles as needed */

static wgri_material_t *wgr_materials; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_material_pool;

wgri_shader_hooks_t wgri_shader_hooks; /* internal/wgr_shader.h */

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
    size_t offset; /* into wgri_material_t, or the texture slot for PARAM_TEXTURE */
} param_t;

#define TEXTURE_PARAMS(prefix, slot)                                                                    \
    {prefix, PARAM_TEXTURE, slot},                                                                      \
    {prefix "_texcoord", PARAM_INT, offsetof(wgri_material_t, textures[slot].texcoord)},                 \
    {prefix "_offset", PARAM_VEC2, offsetof(wgri_material_t, textures[slot].offset)},                    \
    {prefix "_rotation", PARAM_FLOAT, offsetof(wgri_material_t, textures[slot].rotation)},               \
    {prefix "_scale", PARAM_VEC2, offsetof(wgri_material_t, textures[slot].scale)}

static const param_t PARAMS[] = {
    {"base_color", PARAM_VEC4, offsetof(wgri_material_t, base_color)},
    TEXTURE_PARAMS("base_color_texture", WGRI_MATERIAL_TEXTURE_BASE_COLOR),
    {"metallic", PARAM_FLOAT, offsetof(wgri_material_t, metallic)},
    {"roughness", PARAM_FLOAT, offsetof(wgri_material_t, roughness)},
    TEXTURE_PARAMS("metallic_roughness_texture", WGRI_MATERIAL_TEXTURE_METALLIC_ROUGHNESS),
    TEXTURE_PARAMS("normal_texture", WGRI_MATERIAL_TEXTURE_NORMAL),
    {"normal_scale", PARAM_FLOAT, offsetof(wgri_material_t, normal_scale)},
    TEXTURE_PARAMS("occlusion_texture", WGRI_MATERIAL_TEXTURE_OCCLUSION),
    {"occlusion_strength", PARAM_FLOAT, offsetof(wgri_material_t, occlusion_strength)},
    {"emissive", PARAM_VEC3, offsetof(wgri_material_t, emissive)},
    TEXTURE_PARAMS("emissive_texture", WGRI_MATERIAL_TEXTURE_EMISSIVE),
};

static wgri_material_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_material_pool, handle, &index)) {
        if (handle != 0) {
            wgr_logger_warn("Invalid material handle (%u)", (unsigned int)handle);
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
                             wgri_material_t **material_out)
{
    wgri_material_t *material_ptr = resolve(material);
    const param_t *param = lookup_param(name);

    if (material_ptr == NULL) {
        return NULL;
    }
    if (param == NULL) {
        wgr_logger_warn("material: unknown parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    if (param->kind != kind && param->kind != alt_kind) {
        wgr_logger_warn("material: parameter '%s' has a different type", name);
        return NULL;
    }
    *material_out = material_ptr;
    return param;
}

static float *lookup_values(wgr_handle_t material, const char *name, param_kind_t kind)
{
    wgri_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, kind, kind, &material_ptr);
    return param != NULL ? (float *)((char *)material_ptr + param->offset) : NULL;
}

static void clear_textures(wgri_material_t *material_ptr)
{
    for (int i = 0; i < WGRI_MATERIAL_MAX_TEXTURES; i++) {
        wgr_resource_release(material_ptr->textures[i].texture); /* no-op for 0 */
        material_ptr->textures[i].texture = 0;
    }
}

static void forget_kept(wgri_material_t *material_ptr);

/* Everything a material holds: textures, and a custom material's shader and values. */
static void clear(wgri_material_t *material_ptr)
{
    clear_textures(material_ptr);
    free(material_ptr->custom_params);
    forget_kept(material_ptr);
    wgr_resource_release(material_ptr->shader); /* no-op for 0 */
    memset(material_ptr, 0, sizeof(*material_ptr));
}

/* ---------------------------------------------------- custom materials ---- */

/* The material when it has a custom shader (without logging); NULL otherwise. */
static wgri_material_t *resolve_custom(wgr_handle_t material)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_material_pool, material, &index) || wgr_materials[index].shader == 0) {
        return NULL;
    }
    return &wgr_materials[index];
}

/* Where a custom material keeps parameter `name`, when its type is one of `types`
 * (a mask of 1 << wgri_shader_param_type_t); NULL (logged) otherwise. */
static unsigned char *custom_param(wgri_material_t *material_ptr, const char *name, int types,
                                   wgri_shader_param_type_t *type_out)
{
    const wgri_shader_t *shader = wgri_shader_hooks.get != NULL ? wgri_shader_hooks.get(material_ptr->shader) : NULL;
    const int i = shader != NULL ? wgri_shader_hooks.find_param(shader, name) : -1;
    const wgri_shader_param_t *param;

    if (i < 0) {
        wgr_logger_warn("material: its shader has no parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    param = &shader->params[i];
    if (!(types & (1 << param->type))) {
        wgr_logger_warn("material: parameter '%s' has a different type", name);
        return NULL;
    }
    if (type_out != NULL) *type_out = param->type;
    return material_ptr->custom_params + param->offset +
           (param->block == WGRI_SHADER_BLOCK_VS_PARAMS ? shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] : 0);
}

static wgri_material_texture_t *custom_texture(wgri_material_t *material_ptr, const char *name)
{
    const wgri_shader_t *shader = wgri_shader_hooks.get != NULL ? wgri_shader_hooks.get(material_ptr->shader) : NULL;
    const int i = shader != NULL ? wgri_shader_hooks.find_texture(shader, name) : -1;
    if (i < 0) {
        wgr_logger_warn("material: its shader has no texture '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    return &material_ptr->textures[i];
}

static bool set_custom_floats(wgri_material_t *material_ptr, const char *name, wgri_shader_param_type_t type,
                              const float *values, int count)
{
    unsigned char *at = custom_param(material_ptr, name, 1 << type, NULL);
    if (at == NULL) return false;
    memcpy(at, values, sizeof(float) * (size_t)count);
    return true;
}

static bool set_custom_int(wgri_material_t *material_ptr, const char *name, int value)
{
    unsigned char *at = custom_param(material_ptr, name, 1 << WGRI_SHADER_PARAM_INT, NULL);
    if (at != NULL) memcpy(at, &value, sizeof(value));
    return at != NULL;
}

static bool set_custom_color(wgri_material_t *material_ptr, const char *name, wgr_color_t color)
{
    const wgri_colorf_t c = wgri_color_unpack(color);
    const float linear[4] = {wgri_srgb_to_linear(c.r), wgri_srgb_to_linear(c.g), wgri_srgb_to_linear(c.b), c.a};
    wgri_shader_param_type_t type;
    unsigned char *at =
        custom_param(material_ptr, name, (1 << WGRI_SHADER_PARAM_VEC3) | (1 << WGRI_SHADER_PARAM_VEC4), &type);
    if (at != NULL) memcpy(at, linear, sizeof(float) * (type == WGRI_SHADER_PARAM_VEC4 ? 4 : 3));
    return at != NULL;
}

static bool set_custom_texture(wgri_material_t *material_ptr, const char *name, wgr_handle_t texture)
{
    wgri_material_texture_t *custom = custom_texture(material_ptr, name);
    if (custom == NULL) return false;
    if (custom->texture != texture) {
        wgri_resource_retain(texture); /* before releasing, in case they're the same resource */
        wgr_resource_release(custom->texture);
        custom->texture = texture;
    }
    return true;
}

static bool set_custom_sampling(wgri_material_t *material_ptr, const char *name, wgr_texture_wrap_t wrap_u,
                                wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter)
{
    wgri_material_texture_t *custom = custom_texture(material_ptr, name);
    if (custom == NULL) return false;
    custom->wrap_u = wrap_u;
    custom->wrap_v = wrap_v;
    custom->filter = filter;
    return true;
}

/* A setting made on a custom material while its shader loads: kept by name, and
 * applied once the shader is READY (realize), which is when an unknown name is told. */
typedef enum { KEPT_INT, KEPT_FLOATS, KEPT_COLOR, KEPT_TEXTURE, KEPT_SAMPLING } kept_kind_t;
typedef struct wgri_material_kept {
    kept_kind_t kind;
    char name[WGRI_SHADER_NAME_MAX];
    wgri_shader_param_type_t type; /* KEPT_FLOATS */
    int count;
    float values[4];
    int value;
    wgr_color_t color;
    wgr_handle_t texture; /* KEPT_TEXTURE: a reference */
    wgr_texture_wrap_t wrap_u, wrap_v;
    wgr_texture_filter_t filter;
} wgri_material_kept_t;

static void forget_kept(wgri_material_t *material_ptr)
{
    for (int i = 0; i < material_ptr->kept_count; i++) {
        if (material_ptr->kept[i].kind == KEPT_TEXTURE) wgr_resource_release(material_ptr->kept[i].texture);
    }
    free(material_ptr->kept);
    material_ptr->kept = NULL;
    material_ptr->kept_count = material_ptr->kept_capacity = 0;
}

/* Where to keep `kind` of `name`: the same setting made before, replaced, or a new
 * one; NULL (logged) out of memory or for a name that can't be one. */
static wgri_material_kept_t *keep(wgri_material_t *material_ptr, kept_kind_t kind, const char *name)
{
    wgri_material_kept_t *kept;
    if (name == NULL || strlen(name) >= WGRI_SHADER_NAME_MAX) {
        wgr_logger_warn("material: '%s' isn't a name its shader can have", name != NULL ? name : "(null)");
        return NULL;
    }
    for (int i = 0; i < material_ptr->kept_count; i++) {
        kept = &material_ptr->kept[i];
        if (kept->kind == kind && strcmp(kept->name, name) == 0) {
            if (kind == KEPT_TEXTURE) wgr_resource_release(kept->texture);
            return kept;
        }
    }
    if (material_ptr->kept_count == material_ptr->kept_capacity) {
        const int capacity = material_ptr->kept_capacity > 0 ? material_ptr->kept_capacity * 2 : 8;
        kept = realloc(material_ptr->kept, sizeof(*kept) * (size_t)capacity);
        if (kept == NULL) return NULL;
        material_ptr->kept = kept;
        material_ptr->kept_capacity = capacity;
    }
    kept = &material_ptr->kept[material_ptr->kept_count++];
    *kept = (wgri_material_kept_t){.kind = kind};
    snprintf(kept->name, sizeof(kept->name), "%s", name);
    return kept;
}

static bool keep_floats(wgri_material_t *material_ptr, const char *name, wgri_shader_param_type_t type,
                        const float *values, int count)
{
    wgri_material_kept_t *kept = keep(material_ptr, KEPT_FLOATS, name);
    if (kept == NULL) return false;
    kept->type = type;
    kept->count = count;
    memcpy(kept->values, values, sizeof(float) * (size_t)count);
    return true;
}

/* Lay out a custom material's parameters for its shader once it's READY, and apply what
 * was set meanwhile. False while the shader loads (or failed). */
static bool realize(wgri_material_t *material_ptr)
{
    const wgri_shader_t *shader;
    size_t size;

    if (material_ptr->custom_params != NULL) {
        return true;
    }
    shader = wgri_shader_hooks.get != NULL ? wgri_shader_hooks.get(material_ptr->shader) : NULL;
    if (shader == NULL) {
        return false;
    }
    size = (size_t)(shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS] + shader->block_size[WGRI_SHADER_BLOCK_VS_PARAMS]);
    material_ptr->custom_params = calloc(1, size > 0 ? size : 1);
    if (material_ptr->custom_params == NULL) {
        return false;
    }
    for (int i = 0; i < material_ptr->kept_count; i++) { /* an unknown name is logged by its setter */
        const wgri_material_kept_t *kept = &material_ptr->kept[i];
        switch (kept->kind) {
            case KEPT_INT: set_custom_int(material_ptr, kept->name, kept->value); break;
            case KEPT_FLOATS: set_custom_floats(material_ptr, kept->name, kept->type, kept->values, kept->count); break;
            case KEPT_COLOR: set_custom_color(material_ptr, kept->name, kept->color); break;
            case KEPT_TEXTURE: set_custom_texture(material_ptr, kept->name, kept->texture); break;
            case KEPT_SAMPLING:
                set_custom_sampling(material_ptr, kept->name, kept->wrap_u, kept->wrap_v, kept->filter);
                break;
        }
    }
    forget_kept(material_ptr); /* the textures hold their own references now */
    return true;
}

/* -------------------------------------------------------------- internal ---- */

void wgri_material_texture_flips(const wgri_material_t *material, const int *view_slot, int count, float flips[2][4])
{
    memset(flips, 0, sizeof(float) * 8);
    for (int t = 0; t < count && t < WGRI_SHADER_MAX_TEXTURES; t++) {
        const int binding = view_slot[t];
        if (binding >= 0 && binding < 8 && wgri_texture_is_flipped(material->textures[t].texture)) {
            flips[binding / 4][binding % 4] = 1.0f;
        }
    }
}

void wgri_material_uv_matrix(const wgri_material_texture_t *texture, float m[6])
{
    const float c = cosf(texture->rotation), s = sinf(texture->rotation);
    m[0] = c * texture->scale[0];
    m[1] = s * texture->scale[1];
    m[2] = texture->offset[0];
    m[3] = -s * texture->scale[0];
    m[4] = c * texture->scale[1];
    m[5] = texture->offset[1];
}

const wgri_material_t *wgri_material_get(wgr_handle_t material)
{
    uint16_t index = 0;
    return wgri_handle_pool_resolve(&wgr_material_pool, material, &index) ? &wgr_materials[index] : NULL;
}

bool wgri_material_is_screen(wgr_handle_t material)
{
    const wgri_material_t *material_ptr = wgri_material_get(material);
    const wgri_shader_t *shader = material_ptr != NULL && material_ptr->shader != 0 && wgri_shader_hooks.get != NULL
                                    ? wgri_shader_hooks.get(material_ptr->shader)
                                    : NULL;
    return shader != NULL && shader->screen;
}

bool wgri_material_is_surface(wgr_handle_t material)
{
    const wgri_material_t *material_ptr = wgri_material_get(material);
    const wgri_shader_t *shader = material_ptr != NULL && material_ptr->shader != 0 && wgri_shader_hooks.get != NULL
                                    ? wgri_shader_hooks.get(material_ptr->shader)
                                    : NULL;
    return material_ptr != NULL && (material_ptr->shader == 0 || (shader != NULL && !shader->screen));
}

wgri_shader_t *wgri_material_custom_shader(const wgri_material_t *material)
{
    /* realize changes only the material's own record (this module's) */
    if (material == NULL || material->shader == 0 || !realize((wgri_material_t *)material)) {
        return NULL;
    }
    return wgri_shader_hooks.get(material->shader);
}

bool wgri_material_custom_failed(const wgri_material_t *material)
{
    return material != NULL && material->shader != 0 &&
           wgr_resource_get_status(material->shader) == WGR_RESOURCE_FAILED;
}

static wgri_material_t wgr_material_failed; /* set up in init */

const wgri_material_t *wgri_material_failed(void)
{
    return &wgr_material_failed;
}

/* Free what a record holds past its resource header. */
static void free_record(void *record)
{
    clear((wgri_material_t *)record);
}

static const wgri_resource_kind_t wgr_material_kind = {
    .create = "wgr_material_create",
    .free = free_record, /* no loader: a material is made from numbers */
};

void wgri_material_init(void)
{
    if (!wgri_handle_pool_init(&wgr_material_pool, WGR_HANDLE_KIND_MATERIAL, "material", (void **)&wgr_materials,
                             sizeof(wgri_material_t), MATERIALS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        wgr_logger_error("material: out of memory");
    }
    wgri_resource_register(&wgr_material_pool, &wgr_material_kind);
    wgr_material_failed = (wgri_material_t){
        .shading = WGR_MATERIAL_UNLIT,
        .alpha_mode = WGR_ALPHA_OPAQUE,
        .base_color = {1.0f, 0.0f, 1.0f, 1.0f}, /* the placeholder checker's magenta */
        .roughness = 1.0f,
        .normal_scale = 1.0f,
        .occlusion_strength = 1.0f,
    };
    for (int i = 0; i < WGRI_MATERIAL_MAX_TEXTURES; i++) {
        wgr_material_failed.textures[i] = (wgri_material_texture_t){.scale = {1.0f, 1.0f}, .mipmaps = true};
    }
}

void wgri_material_deinit(void)
{
    wgri_resource_register(&wgr_material_pool, NULL);
    for (uint16_t i = 1; i < wgr_material_pool.capacity; i++) {
        if (wgr_material_pool.occupied[i]) {
            clear(&wgr_materials[i]);
        }
    }
    wgri_handle_pool_destroy(&wgr_material_pool);
}

bool wgri_material_set_texture_mipmaps(wgr_handle_t material, const char *name, bool mipmaps)
{
    wgri_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    if (param == NULL) {
        return false;
    }
    material_ptr->textures[param->offset].mipmaps = mipmaps;
    return true;
}

/* ------------------------------------------------------------ public API ---- */

WGRI_KEEP
wgr_handle_t wgr_material_create(wgr_material_shading_t shading)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    if (shading != WGR_MATERIAL_PBR && shading != WGR_MATERIAL_UNLIT) {
        wgr_logger_error("wgr_material_create: unknown shading %d", (int)shading);
        return 0;
    }
    handle = wgri_resource_add(WGR_HANDLE_KIND_MATERIAL);
    if (handle == 0) {
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_material_pool, handle, &index);
    wgr_materials[index] = (wgri_material_t){
        .resource = wgr_materials[index].resource, /* the core's part: READY, one reference */
        .shading = shading,
        .alpha_mode = WGR_ALPHA_OPAQUE,
        .alpha_cutoff = 0.5f,
        .base_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .metallic = 1.0f,
        .roughness = 1.0f,
        .normal_scale = 1.0f,
        .occlusion_strength = 1.0f,
    };
    for (int i = 0; i < WGRI_MATERIAL_MAX_TEXTURES; i++) {
        wgr_materials[index].textures[i] = (wgri_material_texture_t){.scale = {1.0f, 1.0f}, .mipmaps = true};
    }
    return handle;
}

WGRI_KEEP
wgr_handle_t wgr_material_create_custom(wgr_handle_t shader)
{
    wgri_material_t *material_ptr;
    wgr_handle_t handle;

    if (wgr_handle_get_kind(shader) != WGR_HANDLE_KIND_SHADER || wgr_resource_get_status(shader) == WGR_RESOURCE_NONE) {
        wgr_logger_error("wgr_material_create_custom: needs a shader (wgr_shader_create)");
        return 0;
    }
    handle = wgr_material_create(WGR_MATERIAL_UNLIT);
    material_ptr = resolve(handle);
    if (material_ptr == NULL) {
        return 0;
    }
    material_ptr->shading = WGR_MATERIAL_CUSTOM;
    material_ptr->shader = shader;
    wgri_resource_retain(shader);
    realize(material_ptr); /* at once when the shader is READY; else on first use after */
    return handle;
}

WGRI_KEEP
wgr_handle_t wgr_material_get_shader(wgr_handle_t material)
{
    wgri_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shader : 0;
}

WGRI_KEEP
bool wgr_material_set_shading(wgr_handle_t material, wgr_material_shading_t shading)
{
    wgri_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || (shading != WGR_MATERIAL_PBR && shading != WGR_MATERIAL_UNLIT)) {
        return false;
    }
    if (material_ptr->shader != 0) {
        wgr_logger_warn("wgr_material_set_shading: a custom material keeps its shader");
        return false;
    }
    material_ptr->shading = shading;
    return true;
}

WGRI_KEEP
wgr_material_shading_t wgr_material_get_shading(wgr_handle_t material)
{
    wgri_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shading : WGR_MATERIAL_PBR;
}

WGRI_KEEP
bool wgr_material_set_alpha_mode(wgr_handle_t material, wgr_alpha_mode_t mode, float cutoff)
{
    wgri_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || mode < WGR_ALPHA_OPAQUE || mode > WGR_ALPHA_BLEND) {
        return false;
    }
    material_ptr->alpha_mode = mode;
    material_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff;
    return true;
}

WGRI_KEEP
wgr_alpha_mode_t wgr_material_get_alpha_mode(wgr_handle_t material)
{
    wgri_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->alpha_mode : WGR_ALPHA_OPAQUE;
}

WGRI_KEEP
bool wgr_material_set_double_sided(wgr_handle_t material, bool double_sided)
{
    wgri_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) {
        return false;
    }
    material_ptr->double_sided = double_sided;
    return true;
}

WGRI_KEEP
bool wgr_material_is_double_sided(wgr_handle_t material)
{
    wgri_material_t *material_ptr = resolve(material);
    return material_ptr != NULL && material_ptr->double_sided;
}

WGRI_KEEP
bool wgr_material_set_int(wgr_handle_t material, const char *name, int value)
{
    wgri_material_t *material_ptr = resolve_custom(material);
    if (material_ptr != NULL) {
        wgri_material_kept_t *kept;
        if (realize(material_ptr)) return set_custom_int(material_ptr, name, value);
        if ((kept = keep(material_ptr, KEPT_INT, name)) != NULL) kept->value = value;
        return kept != NULL;
    }
    const param_t *param = lookup(material, name, PARAM_INT, PARAM_INT, &material_ptr);
    if (param == NULL) {
        return false;
    }
    if (value < 0 || value > 1) { /* the only int parameters are texture coordinate sets */
        wgr_logger_warn("material: '%s' must be 0 or 1 (got %d)", name, value);
        return false;
    }
    *(int *)((char *)material_ptr + param->offset) = value;
    return true;
}

WGRI_KEEP
bool wgr_material_set_vec2(wgr_handle_t material, const char *name, float x, float y)
{
    wgri_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) {
        return realize(custom_ptr) ? set_custom_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC2, (const float[]){x, y}, 2)
                                   : keep_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC2, (const float[]){x, y}, 2);
    }
    float *values = lookup_values(material, name, PARAM_VEC2);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    return true;
}

WGRI_KEEP
bool wgr_material_set_float(wgr_handle_t material, const char *name, float value)
{
    wgri_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) {
        return realize(custom_ptr) ? set_custom_floats(custom_ptr, name, WGRI_SHADER_PARAM_FLOAT, &value, 1)
                                   : keep_floats(custom_ptr, name, WGRI_SHADER_PARAM_FLOAT, &value, 1);
    }
    float *values = lookup_values(material, name, PARAM_FLOAT);
    if (values == NULL) {
        return false;
    }
    values[0] = value;
    return true;
}

WGRI_KEEP
bool wgr_material_set_vec3(wgr_handle_t material, const char *name, float x, float y, float z)
{
    wgri_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) {
        return realize(custom_ptr) ? set_custom_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC3, (const float[]){x, y, z}, 3)
                                   : keep_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC3, (const float[]){x, y, z}, 3);
    }
    float *values = lookup_values(material, name, PARAM_VEC3);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    values[2] = z;
    return true;
}

WGRI_KEEP
bool wgr_material_set_vec4(wgr_handle_t material, const char *name, float x, float y, float z, float w)
{
    wgri_material_t *custom_ptr = resolve_custom(material);
    if (custom_ptr != NULL) {
        return realize(custom_ptr) ? set_custom_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC4, (const float[]){x, y, z, w}, 4)
                                   : keep_floats(custom_ptr, name, WGRI_SHADER_PARAM_VEC4, (const float[]){x, y, z, w}, 4);
    }
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

WGRI_KEEP
bool wgr_material_set_color(wgr_handle_t material, const char *name, wgr_color_t color)
{
    wgri_material_t *material_ptr = resolve_custom(material);
    const wgri_colorf_t c = wgri_color_unpack(color);
    float *values;

    if (material_ptr != NULL) {
        wgri_material_kept_t *kept;
        if (realize(material_ptr)) return set_custom_color(material_ptr, name, color);
        if ((kept = keep(material_ptr, KEPT_COLOR, name)) != NULL) kept->color = color;
        return kept != NULL;
    }
    const param_t *param = lookup(material, name, PARAM_VEC3, PARAM_VEC4, &material_ptr);

    if (param == NULL) {
        return false;
    }
    values = (float *)((char *)material_ptr + param->offset);
    values[0] = wgri_srgb_to_linear(c.r);
    values[1] = wgri_srgb_to_linear(c.g);
    values[2] = wgri_srgb_to_linear(c.b);
    if (param->kind == PARAM_VEC4) {
        values[3] = c.a; /* alpha is linear */
    }
    return true;
}

WGRI_KEEP
bool wgr_material_set_texture(wgr_handle_t material, const char *name, wgr_handle_t texture)
{
    wgri_material_t *material_ptr = resolve_custom(material);
    const param_t *param = NULL;
    wgr_handle_t *slot;

    if (texture != 0 && wgr_handle_get_kind(texture) != WGR_HANDLE_KIND_TEXTURE) {
        wgr_logger_warn("material: '%s' needs a texture handle", name != NULL ? name : "(null)");
        return false;
    }
    if (material_ptr != NULL) {
        wgri_material_kept_t *kept;
        if (realize(material_ptr)) return set_custom_texture(material_ptr, name, texture);
        if ((kept = keep(material_ptr, KEPT_TEXTURE, name)) != NULL) {
            wgri_resource_retain(texture); /* the kept setting's own reference */
            kept->texture = texture;
        }
        return kept != NULL;
    }
    param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    if (param == NULL) {
        return false;
    }
    slot = &material_ptr->textures[param->offset].texture;
    if (*slot != texture) {
        wgri_resource_retain(texture); /* before releasing, in case they're the same resource */
        wgr_resource_release(*slot);
        *slot = texture;
    }
    return true;
}

WGRI_KEEP
bool wgr_material_set_texture_sampling(wgr_handle_t material, const char *name, wgr_texture_wrap_t wrap_u,
                                      wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter)
{
    wgri_material_t *material_ptr = resolve_custom(material);
    wgri_material_texture_t *texture = NULL;
    const param_t *param = NULL;

    if (wrap_u < WGR_TEXTURE_WRAP_REPEAT || wrap_u > WGR_TEXTURE_WRAP_MIRROR || wrap_v < WGR_TEXTURE_WRAP_REPEAT ||
        wrap_v > WGR_TEXTURE_WRAP_MIRROR || filter < WGR_TEXTURE_FILTER_LINEAR || filter > WGR_TEXTURE_FILTER_NEAREST) {
        wgr_logger_warn("material: invalid sampling for '%s'", name != NULL ? name : "(null)");
        return false;
    }
    if (material_ptr != NULL) {
        wgri_material_kept_t *kept;
        if (realize(material_ptr)) return set_custom_sampling(material_ptr, name, wrap_u, wrap_v, filter);
        if ((kept = keep(material_ptr, KEPT_SAMPLING, name)) != NULL) {
            kept->wrap_u = wrap_u;
            kept->wrap_v = wrap_v;
            kept->filter = filter;
        }
        return kept != NULL;
    }
    param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    if (param != NULL) texture = &material_ptr->textures[param->offset];
    if (texture == NULL) {
        return false;
    }
    texture->wrap_u = wrap_u;
    texture->wrap_v = wrap_v;
    texture->filter = filter;
    return true;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_material_module = {.name = "material", .order = 30, .init = wgri_material_init, .deinit = wgri_material_deinit};
WGRI_MODULE(wgr_material_module)
