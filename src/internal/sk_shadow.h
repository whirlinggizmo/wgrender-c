#ifndef SK_INTERNAL_SHADOW_H
#define SK_INTERNAL_SHADOW_H

#include <stdbool.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_light.h"
#include "internal/sk_math.h"
#include "sk_types.h"
#include "sokol_gfx.h"

/* Shadow maps (docs/PLAN-shadows.md): a casting light draws the frame's casters into a
 * depth map before the passes that shade, and the lit shaders compare against it.
 * An optional subsystem — a program that never turns shadows on doesn't link it. */

void sk_shadow_init(void);
void sk_shadow_deinit(void);

/* One casting light's map: a layer of the shadow array, and how a shader reads it. */
typedef struct {
    int light;           /* which light in the environment casts (index into its lights) */
    sk_mat4_t view_proj; /* world -> that light's clip space */
    float texel;         /* 1 / map size, for the sampling kernel */
    float bias_constant, bias_slope; /* in shadow texels (see sk_light_set_shadow_bias) */
    float bias_scale;                /* texels -> the map's depth units */
    float texel_world;               /* world units one texel covers */
    float strength;   /* how much of the light a shadow blocks (0..1) */
    float tint[3];    /* linear rgb mixed into what a shadow leaves behind */
} sk_shadow_light_t;

/* What a draw needs to shade with this frame's shadows: the array every casting light
 * shares, and a layer's worth of numbers for each of them. */
typedef struct {
    bool valid;         /* false: nothing casts this frame; shading ignores the rest */
    sg_view map;        /* the depth array, one layer per casting light */
    sg_sampler sampler; /* comparison: sampling it returns "how lit", not a depth */
    int count;          /* casting lights, 0..SK_MAX_SHADOW_LIGHTS */
    sk_shadow_light_t lights[SK_MAX_SHADOW_LIGHTS];
    float depth_scale, depth_offset; /* clip z -> the depth the map holds (per backend) */
    bool flipped;                    /* the map's first row is its top, not its bottom */
} sk_shadow_binding_t;

/* What the model and sprite shading reaches shadows through, so they don't depend on
 * this module being linked: it fills these in when it starts and clears them when it
 * stops, and they're NULL otherwise. Defined in sk_render.c. */
typedef struct {
    bool (*get_binding)(int light_env, sk_shadow_binding_t *out);
} sk_shadow_hooks_t;
extern sk_shadow_hooks_t sk_shadow_hooks;

/* Which layer of the map a light in the lighting environment casts into, or -1 when
 * it casts no shadow. `env_light` indexes sk_light_env_t.lights. Pure. */
static inline int sk_shadow_slot_of(const sk_shadow_binding_t *binding, int env_light)
{
    if (binding == NULL || !binding->valid) {
        return -1;
    }
    for (int i = 0; i < binding->count; i++) {
        if (binding->lights[i].light == env_light) return i;
    }
    return -1;
}

/* Copy this frame's shadows into the per-slot arrays a shader reads (the same layout
 * in every shading path: models, sprites and custom shaders). Pure. */
static inline void sk_shadow_fill_uniforms(const sk_shadow_binding_t *binding, float mat[][16], float params[][4],
                                           float tint[][4], float extra[][4], float map[4])
{
    if (binding == NULL || !binding->valid) {
        return;
    }
    for (int i = 0; i < binding->count && i < SK_MAX_SHADOW_LIGHTS; i++) {
        const sk_shadow_light_t *light = &binding->lights[i];
        for (int k = 0; k < 16; k++) mat[i][k] = light->view_proj.m[k];
        params[i][0] = light->texel;
        params[i][1] = light->texel_world;
        params[i][2] = light->bias_constant;
        params[i][3] = light->bias_slope;
        tint[i][0] = light->tint[0];
        tint[i][1] = light->tint[1];
        tint[i][2] = light->tint[2];
        tint[i][3] = light->bias_scale;
        extra[i][0] = light->strength;
    }
    map[0] = binding->depth_scale;
    map[1] = binding->depth_offset;
    map[2] = binding->flipped ? 1.0f : 0.0f;
}

/* An orthographic projection for a light, in the depth range the backend clips to:
 * -1..1 for GL and WebGL2, 0..1 for WebGPU. `zero_to_one` picks it (pure; exposed for
 * tests, where the backend is the dummy one). */
sk_mat4_t sk_shadow_ortho(float l, float r, float b, float t, float n, float f, bool zero_to_one);

/* A directional light's view of what the camera can see, out to `distance`: the
 * world -> light clip matrix and the world size of one shadow texel. The fit is
 * snapped to whole texels, so the shadow doesn't crawl as the camera moves, and its
 * near plane is pulled back by `pullback` so casters behind the camera still cast.
 * Pure; exposed for tests. */
typedef struct {
    sk_mat4_t view_proj;
    float texel_world; /* world units one shadow texel covers */
    float depth_range; /* world units the map's 0..1 depth spans */
} sk_shadow_fit_t;

sk_shadow_fit_t sk_shadow_fit_directional(const sk_camera3d_t *cam, float aspect, vec3_t light_direction,
                                          float distance, int map_size, float pullback, bool zero_to_one);

/* A spot light's view of its own cone: a perspective frustum from `position` along
 * `direction`, wide enough for the outer cone angle, reaching `distance`. `near_plane`
 * keeps the projection sane close to the lamp. Pure; exposed for tests. */
sk_shadow_fit_t sk_shadow_fit_spot(vec3_t position, vec3_t direction, float cos_outer, float distance,
                                   float near_plane, int map_size, bool zero_to_one);

#endif // SK_INTERNAL_SHADOW_H
