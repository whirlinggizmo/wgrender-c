#include "internal/wgr_shadow.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/wgr_light.h"
#include "internal/wgr_model.h"
#include "internal/wgr_module.h"
#include "internal/wgr_render.h"
#include "wgr_light.h"
#include "wgr_logger.h"

/* Shadow maps (docs/PLAN-shadows.md). Once a frame, before anything is shaded, the
 * casting light looks at the slice of the camera's view in front of it and draws the
 * casters into a depth map; the lit shaders then compare each surface against it.
 *
 * Phase 1 is one directional light: the first casting light a scene draw finds
 * (wgr_scene, wgr_light_env_t.shadow_light). The map is a depth texture sampled through
 * a comparison sampler, so the GPU does the depth test and filters the result. */

#define WGR_SHADOW_PULLBACK 50.0f /* world units the light's near plane is pulled back */

static struct {
    sg_image map;                 /* a depth array: one layer per casting light */
    sg_view map_view;
    sg_view layer_attachment[WGR_MAX_SHADOW_LIGHTS];
    sg_sampler sampler;
    int size;              /* each layer's pixels each way, 0 = none made yet */
    int layers;
    bool unsupported;      /* the backend can't sample a depth array (said once) */
    wgr_shadow_binding_t binding; /* this frame's, for the shaders */
    int env;               /* the lighting environment it was drawn for, -1 none */
} wgr_sm;

/* --------------------------------------------------------------- fitting ---- */

