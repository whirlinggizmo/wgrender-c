#include "internal/sk_shadow.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_light.h"
#include "internal/sk_model.h"
#include "internal/sk_module.h"
#include "internal/sk_render.h"
#include "sk_light.h"
#include "sk_logger.h"

/* Shadow maps (docs/PLAN-shadows.md). Once a frame, before anything is shaded, the
 * casting light looks at the slice of the camera's view in front of it and draws the
 * casters into a depth map; the lit shaders then compare each surface against it.
 *
 * Phase 1 is one directional light: the first casting light a scene draw finds
 * (sk_scene, sk_light_env_t.shadow_light). The map is a depth texture sampled through
 * a comparison sampler, so the GPU does the depth test and filters the result. */

#define SK_SHADOW_PULLBACK 50.0f /* world units the light's near plane is pulled back */

static struct {
    sg_image map;
    sg_view map_view, depth_attachment;
    sg_sampler sampler;
    int size;              /* the map's pixels each way, 0 = none made yet */
    bool unsupported;      /* the backend can't sample a depth texture (said once) */
    sk_shadow_binding_t binding; /* this frame's, for the shaders */
    int env;               /* the lighting environment it was drawn for, -1 none */
} sk_sm;

/* --------------------------------------------------------------- fitting ---- */

sk_mat4_t sk_shadow_ortho(float l, float r, float b, float t, float n, float f, bool zero_to_one)
{
    sk_mat4_t m = {{0}};
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[15] = 1.0f;
    if (zero_to_one) { /* WebGPU clips 0 <= z <= w */
        m.m[10] = -1.0f / (f - n);
        m.m[14] = -n / (f - n);
    } else { /* GL and WebGL2 clip -w <= z <= w */
        m.m[10] = -2.0f / (f - n);
        m.m[14] = -(f + n) / (f - n);
    }
    return m;
}

/* The eight corners of what the camera sees between its near plane and `distance`. */
static void view_corners(const sk_camera3d_t *cam, float aspect, float distance, vec3_t out[8])
{
    const vec3_t forward = sk_v3_norm(sk_v3_sub(cam->target, cam->position));
    const vec3_t right = sk_v3_norm(sk_v3_cross(forward, cam->up));
    const vec3_t up = sk_v3_cross(right, forward);
    const bool ortho = cam->projection == SK_CAMERA3D_ORTHOGRAPHIC;
    const float near_plane = ortho ? 0.0f : SK_CAMERA3D_PERSPECTIVE_NEAR;
    const float planes[2] = {near_plane, distance};

    for (int p = 0; p < 2; p++) {
        const float z = planes[p];
        const float half_height = ortho ? cam->ortho_height * 0.5f : tanf(cam->fov * 0.5f) * z;
        const float half_width = half_height * aspect;
        const vec3_t centre = sk_v3_add(cam->position, sk_v3_scale(forward, z));
        for (int c = 0; c < 4; c++) {
            const float x = (c & 1) ? half_width : -half_width;
            const float y = (c & 2) ? half_height : -half_height;
            out[p * 4 + c] = sk_v3_add(centre, sk_v3_add(sk_v3_scale(right, x), sk_v3_scale(up, y)));
        }
    }
}

