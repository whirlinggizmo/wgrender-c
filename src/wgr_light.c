#include "wgr_light.h"

#include <math.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_module_internal.h"
#include "wgr_logger.h"

#define LIGHTS_INITIAL 32 /* slots to start with; the pool doubles as needed */

/* Light object: parameters as set through the API. Resolved into
 * wgri_scene_light_t (world-space, radiance, cone cosines) when a scene draws. */
typedef struct {
    wgr_light_type_t type;
    wgr_color_t color;
    float intensity;
    vec3_t position;
    vec3_t direction;
    float range;
    float inner_angle; /* radians, from the spot direction to where the falloff starts */
    float outer_angle; /* radians, to where the light reaches zero */
    bool enabled;
    /* shadows (docs/PLAN-shadows.md) */
    bool casts_shadows;
    float shadow_distance;
    int shadow_map_size;
    float shadow_bias_constant, shadow_bias_slope;
    float shadow_strength;
    wgr_color_t shadow_color;
} wgr_light_t;

#define WGR_SHADOW_DISTANCE_DEFAULT 50.0f
#define WGR_SHADOW_MAP_SIZE_DEFAULT 2048
#define WGR_SHADOW_MAP_SIZE_MIN 256
#define WGR_SHADOW_MAP_SIZE_MAX 4096
#define WGR_SHADOW_BIAS_CONSTANT_DEFAULT 1.0f /* shadow texels */
#define WGR_SHADOW_BIAS_SLOPE_DEFAULT 4.0f

static wgr_light_t *wgr_lights; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_light_pool;

static wgri_light_env_t wgr_light_envs[WGRI_MAX_LIGHT_ENVS];
static int wgr_light_env_count;
static int wgr_light_env_current_index = -1;
static bool wgr_light_env_full_logged;

void wgri_light_init(void)
{
    wgri_scene_hooks.scene_light = wgri_light_get_scene_light;
    wgri_scene_hooks.light_env_push = wgri_light_env_push;
    wgri_scene_hooks.light_env_set_current = wgri_light_env_set_current;
    wgri_scene_hooks.light_env_get = wgri_light_env_get;
    if (!wgri_handle_pool_init(&wgr_light_pool, WGR_HANDLE_KIND_LIGHT, "light", (void **)&wgr_lights,
                             sizeof(wgr_light_t), LIGHTS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("light: out of memory");
    }
    wgri_light_end_frame();
}

void wgri_light_deinit(void)
{
    wgri_scene_hooks.scene_light = NULL;
    wgri_scene_hooks.light_env_push = NULL;
    wgri_scene_hooks.light_env_set_current = NULL;
    wgri_scene_hooks.light_env_get = NULL;
    wgri_handle_pool_destroy(&wgr_light_pool);
    wgri_light_end_frame();
}

static wgr_light_t *resolve(wgr_handle_t light)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_light_pool, light, &index)) {
        if (light != 0) {
            log_warn("Invalid light handle (%u)", (unsigned int)light);
        }
        return NULL;
    }
    return &wgr_lights[index];
}

/* ------------------------------------------------------------ public API ---- */

WGRI_KEEP
wgr_handle_t wgr_light_create(wgr_light_type_t type)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    if (type != WGR_LIGHT_DIRECTIONAL && type != WGR_LIGHT_POINT && type != WGR_LIGHT_SPOT) {
        log_error("wgr_light_create: unknown light type %d", (int)type);
        return 0;
    }
    handle = wgri_handle_pool_alloc(&wgr_light_pool);
    if (handle == 0) {
        log_error("light: pool full (%u)", (unsigned)wgr_light_pool.max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_light_pool, handle, &index);
    wgr_lights[index] = (wgr_light_t){
        .type = type,
        .color = WGR_COLOR_WHITE,
        .intensity = 1.0f,
        .direction = {0.0f, -1.0f, 0.0f},
        .range = 0.0f,
        .inner_angle = 30.0f * WGRI_DEG2RAD,
        .outer_angle = 45.0f * WGRI_DEG2RAD,
        .enabled = true,
        .shadow_distance = WGR_SHADOW_DISTANCE_DEFAULT,
        .shadow_map_size = WGR_SHADOW_MAP_SIZE_DEFAULT,
        .shadow_bias_constant = WGR_SHADOW_BIAS_CONSTANT_DEFAULT,
        .shadow_bias_slope = WGR_SHADOW_BIAS_SLOPE_DEFAULT,
        .shadow_strength = 1.0f,
        .shadow_color = WGR_COLOR_BLACK,
    };
    return handle;
}

