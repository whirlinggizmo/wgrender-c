#include "wgr_sprite3d.h"

#include <math.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_sprite_batch_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_pick_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_texture_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_material_internal.h"
#include "wgr_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

/* The sprite3d pool starts at SPRITES_INITIAL slots and doubles as needed, up to
 * WGR_MAX_SPRITE3D (overridable at build time, -DWGR_MAX_SPRITE3D=...). */
#ifndef WGR_MAX_SPRITE3D
#define WGR_MAX_SPRITE3D WGRI_HANDLE_POOL_MAX_SLOTS
#endif
#define SPRITES_INITIAL 256

typedef struct {
    wgr_handle_t texture;
    vec3_t position;
    vec3_t rotation;
    vec3_t scale;
    float width, height;                                   /* world size before scale */
    float source_x, source_y, source_width, source_height; /* texture pixels; width/height <= 0: all of it */
    float pivot_x, pivot_y;                                /* 0..1 across the quad; (0.5, 0.5) is its center */
    int facing;
    wgr_color_t tint;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
    bool pick_alpha_test;
    float pick_alpha_threshold;
    wgr_alpha_mode_t alpha_mode;
    float alpha_cutoff; /* WGR_ALPHA_MASK */
    wgr_handle_t material; /* a custom material (referenced), or 0 */
} wgr_sprite3d_t;

/* Swap `*slot` for `material` (built-in or custom, or 0), keeping one reference. */
static bool assign_material(wgr_handle_t *slot, wgr_handle_t material, const char *who)
{
    const wgri_material_t *material_ptr = material != 0 ? wgri_material_get(material) : NULL;
    if (material != 0 && material_ptr == NULL) {
        log_warn("%s: needs a material (wgr_material_create or wgr_material_create_custom) or 0", who);
        return false;
    }
    if (wgri_material_is_screen(material)) {
        log_warn("%s: that material's shader is a screen effect (wgr_render_add_effect), not a surface shader", who);
        return false;
    }
    if (*slot != material) {
        wgri_material_retain(material); /* no-op for 0 */
        wgr_material_release(*slot);
        *slot = material;
    }
    return true;
}

static wgr_sprite3d_t *wgr_sprites; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_sprite_pool;

static void draw_handle(wgr_handle_t handle);
static int collect_transparent(wgr_handle_t handle, const wgri_camera3d_t *cam,
                               wgri_transparent_item_t *out, int max_items);
static void draw_transparent(wgr_handle_t handle, int part);
static bool sprite_bounds(wgr_handle_t handle, vec3_t *lmin, vec3_t *lmax, wgri_mat4_t *model);
static bool sprite_pick(wgr_handle_t handle, vec3_t origin, vec3_t dir, wgr_pick_result_t *out);
static void pivot_offset(const wgr_sprite3d_t *sprite_ptr, float *out_right, float *out_up);
static void source_uv(const wgr_sprite3d_t *sprite_ptr, int texture_width, int texture_height, float out[4]);
static void sprite_quad_corners(const wgr_sprite3d_t *sprite_ptr, const wgri_camera3d_t *cam,
                                vec3_t *tl, vec3_t *tr, vec3_t *br, vec3_t *bl);