wgr_mat4_t wgr_shadow_ortho(float l, float r, float b, float t, float n, float f, bool zero_to_one)
{
    wgr_mat4_t m = {{0}};
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
static void view_corners(const wgr_camera3d_t *cam, float aspect, float distance, vec3_t out[8])
{
    const vec3_t forward = wgr_v3_norm(wgr_v3_sub(cam->target, cam->position));
    const vec3_t right = wgr_v3_norm(wgr_v3_cross(forward, cam->up));
    const vec3_t up = wgr_v3_cross(right, forward);
    const bool ortho = cam->projection == WGR_CAMERA3D_ORTHOGRAPHIC;
    const float near_plane = ortho ? 0.0f : WGR_CAMERA3D_PERSPECTIVE_NEAR;
    const float planes[2] = {near_plane, distance};

    for (int p = 0; p < 2; p++) {
        const float z = planes[p];
        const float half_height = ortho ? cam->ortho_height * 0.5f : tanf(cam->fov * 0.5f) * z;
        const float half_width = half_height * aspect;
        const vec3_t centre = wgr_v3_add(cam->position, wgr_v3_scale(forward, z));
        for (int c = 0; c < 4; c++) {
            const float x = (c & 1) ? half_width : -half_width;
            const float y = (c & 2) ? half_height : -half_height;
            out[p * 4 + c] = wgr_v3_add(centre, wgr_v3_add(wgr_v3_scale(right, x), wgr_v3_scale(up, y)));
        }
    }
}

wgr_shadow_fit_t wgr_shadow_fit_directional(const wgr_camera3d_t *cam, float aspect, vec3_t light_direction,
                                          float distance, int map_size, float pullback, bool zero_to_one)
{
    vec3_t corners[8];
    vec3_t centre = {0, 0, 0};
    vec3_t dir = wgr_v3_norm(light_direction);
    wgr_camera3d_t light_cam;
    wgr_mat4_t light_view;
    wgr_shadow_fit_t fit;
    float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f, min_z = 1e30f, max_z = -1e30f;

    if (!(wgr_v3_dot(dir, dir) > 0.0f)) {
        dir = (vec3_t){0.0f, -1.0f, 0.0f};
    }
    if (!(distance > 0.0f)) {
        distance = 1.0f;
    }
    if (map_size < 1) {
        map_size = 1;
    }
    view_corners(cam, aspect, distance, corners);
    for (int i = 0; i < 8; i++) centre = wgr_v3_add(centre, corners[i]);
    centre = wgr_v3_scale(centre, 1.0f / 8.0f);

    /* look from far enough back along the light that the whole slice is in front */
    light_cam.position = wgr_v3_sub(centre, wgr_v3_scale(dir, distance + pullback));
    light_cam.target = centre;
    light_cam.up = fabsf(dir.y) > 0.99f ? (vec3_t){0.0f, 0.0f, 1.0f} : (vec3_t){0.0f, 1.0f, 0.0f};
    light_cam.fov = 1.0f;
    light_cam.ortho_height = 1.0f;
    light_cam.projection = WGR_CAMERA3D_ORTHOGRAPHIC;
    light_view = wgr_camera3d_view(&light_cam);

    for (int i = 0; i < 8; i++) {
        const vec3_t p = wgr_mat4_mul_point(light_view, corners[i]);
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
    fit.view_proj = wgr_mat4_mul(wgr_shadow_ortho(min_x, max_x, min_y, max_y, 0.0f, fit.depth_range, zero_to_one),
                                light_view);
    return fit;
}

/* A perspective projection in the depth range the backend clips to, like
 * wgr_shadow_ortho but for a spot light's cone. */
static wgr_mat4_t shadow_perspective(float fovy, float aspect, float n, float f, bool zero_to_one)
{
    const float t = tanf(fovy * 0.5f);
    wgr_mat4_t m = {{0}};
    m.m[0] = 1.0f / (aspect * t);
    m.m[5] = 1.0f / t;
    m.m[11] = -1.0f;
    if (zero_to_one) { /* WebGPU clips 0 <= z <= w */
        m.m[10] = f / (n - f);
        m.m[14] = (f * n) / (n - f);
    } else { /* GL and WebGL2 clip -w <= z <= w */
        m.m[10] = (f + n) / (n - f);
        m.m[14] = (2.0f * f * n) / (n - f);
    }
    return m;
}

wgr_shadow_fit_t wgr_shadow_fit_spot(vec3_t position, vec3_t direction, float cos_outer, float distance,
                                   float near_plane, int map_size, bool zero_to_one)
{
    vec3_t dir = wgr_v3_norm(direction);
    wgr_camera3d_t light_cam;
    wgr_shadow_fit_t fit;
    float fovy;

    if (!(wgr_v3_dot(dir, dir) > 0.0f)) {
        dir = (vec3_t){0.0f, -1.0f, 0.0f};
    }
    if (!(distance > 0.0f)) {
        distance = 1.0f;
    }
    if (!(near_plane > 0.0f) || near_plane >= distance) {
        near_plane = distance * 0.01f;
    }
    if (map_size < 1) {
        map_size = 1;
    }
    /* the whole cone has to fit, with a little margin so its edge isn't on the very
       last texel; a cone at or past a right angle is clamped to something projectable */
    fovy = 2.0f * acosf(cos_outer < -0.99f ? -0.99f : (cos_outer > 1.0f ? 1.0f : cos_outer)) * 1.1f;
    if (fovy > 2.8f) {
        fovy = 2.8f; /* about 160 degrees */
    }
    if (fovy < 0.02f) {
        fovy = 0.02f;
    }
    light_cam.position = position;
    light_cam.target = wgr_v3_add(position, dir);
    light_cam.up = fabsf(dir.y) > 0.99f ? (vec3_t){0.0f, 0.0f, 1.0f} : (vec3_t){0.0f, 1.0f, 0.0f};
    light_cam.fov = fovy;
    light_cam.ortho_height = 1.0f;
    light_cam.projection = WGR_CAMERA3D_PERSPECTIVE;

    fit.depth_range = distance;
    /* a spot's texels grow with distance; this is the size at the far end, which is
       where its shadow usually lands */
    fit.texel_world = 2.0f * tanf(fovy * 0.5f) * distance / (float)map_size;
    fit.view_proj = wgr_mat4_mul(shadow_perspective(fovy, 1.0f, near_plane, distance, zero_to_one),
                                wgr_camera3d_view(&light_cam));
    return fit;
}

/* ------------------------------------------------------------------- map ---- */

static bool depth_sampling_supported(void)
{
    const sg_pixelformat_info info = sg_query_pixelformat(SG_PIXELFORMAT_DEPTH);
    return info.depth && info.sample;
}

/* The array at `size` with `layers` layers, made on first use and again when either
 * changes. Layers share one size: that's what a texture array is. */
static bool ensure_map(int size, int layers)
{
    if (wgr_sm.size == size && wgr_sm.layers == layers && wgr_sm.map.id != SG_INVALID_ID) {
        return true;
    }
    if (!depth_sampling_supported()) {
        if (!wgr_sm.unsupported) {
            log_warn("shadows: this graphics backend can't sample a shadow map yet; shadows are off");
            wgr_sm.unsupported = true;
        }
        return false;
    }
    for (int i = 0; i < WGR_MAX_SHADOW_LIGHTS; i++) {
        sg_destroy_view(wgr_sm.layer_attachment[i]);
        wgr_sm.layer_attachment[i] = (sg_view){0};
    }
    sg_destroy_view(wgr_sm.map_view);
    sg_destroy_image(wgr_sm.map);
    wgr_sm.map = sg_make_image(&(sg_image_desc){
        .type = SG_IMAGETYPE_ARRAY,
        .usage = {.depth_stencil_attachment = true},
        .width = size,
        .height = size,
        .num_slices = layers,
        .pixel_format = SG_PIXELFORMAT_DEPTH,
        .sample_count = 1,
        .label = "wgr-shadow-map",
    });
    wgr_sm.map_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sm.map, .label = "wgr-shadow-map-view"});
    for (int i = 0; i < layers; i++) { /* one attachment per layer: a pass writes one */
        wgr_sm.layer_attachment[i] = sg_make_view(&(sg_view_desc){
            .depth_stencil_attachment = {.image = wgr_sm.map, .slice = i}, .label = "wgr-shadow-map-layer"});
    }
    if (wgr_sm.sampler.id == SG_INVALID_ID) {
        /* comparison sampling: reading it gives how lit the point is, filtered by the
           GPU, not the depth that's stored */
        wgr_sm.sampler = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = SG_FILTER_LINEAR,
            .mag_filter = SG_FILTER_LINEAR,
            .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .label = "wgr-shadow-sampler",
        });
    }
    wgr_sm.size = size;
    wgr_sm.layers = layers;
    return sg_query_image_state(wgr_sm.map) == SG_RESOURCESTATE_VALID;
}

