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
 * wgr_scene_light_t (world-space, radiance, cone cosines) when a scene draws. */
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
static wgr_handle_pool_t wgr_light_pool;

static wgr_light_env_t wgr_light_envs[WGR_MAX_LIGHT_ENVS];
static int wgr_light_env_count;
static int wgr_light_env_current_index = -1;
static bool wgr_light_env_full_logged;

void wgr_light_init(void)
{
    wgr_scene_hooks.scene_light = wgr_light_get_scene_light;
    wgr_scene_hooks.light_env_push = wgr_light_env_push;
    wgr_scene_hooks.light_env_set_current = wgr_light_env_set_current;
    wgr_scene_hooks.light_env_get = wgr_light_env_get;
    if (!wgr_handle_pool_init(&wgr_light_pool, WGR_HANDLE_KIND_LIGHT, "light", (void **)&wgr_lights,
                             sizeof(wgr_light_t), LIGHTS_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("light: out of memory");
    }
    wgr_light_end_frame();
}

void wgr_light_deinit(void)
{
    wgr_scene_hooks.scene_light = NULL;
    wgr_scene_hooks.light_env_push = NULL;
    wgr_scene_hooks.light_env_set_current = NULL;
    wgr_scene_hooks.light_env_get = NULL;
    wgr_handle_pool_destroy(&wgr_light_pool);
    wgr_light_end_frame();
}

static wgr_light_t *resolve(wgr_handle_t light)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_light_pool, light, &index)) {
        if (light != 0) {
            log_warn("Invalid light handle (%u)", (unsigned int)light);
        }
        return NULL;
    }
    return &wgr_lights[index];
}

/* ------------------------------------------------------------ public API ---- */

WGR_KEEP
wgr_handle_t wgr_light_create(wgr_light_type_t type)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    if (type != WGR_LIGHT_DIRECTIONAL && type != WGR_LIGHT_POINT && type != WGR_LIGHT_SPOT) {
        log_error("wgr_light_create: unknown light type %d", (int)type);
        return 0;
    }
    handle = wgr_handle_pool_alloc(&wgr_light_pool);
    if (handle == 0) {
        log_error("light: pool full (%u)", (unsigned)wgr_light_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_light_pool, handle, &index);
    wgr_lights[index] = (wgr_light_t){
        .type = type,
        .color = WGR_COLOR_WHITE,
        .intensity = 1.0f,
        .direction = {0.0f, -1.0f, 0.0f},
        .range = 0.0f,
        .inner_angle = 30.0f * WGR_DEG2RAD,
        .outer_angle = 45.0f * WGR_DEG2RAD,
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

WGR_KEEP
void wgr_light_destroy(wgr_handle_t light)
{
    if (resolve(light) != NULL) {
        wgr_scene_forget(light);
        wgr_handle_pool_free(&wgr_light_pool, light);
    }
}

WGR_KEEP
bool wgr_light_set_color(wgr_handle_t light, wgr_color_t color)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->color = color;
    return true;
}

WGR_KEEP
bool wgr_light_set_intensity(wgr_handle_t light, float intensity)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

WGR_KEEP
bool wgr_light_set_position(wgr_handle_t light, float x, float y, float z)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->position = (vec3_t){x, y, z};
    return true;
}

WGR_KEEP
bool wgr_light_set_direction(wgr_handle_t light, float x, float y, float z)
{
    wgr_light_t *light_ptr = resolve(light);
    vec3_t direction = wgr_v3_norm((vec3_t){x, y, z});
    if (light_ptr == NULL || (direction.x == 0.0f && direction.y == 0.0f && direction.z == 0.0f)) {
        return false; /* a zero vector has no direction */
    }
    light_ptr->direction = direction;
    return true;
}

WGR_KEEP
bool wgr_light_set_range(wgr_handle_t light, float range)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->range = range > 0.0f ? range : 0.0f;
    return true;
}

WGR_KEEP
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

WGR_KEEP
bool wgr_light_set_enabled(wgr_handle_t light, bool enabled)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->enabled = enabled;
    return true;
}

WGR_KEEP
bool wgr_light_is_enabled(wgr_handle_t light)
{
    wgr_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->enabled;
}

/* -------------------------------------------------------------- internal ---- */

bool wgr_light_get_scene_light(wgr_handle_t light, wgr_scene_light_t *out)
{
    uint16_t index = 0;
    const wgr_light_t *light_ptr;
    wgr_colorf_t color;

    /* resolve quietly: scenes check every member, most of which aren't lights */
    if (out == NULL || !wgr_handle_pool_resolve(&wgr_light_pool, light, &index)) {
        return false;
    }
    light_ptr = &wgr_lights[index];
    if (!light_ptr->enabled) {
        return false;
    }
    color = wgr_color_unpack(light_ptr->color);
    *out = (wgr_scene_light_t){
        .type = (int)light_ptr->type,
        .radiance = {wgr_srgb_to_linear(color.r) * light_ptr->intensity,
                     wgr_srgb_to_linear(color.g) * light_ptr->intensity,
                     wgr_srgb_to_linear(color.b) * light_ptr->intensity},
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
        .shadow_tint = {wgr_srgb_to_linear(wgr_color_unpack(light_ptr->shadow_color).r),
                        wgr_srgb_to_linear(wgr_color_unpack(light_ptr->shadow_color).g),
                        wgr_srgb_to_linear(wgr_color_unpack(light_ptr->shadow_color).b)},
    };
    return true;
}