/* ---- tiny vec3 helpers ------------------------------------------------- */
static vec3_t v3_sub(vec3_t a, vec3_t b) { return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static vec3_t v3_cross(vec3_t a, vec3_t b)
{
    return (vec3_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static vec3_t v3_norm(vec3_t a)
{
    float len = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len <= 1e-6f) return (vec3_t){0, 0, 0};
    return (vec3_t){a.x / len, a.y / len, a.z / len};
}

static wgr_sprite3d_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_sprite_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid sprite3d handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &wgr_sprites[index];
}

static wgr_handle_t create_sprite(wgr_handle_t texture)
{
    wgr_handle_t handle = wgri_handle_pool_alloc(&wgr_sprite_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("sprite3d: pool full (%u)", (unsigned)wgr_sprite_pool.max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_sprite_pool, handle, &index);
    wgr_sprites[index] = (wgr_sprite3d_t){
        .texture = texture,
        .scale = {1.0f, 1.0f, 1.0f},
        .width = 1.0f,
        .height = 1.0f,
        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
        .facing = WGR_SPRITE3D_FACING_CAMERA,
        .tint = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
        .alpha_mode = WGR_ALPHA_BLEND,
        .alpha_cutoff = 0.5f,
    };
    if (texture != 0) {
        wgri_texture_retain(texture);
    }
    return handle;
}

WGRI_KEEP
wgr_handle_t wgr_sprite3d_create(wgr_handle_t texture)
{
    return create_sprite(texture);
}

WGRI_KEEP
bool wgr_sprite3d_set_texture(wgr_handle_t handle, wgr_handle_t texture)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (sprite_ptr->texture == texture) {
        return true;
    }
    if (sprite_ptr->texture != 0) {
        wgr_texture_release(sprite_ptr->texture);
    }
    sprite_ptr->texture = texture;
    if (texture != 0) {
        wgri_texture_retain(texture);
    }
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_transform(wgr_handle_t handle,
                               float px, float py, float pz,
                               float rx, float ry, float rz,
                               float sx, float sy, float sz)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->position = (vec3_t){px, py, pz};
    sprite_ptr->rotation = (vec3_t){rx, ry, rz};
    sprite_ptr->scale = (vec3_t){sx, sy, sz};
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_size(wgr_handle_t handle, float size)
{
    return wgr_sprite3d_set_extent(handle, size, size);
}

WGRI_KEEP
bool wgr_sprite3d_set_extent(wgr_handle_t handle, float width, float height)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || width <= 0.0f || height <= 0.0f) return false;
    sprite_ptr->width = width;
    sprite_ptr->height = height;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_source(wgr_handle_t handle, float x, float y, float width, float height)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->source_x = x;
    sprite_ptr->source_y = y;
    sprite_ptr->source_width = width;
    sprite_ptr->source_height = height;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_pivot(wgr_handle_t handle, float x, float y)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->pivot_x = x;
    sprite_ptr->pivot_y = y;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_facing(wgr_handle_t handle, wgr_sprite3d_facing_t facing)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || facing < WGR_SPRITE3D_FACING_CAMERA || facing > WGR_SPRITE3D_FACING_FREE) return false;
    sprite_ptr->facing = facing;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_pickable(wgr_handle_t handle, bool pickable)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->pickable = pickable;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_is_pickable(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->pickable;
}

WGRI_KEEP
bool wgr_sprite3d_set_enabled(wgr_handle_t handle, bool enabled)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->enabled = enabled;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_is_enabled(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->enabled;
}

WGRI_KEEP
vec3_t wgr_sprite3d_get_position(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->position : (vec3_t){0, 0, 0};
}

WGRI_KEEP
vec3_t wgr_sprite3d_get_rotation(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->rotation : (vec3_t){0, 0, 0};
}

WGRI_KEEP
vec3_t wgr_sprite3d_get_scale(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->scale : (vec3_t){0, 0, 0};
}

WGRI_KEEP
bool wgr_sprite3d_set_tint(wgr_handle_t handle, wgr_color_t color)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->tint = color;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_visible(wgr_handle_t handle, bool visible)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->visible = visible;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_pick_alpha_test(wgr_handle_t handle, bool enable, float threshold)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (enable && sprite_ptr->texture != 0) {
        wgri_texture_ensure_alpha_mask(sprite_ptr->texture);
    }
    sprite_ptr->pick_alpha_test = enable;
    sprite_ptr->pick_alpha_threshold = threshold;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_is_visible(wgr_handle_t handle)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->visible;
}

