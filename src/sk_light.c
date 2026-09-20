#include "sk_light.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "sk_color.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "internal/sk_scene.h"
#include "internal/sk_module.h"
#include "sk_logger.h"

#define LIGHTS_INITIAL 32 /* slots to start with; the pool doubles as needed */

/* Light object: parameters as set through the API. Resolved into
 * sk_scene_light_t (world-space, radiance, cone cosines) when a scene draws. */
typedef struct {
    sk_light_type_t type;
    sk_color_t color;
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
    sk_color_t shadow_color;
} sk_light_t;

#define SK_SHADOW_DISTANCE_DEFAULT 50.0f
#define SK_SHADOW_MAP_SIZE_DEFAULT 2048
#define SK_SHADOW_MAP_SIZE_MIN 256
#define SK_SHADOW_MAP_SIZE_MAX 4096
#define SK_SHADOW_BIAS_CONSTANT_DEFAULT 1.0f /* shadow texels */
#define SK_SHADOW_BIAS_SLOPE_DEFAULT 4.0f

static sk_light_t *sk_lights; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_light_pool;

static sk_light_env_t sk_light_envs[SK_MAX_LIGHT_ENVS];
static int sk_light_env_count;
static int sk_light_env_current_index = -1;
static bool sk_light_env_full_logged;

void sk_light_init(void)
{
    sk_scene_hooks.scene_light = sk_light_get_scene_light;
    sk_scene_hooks.light_env_push = sk_light_env_push;
    sk_scene_hooks.light_env_set_current = sk_light_env_set_current;
    sk_scene_hooks.light_env_get = sk_light_env_get;
    if (!sk_handle_pool_init(&sk_light_pool, SK_HANDLE_KIND_LIGHT, "light", (void **)&sk_lights,
                             sizeof(sk_light_t), LIGHTS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("light: out of memory");
    }
    sk_light_end_frame();
}

void sk_light_deinit(void)
{
    sk_scene_hooks.scene_light = NULL;
    sk_scene_hooks.light_env_push = NULL;
    sk_scene_hooks.light_env_set_current = NULL;
    sk_scene_hooks.light_env_get = NULL;
    sk_handle_pool_destroy(&sk_light_pool);
    sk_light_end_frame();
}

static sk_light_t *resolve(sk_handle_t light)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_light_pool, light, &index)) {
        if (light != 0) {
            log_warn("Invalid light handle (%u)", (unsigned int)light);
        }
        return NULL;
    }
    return &sk_lights[index];
}

/* ------------------------------------------------------------ public API ---- */

SK_KEEP
sk_handle_t sk_light_create(sk_light_type_t type)
{
    sk_handle_t handle;
    uint16_t index = 0;

    if (type != SK_LIGHT_DIRECTIONAL && type != SK_LIGHT_POINT && type != SK_LIGHT_SPOT) {
        log_error("sk_light_create: unknown light type %d", (int)type);
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_light_pool);
    if (handle == 0) {
        log_error("light: pool full (%u)", (unsigned)sk_light_pool.max - 1u);
        return 0;
    }
    sk_handle_pool_resolve(&sk_light_pool, handle, &index);
    sk_lights[index] = (sk_light_t){
        .type = type,
        .color = SK_COLOR_WHITE,
        .intensity = 1.0f,
        .direction = {0.0f, -1.0f, 0.0f},
        .range = 0.0f,
        .inner_angle = 30.0f * SK_DEG2RAD,
        .outer_angle = 45.0f * SK_DEG2RAD,
        .enabled = true,
        .shadow_distance = SK_SHADOW_DISTANCE_DEFAULT,
        .shadow_map_size = SK_SHADOW_MAP_SIZE_DEFAULT,
        .shadow_bias_constant = SK_SHADOW_BIAS_CONSTANT_DEFAULT,
        .shadow_bias_slope = SK_SHADOW_BIAS_SLOPE_DEFAULT,
        .shadow_strength = 1.0f,
        .shadow_color = SK_COLOR_BLACK,
    };
    return handle;
}

SK_KEEP
void sk_light_destroy(sk_handle_t light)
{
    if (resolve(light) != NULL) {
        sk_scene_forget(light);
        sk_handle_pool_free(&sk_light_pool, light);
    }
}

SK_KEEP
bool sk_light_set_color(sk_handle_t light, sk_color_t color)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->color = color;
    return true;
}