/* ------------------------------------------------------------------ pass ---- */

/* The frame's first lighting environment with a casting light, or -1. */
static int casting_env(void)
{
    for (int i = 0;; i++) {
        const wgr_light_env_t *env = wgr_light_env_get(i);
        if (env == NULL) {
            return -1;
        }
        if (env->shadow_count > 0) {
            return i;
        }
    }
}

/* Where one casting light looks, and how big its texels are there. */
static wgr_shadow_fit_t fit_light(const wgr_scene_light_t *light, const wgr_camera3d_t *cam, float aspect,
                                 bool zero_to_one)
{
    if (light->type == WGR_LIGHT_SPOT) {
        /* a spot only lights its own cone, and only as far as its range reaches */
        const float reach = light->range > 0.0f && light->range < light->shadow_distance ? light->range
                                                                                         : light->shadow_distance;
        return wgr_shadow_fit_spot(light->position, light->direction, light->cos_outer, reach, reach * 0.01f,
                                  wgr_sm.size, zero_to_one);
    }
    return wgr_shadow_fit_directional(cam, aspect, light->direction, light->shadow_distance, wgr_sm.size,
                                     WGR_SHADOW_PULLBACK, zero_to_one);
}

/* wgr_render_hooks: draw the casters into each casting light's layer, before anything
 * is shaded. */