WGRI_KEEP
void wgr_light_destroy(wgr_handle_t light)
{
    if (resolve(light) != NULL) {
        wgri_scene_forget(light);
        wgri_handle_pool_free(&wgr_light_pool, light);
    }
}

WGRI_KEEP
bool wgr_light_set_color(wgr_handle_t light, wgr_color_t color)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->color = color;
    return true;
}

WGRI_KEEP
bool wgr_light_set_intensity(wgr_handle_t light, float intensity)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

WGRI_KEEP
bool wgr_light_set_position(wgr_handle_t light, float x, float y, float z)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->position = (vec3_t){x, y, z};
    return true;
}

WGRI_KEEP
bool wgr_light_set_direction(wgr_handle_t light, float x, float y, float z)
{
    wgr_light_t *light_ptr = resolve(light);
    vec3_t direction = wgri_v3_norm((vec3_t){x, y, z});
    if (light_ptr == NULL) return false;
    if (direction.x == 0.0f && direction.y == 0.0f && direction.z == 0.0f) {
        log_warn("wgr_light_set_direction: a zero vector has no direction");
        return false;
    }
    light_ptr->direction = direction;
    return true;
}

WGRI_KEEP
bool wgr_light_set_range(wgr_handle_t light, float range)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->range = range > 0.0f ? range : 0.0f;
    return true;
}

WGRI_KEEP
bool wgr_light_set_spot_cone(wgr_handle_t light, float inner_angle, float outer_angle)
{
    const float half_pi = 1.5707963267948966f;
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    outer_angle = outer_angle < 0.0f ? 0.0f : (outer_angle > half_pi ? half_pi : outer_angle);
    inner_angle = inner_angle < 0.0f ? 0.0f : (inner_angle > outer_angle ? outer_angle : inner_angle);
    light_ptr->inner_angle = inner_angle;
    light_ptr->outer_angle = outer_angle;
    return true;
}

WGRI_KEEP
bool wgr_light_set_enabled(wgr_handle_t light, bool enabled)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->enabled = enabled;
    return true;
}

WGRI_KEEP
bool wgr_light_is_enabled(wgr_handle_t light)
{
    wgr_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->enabled;
}

/* Getters return what the setters stored, so a clamp is something a caller can see. */
#define LIGHT_GETTER(ret, name, field, none)                    \
    WGRI_KEEP ret wgr_light_get_##name(wgr_handle_t light)      \
    {                                                            \
        const wgr_light_t *light_ptr = resolve(light);           \
        return light_ptr != NULL ? light_ptr->field : (none);    \
    }
LIGHT_GETTER(wgr_light_type_t, type, type, WGR_LIGHT_DIRECTIONAL)
LIGHT_GETTER(wgr_color_t, color, color, 0)
LIGHT_GETTER(float, intensity, intensity, 0.0f)
LIGHT_GETTER(vec3_t, position, position, ((vec3_t){0, 0, 0}))
LIGHT_GETTER(vec3_t, direction, direction, ((vec3_t){0, 0, 0}))
LIGHT_GETTER(float, range, range, 0.0f)
LIGHT_GETTER(float, spot_inner_angle, inner_angle, 0.0f)
LIGHT_GETTER(float, spot_outer_angle, outer_angle, 0.0f)
#undef LIGHT_GETTER

/* -------------------------------------------------------------- internal ---- */

bool wgri_light_get_scene_light(wgr_handle_t light, wgri_scene_light_t *out)
{
    uint16_t index = 0;
    const wgr_light_t *light_ptr;
    wgri_colorf_t color;

    /* resolve quietly: scenes check every member, most of which aren't lights */
    if (out == NULL || !wgri_handle_pool_resolve(&wgr_light_pool, light, &index)) {
        return false;
    }
    light_ptr = &wgr_lights[index];
    if (!light_ptr->enabled) {
        return false;
    }
    color = wgri_color_unpack(light_ptr->color);
    *out = (wgri_scene_light_t){
        .type = (int)light_ptr->type,
        .radiance = {wgri_srgb_to_linear(color.r) * light_ptr->intensity,
                     wgri_srgb_to_linear(color.g) * light_ptr->intensity,
                     wgri_srgb_to_linear(color.b) * light_ptr->intensity},
        .position = light_ptr->position,
        .direction = light_ptr->direction,
        .range = light_ptr->range,
        .cos_inner = cosf(light_ptr->inner_angle),
        .cos_outer = cosf(light_ptr->outer_angle),
        .casts_shadows = light_ptr->casts_shadows,
        .shadow_distance = light_ptr->shadow_distance,
        .shadow_map_size = light_ptr->shadow_map_size,
        .shadow_bias_constant = light_ptr->shadow_bias_constant,
        .shadow_bias_slope = light_ptr->shadow_bias_slope,
        .shadow_strength = light_ptr->shadow_strength,
        .shadow_tint = {wgri_srgb_to_linear(wgri_color_unpack(light_ptr->shadow_color).r),
                        wgri_srgb_to_linear(wgri_color_unpack(light_ptr->shadow_color).g),
                        wgri_srgb_to_linear(wgri_color_unpack(light_ptr->shadow_color).b)},
    };
    return true;
}

