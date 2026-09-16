#include "sk_light.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "sk_logger.h"

#define MAX_LIGHTS 256

/* Light object: parameters as set through the API. Resolved into
 * sk_scene_light_t (world-space, radiance, cone cosines) when a scene draws. */
typedef struct {
    sk_light_type_t type;
    sk_handle_t color;
    float intensity;
    vec3_t position;
    vec3_t direction;
    float range;
    float inner_degrees;
    float outer_degrees;
    bool enabled;
} sk_light_t;

static sk_light_t sk_lights[MAX_LIGHTS];
static sk_handle_pool_t sk_light_pool;
static uint16_t sk_light_free_indices[MAX_LIGHTS];
static uint16_t sk_light_generations[MAX_LIGHTS];
static unsigned char sk_light_occupied[MAX_LIGHTS];

static sk_light_env_t sk_light_envs[SK_MAX_LIGHT_ENVS];
static int sk_light_env_count;
static int sk_light_env_current_index = -1;
static bool sk_light_env_full_logged;

void sk_light_init(void)
{
    memset(sk_lights, 0, sizeof(sk_lights));
    sk_handle_pool_init(&sk_light_pool, SK_HANDLE_KIND_LIGHT, MAX_LIGHTS, sk_light_free_indices,
                        MAX_LIGHTS, sk_light_generations, sk_light_occupied);
    sk_light_end_frame();
}

void sk_light_deinit(void)
{
    sk_handle_pool_reset(&sk_light_pool);
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
        log_error("MAX_LIGHTS reached (%d)", MAX_LIGHTS);
        return 0;
    }
    sk_handle_pool_resolve(&sk_light_pool, handle, &index);
    sk_lights[index] = (sk_light_t){
        .type = type,
        .color = 0,
        .intensity = 1.0f,
        .direction = {0.0f, -1.0f, 0.0f},
        .range = 0.0f,
        .inner_degrees = 30.0f,
        .outer_degrees = 45.0f,
        .enabled = true,
    };
    return handle;
}

SK_KEEP
void sk_light_destroy(sk_handle_t light)
{
    if (resolve(light) != NULL) {
        sk_handle_pool_free(&sk_light_pool, light);
    }
}

SK_KEEP
bool sk_light_set_color(sk_handle_t light, sk_handle_t color)
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
bool sk_light_set_spot_cone(sk_handle_t light, float inner_degrees, float outer_degrees)
{
    sk_light_t *light_ptr = resolve(light);
    if (light_ptr == NULL) {
        return false;
    }
    outer_degrees = outer_degrees < 0.0f ? 0.0f : (outer_degrees > 90.0f ? 90.0f : outer_degrees);
    inner_degrees = inner_degrees < 0.0f ? 0.0f : (inner_degrees > outer_degrees ? outer_degrees : inner_degrees);
    light_ptr->inner_degrees = inner_degrees;
    light_ptr->outer_degrees = outer_degrees;
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
    color_t color;

    /* resolve quietly: scenes check every member, most of which aren't lights */
    if (out == NULL || !sk_handle_pool_resolve(&sk_light_pool, light, &index)) {
        return false;
    }
    light_ptr = &sk_lights[index];
    if (!light_ptr->enabled) {
        return false;
    }
    color = sk_color_get(light_ptr->color);
    *out = (sk_scene_light_t){
        .type = (int)light_ptr->type,
        .radiance = {color.r * light_ptr->intensity, color.g * light_ptr->intensity,
                     color.b * light_ptr->intensity},
        .position = light_ptr->position,
        .direction = light_ptr->direction,
        .range = light_ptr->range,
        .cos_inner = cosf(light_ptr->inner_degrees * SK_DEG2RAD),
        .cos_outer = cosf(light_ptr->outer_degrees * SK_DEG2RAD),
    };
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