SK_KEEP
bool sk_light_set_intensity(sk_handle_t light, float intensity)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

SK_KEEP
bool sk_light_set_position(sk_handle_t light, float x, float y, float z)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->position = (vec3_t){x, y, z};
    return true;
}

SK_KEEP
bool sk_light_set_direction(sk_handle_t light, float x, float y, float z)
{
    sk_light_t *light_ptr = resolve(light);
    vec3_t direction = sk_v3_norm((vec3_t){x, y, z});
    if (light_ptr == NULL || (direction.x == 0.0f && direction.y == 0.0f && direction.z == 0.0f)) {
        return false; /* a zero vector has no direction */
    }
    light_ptr->direction = direction;
    return true;
}

SK_KEEP
bool sk_light_set_range(sk_handle_t light, float range)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->range = range > 0.0f ? range : 0.0f;
    return true;
}

SK_KEEP
bool sk_light_set_spot_cone(sk_handle_t light, float inner_angle, float outer_angle)
{
    const float half_pi = 1.5707963267948966f;
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    outer_angle = outer_angle < 0.0f ? 0.0f : (outer_angle > half_pi ? half_pi : outer_angle);
    inner_angle = inner_angle < 0.0f ? 0.0f : (inner_angle > outer_angle ? outer_angle : inner_angle);
    light_ptr->inner_angle = inner_angle;
    light_ptr->outer_angle = outer_angle;
    return true;
}

SK_KEEP
bool sk_light_set_enabled(sk_handle_t light, bool enabled)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    light_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_light_is_enabled(sk_handle_t light)
{
    sk_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->enabled;
}

/* -------------------------------------------------------------- internal ---- */

bool sk_light_get_scene_light(sk_handle_t light, sk_scene_light_t *out)
{
    uint16_t index = 0;
    const sk_light_t *light_ptr;
    sk_colorf_t color;

    /* resolve quietly: scenes check every member, most of which aren't lights */
    if (out == NULL || !sk_handle_pool_resolve(&sk_light_pool, light, &index)) {
        return false;
    }
    light_ptr = &sk_lights[index];
    if (!light_ptr->enabled) {
        return false;
    }
    color = sk_color_unpack(light_ptr->color);
    *out = (sk_scene_light_t){
        .type = (int)light_ptr->type,
        .radiance = {sk_srgb_to_linear(color.r) * light_ptr->intensity,
                     sk_srgb_to_linear(color.g) * light_ptr->intensity,
                     sk_srgb_to_linear(color.b) * light_ptr->intensity},
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
        .shadow_tint = {sk_srgb_to_linear(sk_color_unpack(light_ptr->shadow_color).r),
                        sk_srgb_to_linear(sk_color_unpack(light_ptr->shadow_color).g),
                        sk_srgb_to_linear(sk_color_unpack(light_ptr->shadow_color).b)},
    };
    return true;
}

/* A power of two in range: the map size the GPU actually gets. */
static int shadow_map_size(int size)
{
    int rounded = SK_SHADOW_MAP_SIZE_MIN;
    if (size > SK_SHADOW_MAP_SIZE_MAX) size = SK_SHADOW_MAP_SIZE_MAX;
    while (rounded * 2 <= size) rounded *= 2;
    return rounded;
}

bool sk_light_shadow_set_casts(sk_handle_t light, bool casts)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    if (casts && light_ptr->type == SK_LIGHT_POINT) {
        log_warn("sk_light_set_casts_shadows: point lights don't cast shadows yet — a point light "
                 "needs six maps, one each way (docs/PLAN-shadows.md)");
        return false;
    }
    light_ptr->casts_shadows = casts;
    return true;
}

bool sk_light_shadow_casts(sk_handle_t light)
{
    const sk_light_t *light_ptr = resolve(light);
    return light_ptr != NULL && light_ptr->casts_shadows;
}

bool sk_light_shadow_set_distance(sk_handle_t light, float distance)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || !(distance > 0.0f)) return false;
    light_ptr->shadow_distance = distance;
    return true;
}

bool sk_light_shadow_set_map_size(sk_handle_t light, int size)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || size < SK_SHADOW_MAP_SIZE_MIN) return false;
    light_ptr->shadow_map_size = shadow_map_size(size);
    return true;
}

bool sk_light_shadow_set_strength(sk_handle_t light, float strength)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || strength < 0.0f || strength > 1.0f) return false;
    light_ptr->shadow_strength = strength;
    return true;
}