/* A power of two in range: the map size the GPU actually gets. Clamps both ways --
 * below WGR_SHADOW_MAP_SIZE_MIN rounds up to it. */
static int shadow_map_size(int size)
{
    int rounded = WGR_SHADOW_MAP_SIZE_MIN;
    if (size > WGR_SHADOW_MAP_SIZE_MAX) size = WGR_SHADOW_MAP_SIZE_MAX;
    while (rounded * 2 <= size) rounded *= 2;
    return rounded;
}

bool wgri_light_shadow_set_casts(wgr_handle_t light, bool casts)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    if (casts && light_ptr->type == WGR_LIGHT_POINT) {
        log_warn("wgr_light_set_casts_shadows: point lights don't cast shadows yet — a point light "
                 "needs six maps, one each way (docs/PLAN-shadows.md)");
        return false;
    }
    light_ptr->casts_shadows = casts;
    return true;
}

bool wgri_light_shadow_casts(wgr_handle_t light)
{
    const wgr_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->casts_shadows;
}

bool wgri_light_shadow_set_distance(wgr_handle_t light, float distance)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    if (!(distance > 0.0f)) {
        log_warn("wgr_light_set_shadow_distance: %g: the distance has to be more than 0", distance);
        return false;
    }
    light_ptr->shadow_distance = distance;
    return true;
}

bool wgri_light_shadow_set_map_size(wgr_handle_t light, int size)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    if (size < 1) { /* a size, so <= 0 means nothing; in range it's a fidelity, so clamp */
        log_warn("wgr_light_set_shadow_map_size: %d: a map has at least 1 pixel (256 after clamping)", size);
        return false;
    }
    light_ptr->shadow_map_size = shadow_map_size(size);
    return true;
}

bool wgri_light_shadow_set_strength(wgr_handle_t light, float strength)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    light_ptr->shadow_strength = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength; /* a fraction: clamp */
    return true;
}

bool wgri_light_shadow_set_color(wgr_handle_t light, wgr_color_t color)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    light_ptr->shadow_color = color;
    return true;
}

#define SHADOW_GETTER(ret, name, field)                                 \
    ret wgri_light_shadow_get_##name(wgr_handle_t light)                \
    {                                                                    \
        const wgr_light_t *light_ptr = resolve(light);                   \
        return light_ptr != NULL ? light_ptr->field : (ret)0;            \
    }
SHADOW_GETTER(float, distance, shadow_distance)
SHADOW_GETTER(int, map_size, shadow_map_size)
SHADOW_GETTER(float, strength, shadow_strength)
SHADOW_GETTER(wgr_color_t, color, shadow_color)
SHADOW_GETTER(float, bias_constant, shadow_bias_constant)
SHADOW_GETTER(float, bias_slope, shadow_bias_slope)
#undef SHADOW_GETTER

bool wgri_light_shadow_set_bias(wgr_handle_t light, float constant, float slope)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    if (constant < 0.0f || slope < 0.0f) {
        log_warn("wgr_light_set_shadow_bias: %g, %g: a bias can't be negative", constant, slope);
        return false;
    }
    light_ptr->shadow_bias_constant = constant;
    light_ptr->shadow_bias_slope = slope;
    return true;
}

float wgri_light_attenuation(float distance, float range)
{
    float inverse_square = 1.0f / (distance * distance > 0.01f ? distance * distance : 0.01f);
    float ratio, window;

    if (range <= 0.0f) {
        return inverse_square;
    }
    /* KHR_lights_punctual: smooth window to zero at range */
    ratio = distance / range;
    window = 1.0f - ratio * ratio * ratio * ratio;
    window = window < 0.0f ? 0.0f : (window > 1.0f ? 1.0f : window);
    return window * window * inverse_square;
}