sk_shadow_fit_t sk_shadow_fit_directional(const sk_camera3d_t *cam, float aspect, vec3_t light_direction,
                                          float distance, int map_size, float pullback, bool zero_to_one)
{
    vec3_t corners[8];
    vec3_t centre = {0, 0, 0};
    vec3_t dir = sk_v3_norm(light_direction);
    sk_camera3d_t light_cam;
    sk_mat4_t light_view;
    sk_shadow_fit_t fit;
    float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f, min_z = 1e30f, max_z = -1e30f;

    if (!(sk_v3_dot(dir, dir) > 0.0f)) {
        dir = (vec3_t){0.0f, -1.0f, 0.0f};
    }
    if (!(distance > 0.0f)) {
        distance = 1.0f;
    }
    if (map_size < 1) {
        map_size = 1;
    }
    view_corners(cam, aspect, distance, corners);
    for (int i = 0; i < 8; i++) centre = sk_v3_add(centre, corners[i]);
    centre = sk_v3_scale(centre, 1.0f / 8.0f);

    /* look from far enough back along the light that the whole slice is in front */
    light_cam.position = sk_v3_sub(centre, sk_v3_scale(dir, distance + pullback));
    light_cam.target = centre;
    light_cam.up = fabsf(dir.y) > 0.99f ? (vec3_t){0.0f, 0.0f, 1.0f} : (vec3_t){0.0f, 1.0f, 0.0f};
    light_cam.fov = 1.0f;
    light_cam.ortho_height = 1.0f;
    light_cam.projection = SK_CAMERA3D_ORTHOGRAPHIC;
    light_view = sk_camera3d_view(&light_cam);

    for (int i = 0; i < 8; i++) {
        const vec3_t p = sk_mat4_mul_point(light_view, corners[i]);
        if (p.x < min_x) min_x = p.x;
        if (p.x > max_x) max_x = p.x;
        if (p.y < min_y) min_y = p.y;
        if (p.y > max_y) max_y = p.y;
        if (-p.z < min_z) min_z = -p.z; /* the view looks down -z: depth grows that way */
        if (-p.z > max_z) max_z = -p.z;
    }

    /* a square that holds the slice whichever way the camera turns, so its size (and
       so the texel size) doesn't change as it does */
    {
        const float width = max_x - min_x, height = max_y - min_y;
        const float side = width > height ? width : height;
        const float mid_x = (min_x + max_x) * 0.5f, mid_y = (min_y + max_y) * 0.5f;
        const float texel = side / (float)map_size;
        min_x = mid_x - side * 0.5f;
        min_y = mid_y - side * 0.5f;
        /* snap the corner to whole texels: the shadow then stays put as the camera
           moves instead of crawling along its edges */
        if (texel > 0.0f) {
            min_x = floorf(min_x / texel) * texel;
            min_y = floorf(min_y / texel) * texel;
        }
        max_x = min_x + side;
        max_y = min_y + side;
        fit.texel_world = texel;
    }
    fit.depth_range = max_z + pullback;
    fit.view_proj = sk_mat4_mul(sk_shadow_ortho(min_x, max_x, min_y, max_y, 0.0f, fit.depth_range, zero_to_one),
                                light_view);
    return fit;
}

/* ------------------------------------------------------------------- map ---- */

static bool depth_sampling_supported(void)
{
    return sg_query_pixelformat(SG_PIXELFORMAT_DEPTH).depth && sg_query_pixelformat(SG_PIXELFORMAT_DEPTH).sample;
}

/* The map at `size`, made on first use and again when the size changes. */
static bool ensure_map(int size)
{
    if (sk_sm.size == size && sk_sm.map.id != SG_INVALID_ID) {
        return true;
    }
    if (!depth_sampling_supported()) {
        if (!sk_sm.unsupported) {
            log_warn("shadows: this graphics backend can't sample a shadow map yet; shadows are off");
            sk_sm.unsupported = true;
        }
        return false;
    }
    sg_destroy_view(sk_sm.depth_attachment);
    sg_destroy_view(sk_sm.map_view);
    sg_destroy_image(sk_sm.map);
    sk_sm.map = sg_make_image(&(sg_image_desc){
        .usage = {.depth_stencil_attachment = true},
        .width = size,
        .height = size,
        .pixel_format = SG_PIXELFORMAT_DEPTH,
        .sample_count = 1,
        .label = "sk-shadow-map",
    });
    sk_sm.map_view = sg_make_view(&(sg_view_desc){.texture.image = sk_sm.map, .label = "sk-shadow-map-view"});
    sk_sm.depth_attachment =
        sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = sk_sm.map, .label = "sk-shadow-map-depth"});
    if (sk_sm.sampler.id == SG_INVALID_ID) {
        /* comparison sampling: reading it gives how lit the point is, filtered by the
           GPU, not the depth that's stored */
        sk_sm.sampler = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = SG_FILTER_LINEAR,
            .mag_filter = SG_FILTER_LINEAR,
            .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .label = "sk-shadow-sampler",
        });
    }
    sk_sm.size = size;
    return sg_query_image_state(sk_sm.map) == SG_RESOURCESTATE_VALID;
}

/* ------------------------------------------------------------------ pass ---- */

/* The frame's first lighting environment with a casting light, or -1. */
static int casting_env(void)
{
    for (int i = 0;; i++) {
        const sk_light_env_t *env = sk_light_env_get(i);
        if (env == NULL) {
            return -1;
        }
        if (env->shadow_light >= 0 && env->shadow_light < env->count) {
            return i;
        }
    }
}

