#include "sk_material.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_material.h"
#include "internal/sk_math.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"

#define MATERIALS_INITIAL 64 /* slots to start with; the pool doubles as needed */

static sk_material_t *sk_materials; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_material_pool;

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
    size_t offset; /* into sk_material_t, or the texture slot for PARAM_TEXTURE */
} param_t;

#define TEXTURE_PARAMS(prefix, slot)                                                                    \
    {prefix, PARAM_TEXTURE, slot},                                                                      \
    {prefix "_texcoord", PARAM_INT, offsetof(sk_material_t, textures[slot].texcoord)},                 \
    {prefix "_offset", PARAM_VEC2, offsetof(sk_material_t, textures[slot].offset)},                    \
    {prefix "_rotation", PARAM_FLOAT, offsetof(sk_material_t, textures[slot].rotation)},               \
    {prefix "_scale", PARAM_VEC2, offsetof(sk_material_t, textures[slot].scale)}

static const param_t PARAMS[] = {
    {"base_color", PARAM_VEC4, offsetof(sk_material_t, base_color)},
    TEXTURE_PARAMS("base_color_texture", SK_MATERIAL_TEXTURE_BASE_COLOR),
    {"metallic", PARAM_FLOAT, offsetof(sk_material_t, metallic)},
    {"roughness", PARAM_FLOAT, offsetof(sk_material_t, roughness)},
    TEXTURE_PARAMS("metallic_roughness_texture", SK_MATERIAL_TEXTURE_METALLIC_ROUGHNESS),
    TEXTURE_PARAMS("normal_texture", SK_MATERIAL_TEXTURE_NORMAL),
    {"normal_scale", PARAM_FLOAT, offsetof(sk_material_t, normal_scale)},
    TEXTURE_PARAMS("occlusion_texture", SK_MATERIAL_TEXTURE_OCCLUSION),
    {"occlusion_strength", PARAM_FLOAT, offsetof(sk_material_t, occlusion_strength)},
    {"emissive", PARAM_VEC3, offsetof(sk_material_t, emissive)},
    TEXTURE_PARAMS("emissive_texture", SK_MATERIAL_TEXTURE_EMISSIVE),
};

static sk_material_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_material_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid material handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &sk_materials[index];
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
static const param_t *lookup(sk_handle_t material, const char *name, param_kind_t kind, param_kind_t alt_kind,
                             sk_material_t **material_out)
{
    sk_material_t *material_ptr = resolve(material);
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

static float *lookup_values(sk_handle_t material, const char *name, param_kind_t kind)
{
    sk_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, kind, kind, &material_ptr);
    return param != NULL ? (float *)((char *)material_ptr + param->offset) : NULL;
}

static void clear_textures(sk_material_t *material_ptr)
{
    for (int i = 0; i < SK_MATERIAL_TEXTURE_COUNT; i++) {
        sk_texture_release(material_ptr->textures[i].texture); /* no-op for 0 */
        material_ptr->textures[i].texture = 0;
    }
}

/* -------------------------------------------------------------- internal ---- */

void sk_material_uv_matrix(const sk_material_texture_t *texture, float m[6])
{
    const float c = cosf(texture->rotation), s = sinf(texture->rotation);
    m[0] = c * texture->scale[0];
    m[1] = s * texture->scale[1];
    m[2] = texture->offset[0];
    m[3] = -s * texture->scale[0];
    m[4] = c * texture->scale[1];
    m[5] = texture->offset[1];
}

const sk_material_t *sk_material_get(sk_handle_t material)
{
    uint16_t index = 0;
    return sk_handle_pool_resolve(&sk_material_pool, material, &index) ? &sk_materials[index] : NULL;
}

void sk_material_retain(sk_handle_t material)
{
    sk_material_t *material_ptr = resolve(material);
    if (material_ptr != NULL) {
        material_ptr->ref_count++;
    }
}

SK_KEEP
void sk_material_release(sk_handle_t material)
{
    sk_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) {
        return;
    }
    if (material_ptr->ref_count > 0) {
        material_ptr->ref_count--;
    }
    if (material_ptr->ref_count == 0) {
        clear_textures(material_ptr);
        memset(material_ptr, 0, sizeof(*material_ptr));
        sk_handle_pool_free(&sk_material_pool, material);
    }
}

void sk_material_init(void)
{
    if (!sk_handle_pool_init(&sk_material_pool, SK_HANDLE_KIND_MATERIAL, "material", (void **)&sk_materials,
                             sizeof(sk_material_t), MATERIALS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("material: out of memory");
    }
}

void sk_material_deinit(void)
{
    for (uint16_t i = 1; i < sk_material_pool.capacity; i++) {
        if (sk_material_pool.occupied[i]) {
            clear_textures(&sk_materials[i]);
            memset(&sk_materials[i], 0, sizeof(sk_materials[i]));
        }
    }
    sk_handle_pool_destroy(&sk_material_pool);
}

bool sk_material_set_texture_mipmaps(sk_handle_t material, const char *name, bool mipmaps)
{
    sk_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    if (param == NULL) {
        return false;
    }
    material_ptr->textures[param->offset].mipmaps = mipmaps;
    return true;
}

/* ------------------------------------------------------------ public API ---- */

SK_KEEP
sk_handle_t sk_material_create(sk_material_shading_t shading)
{
    sk_handle_t handle;
    uint16_t index = 0;

    if (shading != SK_MATERIAL_PBR && shading != SK_MATERIAL_UNLIT) {
        log_error("sk_material_create: unknown shading %d", (int)shading);
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_material_pool);
    if (handle == 0) {
        log_error("material: pool full (%u)", (unsigned)sk_material_pool.max - 1u);
        return 0;
    }
    sk_handle_pool_resolve(&sk_material_pool, handle, &index);
    sk_materials[index] = (sk_material_t){
        .shading = shading,
        .alpha_mode = SK_MATERIAL_ALPHA_OPAQUE,
        .alpha_cutoff = 0.5f,
        .base_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .metallic = 1.0f,
        .roughness = 1.0f,
        .normal_scale = 1.0f,
        .occlusion_strength = 1.0f,
        .ref_count = 1,
    };
    for (int i = 0; i < SK_MATERIAL_TEXTURE_COUNT; i++) {
        sk_materials[index].textures[i] = (sk_material_texture_t){.scale = {1.0f, 1.0f}, .mipmaps = true};
    }
    return handle;
}

SK_KEEP
bool sk_material_set_shading(sk_handle_t material, sk_material_shading_t shading)
{
    sk_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || (shading != SK_MATERIAL_PBR && shading != SK_MATERIAL_UNLIT)) {
        return false;
    }
    material_ptr->shading = shading;
    return true;
}