float wgri_light_spot_factor(float cos_angle, float cos_inner, float cos_outer)
{
    float t;
    if (cos_inner - cos_outer <= 1e-6f) {
        return cos_angle >= cos_outer ? 1.0f : 0.0f;
    }
    t = (cos_angle - cos_outer) / (cos_inner - cos_outer);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t); /* smoothstep */
}

float wgri_light_luminance(vec3_t rgb)
{
    return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Estimated contribution of `light` to a model with world bounds [bmin, bmax];
 * 0 means it can't reach the model. */
static float score_light(const wgri_scene_light_t *light, vec3_t bmin, vec3_t bmax)
{
    float score = wgri_light_luminance(light->radiance);
    vec3_t nearest, to_nearest, center, to_center;
    float distance, center_distance;

    if (score <= 0.0f || light->type == 0 /* WGR_LIGHT_DIRECTIONAL */) {
        return score;
    }
    nearest = (vec3_t){clampf(light->position.x, bmin.x, bmax.x), clampf(light->position.y, bmin.y, bmax.y),
                       clampf(light->position.z, bmin.z, bmax.z)};
    to_nearest = wgri_v3_sub(nearest, light->position);
    distance = sqrtf(to_nearest.x * to_nearest.x + to_nearest.y * to_nearest.y + to_nearest.z * to_nearest.z);
    if (light->range > 0.0f && distance >= light->range) {
        return 0.0f;
    }
    score *= wgri_light_attenuation(distance, light->range);

    if (light->type == 2 /* WGR_LIGHT_SPOT */ && distance > 0.0f) {
        center = (vec3_t){(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
        to_center = wgri_v3_sub(center, light->position);
        center_distance = sqrtf(to_center.x * to_center.x + to_center.y * to_center.y + to_center.z * to_center.z);
        if (center_distance > 1e-6f) {
            float cos_angle = (to_center.x * light->direction.x + to_center.y * light->direction.y +
                               to_center.z * light->direction.z) / center_distance;
            score *= wgri_light_spot_factor(cos_angle, light->cos_inner, light->cos_outer);
        }
    }
    return score;
}

int wgri_light_select(const wgri_light_env_t *env, vec3_t world_min, vec3_t world_max,
                    int *out_indices, int max_out)
{
    float scores[WGRI_MAX_SCENE_LIGHTS];
    int count = 0;

    if (env == NULL || out_indices == NULL || max_out <= 0) {
        return 0;
    }
    if (max_out > WGRI_MAX_SCENE_LIGHTS) {
        max_out = WGRI_MAX_SCENE_LIGHTS;
    }
    for (int i = 0; i < env->count && i < WGRI_MAX_SCENE_LIGHTS; i++) {
        float score = score_light(&env->lights[i], world_min, world_max);
        int at;
        if (score <= 0.0f) {
            continue;
        }
        /* insertion into a descending list; equal scores keep scene order */
        at = count;
        while (at > 0 && scores[at - 1] < score) {
            at--;
        }
        if (at >= max_out) {
            continue;
        }
        if (count < max_out) {
            count++;
        }
        for (int j = count - 1; j > at; j--) {
            scores[j] = scores[j - 1];
            out_indices[j] = out_indices[j - 1];
        }
        scores[at] = score;
        out_indices[at] = i;
    }
    return count;
}

int wgri_light_env_push(const wgri_light_env_t *env)
{
    if (env == NULL) {
        return -1;
    }
    if (wgr_light_env_count >= WGRI_MAX_LIGHT_ENVS) {
        if (!wgr_light_env_full_logged) {
            log_warn("lighting: more than %d scene draws in one frame; extra scenes render unlit",
                     WGRI_MAX_LIGHT_ENVS);
            wgr_light_env_full_logged = true;
        }
        return -1;
    }
    wgr_light_envs[wgr_light_env_count] = *env;
    return wgr_light_env_count++;
}

const wgri_light_env_t *wgri_light_env_get(int index)
{
    return index >= 0 && index < wgr_light_env_count ? &wgr_light_envs[index] : NULL;
}

void wgri_light_env_set_current(int index)
{
    wgr_light_env_current_index = index;
}

int wgri_light_env_current(void)
{
    return wgr_light_env_current_index;
}

void wgri_light_end_frame(void)
{
    wgr_light_env_count = 0;
    wgr_light_env_current_index = -1;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_light_module = {.name = "light", .order = 20, .init = wgri_light_init, .deinit = wgri_light_deinit, .end_frame = wgri_light_end_frame};
WGRI_MODULE(wgr_light_module)
