#ifndef SK_INTERNAL_LIGHT_H
#define SK_INTERNAL_LIGHT_H

#include <stdbool.h>

#include "internal/sk_math.h"
#include "sk_handle.h"
#include "sk_types.h"

/* Internal lighting data shared by sk_scene (which collects a scene's lights),
 * sk_model (which picks lights per placement and uploads them) and the model
 * shader. See docs/PLAN-lighting.md. */

#define SK_MAX_DRAW_LIGHTS 8   /* lights per model placement (shader array size) */
#define SK_MAX_SCENE_LIGHTS 64 /* enabled lights considered per scene draw */
#define SK_MAX_LIGHT_ENVS 16   /* lighting environments (scene draws) per frame */

void sk_light_init(void);
void sk_light_deinit(void);

/* A light resolved for shading, in world space. */
typedef struct {
    int type;              /* sk_light_type_t */
    vec3_t radiance;       /* color rgb * intensity */
    vec3_t position;
    vec3_t direction;      /* normalized, the way the light travels */
    float range;           /* 0 = unlimited */
    float cos_inner;
    float cos_outer;
    /* shadows (docs/PLAN-shadows.md); casts is false unless this light has them on */
    bool casts_shadows;
    float shadow_distance;
    int shadow_map_size;
    float shadow_bias_constant, shadow_bias_slope;
    float shadow_strength;  /* how much of this light a shadow blocks (0..1) */
    vec3_t shadow_tint;     /* linear rgb mixed into what a shadow leaves (black: none) */
} sk_scene_light_t;

/* The lights and ambient a scene draw provides to the models drawn in it. */
typedef struct {
    sk_scene_light_t lights[SK_MAX_SCENE_LIGHTS];
    int count;
    vec3_t ambient; /* color rgb * intensity */
    /* environment lighting and output (docs/PLAN-environment.md) */
    sk_handle_t environment; /* 0 = none */
    float environment_intensity;
    float environment_rotation; /* radians around +y */
    int tonemap;                /* sk_tonemap_t */
    float exposure;             /* stops */
    int shadow_light;           /* index into lights of the one casting shadows, -1 none */
} sk_light_env_t;

/* Resolve a light handle. False if the handle is invalid, not a light, or disabled. */
bool sk_light_get_scene_light(sk_handle_t light, sk_scene_light_t *out);

/* A light's shadow settings. The public functions for these live in the shadow module
 * (src/sk_shadow.c), so a program that asks for shadows links it; a program that never
 * does doesn't carry the depth pass at all. */
bool sk_light_shadow_set_casts(sk_handle_t light, bool casts);
bool sk_light_shadow_casts(sk_handle_t light);
bool sk_light_shadow_set_distance(sk_handle_t light, float distance);
bool sk_light_shadow_set_map_size(sk_handle_t light, int size);
bool sk_light_shadow_set_bias(sk_handle_t light, float constant, float slope);
bool sk_light_shadow_set_strength(sk_handle_t light, float strength);
bool sk_light_shadow_set_color(sk_handle_t light, sk_color_t color);

/* Shading helpers; the model shader implements the same formulas. */
float sk_light_attenuation(float distance, float range);
float sk_light_spot_factor(float cos_angle, float cos_inner, float cos_outer);
float sk_light_luminance(vec3_t rgb);

/* Pick up to `max_out` lights from `env` for a model whose world-space bounds are
 * [world_min, world_max], strongest estimated contribution first. Writes indices
 * into env->lights and returns how many. Pure; exposed for tests. */
int sk_light_select(const sk_light_env_t *env, vec3_t world_min, vec3_t world_max,
                    int *out_indices, int max_out);

/* Per-frame lighting environments. A scene draw pushes one and makes it current;
 * model draws queued while it is current reference it by index. -1 = unlit. */
int  sk_light_env_push(const sk_light_env_t *env);
const sk_light_env_t *sk_light_env_get(int index);
void sk_light_env_set_current(int index);
int  sk_light_env_current(void);
void sk_light_end_frame(void);

#endif // SK_INTERNAL_LIGHT_H