static void shadows_draw(void)
{
    const int env_index = casting_env();
    const wgr_light_env_t *env = env_index >= 0 ? wgr_light_env_get(env_index) : NULL;
    const bool zero_to_one = sg_query_backend() == SG_BACKEND_WGPU;
    wgr_camera3d_t cam;
    vec2_t screen;
    float aspect;
    int wanted_size = 0;

    wgr_sm.binding = (wgr_shadow_binding_t){0};
    wgr_sm.env = -1;
    if (env == NULL || !wgr_model_has_shadow_casters(env_index)) {
        return;
    }
    /* a map nobody samples is a pass for nothing: models can turn receiving off, and
       lit sprites receive without ever casting */
    if (!wgr_model_has_shadow_receivers(env_index) &&
        (wgr_render_hooks.sprites_lit_in == NULL || !wgr_render_hooks.sprites_lit_in(env_index))) {
        return;
    }
    if (!wgr_camera3d_get_active_data(&cam)) {
        return;
    }
    /* the layers of an array share one size, so the largest map anyone asked for wins */
    for (int i = 0; i < env->shadow_count; i++) {
        const int size = env->lights[env->shadow_lights[i]].shadow_map_size;
        if (size > wanted_size) wanted_size = size;
    }
    if (!ensure_map(wanted_size, env->shadow_count)) {
        return;
    }
    screen = wgr_render_target_size();
    aspect = screen.y > 0.0f ? screen.x / screen.y : 1.0f;

    wgr_sm.binding.valid = true;
    wgr_sm.binding.map = wgr_sm.map_view;
    wgr_sm.binding.sampler = wgr_sm.sampler;
    wgr_sm.binding.count = env->shadow_count;
    wgr_sm.binding.depth_scale = zero_to_one ? 1.0f : 0.5f; /* clip z -> the 0..1 depth stored */
    wgr_sm.binding.depth_offset = zero_to_one ? 0.0f : 0.5f;
    wgr_sm.binding.flipped = sg_query_features().origin_top_left;

    for (int i = 0; i < env->shadow_count; i++) {
        const wgr_scene_light_t *light = &env->lights[env->shadow_lights[i]];
        const wgr_shadow_fit_t fit = fit_light(light, &cam, aspect, zero_to_one);

        /* the depth buffer is the whole point here, so say it must be kept: sokol's
           default for depth is DONTCARE, which WebGPU takes at its word and discards
           (GL only treats it as a hint, which is why this once looked like a
           WebGPU-only problem) */
        sg_begin_pass(&(sg_pass){
            .action = {.depth = {.load_action = SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_STORE,
                                 .clear_value = 1.0f},
                       .stencil.load_action = SG_LOADACTION_DONTCARE},
            .attachments = {.depth_stencil = wgr_sm.layer_attachment[i]},
            .label = "wgr-shadow-map",
        });
        wgr_model_draw_shadow_casters(env_index, &fit.view_proj);
        sg_end_pass();

        wgr_sm.binding.lights[i] = (wgr_shadow_light_t){
            .light = env->shadow_lights[i],
            .view_proj = fit.view_proj,
            .texel = 1.0f / (float)wgr_sm.size,
            .bias_constant = light->shadow_bias_constant,
            .bias_slope = light->shadow_bias_slope,
            /* the bias is given in texels, which is what acne is made of; the shader
               works in the map's depth units, so carry the conversion with it */
            .bias_scale = fit.depth_range > 0.0f ? fit.texel_world / fit.depth_range : 0.0f,
            .texel_world = fit.texel_world,
            .strength = light->shadow_strength,
            .tint = {light->shadow_tint.x, light->shadow_tint.y, light->shadow_tint.z},
        };
    }
    wgr_sm.env = env_index;
}

static bool get_binding(int light_env, wgr_shadow_binding_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (!wgr_sm.binding.valid || light_env < 0 || light_env != wgr_sm.env) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = wgr_sm.binding;
    return true;
}

/* ---------------------------------------------------------- public API ---- */

WGR_KEEP
bool wgr_light_set_casts_shadows(wgr_handle_t light, bool casts)
{
    return wgr_light_shadow_set_casts(light, casts);
}

WGR_KEEP
bool wgr_light_get_casts_shadows(wgr_handle_t light)
{
    return wgr_light_shadow_casts(light);
}

WGR_KEEP
bool wgr_light_set_shadow_distance(wgr_handle_t light, float distance)
{
    return wgr_light_shadow_set_distance(light, distance);
}

WGR_KEEP
bool wgr_light_set_shadow_map_size(wgr_handle_t light, int size)
{
    return wgr_light_shadow_set_map_size(light, size);
}

WGR_KEEP
bool wgr_light_set_shadow_bias(wgr_handle_t light, float constant, float slope)
{
    return wgr_light_shadow_set_bias(light, constant, slope);
}

WGR_KEEP
bool wgr_light_set_shadow_strength(wgr_handle_t light, float strength)
{
    return wgr_light_shadow_set_strength(light, strength);
}

WGR_KEEP
bool wgr_light_set_shadow_color(wgr_handle_t light, wgr_color_t color)
{
    return wgr_light_shadow_set_color(light, color);
}

void wgr_shadow_init(void)
{
    memset(&wgr_sm, 0, sizeof(wgr_sm));
    wgr_sm.env = -1;
    wgr_render_hooks.shadows_draw = shadows_draw;
    wgr_shadow_hooks.get_binding = get_binding;
}

void wgr_shadow_deinit(void)
{
    sg_destroy_sampler(wgr_sm.sampler);
    for (int i = 0; i < WGR_MAX_SHADOW_LIGHTS; i++) sg_destroy_view(wgr_sm.layer_attachment[i]);
    sg_destroy_view(wgr_sm.map_view);
    sg_destroy_image(wgr_sm.map);
    memset(&wgr_sm, 0, sizeof(wgr_sm));
    wgr_render_hooks.shadows_draw = NULL;
    wgr_shadow_hooks = (wgr_shadow_hooks_t){0};
}

/* An optional subsystem: part of the runtime when a program turns shadows on
 * (internal/wgr_module.h). After models, whose casters it draws. */
static wgr_module_t wgr_shadow_module = {.name = "shadow", .order = 52, .init = wgr_shadow_init,
                                       .deinit = wgr_shadow_deinit};
WGR_MODULE(wgr_shadow_module)
