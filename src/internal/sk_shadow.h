#ifndef SK_INTERNAL_SHADOW_H
#define SK_INTERNAL_SHADOW_H

#include <stdbool.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_math.h"
#include "sk_types.h"
#include "sokol_gfx.h"

/* Shadow maps (docs/PLAN-shadows.md): a casting light draws the frame's casters into a
 * depth map before the passes that shade, and the lit shaders compare against it.
 * An optional subsystem — a program that never turns shadows on doesn't link it. */

void sk_shadow_init(void);
void sk_shadow_deinit(void);

/* Where a light's shadow map lives, and how a shader reads it. */
typedef struct {
    bool valid;         /* false: nothing casts this frame; shading ignores the rest */
    sg_view map;
    sg_sampler sampler; /* comparison: sampling it returns "how lit", not a depth */
    sk_mat4_t view_proj; /* world -> the light's clip space */
    int light;          /* which light in the environment casts (index into its lights) */
    float texel;        /* 1 / map size, for the sampling kernel */
    float bias_constant, bias_slope; /* in shadow texels (see sk_light_set_shadow_bias) */
    float bias_scale;                /* texels -> the map's depth units */
    float texel_world;               /* world units one texel covers */
    float strength;   /* how much of the light a shadow blocks (0..1) */
    float tint[3];    /* linear rgb mixed into what a shadow leaves behind */
    float depth_scale, depth_offset; /* clip z -> the depth the map holds */
} sk_shadow_binding_t;

/* What the model and sprite shading reaches shadows through, so they don't depend on
 * this module being linked: it fills these in when it starts and clears them when it
 * stops, and they're NULL otherwise. Defined in sk_render.c. */
typedef struct {
    bool (*get_binding)(int light_env, sk_shadow_binding_t *out);
} sk_shadow_hooks_t;
extern sk_shadow_hooks_t sk_shadow_hooks;

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

#endif // SK_INTERNAL_SHADOW_H