static void draw_handle(wgr_handle_t handle)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    wgri_sprite_quad_t instance;
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;

    if (sprite_ptr == NULL || !sprite_ptr->visible) {
        return;
    }
    if (!wgri_texture_get_binding(sprite_ptr->texture, &view, &smp, &tw, &th)) {
        return;
    }
    instance = (wgri_sprite_quad_t){
        .position = {sprite_ptr->position.x, sprite_ptr->position.y, sprite_ptr->position.z},
        .facing = (float)sprite_ptr->facing,
        .size = {sprite_ptr->width * sprite_ptr->scale.x, sprite_ptr->height * sprite_ptr->scale.y},
        .pivot = {sprite_ptr->pivot_x, sprite_ptr->pivot_y},
        .color = {(uint8_t)wgr_color_get_red(sprite_ptr->tint), (uint8_t)wgr_color_get_green(sprite_ptr->tint),
                  (uint8_t)wgr_color_get_blue(sprite_ptr->tint), (uint8_t)wgr_color_get_alpha(sprite_ptr->tint)},
    };
    source_uv(sprite_ptr, tw, th, instance.uv);
    if (wgri_texture_is_flipped(sprite_ptr->texture)) { /* render target stored bottom-up */
        instance.uv[1] = 1.0f - instance.uv[1];
        instance.uv[3] = 1.0f - instance.uv[3];
    }
    if (sprite_ptr->facing != WGR_SPRITE3D_FACING_CAMERA && sprite_ptr->facing != WGR_SPRITE3D_FACING_CAMERA_FIXED_Y) {
        /* its own axes (the billboards' come from the camera, per batch) */
        static const wgri_camera3d_t unused_camera;
        vec3_t right, up;
        wgri_sprite3d_facing_basis((wgr_sprite3d_facing_t)sprite_ptr->facing, sprite_ptr->rotation, &unused_camera,
                                 &right, &up);
        instance.right[0] = right.x, instance.right[1] = right.y, instance.right[2] = right.z;
        instance.up[0] = up.x, instance.up[1] = up.y, instance.up[2] = up.z;
    }
    instance.alpha = sprite_ptr->alpha_mode == WGR_ALPHA_MASK     ? fmaxf(sprite_ptr->alpha_cutoff, 1e-6f)
                     : sprite_ptr->alpha_mode == WGR_ALPHA_OPAQUE ? -1.0f
                                                                 : 0.0f;
    wgri_sprite_batch_add_3d(&instance, view.id, smp.id, (wgr_alpha_mode_t)sprite_ptr->alpha_mode,
                           !wgri_render_is_3d_transparent(), sprite_ptr->material);
}

/* Scene passes: opaque and masked sprites in the opaque pass, blended ones sorted in the
 * transparent pass, additive ones after it. */
static bool in_mode(wgr_handle_t handle, bool opaque, bool blend, bool add)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || !sprite_ptr->visible || sprite_ptr->texture == 0) {
        return false;
    }
    switch (sprite_ptr->alpha_mode) {
        case WGR_ALPHA_OPAQUE:
        case WGR_ALPHA_MASK: return opaque;
        case WGR_ALPHA_ADD: return add;
        default: return blend;
    }
}

static void draw_opaque(wgr_handle_t handle)
{
    if (in_mode(handle, true, false, false)) draw_handle(handle);
}

static void draw_additive(wgr_handle_t handle)
{
    if (in_mode(handle, false, false, true)) draw_handle(handle);
}

WGRI_KEEP
bool wgr_sprite3d_set_alpha_mode(wgr_handle_t handle, wgr_alpha_mode_t mode, float cutoff)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || mode < WGR_ALPHA_OPAQUE || mode > WGR_ALPHA_ADD) {
        return false;
    }
    sprite_ptr->alpha_mode = mode;
    sprite_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff > 1.0f ? 1.0f : cutoff;
    return true;
}

WGRI_KEEP
bool wgr_sprite3d_set_material(wgr_handle_t handle, wgr_handle_t material)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && assign_material(&sprite_ptr->material, material, "wgr_sprite3d_set_material");
}

WGRI_KEEP
wgr_handle_t wgr_sprite3d_get_material(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->material : 0;
}

WGRI_KEEP
wgr_alpha_mode_t wgr_sprite3d_get_alpha_mode(wgr_handle_t handle)
{
    const wgr_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->alpha_mode : WGR_ALPHA_BLEND;
}