/* A power of two in range: the map size the GPU actually gets. */
static int shadow_map_size(int size)
{
    int rounded = WGR_SHADOW_MAP_SIZE_MIN;
    if (size > WGR_SHADOW_MAP_SIZE_MAX) size = WGR_SHADOW_MAP_SIZE_MAX;
    while (rounded * 2 <= size) rounded *= 2;
    return rounded;
}

bool wgr_light_shadow_set_casts(wgr_handle_t light, bool casts)
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

bool wgr_light_shadow_casts(wgr_handle_t light)
{
    const wgr_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->casts_shadows;
}

bool wgr_light_shadow_set_distance(wgr_handle_t light, float distance)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || !(distance > 0.0f)) return false;
    light_ptr->shadow_distance = distance;
    return true;
}

bool wgr_light_shadow_set_map_size(wgr_handle_t light, int size)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || size < WGR_SHADOW_MAP_SIZE_MIN) return false;
    light_ptr->shadow_map_size = shadow_map_size(size);
    return true;
}

bool wgr_light_shadow_set_strength(wgr_handle_t light, float strength)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || strength < 0.0f || strength > 1.0f) return false;
    light_ptr->shadow_strength = strength;
    return true;
}

bool wgr_light_shadow_set_color(wgr_handle_t light, wgr_color_t color)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    light_ptr->shadow_color = color;
    return true;
}

bool wgr_light_shadow_set_bias(wgr_handle_t light, float constant, float slope)
{
    wgr_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || constant < 0.0f || slope < 0.0f) return false;
    light_ptr->shadow_bias_constant = constant;
    light_ptr->shadow_bias_slope = slope;
    return true;
}

float wgr_light_attenuation(float distance, float range)
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

float wgr_light_spot_factor(float cos_angle, float cos_inner, float cos_outer)
{
    float t;
    if (cos_inner - cos_outer <= 1e-6f) {
        return cos_angle >= cos_outer ? 1.0f : 0.0f;
    }
    t = (cos_angle - cos_outer) / (cos_inner - cos_outer);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t); /* smoothstep */
}

float wgr_light_luminance(vec3_t rgb)
{
    return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Estimated contribution of `light` to a model with world bounds [bmin, bmax];
 * 0 means it can't reach the model. */
static float score_light(const wgr_scene_light_t *light, vec3_t bmin, vec3_t bmax)
{
    float score = wgr_light_luminance(light->radiance);
    vec3_t nearest, to_nearest, center, to_center;
    float distance, center_distance;

    if (score <= 0.0f || light->type == 0 /* WGR_LIGHT_DIRECTIONAL */) {
        return score;
    }
    nearest = (vec3_t){clampf(light->position.x, bmin.x, bmax.x), clampf(light->position.y, bmin.y, bmax.y),
                       clampf(light->position.z, bmin.z, bmax.z)};
    to_nearest = wgr_v3_sub(nearest, light->position);
    distance = sqrtf(to_nearest.x * to_nearest.x + to_nearest.y * to_nearest.y + to_nearest.z * to_nearest.z);
    if (light->range > 0.0f && distance >= light->range) {
        return 0.0f;
    }
    score *= wgr_light_attenuation(distance, light->range);

    if (light->type == 2 /* WGR_LIGHT_SPOT */ && distance > 0.0f) {
        center = (vec3_t){(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
        to_center = wgr_v3_sub(center, light->position);
        center_distance = sqrtf(to_center.x * to_center.x + to_center.y * to_center.y + to_center.z * to_center.z);
        if (center_distance > 1e-6f) {
            float cos_angle = (to_center.x * light->direction.x + to_center.y * light->direction.y +
                               to_center.z * light->direction.z) / center_distance;
            score *= wgr_light_spot_factor(cos_angle, light->cos_inner, light->cos_outer);
        }
    }
    return score;
}

int wgr_light_select(const wgr_light_env_t *env, vec3_t world_min, vec3_t world_max,
                    int *out_indices, int max_out)
{
    float scores[WGR_MAX_SCENE_LIGHTS];
    int count = 0;

    if (env == NULL || out_indices == NULL || max_out <= 0) {
        return 0;
    }
    if (max_out > WGR_MAX_SCENE_LIGHTS) {
        max_out = WGR_MAX_SCENE_LIGHTS;
    }
    for (int i = 0; i < env->count && i < WGR_MAX_SCENE_LIGHTS; i++) {
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

int wgr_light_env_push(const wgr_light_env_t *env)
{
    if (env == NULL) {
        return -1;
    }
    if (wgr_light_env_count >= WGR_MAX_LIGHT_ENVS) {
        if (!wgr_light_env_full_logged) {
            log_warn("lighting: more than %d scene draws in one frame; extra scenes render unlit",
                     WGR_MAX_LIGHT_ENVS);
            wgr_light_env_full_logged = true;
        }
        return -1;
    }
    wgr_light_envs[wgr_light_env_count] = *env;
    return wgr_light_env_count++;
}

const wgr_light_env_t *wgr_light_env_get(int index)
{
    return index >= 0 && index < wgr_light_env_count ? &wgr_light_envs[index] : NULL;
}

void wgr_light_env_set_current(int index)
{
    wgr_light_env_current_index = index;
}

int wgr_light_env_current(void)
{
    return wgr_light_env_current_index;
}

void wgr_light_end_frame(void)
{
    wgr_light_env_count = 0;
    wgr_light_env_current_index = -1;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_light_module = {.name = "light", .order = 20, .init = wgr_light_init, .deinit = wgr_light_deinit, .end_frame = wgr_light_end_frame};
WGR_MODULE(wgr_light_module)