bool sk_light_shadow_set_color(sk_handle_t light, sk_color_t color)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) return false;
    light_ptr->shadow_color = color;
    return true;
}

bool sk_light_shadow_set_bias(sk_handle_t light, float constant, float slope)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL || constant < 0.0f || slope < 0.0f) return false;
    light_ptr->shadow_bias_constant = constant;
    light_ptr->shadow_bias_slope = slope;
    return true;
}

float sk_light_attenuation(float distance, float range)
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

float sk_light_spot_factor(float cos_angle, float cos_inner, float cos_outer)
{
    float t;
    if (cos_inner - cos_outer <= 1e-6f) {
        return cos_angle >= cos_outer ? 1.0f : 0.0f;
    }
    t = (cos_angle - cos_outer) / (cos_inner - cos_outer);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t); /* smoothstep */
}

float sk_light_luminance(vec3_t rgb)
{
    return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Estimated contribution of `light` to a model with world bounds [bmin, bmax];
 * 0 means it can't reach the model. */
static float score_light(const sk_scene_light_t *light, vec3_t bmin, vec3_t bmax)
{
    float score = sk_light_luminance(light->radiance);
    vec3_t nearest, to_nearest, center, to_center;
    float distance, center_distance;

    if (score <= 0.0f || light->type == 0 /* SK_LIGHT_DIRECTIONAL */) {
        return score;
    }
    nearest = (vec3_t){clampf(light->position.x, bmin.x, bmax.x), clampf(light->position.y, bmin.y, bmax.y),
                       clampf(light->position.z, bmin.z, bmax.z)};
    to_nearest = sk_v3_sub(nearest, light->position);
    distance = sqrtf(to_nearest.x * to_nearest.x + to_nearest.y * to_nearest.y + to_nearest.z * to_nearest.z);
    if (light->range > 0.0f && distance >= light->range) {
        return 0.0f;
    }
    score *= sk_light_attenuation(distance, light->range);

    if (light->type == 2 /* SK_LIGHT_SPOT */ && distance > 0.0f) {
        center = (vec3_t){(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
        to_center = sk_v3_sub(center, light->position);
        center_distance = sqrtf(to_center.x * to_center.x + to_center.y * to_center.y + to_center.z * to_center.z);
        if (center_distance > 1e-6f) {
            float cos_angle = (to_center.x * light->direction.x + to_center.y * light->direction.y +
                               to_center.z * light->direction.z) / center_distance;
            score *= sk_light_spot_factor(cos_angle, light->cos_inner, light->cos_outer);
        }
    }
    return score;
}

int sk_light_select(const sk_light_env_t *env, vec3_t world_min, vec3_t world_max,
                    int *out_indices, int max_out)
{
    float scores[SK_MAX_SCENE_LIGHTS];
    int count = 0;

    if (env == NULL || out_indices == NULL || max_out <= 0) {
        return 0;
    }
    if (max_out > SK_MAX_SCENE_LIGHTS) {
        max_out = SK_MAX_SCENE_LIGHTS;
    }
    for (int i = 0; i < env->count && i < SK_MAX_SCENE_LIGHTS; i++) {
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

int sk_light_env_push(const sk_light_env_t *env)
{
    if (env == NULL) {
        return -1;
    }
    if (sk_light_env_count >= SK_MAX_LIGHT_ENVS) {
        if (!sk_light_env_full_logged) {
            log_warn("lighting: more than %d scene draws in one frame; extra scenes render unlit",
                     SK_MAX_LIGHT_ENVS);
            sk_light_env_full_logged = true;
        }
        return -1;
    }
    sk_light_envs[sk_light_env_count] = *env;
    return sk_light_env_count++;
}

const sk_light_env_t *sk_light_env_get(int index)
{
    return index >= 0 && index < sk_light_env_count ? &sk_light_envs[index] : NULL;
}

void sk_light_env_set_current(int index)
{
    sk_light_env_current_index = index;
}

int sk_light_env_current(void)
{
    return sk_light_env_current_index;
}

void sk_light_end_frame(void)
{
    sk_light_env_count = 0;
    sk_light_env_current_index = -1;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/sk_module.h). */
static sk_module_t sk_light_module = {.name = "light", .order = 20, .init = sk_light_init, .deinit = sk_light_deinit, .end_frame = sk_light_end_frame};
SK_MODULE(sk_light_module)