/* Scene: sprites are always in the transparent pass (textures usually have
 * alpha), sorted by their center. Direct wgr_sprite3d_draw() calls keep the
 * depth-writing pipeline. */
static int collect_transparent(wgr_handle_t handle, const wgri_camera3d_t *cam,
                               wgri_transparent_item_t *out, int max_items)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || !sprite_ptr->visible || sprite_ptr->texture == 0 || max_items < 1 ||
        sprite_ptr->alpha_mode != WGR_ALPHA_BLEND) {
        return 0;
    }
    out[0] = (wgri_transparent_item_t){
        .handle = handle,
        .part = 0,
        .depth = wgri_scene_view_depth(cam, sprite_ptr->position),
    };
    return 1;
}

static void draw_transparent(wgr_handle_t handle, int part)
{
    (void)part;
    draw_handle(handle);
}

WGRI_KEEP
void wgr_sprite3d_draw(wgr_handle_t handle)
{
    draw_handle(handle);
}

static bool sprite_bounds(wgr_handle_t handle, vec3_t *lmin, vec3_t *lmax, wgri_mat4_t *model)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    float hw, hh, r, ox, oy;
    if (sprite_ptr == NULL || !sprite_ptr->visible) {
        return false;
    }
    /* The billboard rotates to face the camera, so use a conservative cube that
     * encloses the quad at any orientation (radius = half-diagonal). This keeps
     * the broadphase from rejecting a glancing hit on the rotated quad. */
    hw = 0.5f * sprite_ptr->width * sprite_ptr->scale.x;
    hh = 0.5f * sprite_ptr->height * sprite_ptr->scale.y;
    pivot_offset(sprite_ptr, &ox, &oy);
    r = sqrtf(hw * hw + hh * hh) + sqrtf(ox * ox + oy * oy);
    *lmin = (vec3_t){-r, -r, -r};
    *lmax = (vec3_t){r, r, r};
    *model = wgri_mat4_translate(sprite_ptr->position.x, sprite_ptr->position.y, sprite_ptr->position.z);
    return true;
}

void wgri_sprite3d_facing_basis(wgr_sprite3d_facing_t facing, vec3_t rotation, const wgri_camera3d_t *cam,
                              vec3_t *right, vec3_t *up)
{
    if (facing == WGR_SPRITE3D_FACING_Y_UP) {
        *right = (vec3_t){1, 0, 0};
        *up = (vec3_t){0, 0, -1};
    } else if (facing == WGR_SPRITE3D_FACING_FREE) {
        /* the quad lies in the sprite's local XY plane, turned by its rotation */
        const wgri_mat4_t rot = wgri_mat4_trs((vec3_t){0, 0, 0}, rotation, (vec3_t){1, 1, 1});
        *right = (vec3_t){rot.m[0], rot.m[1], rot.m[2]};
        *up = (vec3_t){rot.m[4], rot.m[5], rot.m[6]};
    } else if (facing == WGR_SPRITE3D_FACING_CAMERA_FIXED_Y) {
        /* cylindrical: turns about world Y to face the camera and stays upright, so
           a tree doesn't lean back when the camera looks down at it */
        const vec3_t fwd = v3_norm(v3_sub(cam->target, cam->position));
        vec3_t horizontal = v3_cross(fwd, (vec3_t){0, 1, 0});
        if (horizontal.x * horizontal.x + horizontal.y * horizontal.y + horizontal.z * horizontal.z < 1e-12f) {
            horizontal = v3_cross(fwd, cam->up); /* looking straight up or down: the camera's own right */
        }
        *right = v3_norm(horizontal);
        *up = (vec3_t){0, 1, 0};
    } else {
        /* spherical: parallel to the view plane, facing the camera whatever its pitch */
        const vec3_t fwd = v3_norm(v3_sub(cam->target, cam->position));
        *right = v3_norm(v3_cross(fwd, cam->up));
        *up = v3_norm(v3_cross(*right, fwd));
    }
}

/* The sprite's region of its texture as UVs (u0, v0, u1, v1), top-down like the
 * image: the whole texture unless a source rectangle is set. */
