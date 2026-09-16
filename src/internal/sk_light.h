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
} sk_scene_light_t;

/* The lights and ambient a scene draw provides to the models drawn in it. */
typedef struct {
    sk_scene_light_t lights[SK_MAX_SCENE_LIGHTS];
    int count;
    vec3_t ambient; /* color rgb * intensity */
} sk_light_env_t;

/* Resolve a light handle. False if the handle is invalid, not a light, or disabled. */
bool sk_light_get_scene_light(sk_handle_t light, sk_scene_light_t *out);

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