SK_KEEP
sk_material_shading_t sk_material_get_shading(sk_handle_t material)
{
    sk_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shading : SK_MATERIAL_PBR;
}

SK_KEEP
bool sk_material_set_alpha_mode(sk_handle_t material, sk_material_alpha_t mode, float cutoff)
{
    sk_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || mode < SK_MATERIAL_ALPHA_OPAQUE || mode > SK_MATERIAL_ALPHA_BLEND) {
        return false;
    }
    material_ptr->alpha_mode = mode;
    material_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff;
    return true;
}

SK_KEEP
sk_material_alpha_t sk_material_get_alpha_mode(sk_handle_t material)
{
    sk_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->alpha_mode : SK_MATERIAL_ALPHA_OPAQUE;
}

SK_KEEP
bool sk_material_set_double_sided(sk_handle_t material, bool double_sided)
{
    sk_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) {
        return false;
    }
    material_ptr->double_sided = double_sided;
    return true;
}

SK_KEEP
bool sk_material_is_double_sided(sk_handle_t material)
{
    sk_material_t *material_ptr = resolve(material);
    return material_ptr != NULL && material_ptr->double_sided;
}

SK_KEEP
bool sk_material_set_int(sk_handle_t material, const char *name, int value)
{
    sk_material_t *material_ptr = NULL;
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

SK_KEEP
bool sk_material_set_vec2(sk_handle_t material, const char *name, float x, float y)
{
    float *values = lookup_values(material, name, PARAM_VEC2);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    return true;
}

SK_KEEP
bool sk_material_set_float(sk_handle_t material, const char *name, float value)
{
    float *values = lookup_values(material, name, PARAM_FLOAT);
    if (values == NULL) {
        return false;
    }
    values[0] = value;
    return true;
}

SK_KEEP
bool sk_material_set_vec3(sk_handle_t material, const char *name, float x, float y, float z)
{
    float *values = lookup_values(material, name, PARAM_VEC3);
    if (values == NULL) {
        return false;
    }
    values[0] = x;
    values[1] = y;
    values[2] = z;
    return true;
}

SK_KEEP
bool sk_material_set_vec4(sk_handle_t material, const char *name, float x, float y, float z, float w)
{
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

SK_KEEP
bool sk_material_set_color(sk_handle_t material, const char *name, sk_color_t color)
{
    sk_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_VEC3, PARAM_VEC4, &material_ptr);
    const sk_colorf_t c = sk_color_unpack(color);
    float *values;

    if (param == NULL) {
        return false;
    }
    values = (float *)((char *)material_ptr + param->offset);
    values[0] = sk_srgb_to_linear(c.r);
    values[1] = sk_srgb_to_linear(c.g);
    values[2] = sk_srgb_to_linear(c.b);
    if (param->kind == PARAM_VEC4) {
        values[3] = c.a; /* alpha is linear */
    }
    return true;
}

SK_KEEP
bool sk_material_set_texture(sk_handle_t material, const char *name, sk_handle_t texture)
{
    sk_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    sk_handle_t *slot;

    if (param == NULL) {
        return false;
    }
    if (texture != 0 && sk_handle_get_kind(texture) != SK_HANDLE_KIND_TEXTURE) {
        log_warn("material: '%s' needs a texture handle", name);
        return false;
    }
    slot = &material_ptr->textures[param->offset].texture;
    if (*slot != texture) {
        sk_texture_retain(texture); /* before releasing, in case they're the same resource */
        sk_texture_release(*slot);
        *slot = texture;
    }
    return true;
}

SK_KEEP
bool sk_material_set_texture_sampling(sk_handle_t material, const char *name, sk_texture_wrap_t wrap_u,
                                      sk_texture_wrap_t wrap_v, sk_texture_filter_t filter)
{
    sk_material_t *material_ptr = NULL;
    const param_t *param = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr);
    sk_material_texture_t *texture;

    if (param == NULL) {
        return false;
    }
    if (wrap_u < SK_TEXTURE_WRAP_REPEAT || wrap_u > SK_TEXTURE_WRAP_MIRROR || wrap_v < SK_TEXTURE_WRAP_REPEAT ||
        wrap_v > SK_TEXTURE_WRAP_MIRROR || filter < SK_TEXTURE_FILTER_LINEAR || filter > SK_TEXTURE_FILTER_NEAREST) {
        log_warn("material: invalid sampling for '%s'", name);
        return false;
    }
    texture = &material_ptr->textures[param->offset];
    texture->wrap_u = wrap_u;
    texture->wrap_v = wrap_v;
    texture->filter = filter;
    return true;
}