static void source_uv(const wgr_sprite3d_t *sprite_ptr, int texture_width, int texture_height, float out[4])
{
    if (sprite_ptr->source_width <= 0.0f || sprite_ptr->source_height <= 0.0f || texture_width <= 0 ||
        texture_height <= 0) {
        out[0] = 0.0f, out[1] = 0.0f, out[2] = 1.0f, out[3] = 1.0f;
        return;
    }
    out[0] = sprite_ptr->source_x / (float)texture_width;
    out[1] = sprite_ptr->source_y / (float)texture_height;
    out[2] = (sprite_ptr->source_x + sprite_ptr->source_width) / (float)texture_width;
    out[3] = (sprite_ptr->source_y + sprite_ptr->source_height) / (float)texture_height;
}

/* How far the quad's center sits from the sprite's position, along its right and up
 * axes: 0 for the default center pivot. */
static void pivot_offset(const wgr_sprite3d_t *sprite_ptr, float *out_right, float *out_up)
{
    *out_right = (0.5f - sprite_ptr->pivot_x) * sprite_ptr->width * sprite_ptr->scale.x;
    *out_up = (sprite_ptr->pivot_y - 0.5f) * sprite_ptr->height * sprite_ptr->scale.y;
}

/* The sprite's quad for this camera; drawing and picking both use it, so the quad
 * picked is the quad on screen. */
static void sprite_quad_corners(const wgr_sprite3d_t *sprite_ptr, const wgri_camera3d_t *cam,
                                vec3_t *tl, vec3_t *tr, vec3_t *br, vec3_t *bl)
{
    vec3_t right, up, c;
    float hw, hh, ox, oy;

    wgri_sprite3d_facing_basis((wgr_sprite3d_facing_t)sprite_ptr->facing, sprite_ptr->rotation, cam, &right, &up);

    hw = 0.5f * sprite_ptr->width * sprite_ptr->scale.x;
    hh = 0.5f * sprite_ptr->height * sprite_ptr->scale.y;
    /* the pivot sits on the position, so the quad's center moves away from it
       (pivot y runs down the texture, `up` runs up in the world) */
    pivot_offset(sprite_ptr, &ox, &oy);
    c = sprite_ptr->position;
    c = (vec3_t){c.x + right.x * ox + up.x * oy, c.y + right.y * ox + up.y * oy, c.z + right.z * ox + up.z * oy};
    *tl = (vec3_t){c.x - right.x * hw + up.x * hh, c.y - right.y * hw + up.y * hh, c.z - right.z * hw + up.z * hh};
    *tr = (vec3_t){c.x + right.x * hw + up.x * hh, c.y + right.y * hw + up.y * hh, c.z + right.z * hw + up.z * hh};
    *br = (vec3_t){c.x + right.x * hw - up.x * hh, c.y + right.y * hw - up.y * hh, c.z + right.z * hw - up.z * hh};
    *bl = (vec3_t){c.x - right.x * hw - up.x * hh, c.y - right.y * hw - up.y * hh, c.z - right.z * hw - up.z * hh};
}