/* sk_render_hooks: draw the casters into the map, before anything is shaded. */
static void shadows_draw(void)
{
    const int env_index = casting_env();
    const sk_light_env_t *env = env_index >= 0 ? sk_light_env_get(env_index) : NULL;
    const sk_scene_light_t *light = env != NULL ? &env->lights[env->shadow_light] : NULL;
    const bool zero_to_one = sg_query_backend() == SG_BACKEND_WGPU;
    sk_camera3d_t cam;
    sk_shadow_fit_t fit;
    vec2_t size;

    sk_sm.binding.valid = false;
    sk_sm.env = -1;
    if (light == NULL || !sk_model_has_shadow_casters(env_index)) {
        return;
    }
    if (!sk_camera3d_get_active_data(&cam) || !ensure_map(light->shadow_map_size)) {
        return;
    }
    size = sk_render_target_size();
    fit = sk_shadow_fit_directional(&cam, size.y > 0.0f ? size.x / size.y : 1.0f, light->direction,
                                    light->shadow_distance, sk_sm.size, SK_SHADOW_PULLBACK, zero_to_one);

    /* the depth buffer is the whole point here, so say it must be kept: sokol's default
       for depth is DONTCARE, which WebGPU takes at its word and discards (GL only treats
       it as a hint, which is why this once looked like a WebGPU-only problem) */
    sg_begin_pass(&(sg_pass){
        .action = {.depth = {.load_action = SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_STORE,
                             .clear_value = 1.0f},
                   .stencil.load_action = SG_LOADACTION_DONTCARE},
        .attachments = {.depth_stencil = sk_sm.depth_attachment},
        .label = "sk-shadow-map",
    });
    sk_model_draw_shadow_casters(env_index, &fit.view_proj);
    sg_end_pass();

    sk_sm.env = env_index;
    sk_sm.binding = (sk_shadow_binding_t){
        .valid = true,
        .map = sk_sm.map_view,
        .sampler = sk_sm.sampler,
        .view_proj = fit.view_proj,
        .light = env->shadow_light,
        .texel = 1.0f / (float)sk_sm.size,
        .bias_constant = light->shadow_bias_constant,
        .bias_slope = light->shadow_bias_slope,
        /* the bias is given in texels, which is what acne is made of; the shader
           works in the map's depth units, so carry the conversion with it */
        .bias_scale = fit.depth_range > 0.0f ? fit.texel_world / fit.depth_range : 0.0f,
        .strength = light->shadow_strength,
        .tint = {light->shadow_tint.x, light->shadow_tint.y, light->shadow_tint.z},
        .depth_scale = zero_to_one ? 1.0f : 0.5f, /* clip z -> the 0..1 depth stored */
        .depth_offset = zero_to_one ? 0.0f : 0.5f,
        .texel_world = fit.texel_world,
    };
}

static bool get_binding(int light_env, sk_shadow_binding_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (!sk_sm.binding.valid || light_env < 0 || light_env != sk_sm.env) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = sk_sm.binding;
    return true;
}

/* ---------------------------------------------------------- public API ---- */

SK_KEEP
bool sk_light_set_casts_shadows(sk_handle_t light, bool casts)
{
    return sk_light_shadow_set_casts(light, casts);
}

SK_KEEP
bool sk_light_get_casts_shadows(sk_handle_t light)
{
    return sk_light_shadow_casts(light);
}

SK_KEEP
bool sk_light_set_shadow_distance(sk_handle_t light, float distance)
{
    return sk_light_shadow_set_distance(light, distance);
}

SK_KEEP
bool sk_light_set_shadow_map_size(sk_handle_t light, int size)
{
    return sk_light_shadow_set_map_size(light, size);
}

SK_KEEP
bool sk_light_set_shadow_bias(sk_handle_t light, float constant, float slope)
{
    return sk_light_shadow_set_bias(light, constant, slope);
}

SK_KEEP
bool sk_light_set_shadow_strength(sk_handle_t light, float strength)
{
    return sk_light_shadow_set_strength(light, strength);
}

SK_KEEP
bool sk_light_set_shadow_color(sk_handle_t light, sk_color_t color)
{
    return sk_light_shadow_set_color(light, color);
}

void sk_shadow_init(void)
{
    memset(&sk_sm, 0, sizeof(sk_sm));
    sk_sm.env = -1;
    sk_render_hooks.shadows_draw = shadows_draw;
    sk_shadow_hooks.get_binding = get_binding;
}

void sk_shadow_deinit(void)
{
    sg_destroy_sampler(sk_sm.sampler);
    sg_destroy_view(sk_sm.depth_attachment);
    sg_destroy_view(sk_sm.map_view);
    sg_destroy_image(sk_sm.map);
    memset(&sk_sm, 0, sizeof(sk_sm));
    sk_render_hooks.shadows_draw = NULL;
    sk_shadow_hooks = (sk_shadow_hooks_t){0};
}

/* An optional subsystem: part of the runtime when a program turns shadows on
 * (internal/sk_module.h). After models, whose casters it draws. */
static sk_module_t sk_shadow_module = {.name = "shadow", .order = 52, .init = sk_shadow_init,
                                       .deinit = sk_shadow_deinit};
SK_MODULE(sk_shadow_module)