static bool sprite_pick(wgr_handle_t handle, vec3_t origin, vec3_t dir, wgr_pick_result_t *out)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    wgri_camera3d_t cam;
    wgri_ray_t ray;
    vec3_t tl, tr, br, bl;
    wgri_ray_hit_t h0 = {0}, h1 = {0};
    const wgri_ray_hit_t *best = NULL;
    bool got0, got1;

    if (out == NULL || sprite_ptr == NULL || !sprite_ptr->visible || !sprite_ptr->pickable) {
        return false;
    }
    if (!wgri_camera3d_get_active_data(&cam)) {
        return false;
    }

    sprite_quad_corners(sprite_ptr, &cam, &tl, &tr, &br, &bl);

    ray.origin = origin;
    ray.dir = dir;
    /* The quad is built in world space (its orientation comes from the camera),
     * so the triangle hits are world-space. A billboard has no stable local
     * frame, so local space is just the sprite's translation. */
    got0 = wgri_pick_ray_triangle(ray, tl, tr, br, &h0);
    got1 = wgri_pick_ray_triangle(ray, tl, br, bl, &h1);

    if (got0 && (!got1 || h0.t <= h1.t)) {
        best = &h0;
    } else if (got1) {
        best = &h1;
    }

    /* Optional alpha test: reject the hit if the texel under it is transparent,
     * so the ray passes through to whatever is behind the sprite. UVs follow the
     * quad layout in draw_handle(): tl=(0,0) tr=(1,0) br=(1,1) bl=(0,1). */
    if (best != NULL && sprite_ptr->pick_alpha_test) {
        float uv_x, uv_y, a, uv[4];
        sg_view view;
        sg_sampler smp;
        int tw = 0, th = 0;
        wgri_texture_get_binding(sprite_ptr->texture, &view, &smp, &tw, &th);
        source_uv(sprite_ptr, tw, th, uv);
        if (best == &h0) { /* tri (tl,tr,br): uv = (u+v, v) */
            uv_x = best->u + best->v;
            uv_y = best->v;
        } else { /* tri (tl,br,bl): uv = (u, u+v) */
            uv_x = best->u;
            uv_y = best->u + best->v;
        }
        uv_x = uv[0] + uv_x * (uv[2] - uv[0]);
        uv_y = uv[1] + uv_y * (uv[3] - uv[1]);
        if (wgri_texture_sample_alpha(sprite_ptr->texture, uv_x, uv_y, &a) &&
            a < sprite_ptr->pick_alpha_threshold) {
            best = NULL;
        }
    }

    if (best != NULL) {
        wgri_mat4_t model = wgri_mat4_translate(sprite_ptr->position.x, sprite_ptr->position.y, sprite_ptr->position.z);
        wgri_pick_result_from_world(best, ray, model, out);
    } else {
        *out = (wgr_pick_result_t){0};
    }
    return true;
}

WGRI_KEEP
void wgr_sprite3d_destroy(wgr_handle_t handle)
{
    wgr_sprite3d_t *sprite_ptr = resolve(handle);
    wgr_handle_t texture;
    if (sprite_ptr == NULL) {
        return;
    }
    wgri_scene_forget(handle);
    texture = sprite_ptr->texture;
    wgr_material_release(sprite_ptr->material); /* no-op for 0 */
    *sprite_ptr = (wgr_sprite3d_t){0};
    wgri_handle_pool_free(&wgr_sprite_pool, handle);
    if (texture != 0) {
        wgr_texture_release(texture);
    }
}

void wgri_sprite3d_init(void)
{
    wgri_sprite_batch_init(); /* shared with sprite2d: counted */
    if (!wgri_handle_pool_init(&wgr_sprite_pool, WGR_HANDLE_KIND_SPRITE3D, "sprite3d", (void **)&wgr_sprites,
                             sizeof(wgr_sprite3d_t), SPRITES_INITIAL, WGR_MAX_SPRITE3D)) {
        log_error("sprite3d: out of memory");
    }
    wgri_scene_register_passes(WGR_HANDLE_KIND_SPRITE3D, draw_opaque, collect_transparent, draw_transparent);
    wgri_scene_register_additive(WGR_HANDLE_KIND_SPRITE3D, draw_additive);
    wgri_scene_register_bounds(WGR_HANDLE_KIND_SPRITE3D, sprite_bounds);
    wgri_scene_register_pick(WGR_HANDLE_KIND_SPRITE3D, sprite_pick);
    wgri_scene_register_enabled(WGR_HANDLE_KIND_SPRITE3D, wgr_sprite3d_is_enabled);
}

void wgri_sprite3d_deinit(void)
{
    wgri_handle_pool_destroy(&wgr_sprite_pool);
    wgri_sprite_batch_deinit();
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_sprite3d_module = {.name = "sprite3d", .order = 60, .init = wgri_sprite3d_init, .deinit = wgri_sprite3d_deinit};
WGRI_MODULE(wgr_sprite3d_module)
